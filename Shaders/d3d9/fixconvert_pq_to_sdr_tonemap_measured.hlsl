// fixconvert_pq_to_sdr_tonemap with the content peak taken from the frame instead of the file.
//
// s1 holds the state hdr_peak_state.hlsl wrote for this same frame, so the conversion uses the
// measurement of the frame it is converting: there is no read back and no latency.  Its second
// texel is the peak in nits, already floored.  It is 0 until a frame has been measured, and the
// file's metadata in param2 stands in until then.
sampler s0 : register(s0);
sampler s1 : register(s1);
float4 parameters : register(c0);
#define LuminanceScale parameters[0]
#define param2         parameters[1]

#include "../convert/conv_matrix.hlsl"
#include "../convert/st2084.hlsl"
#include "../convert/hdr_tone_mapping.hlsl"
#include "../convert/colorspace_gamut_conversion.hlsl"
#include "../convert/hdr_tone_mapping_spline.hlsl"

static const float4x4 fix_bt2020_matrix = mul(ycbcr2020nc_rgb, rgb_ycbcr709);

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float4 color = tex2D(s0, tex); // original pixel

    // Fix incorrect (unsupported) conversion from YCbCr BT.2020 to RGB in DXVA2 VP
    color = mul(fix_bt2020_matrix, color);

    // PQ to Linear
    color = saturate(color);
    color = ST2084ToLinear(color, LuminanceScale);

    float contentNits = param2;
    const float peak = tex2D(s1, float2(0.75f, 0.5f)).x;
    if (peak > 0.0f)
        contentNits = clamp(peak, 10000.0f / LuminanceScale + 1.0f, 10000.0f);

    color.rgb = ToneMappingSdr(color.rgb, LuminanceScale, contentNits, convert_matrix_2020_to_709);
    color.rgb = Colorspace_Gamut_Conversion_2020_to_709(color.rgb);

    // Linear to sRGB
    color = saturate(color);
    color = pow(color, 1.0 / 2.2);

    return color;
}
