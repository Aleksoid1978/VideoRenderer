/*
* (C) 2018-2019 see Authors.txt
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

#define D3D9FONT_ENABLE 0

#include <atltypes.h>
#include <evr9.h> // for IMFVideoProcessor
#include "IVideoRenderer.h"
#include "Helper.h"
#include "DX9Helper.h"
#include "FrameStats.h"

#if D3D9FONT_ENABLE
#include "D3D9Font.h"
#include "D3D9Geometry.h"
#else
#include "StatsDrawing.h"
#endif

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

class CMpcVideoRenderer;

class CDX9VideoProcessor
	: public IMFVideoProcessor
{
private:
	long m_nRefCount = 1;
	CMpcVideoRenderer* m_pFilter = nullptr;

	bool m_bShowStats          = false;
	int  m_iTexFormat          = TEXFMT_AUTOINT;
	VPEnableFormats_t m_VPFormats = {true, true, true, true};
	bool m_bDeintDouble        = false;
	bool m_bVPScaling          = true;
	int  m_iChromaScaling      = CHROMA_Bilinear;
	int  m_iUpscaling          = UPSCALE_CatmullRom; // interpolation
	int  m_iDownscaling        = DOWNSCALE_Hamming;  // convolution
	bool m_bInterpolateAt50pct = true;
	int  m_iSwapEffect         = SWAPEFFECT_Discard;

	// Direct3D 9
	CComPtr<IDirect3D9Ex>            m_pD3DEx;
	CComPtr<IDirect3DDevice9Ex>      m_pD3DDevEx;
	CComPtr<IDirect3DDeviceManager9> m_pD3DDeviceManager;
	UINT    m_nResetTocken = 0;
	DWORD   m_VendorId = 0;
	CString m_strAdapterDescription;

	HWND m_hWnd = nullptr;
	UINT m_nCurrentAdapter = D3DADAPTER_DEFAULT;
	D3DDISPLAYMODEEX m_DisplayMode = { sizeof(D3DDISPLAYMODEEX) };
	D3DPRESENT_PARAMETERS m_d3dpp = {};

	// DXVA2 Video Processor
	CComPtr<IDirectXVideoProcessorService> m_pDXVA2_VPService;
	CComPtr<IDirectXVideoProcessor> m_pDXVA2_VP;
	GUID m_DXVA2VPGuid = GUID_NULL;
	DXVA2_VideoProcessorCaps m_DXVA2VPcaps = {};
	DXVA2_ValueRange m_DXVA2ProcAmpRanges[4] = {};

	// Input parameters
	FmtConvParams_t m_srcParams = {};
	D3DFORMAT m_srcDXVA2Format = D3DFMT_UNKNOWN;
	CopyFrameDataFn m_pConvertFn = nullptr;
	UINT  m_srcWidth        = 0;
	UINT  m_srcHeight       = 0;
	UINT  m_srcRectWidth    = 0;
	UINT  m_srcRectHeight   = 0;
	int   m_srcPitch        = 0;
	DWORD m_srcAspectRatioX = 0;
	DWORD m_srcAspectRatioY = 0;
	CRect m_srcRect;
	CRect m_trgRect;
	DXVA2_ExtendedFormat m_decExFmt = {};
	DXVA2_ExtendedFormat m_srcExFmt = {};
	bool m_bInterlaced = false;

	// DXVA2 decoder surface parameters
	UINT      m_SurfaceWidth  = 0;
	UINT      m_SurfaceHeight = 0;

	// intermediate texture format
	D3DFORMAT m_InternalTexFmt = D3DFMT_X8R8G8B8;

	// Processing parameters
	VideoSurfaceBuffer m_SrcSamples;
	std::vector<DXVA2_VideoSample> m_DXVA2Samples;
	DXVA2_SampleFormat m_CurrentSampleFmt = DXVA2_SampleProgressiveFrame;
	DXVA2_VideoProcessBltParams m_BltParams = {};
	int m_FieldDrawn = 0;

	CRect m_videoRect;
	CRect m_windowRect;

	CRect m_srcRenderRect;
	CRect m_dstRenderRect;

	// D3D9 Video Processor
	Tex9Video_t m_TexSrcVideo; // for copy of frame
	Tex_t m_TexConvert;    // for result of color conversion
	Tex_t m_TexCorrection; // for result of correction after DXVA2 VP
	Tex_t m_TexResize;     // for intermediate result of two-pass resize

	CComPtr<IDirect3DPixelShader9> m_pPSCorrection;
	CComPtr<IDirect3DPixelShader9> m_pPSConvertColor;
	struct {
		bool bEnable = false;
		float fConstants[4][4] = {};
	} m_PSConvColorData;

	CComPtr<IDirect3DPixelShader9> m_pShaderUpscaleX;
	CComPtr<IDirect3DPixelShader9> m_pShaderUpscaleY;
	CComPtr<IDirect3DPixelShader9> m_pShaderDownscaleX;
	CComPtr<IDirect3DPixelShader9> m_pShaderDownscaleY;
	const wchar_t* m_strShaderUpscale   = nullptr;
	const wchar_t* m_strShaderDownscale = nullptr;

	CRenderStats m_RenderStats;
	CStringW m_strStatsStatic1;
	CStringW m_strStatsStatic2;
	bool m_bSrcFromGPU = false;

	Tex_t m_TexStats;
#if D3D9FONT_ENABLE
	CD3D9Font m_Font3D;
	CD3D9Rectangle m_Rect3D;
#else
	CComPtr<IDirect3DSurface9> m_pMemOSDSurface;
	CStatsDrawing m_StatsDrawing;
#endif

	REFERENCE_TIME m_rtStart = 0;

public:
	CDX9VideoProcessor(CMpcVideoRenderer* pFilter);
	~CDX9VideoProcessor();

	HRESULT Init(const HWND hwnd, bool* pChangeDevice);
	bool Initialized();

private:
	void ReleaseVP();
	void ReleaseDevice();

	HRESULT InitializeDXVA2VP(const FmtConvParams_t& params, const UINT width, const UINT height, bool only_update_surface);
	BOOL CreateDXVA2VPDevice(const GUID devguid, const DXVA2_VideoDesc& videodesc);

	HRESULT InitializeTexVP(const FmtConvParams_t& params, const UINT width, const UINT height);
	HRESULT CreatePShaderFromResource(IDirect3DPixelShader9** ppPixelShader, UINT resid);
	void SetShaderConvertColorParams();

public:
	BOOL VerifyMediaType(const CMediaType* pmt);
	BOOL InitMediaType(const CMediaType* pmt);

	BOOL GetAlignmentSize(const CMediaType& mt, SIZE& Size);

	void Start();
	void Stop();

	HRESULT ProcessSample(IMediaSample* pSample);
	HRESULT CopySample(IMediaSample* pSample);
	// Render: 1 - render first fied or progressive frame, 2 - render second fied, 0 or other - forced repeat of render.
	HRESULT Render(int field);
	HRESULT FillBlack();
	bool SecondFramePossible() { return m_bDeintDouble && m_CurrentSampleFmt >= DXVA2_SampleFieldInterleavedEvenFirst && m_CurrentSampleFmt <= DXVA2_SampleFieldSingleOdd; }

	void GetSourceRect(CRect& sourceRect) { sourceRect = m_srcRect; }
	void GetVideoRect(CRect& videoRect) { videoRect = m_videoRect; }
	void SetVideoRect(const CRect& videoRect);
	HRESULT SetWindowRect(const CRect& windowRect);

	IDirect3DDeviceManager9* GetDeviceManager9() { return m_pD3DDeviceManager; }
	HRESULT GetVideoSize(long *pWidth, long *pHeight);
	HRESULT GetAspectRatio(long *plAspectX, long *plAspectY);
	HRESULT GetCurentImage(long *pDIBImage);
	HRESULT GetVPInfo(CStringW& str);
	ColorFormat_t GetColorFormat() { return m_srcParams.cformat; }

	void SetDeintDouble(bool value) { m_bDeintDouble = value; }
	void SetShowStats(bool value)   { m_bShowStats   = value; }
	void SetTexFormat(int value);
	void SetVPEnableFmts(VPEnableFormats_t& VPFormats);
	void SetVPScaling(bool value);
	void SetChromaScaling(int value);
	void SetUpscaling(int value);
	void SetDownscaling(int value);
	void SetInterpolateAt50pct(bool value) { m_bInterpolateAt50pct = value; }
	void SetSwapEffect(int value) { m_iSwapEffect = value; }

private:
	HRESULT DXVA2VPPass(IDirect3DSurface9* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect, const bool second);
	void UpdateVideoTexDXVA2VP();
	void UpdateCorrectionTex(const int w, const int h);
	void UpdateUpscalingShaders();
	void UpdateDownscalingShaders();
	HRESULT UpdateChromaScalingShader();

	HRESULT ProcessDXVA2(IDirect3DSurface9* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect, const bool second);
	HRESULT ProcessTex(IDirect3DSurface9* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect);

	HRESULT ResizeShader2Pass(IDirect3DTexture9* pTexture, IDirect3DSurface9* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect);

	HRESULT TextureCopy(IDirect3DTexture9* pTexture);
	HRESULT TextureConvertColor(Tex9Video_t& texVideo);
	HRESULT TextureCopyRect(IDirect3DTexture9* pTexture, const CRect& srcRect, const CRect& destRect, D3DTEXTUREFILTERTYPE filter);
	HRESULT TextureResizeShader(IDirect3DTexture9* pTexture, const CRect& srcRect, const CRect& destRect, IDirect3DPixelShader9* pShader);

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
