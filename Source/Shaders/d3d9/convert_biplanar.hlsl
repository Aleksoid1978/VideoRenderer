#ifndef C_HDR
    #define C_HDR 0
#endif

sampler sY : register(s0);
sampler sUV : register(s1);

float4 cm_r : register(c0);
float4 cm_g : register(c1);
float4 cm_b : register(c2);
float3 cm_c : register(c3);

#if (C_HDR == 1)
#include "../convert/correct_st2084.hlsl"
#elif (C_HDR == 2)
#include "../convert/correct_hlg.hlsl"
#endif


float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float colorY = tex2D(sY, tex).r;
    float3 colorUV = tex2D(sUV, tex).rga; // for D3DFMT_G16R16 and D3DFMT_A8L8 variants

    float4 color = float4(colorY, colorUV);

    color.rgb = float3(mul(cm_r, color), mul(cm_g, color), mul(cm_b, color)) + cm_c;

#if (C_HDR == 1)
    color = correct_ST2084(color);
#elif (C_HDR == 2)
    color = correct_HLG(color);
#endif

    return color;
}
