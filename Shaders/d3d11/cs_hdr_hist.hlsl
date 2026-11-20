// Per-frame HDR peak measurement, pass 1 of 2.
//
// Builds a histogram of max(R, G, B) of the PQ-encoded frame. PQ is monotonic, so the
// histogram is taken directly on the encoded values and no EOTF is needed here: bin i
// stands for the PQ value i / (HIST_BINS - 1). That also makes the bins perceptually
// uniform, so the resolution is about the same everywhere in the range.
// max(R, G, B) is the quantity MaxCLL and MaxFALL are defined on (CTA-861.3).
//
// SKIV measures linear BT.2020 luminance instead, binned on the same PQ scale.
#include "../convert/conv_matrix.hlsl"
#include "../convert/st2084.hlsl"

static const float4x4 fixBt2020 = mul(ycbcr2020nc_rgb, rgb_ycbcr709);

// Pass 2 (cs_hdr_resolve.hlsl) turns the histogram into a peak and an average.

#define HIST_BINS 1024
#define GROUP_SIZE 16 // 16x16 threads, each covering 2x2 pixels = 32x32 pixels per group

Texture2D<float4> tex : register(t0);
RWStructuredBuffer<uint> hist : register(u0);

cbuffer MeasureConstants : register(b0)
{
	uint2 rectOrigin; // top-left corner of the video rectangle inside the texture
	uint2 rectSize;
	uint measureLuminance;
	uint fixVpColors;
	uint2 padding;
};

groupshared uint gsHist[HIST_BINS];

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
	for (uint i = groupIndex; i < HIST_BINS; i += GROUP_SIZE * GROUP_SIZE) {
		gsHist[i] = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	const uint2 origin = groupId.xy * (GROUP_SIZE * 2) + threadId.xy * 2;

	[unroll]
	for (uint dy = 0; dy < 2; dy++) {
		[unroll]
		for (uint dx = 0; dx < 2; dx++) {
			const uint2 p = origin + uint2(dx, dy);
			if (p.x < rectSize.x && p.y < rectSize.y) {
				const float4 source = tex.Load(int3(rectOrigin + p, 0));
				const float3 c = saturate(source.rgb);
				float m = max(c.r, max(c.g, c.b));
				if (measureLuminance) {
					const float4 pqRgb = fixVpColors ? mul(fixBt2020, source) : source;
					const float3 linearRgb = ST2084ToLinear(saturate(pqRgb), 1.0f).rgb;
					const float y = dot(linearRgb, float3(0.2627002f, 0.6779981f, 0.0593017f));
					m = LinearToST2084(y, 1.0f).x;
				}
				const uint bin = min((uint)(m * (HIST_BINS - 1) + 0.5f), HIST_BINS - 1);
				InterlockedAdd(gsHist[bin], 1);
			}
		}
	}
	GroupMemoryBarrierWithGroupSync();

	for (uint j = groupIndex; j < HIST_BINS; j += GROUP_SIZE * GROUP_SIZE) {
		if (gsHist[j] != 0) {
			InterlockedAdd(hist[j], gsHist[j]);
		}
	}
}
