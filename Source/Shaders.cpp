/*
* (C) 2019-2020 see Authors.txt
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

HRESULT CompileShader(const std::string& srcCode, const D3D_SHADER_MACRO* pDefines, LPCSTR pTarget, ID3DBlob** ppShaderBlob)
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
	HRESULT hr = s_fnD3DCompile(srcCode.c_str(), srcCode.size(), nullptr, pDefines, nullptr, "main", pTarget, 0, 0, ppShaderBlob, &pErrorBlob);

	if (FAILED(hr)) {
		SAFE_RELEASE(*ppShaderBlob);

		if (pErrorBlob) {
			std::string strErrorMsgs((char*)pErrorBlob->GetBufferPointer(), pErrorBlob->GetBufferSize());
			DLog(A2WStr(strErrorMsgs));
		} else {
			DLog(L"Unexpected compiler error");
		}
	}

	SAFE_RELEASE(pErrorBlob);

	return hr;
}

const char code_ST2084[] =
	"#define ST2084_m1 ((2610.0 / 4096.0) / 4.0)\n"
	"#define ST2084_m2 ((2523.0 / 4096.0) * 128.0)\n"
	"#define ST2084_c1 ( 3424.0 / 4096.0)\n"
	"#define ST2084_c2 ((2413.0 / 4096.0) * 32.0)\n"
	"#define ST2084_c3 ((2392.0 / 4096.0) * 32.0)\n"
	"#define SRC_LUMINANCE_PEAK     10000.0\n"
	"#define DISPLAY_LUMINANCE_PEAK 125.0\n";

const char code_HLG[] =
	"#define SRC_LUMINANCE_PEAK     1000.0\n"
	"#define DISPLAY_LUMINANCE_PEAK 80.0\n"
	"\n"
	"inline float3 inverse_HLG(float3 x)\n"
	"{\n"
		"const float B67_a = 0.17883277;\n"
		"const float B67_b = 0.28466892;\n"
		"const float B67_c = 0.55991073;\n"
		"const float B67_inv_r2 = 4.0;\n"
		"x = (x <= 0.5)\n"
			"? x * x * B67_inv_r2\n"
			": exp((x - B67_c) / B67_a) + B67_b;\n"
		"return x;\n"
	"}\n";

HRESULT GetShaderConvertColor(
	const bool bDX11,
	const UINT texW, UINT texH,
	const FmtConvParams_t& fmtParams,
	const DXVA2_ExtendedFormat exFmt,
	const int chromaScaling,
	ID3DBlob** ppCode)
{
	DLog(L"GetShaderConvertColor() started for {} {}x{} extfmt:{:#010x} chroma:{}", fmtParams.str, texW, texH, exFmt.value, chromaScaling);

	std::string code;
	HRESULT hr = S_OK;
	LPVOID data;
	DWORD size;

	bool isHDR = (exFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084 || exFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG);

	if (isHDR) {
		hr = GetDataFromResource(data, size, IDF_HLSL_HDR_TONE_MAPPING);
		if (S_OK == hr) {
			code.append((LPCSTR)data, size);
			code += '\n';

			if (exFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084) {
				hr = GetDataFromResource(data, size, IDF_HLSL_COLORSPACE_GAMUT_CONV);
				if (S_OK == hr) {
					code.append((LPCSTR)data, size);
					code += '\n';
					code.append(code_ST2084);
					DLog(L"GetShaderConvertColor() add code for HDR ST2084");
				}
			} else { // if (exFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG)
				code.append(code_HLG);
				DLog(L"GetShaderConvertColor() add code for HDR HLG");
			}
		}
	}

	const int planes = fmtParams.pDX9Planes ? (fmtParams.pDX9Planes->FmtPlane3 ? 3 : 2) : 1;
	ASSERT(planes == (fmtParams.pDX11Planes ? (fmtParams.pDX11Planes->FmtPlane3 ? 3 : 2) : 1));
	DLog(L"GetShaderConvertColor() frame consists of {} planes", planes);

	code += fmt::format("#define w {}\n", (fmtParams.cformat == CF_YUY2) ? texW * 2 : texW);
	code += fmt::format("#define dx {:.15f}\n", 1.0 / texW);
	code += fmt::format("static const float2 wh = {{{}, {}}};\n", (fmtParams.cformat == CF_YUY2) ? texW*2 : texW, texH);
	code += fmt::format("static const float2 dxdy = {{{:.15f}, {:.15f}}};\n", 1.0 / texW, 1.0 / texH);

	if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 422) {
		code.append("#define CATMULLROM_05(c0,c1,c2,c3) (9*(c1+c2)-(c0+c3))*0.0625\n");
	}

	char* strChromaPos = "";
	char* strChromaPos2 = "";
	if (fmtParams.Subsampling == 420) {
		switch (exFmt.VideoChromaSubsampling) {
		case DXVA2_VideoChromaSubsampling_Cosited:
			strChromaPos = "+dxdy*0.5";
			strChromaPos2 = "+float2(-0.25,-0.25)";
			DLog(L"GetShaderConvertColor() set chroma location Co-sited");
			break;
		case DXVA2_VideoChromaSubsampling_MPEG1:
			//strChromaPos = "";
			strChromaPos2 = "+float2(-0.5,-0.5)";
			DLog(L"GetShaderConvertColor() set chroma location MPEG-1");
			break;
		case DXVA2_VideoChromaSubsampling_MPEG2:
		default:
			strChromaPos = "+float2(dx*0.5,0)";
			strChromaPos2 = "+float2(-0.25,-0.5)";
			DLog(L"GetShaderConvertColor() set chroma location MPEG-2");
		}
	}
	else if (fmtParams.Subsampling == 422) {
		strChromaPos = "+float2(dx*0.5,0)";
		DLog(L"GetShaderConvertColor() set chroma location for YUV 4:2:2");
	}

	if (bDX11) {
		switch (planes) {
		case 1:
			code.append("Texture2D tex : register(t0);\n");
			break;
		case 2:
			code.append("Texture2D texY : register(t0);\n"
						"Texture2D texUV : register(t1);\n");
			break;
		case 3:
			code.append("Texture2D texY : register(t0);\n"
						"Texture2D texV : register(t1);\n"
						"Texture2D texU : register(t2);\n");
			break;
		}

		code.append("SamplerState samp : register(s0);\n"
					"SamplerState sampL : register(s1);\n");

		code.append("cbuffer PS_COLOR_TRANSFORM : register(b0) {"
						"float3 cm_r;" // NB: sizeof(float3) == sizeof(float4)
						"float3 cm_g;"
						"float3 cm_b;"
						"float3 cm_c;"
					"};\n");

		code.append("struct PS_INPUT {"
						"float4 Pos : SV_POSITION;"
						"float2 Tex : TEXCOORD;"
					"};\n");

		code.append("\nfloat4 main(PS_INPUT input) : SV_Target\n{\n");

		switch (planes) {
		case 1:
			code.append("float4 color = tex.Sample(samp, input.Tex);\n");
			if (fmtParams.cformat == CF_YUY2) {
				code.append("if (fmod(input.Tex.x*w, 2) < 1.0) {\n"
					"color = float4(color[0], color[1], color[3], 0);\n"
					"} else {\n");
				if (chromaScaling == CHROMA_CatmullRom) {
					code.append("float2 c0 = tex.Sample(samp, input.Tex, int2(-1, 0)).yw;\n"
						"float2 c1 = color.yw;\n"
						"float2 c2 = tex.Sample(samp, input.Tex, int2(1, 0)).yw;\n"
						"float2 c3 = tex.Sample(samp, input.Tex, int2(2, 0)).yw;\n"
						"float2 chroma = CATMULLROM_05(c0,c1,c2,c3);\n");
				} else { // linear
					code.append("float2 chroma = (color.yw + tex.Sample(samp, input.Tex, int2(1, 0)).yw) * 0.5;\n");
				}
				code.append("color = float4(color[2], chroma, 0);\n"
					"}\n");
			}
			break;
		case 2:
			code.append("float colorY = texY.Sample(samp, input.Tex).r;\n"
				"float2 colorUV;\n");
			if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 420) {
				code += fmt::format("float2 t = frac(input.Tex * (wh*0.5)){};\n", strChromaPos2); // Very strange, but it works.
				code.append("float2 t2 = t * t;\n"
					"float2 t3 = t * t2;\n"
					"float2 w0 = t2 - (t3 + t) / 2;\n"
					"float2 w1 = t3 * 1.5 + 1 - t2 * 2.5;\n"
					"float2 w2 = t2 * 2 + t / 2 - t3 * 1.5;\n"
					"float2 w3 = (t3 - t2) / 2;\n");
				for (int y = 0; y < 4; y++) {
					for (int x = 0; x < 4; x++) {
						code += fmt::format("float2 c%d%d = texUV.Sample(samp, input.Tex, int2({}, {})).rg;\n", x, y, x-1, y-1);
					}
				}
				code.append(
					"float2 Q0 = c00 * w0.y + c01 * w1.y + c02 * w2.y + c03 * w3.y;\n"
					"float2 Q1 = c10 * w0.y + c11 * w1.y + c12 * w2.y + c13 * w3.y;\n"
					"float2 Q2 = c20 * w0.y + c21 * w1.y + c22 * w2.y + c23 * w3.y;\n"
					"float2 Q3 = c30 * w0.y + c31 * w1.y + c32 * w2.y + c33 * w3.y;\n"
					"colorUV = Q0 * w0.x + Q1 * w1.x + Q2 * w2.x + Q3 * w3.x;\n");
			}
			else if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 422) {
				code.append(
					"if (fmod(input.Tex.x*w, 2) < 1.0) {\n"
						"colorUV = texUV.Sample(samp, input.Tex).rg;\n"
					"} else {\n"
						"input.Tex.x -= dx;\n"
						"float2 c0 = texUV.Sample(samp, input.Tex + float2(-2*dx, 0)).rg;\n"
						"float2 c1 = texUV.Sample(samp, input.Tex).rg;\n"
						"float2 c2 = texUV.Sample(samp, input.Tex + float2(2*dx, 0)).rg;\n"
						"float2 c3 = texUV.Sample(samp, input.Tex + float2(4*dx, 0)).rg;\n"
						"colorUV = CATMULLROM_05(c0,c1,c2,c3);\n"
					"}\n");
			}
			else { // CHROMA_Bilinear or YUV 4:4:4
				code += fmt::format("colorUV = texUV.Sample(sampL, input.Tex{}).rg;\n", strChromaPos);
			}
			code.append("float4 color = float4(colorY, colorUV, 0);\n");
			break;
		case 3:
			code.append("float colorY = texY.Sample(samp, input.Tex).r;\n"
				"float2 colorUV;\n");
			if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 420) {
				code += fmt::format("float2 t = frac(input.Tex * (wh*0.5)){};\n", strChromaPos2); // I don't know why, but it works.
				code.append("float2 t2 = t * t;\n"
					"float2 t3 = t * t2;\n"
					"float2 w0 = t2 - (t3 + t) / 2;\n"
					"float2 w1 = t3 * 1.5 + 1 - t2 * 2.5;\n"
					"float2 w2 = t2 * 2 + t / 2 - t3 * 1.5;\n"
					"float2 w3 = (t3 - t2) / 2;\n"
					"float2 c00,c10,c20,c30,c01,c11,c21,c31,c02,c12,c22,c32,c03,c13,c23,c33;\n");
				for (int y = 0; y < 4; y++) {
					for (int x = 0; x < 4; x++) {
						code += fmt::format("c{}{}[0] = texU.Sample(samp, input.Tex, int2({}, {})).r;\n", x, y, x-1, y-1);
						code += fmt::format("c{}{}[1] = texV.Sample(samp, input.Tex, int2({}, {})).r;\n", x, y, x-1, y-1);
					}
				}
				code.append(
					"float2 Q0 = c00 * w0.y + c01 * w1.y + c02 * w2.y + c03 * w3.y;\n"
					"float2 Q1 = c10 * w0.y + c11 * w1.y + c12 * w2.y + c13 * w3.y;\n"
					"float2 Q2 = c20 * w0.y + c21 * w1.y + c22 * w2.y + c23 * w3.y;\n"
					"float2 Q3 = c30 * w0.y + c31 * w1.y + c32 * w2.y + c33 * w3.y;\n"
					"colorUV = Q0 * w0.x + Q1 * w1.x + Q2 * w2.x + Q3 * w3.x;\n");
			}
			else if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 422) {
				code.append(
					"if (fmod(input.Tex.x*w, 2) < 1.0) {\n"
						"colorUV[0] = texU.Sample(samp, input.Tex).r;\n"
						"colorUV[1] = texV.Sample(samp, input.Tex).r;\n"
					"} else {\n"
						"input.Tex.x -= dx;\n"
						"float2 c0,c1,c2,c3;\n"
						"c0[0] = texU.Sample(samp, input.Tex + float2(-2*dx, 0)).r;\n"
						"c1[0] = texU.Sample(samp, input.Tex).r;\n"
						"c2[0] = texU.Sample(samp, input.Tex + float2(2*dx, 0)).r;\n"
						"c3[0] = texU.Sample(samp, input.Tex + float2(4*dx, 0)).r;\n"
						"c0[1] = texV.Sample(samp, input.Tex + float2(-2*dx, 0)).r;\n"
						"c1[1] = texV.Sample(samp, input.Tex).r;\n"
						"c2[1] = texV.Sample(samp, input.Tex + float2(2*dx, 0)).r;\n"
						"c3[1] = texV.Sample(samp, input.Tex + float2(4*dx, 0)).r;\n"
						"colorUV = CATMULLROM_05(c0,c1,c2,c3);\n"
					"}\n");
			}
			else { // CHROMA_Bilinear or YUV 4:4:4
				code += fmt::format("colorUV[0] = texU.Sample(sampL, input.Tex{}).r;\n", strChromaPos);
				code += fmt::format("colorUV[1] = texV.Sample(sampL, input.Tex{}).r;\n", strChromaPos);
			}
			code.append("float4 color = float4(colorY, colorUV, 0);\n");
			break;
		}
	}
	else {
		switch (planes) {
		case 1:
			code.append("sampler s0 : register(s0);\n");
			break;
		case 2:
			code.append("sampler sY : register(s0);\n"
						"sampler sUV : register(s1);\n");
			break;
		case 3:
			code.append("sampler sY : register(s0);\n"
						"sampler sV : register(s1);\n"
						"sampler sU : register(s2);\n");
			break;
		}

		code.append("float3 cm_r : register(c0);\n"
					"float3 cm_g : register(c1);\n"
					"float3 cm_b : register(c2);\n"
					"float3 cm_c : register(c3);\n");

		switch (planes) {
		case 1:
			code.append("\nfloat4 main(float2 tex : TEXCOORD0) : COLOR\n"
				"{\n");
			code.append("float4 color = tex2D(s0, tex);\n");
			if (fmtParams.cformat == CF_YUY2) {
				code.append("if (fmod(tex.x*w, 2) < 1.0) {\n"
					"color = float4(color[2], color[1], color[3], 0);\n"
					"} else {\n");
				if (chromaScaling == CHROMA_CatmullRom) {
					code.append("float2 c0 = tex2D(s0, tex + float2(-dx, 0)).yw;\n"
						"float2 c1 = color.yw;\n"
						"float2 c2 = tex2D(s0, tex + float2(dx, 0)).yw;\n"
						"float2 c3 = tex2D(s0, tex + float2(2*dx, 0)).yw;\n"
						"float2 chroma = CATMULLROM_05(c0,c1,c2,c3);\n");
				} else { // linear
					code.append("float2 chroma = (color.yw + tex2D(s0, tex + float2(dx, 0)).yw) * 0.5;\n");
				}
				code.append("color = float4(color[0], chroma, 0);\n"
					"}\n");
			}
			break;
		case 2:
			code.append("\nfloat4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1) : COLOR\n{\n");
			code.append("float colorY = tex2D(sY, t0).r;\n"
				"float2 colorUV;\n");
			if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 420) {
				code.append("float2 pos = t0 * (wh*0.5);"
					"float2 t = frac(pos);\n"
					"pos -= t;\n");
				code += fmt::format("t = t{};\n", strChromaPos2);
				code.append("float2 t2 = t * t;\n"
					"float2 t3 = t * t2;\n"
					"float2 w0 = t2 - (t3 + t) / 2;\n"
					"float2 w1 = t3 * 1.5 + 1 - t2 * 2.5;\n"
					"float2 w2 = t2 * 2 + t / 2 - t3 * 1.5;\n"
					"float2 w3 = (t3 - t2) / 2;\n");
				for (int y = 0; y < 4; y++) {
					for (int x = 0; x < 4; x++) {
						if (fmtParams.cformat == CF_NV12) {
							code += fmt::format("float2 c{}{} = tex2D(sUV, (pos + float2({}+0.5, {}+0.5))*dxdy*2).ra;\n", x, y, x - 1, y - 1);
						} else {
							code += fmt::format("float2 c{}{} = tex2D(sUV, (pos + float2({}+0.5, {}+0.5))*dxdy*2).rg;\n", x, y, x - 1, y - 1);
						}
					}
				}
				code.append(
					"float2 Q0 = c00 * w0.y + c01 * w1.y + c02 * w2.y + c03 * w3.y;\n"
					"float2 Q1 = c10 * w0.y + c11 * w1.y + c12 * w2.y + c13 * w3.y;\n"
					"float2 Q2 = c20 * w0.y + c21 * w1.y + c22 * w2.y + c23 * w3.y;\n"
					"float2 Q3 = c30 * w0.y + c31 * w1.y + c32 * w2.y + c33 * w3.y;\n"
					"colorUV = Q0 * w0.x + Q1 * w1.x + Q2 * w2.x + Q3 * w3.x;\n");
			}
			else if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 422) {
				code.append(
					"if (fmod(t0.x*w, 2) < 1.0) {\n"
						"t0.x += 0.5*dx;\n"
						"colorUV = tex2D(sUV, t0).rg;\n"
					"} else {\n"
						"t0.x -= 0.5*dx;\n"
						"float2 c0 = tex2D(sUV, t0 + float2(-2*dx, 0)).rg;\n"
						"float2 c1 = tex2D(sUV, t0).rg;\n"
						"float2 c2 = tex2D(sUV, t0 + float2(2*dx, 0)).rg;\n"
						"float2 c3 = tex2D(sUV, t0 + float2(4*dx, 0)).rg;\n"
						"colorUV = CATMULLROM_05(c0,c1,c2,c3);\n"
					"}\n");
			}
			else { // CHROMA_Bilinear or YUV 4:4:4
				if (fmtParams.cformat == CF_NV12) {
					code.append("colorUV = tex2D(sUV, t1).ra;\n");
				} else {
					code.append("colorUV = tex2D(sUV, t1).rg;\n");
				}
			}
			code.append("float4 color = float4(colorY, colorUV, 0);\n");
			break;
		case 3:
			code.append("\nfloat4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1) : COLOR\n"
				"{\n");
			code.append("float colorY = tex2D(sY, t0).r;\n"
				"float2 colorUV;\n");
			if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 420) {
				code.append("float2 pos = t0 * (wh*0.5);"
					"float2 t = frac(pos);\n"
					"pos -= t;\n");
				code += fmt::format("t = t{};\n", strChromaPos2);
				code.append("float2 t2 = t * t;\n"
					"float2 t3 = t * t2;\n"
					"float2 w0 = t2 - (t3 + t) / 2;\n"
					"float2 w1 = t3 * 1.5 + 1 - t2 * 2.5;\n"
					"float2 w2 = t2 * 2 + t / 2 - t3 * 1.5;\n"
					"float2 w3 = (t3 - t2) / 2;\n"
					"float2 c00,c10,c20,c30,c01,c11,c21,c31,c02,c12,c22,c32,c03,c13,c23,c33;\n");
				for (int y = 0; y < 4; y++) {
					for (int x = 0; x < 4; x++) {
						code += fmt::format("c{}{}[0] = tex2D(sU, (pos + float2({}+0.5, {}+0.5))*dxdy*2).r;\n", x, y, x-1, y-1);
						code += fmt::format("c{}{}[1] = tex2D(sV, (pos + float2({}+0.5, {}+0.5))*dxdy*2).r;\n", x, y, x-1, y-1);
					}
				}
				code.append(
					"float2 Q0 = c00 * w0.y + c01 * w1.y + c02 * w2.y + c03 * w3.y;\n"
					"float2 Q1 = c10 * w0.y + c11 * w1.y + c12 * w2.y + c13 * w3.y;\n"
					"float2 Q2 = c20 * w0.y + c21 * w1.y + c22 * w2.y + c23 * w3.y;\n"
					"float2 Q3 = c30 * w0.y + c31 * w1.y + c32 * w2.y + c33 * w3.y;\n"
					"colorUV = Q0 * w0.x + Q1 * w1.x + Q2 * w2.x + Q3 * w3.x;\n");
			}
			else if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 422) {
				code.append(
					"if (fmod(t0.x*w, 2) < 1.0) {\n"
						"t0.x += 0.5*dx;\n"
						"colorUV[0] = tex2D(sU, t0).r;\n"
						"colorUV[1] = tex2D(sV, t0).r;\n"
					"} else {\n"
						"t0.x -= 0.5*dx;\n"
						"float2 c0,c1,c2,c3;\n"
						"c0[0] = tex2D(sU, t0 + float2(-2*dx, 0)).r;\n"
						"c1[0] = tex2D(sU, t0).r;\n"
						"c2[0] = tex2D(sU, t0 + float2(2*dx, 0)).r;\n"
						"c3[0] = tex2D(sU, t0 + float2(4*dx, 0)).r;\n"
						"c0[1] = tex2D(sV, t0 + float2(-2*dx, 0)).r;\n"
						"c1[1] = tex2D(sV, t0).r;\n"
						"c2[1] = tex2D(sV, t0 + float2(2*dx, 0)).r;\n"
						"c3[1] = tex2D(sV, t0 + float2(4*dx, 0)).r;\n"
						"colorUV = CATMULLROM_05(c0,c1,c2,c3);\n"
					"}\n");
			}
			else { // CHROMA_Bilinear or YUV 4:4:4
				code.append("colorUV[0] = tex2D(sU, t1).r;\n"
					"colorUV[1] = tex2D(sV, t1).r;\n");
			}
			code.append("float4 color = float4(colorY, colorUV, 0);\n");
			break;
		}
	}
	code.append("//convert color\n");
	code.append("color.rgb = float3(mul(cm_r, color), mul(cm_g, color), mul(cm_b, color)) + cm_c;\n");

	if (isHDR) {
		if (exFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084) {
			code.append(
				"color = saturate(color);\n" // use saturate(), because pow() can not take negative values
				// ST2084 to Linear
				"color.rgb = pow(color.rgb, 1.0 / ST2084_m2);\n"
				"color.rgb = max(color.rgb - ST2084_c1, 0.0) / (ST2084_c2 - ST2084_c3 * color.rgb);\n"
				"pixel.rgb = pow(color.rgb, 1.0 / ST2084_m1);\n"
				// Peak luminance
				"color.rgb = color.rgb * (SRC_LUMINANCE_PEAK / DISPLAY_LUMINANCE_PEAK);\n"
				// HDR tone mapping
				"color.rgb = ToneMappingHable(color.rgb);\n"
				// Colorspace Gamut Conversion
				"color.rgb = Colorspace_Gamut_Conversion_2020_to_709(color.rgb);\n"
				// Linear to sRGB
				"color.rgb = saturate(color.rgb);\n"
				"color.rgb = pow(color.rgb, 1.0 / 2.2);\n"
			);
		}
		else { // if (exFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG)
			code.append(
				"color.rgb = inverse_HLG(color.rgb);\n"
				"color.rgb /= 12.0;\n"
				// HDR tone mapping
				"color.rgb = ToneMappingHable(color.rgb);\n"
				// Peak luminance
				"color.rgb = pixel.rgb * (SRC_LUMINANCE_PEAK / DISPLAY_LUMINANCE_PEAK);\n"
				// Linear to sRGB
				"color.rgb = saturate(color.rgb);\n"
				"color.rgb = pow(color.rgb, 1.0 / 2.2);\n"
			);
		}
		// Linear to sRGB
		code.append(
			"color.rgb = saturate(color.rgb);\n"
			"color.rgb = pow(color.rgb, 1.0 / 2.2);\n"
		);
	}

	code.append("return color;\n}");

	LPCSTR target = bDX11 ? "ps_4_0" : "ps_3_0";

	return CompileShader(code, nullptr, target, ppCode);
}
