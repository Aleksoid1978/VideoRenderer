/*
* (C) 2019-2024 see Authors.txt
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
#include <memory>
#include "Helper.h"
#include "DX9Helper.h"

UINT GetAdapter(HWND hWnd, IDirect3D9Ex* pD3D)
{
	CheckPointer(hWnd, D3DADAPTER_DEFAULT);
	CheckPointer(pD3D, D3DADAPTER_DEFAULT);

	const HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
	CheckPointer(hMonitor, D3DADAPTER_DEFAULT);

	const UINT adapterCount = pD3D->GetAdapterCount();
	for (UINT adapter = 0; adapter < adapterCount; ++adapter) {
		const HMONITOR hAdapterMonitor = pD3D->GetAdapterMonitor(adapter);
		if (hAdapterMonitor == hMonitor) {
			return adapter;
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
				hr = SaveToBMP((BYTE*)lr.pBits, lr.Pitch, desc.Width, desc.Height, 32, filename);
				pSurfaceShared->UnlockRect();
			}
		}

		return hr;
	}

	return E_FAIL;
}

HRESULT DumpDX9Surface(IDirect3DSurface9* pSurface, const wchar_t* filename)
{
	CheckPointer(pSurface, E_POINTER);

	HRESULT hr = S_OK;

	D3DSURFACE_DESC desc = {};
	hr = pSurface->GetDesc(&desc);
	if (FAILED(hr)) {
		return hr;
	};

	CComPtr<IDirect3DDevice9> pDevice;
	hr = pSurface->GetDevice(&pDevice);
	if (FAILED(hr)) {
		return hr;
	};

	CComPtr<IDirect3DSurface9> pTarget;
	hr = pDevice->CreateRenderTarget(desc.Width, desc.Height, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, &pTarget, nullptr);
	if (FAILED(hr)) {
		return hr;
	};

	hr = pDevice->StretchRect(pSurface, nullptr, pTarget, nullptr, D3DTEXF_NONE);
	if (FAILED(hr)) {
		return hr;
	};

	const UINT dib_bitdepth = 32;
	const UINT dib_pitch = CalcDibRowPitch(desc.Width, dib_bitdepth);
	const UINT dib_size = dib_pitch * desc.Height;

	std::unique_ptr<BYTE[]> dib(new(std::nothrow) BYTE[sizeof(BITMAPINFOHEADER) + dib_size]);
	if (!dib) {
		return E_OUTOFMEMORY;
	}

	BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)dib.get();
	ZeroMemory(bih, sizeof(BITMAPINFOHEADER));
	bih->biSize      = sizeof(BITMAPINFOHEADER);
	bih->biWidth     = desc.Width;
	bih->biHeight    = -(LONG)desc.Height; // top-down RGB bitmap
	bih->biBitCount  = dib_bitdepth;
	bih->biPlanes    = 1;
	bih->biSizeImage = dib_size;

	D3DLOCKED_RECT r;
	hr = pTarget->LockRect(&r, nullptr, D3DLOCK_READONLY);
	if (FAILED(hr)) {
		return hr;
	}

	CopyPlaneAsIs(desc.Height, (BYTE*)(bih + 1), dib_pitch, (BYTE*)r.pBits, r.Pitch);

	pTarget->UnlockRect();

	BITMAPFILEHEADER bfh;
	bfh.bfType = 0x4d42;
	bfh.bfOffBits = sizeof(bfh) + sizeof(BITMAPINFOHEADER);
	bfh.bfSize = bfh.bfOffBits + dib_size;
	bfh.bfReserved1 = bfh.bfReserved2 = 0;

	FILE* fp;
	if (_wfopen_s(&fp, filename, L"wb") == 0) {
		fwrite(&bfh, sizeof(bfh), 1, fp);
		fwrite(dib.get(), sizeof(BITMAPINFOHEADER) + dib_size, 1, fp);
		fclose(fp);
	} else {
		hr = E_ACCESSDENIED;
	}

	return hr;
}
