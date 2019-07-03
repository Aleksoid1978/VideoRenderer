#ifndef C_HDR
    #define C_HDR 0
#endif

Texture2D texY : register(t0);
Texture2D texUV : register(t1);
SamplerState samp : register(s0);
SamplerState sampL : register(s1);

cbuffer PS_COLOR_TRANSFORM : register(b0)
{
    float3 cm_r;
    float3 cm_g;
    float3 cm_b;
    float3 cm_c;
    // NB: sizeof(float3) == sizeof(float4)
};
cbuffer PS_TEX_DIMENSIONS : register(b4)
{
    float width;
    float height;
    float dx;
    float dy;
};

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
    float colorY = texY.Sample(samp, input.Tex).r;
    float2 colorUV = texUV.Sample(sampL, input.Tex).rg;

    float4 color = float4(colorY, colorUV, 0);

    color.rgb = float3(mul(cm_r, color.rgb), mul(cm_g, color.rgb), mul(cm_b, color.rgb)) + cm_c;

#if (C_HDR == 1)
    color = correct_ST2084(color);
#elif (C_HDR == 2)
    color = correct_HLG(color);
#endif

    return color;
}
