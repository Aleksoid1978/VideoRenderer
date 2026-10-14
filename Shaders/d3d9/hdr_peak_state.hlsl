// Per-frame HDR peak for Direct3D 9, the last pass: what the frame measured, smoothed over time.
//
// cs_hdr_resolve.hlsl keeps a ring of the last 512 frame peaks and reports their mean, which
// needs a buffer this path does not have.  Two first order filters in series stand in for that
// mean: with the time constant at a quarter of the window, a step reaches half way at 0.42 of
// the window against the mean's 0.50, and 90% at 0.97 against 0.90.
//
// A scene change is judged on the mean of the picture rather than the peak, so a bright object
// entering a scene is not a cut, and it is only followed if the next frame agrees with it, so a
// one frame flash does not restart the window.  Both of those match Direct3D 11.
//
// The target is 2x1:
//   texel 0 = { peak (PQ), mean of the picture (PQ), first filter (PQ), pending cut (PQ, -1 none) }
//   texel 1 = { peak (nits, floored), 0, 0, 0 }  <- the only one the conversion reads
sampler sReduced : register(s0); // the 1x1 end of the reduction
sampler sPrev    : register(s1); // this target as it was for the previous frame

float4 timing : register(c0); // frameTime, releaseTime, windowTime, sceneCutPQ
float4 limits : register(c1); // peakFloor nits, 0, 0, 0

#include "../convert/st2084.hlsl"

#define frameTime   timing[0]
#define releaseTime timing[1]
#define windowTime  timing[2]
#define sceneCutPQ  timing[3]
#define peakFloor   limits[0]

float4 main(float2 tex : TEXCOORD0, float2 vpos : VPOS) : COLOR
{
    const float4 m = tex2D(sReduced, float2(0.5f, 0.5f));
    const float peakPQ = m.x;
    const float meanPQ = m.y;

    float4 prev = tex2D(sPrev, float2(0.25f, 0.5f));
    const bool first = prev.x <= 0.0f;

    // smoothing per frame: a for the release, k for the window
    const float a = 1.0f - exp(-frameTime / max(releaseTime, 0.001f));
    const float k = 1.0f - exp(-frameTime / max(windowTime * 0.25f, 0.001f));

    float peak = prev.x;
    float mean = prev.y;
    float stage1 = prev.z;
    float pending = prev.w;

    if (first) {
        peak = peakPQ;
        mean = meanPQ;
        stage1 = peakPQ;
        pending = -1.0f;
    }
    else if (windowTime <= 0.0f) {
        // no window: rise at once so a highlight is never clipped by a stale measurement, and
        // relax slowly, which is what the models that use it are safe with
        const float d = peakPQ - peak;
        peak = (d > 0.0f || d < -sceneCutPQ) ? peakPQ : peak + d * a;
        stage1 = peak;
        mean += (meanPQ - mean) * a;
        pending = -1.0f;
    }
    else {
        const bool jump = abs(meanPQ - mean) > sceneCutPQ;
        const bool confirmed = jump && pending >= 0.0f && abs(meanPQ - pending) < sceneCutPQ;
        if (confirmed) {
            peak = peakPQ;
            stage1 = peakPQ;
            mean = meanPQ;
            pending = -1.0f;
        } else {
            stage1 += (peakPQ - stage1) * k;
            peak += (stage1 - peak) * k;
            pending = jump ? meanPQ : -1.0f;
            if (!jump) {
                mean += (meanPQ - mean) * a;
            }
        }
    }

    // the floor is applied to what the conversion reads, not to what is smoothed, so it cannot
    // look like a scene change or hold the peak up once the content is brighter
    if (floor(vpos.x) > 0.5f) {
        return float4(max(ST2084ToLinear(float4(peak, 0.0f, 0.0f, 0.0f), 10000.0f).x, peakFloor), 0.0f, 0.0f, 0.0f);
    }
    return float4(peak, mean, stage1, pending);
}
