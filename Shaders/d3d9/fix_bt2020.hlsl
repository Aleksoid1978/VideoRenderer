sampler s0 : register(s0);

#include "../convert/colorspace_gamut_conversion.hlsl"

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float4 pixel = tex2D(s0, tex);

    pixel = saturate(pixel);
    pixel = pow(pixel, 2.2);

    pixel.rgb = Colorspace_Gamut_Conversion_2020_to_709(pixel.rgb);

    pixel = saturate(pixel);
    pixel = pow(pixel, 1.0 / 2.2);

    return pixel;
}
