sampler s0 : register(s0);
float4 parameters : register(c0);
#define LuminanceScale parameters[0]
#define param2         parameters[1]

#include "../convert/conv_matrix.hlsl"
#include "../convert/st2084.hlsl"
#include "../convert/hdr_tone_mapping.hlsl"
#include "../convert/colorspace_gamut_conversion.hlsl"

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float4 color = tex2D(s0, tex); // original pixel

    // PQ to Linear
    color = saturate(color);
    color = ST2084ToLinear(color, LuminanceScale);

    color.rgb = ToneMappingHable(color.rgb);
    color.rgb = Colorspace_Gamut_Conversion_2020_to_709(color.rgb);

    // Linear to sRGB
    color = saturate(color);
    color = pow(color, 1.0 / 2.2);

    return color;
}
