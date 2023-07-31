/*
* (C) 2019-2023 see Authors.txt
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

const char code_CatmullRom_weights[] =
	"float2 t2 = t * t;\n"
	"float2 t3 = t * t2;\n"
	"float2 w0 = t2 - (t3 + t) / 2;\n"
	"float2 w1 = t3 * 1.5 + 1 - t2 * 2.5;\n"
	"float2 w2 = t2 * 2 + t / 2 - t3 * 1.5;\n"
	"float2 w3 = (t3 - t2) / 2;\n";

const char code_Bicubic_UV[] =
	"float2 Q0 = c00 * w0.x + c10 * w1.x + c20 * w2.x + c30 * w3.x;\n"
	"float2 Q1 = c01 * w0.x + c11 * w1.x + c21 * w2.x + c31 * w3.x;\n"
	"float2 Q2 = c02 * w0.x + c12 * w1.x + c22 * w2.x + c32 * w3.x;\n"
	"float2 Q3 = c03 * w0.x + c13 * w1.x + c23 * w2.x + c33 * w3.x;\n"
	"colorUV = Q0 * w0.y + Q1 * w1.y + Q2 * w2.y + Q3 * w3.y;\n";


void ShaderGetPixels(
	const bool bDX11,
	const FmtConvParams_t& fmtParams,
	const UINT chromaSubsampling,
	const int chromaScaling,
	const bool blendDeinterlace,
	std::string& code)
{
	int planes = 1;
	if (bDX11) {
		if (fmtParams.pDX11Planes) {
			if (fmtParams.pDX11Planes->FmtPlane3) {
				planes = 3;
			}
			else if (fmtParams.pDX11Planes->FmtPlane2) {
				planes = 2;
			}
		}
	} else {
		if (fmtParams.pDX9Planes) {
			if (fmtParams.pDX9Planes->FmtPlane3) {
				planes = 3;
			}
			else if (fmtParams.pDX9Planes->FmtPlane2) {
				planes = 2;
			}
		}
	}
	DLog(L"ConvertColorShader: frame consists of {} planes", planes);

	const bool packed422 = (fmtParams.cformat == CF_YUY2 || fmtParams.cformat == CF_Y210 || fmtParams.cformat == CF_Y216);
	const bool blendDeint420 = (blendDeinterlace && fmtParams.Subsampling == 420);

	const char* strChromaPos = "";
	const char* strChromaPos2 = "";
	if (fmtParams.Subsampling == 420) {
		switch (chromaSubsampling) {
		case DXVA2_VideoChromaSubsampling_Cosited:
			strChromaPos = "+float2(dx*0.5,dy*0.5)";
			strChromaPos2 = "+float2(-0.25,-0.25)";
			DLog(L"ConvertColorShader: chroma location - Co-sited");
			break;
		case DXVA2_VideoChromaSubsampling_MPEG1:
			//strChromaPos = "";
			strChromaPos2 = "+float2(-0.5,-0.5)";
			DLog(L"ConvertColorShader: chroma location - MPEG-1");
			break;
		case DXVA2_VideoChromaSubsampling_MPEG2:
		default:
			strChromaPos = "+float2(dx*0.5,0)";
			strChromaPos2 = "+float2(-0.25,-0.5)";
			DLog(L"ConvertColorShader: chroma location - MPEG-2");
		}
	}
	else if (fmtParams.Subsampling == 422) {
		strChromaPos = "+float2(dx*0.5,0)";
		DLog(L"ConvertColorShader: chroma location - YUV 4:2:2");
	}

	if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 422) {
		code.append("#define CATMULLROM_05(c0,c1,c2,c3) (9*(c1+c2)-(c0+c3))*0.0625\n");
	}

	if (bDX11) {
		switch (planes) {
		case 1:
			code.append("Texture2D tex : register(t0);\n");
			break;
		case 2:
			code.append("Texture2D texY : register(t0);\n");
			code.append("Texture2D texUV : register(t1);\n");
			break;
		case 3:
			code.append("Texture2D texY : register(t0);\n");
			if (fmtParams.cformat == CF_YV12 || fmtParams.cformat == CF_YV16 || fmtParams.cformat == CF_YV24) {
				code.append("Texture2D texV : register(t1);\n");
				code.append("Texture2D texU : register(t2);\n");
			} else {
				code.append("Texture2D texU : register(t1);\n");
				code.append("Texture2D texV : register(t2);\n");
			}
			break;
		}

		code.append(
			"SamplerState samp : register(s0);\n"
			"SamplerState sampL : register(s1);\n");

		code.append(
			"struct PS_INPUT {"
				"float4 Pos : SV_POSITION;"
				"float2 Tex : TEXCOORD;"
			"};\n");

		code.append("\nfloat4 main(PS_INPUT input) : SV_Target\n{\n");

		switch (planes) {
		case 1:
			switch (fmtParams.cformat) {
			default:
				code.append("float4 color = tex.Sample(samp, input.Tex);\n");
				break;
			case CF_AYUV:
				code.append("float4 color = tex.Sample(samp, input.Tex).zyxw;\n");
				break;
			case CF_Y410:
			case CF_Y416:
				code.append("float4 color = tex.Sample(samp, input.Tex).yxzw;\n");
				break;
			}
			if (packed422) {
				code.append("if (fmod(input.Tex.x*w, 2) < 1.0) {\n"
					"color = float4(color[0], color[1], color[3], 1);\n"
					"} else {\n");
				if (chromaScaling == CHROMA_CatmullRom) {
					code.append(
						"float2 c0 = tex.Sample(samp, input.Tex, int2(-1, 0)).yw;\n"
						"float2 c1 = color.yw;\n"
						"float2 c2 = tex.Sample(samp, input.Tex, int2(1, 0)).yw;\n"
						"float2 c3 = tex.Sample(samp, input.Tex, int2(2, 0)).yw;\n"
						"float2 chroma = CATMULLROM_05(c0,c1,c2,c3);\n");
				} else { // linear
					code.append("float2 chroma = (color.yw + tex.Sample(samp, input.Tex, int2(1, 0)).yw) * 0.5;\n");
				}
				code.append("color = float4(color[2], chroma, 1);\n"
					"}\n");
			}
			break;
		case 2:
			code.append("float colorY = texY.Sample(samp, input.Tex).r;\n");
			if (blendDeint420) {
				code.append(
					"float y1 = texY.Sample(samp, input.Tex, int2(0, -1));\n"
					"float y2 = texY.Sample(samp, input.Tex, int2(0, 1));\n"
					"colorY = (colorY * 2 + y1 + y2) / 4;\n");
			}
			code.append("float2 colorUV;\n");
			if (chromaScaling == CHROMA_Nearest || fmtParams.Subsampling == 444) {
				code.append("colorUV = texUV.Sample(samp, input.Tex).rg;\n");
			}
			else if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 420) {
				code += std::format("float2 t = frac(input.Tex * (wh*0.5)){};\n", strChromaPos2); // Very strange, but it works.
				code.append(code_CatmullRom_weights);
				for (int y = 0; y < 4; y++) {
					for (int x = 0; x < 4; x++) {
						code += std::format("float2 c{}{} = texUV.Sample(samp, input.Tex, int2({}, {})).rg;\n", x, y, x-1, y-1);
					}
				}
				code.append(code_Bicubic_UV);
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
			else { // CHROMA_Bilinear
				code += std::format("float2 pos = input.Tex{};\n", strChromaPos);
				code.append(
					"colorUV = texUV.Sample(sampL, pos).rg;\n"
				);
			}
			code.append("float4 color = float4(colorY, colorUV, 1);\n");
			break;
		case 3:
			code.append("float colorY = texY.Sample(samp, input.Tex).r;\n");
			if (blendDeint420) {
				code.append(
					"float y1 = texY.Sample(samp, input.Tex, int2(0, -1));\n"
					"float y2 = texY.Sample(samp, input.Tex, int2(0, 1));\n"
					"colorY = (colorY * 2 + y1 + y2) / 4;\n");
			}
			code.append("float2 colorUV;\n");
			if (chromaScaling == CHROMA_Nearest || fmtParams.Subsampling == 444) {
				code.append(
					"colorUV[0] = texU.Sample(samp, input.Tex).r;\n"
					"colorUV[1] = texV.Sample(samp, input.Tex).r;\n"
				);
			}
			else if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 420) {
				code += std::format("float2 t = frac(input.Tex * (wh*0.5)){};\n", strChromaPos2); // I don't know why, but it works.
				code.append(code_CatmullRom_weights);
				code.append("float2 c00,c10,c20,c30,c01,c11,c21,c31,c02,c12,c22,c32,c03,c13,c23,c33;\n");
				for (int y = 0; y < 4; y++) {
					for (int x = 0; x < 4; x++) {
						code += std::format("c{}{}[0] = texU.Sample(samp, input.Tex, int2({}, {})).r;\n", x, y, x-1, y-1);
						code += std::format("c{}{}[1] = texV.Sample(samp, input.Tex, int2({}, {})).r;\n", x, y, x-1, y-1);
					}
				}
				code.append(code_Bicubic_UV);
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
			else { // CHROMA_Bilinear
				code += std::format("float2 pos = input.Tex{};\n", strChromaPos);
				code.append(
					"colorUV[0] = texU.Sample(sampL, pos).r;\n"
					"colorUV[1] = texV.Sample(sampL, pos).r;\n"
				);
			}
			code.append("float4 color = float4(colorY, colorUV, 1);\n");
			break;
		}
	}
	else {
		switch (planes) {
		case 1:
			code.append("sampler s0 : register(s0);\n");
			break;
		case 2:
			code.append("sampler sY : register(s0);\n");
			code.append("sampler sUV : register(s1);\n");
			break;
		case 3:
			code.append("sampler sY : register(s0);\n");
			if (fmtParams.cformat == CF_YV12 || fmtParams.cformat == CF_YV16 || fmtParams.cformat == CF_YV24) {
				code.append("sampler sV : register(s1);\n");
				code.append("sampler sU : register(s2);\n");
			} else {
				code.append("sampler sU : register(s1);\n");
				code.append("sampler sV : register(s2);\n");
			}
			break;
		}

		switch (planes) {
		case 1:
			code.append(
				"\n"
				"float4 main(float2 tex : TEXCOORD0) : COLOR\n"
				"{\n"
			);
			switch (fmtParams.cformat) {
			default:
				code.append("float4 color = tex2D(s0, tex);\n");
				break;
			case CF_AYUV:
				code.append("float4 color = tex2D(s0, tex).zyxw;\n");
				break;
			case CF_Y410:
			case CF_Y416:
				code.append("float4 color = tex2D(s0, tex).yxzw;\n");
				break;
			}
			if (packed422) {
				code.append("if (fmod(tex.x*w, 2) < 1.0) {\n"
					"color = float4(color[2], color[1], color[3], 1);\n"
					"} else {\n");
				if (chromaScaling == CHROMA_CatmullRom) {
					code.append(
						"float2 c0 = tex2D(s0, tex + float2(-dx, 0)).yw;\n"
						"float2 c1 = color.yw;\n"
						"float2 c2 = tex2D(s0, tex + float2(dx, 0)).yw;\n"
						"float2 c3 = tex2D(s0, tex + float2(2*dx, 0)).yw;\n"
						"float2 chroma = CATMULLROM_05(c0,c1,c2,c3);\n");
				} else { // linear
					code.append("float2 chroma = (color.yw + tex2D(s0, tex + float2(dx, 0)).yw) * 0.5;\n");
				}
				code.append("color = float4(color[0], chroma, 1);\n"
					"}\n");
			}
			break;
		case 2:
			code.append(
				"\n"
				"float4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1) : COLOR\n"
				"{\n"
				"float colorY = tex2D(sY, t0).r;\n");
			if (blendDeint420) {
				code.append(
					"float y1 = tex2D(sY, t0 + float2(0, -dy)).r;\n"
					"float y2 = tex2D(sY, t0 + float2(0, dy)).r;\n"
					"colorY = (colorY * 2 + y1 + y2) / 4;\n");
			}
			code.append("float2 colorUV;\n");
			if (chromaScaling == CHROMA_Nearest || fmtParams.Subsampling == 444) {
				if (fmtParams.cformat == CF_NV12) {
					code.append("colorUV = tex2D(sUV, t0).ra;\n");
				} else {
					code.append("colorUV = tex2D(sUV, t0).rg;\n");
				}
			}
			else if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 420) {
				code.append("float2 pos = t0 * (wh*0.5);\n"
					"float2 t = frac(pos);\n"
					"pos -= t;\n");
				code += std::format("t = t{};\n", strChromaPos2);
				code.append(code_CatmullRom_weights);
				for (int y = 0; y < 4; y++) {
					for (int x = 0; x < 4; x++) {
						if (fmtParams.cformat == CF_NV12) {
							code += std::format("float2 c{}{} = tex2D(sUV, (pos + float2({}+0.5, {}+0.5))*dxdy2).ra;\n", x, y, x - 1, y - 1);
						} else {
							code += std::format("float2 c{}{} = tex2D(sUV, (pos + float2({}+0.5, {}+0.5))*dxdy2).rg;\n", x, y, x - 1, y - 1);
						}
					}
				}
				code.append(code_Bicubic_UV);
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
			else { // CHROMA_Bilinear
				if (fmtParams.cformat == CF_NV12) {
					code.append("colorUV = tex2D(sUV, t1).ra;\n");
				} else {
					code.append("colorUV = tex2D(sUV, t1).rg;\n");
				}
			}
			code.append("float4 color = float4(colorY, colorUV, 1);\n");
			break;
		case 3:
			code.append(
				"\n"
				"float4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1) : COLOR\n"
				"{\n"
				"float colorY = tex2D(sY, t0).r;\n");
			if (blendDeint420) {
				code.append(
					"float y1 = tex2D(sY, t0 + float2(0, -dy)).r;\n"
					"float y2 = tex2D(sY, t0 + float2(0, dy)).r;\n"
					"colorY = (colorY * 2 + y1 + y2) / 4;\n");
			}
			code.append("float2 colorUV;\n");
			if (chromaScaling == CHROMA_Nearest || fmtParams.Subsampling == 444) {
				code.append(
					"colorUV[0] = tex2D(sU, t0).r;\n"
					"colorUV[1] = tex2D(sV, t0).r;\n"
				);
			}
			else if (chromaScaling == CHROMA_CatmullRom && fmtParams.Subsampling == 420) {
				code.append("float2 pos = t0 * (wh*0.5);\n"
					"float2 t = frac(pos);\n"
					"pos -= t;\n");
				code += std::format("t = t{};\n", strChromaPos2);
				code.append(code_CatmullRom_weights);
				code.append("float2 c00,c10,c20,c30,c01,c11,c21,c31,c02,c12,c22,c32,c03,c13,c23,c33;\n");
				for (int y = 0; y < 4; y++) {
					for (int x = 0; x < 4; x++) {
						code += std::format("c{}{}[0] = tex2D(sU, (pos + float2({}+0.5, {}+0.5))*dxdy2).r;\n", x, y, x-1, y-1);
						code += std::format("c{}{}[1] = tex2D(sV, (pos + float2({}+0.5, {}+0.5))*dxdy2).r;\n", x, y, x-1, y-1);
					}
				}
				code.append(code_Bicubic_UV);
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
			else { // CHROMA_Bilinear
				code.append(
					"colorUV[0] = tex2D(sU, t1).r;\n"
					"colorUV[1] = tex2D(sV, t1).r;\n"
				);
			}
			code.append("float4 color = float4(colorY, colorUV, 1);\n");
			break;
		}
	}
}

void ShaderDoviReshapePoly(std::string& code)
{
	code.append(
		"// dovi reshape\n"
		"float3 sig = saturate(color.rgb);\n"
		"[unroll(3)]\n"
		"for (uint c = 0; c < 3; c++) {\n"
		"    float s = sig[c];\n"
		"    #define test(i) (s >= curves[c].pivots_data[i]) ? 1.0 : 0.0\n"
		"    #define coef(i) curves[c].coeffs_data[i]\n"
		"    float4 coeffs =\n"
		"        lerp(lerp(lerp(coef(0), coef(1), test(0)),\n"
		"                  lerp(coef(2), coef(3), test(2)),\n"
		"                  test(1)),\n"
		"             lerp(lerp(coef(4), coef(5), test(4)),\n"
		"                  lerp(coef(6), coef(7), test(6)),\n"
		"                  test(5)),\n"
		"             test(3));\n"
		"    #undef test\n"
		"    #undef coef\n"
		"    s = (coeffs.z * s + coeffs.y) * s + coeffs.x;\n"
		"    color[c] = saturate(s);\n"
		"}\n"
	);
}

void ShaderDoviReshape(std::string& code)
{
	// based on libplacebo source code
	code.append(
		"{// dovi reshape\n"
		"    float3 sig = saturate(color.rgb);\n"
		"    [unroll(3)]\n"
		"    for (uint c = 0; c < 3; c++) {\n"
		"        float s = sig[c];\n"
		"        #define test(i) (s >= curves[c].pivots_data[i]) ? 1.0 : 0.0\n"
		"        #define coef(i) curves[c].coeffs_data[i]\n"
		"        float4 coeffs =\n"
		"                 lerp(lerp(lerp(coef(0), coef(1), test(0)),\n"
		"                           lerp(coef(2), coef(3), test(2)),\n"
		"                           test(1)),\n"
		"                 lerp(lerp(coef(4), coef(5), test(4)),\n"
		"                      lerp(coef(6), coef(7), test(6)),\n"
		"                      test(5)),\n"
		"                 test(3));\n"
		"        #undef test\n"
		"        #undef coef\n"
		"        if (curves[c].params.has_poly && curves[c].params.has_mmr) {\n"
		"            if (coeffs[3] == 0.0) {\n"
		"                // reshape_poly\n"
		"                s = (coeffs.z * s + coeffs.y) * s + coeffs.x;\n"
		"            } else {\n"
		"                s = reshape_mmr(coeffs, sig, c, curves[c].mmr_flags.mmr_single, curves[c].mmr_flags.min_order, curves[c].mmr_flags.max_order);\n"
		"            }\n"
		"        } else if (curves[c].params.has_poly) {\n"
		"            // reshape_poly\n"
		"            s = (coeffs.z * s + coeffs.y) * s + coeffs.x;\n"
		"        } else {\n"
		"            s = reshape_mmr(coeffs, sig, c, curves[c].mmr_flags.mmr_single, curves[c].mmr_flags.min_order, curves[c].mmr_flags.max_order);\n"
		"        }\n"
		"        color[c] = saturate(s);\n"
		"    }\n"
		"}\n"
	);
}

//////////////////////////////

HRESULT GetShaderConvertColor(
	const bool bDX11,
	const UINT width,
	const long texW, long texH,
	const RECT rect,
	const FmtConvParams_t& fmtParams,
	const DXVA2_ExtendedFormat exFmt,
	const MediaSideDataDOVIMetadata* const pDoviMetadata,
	const int chromaScaling,
	const int convertType,
	const bool blendDeinterlace,
	ID3DBlob** ppCode)
{
	DLog(L"GetShaderConvertColor() started for {} {}x{} extfmt:{:#010x} chroma:{}", fmtParams.str, texW, texH, exFmt.value, chromaScaling);

	std::string code;
	HRESULT hr = S_OK;
	LPVOID data;
	DWORD size;

	const bool bBT2020Primaries = (exFmt.VideoPrimaries == MFVideoPrimaries_BT2020);
	const bool bConvertHDRtoSDR = (convertType == SHADER_CONVERT_TO_SDR && (exFmt.VideoTransferFunction == MFVideoTransFunc_2084 || exFmt.VideoTransferFunction == MFVideoTransFunc_HLG || pDoviMetadata));
	const bool bApplyHLG = (exFmt.VideoTransferFunction == MFVideoTransFunc_HLG && !pDoviMetadata);
	const bool bConvertHLGtoPQ = (convertType == SHADER_CONVERT_TO_PQ && bApplyHLG);

	if (bApplyHLG) {
		hr = GetDataFromResource(data, size, IDF_HLSL_HLG);
		if (S_OK == hr) {
			code.append((LPCSTR)data, size);
			code += '\n';
		}
	}

	if (bConvertHDRtoSDR || bConvertHLGtoPQ || pDoviMetadata) {
		hr = GetDataFromResource(data, size, IDF_HLSL_ST2084);
		if (S_OK == hr) {
			code.append((LPCSTR)data, size);
			code += '\n';
		}
	}

	if (bBT2020Primaries || bConvertHDRtoSDR) {
		float matrix_conv_prim[3][3];
		GetColorspaceGamutConversionMatrix(matrix_conv_prim, MP_CSP_PRIM_BT_2020, MP_CSP_PRIM_BT_709);
		code.append("static const float3x3 matrix_conv_prim = {\n");
		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++) {
				code += std::format("{}, ", matrix_conv_prim[i][j]);
			}
			code.append("\n");
		}
		code.append("};\n");
	}

	if (bConvertHDRtoSDR) {
		hr = GetDataFromResource(data, size, IDF_HLSL_HDR_TONE_MAPPING);
		if (S_OK == hr) {
			code.append((LPCSTR)data, size);
			code += '\n';
		}
	}

	const bool packed422 = (fmtParams.cformat == CF_YUY2 || fmtParams.cformat == CF_Y210 || fmtParams.cformat == CF_Y216);
	const bool fix422 = (packed422 && texW * 2 == width);

	code += std::format("#define w {}\n", fix422 ? width : texW);
	code += std::format("#define dx (1.0/{})\n", texW);
	code += std::format("#define dy (1.0/{})\n", texH);
	code += std::format("static const float2 wh = {{{}, {}}};\n", fix422 ? width : texW, texH);
	code += std::format("static const float2 dxdy2 = {{2.0/{}, 2.0/{}}};\n", texW, texH);

	bool has_mmr = false;

	if (bDX11) {
		code.append(
			"cbuffer PS_COLOR_TRANSFORM : register(b0) {\n"
			"float3 cm_r;\n" // NB: sizeof(float3) == sizeof(float4)
			"float3 cm_g;\n"
			"float3 cm_b;\n"
			"float3 cm_c;\n"
			"};\n"
		);
		if (pDoviMetadata) {
			for (const auto& curve : pDoviMetadata->Mapping.curves) {
				for (uint8_t i = 0; i < (curve.num_pivots - 1); i++) {
					if (curve.mapping_idc[i] == 1) {
						has_mmr = true;
						break;
					}
				}
			}

			if (has_mmr) {
				code.append(
					"struct dovi_params {\n"
					"    uint num_pivots;\n"
					"    uint has_poly;\n"
					"    uint has_mmr;\n"
					"    uint padding;\n"
					"};\n"
					"struct mmr_flags {\n"
					"    uint mmr_single;\n"
					"    uint min_order;\n"
					"    uint max_order;\n"
					"    uint padding;\n"
					"};\n"
					"struct PS_DOVI_CURVE {\n"
					"    float pivots_data[7];\n" // NB: sizeof(float) == sizeof(float4)
					"    float4 coeffs_data[8];\n"
					"    float4 mmr_data[8 * 6];\n"
					"    dovi_params params;\n"
					"    mmr_flags mmr_flags;\n"
					"};\n"
					"cbuffer PS_DOVI_CURVES : register(b1) {\n"
					"    PS_DOVI_CURVE curves[3];\n"
					"};\n"
				);
				code.append(
					"const float reshape_mmr(const float4 coeffs, const float3 sig, uint c,\n"
					"                        const uint mmr_single, const uint min_order, const uint max_order) {\n"
					"    uint mmr_idx = mmr_single ? 0u : uint(coeffs.y);\n"
					"    float s = coeffs.x;\n"
					"    float4 sigX = float4(0.0, 0.0, 0.0, 0.0);\n"
					"    sigX.xyz = sig.xxy * sig.yzz;\n"
					"    sigX[3] = sigX.x * sig.z;\n"
					"    sigX = float4(sigX.xyz, sigX.x * sig.z);\n"
					"    s += dot(curves[c].mmr_data[mmr_idx + 0].xyz, sig);\n"
					"    s += dot(curves[c].mmr_data[mmr_idx + 1], sigX);\n"
					"    if (max_order >= 2) {\n"
					"        uint order = uint(coeffs[3]);\n"
					"        if (min_order < 2 && order < 2) {\n"
					"            return s;\n"
					"        }\n"
					"        float3 sig2 = sig * sig;\n"
					"        float4 sigX2 = sigX * sigX;\n"
					"        s += dot(curves[c].mmr_data[mmr_idx + 2].xyz, sig2);\n"
					"        s += dot(curves[c].mmr_data[mmr_idx + 3], sigX2);\n"
					"        if (max_order == 3) {\n"
					"            if (min_order < 3 && order < 3) {\n"
					"                return s;\n"
					"            }\n"
					"            s += dot(curves[c].mmr_data[mmr_idx + 4].xyz, sig2 * sig);\n"
					"            s += dot(curves[c].mmr_data[mmr_idx + 5], sigX2 * sigX);\n"
					"        }\n"
					"    }"
					"    return s;\n"
					"}\n"
				);
			}
			else {
				code.append(
					"struct PS_DOVI_CURVE {\n"
					"    float pivots_data[7];\n" // NB: sizeof(float) == sizeof(float4)
					"    float4 coeffs_data[8];\n"
					"};\n"
					"cbuffer PS_DOVI_CURVES : register(b1) {\n"
					"    PS_DOVI_CURVE curves[3];\n"
					"};\n"
				);
			}
		}
	}
	else {
		code.append(
			"float3 cm_r : register(c0);\n"
			"float3 cm_g : register(c1);\n"
			"float3 cm_b : register(c2);\n"
			"float3 cm_c : register(c3);\n"
		);
		if (pDoviMetadata) {
			code.append(
				"struct PS_DOVI_CURVE {\n"
				"    float pivots_data[7];\n" // NB: sizeof(float) == sizeof(float4)
				"    float4 coeffs_data[8];\n"
				"};\n"
				"PS_DOVI_CURVE curves[3] : register(c4);\n"
			);
		}
	}

	ShaderGetPixels(bDX11, fmtParams, exFmt.VideoChromaSubsampling, chromaScaling, blendDeinterlace, code);

	if (pDoviMetadata) {
		if (has_mmr) {
			ShaderDoviReshape(code);
		} else {
			ShaderDoviReshapePoly(code);
		}
	}

	code.append("//convert color\n");
	code.append("color.rgb = float3(mul(cm_r, color.rgb), mul(cm_g, color.rgb), mul(cm_b, color.rgb)) + cm_c;\n");

	bool isLinear = false;

	if (pDoviMetadata) {
		float dovi_lms2rgb[3][3] = {
			{ 3.06441879f, -2.16597676f,  0.10155818f},
			{-0.65612108f,  1.78554118f, -0.12943749f},
			{ 0.01736321f, -0.04725154f,  1.03004253f},
		};
		float linear[3][3];
		for (int i = 0; i < 3; i++) {
			linear[i][0] = (float)pDoviMetadata->ColorMetadata.rgb_to_lms_matrix[i * 3 + 0];
			linear[i][1] = (float)pDoviMetadata->ColorMetadata.rgb_to_lms_matrix[i * 3 + 1];
			linear[i][2] = (float)pDoviMetadata->ColorMetadata.rgb_to_lms_matrix[i * 3 + 2];
		}
		float mat[3][3];
		mul_matrix3x3(mat, dovi_lms2rgb, linear);

		code.append("float3x3 mat = {\n");
		code += std::format("{}, {}, {},\n", mat[0][0], mat[0][1], mat[0][2]);
		code += std::format("{}, {}, {},\n", mat[1][0], mat[1][1], mat[1][2]);
		code += std::format("{}, {}, {}\n", mat[2][0], mat[2][1], mat[2][2]);
		code.append("};\n");

		// PQ EOTF
		code.append(
			"color = max(color, 0.0);\n"
			"color = ST2084ToLinear(color, 1.0);\n"
		);

		// LMS matrix
		code.append("color.rgb = mul(mat, color.rgb); \n");

		// PQ OETF
		code.append(
			"color = max(color, 0.0);\n"
			"color = LinearToST2084(color, 1.0);\n"
		);
	}

	if (bConvertHDRtoSDR) {
		if (bApplyHLG) {
			code.append(
				"color = saturate(color);\n"
				"color.rgb = HLGtoLinear(color.rgb);\n"
				"color = LinearToST2084(color, 1000.0);\n"
			);
		}
		code.append(
			"#define SRC_LUMINANCE_PEAK     10000.0\n"
			"#define DISPLAY_LUMINANCE_PEAK 125.0\n"
			"color = saturate(color);\n"
			"color = ST2084ToLinear(color, SRC_LUMINANCE_PEAK/DISPLAY_LUMINANCE_PEAK);\n"
			"color.rgb = ToneMappingHable(color.rgb);\n"
			"color.rgb = mul(matrix_conv_prim, color.rgb);\n"
		);
		isLinear = true;
	}
	else if (bConvertHLGtoPQ) {
		code.append(
			"color = saturate(color);\n"
			"color.rgb = HLGtoLinear(color.rgb);\n"
			"color = LinearToST2084(color, 1000.0);\n"
		);
	}
	else if (bBT2020Primaries) {
		std::string toLinear;
		switch (exFmt.VideoTransferFunction) {
		case DXVA2_VideoTransFunc_10:   toLinear = "\\\\nothing\n";                  break;
		case DXVA2_VideoTransFunc_18:   toLinear = "color = pow(color, 1.8);\n"; break;
		case DXVA2_VideoTransFunc_20:   toLinear = "color = pow(color, 2.0);\n"; break;
		case MFVideoTransFunc_HLG: // HLG compatible with SDR
		case DXVA2_VideoTransFunc_22:
		case DXVA2_VideoTransFunc_709:
		case DXVA2_VideoTransFunc_240M: toLinear = "color = pow(color, 2.2);\n"; break;
		case DXVA2_VideoTransFunc_sRGB: toLinear = "color = pow(color, 2.2);\n"; break;
		case DXVA2_VideoTransFunc_28:   toLinear = "color = pow(color, 2.8);\n"; break;
		case MFVideoTransFunc_26:       toLinear = "color = pow(color, 2.6);\n"; break;
		}

		if (toLinear.size()) {
			code.append("color = saturate(color);\n");
			code.append(toLinear);
			code.append(
				"color.rgb = mul(matrix_conv_prim, color.rgb);\n"
			);
			isLinear = true;
		}
	}

	if (isLinear) {
		// Linear to sRGB
		code.append(
			"color = saturate(color);\n"
			"color = pow(color, 1.0/2.2);\n"
		);
	}

	code.append("return color;\n}");

	LPCSTR target = bDX11 ? "ps_4_0" : "ps_3_0";

	return CompileShader(code, nullptr, target, ppCode);
}
