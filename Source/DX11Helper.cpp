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

#include "stdafx.h"
#include <d3d11.h>
#include "Helper.h"
#include "DX11Helper.h"

UINT GetAdapter(HWND hWnd, IDXGIFactory1* pDXGIFactory, IDXGIAdapter** ppDXGIAdapter)
{
	*ppDXGIAdapter = nullptr;

	CheckPointer(pDXGIFactory, 0);

	const HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);

	UINT i = 0;
	IDXGIAdapter* pDXGIAdapter = nullptr;
	while (SUCCEEDED(pDXGIFactory->EnumAdapters(i, &pDXGIAdapter))) {
		UINT k = 0;
		IDXGIOutput* pDXGIOutput = nullptr;
		while (SUCCEEDED(pDXGIAdapter->EnumOutputs(k, &pDXGIOutput))) {
			DXGI_OUTPUT_DESC desc = {};
			if (SUCCEEDED(pDXGIOutput->GetDesc(&desc))) {
				if (desc.Monitor == hMonitor) {
					SAFE_RELEASE(pDXGIOutput);
					*ppDXGIAdapter = pDXGIAdapter;
					return i;
				}
			}
			SAFE_RELEASE(pDXGIOutput);
			k++;
		}

		SAFE_RELEASE(pDXGIAdapter);
		i++;
	}

	return 0;
}

HRESULT Dump4ByteTexture2D(ID3D11DeviceContext* pDeviceContext, ID3D11Texture2D* pRGB32Texture2D, const wchar_t* filename)
{
	HRESULT hr = S_OK;
	D3D11_TEXTURE2D_DESC desc;
	pRGB32Texture2D->GetDesc(&desc);

	if (desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_AYUV) {
		D3D11_MAPPED_SUBRESOURCE mappedResource = {};
		hr = pDeviceContext->Map(pRGB32Texture2D, 0, D3D11_MAP_READ, 0, &mappedResource);

		if (SUCCEEDED(hr)) {
			hr = SaveARGB32toBMP((BYTE*)mappedResource.pData, mappedResource.RowPitch, desc.Width, desc.Height, filename);
			pDeviceContext->Unmap(pRGB32Texture2D, 0);
		}
	}

	return hr;
}
