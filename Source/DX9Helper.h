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

struct Tex_t
{
	CComPtr<IDirect3DTexture9> pTexture;
	CComPtr<IDirect3DSurface9> pSurface;
	D3DFORMAT Format = D3DFMT_UNKNOWN;
	UINT Width  = 0;
	UINT Height = 0;


	HRESULT CheckCreate(IDirect3DDevice9Ex* pDevice, const D3DFORMAT format, const UINT width, const UINT height, DWORD usage) {
		if (format == Format && width == Width && height == Height) {
			return S_OK;
		}

		return Create(pDevice, format, width, height, usage);
	}

	HRESULT Create(IDirect3DDevice9Ex* pDevice, const D3DFORMAT format, const UINT width, const UINT height, DWORD usage) {
		Release();

		HRESULT hr = pDevice->CreateTexture(width, height, 1, usage, format, D3DPOOL_DEFAULT, &pTexture, nullptr);
		if (S_OK == hr) {
			EXECUTE_ASSERT(S_OK == pTexture->GetSurfaceLevel(0, &pSurface));
			D3DSURFACE_DESC desc = {};
			EXECUTE_ASSERT(S_OK == pSurface->GetDesc(&desc));
			Format = desc.Format;
			Width  = desc.Width;
			Height = desc.Height;
		}

		return hr;
	}

	virtual void Release() {
		pSurface.Release();
		pTexture.Release();
		Format = D3DFMT_UNKNOWN;
		Width  = 0;
		Height = 0;
	}
};

struct Tex9Video_t : Tex_t
{
	Tex_t Plane2;
	Tex_t Plane3;

	HRESULT CreateEx(IDirect3DDevice9Ex* pDevice, const D3DFORMAT format, const DX9PlanarPrms_t* pPlanes, const UINT width, const UINT height, DWORD usage) {
		Release();

		HRESULT hr;

		if (pPlanes) {
			hr = Create(pDevice, pPlanes->FmtPlane1, width, height, usage);
			if (S_OK == hr) {
				const UINT chromaWidth  = width / pPlanes->div_chroma_w;
				const UINT chromaHeight = height / pPlanes->div_chroma_h;
				hr = Plane2.Create(pDevice, pPlanes->FmtPlane2, chromaWidth, chromaHeight, usage);
				if (S_OK == hr && pPlanes->FmtPlane3) {
					hr = Plane3.Create(pDevice, pPlanes->FmtPlane3, chromaWidth, chromaHeight, usage);
				}
			}
		}
		else {
			hr = Create(pDevice, format, width, height, usage);
		}

		if (FAILED(hr)) {
			Release();
		}

		return hr;
	}

	void Release() override {
		Plane3.Release();
		Plane2.Release();
		Tex_t::Release();
	}
};

class CTexRing
{
	Tex_t Texs[2];
	int index = 0;
	UINT size = 0;

public:
	HRESULT CheckCreate(IDirect3DDevice9Ex* pDevice, const D3DFORMAT format, const UINT width, const UINT height, const UINT num) {
		if (num == size && format == Texs[0].Format && width == Texs[0].Width && height == Texs[0].Height) {
			return S_OK;
		}

		return Create(pDevice, format, width, height, num);
	}

	HRESULT Create(IDirect3DDevice9Ex* pDevice, const D3DFORMAT format, const UINT width, const UINT height, const UINT num) {
		Release();
		HRESULT hr = S_FALSE;
		if (num >= 1) {
			hr = Texs[0].Create(pDevice, format, width, height, D3DUSAGE_RENDERTARGET);
			size++;
			if (S_OK == hr && num >= 2) {
				hr = Texs[1].Create(pDevice, format, width, height, D3DUSAGE_RENDERTARGET);
				size++;
			}
		}
		return hr;
	}

	void Release() {
		Texs[0].Release();
		Texs[1].Release();
		index = 0;
		size = 0;
	}

	Tex_t& GetFirstTex() {
		index = 0;
		return Texs[0];
	}

	Tex_t& GetNextTex() {
		index = (index + 1) & 1;
		return Texs[index];
	}
};

UINT GetAdapter(HWND hWnd, IDirect3D9Ex* pD3D);

HRESULT Dump4ByteSurface(IDirect3DSurface9* pSurface, const wchar_t* filename);

HRESULT DumpDX9Surface(IDirect3DSurface9* pSurface, const wchar_t* filename);
