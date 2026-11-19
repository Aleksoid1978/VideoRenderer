// Requires st2084.hlsl, hdr_tone_mapping.hlsl, and hdr_tone_mapping_spline.hlsl.

// Adapted from SKIV's SDR export path (src/utility/image.cpp and
// include/utility/image.h). Copyright (c) 2024 Aemony.
// Copyright 2024 Andon "Kaldaien" Coleman (tone_mapping.hlsli).
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

// SKIV works on PQ-encoded LMS intensity and scales chroma with the intensity change.
float SkivIntensity(float x, float peak, float white)
{
    return pow(x, 1.18f) * (1.0f + white * x / (peak * peak)) / (1.0f + x / white);
}

float3 ToneMappingSkiv(float3 rgb, float peakNits, float sdrWhiteNits, bool piecewise)
{
    static const float3x3 rgbToLms = float3x3(
        0.412054996f, 0.523912840f, 0.064047655f,
        0.166651961f, 0.720393192f, 0.112901059f,
        0.024134951f, 0.075470882f, 0.900458689f);
    static const float3x3 lmsToIctcp = float3x3(
        0.5f, 0.5f, 0.0f,
        1.6137f, -3.3234f, 1.7097f,
        4.378f, -4.2455f, -0.1325f);
    float3 lms = LinearToST2084(float4(max(mul(rgbToLms, rgb), 0.0f), 0.0f), 10000.0f).rgb;
    float3 ictcp = mul(lmsToIctcp, lms);
    float input = max(ictcp.x, 0.0f);
    float white = LinearToST2084(120.0f, 10000.0f).x;
    float peak = max(white, LinearToST2084(min(peakNits, 10000.0f), 10000.0f).x);
    float reference, mapped;

    if (piecewise)
    {
        float outputWhite = LinearToST2084(sdrWhiteNits, 10000.0f).x;
        float knee = LinearToST2084(0.67f * sdrWhiteNits, 10000.0f).x;
        if (input <= knee)
            return rgb;
        peak = LinearToST2084(clamp(peakNits, sdrWhiteNits, 10000.0f), 10000.0f).x;
        float lo = SkivIntensity(knee, peak, white);
        float hi = SkivIntensity(peak, peak, white);
        reference = input;
        mapped = lerp(knee, outputWhite, saturate((SkivIntensity(min(input, peak), peak, white) - lo) / max(hi - lo, 1e-6f)));
    }
    else
    {
        reference = pow(input, 1.18f);
        mapped = SkivIntensity(input, peak, white);
    }

    ictcp.x = mapped;
    ictcp.yz *= min(reference, mapped) / max(max(reference, mapped), 1e-6f);
    static const float3x3 ictcpToLms = float3x3(
        1.0f, 0.008605146f, 0.111035604f,
        1.0f, -0.008605146f, -0.111035604f,
        1.0f, 0.560048860f, -0.320637470f);
    static const float3x3 lmsToRgb = float3x3(
        3.436527235f, -2.506564477f, 0.069844690f,
        -0.790942041f, 1.983511467f, -0.192438102f,
        -0.025817205f, -0.099062347f, 1.124802078f);
    lms = ST2084ToLinear(float4(saturate(mul(ictcpToLms, ictcp)), 0.0f), 10000.0f).rgb;
    float3 result = mul(lmsToRgb, lms);
    return clamp(result * (piecewise ? 1.0f : sdrWhiteNits / 80.0f), 0.0f, sdrWhiteNits);
}

// RGB is relative to SDR white. Curve values match SDRCurve_* in IVideoRenderer.h.
float3 ConvertHdrToSdr(float3 rgb, float luminanceScale, float contentNits, float3x3 toDisplay, float curve)
{
	[branch]
	if (curve > 0.5f) {
		const float displayNits = 10000.0f / luminanceScale;
		const float peakNits = contentNits > 0.0f ? contentNits : 1000.0f;
		return ToneMappingSkiv(rgb * displayNits, peakNits, displayNits, curve > 1.5f) / displayNits;
	}
	return ToneMappingSdr(rgb, luminanceScale, contentNits, toDisplay);
}
