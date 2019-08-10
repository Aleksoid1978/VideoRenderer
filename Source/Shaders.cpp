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
#include "IVideoRenderer.h"
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

HRESULT GetShaderConvertColor(
	const bool bDX11,
	const UINT texW, UINT texH,
	const FmtConvParams_t& fmtParams,
	const DXVA2_ExtendedFormat exFmt,
	const int chromaScaling,
	ID3DBlob** ppCode)
{
	CStringA code;

	HRESULT hr = S_OK;
	LPVOID data;
	DWORD size;

	if (exFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084 || exFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG) {
		hr = GetDataFromResource(data, size, IDF_HLSL_HDR_TONE_MAPPING);
		if (S_OK == hr) {
			code.Append((LPCSTR)data, size);
			code.AppendChar('\n');

			if (exFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084) {
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

	const int planes = fmtParams.pDX9Planes ? (fmtParams.pDX9Planes->FmtPlane3 ? 3 : 2) : 1;
	ASSERT(planes == (fmtParams.pDX11Planes ? (fmtParams.pDX11Planes->FmtPlane3 ? 3 : 2) : 1));

	code.AppendFormat("#define w %u\n", (fmtParams.cformat == CF_YUY2) ? texW*2 : texW);
	code.AppendFormat("#define h %u\n", texH);
	code.AppendFormat("#define dx %.15f\n", 1.0 / texW);
	code.AppendFormat("#define dy %.15f\n", 1.0 / texH);
	code.Append("static const float2 wh = {w, h};\n");
	code.Append("static const float2 dxdy = {dx, dy};\n");

	if (chromaScaling == CHROMA_CatmullRom) {
		code.Append("#define CATMULLROM_05(c0,c1,c2,c3) (9*(c1+c2)-(c0+c3))*0.0625\n");
		if (fmtParams.Subsampling == 420) {
			code.Append("#define BICATMULLROM_05(c00,c10,c20,c30,c01,c11,c21,c31,c02,c12,c22,c32,c03,c13,c23,c33) (81*(c11+c12+c21+c22) - 9*(c01+c02+c10+c13+c20+c23+c31+c32) + c00+c03+c30+c33)*0.00390625\n");
		}
	}

	char* strChromaPos = "";
	char* strChromaPos2 = "";
	if (fmtParams.Subsampling == 420) {
		switch (exFmt.VideoChromaSubsampling) {
		case DXVA2_VideoChromaSubsampling_Cosited:
			strChromaPos = "+float2(dx*0.5,dy*0.5)";
			strChromaPos2 = "+float2(-0.25,-0.25)";
			break;
		case DXVA2_VideoChromaSubsampling_MPEG1:
			//strChromaPos = "";
			strChromaPos2 = "+float2(-0.5,-0.5)";
			break;
		case DXVA2_VideoChromaSubsampling_MPEG2:
		default:
			strChromaPos = "+float2(dx*0.5,0)";
			strChromaPos2 = "+float2(-0.25,-0.5)";
		}
	}

	if (bDX11) {
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

		code.Append("struct PS_INPUT {"
						"float4 Pos : SV_POSITION;"
						"float2 Tex : TEXCOORD;"
					"};\n");

		code.Append("\nfloat4 main(PS_INPUT input) : SV_Target\n{\n");

		switch (planes) {
		case 1:
			code.Append("float4 color = tex.Sample(samp, input.Tex);\n");
			if (fmtParams.cformat == CF_YUY2) {
				code.Append("if (fmod(input.Tex.x*w, 2) < 1.0) {\n"
					"color = float4(color[0], color[1], color[3], 0);\n"
					"} else {\n");
				if (chromaScaling == CHROMA_CatmullRom) {
					code.Append("float2 c0 = tex.Sample(samp, input.Tex, int2(-1, 0)).yw;\n"
						"float2 c1 = color.yw;\n"
						"float2 c2 = tex.Sample(samp, input.Tex, int2(1, 0)).yw;\n"
						"float2 c3 = tex.Sample(samp, input.Tex, int2(2, 0)).yw;\n"
						"float2 chroma = CATMULLROM_05(c0,c1,c2,c3);\n");
				} else { // linear
					code.Append("float2 chroma = (color.yw + tex.Sample(samp, input.Tex, int2(1, 0)).yw) * 0.5;\n");
				}
				code.Append("color = float4(color[2], chroma, 0);\n"
					"}\n");
			}
			break;
		case 2:
			code.Append("float colorY = texY.Sample(samp, input.Tex).r;\n"
				"float2 colorUV;\n");
			if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 420) {
				code.AppendFormat("float2 t = frac(input.Tex * (wh*0.5))%s;\n", strChromaPos2); // Very strange, but it works.
				code.Append("float2 t2 = t * t;\n"
					"float2 t3 = t * t2;\n"
					"float2 w0 = t2 - (t3 + t) / 2;\n"
					"float2 w1 = t3 * 1.5 + 1 - t2 * 2.5;\n"
					"float2 w2 = t2 * 2 + t / 2 - t3 * 1.5;\n"
					"float2 w3 = (t3 - t2) / 2;\n");
				for (int y = 0; y < 4; y++) {
					for (int x = 0; x < 4; x++) {
						code.AppendFormat("float2 c%d%d = texUV.Sample(samp, input.Tex, int2(%d, %d)).rg;\n", x, y, x-1, y-1);
					}
				}
				code.Append(
					"float2 Q0 = c00 * w0.y + c01 * w1.y + c02 * w2.y + c03 * w3.y;\n"
					"float2 Q1 = c10 * w0.y + c11 * w1.y + c12 * w2.y + c13 * w3.y;\n"
					"float2 Q2 = c20 * w0.y + c21 * w1.y + c22 * w2.y + c23 * w3.y;\n"
					"float2 Q3 = c30 * w0.y + c31 * w1.y + c32 * w2.y + c33 * w3.y;\n"
					"colorUV = Q0 * w0.x + Q1 * w1.x + Q2 * w2.x + Q3 * w3.x;\n");
			} else {
				code.AppendFormat("colorUV = texUV.Sample(sampL, input.Tex%s).rg;\n", strChromaPos);
			}
			code.Append("float4 color = float4(colorY, colorUV, 0);\n");
			break;
		case 3:
			code.Append("float colorY = texY.Sample(samp, input.Tex).r;\n"
				"float2 colorUV;\n");
			if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 420) {
				code.AppendFormat("float2 t = frac(input.Tex * (wh*0.5))%s;\n", strChromaPos2); // I don't know why, but it works.
				code.Append("float2 t2 = t * t;\n"
					"float2 t3 = t * t2;\n"
					"float2 w0 = t2 - (t3 + t) / 2;\n"
					"float2 w1 = t3 * 1.5 + 1 - t2 * 2.5;\n"
					"float2 w2 = t2 * 2 + t / 2 - t3 * 1.5;\n"
					"float2 w3 = (t3 - t2) / 2;\n"
					"float2 c00,c10,c20,c30,c01,c11,c21,c31,c02,c12,c22,c32,c03,c13,c23,c33;\n");
				for (int y = 0; y < 4; y++) {
					for (int x = 0; x < 4; x++) {
						code.AppendFormat("c%d%d[0] = texU.Sample(samp, input.Tex, int2(%d, %d)).r;\n", x, y, x-1, y-1);
						code.AppendFormat("c%d%d[1] = texV.Sample(samp, input.Tex, int2(%d, %d)).r;\n", x, y, x-1, y-1);
					}
				}
				code.Append(
					"float2 Q0 = c00 * w0.y + c01 * w1.y + c02 * w2.y + c03 * w3.y;\n"
					"float2 Q1 = c10 * w0.y + c11 * w1.y + c12 * w2.y + c13 * w3.y;\n"
					"float2 Q2 = c20 * w0.y + c21 * w1.y + c22 * w2.y + c23 * w3.y;\n"
					"float2 Q3 = c30 * w0.y + c31 * w1.y + c32 * w2.y + c33 * w3.y;\n"
					"colorUV = Q0 * w0.x + Q1 * w1.x + Q2 * w2.x + Q3 * w3.x;\n");
			} else {
				code.AppendFormat("colorUV[0] = texU.Sample(sampL, input.Tex%s).r;\n", strChromaPos);
				code.AppendFormat("colorUV[1] = texV.Sample(sampL, input.Tex%s).r;\n", strChromaPos);
			}
			code.Append("float4 color = float4(colorY, colorUV, 0);\n");
			break;
		}

		code.Append("color.rgb = float3(mul(cm_r, color.rgb), mul(cm_g, color.rgb), mul(cm_b, color.rgb)) + cm_c;\n");
	}
	else {
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

		switch (planes) {
		case 1:
			code.Append("float4 main(float2 tex : TEXCOORD0) : COLOR\n"
				"{\n");
			code.Append("float4 color = tex2D(s0, tex);\n");
			if (fmtParams.cformat == CF_YUY2) {
				code.Append("if (fmod(tex.x*w, 2) < 1.0) {\n"
					"color = float4(color[2], color[1], color[3], 0);\n"
					"} else {\n");
				if (chromaScaling == CHROMA_CatmullRom) {
					code.Append("float2 c0 = tex2D(s0, tex + float2(-dx, 0)).yw;\n"
						"float2 c1 = color.yw;\n"
						"float2 c2 = tex2D(s0, tex + float2(dx, 0)).yw;\n"
						"float2 c3 = tex2D(s0, tex + float2(2*dx, 0)).yw;\n"
						"float2 chroma = CATMULLROM_05(c0,c1,c2,c3);\n");
				} else { // linear
					code.Append("float2 chroma = (color.yw + tex2D(s0, tex + float2(dx, 0)).yw) * 0.5;\n");
				}
				code.Append("color = float4(color[0], chroma, 0);\n"
					"}\n");
			}
			break;
		case 2:
			code.Append("float4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1) : COLOR\n{\n");
			code.Append("float colorY = tex2D(sY, t0).r;\n"
				"float3 colorUV;\n");
			if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 420) {
				code.Append("float2 pos = t0 * (wh*0.5);"
					"float2 t = frac(pos);\n"
					"pos -= t;\n");
				code.AppendFormat("t = t%s;\n", strChromaPos2);
				code.Append("float2 t2 = t * t;\n"
					"float2 t3 = t * t2;\n"
					"float2 w0 = t2 - (t3 + t) / 2;\n"
					"float2 w1 = t3 * 1.5 + 1 - t2 * 2.5;\n"
					"float2 w2 = t2 * 2 + t / 2 - t3 * 1.5;\n"
					"float2 w3 = (t3 - t2) / 2;\n");
				for (int y = 0; y < 4; y++) {
					for (int x = 0; x < 4; x++) {
						code.AppendFormat("float3 c%d%d = tex2D(sUV, (pos + float2(%d+0.5, %d+0.5))*dxdy*2).rga;\n", x, y, x-1, y-1);
					}
				}
				code.Append(
					"float3 Q0 = c00 * w0.y + c01 * w1.y + c02 * w2.y + c03 * w3.y;\n"
					"float3 Q1 = c10 * w0.y + c11 * w1.y + c12 * w2.y + c13 * w3.y;\n"
					"float3 Q2 = c20 * w0.y + c21 * w1.y + c22 * w2.y + c23 * w3.y;\n"
					"float3 Q3 = c30 * w0.y + c31 * w1.y + c32 * w2.y + c33 * w3.y;\n"
					"colorUV = Q0 * w0.x + Q1 * w1.x + Q2 * w2.x + Q3 * w3.x;\n");
			} else {
				code.Append("colorUV = tex2D(sUV, t1).rga;\n");
			}
			code.Append("float4 color = float4(colorY, colorUV);\n");
			break;
		case 3:
			code.Append("float4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1) : COLOR\n"
				"{\n");
			code.AppendFormat("float colorY = tex2D(sY, t0).r;\n"
				"float2 colorUV;\n");
			if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 420) {
				code.Append("float2 pos = t0 * (wh*0.5);"
					"float2 t = frac(pos);\n"
					"pos -= t;\n");
				code.AppendFormat("t = t%s;\n", strChromaPos2);
				code.Append("float2 t2 = t * t;\n"
					"float2 t3 = t * t2;\n"
					"float2 w0 = t2 - (t3 + t) / 2;\n"
					"float2 w1 = t3 * 1.5 + 1 - t2 * 2.5;\n"
					"float2 w2 = t2 * 2 + t / 2 - t3 * 1.5;\n"
					"float2 w3 = (t3 - t2) / 2;\n"
					"float2 c00,c10,c20,c30,c01,c11,c21,c31,c02,c12,c22,c32,c03,c13,c23,c33;\n");
				for (int y = 0; y < 4; y++) {
					for (int x = 0; x < 4; x++) {
						code.AppendFormat("c%d%d[0] = tex2D(sU, (pos + float2(%d+0.5, %d+0.5))*dxdy*2).r;\n", x, y, x-1, y-1);
						code.AppendFormat("c%d%d[1] = tex2D(sV, (pos + float2(%d+0.5, %d+0.5))*dxdy*2).r;\n", x, y, x-1, y-1);
					}
				}
				code.Append(
					"float2 Q0 = c00 * w0.y + c01 * w1.y + c02 * w2.y + c03 * w3.y;\n"
					"float2 Q1 = c10 * w0.y + c11 * w1.y + c12 * w2.y + c13 * w3.y;\n"
					"float2 Q2 = c20 * w0.y + c21 * w1.y + c22 * w2.y + c23 * w3.y;\n"
					"float2 Q3 = c30 * w0.y + c31 * w1.y + c32 * w2.y + c33 * w3.y;\n"
					"colorUV = Q0 * w0.x + Q1 * w1.x + Q2 * w2.x + Q3 * w3.x;\n");
			} else {
				code.Append("colorUV[0] = tex2D(sU, t1).r;\n"
					"colorUV[1] = tex2D(sV, t1).r;\n");
			}
			code.Append("float4 color = float4(colorY, colorUV, 0);\n");
			break;
		}

		code.Append("color.rgb = float3(mul(cm_r, color), mul(cm_g, color), mul(cm_b, color)) + cm_c;\n");
	}

	if (exFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084) {
		code.Append("color = correct_ST2084(color);\n");
	}
	else if (exFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG) {
		code.Append("color = correct_HLG(color);\n");
	}

	code.Append("return color;\n}");

	LPCSTR target = bDX11 ? "ps_4_0" : "ps_3_0";

	return CompileShader(code, nullptr, target, ppCode);
}
