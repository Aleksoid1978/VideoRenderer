static const float ST2084_m1 =  2610.0f / (4096.0f * 4.0f);
static const float ST2084_m2 = (2523.0f / 4096.0f) * 128.0f;
static const float ST2084_c1 =  3424.0f / 4096.0f;
static const float ST2084_c2 = (2413.0f / 4096.0f) * 32.0f;
static const float ST2084_c3 = (2392.0f / 4096.0f) * 32.0f;

#pragma warning(disable: 3571)

inline float4 ST2084ToLinear(float4 rgb, float factor)
{
    rgb = pow(rgb, 1.0 / ST2084_m2);
    rgb = max(rgb - ST2084_c1, 0.0) / (ST2084_c2 - ST2084_c3 * rgb);
    rgb = pow(rgb, 1.0 / ST2084_m1);
    rgb *= factor;
    return rgb;
}

inline float4 LinearToST2084(float4 rgb, float divider)
{
    rgb /= divider;
    rgb = pow(rgb, ST2084_m1);
    rgb = (ST2084_c1 + ST2084_c2 * rgb) / (1.0f + ST2084_c3 * rgb);
    rgb = pow(rgb, ST2084_m2);
    return rgb;
}
