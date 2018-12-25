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
#include <evr9.h> // for IMFVideoProcessor
#include "IVideoRenderer.h"
#include "Helper.h"
#include "FrameStats.h"
#include "resource.h"

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
	: public IMFVideoProcessor
{
private:
	struct Tex_t {
		CComPtr<IDirect3DTexture9> pTexture;
		CComPtr<IDirect3DSurface9> pSurface;
		UINT Width  = 0;
		UINT Height = 0;
		HRESULT Update() {
			if (pTexture) {
				HRESULT hr = pTexture->GetSurfaceLevel(0, &pSurface);
				if (S_OK == hr) {
					D3DSURFACE_DESC desc;
					hr = pTexture->GetLevelDesc(0, &desc);
					if (S_OK == hr) {
						Width  = desc.Width;
						Height = desc.Height;
						return S_OK;
					}
				}
				return hr;
			}
			return E_ABORT;
		}
		void Release() {
			pSurface.Release();
			pTexture.Release();
			Width  = 0;
			Height = 0;
		}
	};

	long m_nRefCount = 1;
	CBaseRenderer* m_pFilter = nullptr;

	bool m_bDeintDouble = false;
	bool m_bShowStats = false;

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
	DXVA2_ValueRange m_DXVA2ProcValueRange[4] = {};

	// Input parameters
	GUID      m_srcSubType      = GUID_NULL;
	D3DFORMAT m_srcD3DFormat    = D3DFMT_UNKNOWN;
	UINT      m_srcWidth        = 0;
	UINT      m_srcHeight       = 0;
	UINT      m_srcPitch        = 0;
	DWORD     m_srcAspectRatioX = 0;
	DWORD     m_srcAspectRatioY = 0;
	CRect m_srcRect;
	CRect m_trgRect;
	DXVA2_ExtendedFormat m_srcExFmt = {};
	bool m_bInterlaced = false;

	// Output parameters
	D3DFORMAT m_VPOutputFmt = D3DFMT_X8R8G8B8;

	// Processing parameters
	VideoSurfaceBuffer m_SrcSamples;
	std::vector<DXVA2_VideoSample> m_DXVA2Samples;
	DXVA2_SampleFormat m_CurrentSampleFmt = DXVA2_SampleProgressiveFrame;
	DXVA2_VideoProcessBltParams m_BltParams = {};
	int m_FieldDrawn = 0;

	CRect m_videoRect;
	CRect m_windowRect;

	CComPtr<IDirect3DTexture9> m_pOSDTexture;
	CComPtr<IDirect3DSurface9> m_pMemSurface;

	// D3D9 Video Processor
	CComPtr<IDirect3DTexture9> m_pSrcVideoTexture;
	CopyFrameDataFn m_pConvertFn = nullptr;
	Tex_t m_TexConvert;
	Tex_t m_TexResize;

	enum {
		shader_catmull_x,
		shader_catmull_y,
		shader_lanczos2_x,
		shader_lanczos2_y,
		shader_downscaler_hamming_x,
		shader_downscaler_hamming_y,
		shader_downscaler_bicubic_x,
		shader_downscaler_bicubic_y,
		shader_convert_color,
		shader_correction_st2084,
		shader_correction_hlg,
		shader_correction_ycgco,
		shader_test,
		shader_count
	};
	struct {
		UINT resid;
		CComPtr<IDirect3DPixelShader9> pShader;
	} m_PixelShaders[shader_count] = {
		{IDF_SHADER_RESIZER_CATMULL4_X},
		{IDF_SHADER_RESIZER_CATMULL4_Y},
		{IDF_SHADER_RESIZER_LANCZOS2_X},
		{IDF_SHADER_RESIZER_LANCZOS2_Y},
		{IDF_SHADER_DOWNSCALER_HAMMING_X},
		{IDF_SHADER_DOWNSCALER_HAMMING_Y},
		{IDF_SHADER_DOWNSCALER_BICUBIC_X},
		{IDF_SHADER_DOWNSCALER_BICUBIC_Y},
		{IDF_SHADER_CONVERT_COLOR},
		{IDF_SHADER_CORRECTION_ST2084},
		{IDF_SHADER_CORRECTION_HLG},
		{IDF_SHADER_CORRECTION_YCGCO},
		{IDF_SHADER_TEST},
	};

	int m_iConvertShader = -1;
	float m_fConstData[4][4] = {};

	CFrameStats m_FrameStats;
	CFrameStats m_DrawnFrameStats;
	struct {
		unsigned skipped1 = 0;
		unsigned skipped2 = 0;
		unsigned failed = 0;

		uint64_t copyticks = 0;
		uint64_t renderticks = 0;
		REFERENCE_TIME syncoffset = 0;

		uint64_t copy1 = 0;
		uint64_t copy2 = 0;
		uint64_t copy3 = 0;
	} m_RenderStats;

	// GDI+ handling
	ULONG_PTR m_gdiplusToken;
	Gdiplus::GdiplusStartupInput m_gdiplusStartupInput;

	CStringW m_strStatsStatic;
	double m_DetectedRefreshRate = 0.0;
	CCritSec m_RefreshRateLock;

	HANDLE m_hEvtQuit; // Stop threads event
	HANDLE m_hSyncThread = nullptr;

public:
	CDX9VideoProcessor(CBaseRenderer* pFilter);
	~CDX9VideoProcessor();

	HRESULT Init(const HWND hwnd, const int iSurfaceFmt, bool* pChangeDevice);

private:
	void ReleaseVP();
	void ReleaseDevice();

	BOOL InitializeDXVA2VP(const D3DFORMAT d3dformat, const UINT width, const UINT height);
	BOOL CreateDXVA2VPDevice(const GUID devguid, const DXVA2_VideoDesc& videodesc);

	BOOL InitializeTexVP(const D3DFORMAT d3dformat, const UINT width, const UINT height);
	HRESULT CreateShaderFromResource(IDirect3DPixelShader9** ppPixelShader, UINT resid);

	void StartWorkerThreads();
	void StopWorkerThreads();

	static DWORD WINAPI SyncThreadStatic(LPVOID lpParam);
	void SyncThread();

public:
	BOOL VerifyMediaType(const CMediaType* pmt);
	BOOL InitMediaType(const CMediaType* pmt);
	void Start();

	HRESULT ProcessSample(IMediaSample* pSample);
	HRESULT CopySample(IMediaSample* pSample);
	// Render: 1 - render first fied or progressive frame, 2 - render second fied, 0 or other - forced repeat of render.
	HRESULT Render(int field);
	HRESULT FillBlack();
	void StopInputBuffer();
	bool SecondFramePossible() { return m_bDeintDouble && m_CurrentSampleFmt >= DXVA2_SampleFieldInterleavedEvenFirst && m_CurrentSampleFmt <= DXVA2_SampleFieldSingleOdd; }

	void SetVideoRect(const CRect& videoRect) { m_videoRect = videoRect; }
	void SetWindowRect(const CRect& windowRect) { m_windowRect = windowRect; }

	IDirect3DDeviceManager9* GetDeviceManager9() { return m_pD3DDeviceManager; }
	HRESULT GetVideoSize(long *pWidth, long *pHeight);
	HRESULT GetAspectRatio(long *plAspectX, long *plAspectY);
	HRESULT GetFrameInfo(VRFrameInfo* pFrameInfo);
	HRESULT GetAdapterDecription(CStringW& str);
	HRESULT GetDXVA2VPCaps(DXVA2_VideoProcessorCaps* pDXVA2VPCaps);

	bool GetDeintDouble() { return m_bDeintDouble; }
	void SetDeintDouble(bool value) { m_bDeintDouble = value; };
	bool GetShowStats() { return m_bShowStats; }
	void SetShowStats(bool value) { m_bShowStats = value; };

private:
	HRESULT ProcessDXVA2(IDirect3DSurface9* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect, const bool second);

	HRESULT ProcessTex(IDirect3DSurface9* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect);
	HRESULT TextureCopy(IDirect3DTexture9* pTexture);
	HRESULT TextureResize(IDirect3DTexture9* pTexture, const CRect& srcRect, const CRect& destRect, D3DTEXTUREFILTERTYPE filter);
	HRESULT TextureResizeShader(IDirect3DTexture9* pTexture, const CRect& srcRect, const CRect& destRect, IDirect3DPixelShader9* pShader);

	HRESULT AlphaBlt(RECT* pSrc, RECT* pDst, IDirect3DTexture9* pTexture);
	void UpdateStatsStatic();
	HRESULT DrawStats();

public:
	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// IMFVideoProcessor
	STDMETHODIMP GetAvailableVideoProcessorModes(UINT *lpdwNumProcessingModes, GUID **ppVideoProcessingModes) { return E_NOTIMPL; }
	STDMETHODIMP GetVideoProcessorCaps(LPGUID lpVideoProcessorMode, DXVA2_VideoProcessorCaps *lpVideoProcessorCaps) { return E_NOTIMPL; }
	STDMETHODIMP GetVideoProcessorMode(LPGUID lpMode) { return E_NOTIMPL; }
	STDMETHODIMP SetVideoProcessorMode(LPGUID lpMode) { return E_NOTIMPL; }
	STDMETHODIMP GetProcAmpRange(DWORD dwProperty, DXVA2_ValueRange *pPropRange);
	STDMETHODIMP GetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *Values);
	STDMETHODIMP SetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *pValues);
	STDMETHODIMP GetFilteringRange(DWORD dwProperty, DXVA2_ValueRange *pPropRange) { return E_NOTIMPL; }
	STDMETHODIMP GetFilteringValue(DWORD dwProperty, DXVA2_Fixed32 *pValue) { return E_NOTIMPL; }
	STDMETHODIMP SetFilteringValue(DWORD dwProperty, DXVA2_Fixed32 *pValue) { return E_NOTIMPL; }
	STDMETHODIMP GetBackgroundColor(COLORREF *lpClrBkg);
	STDMETHODIMP SetBackgroundColor(COLORREF ClrBkg) { return E_NOTIMPL; }
};
