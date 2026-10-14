// Per-frame HDR peak for Direct3D 9, the repeated reduction.
//
// 16 point taps over the block this output texel stands for, worked out from VPOS and the step,
// since the step stops being a whole number once the sizes no longer divide evenly.  The taps are
// exact texels rather than the bilinear pairs hdr_peak_first.hlsl uses: these values are already
// maxima, and averaging maxima before taking their max would dilute the peak once per level.
sampler s0 : register(s0);
float4 block : register(c0);     // origin x, y and step x, y, in source texels
float4 texelsize : register(c1); // 1/width, 1/height of the source

float4 main(float2 tex : TEXCOORD0, float2 vpos : VPOS) : COLOR
{
    const float2 base = block.xy + floor(vpos) * block.zw;
    const float2 q = block.zw * 0.25f;

    float peak = 0.0f;
    float sum = 0.0f;

    [unroll] for (int y = 0; y < 4; y++) {
        [unroll] for (int x = 0; x < 4; x++) {
            const float2 uv = (base + float2((x + 0.5f) * q.x, (y + 0.5f) * q.y)) * texelsize.xy;
            const float2 v = tex2D(s0, uv).xy;
            peak = max(peak, v.x);
            sum += v.y;
        }
    }

    return float4(peak, sum * (1.0f / 16.0f), 0.0f, 0.0f);
}
