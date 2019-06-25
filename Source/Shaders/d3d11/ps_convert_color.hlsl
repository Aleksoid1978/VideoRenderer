#ifndef C_CSP
    #define C_CSP 1
#endif

#ifndef C_YUY2
    #define C_YUY2 0
#endif

#ifndef C_HDR
    #define C_HDR 0
#endif

Texture2D tex : register(t0);
SamplerState samp : register(s0);

#if C_CSP
cbuffer PS_COLOR_TRANSFORM : register(b0)
{
    float3 cm_r;
    float3 cm_g;
    float3 cm_b;
    float3 cm_c;
    // NB: sizeof(float3) == sizeof(float4)
};
#endif

#if (C_HDR == 1)
#include "../convert/correct_st2084.hlsl"
#elif (C_HDR == 2)
#include "../convert/correct_hlg.hlsl"
#endif

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 color = tex.Sample(samp, input.Tex); // original pixel
#if C_YUY2
    if (fmod(input.Tex.x, 2) < 1.0) {
        color = float4(color[2], color[1], color[3], 0);
    } else {
        color = float4(color[0], color[1], color[3], 0);
    }
#endif

#if C_CSP
    color.rgb = float3(mul(cm_r, color.rgb), mul(cm_g, color.rgb), mul(cm_b, color.rgb)) + cm_c;
#endif

#if (C_HDR == 1)
    color = correct_ST2084(color);
#elif (C_HDR == 2)
    color = correct_HLG(color);
#endif

    return color;
}
