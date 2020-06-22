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

#include "IVideoRenderer.h"
#include "Helper.h"
#include "DX9Helper.h"
#include "DXVA2VP.h"
#include "D3DUtil/D3D9Font.h"
#include "D3DUtil/D3D9Geometry.h"
#include "VideoProcessor.h"


class CDX9VideoProcessor
	: public CVideoProcessor
{
private:
	// Direct3D 9
	CComPtr<IDirect3D9Ex>            m_pD3DEx;
	CComPtr<IDirect3DDevice9Ex>      m_pD3DDevEx;
	CComPtr<IDirect3DDeviceManager9> m_pD3DDeviceManager;
	UINT m_nResetTocken = 0;

	D3DDISPLAYMODEEX m_DisplayMode = { sizeof(D3DDISPLAYMODEEX) };
	D3DPRESENT_PARAMETERS m_d3dpp = {};

	// DXVA2 Video Processor
	CDXVA2VP m_DXVA2VP;

	// Input parameters
	D3DFORMAT m_srcDXVA2Format = D3DFMT_UNKNOWN;

	// DXVA2 surface format
	D3DFORMAT m_DXVA2OutputFmt = D3DFMT_UNKNOWN;

	// intermediate texture format
	D3DFORMAT m_InternalTexFmt = D3DFMT_X8R8G8B8;

	// Processing parameters
	DXVA2_SampleFormat m_CurrentSampleFmt = DXVA2_SampleProgressiveFrame;

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

	std::vector<ExternalPixelShader9_t> m_pPostScaleShaders;
	CComPtr<IDirect3DPixelShader9> m_pPSFinalPass;

	// AlphaBitmap
	Tex_t m_TexAlphaBitmap;

	// Statistics
	CD3D9Rectangle m_StatsBackground;
	CD3D9Font      m_Font3D;
	CD3D9Rectangle m_Rect3D;
	CD3D9Rectangle m_Underlay;
	CD3D9Lines     m_Lines;
	CD3D9Polyline  m_SyncLine;

public:
	CDX9VideoProcessor(CMpcVideoRenderer* pFilter);
	~CDX9VideoProcessor();

	HRESULT Init(const HWND hwnd, bool* pChangeDevice = nullptr);
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

	void SetGraphSize();

public:
	BOOL VerifyMediaType(const CMediaType* pmt);
	BOOL InitMediaType(const CMediaType* pmt);

	BOOL GetAlignmentSize(const CMediaType& mt, SIZE& Size);

	HRESULT ProcessSample(IMediaSample* pSample);
	HRESULT CopySample(IMediaSample* pSample);
	// Render: 1 - render first fied or progressive frame, 2 - render second fied, 0 or other - forced repeat of render.
	HRESULT Render(int field);
	HRESULT FillBlack();
	bool SecondFramePossible() { return m_bDeintDouble && m_CurrentSampleFmt >= DXVA2_SampleFieldInterleavedEvenFirst && m_CurrentSampleFmt <= DXVA2_SampleFieldSingleOdd; }

	void SetVideoRect(const CRect& videoRect);
	HRESULT SetWindowRect(const CRect& windowRect);
	HRESULT Reset();

	IDirect3DDeviceManager9* GetDeviceManager9() { return m_pD3DDeviceManager; }
	HRESULT GetCurentImage(long *pDIBImage);
	HRESULT GetDisplayedImage(BYTE **ppDib, unsigned *pSize);
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

	HRESULT DxvaVPPass(IDirect3DSurface9* pRenderTarget, const CRect& srcRect, const CRect& dstRect, const bool second);
	HRESULT ConvertColorPass(IDirect3DSurface9* pRenderTarget);
	HRESULT ResizeShaderPass(IDirect3DTexture9* pTexture, IDirect3DSurface9* pRenderTarget, const CRect& srcRect, const CRect& dstRect);
	HRESULT FinalPass(IDirect3DTexture9* pTexture, IDirect3DSurface9* pRenderTarget, const CRect& srcRect, const CRect& dstRect);

	HRESULT Process(IDirect3DSurface9* pRenderTarget, const CRect& srcRect, const CRect& dstRect, const bool second);

	HRESULT TextureCopy(IDirect3DTexture9* pTexture);
	HRESULT TextureCopyRect(IDirect3DTexture9* pTexture, const CRect& srcRect, const CRect& dstRect,
		D3DTEXTUREFILTERTYPE filter, const int iRotation, const bool bFlip);
	HRESULT TextureResizeShader(IDirect3DTexture9* pTexture, const CRect& srcRect, const CRect& dstRect,
		IDirect3DPixelShader9* pShader, const int iRotation, const bool bFlip);

	void UpdateStatsStatic();
	void UpdateStatsPostProc();
	HRESULT DrawStats(IDirect3DSurface9* pRenderTarget);

public:
	// IMFVideoProcessor
	STDMETHODIMP SetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *pValues) override;

	// IMFVideoMixerBitmap
	STDMETHODIMP SetAlphaBitmap(const MFVideoAlphaBitmap *pBmpParms) override;
	STDMETHODIMP UpdateAlphaBitmapParameters(const MFVideoAlphaBitmapParams *pBmpParms) override;
};
