#ifndef C_CSP
    #define C_CSP 1
#endif

#ifndef C_HDR
    #define C_HDR 0
#endif

sampler s0 : register(s0);

#if C_CSP
float3 cm_r : register(c0);
float3 cm_g : register(c1);
float3 cm_b : register(c2);
float3 cm_c : register(c3);
#endif

#if (C_HDR == 1)
#include "../convert/correct_st2084.hlsl"
#elif (C_HDR == 2)
#include "../convert/correct_hlg.hlsl"
#endif


float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float4 color = tex2D(s0, tex); // original pixel

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
