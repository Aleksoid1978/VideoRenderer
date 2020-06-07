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

#include <ntverp.h>
#include <DXGI1_2.h>
#include <dxva2api.h>
#include <strmif.h>
#include "IVideoRenderer.h"
#include "DX11Helper.h"
#include "D3D11VP.h"
#include "D3DUtil/D3D11Font.h"
#include "D3DUtil/D3D11Geometry.h"
#include "DX9Device.h"
#include "VideoProcessor.h"

class CVideoRendererInputPin;

class CDX11VideoProcessor
	: public CVideoProcessor
	, public CDX9Device
{
private:
	friend class CVideoRendererInputPin;

	// Direct3D 11
	CComPtr<ID3D11Device>        m_pDevice;
	CComPtr<ID3D11DeviceContext> m_pDeviceContext;
	ID3D11SamplerState*          m_pSamplerPoint = nullptr;
	ID3D11SamplerState*          m_pSamplerLinear = nullptr;
	ID3D11SamplerState*          m_pSamplerDither = nullptr;
	CComPtr<ID3D11BlendState>    m_pAlphaBlendState;
	CComPtr<ID3D11BlendState>    m_pAlphaBlendStateInv;
	ID3D11Buffer*                m_pFullFrameVertexBuffer = nullptr;
	CComPtr<ID3D11VertexShader>  m_pVS_Simple;
	CComPtr<ID3D11PixelShader>   m_pPS_Simple;
	CComPtr<ID3D11InputLayout>   m_pVSimpleInputLayout;

	Tex11Video_t m_TexSrcVideo; // for copy of frame
	Tex2D_t m_TexD3D11VPOutput;
	Tex2D_t m_TexConvertOutput;
	Tex2D_t m_TexResize;        // for intermediate result of two-pass resize
	CTex2DRing m_TexsPostScale;
	Tex2D_t m_TexDither;

	// D3D11 Video Processor
	CD3D11VP m_D3D11VP;
	CComPtr<ID3D11PixelShader> m_pPSCorrection;
	const wchar_t* m_strCorrection = nullptr;

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

	std::vector<ExternalPixelShader11_t> m_pPostScaleShaders;
	ID3D11Buffer* m_pPostScaleConstants = nullptr;
	CComPtr<ID3D11PixelShader> m_pPSFinalPass;

	CComPtr<IDXGIFactory2> m_pDXGIFactory2;
	CComPtr<IDXGISwapChain1> m_pDXGISwapChain1;

	// Input parameters
	DXGI_FORMAT m_srcDXGIFormat = DXGI_FORMAT_UNKNOWN;

	// D3D11 VP texture format
	DXGI_FORMAT m_D3D11OutputFmt = DXGI_FORMAT_UNKNOWN;

	// intermediate texture format
	DXGI_FORMAT m_InternalTexFmt = DXGI_FORMAT_B8G8R8A8_UNORM;

	D3D11_VIDEO_FRAME_FORMAT m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;

	HMODULE m_hDXGILib = nullptr;
	HMODULE m_hD3D11Lib = nullptr;

	typedef HRESULT(WINAPI *PFNCREATEDXGIFACTORY1)(
		REFIID riid,
		void   **ppFactory);

	PFNCREATEDXGIFACTORY1 m_fnCreateDXGIFactory1 = nullptr;
	PFN_D3D11_CREATE_DEVICE m_fnD3D11CreateDevice = nullptr;

	CComPtr<IDXGIFactory1> m_pDXGIFactory1;

	CComPtr<IDirect3DSurface9>        m_pSurface9SubPic;
	CComPtr<ID3D11Texture2D>          m_pTextureSubPic;
	CComPtr<ID3D11ShaderResourceView> m_pShaderResourceSubPic;

	// AlphaBitmap
	Tex2D_t m_TexAlphaBitmap;
	CComPtr<ID3D11Buffer> m_pAlphaBitmapVertex;

	// Statistics
	Tex2D_t m_TexStats;
	CD3D11Font m_Font3D;
	CD3D11Rectangle m_Rect3D;
	CD3D11Rectangle m_Underlay;
	CD3D11Lines     m_Lines;
	CD3D11Polyline  m_SyncLine;

	bool m_bUseNativeExternalDecoder = false;

public:
	CDX11VideoProcessor(CMpcVideoRenderer* pFilter);
	~CDX11VideoProcessor();

	HRESULT Init(const HWND hwnd, bool* pChangeDevice = nullptr);
	bool Initialized();

private:
	void ReleaseVP();
	void ReleaseDevice();

	HRESULT CreatePShaderFromResource(ID3D11PixelShader** ppPixelShader, UINT resid);
	void SetShaderConvertColorParams();

	void UpdateRenderRect();

	void SetGraphSize();

	HRESULT MemCopyToTexSrcVideo(const BYTE* srcData, const int srcPitch);

public:
	HRESULT SetDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext, const bool bFromDecoder = false);
	HRESULT InitSwapChain();
	void ReleaseSwapChain() { m_pDXGISwapChain1.Release(); }

	BOOL VerifyMediaType(const CMediaType* pmt);
	BOOL InitMediaType(const CMediaType* pmt);

	HRESULT InitializeD3D11VP(const FmtConvParams_t& params, const UINT width, const UINT height);
	HRESULT InitializeTexVP(const FmtConvParams_t& params, const UINT width, const UINT height);
	void UpdatFrameProperties(); // use this after receiving modified frame from hardware decoder

	BOOL GetAlignmentSize(const CMediaType& mt, SIZE& Size);

	HRESULT ProcessSample(IMediaSample* pSample);
	HRESULT CopySample(IMediaSample* pSample);
	// Render: 1 - render first fied or progressive frame, 2 - render second fied, 0 or other - forced repeat of render.
	HRESULT Render(int field);
	HRESULT FillBlack();
	bool SecondFramePossible() { return m_bDeintDouble && m_SampleFormat != D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE; }

	void SetVideoRect(const CRect& videoRect);
	HRESULT SetWindowRect(const CRect& windowRect);

	HRESULT GetCurentImage(long *pDIBImage);
	HRESULT GetDisplayedImage(BYTE **ppDib, unsigned* pSize);
	HRESULT GetVPInfo(std::wstring& str);

	// Settings
	void SetVPScaling(bool value);
	void SetChromaScaling(int value);
	void SetUpscaling(int value);
	void SetDownscaling(int value);
	void SetDither(bool value);
	void SetSwapEffect(int value) { m_iSwapEffect = value; }

	void SetRotation(int value);
	void SetFlip(bool value) { m_bFlip = value; }

	void Flush();

	void ClearPostScaleShaders();
	HRESULT AddPostScaleShader(const std::wstring& name, const std::string& srcCode);

private:
	void UpdateTexures(SIZE texsize);
	void UpdatePostScaleTexures(SIZE texsize);
	void UpdateUpscalingShaders();
	void UpdateDownscalingShaders();
	HRESULT UpdateChromaScalingShader();

	HRESULT D3D11VPPass(ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect, const bool second);
	HRESULT ConvertColorPass(ID3D11Texture2D* pRenderTarget);
	HRESULT ResizeShaderPass(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect);
	HRESULT FinalPass(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect);

	HRESULT Process(ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect, const bool second);

	HRESULT AlphaBlt(ID3D11ShaderResourceView* pShaderResource, ID3D11Texture2D* pRenderTarget,
					ID3D11Buffer* pVertexBuffer, D3D11_VIEWPORT* pViewPort, ID3D11SamplerState* pSampler);
	HRESULT AlphaBltSub(ID3D11ShaderResourceView* pShaderResource, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, D3D11_VIEWPORT& viewport);
	HRESULT TextureCopyRect(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget,
							const CRect& srcRect, const CRect& destRect,
							ID3D11PixelShader* pPixelShader, ID3D11Buffer* pConstantBuffer,
							const int iRotation, const bool bFlip);

	HRESULT TextureResizeShader(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget,
							const CRect& srcRect, const CRect& destRect,
							ID3D11PixelShader* pPixelShader,
							const int iRotation, const bool bFlip);

	void UpdateStatsStatic();
	void UpdateStatsStatic3();
	HRESULT DrawStats(ID3D11Texture2D* pRenderTarget);

public:
	// IMFVideoProcessor
	STDMETHODIMP SetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *pValues) override;

	// IMFVideoMixerBitmap
	STDMETHODIMP SetAlphaBitmap(const MFVideoAlphaBitmap *pBmpParms) override;
	STDMETHODIMP UpdateAlphaBitmapParameters(const MFVideoAlphaBitmapParams *pBmpParms) override;
};
