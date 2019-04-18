#ifndef AXIS
    #define AXIS 0
#endif

#include "../resize/downscaler_filters.hlsl"

Texture2D tex : register(t0);
SamplerState samp : register(s0);

cbuffer PS_WH : register(b0)
{
    float2 wh;
    float2 dxdy;
    float2 scale;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

static const float support = filter_support * scale[AXIS];
static const float ss = 1.0 / scale[AXIS];

#pragma warning(disable: 13595) // disable warning X3595: gradient instruction used in a loop with varying iteration; partial derivatives may have undefined value

float4 main(PS_INPUT input) : SV_Target
{
    float pos = input.Tex[AXIS] * wh[AXIS] + 0.5;

    int low = (int)floor(pos - support);
    int high = (int)ceil(pos + support);

    float ww = 0.0;
    float4 avg = 0;

    [loop] for (int n = low; n < high; n++) {
        float w = filter((n - pos + 0.5) * ss);
        ww += w;
#if (AXIS == 0)
        avg += w * tex.Sample(samp, float2((n + 0.5) * dxdy.x, input.Tex.y));
#else
        avg += w * tex.Sample(samp, float2(input.Tex.x, (n + 0.5) * dxdy.y));
#endif
    }
    avg /= ww;

    return avg;
}
