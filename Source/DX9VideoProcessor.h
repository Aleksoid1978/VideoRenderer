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

#include <atltypes.h>
#include <gdiplus.h>
#include "IVideoRenderer.h"

struct VideoSurface {
	REFERENCE_TIME Start = 0;
	REFERENCE_TIME End = 0;
	CComPtr<IDirect3DSurface9> pSrcSurface;
	DXVA2_SampleFormat SampleFormat = DXVA2_SampleUnknown;
};

class VideoSurfaceBuffer
{
	std::vector<VideoSurface> m_Surfaces;
	unsigned m_LastPos = 0;

public:
	unsigned Size() const {
		return (unsigned)m_Surfaces.size();
	}
	bool Empty() const {
		return m_Surfaces.empty();
	}
	void Clear() {
		m_Surfaces.clear();
	}
	void Resize(const unsigned size) {
		Clear();
		m_Surfaces.resize(size);
		m_LastPos = Size() - 1;
	}
	VideoSurface& Get() {
		return m_Surfaces[m_LastPos];
	}
	VideoSurface& GetAt(const unsigned pos) {
		unsigned InternalPos = (m_LastPos + 1 + pos) % Size();
		return m_Surfaces[InternalPos];
	}
	void Next() {
		m_LastPos++;
		if (m_LastPos >= Size()) {
			m_LastPos = 0;
		}
	}
};

class CDX9VideoProcessor
{
private:
	// Direct3D 9
	HMODULE m_hD3D9Lib = nullptr;
	CComPtr<IDirect3D9Ex>            m_pD3DEx;
	CComPtr<IDirect3DDevice9Ex>      m_pD3DDevEx;
	CComPtr<IDirect3DDeviceManager9> m_pD3DDeviceManager;
	UINT    m_nResetTocken = 0;
	DWORD   m_VendorId = 0;
	CString m_strAdapterDescription;

	HWND m_hWnd = nullptr;
	UINT m_CurrentAdapter = D3DADAPTER_DEFAULT;
	D3DDISPLAYMODEEX m_DisplayMode = { sizeof(D3DDISPLAYMODEEX) };
	D3DPRESENT_PARAMETERS m_d3dpp = {};

	// DXVA2 Video Processor
	CComPtr<IDirectXVideoProcessorService> m_pDXVA2_VPService;
	CComPtr<IDirectXVideoProcessor> m_pDXVA2_VP;
	GUID m_DXVA2VPGuid = GUID_NULL;
	DXVA2_VideoProcessorCaps m_DXVA2VPcaps = {};
	DXVA2_Fixed32 m_DXVA2ProcAmpValues[4] = {};

	// Input parameters
	D3DFORMAT m_srcD3DFormat = D3DFMT_UNKNOWN;
	UINT m_srcWidth = 0;
	UINT m_srcHeight = 0;
	UINT m_srcPitch = 0;
	DWORD m_srcAspectRatioX = 0;
	DWORD m_srcAspectRatioY = 0;
	DXVA2_ExtendedFormat m_srcExFmt = {};
	bool m_bInterlaced = false;
	RECT m_srcRect = {};
	RECT m_trgRect = {};

	// Processing parameters
	DWORD m_frame = 0;
	VideoSurfaceBuffer m_SrcSamples;
	std::vector<DXVA2_VideoSample> m_DXVA2Samples;
	DXVA2_SampleFormat m_CurrentSampleFmt = DXVA2_SampleProgressiveFrame;
	DXVA2_VideoProcessBltParams m_BltParams = {};

	CRect m_videoRect;
	CRect m_windowRect;

	D3DFORMAT m_D3D9_Src_Format = D3DFMT_UNKNOWN;
	UINT m_D3D9_Src_Width = 0;
	UINT m_D3D9_Src_Height = 0;

	bool m_bShowStats = false;
	CComPtr<IDirect3DTexture9> m_pOSDTexture;
	CComPtr<IDirect3DSurface9> m_pMemSurface;

	// GDI+ handling
	ULONG_PTR m_gdiplusToken;
	Gdiplus::GdiplusStartupInput m_gdiplusStartupInput;

public:
	CDX9VideoProcessor();
	~CDX9VideoProcessor();

	HRESULT Init(const HWND hwnd, bool* pChangeDevice = nullptr);

private:
	BOOL CheckInput(const D3DFORMAT d3dformat, const UINT width, const UINT height);
	BOOL InitializeDXVA2VP(const D3DFORMAT d3dformat, const UINT width, const UINT height);
	BOOL CreateDXVA2VPDevice(const GUID devguid, const DXVA2_VideoDesc& videodesc);

public:
	BOOL InitMediaType(const CMediaType* pmt);
	HRESULT CopySample(IMediaSample* pSample);
	HRESULT Render(const FILTER_STATE filterState, const bool deintDouble = false);
	void StopInputBuffer();

	void SetVideoRect(const CRect& videoRect) { m_videoRect = videoRect; }
	void SetWindowRect(const CRect& windowRect) { m_windowRect = windowRect; }

	IDirect3DDeviceManager9* GetDeviceManager9() { return m_pD3DDeviceManager; }
	HRESULT GetVideoSize(long *pWidth, long *pHeight);
	HRESULT GetAspectRatio(long *plAspectX, long *plAspectY);
	HRESULT GetFrameInfo(VRFrameInfo* pFrameInfo);
	HRESULT GetAdapterDecription(CStringW& str);
	HRESULT GetDXVA2VPCaps(DXVA2_VideoProcessorCaps* pDXVA2VPCaps);
	bool GetShowStats() { return m_bShowStats; }
	void SetShowStats(bool value) { m_bShowStats = value; };

private:
	HRESULT ProcessDXVA2(IDirect3DSurface9* pRenderTarget, const bool second);
	HRESULT AlphaBlt(RECT* pSrc, RECT* pDst, IDirect3DTexture9* pTexture);
	HRESULT DrawStats();
};
