// based on globVertexShaderFlat from code of VLC

struct VS_INPUT
{
  float4 Position   : POSITION;
  float4 Texture    : TEXCOORD0;
};

struct VS_OUTPUT
{
  float4 Position   : SV_POSITION;
  float4 Texture    : TEXCOORD0;
};

VS_OUTPUT main( VS_INPUT In )
{
  return In;
}
