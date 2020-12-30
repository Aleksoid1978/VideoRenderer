Texture2D tex : register(t0);
SamplerState samp : register(s0);

#include "../convert/hlg.hlsl"
#include "../convert/st2084.hlsl"

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 color = tex.Sample(samp, input.Tex); // original pixel

    color = saturate(color); // use saturate(), because pow() can not take negative values
    color.rgb = HLGtoLinear(color.rgb);
    color = LinearToST2084(color, 1000.0);

    return color;
}
