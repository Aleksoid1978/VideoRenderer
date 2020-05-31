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
#include <memory>
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

HRESULT Dump4ByteSurface(IDirect3DSurface9* pSurface, const wchar_t* filename)
{
	D3DSURFACE_DESC desc;
	HRESULT hr = pSurface->GetDesc(&desc);

	if (SUCCEEDED(hr) && (desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8 || desc.Format == D3DFMT_AYUV)) {
		CComPtr<IDirect3DSurface9> pSurfaceShared;

		if (desc.Pool == D3DPOOL_DEFAULT) {
			IDirect3DDevice9* pDevice;
			pSurface->GetDevice(&pDevice);
			hr = pDevice->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &pSurfaceShared, nullptr);
			if (SUCCEEDED(hr)) {
				hr = pDevice->GetRenderTargetData(pSurface, pSurfaceShared);
			}
			pDevice->Release();
		} else {
			pSurfaceShared = pSurface;
		}

		if (SUCCEEDED(hr)) {
			D3DLOCKED_RECT lr;
			hr = pSurfaceShared->LockRect(&lr, nullptr, D3DLOCK_READONLY);

			if (SUCCEEDED(hr)) {
				hr = SaveARGB32toBMP((BYTE*)lr.pBits, lr.Pitch, desc.Width, desc.Height, filename);
				pSurfaceShared->UnlockRect();
			}
		}

		return hr;
	}

	return E_FAIL;
}

bool RetrieveBitmapData(unsigned w, unsigned h, unsigned bpp, BYTE* dst, BYTE* src, int srcpitch)
{
	unsigned linesize = w * bpp / 8;
	if ((int)linesize > srcpitch) {
		return false;
	}

	src += srcpitch * (h - 1);

	for (unsigned y = 0; y < h; ++y) {
		memcpy(dst, src, linesize);
		src -= srcpitch;
		dst += linesize;
	}

	return true;
}

HRESULT DumpDX9Surface(IDirect3DSurface9* pSurface, const wchar_t* filename)
{
	CheckPointer(pSurface, E_POINTER);

	HRESULT hr = S_OK;

	D3DSURFACE_DESC desc = {};
	if (FAILED(hr = pSurface->GetDesc(&desc))) {
		return hr;
	};

	CComPtr<IDirect3DDevice9> pDevice;
	if (FAILED(hr = pSurface->GetDevice(&pDevice))) {
		return hr;
	};

	CComPtr<IDirect3DSurface9> pTarget;
	if (FAILED(hr = pDevice->CreateRenderTarget(desc.Width, desc.Height, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, &pTarget, nullptr))
		|| FAILED(hr = pDevice->StretchRect(pSurface, nullptr, pTarget, nullptr, D3DTEXF_NONE))) {
		return hr;
	}

	unsigned len = desc.Width * desc.Height * 4;
	std::unique_ptr<BYTE[]> dib(new(std::nothrow) BYTE[sizeof(BITMAPINFOHEADER) + len]);
	if (!dib) {
		return E_OUTOFMEMORY;
	}

	BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)dib.get();
	memset(bih, 0, sizeof(BITMAPINFOHEADER));
	bih->biSize = sizeof(BITMAPINFOHEADER);
	bih->biWidth = desc.Width;
	bih->biHeight = desc.Height;
	bih->biBitCount = 32;
	bih->biPlanes = 1;
	bih->biSizeImage = DIBSIZE(*bih);

	D3DLOCKED_RECT r;
	if (FAILED(hr = pTarget->LockRect(&r, nullptr, D3DLOCK_READONLY))) {
		return hr;
	}

	RetrieveBitmapData(desc.Width, desc.Height, 32, (BYTE*)(bih + 1), (BYTE*)r.pBits, r.Pitch);

	pTarget->UnlockRect();

	BITMAPFILEHEADER bfh;
	bfh.bfType = 0x4d42;
	bfh.bfOffBits = sizeof(bfh) + sizeof(BITMAPINFOHEADER);
	bfh.bfSize = bfh.bfOffBits + len;
	bfh.bfReserved1 = bfh.bfReserved2 = 0;

	FILE* fp;
	if (_wfopen_s(&fp, filename, L"wb") == 0) {
		fwrite(&bfh, sizeof(bfh), 1, fp);
		fwrite(dib.get(), sizeof(BITMAPINFOHEADER) + len, 1, fp);
		fclose(fp);
	} else {
		hr = E_ACCESSDENIED;
	}

	return hr;
}
