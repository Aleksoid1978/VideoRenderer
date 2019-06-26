#ifndef C_CSP
    #define C_CSP 1
#endif

#ifndef C_YUY2
    #define C_YUY2 0
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

#if C_YUY2
float4 p4 : register(c4);
#define width  (p4[0])
#define height (p4[1])
#define dx     (p4[2])
#define dy     (p4[3])
#endif

#if (C_HDR == 1)
#include "../convert/correct_st2084.hlsl"
#elif (C_HDR == 2)
#include "../convert/correct_hlg.hlsl"
#endif


float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float4 color = tex2D(s0, tex); // original pixel

#if C_YUY2
    if (fmod(tex.x*width, 2) < 1.0) {
#if (C_YUY2 == 1) // nearest neighbor
        color = float4(color[2], color[1], color[3], 0);
#elif (C_YUY2 == 2) // linear
        float2 chroma0 = color.yw;
        float2 chroma1 = tex2D(s0, tex + float2(0, dx)).yw;
        float2 chroma = (chroma0 + chroma1) * 0.5;
        color = float4(color[2], chroma, 0);
#endif
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
