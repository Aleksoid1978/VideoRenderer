sampler s0 : register(s0);

#include "../convert/colorspace_gamut_conversion.hlsl"

float4 main(float2 tex : TEXCOORD0) : COLOR
{
	//TODO
    float4 color = tex2D(s0, tex); // original pixel

    color.rgb = Colorspace_Gamut_Conversion_2020_to_709(color.rgb);

    return color;
}
