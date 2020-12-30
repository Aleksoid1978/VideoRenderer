Texture2D tex : register(t0);
SamplerState samp : register(s0);

#include "../convert/conv_matrix.hlsl"

static const float4x4 fix_ycgco_matrix = mul(ycgco_rgb, rgb_ycbcr709);

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 pixel = tex.Sample(samp, input.Tex); // original pixel

    // Fix incorrect (unsupported) conversion from YCgCo to RGB in D3D11 VP
    pixel = mul(fix_ycgco_matrix, pixel);

    return pixel;
}
