// Catmull-Rom spline4

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

#define sp(a, b, c) float4 a = tex.Sample(samp, pos + dxdy*float2(b, c))

float4 main(PS_INPUT input) : SV_Target
{
    float2 pos = (input.Tex) * wh - 0.5;
    float2 t = frac(pos);         // calculate the difference between the output pixel and the original surrounding two pixels
    pos = (pos - t + 0.5) * dxdy; // adjust sampling matrix to put the output pixel in the interval [Q1, Q2)

    // weights
    float2 t2 = t * t;
    float2 t3 = t * t2;
    float2 w0 = t2 - (t3 + t) / 2;
    float2 w1 = t3 * 1.5 + 1 - t2 * 2.5;
    float2 w2 = t2 * 2 + t / 2 - t3 * 1.5;
    float2 w3 = (t3 - t2) / 2;

    // original pixels
    sp(M0, -1, -1); sp(M1, -1, 0); sp(M2, -1, 1); sp(M3, -1, 2);
    sp(L0,  0, -1); sp(L1,  0, 0); sp(L2,  0, 1); sp(L3,  0, 2);
    sp(K0,  1, -1); sp(K1,  1, 0); sp(K2,  1, 1); sp(K3,  1, 2);
    sp(J0,  2, -1); sp(J1,  2, 0); sp(J2,  2, 1); sp(J3,  2, 2);

    // vertical interpolation
    float4 Q0 = M0 * w0.y + M1 * w1.y + M2 * w2.y + M3 * w3.y;
    float4 Q1 = L0 * w0.y + L1 * w1.y + L2 * w2.y + L3 * w3.y;
    float4 Q2 = K0 * w0.y + K1 * w1.y + K2 * w2.y + K3 * w3.y;
    float4 Q3 = J0 * w0.y + J1 * w1.y + J2 * w2.y + J3 * w3.y;

    return Q0 * w0.x + Q1 * w1.x + Q2 * w2.x + Q3 * w3.x; // horizontal interpolation and output
}
