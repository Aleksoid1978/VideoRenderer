// Convert HLG to PQ

static const float ST2084_m1 = 2610.0f / (4096.0f * 4.0f);
static const float ST2084_m2 = (2523.0f / 4096.0f) * 128.0f;
static const float ST2084_c1 = 3424.0f / 4096.0f;
static const float ST2084_c2 = (2413.0f / 4096.0f) * 32.0f;
static const float ST2084_c3 = (2392.0f / 4096.0f) * 32.0f;

inline float3 inverse_HLG(float3 x)
{
    const float B67_a = 0.17883277;
    const float B67_b = 0.28466892;
    const float B67_c = 0.55991073;
    const float B67_inv_r2 = 4.0;
    x = (x <= 0.5)
        ? x * x * B67_inv_r2
        : exp((x - B67_c) / B67_a) + B67_b;

    return x;
}

float3 transfer_PQ(float3 x)
{
    x = pow(x / 1000.0f, ST2084_m1);
    x = (ST2084_c1 + ST2084_c2 * x) / (1.0f + ST2084_c3 * x);
    x = pow(x, ST2084_m2);
    return x;
}

inline float4 convert_HLG_to_PQ(float4 pixel)
{
    // HLG to Linear
    pixel.rgb = inverse_HLG(pixel.rgb);
    float3 ootf_2020 = float3(0.2627f, 0.6780f, 0.0593f);
    float ootf_ys = 2000.0f * dot(ootf_2020, pixel.rgb);
    pixel.rgb *= pow(ootf_ys, 0.2f);

    // Linear to PQ
    pixel.rgb = transfer_PQ(pixel.rgb);

    return pixel;
}
