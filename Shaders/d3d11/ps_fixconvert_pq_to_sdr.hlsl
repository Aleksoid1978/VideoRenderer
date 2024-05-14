Texture2D tex : register(t0);
SamplerState samp : register(s0);

cbuffer PS_COLOR_TRANSFORM : register(b0)
{
    float LuminanceScale;
};

#include "../convert/conv_matrix.hlsl"
#include "../convert/st2084.hlsl"
#include "../convert/hdr_tone_mapping.hlsl"
#include "../convert/colorspace_gamut_conversion.hlsl"

static const float4x4 fix_bt2020_matrix = mul(ycbcr2020nc_rgb, rgb_ycbcr709);

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 color = tex.Sample(samp, input.Tex); // original pixel

    // Fix incorrect (unsupported) conversion from YCbCr BT.2020 to RGB in D3D11 VP
    color = mul(fix_bt2020_matrix, color);

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
