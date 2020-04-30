/*
* (C) 2018-2020 see Authors.txt
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

#include <evr9.h> // for IMFVideoProcessor
#include "IVideoRenderer.h"
#include "Helper.h"
#include "DX9Helper.h"
#include "DXVA2VP.h"
#include "FrameStats.h"
#include "D3DUtil/D3D9Font.h"
#include "D3DUtil/D3D9Geometry.h"

class CMpcVideoRenderer;

class CDX9VideoProcessor
	: public IMFVideoProcessor
	, public IMFVideoMixerBitmap
{
private:
	long m_nRefCount = 1;
	CMpcVideoRenderer* m_pFilter = nullptr;

	bool m_bShowStats          = false;
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

	// Direct3D 9
	CComPtr<IDirect3D9Ex>            m_pD3DEx;
	CComPtr<IDirect3DDevice9Ex>      m_pD3DDevEx;
	CComPtr<IDirect3DDeviceManager9> m_pD3DDeviceManager;
	UINT     m_nResetTocken = 0;
	DWORD    m_VendorId = 0;
	CStringW m_strAdapterDescription;

	HWND m_hWnd = nullptr;
	UINT m_nCurrentAdapter = D3DADAPTER_DEFAULT;
	D3DDISPLAYMODEEX m_DisplayMode = { sizeof(D3DDISPLAYMODEEX) };
	D3DPRESENT_PARAMETERS m_d3dpp = {};

	bool   m_bPrimaryDisplay     = false;
	double m_dRefreshRate        = 0.0;
	double m_dRefreshRatePrimary = 0.0;

	// DXVA2 Video Processor
	CDXVA2VP m_DXVA2VP;
	DXVA2_ValueRange m_DXVA2ProcAmpRanges[4] = {};
	DXVA2_ProcAmpValues m_DXVA2ProcAmpValues = {};

	// Input parameters
	FmtConvParams_t m_srcParams = {};
	D3DFORMAT m_srcDXVA2Format = D3DFMT_UNKNOWN;
	CopyFrameDataFn m_pConvertFn = nullptr;
	UINT  m_srcWidth        = 0;
	UINT  m_srcHeight       = 0;
	UINT  m_srcRectWidth    = 0;
	UINT  m_srcRectHeight   = 0;
	int   m_srcPitch        = 0;
	UINT  m_srcLines        = 0;
	DWORD m_srcAspectRatioX = 0;
	DWORD m_srcAspectRatioY = 0;
	CRect m_srcRect;
	DXVA2_ExtendedFormat m_decExFmt = {};
	DXVA2_ExtendedFormat m_srcExFmt = {};
	bool  m_bInterlaced = false;
	REFERENCE_TIME m_rtAvgTimePerFrame = 0;

	// DXVA2 surface format
	D3DFORMAT m_DXVA2OutputFmt = D3DFMT_UNKNOWN;

	// intermediate texture format
	D3DFORMAT m_InternalTexFmt = D3DFMT_X8R8G8B8;

	// Processing parameters
	DXVA2_SampleFormat m_CurrentSampleFmt = DXVA2_SampleProgressiveFrame;
	int m_FieldDrawn = 0;

	CRect m_videoRect;
	CRect m_windowRect;
	CRect m_renderRect;

	int m_iRotation = 0;
	bool m_bFinalPass = false;

	// D3D9 Video Processor
	Tex9Video_t m_TexSrcVideo; // for copy of frame
	Tex_t m_TexDxvaOutput;
	Tex_t m_TexConvertOutput;
	Tex_t m_TexResize;         // for intermediate result of two-pass resize
	CTexRing m_TexsPostScale;
	Tex_t m_TexDither;

	CComPtr<IDirect3DPixelShader9> m_pPSCorrection;
	const wchar_t* m_strCorrection = nullptr;
	CComPtr<IDirect3DPixelShader9> m_pPSConvertColor;
	struct {
		bool bEnable = false;
		struct ConvColorVertex_t {
			DirectX::XMFLOAT4 Pos;
			DirectX::XMFLOAT2 Tex[2];
		} VertexData[4] = {};
		float fConstants[4][4] = {};
	} m_PSConvColorData;

	CComPtr<IDirect3DPixelShader9> m_pShaderUpscaleX;
	CComPtr<IDirect3DPixelShader9> m_pShaderUpscaleY;
	CComPtr<IDirect3DPixelShader9> m_pShaderDownscaleX;
	CComPtr<IDirect3DPixelShader9> m_pShaderDownscaleY;
	const wchar_t* m_strShaderX = nullptr;
	const wchar_t* m_strShaderY = nullptr;

	std::vector<ExternalPixelShader9_t> m_pPostScaleShaders;
	CComPtr<IDirect3DPixelShader9> m_pPSFinalPass;

	CRenderStats m_RenderStats;
	CStringW m_strStatsStatic1;
	CStringW m_strStatsStatic2;
	CStringW m_strStatsStatic3;
	CStringW m_strStatsStatic4;
	int m_iSrcFromGPU = 0;

	Tex_t m_TexStats;
	CD3D9Font      m_Font3D;
	CD3D9Rectangle m_Rect3D;
	CD3D9Rectangle m_Underlay;
	CD3D9Lines     m_Lines;
	CD3D9Polyline  m_SyncLine;
	CMovingAverage<int> m_Syncs = CMovingAverage<int>(100);
	int m_Xstep  = 5;
	int m_Xstart = 0;
	int m_Yaxis  = 0;

	REFERENCE_TIME m_rtStart = 0;

	bool     m_bAlphaBitmapEnable = false;
	Tex_t    m_TexAlphaBitmap;
	RECT     m_AlphaBitmapRectSrc = {};
	MFVideoNormalizedRect m_AlphaBitmapNRectDest = {};

public:
	CDX9VideoProcessor(CMpcVideoRenderer* pFilter);
	~CDX9VideoProcessor();

	HRESULT Init(const HWND hwnd, bool* pChangeDevice);
	bool Initialized();

private:
	void ReleaseVP();
	void ReleaseDevice();

	HRESULT InitializeDXVA2VP(const FmtConvParams_t& params, const UINT width, const UINT height);
	HRESULT InitializeTexVP(const FmtConvParams_t& params, const UINT width, const UINT height);
	void UpdatFrameProperties(); // use this after receiving modified frame from hardware decoder

	HRESULT CreatePShaderFromResource(IDirect3DPixelShader9** ppPixelShader, UINT resid);
	void SetShaderConvertColorParams();

	void UpdateRenderRect();

public:
	BOOL VerifyMediaType(const CMediaType* pmt);
	BOOL InitMediaType(const CMediaType* pmt);

	BOOL GetAlignmentSize(const CMediaType& mt, SIZE& Size);

	void Start();

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
	HRESULT GetDisplayedImage(BYTE **ppDib, unsigned *pSize);
	HRESULT GetVPInfo(CStringW& str);
	ColorFormat_t GetColorFormat() { return m_srcParams.cformat; }

	void SetDeintDouble(bool value) { m_bDeintDouble = value; }
	void SetShowStats(bool value)   { m_bShowStats   = value; }
	void SetTexFormat(int value);
	void SetVPEnableFmts(const VPEnableFormats_t& VPFormats);
	void SetVPScaling(bool value);
	void SetChromaScaling(int value);
	void SetUpscaling(int value);
	void SetDownscaling(int value);
	void SetInterpolateAt50pct(bool value) { m_bInterpolateAt50pct = value; }
	void SetDither(bool value);
	void SetSwapEffect(int value) { m_iSwapEffect = value; }

	void SetRotation(int value);
	int GetRotation() { return m_iRotation; }

	void Flush();

	void UpdateDiplayInfo();
	void ClearPostScaleShaders();
	HRESULT AddPostScaleShader(const CStringW& name, const CStringA& srcCode);

private:
	void UpdateTexures(int w, int h);
	void UpdatePostScaleTexures();
	void UpdateUpscalingShaders();
	void UpdateDownscalingShaders();
	HRESULT UpdateChromaScalingShader();

	HRESULT DxvaVPPass(IDirect3DSurface9* pRenderTarget, const CRect& srcRect, const CRect& dstRect, const bool second);
	HRESULT ConvertColorPass(IDirect3DSurface9* pRenderTarget);
	HRESULT ResizeShaderPass(IDirect3DTexture9* pTexture, IDirect3DSurface9* pRenderTarget, const CRect& srcRect, const CRect& dstRect);
	HRESULT FinalPass(IDirect3DTexture9* pTexture, IDirect3DSurface9* pRenderTarget, const CRect& srcRect, const CRect& dstRect);

	HRESULT Process(IDirect3DSurface9* pRenderTarget, const CRect& srcRect, const CRect& dstRect, const bool second);

	HRESULT TextureCopy(IDirect3DTexture9* pTexture);
	HRESULT TextureCopyRect(IDirect3DTexture9* pTexture, const CRect& srcRect, const CRect& dstRect, D3DTEXTUREFILTERTYPE filter, const int iRotation);
	HRESULT TextureResizeShader(IDirect3DTexture9* pTexture, const CRect& srcRect, const CRect& dstRect, IDirect3DPixelShader9* pShader, const int iRotation);

	void UpdateStatsStatic();
	void UpdateStatsStatic3();
	HRESULT DrawStats(IDirect3DSurface9* pRenderTarget);

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

	// IMFVideoMixerBitmap
	STDMETHODIMP ClearAlphaBitmap() override;
	STDMETHODIMP GetAlphaBitmapParameters(MFVideoAlphaBitmapParams *pBmpParms) override;
	STDMETHODIMP SetAlphaBitmap(const MFVideoAlphaBitmap *pBmpParms) override;
	STDMETHODIMP UpdateAlphaBitmapParameters(const MFVideoAlphaBitmapParams *pBmpParms) override;
};
