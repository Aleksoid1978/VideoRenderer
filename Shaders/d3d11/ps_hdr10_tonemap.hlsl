#include "../convert/st2084.hlsl"

Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

cbuffer HDRParamsConstantBuffer : register(b0)
{
	float MasteringMinLuminanceNits;
	float MasteringMaxLuminanceNits;
	float maxCLL;
	float maxFALL;
	float displayMaxNits;
	uint selection; // 1 = ACES, 2 = Reinhard, 3 = Habel, 4 = Möbius, 5 = BT2390, 6 = ST 2094-10
	float padding[2];
};

cbuffer DolbyConstants : register(b1)
{
	float ChromaWeight;
	float SaturationGain;
	float TrimSlope;
	float TrimOffset;
	float TrimPower;
	uint L2Enabled;
	float L2Padding[2];
};

float3 ACESFilmTonemap(float3 color)
{
	// Constants used in the ACES Filmic tone mapping
	static float A = 2.51f;
	static float B = 0.03f;
	static float C = 2.43f;
	static float D = 0.59f;
	static float E = 0.14f;

	// Apply the ACES RRT + ODT
	color = (color * (A * color + B)) / (color * (C * color + D) + E);

	return color;
}

float3 ReinhardTonemap(float3 color)
{
	return color / (1.0 + color);
}

float3 HabelTonemap(float3 color)
{
	float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
	return ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
}

float3 MobiusTonemap(float3 color)
{
	static float epsilon = 1e-6;
	float maxL = displayMaxNits;
	return color / (1.0 + color / (maxL + epsilon));
}

float3 BT2390Tonemap(float3 color)
{
	//Safe Metadata Fallbacks (Fixes black screens on bad video files)
	float safeMaxCLL = maxCLL;
	if (safeMaxCLL <= 10.0f)
		safeMaxCLL = MasteringMaxLuminanceNits;
	if (safeMaxCLL <= 10.0f)
		safeMaxCLL = 1000.0f; // Ultimate safety fallback
	// Optimization: Skip processing if display is brighter than the content
	if (displayMaxNits >= safeMaxCLL)
		return color;

	// Find the average RGB component to preserve hue and saturation
	float avgRGB = 0.2627 * color.r + 0.6780 * color.g + 0.0593 * color.b; // Use average instead of max to better preserve color balance)

	// Avoid division by zero on pure black pixels
	if (avgRGB <= 0.000001)
		return color;

	// Convert peaks and current pixel luminance to PQ space
	float maxCLL_PQ = LinearToST2084(safeMaxCLL, 10000.0f).x;
	float target_PQ = LinearToST2084(displayMaxNits, 10000.0f).x;
	float E1 = LinearToST2084(avgRGB, 10000.0f).x;

	// Calculate BT.2390 Knee Start (KS) point
	float KS = 1.5 * target_PQ - 0.5 * maxCLL_PQ;
	KS = max(0.0, KS); // Knee Start cannot be negative

	float E2 = E1;

	// Apply the Hermite Spline roll-off if the pixel is brighter than the Knee
	if (E1 > KS)
	{
		// max(1e-6, ...) prevents division by zero if maxCLL_PQ happens to equal KS
		float T = (E1 - KS) / max(1e-6, maxCLL_PQ - KS);
		float T2 = T * T;
		float T3 = T2 * T;

		E2 = (2.0 * T3 - 3.0 * T2 + 1.0) * KS +
			 (T3 - 2.0 * T2 + T) * (maxCLL_PQ - KS) +
			 (-2.0 * T3 + 3.0 * T2) * target_PQ;
	}

	// Convert the tone-mapped PQ value back to linear light
	float linearMapped = ST2084ToLinear(E2, 10000.0f).x;

	// 8. Scale the original RGB channels equally to preserve the exact color hue
	float3 mappedColor = color * (linearMapped / avgRGB);

	return mappedColor;
}

float pl_smoothstep(float edge0, float edge1, float x)
{
	float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// --- ST 2094-10 EETF Tone Mapping Function
float3 ST209410Tonemap(float3 color)
{
	if (displayMaxNits >= maxCLL)
		return color;

	float src_min = LinearToST2084(MasteringMinLuminanceNits, 10000.0f).x;
	float src_max = LinearToST2084(maxCLL, 10000.0f).x;
	float src_avg = LinearToST2084(maxFALL, 10000.0f).x;
	float dst_min = LinearToST2084(0.0f, 10000.0f).x;
	float dst_max = LinearToST2084(displayMaxNits, 10000.0f).x;

	const float min_knee = 0.1f;
	const float max_knee = 0.8f;
	const float def_knee = 0.4f;
	const float knee_adaptation = 0.4f;

	const float src_knee_min = lerp(src_min, src_max, min_knee);
	const float src_knee_max = lerp(src_min, src_max, max_knee);
	const float dst_knee_min = lerp(dst_min, dst_max, min_knee);
	const float dst_knee_max = lerp(dst_min, dst_max, max_knee);

	float src_knee = (maxFALL > 0.0f) ? src_avg : lerp(src_min, src_max, def_knee);
	src_knee = clamp(src_knee, src_knee_min, src_knee_max);

	float target = (src_knee - src_min) / (src_max - src_min);
	float adapted = lerp(dst_min, dst_max, target);

	float tuning = 1.0f - pl_smoothstep(max_knee, def_knee, target) * pl_smoothstep(min_knee, def_knee, target);
	float adaptation = lerp(knee_adaptation, 1.0f, tuning);

	float dst_knee = lerp(src_knee, adapted, adaptation);
	dst_knee = clamp(dst_knee, dst_knee_min, dst_knee_max);

	float out_src_knee = ST2084ToLinear(src_knee, 10000.0f).x;
	float out_dst_knee = ST2084ToLinear(dst_knee, 10000.0f).x;

	float x1 = MasteringMinLuminanceNits;
	float x3 = maxCLL;
	float x2 = out_src_knee;

	float y1 = 0.0f;
	float y3 = displayMaxNits;
	float y2 = out_dst_knee;

	// Build the 3x3 cmat array
	float m00 = x2 * x3 * (y2 - y3);
	float m01 = x1 * x3 * (y3 - y1);
	float m02 = x1 * x2 * (y1 - y2);
	float m10 = x3 * y3 - x2 * y2;
	float m11 = x1 * y1 - x3 * y3;
	float m12 = x2 * y2 - x1 * y1;
	float m20 = x3 - x2;
	float m21 = x1 - x3;
	float m22 = x2 - x1;

	float coef0 = m00 * y1 + m01 * y2 + m02 * y3;
	float coef1 = m10 * y1 + m11 * y2 + m12 * y3;
	float coef2 = m20 * y1 + m21 * y2 + m22 * y3;

	float k = 1.0f / (x3 * y3 * (x1 - x2) + x2 * y2 * (x3 - x1) + x1 * y1 * (x2 - x3));

	float c1 = k * coef0;
	float c2 = k * coef1;
	float c3 = k * coef2;

	float x_nits = 0.2627 * color.r + 0.6780 * color.g + 0.0593 * color.b;

	float y_nits = (c1 + c2 * x_nits) / (1.0f + c3 * x_nits);

	color.rgb *= (x_nits > 0.0f) ? (y_nits / x_nits) : 1.0f;

	return color;
}

float3 RGB_to_ICTCP(float3 rgb_nits)
{
	float3 lms;
	lms.x = (1688.0f * rgb_nits.x + 2146.0f * rgb_nits.y + 262.0f * rgb_nits.z) / 4096.0f;
	lms.y = (683.0f * rgb_nits.x + 2951.0f * rgb_nits.y + 462.0f * rgb_nits.z) / 4096.0f;
	lms.z = (99.0f * rgb_nits.x + 309.0f * rgb_nits.y + 3688.0f * rgb_nits.z) / 4096.0f;

	lms.x = LinearToST2084(lms.x, 10000.0f).x;
	lms.y = LinearToST2084(lms.y, 10000.0f).x;
	lms.z = LinearToST2084(lms.z, 10000.0f).x;

	float3 ictcp;
	ictcp.x = (2048.0f * lms.x + 2048.0f * lms.y) / 4096.0f;
	ictcp.y = (6610.0f * lms.x - 13613.0f * lms.y + 7003.0f * lms.z) / 4096.0f;
	ictcp.z = (17933.0f * lms.x - 17390.0f * lms.y - 543.0f * lms.z) / 4096.0f;

	return ictcp;
}

float3 ICTCP_to_RGB(float3 ictcp)
{
	float3 lms;
	lms.x = 1.0f * ictcp.x + 0.00860904f * ictcp.y + 0.11102963f * ictcp.z;
	lms.y = 1.0f * ictcp.x - 0.00860904f * ictcp.y - 0.11102963f * ictcp.z;
	lms.z = 1.0f * ictcp.x + 0.56003134f * ictcp.y - 0.32062717f * ictcp.z;

	lms.x = ST2084ToLinear(lms.x, 10000.0f).x;
	lms.y = ST2084ToLinear(lms.y, 10000.0f).x;
	lms.z = ST2084ToLinear(lms.z, 10000.0f).x;

	float3 rgb_nits;
	rgb_nits.x = 3.43660669f * lms.x - 2.50645212f * lms.y + 0.06984542f * lms.z;
	rgb_nits.y = -0.79132956f * lms.x + 1.98360045f * lms.y - 0.19227090f * lms.z;
	rgb_nits.z = -0.02594990f * lms.x - 0.09891371f * lms.y + 1.12486361f * lms.z;

	return rgb_nits;
}

float3 ApplyL2Trim(float3 linearRGB)
{
	float3 ictcp = RGB_to_ICTCP(linearRGB);
	float originalI = ictcp.x;

	ictcp.x = max(ictcp.x * TrimSlope + TrimOffset, 0.0f);
	ictcp.x = pow(ictcp.x, max(TrimPower, 0.1f));

	float saturationFactor = max(SaturationGain, 0.0f);
	float highlightWeight = saturate(originalI * 2.0f);

	float targetSaturationForHighlights = 1.0;
	float effectiveSaturation = lerp(saturationFactor, targetSaturationForHighlights, highlightWeight * (1.0 - ChromaWeight));

	ictcp.yz *= effectiveSaturation;

	return ICTCP_to_RGB(ictcp);
}

float4 DolbyVisionTrims(float4 color)
{
	color = LinearToST2084(color, 10000.0f);

	color = pow((color * TrimSlope) + TrimOffset, TrimPower);

	float Y = 0.2627f * color.r + 0.6780f * color.g + 0.0593f * color.b;

	color = color * pow((1.0 + ChromaWeight) * color / Y, SaturationGain);

	color = ST2084ToLinear(color, 10000.0f);

	return color;
}

float4 main(PS_INPUT input) : SV_Target
{
	// Sample texture and convert from PQ to linear
	float4 color = tex.Sample(samp, input.Tex);
	color = ST2084ToLinear(color, 10000.0f); // Convert PQ to Linear space

	if (L2Enabled)
	{
		color = DolbyVisionTrims(color);
	}

	if (selection == 5)
	{
		color.rgb = BT2390Tonemap(color.rgb); // Apply BT.2390 EETF Tone Mapping
		color = LinearToST2084(color, 10000.0f);
		return float4(color.rgb, color.a);
	}

	if (selection == 6)
	{
		color.rgb = ST209410Tonemap(color.rgb); // Apply ST.2094-10 EETF Tone Mapping
		color = LinearToST2084(color, 10000.0f);
		return float4(color.rgb, color.a);
	}

	float baseLum = max(displayMaxNits, MasteringMaxLuminanceNits);
	float effectiveMaxLum = min(baseLum, maxCLL);
	float fallAdjustment = min(baseLum / maxFALL, 1.0);

	// Apply global normalization *before tone mapping*
	color.rgb *= (1.0f / effectiveMaxLum);
	color.rgb = saturate(color.rgb);
	color.rgb *= fallAdjustment;

	// Select the tone mapping function based on `selection`
	if (selection == 1)
	{
		color.rgb = ACESFilmTonemap(color.rgb); // Apply ACES Tone Mapping
	}
	else if (selection == 2)
	{
		color.rgb = ReinhardTonemap(color.rgb); // Apply Reinhard Tone Mapping
	}
	else if (selection == 3)
	{
		color.rgb = HabelTonemap(color.rgb); // Apply Habel Tone Mapping
	}
	else if (selection == 4)
	{
		color.rgb = MobiusTonemap(color.rgb); // Apply Möbius Tone Mapping
	}
	else
	{
		color.rgb = ACESFilmTonemap(color.rgb); // Default fallback to ACES
	}

	// Scale to display peak brightness after tone mapping
	color.rgb *= displayMaxNits;

	// Convert back from linear to PQ color space
	color = LinearToST2084(color, 10000.0f); // Convert Linear to PQ

	return float4(color.rgb, color.a); // Final output
}
