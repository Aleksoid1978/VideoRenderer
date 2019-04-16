#ifndef AXIS
    #define AXIS 0
#endif

#ifndef METHOD
    #define METHOD 1
#endif

sampler s0 : register(s0);
float2 dxdy : register(c0);

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float t = frac(tex[AXIS]);
#if (AXIS == 0)
    float2 pos = tex-float2(t, 0.);
    // original pixels
    float4 Q0 = tex2D(s0, (pos+float2(-.5, .5))*dxdy);
    float4 Q1 = tex2D(s0, (pos+.5)*dxdy);
    float4 Q2 = tex2D(s0, (pos+float2(1.5, .5))*dxdy);
    float4 Q3 = tex2D(s0, (pos+float2(2.5, .5))*dxdy);
#elif (AXIS == 1)
    float2 pos = tex-float2(0., t);
    // original pixels
    float4 Q0 = tex2D(s0, (pos+float2(.5, -.5))*dxdy);
    float4 Q1 = tex2D(s0, (pos+.5)*dxdy);
    float4 Q2 = tex2D(s0, (pos+float2(.5, 1.5))*dxdy);
    float4 Q3 = tex2D(s0, (pos+float2(.5, 2.5))*dxdy);
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
