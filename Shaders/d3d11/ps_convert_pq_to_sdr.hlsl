Texture2D tex : register(t0);
SamplerState samp : register(s0);

#include "../convert/hdr_tone_mapping.hlsl"
#include "../convert/colorspace_gamut_conversion.hlsl"
#include "../convert/convert_pq_to_sdr.hlsl"

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 color = tex.Sample(samp, input.Tex); // original pixel

    color = convert_PQ_to_SDR(color);

    return color;
}
