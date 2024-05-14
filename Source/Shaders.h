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

#pragma once

#include <d3dcommon.h>

struct PS_COLOR_TRANSFORM {
	DirectX::XMFLOAT4 cm_r;
	DirectX::XMFLOAT4 cm_g;
	DirectX::XMFLOAT4 cm_b;
	DirectX::XMFLOAT4 cm_c;
};

struct PS_DOVI_POLY_CURVE {
	DirectX::XMFLOAT4 pivots_data[7];
	DirectX::XMFLOAT4 coeffs_data[8];
};

#define PS_RESHAPE_POLY 1
#define PS_RESHAPE_MMR  2

struct PS_DOVI_CURVE {
	DirectX::XMFLOAT4 pivots_data[7];
	DirectX::XMFLOAT4 coeffs_data[8];
	DirectX::XMFLOAT4 mmr_data[8 * 6];
	struct {
		uint32_t methods;
		uint32_t mmr_single;
		uint32_t min_order;
		uint32_t max_order;
	} params;
};

static_assert(sizeof(PS_DOVI_CURVE) % 16 == 0);

enum :int {
	SHADER_CONVERT_NONE = 0,
	SHADER_CONVERT_TO_SDR,
	SHADER_CONVERT_TO_PQ,
};

HRESULT CompileShader(const std::string& srcCode, const D3D_SHADER_MACRO* pDefines, LPCSTR pTarget, ID3DBlob** ppCode);

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
	const float LuminanceScale,
	ID3DBlob** ppCode);
