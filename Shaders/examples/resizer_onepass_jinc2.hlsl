// jinc 2

sampler s0   : register(s0); // input texture
float2 dxdy  : register(c0);

#define JINC2_WINDOW_SINC 0.416
#define JINC2_SINC        0.985
#define JINC2_AR_STRENGTH 0.8

static const float pi = acos(-1);
static const float wa = JINC2_WINDOW_SINC * pi;
static const float wb = JINC2_SINC * pi;

#define min4(a, b, c, d) min(min(min(a, b), c), d)
#define max4(a, b, c, d) max(max(max(a, b), c), d)

// Calculates the distance between two points
float d(float2 pt1, float2 pt2)
{
    float2 v = pt2 - pt1;
    return sqrt(dot(v, v));
}

float4 resampler(float4 x)
{
    return (x == float4(0, 0, 0, 0))
        ? float4(wa * wb, wa * wb, wa * wb, wa * wb)
        : sin(x * wa) * sin(x * wb) / (x * x);
}

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    float2 dx = float2(1, 0);
    float2 dy = float2(0, 1);

    float2 pc = tex+0.5;
    float2 tc = floor(tex) + 0.5;

    float4x4 weights = {
        resampler(float4(d(pc, tc - dx - dy  ), d(pc, tc - dy  ), d(pc, tc + dx - dy  ), d(pc, tc + 2*dx - dy  ))),
        resampler(float4(d(pc, tc - dx       ), d(pc, tc       ), d(pc, tc + dx       ), d(pc, tc + 2*dx       ))),
        resampler(float4(d(pc, tc - dx + dy  ), d(pc, tc + dy  ), d(pc, tc + dx + dy  ), d(pc, tc + 2*dx + dy  ))),
        resampler(float4(d(pc, tc - dx + 2*dy), d(pc, tc + 2*dy), d(pc, tc + dx + 2*dy), d(pc, tc + 2*dx + 2*dy)))
    };

    dx *= dxdy;
    dy *= dxdy;
    tc *= dxdy;

    // reading the texels
    // [ c00, c10, c20, c30 ]
    // [ c01, c11, c21, c31 ]
    // [ c02, c12, c22, c32 ]
    // [ c03, c13, c23, c33 ]
    float3 c00 = tex2D(s0, tc - dx   - dy  ).rgb;
    float3 c10 = tex2D(s0, tc        - dy  ).rgb;
    float3 c20 = tex2D(s0, tc + dx   - dy  ).rgb;
    float3 c30 = tex2D(s0, tc + 2*dx - dy  ).rgb;
    float3 c01 = tex2D(s0, tc - dx         ).rgb;
    float3 c11 = tex2D(s0, tc              ).rgb;
    float3 c21 = tex2D(s0, tc + dx         ).rgb;
    float3 c31 = tex2D(s0, tc + 2*dx       ).rgb;
    float3 c02 = tex2D(s0, tc - dx   + dy  ).rgb;
    float3 c12 = tex2D(s0, tc        + dy  ).rgb;
    float3 c22 = tex2D(s0, tc + dx   + dy  ).rgb;
    float3 c32 = tex2D(s0, tc + 2*dx + dy  ).rgb;
    float3 c03 = tex2D(s0, tc - dx   + 2*dy).rgb;
    float3 c13 = tex2D(s0, tc        + 2*dy).rgb;
    float3 c23 = tex2D(s0, tc + dx   + 2*dy).rgb;
    float3 c33 = tex2D(s0, tc + 2*dx + 2*dy).rgb;

    float3 color = mul(weights[0], float4x3(c00, c10, c20, c30));
    color += mul(weights[1], float4x3(c01, c11, c21, c31));
    color += mul(weights[2], float4x3(c02, c12, c22, c32));
    color += mul(weights[3], float4x3(c03, c13, c23, c33));
    color /= dot(mul(weights, float4(1, 1, 1, 1)), 1);

    // calc min/max samples
    float3 min_sample = min4(c11, c21, c12, c22);
    float3 max_sample = max4(c11, c21, c12, c22);

    // Anti-ringing
    color = lerp(color, clamp(color, min_sample, max_sample), JINC2_AR_STRENGTH);

    return float4(color, 1);
}
