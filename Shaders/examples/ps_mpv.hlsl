// based on blit_float_ps from code of mpv

Texture2D<float4> tex : register(t0);
SamplerState samp : register(s0);

float4 main(float4 pos : SV_Position, float2 coord : TEXCOORD0) : SV_Target
{
    return tex.Sample(samp, coord);
}
