/*
 * (C) 2020 see Authors.txt
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

#include <evr9.h>
#include "DisplayConfig.h"
#include "FrameStats.h"

enum : int {
	VP_DX9 = 9,
	VP_DX11 = 11
};

class CMpcVideoRenderer;

class CVideoProcessor
	: public IMFVideoProcessor
	, public IMFVideoMixerBitmap
{
protected:
	long m_nRefCount = 1;
	CMpcVideoRenderer* m_pFilter = nullptr;

	// Settings
	bool m_bShowStats          = false;
	int  m_iResizeStats        = 0;
	int  m_iTexFormat          = TEXFMT_AUTOINT;
	VPEnableFormats_t m_VPFormats = {true, true, true, true};
	bool m_bDeintDouble        = true;
	bool m_bVPScaling          = true;
	int  m_iChromaScaling      = CHROMA_Bilinear;
	int  m_iUpscaling          = UPSCALE_CatmullRom; // interpolation
	int  m_iDownscaling        = DOWNSCALE_Hamming;  // convolution
	bool m_bInterpolateAt50pct = true;
	bool m_bUseDither          = true;
	int  m_iSwapEffect         = SWAPEFFECT_Discard;
	bool m_bHdrPassthrough     = true;
	bool m_bHdrToggleDisplay   = true;
	bool m_bConvertToSdr       = true;

	// Input parameters
	FmtConvParams_t m_srcParams = {};
	CopyFrameDataFn m_pConvertFn = nullptr;
	UINT  m_srcWidth        = 0;
	UINT  m_srcHeight       = 0;
	UINT  m_srcRectWidth    = 0;
	UINT  m_srcRectHeight   = 0;
	int   m_srcPitch        = 0;
	UINT  m_srcLines        = 0;
	DWORD m_srcAspectRatioX = 0;
	DWORD m_srcAspectRatioY = 0;
	bool  m_srcAnamorphic   = false;
	CRect m_srcRect;
	DXVA2_ExtendedFormat m_decExFmt = {};
	DXVA2_ExtendedFormat m_srcExFmt = {};
	bool  m_bInterlaced = false;
	REFERENCE_TIME m_rtAvgTimePerFrame = 0;

	CRect m_videoRect;
	CRect m_windowRect;
	CRect m_renderRect;

	int  m_iRotation  = 0;
	bool m_bFlip      = false;
	bool m_bFinalPass = false;

	DXVA2_ValueRange m_DXVA2ProcAmpRanges[4] = {};
	DXVA2_ProcAmpValues m_DXVA2ProcAmpValues = {};

	HWND m_hWnd = nullptr;
	UINT m_nCurrentAdapter; // set it in subclasses
	DWORD m_VendorId = 0;
	std::wstring m_strAdapterDescription;

	REFERENCE_TIME m_rtStart = 0;
	int m_FieldDrawn = 0;
	bool m_bDoubleFrames = false;

	// AlphaBitmap
	bool m_bAlphaBitmapEnable = false;
	RECT m_AlphaBitmapRectSrc = {};
	MFVideoNormalizedRect m_AlphaBitmapNRectDest = {};

	// Statistics
	CRenderStats m_RenderStats;
	std::wstring m_strStatsStatic1;
	std::wstring m_strStatsStatic2;
	std::wstring m_strStatsDispInfo;
	std::wstring m_strStatsPostProc;
	std::wstring m_strStatsHDR;
	std::wstring m_strStatsPresent;
	int m_iSrcFromGPU = 0;
	D3DDISPLAYMODEEX* m_pDisplayMode = nullptr;
	const wchar_t* m_strShaderX = nullptr;
	const wchar_t* m_strShaderY = nullptr;
	int m_StatsFontH = 14;
	RECT m_StatsRect = { 10, 10, 10 + 5 + 63*8 + 3, 10 + 5 + 17*17 + 3 };
	const POINT m_StatsTextPoint = { 10 + 5, 10 + 5};

	// Graph of a function
	CMovingAverage<int> m_Syncs = CMovingAverage<int>(120);
	int m_Xstep  = 4;
	int m_Yscale = 2;
	RECT m_GraphRect = {};
	int m_Yaxis  = 0;

	CVideoProcessor(CMpcVideoRenderer* pFilter) : m_pFilter(pFilter) {}

public:
	virtual ~CVideoProcessor() = default;

	virtual int Type() = 0;

	virtual HRESULT Init(const HWND hwnd, bool* pChangeDevice = nullptr) = 0;

	virtual IDirect3DDeviceManager9* GetDeviceManager9() = 0;
	UINT GetCurrentAdapter() { return m_nCurrentAdapter; }

	virtual BOOL VerifyMediaType(const CMediaType* pmt) = 0;
	virtual BOOL InitMediaType(const CMediaType* pmt) = 0;

	virtual BOOL GetAlignmentSize(const CMediaType& mt, SIZE& Size) = 0;

	virtual HRESULT ProcessSample(IMediaSample* pSample) = 0;
	virtual HRESULT Render(int field) = 0;
	virtual HRESULT FillBlack() = 0;

	void Start() { m_rtStart = 0; }
	virtual void Flush() = 0;
	virtual HRESULT Reset() = 0;

	virtual bool IsInit() const { return false; }

	ColorFormat_t GetColorFormat() { return m_srcParams.cformat; }

	void GetSourceRect(CRect& sourceRect) { sourceRect = m_srcRect; }
	void GetVideoRect(CRect& videoRect) { videoRect = m_videoRect; }
	virtual void SetVideoRect(const CRect& videoRect) = 0;
	virtual HRESULT SetWindowRect(const CRect& windowRect) = 0;

	HRESULT GetVideoSize(long *pWidth, long *pHeight);
	HRESULT GetAspectRatio(long *plAspectX, long *plAspectY);

	// Settings
	void SetShowStats(bool value) { m_bShowStats   = value; }
	virtual void Configure(const Settings_t& config) = 0;

	int GetRotation() { return m_iRotation; }
	virtual void SetRotation(int value) = 0;
	int GetFlip() { return m_bFlip; }
	void SetFlip(bool value) { m_bFlip = value; }

	virtual void ClearPostScaleShaders() = 0;
	virtual HRESULT AddPostScaleShader(const std::wstring& name, const std::string& srcCode) = 0;

	virtual HRESULT GetCurentImage(long *pDIBImage) = 0;
	virtual HRESULT GetDisplayedImage(BYTE **ppDib, unsigned *pSize) = 0;
	virtual HRESULT GetVPInfo(std::wstring& str) = 0;

	void CalcStatsFont();
	bool CheckGraphPlacement();
	void CalcGraphParams();
	virtual void SetGraphSize() = 0;

	void SetDisplayInfo(const DisplayConfig_t& displayConfig, const bool primary, const bool fullscreen);

protected:
	bool SourceIsHDR() {
		return m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084 || m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG;
	}

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
	STDMETHODIMP GetFilteringRange(DWORD dwProperty, DXVA2_ValueRange *pPropRange) { return E_NOTIMPL; }
	STDMETHODIMP GetFilteringValue(DWORD dwProperty, DXVA2_Fixed32 *pValue) { return E_NOTIMPL; }
	STDMETHODIMP SetFilteringValue(DWORD dwProperty, DXVA2_Fixed32 *pValue) { return E_NOTIMPL; }
	STDMETHODIMP GetBackgroundColor(COLORREF *lpClrBkg);
	STDMETHODIMP SetBackgroundColor(COLORREF ClrBkg) { return E_NOTIMPL; }

	// IMFVideoMixerBitmap
	STDMETHODIMP ClearAlphaBitmap() override;
	STDMETHODIMP GetAlphaBitmapParameters(MFVideoAlphaBitmapParams *pBmpParms) override;
};
