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
			DLog("Unexpected compiler error");
		}
	}

	SAFE_RELEASE(pErrorBlob);

	return hr;
}
