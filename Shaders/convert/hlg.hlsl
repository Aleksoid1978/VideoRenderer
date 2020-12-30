inline float3 inverse_HLG(float3 rgb)
{
    const float B67_a = 0.17883277;
    const float B67_b = 0.28466892;
    const float B67_c = 0.55991073;
    const float B67_inv_r2 = 4.0;
    rgb = (rgb <= 0.5)
        ? rgb * rgb * B67_inv_r2
        : exp((rgb - B67_c) / B67_a) + B67_b;
    return rgb;
}

inline float3 HLGtoLinear(float3 rgb)
{
    rgb = inverse_HLG(rgb);
    float3 ootf_2020 = float3(0.2627, 0.6780, 0.0593);
    float ootf_ys = 2000.0f * dot(ootf_2020, rgb);
    rgb *= pow(ootf_ys, 0.2f);
    return rgb;
}
