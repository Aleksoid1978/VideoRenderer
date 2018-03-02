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
#include <evr.h>
#include <d3d9.h>
#include <dxva2api.h>
#include <dxvahd.h>

const AMOVIESETUP_MEDIATYPE sudPinTypesIn[] = {
	{&MEDIATYPE_Video, &MEDIASUBTYPE_YV12},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_NV12},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_YUY2},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_RGB32},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_P010}
};

class __declspec(uuid("71F080AA-8661-4093-B15E-4F6903E77D0A"))
	CMpcVideoRenderer : public CBaseRenderer,
	public IMFGetService,
	public IMFVideoDisplayControl
{
private:
	CMediaType m_mt;
	RECT m_srcRect = {};
	RECT m_trgRect = {};
	UINT m_srcWidth = 0;
	UINT m_srcHeight = 0;
	UINT m_srcLines = 0;
	INT  m_srcPitch = 0;
	D3DFORMAT m_srcFormat = D3DFMT_UNKNOWN;
	CComPtr<IDirect3DSurface9> m_pSrcSurface;

	HWND m_hWnd = nullptr;
	UINT m_CurrentAdapter = D3DADAPTER_DEFAULT;

	HMODULE m_hD3D9Lib = nullptr;
	CComPtr<IDirect3D9Ex>       m_pD3DEx;
	CComPtr<IDirect3DDevice9Ex> m_pD3DDevEx;

	D3DDISPLAYMODEEX m_DisplayMode = { sizeof(D3DDISPLAYMODEEX) };
	D3DPRESENT_PARAMETERS m_d3dpp = {};

	HMODULE m_hDxva2Lib = nullptr;
	CComPtr<IDXVAHD_Device>         m_pDXVAHD_Device;
	CComPtr<IDXVAHD_VideoProcessor> m_pDXVAHD_VP;
	DXVAHD_VPDEVCAPS m_DXVAHDDevCaps = {};

	typedef HRESULT (__stdcall *PTR_DXVA2CreateDirect3DDeviceManager9)(UINT* pResetToken, IDirect3DDeviceManager9** ppDeviceManager);
	typedef HRESULT (__stdcall *PTR_DXVA2CreateVideoService)(IDirect3DDevice9* pDD, REFIID riid, void** ppService);

	PTR_DXVA2CreateDirect3DDeviceManager9 pDXVA2CreateDirect3DDeviceManager9 = nullptr;
	PTR_DXVA2CreateVideoService           pDXVA2CreateVideoService = nullptr;
	CComPtr<IDirect3DDeviceManager9>      m_pD3DDeviceManager;
	UINT                                  m_nResetTocken = 0;
	HANDLE                                m_hDevice = nullptr;

public:
	CMpcVideoRenderer(LPUNKNOWN pUnk, HRESULT* phr);
	~CMpcVideoRenderer();

	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	// IMFGetService
	STDMETHODIMP GetService(REFGUID guidService, REFIID riid, LPVOID *ppvObject);

	// IMFVideoDisplayControl
	STDMETHODIMP GetNativeVideoSize(SIZE *pszVideo, SIZE *pszARVideo) { return E_NOTIMPL; };
	STDMETHODIMP GetIdealVideoSize(SIZE *pszMin, SIZE *pszMax) { return E_NOTIMPL; };
	STDMETHODIMP SetVideoPosition(const MFVideoNormalizedRect *pnrcSource, const LPRECT prcDest) { return E_NOTIMPL; };
	STDMETHODIMP GetVideoPosition(MFVideoNormalizedRect *pnrcSource, LPRECT prcDest) { return E_NOTIMPL; };
	STDMETHODIMP SetAspectRatioMode(DWORD dwAspectRatioMode) { return E_NOTIMPL; };
	STDMETHODIMP GetAspectRatioMode(DWORD *pdwAspectRatioMode) { return E_NOTIMPL; };
	STDMETHODIMP SetVideoWindow(HWND hwndVideo);
	STDMETHODIMP GetVideoWindow(HWND *phwndVideo);
	STDMETHODIMP RepaintVideo(void) { return E_NOTIMPL; };
	STDMETHODIMP GetCurrentImage(BITMAPINFOHEADER *pBih, BYTE **pDib, DWORD *pcbDib, LONGLONG *pTimeStamp) { return E_NOTIMPL; };
	STDMETHODIMP SetBorderColor(COLORREF Clr) { return E_NOTIMPL; };
	STDMETHODIMP GetBorderColor(COLORREF *pClr) { return E_NOTIMPL; };
	STDMETHODIMP SetRenderingPrefs(DWORD dwRenderFlags) { return E_NOTIMPL; };
	STDMETHODIMP GetRenderingPrefs(DWORD *pdwRenderFlags) { return E_NOTIMPL; };
	STDMETHODIMP SetFullscreen(BOOL fFullscreen) { return E_NOTIMPL; };
	STDMETHODIMP GetFullscreen(BOOL *pfFullscreen) { return E_NOTIMPL; };

protected:
	HRESULT InitDirect3D9();
	BOOL InitializeDXVAHDVP(int width, int height);
	HRESULT ResizeDXVAHD(IDirect3DSurface9* pSurface);
	HRESULT ResizeDXVAHD(BYTE* data, const long size);

public:
	HRESULT CheckMediaType(const CMediaType *pmt) override;
	HRESULT SetMediaType(const CMediaType *pmt) override;
	HRESULT DoRenderSample(IMediaSample* pSample) override;
};
