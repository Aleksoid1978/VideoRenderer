#ifndef C_HDR
    #define C_HDR 0
#endif
#ifndef C_A8L8
    #define C_A8L8 0
#endif

sampler sY : register(s0);
sampler sUV : register(s1);

float3 cm_r : register(c0);
float3 cm_g : register(c1);
float3 cm_b : register(c2);
float3 cm_c : register(c3);
/*
float4 p4 : register(c4);
#define width  (p4[0])
#define height (p4[1])
#define dx     (p4[2])
#define dy     (p4[3])
*/

#if (C_HDR == 1)
#include "../convert/correct_st2084.hlsl"
#elif (C_HDR == 2)
#include "../convert/correct_hlg.hlsl"
#endif


float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float colorY = tex2D(sY, tex).r;
#if (C_A8L8)
    float2 colorUV = tex2D(sUV, tex).ra; // for D3DFMT_A8L8
#else
    float2 colorUV = tex2D(sUV, tex).rg; // for D3DFMT_G16R16
#endif

    float4 color = float4(colorY, colorUV, 0);

    color.rgb = float3(mul(cm_r, color.rgb), mul(cm_g, color.rgb), mul(cm_b, color.rgb)) + cm_c;

#if (C_HDR == 1)
    color = correct_ST2084(color);
#elif (C_HDR == 2)
    color = correct_HLG(color);
#endif

    return color;
}
