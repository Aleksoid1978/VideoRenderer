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

#include <atltypes.h>
#include <ntverp.h>
#include <DXGI1_2.h>
#include <dxva2api.h>
#include <strmif.h>
#include <evr9.h> // for IMFVideoProcessor
#include "IVideoRenderer.h"
#include "DX11Helper.h"
#include "D3D11VP.h"
#include "FrameStats.h"
#include "D3DUtil/D3D11Font.h"
#include "D3DUtil/D3D11Geometry.h"
#include "DX9Device.h"


class CMpcVideoRenderer;
class CVideoRendererInputPin;

class CDX11VideoProcessor
	: public CDX9Device
	, public IMFVideoProcessor
{
private:
	friend class CVideoRendererInputPin;

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
	int  m_iSwapEffect         = SWAPEFFECT_Discard;

	CComPtr<ID3D11Device>        m_pDevice;
	CComPtr<ID3D11DeviceContext> m_pDeviceContext;
	ID3D11SamplerState*          m_pSamplerPoint = nullptr;
	ID3D11SamplerState*          m_pSamplerLinear = nullptr;
	CComPtr<ID3D11BlendState>    m_pAlphaBlendState;
	CComPtr<ID3D11BlendState>    m_pAlphaBlendStateInv;
	ID3D11Buffer*                m_pFullFrameVertexBuffer = nullptr;
	CComPtr<ID3D11VertexShader>  m_pVS_Simple;
	CComPtr<ID3D11PixelShader>   m_pPS_Simple;
	CComPtr<ID3D11InputLayout>   m_pVSimpleInputLayout;

	Tex11Video_t m_TexSrcVideo; // for copy of frame
	Tex2D_t m_TexConvert;     // for result of color conversion
	Tex2D_t m_TexCorrection;  // for result of correction after D3D11 VP
	Tex2D_t m_TexResize;      // for intermediate result of two-pass resize

	// D3D11 Video Processor
	CD3D11VP m_D3D11VP;
	CComPtr<ID3D11PixelShader> m_pPSCorrection;

	// D3D11 Shader Video Processor
	CComPtr<ID3D11PixelShader> m_pPSConvertColor;
	struct {
		bool bEnable = false;
		ID3D11Buffer* pConstants = nullptr;
	} m_PSConvColorData;
	CComPtr<ID3D11PixelShader> m_pShaderUpscaleX;
	CComPtr<ID3D11PixelShader> m_pShaderUpscaleY;
	CComPtr<ID3D11PixelShader> m_pShaderDownscaleX;
	CComPtr<ID3D11PixelShader> m_pShaderDownscaleY;
	const wchar_t* m_strShaderX = nullptr;
	const wchar_t* m_strShaderY = nullptr;

	std::vector<ExternalPixelShader11_t> m_pPostScaleShaders;

	CComPtr<IDXGIFactory2> m_pDXGIFactory2;
	CComPtr<IDXGISwapChain1> m_pDXGISwapChain1;

	// Input parameters
	FmtConvParams_t m_srcParams = {};
	DXGI_FORMAT m_srcDXGIFormat = DXGI_FORMAT_UNKNOWN;
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

	// D3D11 decoder texture parameters
	UINT m_TextureWidth  = 0;
	UINT m_TextureHeight = 0;
	DXGI_FORMAT m_D3D11OutputFmt = DXGI_FORMAT_UNKNOWN;

	// intermediate texture format
	DXGI_FORMAT m_InternalTexFmt = DXGI_FORMAT_B8G8R8A8_UNORM;

	D3D11_VIDEO_FRAME_FORMAT m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
	int m_FieldDrawn = 0;

	DXVA2_ValueRange m_DXVA2ProcAmpRanges[4] = {};
	DXVA2_ProcAmpValues m_DXVA2ProcAmpValues = {};

	CRect m_videoRect;
	CRect m_windowRect;

	CRect m_srcRenderRect;
	CRect m_dstRenderRect;

	int m_iRotation = 0;

	HWND m_hWnd = nullptr;
	UINT m_nCurrentAdapter = -1;

	DWORD m_VendorId = 0;
	CStringW m_strAdapterDescription;

	CRenderStats m_RenderStats;
	CStringW m_strStatsStatic1;
	CStringW m_strStatsStatic2;
	CStringW m_strStatsStatic3;
	int m_iSrcFromGPU = 0;

	Tex2D_t m_TexStats;
	CD3D11Font m_Font3D;
	CD3D11Rectangle m_Rect3D;

	HMODULE m_hDXGILib = nullptr;
	HMODULE m_hD3D11Lib = nullptr;

	typedef HRESULT(WINAPI *PFNCREATEDXGIFACTORY1)(
		REFIID riid,
		void   **ppFactory);
	typedef HRESULT(WINAPI *PFND3D11CREATEDEVICE)(
		IDXGIAdapter            *pAdapter,
		D3D_DRIVER_TYPE         DriverType,
		HMODULE                 Software,
		UINT                    Flags,
		const D3D_FEATURE_LEVEL *pFeatureLevels,
		UINT                    FeatureLevels,
		UINT                    SDKVersion,
		ID3D11Device            **ppDevice,
		D3D_FEATURE_LEVEL       *pFeatureLevel,
		ID3D11DeviceContext     **ppImmediateContext);

	PFNCREATEDXGIFACTORY1 m_CreateDXGIFactory1 = nullptr;
	PFND3D11CREATEDEVICE m_D3D11CreateDevice = nullptr;

	CComPtr<IDXGIFactory1> m_pDXGIFactory1;

	CComPtr<IDirect3DSurface9>        m_pSurface9SubPic;
	CComPtr<ID3D11Texture2D>          m_pTextureSubPic;
	CComPtr<ID3D11ShaderResourceView> m_pShaderResourceSubPic;

	REFERENCE_TIME m_rtStart = 0;

public:
	CDX11VideoProcessor(CMpcVideoRenderer* pFilter);
	~CDX11VideoProcessor();

	HRESULT Init(const HWND hwnd);
	bool Initialized();

private:
	void ReleaseVP();
	void ReleaseDevice();

	HRESULT CreatePShaderFromResource(ID3D11PixelShader** ppPixelShader, UINT resid);
	void SetShaderConvertColorParams();

	void UpdateRenderRects();

public:
	HRESULT SetDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext);
	HRESULT InitSwapChain();

	BOOL VerifyMediaType(const CMediaType* pmt);
	BOOL InitMediaType(const CMediaType* pmt);

	HRESULT InitializeD3D11VP(const FmtConvParams_t& params, const UINT width, const UINT height, bool only_update_surface);
	HRESULT InitializeTexVP(const FmtConvParams_t& params, const UINT width, const UINT height);

	BOOL GetAlignmentSize(const CMediaType& mt, SIZE& Size);

	void Start();

	HRESULT ProcessSample(IMediaSample* pSample);
	HRESULT CopySample(IMediaSample* pSample);
	// Render: 1 - render first fied or progressive frame, 2 - render second fied, 0 or other - forced repeat of render.
	HRESULT Render(int field);
	HRESULT FillBlack();
	bool SecondFramePossible() { return m_bDeintDouble && m_SampleFormat != D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE; }

	void GetSourceRect(CRect& sourceRect) { sourceRect = m_srcRect; }
	void GetVideoRect(CRect& videoRect) { videoRect = m_videoRect; }
	void SetVideoRect(const CRect& videoRect);
	HRESULT SetWindowRect(const CRect& windowRect);

	HRESULT GetVideoSize(long *pWidth, long *pHeight);
	HRESULT GetAspectRatio(long *plAspectX, long *plAspectY);
	HRESULT GetCurentImage(long *pDIBImage);
	HRESULT GetVPInfo(CStringW& str);
	ColorFormat_t GetColorFormat() { return m_srcParams.cformat; }

	void SetDeintDouble(bool value) { m_bDeintDouble = value; };
	void SetShowStats(bool value)   { m_bShowStats   = value; };
	void SetTexFormat(int value);
	void SetVPEnableFmts(const VPEnableFormats_t& VPFormats);
	void SetVPScaling(bool value);
	void SetChromaScaling(int value);
	void SetUpscaling(int value);
	void SetDownscaling(int value);
	void SetInterpolateAt50pct(bool value) { m_bInterpolateAt50pct = value; }
	void SetSwapEffect(int value) { m_iSwapEffect = value; }

	void SetRotation(int value);
	int GetRotation() { return m_iRotation; }

	void Flush();

	void ClearPostScaleShaders();
	HRESULT AddPostScaleShader(const CStringW& name, const CStringA& srcCode);

private:
	void UpdateConvertTexD3D11VP();
	void UpdateCorrectionTex(const int w, const int h);
	void UpdateUpscalingShaders();
	void UpdateDownscalingShaders();
	HRESULT UpdateChromaScalingShader();

	HRESULT AlphaBlt(ID3D11ShaderResourceView* pShaderResource, ID3D11Texture2D* pRenderTarget, D3D11_VIEWPORT& viewport);
	HRESULT AlphaBltSub(ID3D11ShaderResourceView* pShaderResource, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, D3D11_VIEWPORT& viewport);
	HRESULT TextureCopyRect(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget,
							const CRect& srcRect, const CRect& destRect,
							ID3D11PixelShader* pPixelShader, ID3D11Buffer* pConstantBuffer, const int iRotation);
	HRESULT TextureConvertColor(const Tex11Video_t& texVideo, ID3D11Texture2D* pRenderTarget);
	HRESULT TextureResizeShader(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget,
							const CRect& srcRect, const CRect& destRect,
							ID3D11PixelShader* pPixelShader, const int iRotation);

	HRESULT Process(ID3D11Texture2D* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect, const bool second);

	HRESULT D3D11VPPass(ID3D11Texture2D* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect, const bool second);
	HRESULT ResizeShader2Pass(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect);

	void UpdateStatsStatic();
	HRESULT DrawStats(ID3D11Texture2D* pRenderTarget);

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
