sampler s0 : register(s0);

#include "../convert/conv_matrix.hlsl"

static const float4x4 fix_ycgco_matrix = mul(ycgco_rgb, rgb_ycbcr709);

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float4 pixel = tex2D(s0, tex); // original pixel

    // Fix incorrect (unsupported) conversion from YCgCo to RGB in DXVA2 VP
    pixel = mul(fix_ycgco_matrix, pixel);

    return pixel;
}
