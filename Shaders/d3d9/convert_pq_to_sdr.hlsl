sampler s0 : register(s0);

#include "../convert/hdr_tone_mapping.hlsl"
#include "../convert/colorspace_gamut_conversion.hlsl"
#include "../convert/convert_pq_to_sdr.hlsl"

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float4 color = tex2D(s0, tex); // original pixel

    color = convert_PQ_to_SDR(color);

    return color;
}
