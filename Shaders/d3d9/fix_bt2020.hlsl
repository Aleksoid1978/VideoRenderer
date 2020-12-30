sampler s0 : register(s0);

#include "../convert/conv_matrix.hlsl"
#include "../convert/colorspace_gamut_conversion.hlsl"

static const float4x4 fix_bt2020_matrix = mul(ycbcr2020nc_rgb, rgb_ycbcr709);

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float4 color = tex2D(s0, tex);

    // Fix incorrect (unsupported) conversion from YCbCr BT.2020 to RGB in DXVA2 VP
    color = mul(fix_bt2020_matrix, color);

    // sRGB to Linear
    color = saturate(color);
    color = pow(color, 2.2);

    color.rgb = Colorspace_Gamut_Conversion_2020_to_709(color.rgb);

    // Linear to sRGB
    color = saturate(color);
    color = pow(color, 1.0 / 2.2);

    return color;
}
