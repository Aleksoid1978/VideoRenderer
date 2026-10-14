// Per-frame HDR peak measurement, pass 2 of 2.
//
// Reads the histogram written by cs_hdr_hist.hlsl, derives the frame's peak and average
// brightness, smooths them over time and stores the result for the tone mapping pass.
// It also clears the histogram, ready for the next frame.
//
// Everything stays on the GPU, so the tone mapping of a frame uses the measurement of
// that same frame: there is no read-back and no latency.
//
// With windowTime > 0 the peak is the mean, in nits, of the frame peaks of the last windowTime
// seconds.  A highlight that appears is followed by a straight ramp of that length in either
// direction, and a single bright frame moves the result by one part in the number of frames.
// The window starts afresh at a scene change, which is taken from the mean brightness of the
// whole picture and not from the peak: a bright object entering a scene is not a cut.
//
// With windowTime == 0 the peak rises at once and relaxes slowly.  That is safe for the models
// that use it, BT.2390 and Mobius: below their knee nothing moves whatever the peak does.
//
// The smoothed average and minimum are only reported.
//
// state[0] = { peak (PQ), smoothed average (PQ), initialized, smoothed min (PQ) }
// state[1] = { raw peak (nits), raw average (nits), peak (nits, floored), smoothed average (nits) }
// state[2] = { raw min (nits), smoothed min (nits), 0, 0 }
// state[3] = { pending cut (PQ), pending count, 0, 0 }
// state[4] = { window write position, frames in the window, smoothed mean of PQ, 0 }
// state[5 ...] = the window, one frame peak in nits per element (x)

#include "../convert/st2084.hlsl"

#define HIST_BINS 1024
#define GROUP_SIZE 256
#define BINS_PER_THREAD (HIST_BINS / GROUP_SIZE)
#define STATE_HEAD 5
#define WINDOW_SIZE 512 // must match kHdrWindowFrames in DX11VideoProcessor.cpp

RWStructuredBuffer<uint> hist : register(u0);
RWStructuredBuffer<float4> state : register(u1);

cbuffer ResolveConstants : register(b0)
{
	float frameTime;     // seconds the frame is displayed for, drives the release
	float releaseTime;   // seconds for the peak to relax towards a lower measurement
	float sceneCutPQ;    // a change larger than this (in PQ) is taken as a scene change
	float peakFraction;  // share of pixels allowed to be brighter than the reported peak
	float windowTime;    // seconds of frame peaks that are averaged, 0 for the fast and slow peaks
	float peakFloor;     // nits, the smoothed peak is never reported lower than this
	float2 padding;
};

groupshared uint gsCount[GROUP_SIZE];
groupshared float gsWeighted[GROUP_SIZE];
groupshared float gsWeightedPQ[GROUP_SIZE];
groupshared uint gsBin[HIST_BINS];

[numthreads(GROUP_SIZE, 1, 1)]
void main(uint tid : SV_GroupIndex)
{
	uint total = 0;
	float weighted = 0.0f;
	float weightedPQ = 0.0f;

	for (uint k = 0; k < BINS_PER_THREAD; k++) {
		const uint bin = tid * BINS_PER_THREAD + k;
		const uint n = hist[bin];
		hist[bin] = 0; // ready for the next frame
		gsBin[bin] = n;
		total += n;
		// mean of max(R,G,B) in nits, taken over the bin centers
		weighted += n * ST2084ToLinear(bin / (float)(HIST_BINS - 1), 10000.0f).x;
		weightedPQ += n * (bin / (float)(HIST_BINS - 1));
	}
	gsCount[tid] = total;
	gsWeighted[tid] = weighted;
	gsWeightedPQ[tid] = weightedPQ;
	GroupMemoryBarrierWithGroupSync();

	if (tid != 0) {
		return;
	}

	uint pixels = 0;
	float weightedSum = 0.0f;
	float weightedSumPQ = 0.0f;
	for (uint t = 0; t < GROUP_SIZE; t++) {
		pixels += gsCount[t];
		weightedSum += gsWeighted[t];
		weightedSumPQ += gsWeightedPQ[t];
	}
	if (pixels == 0) {
		return; // nothing measured, keep the previous state
	}

	// The peak is the brightest value that is not just a handful of stray pixels: walk down
	// from the top until more than peakFraction of the pixels have been passed.
	const float allowed = pixels * peakFraction;
	float above = 0.0f;
	int peakBin = 0;
	for (int b = HIST_BINS - 1; b >= 0; b--) {
		above += gsBin[b];
		if (above > allowed) {
			peakBin = b;
			break;
		}
	}
	// upper edge of the bin, so that the pixels inside it are not clipped
	const float peakPQ = min((peakBin + 0.5f) / (HIST_BINS - 1), 1.0f);
	const float peakNits = ST2084ToLinear(peakPQ, 10000.0f).x;

	// The same from the bottom, for the darkest value that is not just stray pixels
	float below = 0.0f;
	int minBin = HIST_BINS - 1;
	for (int c = 0; c < HIST_BINS; c++) {
		below += gsBin[c];
		if (below > allowed) {
			minBin = c;
			break;
		}
	}
	const float minPQ = max((minBin - 0.5f) / (HIST_BINS - 1), 0.0f);
	const float minNits = ST2084ToLinear(minPQ, 10000.0f).x;
	const float avgNits = weightedSum / pixels;
	const float avgPQ = LinearToST2084(avgNits, 10000.0f).x;
	const float meanPQ = weightedSumPQ / pixels;

	float4 s = state[0];
	float4 cut = state[3];
	float4 win = state[STATE_HEAD - 1];
	const bool first = s.z < 0.5f;
	const float a = 1.0f - exp(-frameTime / max(releaseTime, 0.001f));

	if (windowTime > 0.0f) {
		// A jump of the picture's mean brightness is a candidate cut; it is only followed if
		// the next frame agrees with it, so that a flash does not empty the window.
		const bool jump = !first && abs(meanPQ - win.z) > sceneCutPQ;
		const bool confirmed = jump && cut.y > 0.5f && abs(meanPQ - cut.x) < sceneCutPQ;
		if (first || confirmed) {
			s = float4(peakPQ, avgPQ, 1.0f, minPQ);
			win = float4(0.0f, 0.0f, meanPQ, 0.0f);
			cut = (float4)0;
		} else {
			s.y += (avgPQ - s.y) * a;
			s.w += (minPQ - s.w) * a;
			cut = jump ? float4(meanPQ, 1.0f, 0.0f, 0.0f) : (float4)0;
			if (!jump) {
				win.z += (meanPQ - win.z) * a;
			}
		}

		uint pos = (uint)win.x % WINDOW_SIZE;
		state[STATE_HEAD + pos].x = peakNits;
		pos = (pos + 1) % WINDOW_SIZE;
		const uint wanted = clamp((uint)(windowTime / frameTime + 0.5f), 1, WINDOW_SIZE);
		const uint filled = min((uint)win.y + 1, WINDOW_SIZE);
		const uint count = min(filled, wanted);
		float sum = 0.0f;
		for (uint i = 1; i <= count; i++) {
			sum += state[STATE_HEAD + (pos + WINDOW_SIZE - i) % WINDOW_SIZE].x;
		}
		win.x = pos;
		win.y = filled;

		s.x = LinearToST2084(sum / count, 10000.0f).x;
	} else if (first) {
		s = float4(peakPQ, avgPQ, 1.0f, minPQ);
		cut = (float4)0;
	} else {
		// The fast peak rises at once, so highlights are never clipped by a stale measurement,
		// and relaxes slowly unless the fall is large enough to be a scene change.
		const float dPeak = peakPQ - s.x;
		s.x = (dPeak > 0.0f || dPeak < -sceneCutPQ) ? peakPQ : s.x + dPeak * a;
		// The slow peak, the average and the minimum relax in both directions.  A jump is a
		// candidate cut; it is only followed if the next frame agrees with it.
		const bool jump = abs(avgPQ - s.y) > sceneCutPQ;
		const bool confirmed = jump && cut.y > 0.5f && abs(avgPQ - cut.x) < sceneCutPQ;
		if (confirmed) {
			s.y = avgPQ;
			s.w = minPQ;
			cut = (float4)0;
		} else {
			s.y += (avgPQ - s.y) * a;
			s.w += (minPQ - s.w) * a;
			cut = jump ? float4(avgPQ, 1.0f, 0.0f, 0.0f) : (float4)0;
		}
	}

	state[0] = s;
	// The floor is applied to what the tone mapping reads, not to the state that is smoothed,
	// so it cannot look like a scene change or hold the peak up once the content is brighter.
	state[1] = float4(peakNits, avgNits,
					  max(ST2084ToLinear(s.x, 10000.0f).x, peakFloor),
					  ST2084ToLinear(s.y, 10000.0f).x);
	state[2] = float4(minNits, ST2084ToLinear(s.w, 10000.0f).x, 0.0f, 0.0f);
	state[3] = cut;
	state[STATE_HEAD - 1] = win;
}
