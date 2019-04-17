#ifndef AXIS
    #define AXIS 0
#endif

#define PI acos(-1.)

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
    float pos = input.Tex[AXIS] * wh[AXIS] - 0.5;
    float t = frac(pos); // calculate the difference between the output pixel and the original surrounding two pixels
    pos = pos - t;

#if (AXIS == 0)
    float4 Q2 = tex.Sample(samp, float2((pos + 0.5) * dxdy.x, input.Tex.y)); // nearest original pixel to the left
    if (t) {
        // original pixels
        float4 Q0 = tex.Sample(samp, float2((pos - 1.5) * dxdy.x, input.Tex.y));
        float4 Q1 = tex.Sample(samp, float2((pos - 1.5) * dxdy.x, input.Tex.y));
        float4 Q3 = tex.Sample(samp, float2((pos + 1.5) * dxdy.x, input.Tex.y));
        float4 Q4 = tex.Sample(samp, float2((pos + 2.5) * dxdy.x, input.Tex.y));
        float4 Q5 = tex.Sample(samp, float2((pos + 3.5) * dxdy.x, input.Tex.y));
#elif (AXIS == 1)
    float4 Q1 = tex.Sample(samp, float2(input.Tex.x, (pos + 0.5) * dxdy.y));
    if (t) {
        // original pixels
        float4 Q0 = tex.Sample(samp, float2(input.Tex.x, (pos - 1.5) * dxdy.y));
        float4 Q1 = tex.Sample(samp, float2(input.Tex.x, (pos - 1.5) * dxdy.y));
        float4 Q3 = tex.Sample(samp, float2(input.Tex.x, (pos + 1.5) * dxdy.y));
        float4 Q4 = tex.Sample(samp, float2(input.Tex.x, (pos + 2.5) * dxdy.y));
        float4 Q5 = tex.Sample(samp, float2(input.Tex.x, (pos + 3.5) * dxdy.y));
#else
    #error ERROR: incorrect AXIS.
#endif
        float3 wset0 = float3(2., 1., 0.) * PI + t * PI;
        float3 wset1 = float3(1., 2., 3.) * PI - t * PI;
        float3 wset0s = wset0 * .5;
        float3 wset1s = wset1 * .5;
        float3 w0 = sin(wset0) * sin(wset0s) / (wset0 * wset0s);
        float3 w1 = sin(wset1) * sin(wset1s) / (wset1 * wset1s);

        float wc = 1. - dot(1., w0 + w1); // compensate truncated window factor by linear factoring on the two nearest samples
        w0.z += wc * (1. - t);
        w1.x += wc * t;

        return w0.x * Q0 + w0.y * Q1 + w0.z * Q2 + w1.x * Q3 + w1.y * Q4 + w1.z * Q5; // interpolation output
    }

    return Q2; // case t == 0. is required to return sample Q1, because of a possible division by 0.
}
