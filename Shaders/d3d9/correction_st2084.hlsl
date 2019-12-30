sampler s0 : register(s0);

#include "../convert/correct_st2084.hlsl"

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float4 color = tex2D(s0, tex); // original pixel

    color = correct_ST2084(color);

    return color;
}
