Texture2D tx : register(t0);
SamplerState samLinear : register(s0);

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 pix = tx.Sample(samLinear, input.Tex);
    pix.rgb = 1.0 - pix.rgb; // invert
    return pix;
}
