// pixel shader for D3D11Font
Texture2D shaderTexture;
SamplerState SampleType;

cbuffer PS_COLOR
{
    float4 pixelColor;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
    return shaderTexture.Sample(SampleType, input.tex) * pixelColor;
}
