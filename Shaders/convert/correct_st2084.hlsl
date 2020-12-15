// Convert HDR to SDR for SMPTE ST 2084

static const float ST2084_m1 =  2610.0f / (4096.0f * 4.0f);
static const float ST2084_m2 = (2523.0f / 4096.0f) * 128.0f;
static const float ST2084_c1 =  3424.0f / 4096.0f;
static const float ST2084_c2 = (2413.0f / 4096.0f) * 32.0f;
static const float ST2084_c3 = (2392.0f / 4096.0f) * 32.0f;

#define SRC_LUMINANCE_PEAK     10000.0
#define DISPLAY_LUMINANCE_PEAK 125.0

#include "hdr_tone_mapping.hlsl"
#include "colorspace_gamut_conversion.hlsl"

//#pragma warning(disable: 3571) // fix warning X3571 in pow()

inline float4 correct_ST2084(float4 pixel)
{
    pixel = saturate(pixel); // use saturate(), because pow() can not take negative values

    // ST2084 to Linear
    pixel.rgb = pow(pixel.rgb, 1.0 / ST2084_m2);
    pixel.rgb = max(pixel.rgb - ST2084_c1, 0.0) / (ST2084_c2 - ST2084_c3 * pixel.rgb);
    pixel.rgb = pow(pixel.rgb, 1.0 / ST2084_m1);

    // Peak luminance
    pixel.rgb = pixel.rgb * (SRC_LUMINANCE_PEAK / DISPLAY_LUMINANCE_PEAK);

    // HDR tone mapping
    pixel.rgb = ToneMappingHable(pixel.rgb);

    // Colorspace Gamut Conversion
    pixel.rgb = Colorspace_Gamut_Conversion_2020_to_709(pixel.rgb);

    // Linear to sRGB
    pixel.rgb = saturate(pixel.rgb);
    pixel.rgb = pow(pixel.rgb, 1.0 / 2.2);

    return pixel;
}
