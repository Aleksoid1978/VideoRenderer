// A curve and color model for the HDR to SDR conversion, used only by the Metadata
// method.  It is kept out of hdr_tone_mapping.hlsl so that the shaders for the Fixed method
// contain none of it and are unchanged.
//
// BT.2020 linear light, nits in and nits out, content peak P onto display peak D.  ToneMappingHable
// knows nothing about how bright the content is, so it compresses a 400 nit scene as hard as a 4000
// nit one; this does not.
//
//   the gray curve  the Hermite spline of BT.2390 in PQ, knee at 1.5 * PQ(D) - 0.5 so it follows
//                   the display and not the content, flat at max(P, 4000 nits)
//   color           the curve is given a saturation weighted mix of luminance and largest channel,
//                   mixed towards a per channel curve in LMS so very bright colors drift in hue
//                   and turn paler instead of clipping
//
// Saturation is taken on PQ encoded values rather than linear light.  In linear light nearly every
// pixel of real content carrying any tint reads as almost fully saturated, so weighting by it
// compresses colored mid tones about twice as hard as it should; on a real 4K frame that was a 13%
// shortfall against an 8% one.  A test chart cannot tell the two apart, because chart colors read
// as saturated on either scale, which is why this was not visible in the patch fit.
//
// Requires st2084.hlsl and hdr_tone_mapping.hlsl before it.
static const float kSplineFlatAt = 4000.0f;
static const float kSplineLmsShare = 0.2003f;
static const float kSplineLmsPower = 2.73f;
static const float3x3 kSplineRgbToLms = float3x3(
	0.412109375f, 0.523925781f, 0.063964844f,
	0.166748047f, 0.720458984f, 0.112792969f,
	0.024169922f, 0.075439453f, 0.900390625f);
static const float3x3 kSplineLmsToRgb = float3x3(
	 3.436606694f, -2.506452119f,  0.069845424f,
	-0.791329556f,  1.983600452f, -0.192270896f,
	-0.025949900f, -0.098913715f,  1.124863614f);

inline float SplineCurve(float nits, float knee, float width, float endVal, float top)
{
	const float e = LinearToST2084(nits, 10000.0f).x;
	if (e <= knee)
		return nits;
	const float t = saturate((e - knee) / width);
	const float t2 = t * t;
	const float t3 = t2 * t;
	const float h = (2.0f * t3 - 3.0f * t2 + 1.0f) * knee + (t3 - 2.0f * t2 + t) * width + (-2.0f * t3 + 3.0f * t2) * endVal;
	return ST2084ToLinear(min(h, top), 10000.0f).x;
}

// toDisplay converts BT.2020 to the primaries being shown.  It is needed because how far a color
// overshoots has to be judged on the channels the display will actually get, not on BT.2020: a
// saturated color that fits in BT.2020 can still be well outside BT.709, and judging it before the
// conversion leaves it to be clipped a channel at a time afterwards.
float3 ToneMappingSpline(float3 nits, float D, float P, float3x3 toDisplay)
{
	const float M = max(nits.r, max(nits.g, nits.b));
	if (M <= 0.000001f || P <= D)
		return nits;

	const float top = LinearToST2084(D, 10000.0f).x;
	const float knee = max(1.5f * top - 0.5f, 0.0f);
	const float width = LinearToST2084(max(P, kSplineFlatAt), 10000.0f).x - knee;
	float endVal = top;
	if (P < kSplineFlatAt) {
		// the value at the content peak is linear in the end value, so it can be solved for
		const float t = (LinearToST2084(P, 10000.0f).x - knee) / width;
		const float t2 = t * t;
		const float t3 = t2 * t;
		endVal = (top - (2.0f * t3 - 3.0f * t2 + 1.0f) * knee - (t3 - 2.0f * t2 + t) * width) / (-2.0f * t3 + 3.0f * t2);
	}

	const float Y = 0.2627f * nits.r + 0.6780f * nits.g + 0.0593f * nits.b;
	if (Y <= 0.000001f)
		return nits;
	const float3 e = LinearToST2084(float4(nits, 0.0f), 10000.0f).rgb;
	const float eMax = max(e.r, max(e.g, e.b));
	const float s = (eMax > 0.000001f) ? saturate(1.0f - min(e.r, min(e.g, e.b)) / eMax) : 0.0f;
	const float N = Y * pow(max(M / Y, 1.0f), s);
	const float3 S = nits * (SplineCurve(N, knee, width, endVal, top) / N);

	const float3 lms = max(mul(kSplineRgbToLms, nits), 0.0f);
	const float3 PC = max(mul(kSplineLmsToRgb, float3(
		SplineCurve(lms.x, knee, width, endVal, top),
		SplineCurve(lms.y, knee, width, endVal, top),
		SplineCurve(lms.z, knee, width, endVal, top))), 0.0f);

	const float3 Sd = mul(toDisplay, S);
	const float rho = max(max(Sd.r, max(Sd.g, Sd.b)) / D, 1.0f);
	const float mu = kSplineLmsShare + (1.0f - kSplineLmsShare) * (1.0f - pow(rho, -kSplineLmsPower));
	const float3 c = max(lerp(S, PC, mu), 0.0f);

	const float3 d = mul(toDisplay, c);
	const float m = max(d.r, max(d.g, d.b));
	return (m > D) ? c * (D / m) : c;
}

// rgb is scaled so that 1.0 is the display's white; contentNits carries the content peak, and is 0
// when there is nothing usable, which falls back to the fixed curve.
float3 ToneMappingSdr(float3 rgb, float luminanceScale, float contentNits, float3x3 toDisplay)
{
	if (contentNits <= 0.0f)
		return ToneMappingHable(rgb);
	const float D = 10000.0f / max(luminanceScale, 0.0001f);
	return ToneMappingSpline(rgb * D, D, contentNits, toDisplay) / D;
}
