// Fix incorrect display YCgCo after incorrect YUV to RGB conversion in D3D11 Video Processor

Texture2D tex : register(t0);
SamplerState samp : register(s0);

static float4x4 rgb_ycbcr709 = {
     0.2126,  0.7152,  0.0722, 0.0,
    -0.1146, -0.3854,  0.5,    0.0,
     0.5,    -0.4542, -0.0458, 0.0,
     0.0,     0.0,     0.0,    0.0
};

static float4x4 ycgco_rgb = {
    1.0, -1.0,  1.0, 0.0,
    1.0,  1.0,  0.0, 0.0,
    1.0, -1.0, -1.0, 0.0,
    0.0,  0.0,  0.0, 0.0
};

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
