// ps_convert_pq_to_sdr with the curve built from the content's brightness instead of the fixed one.
// Used only by the Metadata method, so the shader for the Fixed method stays exactly as it was.
Texture2D tex : register(t0);
SamplerState samp : register(s0);

cbuffer PS_PARAMETERS : register(b0)
{
    float LuminanceScale;
    float param2; // the content peak in nits, 0 when there is nothing usable
};

#ifdef MEASURED
// [1].z is the peak the measurement settled on, in nits, windowed and floored
// (cs_hdr_resolve.hlsl).  It is 0 until a frame has been measured, and the file's
// metadata in param2 stands in until then.
StructuredBuffer<float4> measured : register(t1);
#endif

#include "../convert/conv_matrix.hlsl"
#include "../convert/st2084.hlsl"
#include "../convert/hdr_tone_mapping.hlsl"
#include "../convert/colorspace_gamut_conversion.hlsl"
#include "../convert/hdr_tone_mapping_spline.hlsl"

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 color = tex.Sample(samp, input.Tex); // original pixel

    // PQ to Linear
    color = saturate(color);
    color = ST2084ToLinear(color, LuminanceScale);

#ifdef MEASURED
    float contentNits = param2;
    const float peak = measured[1].z;
    if (peak > 0.0f)
        contentNits = clamp(peak, 10000.0f / LuminanceScale + 1.0f, 10000.0f);
    color.rgb = ToneMappingSdr(color.rgb, LuminanceScale, contentNits, convert_matrix_2020_to_709);
#else
    color.rgb = ToneMappingSdr(color.rgb, LuminanceScale, param2, convert_matrix_2020_to_709);
#endif
    color.rgb = Colorspace_Gamut_Conversion_2020_to_709(color.rgb);

    // Linear to sRGB
    color = saturate(color);
    color = pow(color, 1.0 / 2.2);

    return color;
}
