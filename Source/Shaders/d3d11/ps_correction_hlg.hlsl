Texture2D tex : register(t0);
SamplerState samp : register(s0);

#include "../convert/correct_hlg.hlsl"

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 color = tex.Sample(samp, input.Tex); // original pixel

    color = correct_HLG(color);

    return color;
}
