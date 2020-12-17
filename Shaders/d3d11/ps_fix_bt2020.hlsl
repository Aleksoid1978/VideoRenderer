Texture2D tex : register(t0);
SamplerState samp : register(s0);

#include "../convert/colorspace_gamut_conversion.hlsl"

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 pixel = tex.Sample(samp, input.Tex);

    pixel = saturate(pixel);
    pixel = pow(pixel, 2.2);

    pixel.rgb = Colorspace_Gamut_Conversion_2020_to_709(pixel.rgb);

    pixel = saturate(pixel);
    pixel = pow(pixel, 1.0 / 2.2);

    return pixel;
}
