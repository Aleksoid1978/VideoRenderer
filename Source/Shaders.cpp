/*
* (C) 2019 see Authors.txt
*
* This file is part of MPC-BE.
*
* MPC-BE is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
* (at your option) any later version.
*
* MPC-BE is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*/

#include "stdafx.h"
#include <D3Dcompiler.h>
#include "Helper.h"
#include "resource.h"
#include "Shaders.h"

HRESULT CompileShader(const CStringA& srcCode, const D3D_SHADER_MACRO* pDefines, LPCSTR pTarget, ID3DBlob** ppShaderBlob)
{
	//ASSERT(*ppShaderBlob == nullptr);

	static HMODULE s_hD3dcompilerDll = LoadLibraryW(L"d3dcompiler_47.dll");
	static pD3DCompile s_fnD3DCompile = nullptr;

	if (s_hD3dcompilerDll && !s_fnD3DCompile) {
		s_fnD3DCompile = (pD3DCompile)GetProcAddress(s_hD3dcompilerDll, "D3DCompile");
	}

	if (!s_fnD3DCompile) {
		return E_FAIL;
	}

	ID3DBlob* pErrorBlob = nullptr;
	HRESULT hr = s_fnD3DCompile(srcCode, srcCode.GetLength(), nullptr, pDefines, nullptr, "main", pTarget, 0, 0, ppShaderBlob, &pErrorBlob);

	if (FAILED(hr)) {
		SAFE_RELEASE(*ppShaderBlob);

		if (pErrorBlob) {
			CStringA strErrorMsgs((char*)pErrorBlob->GetBufferPointer(), pErrorBlob->GetBufferSize());
			DLog(strErrorMsgs);
		} else {
			DLog(L"Unexpected compiler error");
		}
	}

	SAFE_RELEASE(pErrorBlob);

	return hr;
}

const char correct_ST2084[] =
	"#define ST2084_m1 ((2610.0 / 4096.0) / 4.0)\n"
	"#define ST2084_m2 ((2523.0 / 4096.0) * 128.0)\n"
	"#define ST2084_c1 ( 3424.0 / 4096.0)\n"
	"#define ST2084_c2 ((2413.0 / 4096.0) * 32.0)\n"
	"#define ST2084_c3 ((2392.0 / 4096.0) * 32.0)\n"
	"#define SRC_LUMINANCE_PEAK     10000.0\n"
	"#define DISPLAY_LUMINANCE_PEAK 125.0\n"
	"\n"
	"inline float4 correct_ST2084(float4 pixel)\n"
	"{\n"
		"pixel = saturate(pixel);\n" // use saturate(), because pow() can not take negative values
		// ST2084 to Linear
		"pixel.rgb = pow(pixel.rgb, 1.0 / ST2084_m2);\n"
		"pixel.rgb = max(pixel.rgb - ST2084_c1, 0.0) / (ST2084_c2 - ST2084_c3 * pixel.rgb);\n"
		"pixel.rgb = pow(pixel.rgb, 1.0 / ST2084_m1);\n"
		// Peak luminance
		"pixel.rgb = pixel.rgb * (SRC_LUMINANCE_PEAK / DISPLAY_LUMINANCE_PEAK);\n"
		// HDR tone mapping
		"pixel.rgb = ToneMappingHable(pixel.rgb);\n"
		// Colorspace Gamut Conversion
		"pixel.rgb = Colorspace_Gamut_Conversion_2020_to_709(pixel.rgb);\n"
		// Linear to sRGB
		"pixel.rgb = saturate(pixel.rgb);\n"
		"pixel.rgb = pow(pixel.rgb, 1.0 / 2.2);\n"
		"return pixel;\n"
	"}\n";

const char correct_HLG[] =
	"#define SRC_LUMINANCE_PEAK     1000.0\n"
	"#define DISPLAY_LUMINANCE_PEAK 80.0\n"
	"\n"
	"inline float inverse_HLG(float x)\n"
	"{\n"
		"const float B67_a = 0.17883277;\n"
		"const float B67_b = 0.28466892;\n"
		"const float B67_c = 0.55991073;\n"
		"const float B67_inv_r2 = 4.0;\n"
		"if (x <= 0.5)\n"
			"x = x * x * B67_inv_r2;\n"
		"else\n"
			"x = exp((x - B67_c) / B67_a) + B67_b;\n"
		"return x;\n"
	"}\n"
	"\n"
	"inline float4 correct_HLG(float4 pixel)\n"
	"{\n"
		// HLG to Linear
		"pixel.r = inverse_HLG(pixel.r);\n"
		"pixel.g = inverse_HLG(pixel.g);\n"
		"pixel.b = inverse_HLG(pixel.b);\n"
		"pixel.rgb /= 12.0;\n"
		// HDR tone mapping
		"pixel.rgb = ToneMappingHable(pixel.rgb);\n"
		// Peak luminance
		"pixel.rgb = pixel.rgb * (SRC_LUMINANCE_PEAK / DISPLAY_LUMINANCE_PEAK);\n"
		// Linear to sRGB
		"pixel.rgb = saturate(pixel.rgb);\n"
		"pixel.rgb = pow(pixel.rgb, 1.0 / 2.2);\n"
		"return pixel;\n"
	"}\n";

HRESULT GetShaderConvertColor(const bool bDX11, const FmtConvParams_t fmtParams, const int iHdr, ID3DBlob** ppCode)
{
	CStringA code;

	HRESULT hr = S_OK;
	LPVOID data;
	DWORD size;

	if (iHdr == 1 || iHdr == 2) {
		hr = GetDataFromResource(data, size, IDF_HLSL_HDR_TONE_MAPPING);
		if (S_OK == hr) {
			code.Append((LPCSTR)data, size);
			code.AppendChar('\n');

			if (iHdr == 1) {
				hr = GetDataFromResource(data, size, IDF_HLSL_COLORSPACE_GAMUT_CONV);
				if (S_OK == hr) {
					code.Append((LPCSTR)data, size);
					code.AppendChar('\n');
					code.Append(correct_ST2084);
				}
			} else {
				code.Append(correct_HLG);
			}
		}
	}

	if (bDX11) {
		const int planes = fmtParams.pDX11Planes ? (fmtParams.pDX11Planes->FmtPlane3 ? 3 : 2) : 1;

		switch (planes) {
		case 1:
			code.Append("Texture2D tex : register(t0);\n");
			break;
		case 2:
			code.Append("Texture2D texY : register(t0);\n"
						"Texture2D texUV : register(t1);\n");
			break;
		case 3:
			code.Append("Texture2D texY : register(t0);\n"
						"Texture2D texV : register(t1);\n"
						"Texture2D texU : register(t2);\n");
			break;
		}

		code.Append("SamplerState samp : register(s0);\n"
					"SamplerState sampL : register(s1);\n");

		code.Append("cbuffer PS_COLOR_TRANSFORM : register(b0) {"
						"float3 cm_r;" // NB: sizeof(float3) == sizeof(float4)
						"float3 cm_g;"
						"float3 cm_b;"
						"float3 cm_c;"
					"};\n");

		if (fmtParams.cformat == CF_YUY2) {
			code.Append("cbuffer PS_TEX_DIMENSIONS : register(b4) {\n"
							"float width;\n"
							"float height;\n"
							"float dx;\n"
							"float dy;\n"
						"};\n");
		}

		code.Append("struct PS_INPUT {"
						"float4 Pos : SV_POSITION;"
						"float2 Tex : TEXCOORD;"
					"};\n");

		code.Append("\nfloat4 main(PS_INPUT input) : SV_Target\n{\n");

		switch (planes) {
		case 1:
			code.Append("float4 color = tex.Sample(samp, input.Tex);\n");
			if (fmtParams.cformat == CF_YUY2) {
				code.Append("if (fmod(input.Tex.x*width, 2) < 1.0) {\n"
								"color = float4(color[0], color[1], color[3], 0);\n"
							"} else {\n"
								//"color = float4(color[2], color[1], color[3], 0);\n" // nearest neighbor
								"float2 chroma = (color.yw + tex.Sample(samp, input.Tex + float2(dx, 0)).yw) * 0.5;\n"
								"color = float4(color[2], chroma, 0);\n" // linear
							"}\n");
			}
			break;
		case 2:
			code.Append("float colorY = texY.Sample(samp, input.Tex).r;\n"
						"float2 colorUV = texUV.Sample(sampL, input.Tex).rg; \n"
						"float4 color = float4(colorY, colorUV, 0);\n");
			break;
		case 3:
			code.Append("float colorY = texY.Sample(samp, input.Tex).r;\n"
						"float colorU = texU.Sample(sampL, input.Tex).r\n"
						"float colorV = texV.Sample(sampL, input.Tex).r;\n"
						"float4 color = float4(colorY, colorU, colorV, 0);\n");
			break;
		}

		code.Append("color.rgb = float3(mul(cm_r, color.rgb), mul(cm_g, color.rgb), mul(cm_b, color.rgb)) + cm_c;\n");

	}
	else {
		const int planes = fmtParams.pDX9Planes ? (fmtParams.pDX9Planes->FmtPlane3 ? 3 : 2) : 1;

		switch (planes) {
		case 1:
			code.Append("sampler s0 : register(s0);\n");
			break;
		case 2:
			code.Append("sampler sY : register(s0);\n"
						"sampler sUV : register(s1);\n");
			break;
		case 3:
			code.Append("sampler sY : register(s0);\n"
						"sampler sV : register(s1);\n"
						"sampler sU : register(s2);\n");
			break;
		}

		code.Append("float4 cm_r : register(c0);\n"
					"float4 cm_g : register(c1);\n"
					"float4 cm_b : register(c2);\n"
					"float3 cm_c : register(c3);\n");

		if (fmtParams.cformat == CF_YUY2) {
			code.Append("float4 p4 : register(c4);\n"
						"#define width  (p4[0])\n"
						"#define height (p4[1])\n"
						"#define dx     (p4[2])\n"
						"#define dy     (p4[3])\n");
		}

		code.Append("float4 main(float2 tex : TEXCOORD0) : COLOR\n"
					"{\n");

		switch (planes) {
		case 1:
			code.Append("float4 color = tex2D(s0, tex);\n");
			if (fmtParams.cformat == CF_YUY2) {
				code.Append("if (fmod(tex.x*width, 2) < 1.0) {\n"
								"color = float4(color[2], color[1], color[3], 0);\n"
							"} else {\n"
								//"color = float4(color[0], color[1], color[3], 0);\n" // nearest neighbor
								"float2 chroma = (color.yw + tex2D(s0, tex + float2(dx, 0)).yw) * 0.5;\n"
								"color = float4(color[0], chroma, 0);\n" // linear
							"}\n");
			}
			break;
		case 2:
			code.Append("float colorY = tex2D(sY, tex).r;\n"
						"float3 colorUV = tex2D(sUV, tex).rga;\n"
						"float4 color = float4(colorY, colorUV);\n");
			break;
		case 3:
			code.Append("float colorY = tex2D(sY, tex).r;\n"
						"float colorU = tex2D(sU, tex).r;\n"
						"float colorV = tex2D(sV, tex).r;\n"
						"float4 color = float4(colorY, colorU, colorV, 0);\n");
			break;
		}

		code.Append("color.rgb = float3(mul(cm_r, color), mul(cm_g, color), mul(cm_b, color)) + cm_c;\n");
	}

	if (iHdr == 1) {
		code.Append("color = correct_ST2084(color);\n");
	}
	else if (iHdr == 2) {
		code.Append("color = correct_HLG(color);\n");
	}

	code.Append("return color;\n}");

	LPCSTR target = bDX11 ? "ps_4_0" : "ps_3_0";

	return CompileShader(code, nullptr, target, ppCode);
}
