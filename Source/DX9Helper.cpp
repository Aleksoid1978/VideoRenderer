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
#include <d3d9.h>
#include "Helper.h"
#include "DX9Helper.h"

UINT GetAdapter(HWND hWnd, IDirect3D9Ex* pD3D)
{
	CheckPointer(hWnd, D3DADAPTER_DEFAULT);
	CheckPointer(pD3D, D3DADAPTER_DEFAULT);

	const HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
	CheckPointer(hMonitor, D3DADAPTER_DEFAULT);

	for (UINT adp = 0, num_adp = pD3D->GetAdapterCount(); adp < num_adp; ++adp) {
		const HMONITOR hAdapterMonitor = pD3D->GetAdapterMonitor(adp);
		if (hAdapterMonitor == hMonitor) {
			return adp;
		}
	}

	return D3DADAPTER_DEFAULT;
}

HRESULT Dump4ByteSurface(IDirect3DSurface9* pRGB32Surface, const wchar_t* filename)
{
	D3DSURFACE_DESC desc;
	HRESULT hr = pRGB32Surface->GetDesc(&desc);

	if (SUCCEEDED(hr) && (desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8 || desc.Format == D3DFMT_AYUV)) {
		D3DLOCKED_RECT lr;
		hr = pRGB32Surface->LockRect(&lr, nullptr, D3DLOCK_READONLY);

		if (SUCCEEDED(hr)) {
			hr = SaveARGB32toBMP((BYTE*)lr.pBits, lr.Pitch, desc.Width, desc.Height, filename);
			pRGB32Surface->UnlockRect();
		}
	}

	return hr;
}
