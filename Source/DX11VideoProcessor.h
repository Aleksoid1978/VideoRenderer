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

#include <atltypes.h>
#include <ntverp.h>
#include <DXGI1_2.h>
#include <dxva2api.h>
#include <strmif.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <evr9.h> // for IMFVideoProcessor
#include <DirectXMath.h>
#include "IVideoRenderer.h"
#include "DX11Helper.h"
#include "FrameStats.h"
#include "StatsDrawing.h"

class CMpcVideoRenderer;
class CVideoRendererInputPin;

class CDX11VideoProcessor
	: public IMFVideoProcessor
{
private:
	friend class CVideoRendererInputPin;

	long m_nRefCount = 1;
	CMpcVideoRenderer* m_pFilter = nullptr;

	bool m_bDeintDouble = false;
	bool m_bShowStats   = false;
	bool m_bVPScaling   = true;
	bool m_bInterpolateAt50pct = true;
	int  m_iSwapEffect  = SWAPEFFECT_Discard;

	CComPtr<ID3D11Device> m_pDevice;
	CComPtr<ID3D11DeviceContext> m_pDeviceContext;
	ID3D11SamplerState* m_pSamplerPoint = nullptr;
	ID3D11Buffer* m_pFullFrameVertexBuffer = nullptr;
	CComPtr<ID3D11VertexShader> m_pVS_Simple;
	CComPtr<ID3D11PixelShader>  m_pPS_Simple;
	CComPtr<ID3D11InputLayout>  m_pInputLayout;

	CComPtr<ID3D11Texture2D> m_pSrcTexture2D; // Used if D3D11 VP is active
	Tex2D_t m_TexSrcCPU;     // for copy of frame from system memory (software decoding)
	Tex2D_t m_TexConvert;    // for result of color conversion
	Tex2D_t m_TexCorrection; // for result of correction after D3D11 VP
	Tex2D_t m_TexResize;     // for intermediate result of two-pass resize

	// D3D11 Video Processor
	CComPtr<ID3D11VideoContext> m_pVideoContext;
	CComPtr<ID3D11VideoDevice> m_pVideoDevice;
	CComPtr<ID3D11VideoProcessor> m_pVideoProcessor;
	CComPtr<ID3D11VideoProcessorEnumerator> m_pVideoProcessorEnum;
	CComPtr<ID3D11VideoProcessorInputView> m_pInputView;

	// D3D11 Shader Video Processor
	ID3D11SamplerState* m_pSamplerLinear = nullptr;
	ID3D11Buffer* m_pVertexBuffer = nullptr;

	CComPtr<ID3D11PixelShader> m_pPSCorrection;
	CComPtr<ID3D11PixelShader> m_pPSConvertColor;
	struct {
		bool bEnable = false;
		ID3D11Buffer* pConstants = nullptr;
	} m_PSConvColorData;
	CComPtr<ID3D11PixelShader> m_pShaderUpscaleX;
	CComPtr<ID3D11PixelShader> m_pShaderUpscaleY;
	CComPtr<ID3D11PixelShader> m_pShaderDownscaleX;
	CComPtr<ID3D11PixelShader> m_pShaderDownscaleY;
	const wchar_t* m_strShaderUpscale   = nullptr;
	const wchar_t* m_strShaderDownscale = nullptr;

	CComPtr<IDXGIFactory2> m_pDXGIFactory2;
	CComPtr<IDXGISwapChain1> m_pDXGISwapChain1;

	// Input parameters
	GUID        m_srcSubType      = GUID_NULL;
	DXGI_FORMAT m_srcDXGIFormat   = DXGI_FORMAT_UNKNOWN;
	UINT        m_srcWidth        = 0;
	UINT        m_srcHeight       = 0;
	UINT        m_srcRectWidth    = 0;
	UINT        m_srcRectHeight   = 0;
	int         m_srcPitch        = 0;
	DWORD       m_srcAspectRatioX = 0;
	DWORD       m_srcAspectRatioY = 0;
	CRect m_srcRect;
	CRect m_trgRect;
	DXVA2_ExtendedFormat m_srcExFmt = {};
	bool m_bInterlaced = false;

	// Input MediaType. Used in SetDevice() that is called from CVideoRendererInputPin::ActivateD3D11Decoding()
	CMediaType m_inputMT;

	// D3D11 decoder texture parameters
	UINT m_TextureWidth  = 0;
	UINT m_TextureHeight = 0;

	// intermediate texture format
	DXGI_FORMAT m_InternalTexFmt = DXGI_FORMAT_B8G8R8X8_UNORM;

	typedef void(*CopyFrameDataFn)(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, int src_pitch);
	CopyFrameDataFn m_pConvertFn = nullptr;

	D3D11_VIDEO_FRAME_FORMAT m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
	int m_FieldDrawn = 0;

	D3D11_VIDEO_PROCESSOR_CAPS m_VPCaps = {};
	D3D11_VIDEO_PROCESSOR_FILTER_RANGE m_VPFilterRange[4] = {};
	struct {
		BOOL Enabled;
		int Level;
	} m_VPFilterSettings[4] = {};

	CRect m_videoRect;
	CRect m_windowRect;

	CRect m_srcRenderRect;
	CRect m_dstRenderRect;

	HWND m_hWnd = nullptr;
	UINT m_nCurrentAdapter = -1;

	DWORD m_VendorId = 0;
	CString m_strAdapterDescription;

	Tex2D_t m_TexOSD;

	CRenderStats m_RenderStats;

	CStatsDrawing m_StatsDrawing;
	CStringW m_strStatsStatic1;
	CStringW m_strStatsStatic2;
	bool m_bSrcFromGPU = false;

	bool resetmt = false;

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

public:
	CDX11VideoProcessor(CMpcVideoRenderer* pFilter);
	~CDX11VideoProcessor();

	HRESULT Init(const HWND hwnd, const int iSurfaceFmt);

private:
	void ReleaseVP();
	void ReleaseDevice();

	HRESULT GetDataFromResource(LPVOID& data, DWORD& size, UINT resid);
	HRESULT CreatePShaderFromResource(ID3D11PixelShader** ppPixelShader, UINT resid);

public:
	HRESULT SetDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext);
	HRESULT InitSwapChain();

	BOOL VerifyMediaType(const CMediaType* pmt);
	BOOL InitMediaType(const CMediaType* pmt);

	HRESULT InitializeD3D11VP(const DXGI_FORMAT dxgiFormat, const UINT width, const UINT height, bool only_update_surface);
	HRESULT InitializeTexVP(const DXGI_FORMAT dxgiFormat, const UINT width, const UINT height);

	BOOL GetAlignmentSize(const CMediaType& mt, SIZE& Size);

	void Start();
	void Stop();

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

	void SetDeintDouble(bool value) { m_bDeintDouble = value; };
	void SetShowStats(bool value)   { m_bShowStats   = value; };
	void SetVPScaling(bool value);
	void SetUpscaling(int value);
	void SetDownscaling(int value);
	void SetInterpolateAt50pct(bool value) { m_bInterpolateAt50pct = value; }
	void SetSwapEffect(int value) { m_iSwapEffect = value; }

private:
	void UpdateCorrectionTex(const int w, const int h);

	HRESULT ProcessD3D11(ID3D11Texture2D* pRenderTarget, const RECT* pSrcRect, const RECT* pDstRect, const bool second);
	HRESULT ProcessTex(ID3D11Texture2D* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect);

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
