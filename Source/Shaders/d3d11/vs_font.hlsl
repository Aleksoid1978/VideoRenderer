// vertex shader for D3D11Font
struct VS_INPUT
{
    float4 pos : POSITION;
    float2 tex : TEXCOORD;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD;
};

VS_OUTPUT main(VS_INPUT input)
{
    // TODO
    VS_OUTPUT output;
    output = input;

    return output;
}
