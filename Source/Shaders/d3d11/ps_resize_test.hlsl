Texture2D tex : register(t0);
SamplerState samp : register(s0);

cbuffer PS_WH : register(b0)
{
    float2 wh;
    float2 dxdy;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_Target
{
    float2 pos = input.Tex * wh;
    float2 t = frac(pos);
    pos -= t;

    float4 c00 = tex.Sample(samp, (pos + 0.5) * dxdy);
    float4 c01 = tex.Sample(samp, (pos + float2(0.5, 1.5)) * dxdy);
    float4 c10 = tex.Sample(samp, (pos + float2(1.5, 0.5)) * dxdy);
    float4 c11 = tex.Sample(samp, (pos + 1.5) * dxdy);


    return lerp(
        lerp(c00, c10, t.x),
        lerp(c01, c11, t.x),
        t.y); // interpolate and output
}
