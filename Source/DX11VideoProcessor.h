/*
* (C) 2018-2024 see Authors.txt
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

#include <DXGI1_2.h>
#include <dxva2api.h>
#include <dxgi1_5.h>
#include <strmif.h>
#include <map>
#include "IVideoRenderer.h"
#include "DX11Helper.h"
#include "D3D11VP.h"
#include "D3DUtil/D3D11Font.h"
#include "D3DUtil/D3D11Geometry.h"
#include "VideoProcessor.h"

#define TEST_SHADER 0

class CVideoRendererInputPin;

class CDX11VideoProcessor
	: public CVideoProcessor
{
private:
	friend class CVideoRendererInputPin;

	// Direct3D 11
	CComPtr<ID3D11Device1>        m_pDevice;
	CComPtr<ID3D11DeviceContext1> m_pDeviceContext;
	CComPtr<ID3D11SamplerState>   m_pSamplerPoint;
	CComPtr<ID3D11SamplerState>   m_pSamplerLinear;
	CComPtr<ID3D11SamplerState>   m_pSamplerDither;
	CComPtr<ID3D11BlendState>     m_pAlphaBlendState;
	CComPtr<ID3D11VertexShader>   m_pVS_Simple;
	CComPtr<ID3D11PixelShader>    m_pPS_Simple;
	CComPtr<ID3D11PixelShader>    m_pPS_BitmapToFrame;
	CComPtr<ID3D11InputLayout>    m_pVSimpleInputLayout;
	CComPtr<ID3D11Buffer>         m_pVertexBuffer;
	CComPtr<ID3D11Buffer>         m_pResizeShaderConstantBuffer;
	CComPtr<ID3D11Buffer>         m_pHalfOUtoInterlaceConstantBuffer;
	CComPtr<ID3D11Buffer>         m_pFinalPassConstantBuffer;

	DXGI_SWAP_EFFECT              m_UsedSwapEffect = DXGI_SWAP_EFFECT_DISCARD;

#if TEST_SHADER
	CComPtr<ID3D11PixelShader>    m_pPS_TEST;
#endif

	Tex11Video_t m_TexSrcVideo; // for copy of frame
	Tex2D_t m_TexConvertOutput;
	Tex2D_t m_TexResize;        // for intermediate result of two-pass resize
	CTex2DRing m_TexsPostScale;
	Tex2D_t m_TexDither;

	// for GetAlignmentSize()
	struct Alignment_t {
		Tex11Video_t texture;
		ColorFormat_t cformat = {};
		LONG cx = {};
	} m_Alignment;

	// D3D11 Video Processor
	CD3D11VP m_D3D11VP;
	CComPtr<ID3D11PixelShader> m_pPSCorrection;
	const wchar_t* m_strCorrection = nullptr;

	// D3D11 Shader Video Processor
	CComPtr<ID3D11PixelShader> m_pPSConvertColor;
	CComPtr<ID3D11PixelShader> m_pPSConvertColorDeint;
	struct {
		bool bEnable = false;
		ID3D11Buffer* pVertexBuffer = nullptr;
		ID3D11Buffer* pConstants = nullptr;
		void Release() {
			bEnable = false;
			SAFE_RELEASE(pVertexBuffer);
			SAFE_RELEASE(pConstants);
		}
	} m_PSConvColorData;

	CComPtr<ID3D11Buffer> m_pDoviCurvesConstantBuffer;

	CComPtr<ID3D11PixelShader> m_pShaderUpscaleX;
	CComPtr<ID3D11PixelShader> m_pShaderUpscaleY;
	CComPtr<ID3D11PixelShader> m_pShaderDownscaleX;
	CComPtr<ID3D11PixelShader> m_pShaderDownscaleY;

	std::vector<ExternalPixelShader11_t> m_pPreScaleShaders;
	std::vector<ExternalPixelShader11_t> m_pPostScaleShaders;
	CComPtr<ID3D11Buffer> m_pPostScaleConstants;
	CComPtr<ID3D11PixelShader> m_pPSHalfOUtoInterlace;
	CComPtr<ID3D11PixelShader> m_pPSFinalPass;

	CComPtr<IDXGIFactory2>   m_pDXGIFactory2;
	CComPtr<IDXGISwapChain1> m_pDXGISwapChain1;
	CComPtr<IDXGISwapChain4> m_pDXGISwapChain4;
	CComPtr<IDXGIOutput>    m_pDXGIOutput;
	DXGI_COLOR_SPACE_TYPE m_currentSwapChainColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

	// Input parameters
	DXGI_FORMAT m_srcDXGIFormat = DXGI_FORMAT_UNKNOWN;

	// D3D11 VP texture format
	DXGI_FORMAT m_D3D11OutputFmt = DXGI_FORMAT_UNKNOWN;

	// intermediate texture format
	DXGI_FORMAT m_InternalTexFmt = DXGI_FORMAT_B8G8R8A8_UNORM;

	// swap chain format
	DXGI_FORMAT m_SwapChainFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
	UINT32 m_DisplayBitsPerChannel = 8;

	D3D11_VIDEO_FRAME_FORMAT m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;

	CComPtr<IDXGIFactory1> m_pDXGIFactory1;

	bool m_bSubPicWasRendered = false;

	// AlphaBitmap
	Tex2D_t m_TexAlphaBitmap;
	CComPtr<ID3D11Buffer> m_pAlphaBitmapVertex;

	// Statistics
	CD3D11Rectangle m_StatsBackground;
	CD3D11Font      m_Font3D;
	CD3D11Rectangle m_Rect3D;
	CD3D11Rectangle m_Underlay;
	CD3D11Lines     m_Lines;
	CD3D11Polyline  m_SyncLine;

	bool m_bDecoderDevice = false;
	bool m_bIsFullscreen = false;

	int m_iVPSuperRes = 0;
	bool m_bVPUseSuperRes = false; // but it is not exactly
	bool m_bVPRTXVideoHDR = false;
	bool m_bVPUseRTXVideoHDR = false;
	bool m_bVPSuperResIfScaling = false;

	bool m_bHdrPassthroughSupport = false;
	bool m_bHdrDisplaySwitching   = false; // switching HDR display in progress
	bool m_bHdrDisplayModeEnabled = false;
	bool m_bHdrAllowSwitchDisplay = true;
	UINT m_srcVideoTransferFunction = 0; // need a description or rename

	std::map<std::wstring, BOOL> m_hdrModeSavedState;
	std::map<std::wstring, BOOL> m_hdrModeStartState;

	struct HDRMetadata {
		DXGI_HDR_METADATA_HDR10 hdr10 = {};
		bool bValid = false;
	};
	HDRMetadata m_hdr10 = {};
	HDRMetadata m_lastHdr10 = {};

	UINT m_DoviMaxMasteringLuminance = 0;
	UINT m_DoviMinMasteringLuminance = 0;

	HMONITOR m_lastFullscreenHMonitor = nullptr;

	D3DCOLOR m_dwStatsTextColor = D3DCOLOR_XRGB(255, 255, 255);

	bool m_bCallbackDeviceIsSet = false;
	void SetCallbackDevice();

public:
	CDX11VideoProcessor(CMpcVideoRenderer* pFilter, const Settings_t& config, HRESULT& hr);
	~CDX11VideoProcessor() override;

	int Type() override { return VP_DX11; }

	HRESULT Init(const HWND hwnd, bool* pChangeDevice = nullptr) override;
	bool Initialized();

private:
	void ReleaseVP();
	void ReleaseDevice();
	void ReleaseSwapChain();

	UINT GetPostScaleSteps();

	HRESULT CreatePShaderFromResource(ID3D11PixelShader** ppPixelShader, UINT resid);
	void SetShaderConvertColorParams();

	HRESULT SetShaderDoviCurvesPoly();
	HRESULT SetShaderDoviCurves();

	void UpdateTexParams(int cdepth);
	void UpdateRenderRect();
	void UpdateScalingStrings();

	void SetGraphSize() override;

	HRESULT MemCopyToTexSrcVideo(const BYTE* srcData, const int srcPitch);

	bool Preferred10BitOutput() {
		return m_DisplayBitsPerChannel >= 10 && (m_InternalTexFmt == DXGI_FORMAT_R10G10B10A2_UNORM || m_InternalTexFmt == DXGI_FORMAT_R16G16B16A16_FLOAT);
	}

	bool HandleHDRToggle();
	bool SuperResValid();
	bool RTXVideoHDRValid();

public:
	HRESULT SetDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext, const bool bDecoderDevice);
	HRESULT InitSwapChain();

	BOOL VerifyMediaType(const CMediaType* pmt) override;
	BOOL InitMediaType(const CMediaType* pmt) override;

	HRESULT InitializeD3D11VP(const FmtConvParams_t& params, const UINT width, const UINT height);
	HRESULT InitializeTexVP(const FmtConvParams_t& params, const UINT width, const UINT height);
	void UpdatFrameProperties(); // use this after receiving modified frame from hardware decoder

	BOOL GetAlignmentSize(const CMediaType& mt, SIZE& Size) override;

	HRESULT ProcessSample(IMediaSample* pSample) override;
	HRESULT CopySample(IMediaSample* pSample);
	// Render: 1 - render first fied or progressive frame, 2 - render second fied, 0 or other - forced repeat of render.
	HRESULT Render(int field) override;
	HRESULT FillBlack() override;

	void SetVideoRect(const CRect& videoRect)      override;
	HRESULT SetWindowRect(const CRect& windowRect) override;
	HRESULT Reset() override;
	bool IsInit() const override { return m_bHdrDisplaySwitching; }

	HRESULT GetCurentImage(long *pDIBImage) override;
	HRESULT GetDisplayedImage(BYTE **ppDib, unsigned* pSize) override;
	HRESULT GetVPInfo(std::wstring& str) override;

	// Settings
	void Configure(const Settings_t& config) override;

	void SetRotation(int value) override;
	void SetStereo3dTransform(int value) override;

	void Flush() override;

	void ClearPreScaleShaders() override;
	void ClearPostScaleShaders() override;

	HRESULT AddPreScaleShader(const std::wstring& name, const std::string& srcCode) override;
	HRESULT AddPostScaleShader(const std::wstring& name, const std::string& srcCode) override;

private:
	void UpdateTexures();
	void UpdatePostScaleTexures();
	void UpdateUpscalingShaders();
	void UpdateDownscalingShaders();
	HRESULT UpdateConvertColorShader();
	void UpdateBitmapShader();

	HRESULT D3D11VPPass(ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect, const bool second);
	HRESULT ConvertColorPass(ID3D11Texture2D* pRenderTarget);
	HRESULT ResizeShaderPass(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect, const int rotation);
	HRESULT FinalPass(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect);

	void DrawSubtitles(ID3D11Texture2D* pRenderTarget);
	HRESULT Process(ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect, const bool second);

	HRESULT AlphaBlt(ID3D11ShaderResourceView* pShaderResource, ID3D11Texture2D* pRenderTarget,
					 ID3D11Buffer* pVertexBuffer, D3D11_VIEWPORT* pViewPort,
					 ID3D11SamplerState* pSampler);
	HRESULT TextureCopyRect(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget,
							const CRect& srcRect, const CRect& destRect,
							ID3D11PixelShader* pPixelShader, ID3D11Buffer* pConstantBuffer,
							const int iRotation, const bool bFlip);

	HRESULT TextureResizeShader(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget,
								const CRect& srcRect, const CRect& destRect,
								ID3D11PixelShader* pPixelShader,
								const int iRotation, const bool bFlip);

	void UpdateStatsPresent();
	void UpdateStatsStatic();
	//void UpdateStatsPostProc();
	HRESULT DrawStats(ID3D11Texture2D* pRenderTarget);

public:
	// IMFVideoProcessor
	STDMETHODIMP SetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *pValues) override;

	// IMFVideoMixerBitmap
	STDMETHODIMP SetAlphaBitmap(const MFVideoAlphaBitmap *pBmpParms) override;
	STDMETHODIMP UpdateAlphaBitmapParameters(const MFVideoAlphaBitmapParams *pBmpParms) override;
};
