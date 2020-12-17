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
	//TODO
    float4 color = tex.Sample(samp, input.Tex);

    color.rgb = Colorspace_Gamut_Conversion_2020_to_709(color.rgb);

    return color;
}
