#ifndef AXIS
    #define AXIS 0
#endif

#ifndef METHOD
    #define METHOD 1
#endif

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
    float p = input.Tex[AXIS] * wh[AXIS] - 0.5;
    float t = frac(p); // calculate the difference between the output pixel and the original surrounding two pixels

#if (AXIS == 0)
    float2 pos = input.Tex - float2(t, 0);
    // original pixels
    float4 Q0 = tex.Sample(samp, (pos+float2(-.5, .5))*dxdy);
    float4 Q1 = tex.Sample(samp, (pos+.5)*dxdy);
    float4 Q2 = tex.Sample(samp, (pos+float2(1.5, .5))*dxdy);
    float4 Q3 = tex.Sample(samp, (pos + float2(2.5, .5)) * dxdy);
#elif (AXIS == 1)
    float2 pos = input.Tex - float2(0, t);
    // original pixels
    float4 Q0 = tex.Sample(samp, (pos+float2(.5, -.5))*dxdy);
    float4 Q1 = tex.Sample(samp, (pos+.5)*dxdy);
    float4 Q2 = tex.Sample(samp, (pos+float2(.5, 1.5))*dxdy);
    float4 Q3 = tex.Sample(samp, (pos+float2(.5, 2.5))*dxdy);
#else
    #error ERROR: incorrect AXIS.
#endif

    // calculate weights
    float t2 = t * t;
    float t3 = t * t2;
#if (METHOD == 0)   // Mitchell-Netravali spline4
    float4 w0123 = float4(1., 16., 1., 0.)/18.+float4(-.5, 0., .5, 0.)*t+float4(5., -12., 9., -2.)/6.*t2+float4(-7., 21., -21., 7.)/18.*t3;
#elif (METHOD == 1) // Catmull-Rom spline4
    float4 w0123 = float4(-.5, 0., .5, 0.)*t+float4(1., -2.5, 2., -.5)*t2+float4(-.5, 1.5, -1.5, .5)*t3;
    w0123.y += 1.;
#else
    #error ERROR: incorrect METHOD.
#endif

    return w0123[0] * Q0 + w0123[1] * Q1 + w0123[2] * Q2 + w0123[3] * Q3; // interpolation output
}
