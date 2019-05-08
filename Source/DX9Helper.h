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

struct Tex_t {
	CComPtr<IDirect3DTexture9> pTexture;
	CComPtr<IDirect3DSurface9> pSurface;
	UINT Width  = 0;
	UINT Height = 0;

	HRESULT Create(IDirect3DDevice9Ex* pDevice, const D3DFORMAT format, const UINT width, const UINT height) {
		Release();

		HRESULT hr = pDevice->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &pTexture, nullptr);
		if (S_OK == hr) {
			hr = pTexture->GetSurfaceLevel(0, &pSurface);
			if (S_OK == hr) {
				D3DSURFACE_DESC desc;
				hr = pTexture->GetLevelDesc(0, &desc);
				if (S_OK == hr) {
					Width  = desc.Width;
					Height = desc.Height;
				}
			}
		}

		return hr;
	}

	void Release() {
		pSurface.Release();
		pTexture.Release();
		Width  = 0;
		Height = 0;
	}
};
