// pixel shader for D3D11Geometry
struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

float4 main(PS_INPUT input) : SV_Target
{
    return input.color;
}
