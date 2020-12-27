// Fix incorrect display YCgCo after incorrect YUV to RGB conversion in D3D11 Video Processor

Texture2D tex : register(t0);
SamplerState samp : register(s0);

#include "../convert/conv_matrix.hlsl"

static const float4x4 rgb_ycbcr709_ycgco_rgb = mul(ycgco_rgb, rgb_ycbcr709);

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 pixel = tex.Sample(samp, input.Tex); // original pixel

    // convert RGB to YUV and get original YCgCo. convert YCgCo to RGB
    pixel = mul(rgb_ycbcr709_ycgco_rgb, pixel);

    return pixel;
}
