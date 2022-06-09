Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

#pragma warning(disable: 3571) // fix warning X3571 in pow().

// https://github.com/thexai/xbmc/blob/master/system/shaders/guishader_common.hlsl
inline float3 transferPQ(float3 x)
{
    static const float ST2084_m1 = 2610.0f / (4096.0f * 4.0f);
    static const float ST2084_m2 = (2523.0f / 4096.0f) * 128.0f;
    static const float ST2084_c1 = 3424.0f / 4096.0f;
    static const float ST2084_c2 = (2413.0f / 4096.0f) * 32.0f;
    static const float ST2084_c3 = (2392.0f / 4096.0f) * 32.0f;
    static const float SDR_peak_lum = 100.0f;
    static const float3x3 matx = {
        0.627402, 0.329292, 0.043306,
        0.069095, 0.919544, 0.011360,
        0.016394, 0.088028, 0.895578
    };
    // REC.709 to linear
    x = pow(x, 1.0f / 0.45f);
    // REC.709 to BT.2020
    x = mul(matx, x);
    // linear to PQ
    x = pow(x / SDR_peak_lum, ST2084_m1);
    x = (ST2084_c1 + ST2084_c2 * x) / (1.0f + ST2084_c3 * x);
    x = pow(x, ST2084_m2);
    return x;
}

float4 main(PS_INPUT input) : SV_Target
{
    float4 color = tex.Sample(samp, input.Tex); // original pixel

    return float4(transferPQ(color.rgb), color.a);
}
