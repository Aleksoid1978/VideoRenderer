/*
 * (C) 2018 see Authors.txt
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

#include "./BaseClasses/streams.h"
#include <d3d9.h>
#include <dxvahd.h>

const AMOVIESETUP_MEDIATYPE sudPinTypesIn[] = {
	{&MEDIATYPE_Video, &MEDIASUBTYPE_YV12},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_NV12},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_YUY2},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_RGB32},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_P010}
};

class __declspec(uuid("71F080AA-8661-4093-B15E-4F6903E77D0A"))
	CMpcVideoRenderer : public CBaseRenderer
{
private:
	CMediaType m_mt;

	HWND m_hWnd;
	UINT m_CurrentAdapter;

	HMODULE m_hD3D9Lib = nullptr;
	CComPtr<IDirect3D9Ex>       m_pD3DEx;
	CComPtr<IDirect3DDevice9Ex> m_pD3DDevEx;
	D3DDISPLAYMODE m_DisplayMode = { sizeof(D3DDISPLAYMODE) };


	HMODULE m_hDxva2Lib = nullptr;
	CComPtr<IDXVAHD_Device>         m_pDXVAHD_Device;
	CComPtr<IDXVAHD_VideoProcessor> m_pDXVAHD_VP;

public:
	CMpcVideoRenderer(LPUNKNOWN pUnk, HRESULT* phr);
	~CMpcVideoRenderer();

protected:
	HRESULT SetMediaType(const CMediaType *pmt) override;
	HRESULT CheckMediaType(const CMediaType* pmt) override;

	BOOL InitDirect3D9();
	BOOL InitializeDXVAHDVP(int width, int height);

public:
	HRESULT DoRenderSample(IMediaSample* pSample) override;
};
