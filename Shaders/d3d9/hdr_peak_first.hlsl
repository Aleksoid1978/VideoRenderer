// Per-frame HDR peak for Direct3D 9, pass 1 of the reduction.
//
// Direct3D 9 has no compute shader, so the peak cannot be taken from a histogram the way
// cs_hdr_hist.hlsl does it.  Instead the frame is reduced down to one texel over several passes,
// and this first one takes the MAXIMUM of values that are themselves 2x2 AVERAGES.  The averaging
// is what the histogram's peakFraction buys on Direct3D 11: a single stray bright texel cannot set
// the peak for the whole frame, because it is averaged with its neighbours first.
//
// The block each output texel stands for is worked out from VPOS and the step given as a constant,
// rather than from the interpolated texture coordinate.  The steps are not whole numbers once the
// sizes stop dividing evenly, and anything that assumes they are reads the wrong texels.
//
//   x = peak, as max(R,G,B) in PQ
//   y = mean of the picture in PQ, which is what a scene change is judged on
sampler s0 : register(s0);
float4 block : register(c0);     // origin x, y and step x, y, in source texels
float4 texelsize : register(c1); // 1/width, 1/height of the source

#include "../convert/conv_matrix.hlsl"

// The DXVA2 VP cannot convert YCbCr BT.2020, so what it hands back has to be put right before it
// means anything.  fixconvert_pq_to_sdr.hlsl does this to the picture; the measurement has to do
// it too, or it reads a limited range signal as if it were full range and under-reports the peak.
static const float4x4 fix_bt2020_matrix = mul(ycbcr2020nc_rgb, rgb_ycbcr709);

float4 main(float2 tex : TEXCOORD0, float2 vpos : VPOS) : COLOR
{
    const float2 base = block.xy + floor(vpos) * block.zw;
    const float2 q = block.zw * 0.25f;

    // on a texel boundary bilinear returns the average of the two texels either side, so each of
    // these four taps is the average of the 2x2 under it
    float4 a = mul(fix_bt2020_matrix, tex2D(s0, (base + q) * texelsize.xy));
    float4 b = mul(fix_bt2020_matrix, tex2D(s0, (base + float2(q.x * 3.0f, q.y)) * texelsize.xy));
    float4 c = mul(fix_bt2020_matrix, tex2D(s0, (base + float2(q.x, q.y * 3.0f)) * texelsize.xy));
    float4 e = mul(fix_bt2020_matrix, tex2D(s0, (base + q * 3.0f) * texelsize.xy));

    // the frame arrives PQ encoded, so max(R,G,B) is already the value the curve is built on
    float4 v = float4(max(a.r, max(a.g, a.b)),
                      max(b.r, max(b.g, b.b)),
                      max(c.r, max(c.g, c.b)),
                      max(e.r, max(e.g, e.b)));

    return float4(max(max(v.x, v.y), max(v.z, v.w)), dot(v, 0.25f), 0.0f, 0.0f);
}
