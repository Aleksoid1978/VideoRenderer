// ps_fixconvert_hlg_to_sdr with the curve built from the content's brightness instead of the fixed
// one.  Used only by the Metadata method, so the shader for the Fixed method stays exactly as it was.
//
// HLG carries no MaxCLL of its own as a rule, so param2 is usually the 1000 nit fallback.  That is
// the right number here rather than a guess, since HLGtoLinear scales HLG white to 1000 nits.
Texture2D tex : register(t0);
SamplerState samp : register(s0);

#include "../convert/conv_matrix.hlsl"
#include "../convert/hlg.hlsl"
#include "../convert/st2084.hlsl"
#include "../convert/hdr_tone_mapping.hlsl"
#include "../convert/colorspace_gamut_conversion.hlsl"
#include "../convert/hdr_tone_mapping_spline.hlsl"

static const float4x4 fix_bt2020_matrix = mul(ycbcr2020nc_rgb, rgb_ycbcr709);

cbuffer PS_PARAMETERS : register(b0)
{
    float LuminanceScale;
    float param2; // the content peak in nits, 0 when there is nothing usable
};

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

    // HLG to PQ
    color = saturate(color);
    color.rgb = HLGtoLinear(color.rgb);
    color = LinearToST2084(color, 10000.0);

    // PQ to Linear
    color = saturate(color);
    color = ST2084ToLinear(color, LuminanceScale);

    color.rgb = ToneMappingSdr(color.rgb, LuminanceScale, param2, convert_matrix_2020_to_709);
    color.rgb = Colorspace_Gamut_Conversion_2020_to_709(color.rgb);

    // Linear to sRGB
    color = saturate(color);
    color = pow(color, 1.0 / 2.2);

    return color;
}
