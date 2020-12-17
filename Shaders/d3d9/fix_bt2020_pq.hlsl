sampler s0 : register(s0);

#include "../convert/colorspace_gamut_conversion.hlsl"

static const float ST2084_m1 = 2610.0f / (4096.0f * 4.0f);
static const float ST2084_m2 = (2523.0f / 4096.0f) * 128.0f;
static const float ST2084_c1 = 3424.0f / 4096.0f;
static const float ST2084_c2 = (2413.0f / 4096.0f) * 32.0f;
static const float ST2084_c3 = (2392.0f / 4096.0f) * 32.0f;

#define SRC_LUMINANCE_PEAK     10000.0
#define DISPLAY_LUMINANCE_PEAK 125.0

float4 main(float2 tex : TEXCOORD0) : COLOR
{
    //TODO
    float4 pixel = tex2D(s0, tex);

    pixel = saturate(pixel); // use saturate(), because pow() can not take negative values

    // ST2084 to Linear
    pixel.rgb = pow(pixel.rgb, 1.0 / ST2084_m2);
    pixel.rgb = max(pixel.rgb - ST2084_c1, 0.0) / (ST2084_c2 - ST2084_c3 * pixel.rgb);
    pixel.rgb = pow(pixel.rgb, 1.0 / ST2084_m1);

    // Peak luminance
    pixel.rgb = pixel.rgb * (SRC_LUMINANCE_PEAK / DISPLAY_LUMINANCE_PEAK);

    // Colorspace Gamut Conversion
    pixel.rgb = Colorspace_Gamut_Conversion_2020_to_709(pixel.rgb);

    // Linear to sRGB
    pixel.rgb = saturate(pixel.rgb);
    pixel.rgb = pow(pixel.rgb, 1.0 / 2.2);

    return pixel;
}
