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

#pragma once

#include "d3dcommon.h"

HRESULT CompileShader(const CStringA& srcCode, const D3D_SHADER_MACRO* pDefines, LPCSTR pTarget, ID3DBlob** ppCode);

HRESULT GetShaderConvertColor(const bool bDX11, const FmtConvParams_t fmtParams, const DXVA2_ExtendedFormat exFmt, ID3DBlob** ppCode);
