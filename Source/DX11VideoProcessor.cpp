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

#include "stdafx.h"
#include <uuids.h>
#include <d3d9.h>
#include <mfapi.h> // for MR_BUFFER_SERVICE
#include <Mferror.h>
#include <Mfidl.h>
#include <dwmapi.h>
#include "Helper.h"
#include "Time.h"
#include "resource.h"
#include "VideoRenderer.h"
#include "../Include/Version.h"
#include "DX11VideoProcessor.h"
#include "../Include/ID3DVideoMemoryConfiguration.h"
#include "DisplayConfig.h"
#include "Shaders.h"

static const ScalingShaderResId s_Upscaling11ResIDs[UPSCALE_COUNT] = {
	{0,                            0,                            L"Nearest-neighbor"  },
	{IDF_PSH11_INTERP_MITCHELL4_X, IDF_PSH11_INTERP_MITCHELL4_Y, L"Mitchell-Netravali"},
	{IDF_PSH11_INTERP_CATMULL4_X,  IDF_PSH11_INTERP_CATMULL4_Y , L"Catmull-Rom"       },
	{IDF_PSH11_INTERP_LANCZOS2_X,  IDF_PSH11_INTERP_LANCZOS2_Y , L"Lanczos2"          },
	{IDF_PSH11_INTERP_LANCZOS3_X,  IDF_PSH11_INTERP_LANCZOS3_Y , L"Lanczos3"          },
};

static const ScalingShaderResId s_Downscaling11ResIDs[DOWNSCALE_COUNT] = {
	{IDF_PSH11_CONVOL_BOX_X,       IDF_PSH11_CONVOL_BOX_Y,       L"Box"          },
	{IDF_PSH11_CONVOL_BILINEAR_X,  IDF_PSH11_CONVOL_BILINEAR_Y,  L"Bilinear"     },
	{IDF_PSH11_CONVOL_HAMMING_X,   IDF_PSH11_CONVOL_HAMMING_Y,   L"Hamming"      },
	{IDF_PSH11_CONVOL_BICUBIC05_X, IDF_PSH11_CONVOL_BICUBIC05_Y, L"Bicubic"      },
	{IDF_PSH11_CONVOL_BICUBIC15_X, IDF_PSH11_CONVOL_BICUBIC15_Y, L"Bicubic sharp"},
	{IDF_PSH11_CONVOL_LANCZOS_X,   IDF_PSH11_CONVOL_LANCZOS_Y,   L"Lanczos"      }
};

const UINT dither_size = 32;

struct VERTEX {
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT2 TexCoord;
};

struct PS_COLOR_TRANSFORM {
	DirectX::XMFLOAT4 cm_r;
	DirectX::XMFLOAT4 cm_g;
	DirectX::XMFLOAT4 cm_b;
	DirectX::XMFLOAT4 cm_c;
};

struct PS_EXTSHADER_CONSTANTS {
	DirectX::XMFLOAT2 pxy; // pixel size in normalized coordinates
	DirectX::XMFLOAT2 wh;  // width and height of texture
	uint32_t counter;      // rendered frame counter
	float clock;           // some time in seconds
	float reserved1;
	float reserved2;
};

static_assert(sizeof(PS_EXTSHADER_CONSTANTS) % 16 == 0);

HRESULT CreateVertexBuffer(ID3D11Device* pDevice, ID3D11Buffer** ppVertexBuffer, const UINT srcW, const UINT srcH, const RECT& srcRect, const int iRotation)
{
	ASSERT(ppVertexBuffer);
	ASSERT(*ppVertexBuffer == nullptr);

	const float src_dx = 1.0f / srcW;
	const float src_dy = 1.0f / srcH;
	const float src_l = src_dx * srcRect.left;
	const float src_r = src_dx * srcRect.right;
	const float src_t = src_dy * srcRect.top;
	const float src_b = src_dy * srcRect.bottom;

	POINT points[4];
	switch (iRotation) {
	case 90:
		points[0] = { -1, +1 };
		points[1] = { +1, +1 };
		points[2] = { -1, -1 };
		points[3] = { +1, -1 };
		break;
	case 180:
		points[0] = { +1, +1 };
		points[1] = { +1, -1 };
		points[2] = { -1, +1 };
		points[3] = { -1, -1 };
		break;
	case 270:
		points[0] = { +1, -1 };
		points[1] = { -1, -1 };
		points[2] = { +1, +1 };
		points[3] = { -1, +1 };
		break;
	default:
		points[0] = { -1, -1 };
		points[1] = { -1, +1 };
		points[2] = { +1, -1 };
		points[3] = { +1, +1 };
		break;
	}

	VERTEX Vertices[4] = {
		// Vertices for drawing whole texture
		// 2 ___4
		//  |\ |
		// 1|_\|3
		{ {(float)points[0].x, (float)points[0].y, 0}, {src_l, src_b} },
		{ {(float)points[1].x, (float)points[1].y, 0}, {src_l, src_t} },
		{ {(float)points[2].x, (float)points[2].y, 0}, {src_r, src_b} },
		{ {(float)points[3].x, (float)points[3].y, 0}, {src_r, src_t} },
	};

	D3D11_BUFFER_DESC BufferDesc = { sizeof(Vertices), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
	D3D11_SUBRESOURCE_DATA InitData = { Vertices, 0, 0 };

	HRESULT hr = pDevice->CreateBuffer(&BufferDesc, &InitData, ppVertexBuffer);
	DLogIf(FAILED(hr), L"CreateVertexBuffer() : CreateBuffer() failed with error %s", HR2Str(hr));

	return hr;
}

void TextureBlt11(
	ID3D11DeviceContext* pDeviceContext,
	ID3D11RenderTargetView* pRenderTargetView, D3D11_VIEWPORT& viewport,
	ID3D11InputLayout* pInputLayout,
	ID3D11VertexShader* pVertexShader,
	ID3D11PixelShader* pPixelShader,
	ID3D11ShaderResourceView* pShaderResourceViews,
	ID3D11SamplerState* pSampler,
	ID3D11Buffer* pConstantBuffer,
	ID3D11Buffer* pVertexBuffer)
{
	ASSERT(pDeviceContext);
	ASSERT(pRenderTargetView);
	ASSERT(pShaderResourceViews);

	const UINT Stride = sizeof(VERTEX);
	const UINT Offset = 0;

	// Set resources
	pDeviceContext->IASetInputLayout(pInputLayout);
	pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
	pDeviceContext->RSSetViewports(1, &viewport);
	pDeviceContext->OMSetBlendState(nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
	pDeviceContext->VSSetShader(pVertexShader, nullptr, 0);
	pDeviceContext->PSSetShader(pPixelShader, nullptr, 0);
	pDeviceContext->PSSetShaderResources(0, 1, &pShaderResourceViews);
	pDeviceContext->PSSetSamplers(0, 1, &pSampler);
	pDeviceContext->PSSetConstantBuffers(0, 1, &pConstantBuffer);
	pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	pDeviceContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &Stride, &Offset);

	// Draw textured quad onto render target
	pDeviceContext->Draw(4, 0);

	ID3D11ShaderResourceView* views[1] = {};
	pDeviceContext->PSSetShaderResources(0, 1, views);
}

HRESULT CDX11VideoProcessor::AlphaBlt(ID3D11ShaderResourceView* pShaderResource, ID3D11Texture2D* pRenderTarget, D3D11_VIEWPORT& viewport)
{
	ID3D11RenderTargetView* pRenderTargetView;
	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);

	if (S_OK == hr) {
		UINT Stride = sizeof(VERTEX);
		UINT Offset = 0;

		// Set resources
		m_pDeviceContext->IASetInputLayout(m_pVSimpleInputLayout);
		m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
		m_pDeviceContext->RSSetViewports(1, &viewport);
		m_pDeviceContext->OMSetBlendState(m_pAlphaBlendState, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
		m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
		m_pDeviceContext->PSSetShader(m_pPS_Simple, nullptr, 0);
		m_pDeviceContext->PSSetShaderResources(0, 1, &pShaderResource);
		m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerPoint);
		m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pFullFrameVertexBuffer, &Stride, &Offset);

		// Draw textured quad onto render target
		m_pDeviceContext->Draw(4, 0);

		pRenderTargetView->Release();
	}
	DLogIf(FAILED(hr), L"AlphaBlt() : CreateRenderTargetView() failed with error %s", HR2Str(hr));

	return hr;
}

HRESULT CDX11VideoProcessor::AlphaBltSub(ID3D11ShaderResourceView* pShaderResource, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, D3D11_VIEWPORT& viewport)
{
	ID3D11RenderTargetView* pRenderTargetView;
	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);

	if (S_OK == hr) {
		UINT Stride = sizeof(VERTEX);
		UINT Offset = 0;
		ID3D11Buffer* pVertexBuffer = nullptr;
		hr = CreateVertexBuffer(m_pDevice, &pVertexBuffer, m_d3dpp.BackBufferWidth, m_d3dpp.BackBufferHeight, srcRect, 0);

		if (S_OK == hr) {
			// Set resources
			m_pDeviceContext->IASetInputLayout(m_pVSimpleInputLayout);
			m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
			m_pDeviceContext->RSSetViewports(1, &viewport);
			m_pDeviceContext->OMSetBlendState(m_pFilter->m_bSubInvAlpha ? m_pAlphaBlendStateInv : m_pAlphaBlendState, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
			m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
			m_pDeviceContext->PSSetShader(m_pPS_Simple, nullptr, 0);
			m_pDeviceContext->PSSetShaderResources(0, 1, &pShaderResource);
			m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerPoint);
			m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			m_pDeviceContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &Stride, &Offset);

			// Draw textured quad onto render target
			m_pDeviceContext->Draw(4, 0);

			pVertexBuffer->Release();
		}
		pRenderTargetView->Release();
	}
	DLogIf(FAILED(hr), L"CDX11VideoProcessor:AlphaBlt() : CreateRenderTargetView() failed with error %s", HR2Str(hr));

	return hr;
}

HRESULT CDX11VideoProcessor::TextureCopyRect(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& destRect, ID3D11PixelShader* pPixelShader, ID3D11Buffer* pConstantBuffer, const int iRotation)
{
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;
	CComPtr<ID3D11Buffer> pVertexBuffer;

	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (FAILED(hr)) {
		DLog(L"TextureCopyRect() : CreateRenderTargetView() failed with error %s", HR2Str(hr));
		return hr;
	}

	hr = CreateVertexBuffer(m_pDevice, &pVertexBuffer, Tex.desc.Width, Tex.desc.Height, srcRect, iRotation);
	if (FAILED(hr)) {
		return hr;
	}

	D3D11_VIEWPORT VP;
	VP.TopLeftX = (FLOAT)destRect.left;
	VP.TopLeftY = (FLOAT)destRect.top;
	VP.Width    = (FLOAT)destRect.Width();
	VP.Height   = (FLOAT)destRect.Height();
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;

	TextureBlt11(m_pDeviceContext, pRenderTargetView, VP, m_pVSimpleInputLayout, m_pVS_Simple, pPixelShader, Tex.pShaderResource, m_pSamplerPoint, pConstantBuffer, pVertexBuffer);

	return hr;
}

HRESULT CDX11VideoProcessor::TextureResizeShader(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect, ID3D11PixelShader* pPixelShader, const int iRotation)
{
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;
	CComPtr<ID3D11Buffer> pVertexBuffer;
	CComPtr<ID3D11Buffer> pConstantBuffer;

	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (FAILED(hr)) {
		DLog(L"TextureResizeShader() : CreateRenderTargetView() failed with error %s", HR2Str(hr));
		return hr;
	}

	hr = CreateVertexBuffer(m_pDevice, &pVertexBuffer, Tex.desc.Width, Tex.desc.Height, srcRect, iRotation);
	if (FAILED(hr)) {
		return hr;
	}

	const FLOAT constants[][4] = { // TODO: check for rotation
		{(float)Tex.desc.Width, (float)Tex.desc.Height, 1.0f / Tex.desc.Width, 1.0f / Tex.desc.Height},
		{(float)srcRect.Width() / dstRect.Width(), (float)srcRect.Height() / dstRect.Height(), 0, 0}
	};
	D3D11_BUFFER_DESC BufferDesc = {};
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(constants);
	BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData = { &constants, 0, 0 };

	hr = m_pDevice->CreateBuffer(&BufferDesc, &InitData, &pConstantBuffer);
	if (FAILED(hr)) {
		DLog(L"TextureResizeShader() : Create constant buffer failed with error %s", HR2Str(hr));
		return hr;
	}

	D3D11_VIEWPORT VP;
	VP.TopLeftX = (FLOAT)dstRect.left;
	VP.TopLeftY = (FLOAT)dstRect.top;
	VP.Width = (FLOAT)dstRect.Width();
	VP.Height = (FLOAT)dstRect.Height();
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;

	TextureBlt11(m_pDeviceContext, pRenderTargetView, VP, m_pVSimpleInputLayout, m_pVS_Simple, pPixelShader, Tex.pShaderResource, m_pSamplerPoint, pConstantBuffer, pVertexBuffer);

	return hr;
}

// CDX11VideoProcessor

CDX11VideoProcessor::CDX11VideoProcessor(CMpcVideoRenderer* pFilter)
	: m_pFilter(pFilter)
	, m_Font3D(L"Consolas", 14)
{
	m_hDXGILib = LoadLibraryW(L"dxgi.dll");
	if (!m_hDXGILib) {
		DLog(L"CDX11VideoProcessor::CDX11VideoProcessor() : failed to load dxgi.dll");
		return;
	}
	m_CreateDXGIFactory1 = (PFNCREATEDXGIFACTORY1)GetProcAddress(m_hDXGILib, "CreateDXGIFactory1");
	if (!m_CreateDXGIFactory1) {
		DLog(L"CDX11VideoProcessor::CDX11VideoProcessor() : failed to get CreateDXGIFactory1()");
		return;
	}
	HRESULT hr = m_CreateDXGIFactory1(IID_IDXGIFactory1, (void**)&m_pDXGIFactory1);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::CDX11VideoProcessor() : CreateDXGIFactory1() failed with error %s", HR2Str(hr));
		return;
	}

	m_hD3D11Lib = LoadLibraryW(L"d3d11.dll");
	if (!m_hD3D11Lib) {
		DLog(L"CDX11VideoProcessor::CDX11VideoProcessor() : failed to load d3d11.dll");
		return;
	}
	m_D3D11CreateDevice = (PFND3D11CREATEDEVICE)GetProcAddress(m_hD3D11Lib, "D3D11CreateDevice");
	if (!m_D3D11CreateDevice) {
		DLog(L"CDX11VideoProcessor::CDX11VideoProcessor() : failed to get D3D11CreateDevice()");
	}

	// set default ProcAmp ranges and values
	SetDefaultDXVA2ProcAmpRanges(m_DXVA2ProcAmpRanges);
	SetDefaultDXVA2ProcAmpValues(m_DXVA2ProcAmpValues);
}

CDX11VideoProcessor::~CDX11VideoProcessor()
{
	m_pDXGISwapChain1.Release();
	m_pDXGIFactory2.Release();

	ReleaseDevice();

	m_pDXGIFactory1.Release();

	if (m_hD3D11Lib) {
		FreeLibrary(m_hD3D11Lib);
	}

	if (m_hDXGILib) {
		FreeLibrary(m_hDXGILib);
	}
}

HRESULT CDX11VideoProcessor::Init(const HWND hwnd)
{
	DLog(L"CDX11VideoProcessor::Init()");

	CheckPointer(m_D3D11CreateDevice, E_FAIL);

	m_hWnd = hwnd;

	IDXGIAdapter* pDXGIAdapter = nullptr;
	const UINT currentAdapter = GetAdapter(hwnd, m_pDXGIFactory1, &pDXGIAdapter);
	CheckPointer(pDXGIAdapter, E_FAIL);
	if (m_nCurrentAdapter == currentAdapter) {
		SAFE_RELEASE(pDXGIAdapter);
		if (!m_pDXGISwapChain1) {
			InitSwapChain();
			UpdateStatsStatic();
		}
		return S_OK;
	}
	m_nCurrentAdapter = currentAdapter;

	m_pDXGISwapChain1.Release();
	m_pDXGIFactory2.Release();
	ReleaseDevice();

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
		//D3D_FEATURE_LEVEL_9_3 // necessary changes and testing
	};
	D3D_FEATURE_LEVEL featurelevel;

	ID3D11Device *pDevice = nullptr;
	ID3D11DeviceContext *pDeviceContext = nullptr;

	UINT flags = 0;
#ifdef _DEBUG
	HMODULE hD3D11SDKLayers = LoadLibraryW(L"D3D11_1SDKLayers.dll");
	if (hD3D11SDKLayers) {
		flags |= D3D11_CREATE_DEVICE_DEBUG;
		FreeLibrary(hD3D11SDKLayers);
	} else {
		DLog(L"D3D11_1SDKLayers.dll could not be loaded. D3D11 debugging messages will not be displayed");
	}
#endif

	HRESULT hr = m_D3D11CreateDevice(
		pDXGIAdapter,
		D3D_DRIVER_TYPE_UNKNOWN,
		nullptr,
		flags,
		featureLevels,
		std::size(featureLevels),
		D3D11_SDK_VERSION,
		&pDevice,
		&featurelevel,
		&pDeviceContext);
	SAFE_RELEASE(pDXGIAdapter);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::Init() : D3D11CreateDevice() failed with error %s", HR2Str(hr));
		return hr;
	}

	DLog(L"CDX11VideoProcessor::Init() : D3D11CreateDevice() successfully with feature level %d.%d", (featurelevel >> 12), (featurelevel >> 8) & 0xF);

	hr = SetDevice(pDevice, pDeviceContext);
	pDeviceContext->Release();
	pDevice->Release();

	if (S_OK == hr) {
		UpdateDiplayInfo();
		UpdateStatsStatic();
	}

	return hr;
}

bool CDX11VideoProcessor::Initialized()
{
	return (m_pDevice.p != nullptr && m_pDeviceContext.p != nullptr);
}

void CDX11VideoProcessor::ReleaseVP()
{
	DLog(L"CDX11VideoProcessor::ReleaseVP()");

	m_pFilter->ResetStreamingTimes2();
	m_RenderStats.Reset();

	if (m_pDeviceContext) {
		m_pDeviceContext->ClearState();
	}

	m_TexSrcVideo.Release();
	m_TexD3D11VPOutput.Release();
	m_TexConvertOutput.Release();
	m_TexResize.Release();
	m_TexsPostScale.Release();

	SAFE_RELEASE(m_PSConvColorData.pConstants);

	m_D3D11VP.ReleaseVideoProcessor();
	m_strCorrection = nullptr;

	m_srcParams      = {};
	m_srcDXGIFormat  = DXGI_FORMAT_UNKNOWN;
	m_srcDXVA2Format = D3DFMT_UNKNOWN;
	m_pConvertFn     = nullptr;
	m_srcWidth       = 0;
	m_srcHeight      = 0;
}

void CDX11VideoProcessor::ReleaseDevice()
{
	DLog(L"CDX11VideoProcessor::ReleaseDevice()");

	ReleaseVP();
	m_D3D11VP.ReleaseVideoDevice();

	m_Font3D.InvalidateDeviceObjects();
	m_Rect3D.InvalidateDeviceObjects();

	m_TexDither.Release();
	m_TexStats.Release();

	ClearPostScaleShaders();
	m_pPSCorrection.Release();
	m_pPSConvertColor.Release();

	m_pShaderUpscaleX.Release();
	m_pShaderUpscaleY.Release();
	m_pShaderDownscaleX.Release();
	m_pShaderDownscaleY.Release();
	m_strShaderX = nullptr;
	m_strShaderY = nullptr;
	m_pPSFinalPass.Release();

	SAFE_RELEASE(m_pPostScaleConstants);

	m_pVSimpleInputLayout.Release();
	m_pVS_Simple.Release();
	m_pPS_Simple.Release();
	SAFE_RELEASE(m_pSamplerPoint);
	SAFE_RELEASE(m_pSamplerLinear);
	SAFE_RELEASE(m_pSamplerDither);
	m_pAlphaBlendState.Release();
	m_pAlphaBlendStateInv.Release();
	SAFE_RELEASE(m_pFullFrameVertexBuffer);

	m_pShaderResourceSubPic.Release();
	m_pTextureSubPic.Release();

	m_pSurface9SubPic.Release();

	if (m_pDeviceContext) {
		// need ClearState() (see ReleaseVP()) and Flush() for ID3D11DeviceContext when using DXGI_SWAP_EFFECT_DISCARD in Windows 8/8.1
		m_pDeviceContext->Flush();
	}
	m_pDeviceContext.Release();
	ReleaseDX9Device();

#if (1 && _DEBUG)
	if (m_pDevice) {
		ID3D11Debug* pDebugDevice = nullptr;
		HRESULT hr2 = m_pDevice->QueryInterface(IID_PPV_ARGS(&pDebugDevice));
		if (S_OK == hr2) {
			hr2 = pDebugDevice->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
			ASSERT(S_OK == hr2);
		}
		SAFE_RELEASE(pDebugDevice);
	}
#endif

	m_pDevice.Release();
}

HRESULT CDX11VideoProcessor::CreatePShaderFromResource(ID3D11PixelShader** ppPixelShader, UINT resid)
{
	if (!m_pDevice || !ppPixelShader) {
		return E_POINTER;
	}

	LPVOID data;
	DWORD size;
	HRESULT hr = GetDataFromResource(data, size, resid);
	if (FAILED(hr)) {
		return hr;
	}

	return m_pDevice->CreatePixelShader(data, size, nullptr, ppPixelShader);
}

void CDX11VideoProcessor::SetShaderConvertColorParams()
{
	mp_csp_params csp_params;
	set_colorspace(m_srcExFmt, csp_params.color);
	csp_params.brightness = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Brightness) / 255;
	csp_params.contrast   = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Contrast);
	csp_params.hue        = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Hue) / 180 * acos(-1);
	csp_params.saturation = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Saturation);
	csp_params.gray       = m_srcParams.CSType == CS_GRAY;

	m_PSConvColorData.bEnable = m_srcParams.CSType == CS_YUV || csp_params.gray || fabs(csp_params.brightness) > 1e-4f || fabs(csp_params.contrast - 1.0f) > 1e-4f;

	mp_cmat cmatrix;
	mp_get_csp_matrix(&csp_params, &cmatrix);

	PS_COLOR_TRANSFORM cbuffer = {
		{cmatrix.m[0][0], cmatrix.m[0][1], cmatrix.m[0][2], 0},
		{cmatrix.m[1][0], cmatrix.m[1][1], cmatrix.m[1][2], 0},
		{cmatrix.m[2][0], cmatrix.m[2][1], cmatrix.m[2][2], 0},
		{cmatrix.c[0],    cmatrix.c[1],    cmatrix.c[2],    0},
	};

	if (m_srcParams.cformat == CF_Y410 || m_srcParams.cformat == CF_Y416) {
		std::swap(cbuffer.cm_r.x, cbuffer.cm_r.y);
		std::swap(cbuffer.cm_g.x, cbuffer.cm_g.y);
		std::swap(cbuffer.cm_b.x, cbuffer.cm_b.y);
	}
	else if (csp_params.gray) {
		cbuffer.cm_g.x = cbuffer.cm_g.y;
		cbuffer.cm_g.y = 0;
		cbuffer.cm_b.x = cbuffer.cm_b.z;
		cbuffer.cm_b.z = 0;
	}

	SAFE_RELEASE(m_PSConvColorData.pConstants);

	D3D11_BUFFER_DESC BufferDesc = {};
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(cbuffer);
	BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData = { &cbuffer, 0, 0 };
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateBuffer(&BufferDesc, &InitData, &m_PSConvColorData.pConstants));
}

void CDX11VideoProcessor::UpdateRenderRect()
{
	m_renderRect.IntersectRect(m_videoRect, m_windowRect);

	const int w2 = m_videoRect.Width();
	const int h2 = m_videoRect.Height();
	const int k = m_bInterpolateAt50pct ? 2 : 1;
	int w1, h1;
	if (m_iRotation == 90 || m_iRotation == 270) {
		w1 = m_srcRect.Height();
		h1 = m_srcRect.Width();
	} else {
		w1 = m_srcRect.Width();
		h1 = m_srcRect.Height();
	}
	m_strShaderX = (w1 == w2) ? nullptr
		: (w1 > k * w2)
		? s_Downscaling11ResIDs[m_iDownscaling].description
		: s_Upscaling11ResIDs[m_iUpscaling].description;
	m_strShaderY = (h1 == h2) ? nullptr
		: (h1 > k * h2)
		? s_Downscaling11ResIDs[m_iDownscaling].description
		: s_Upscaling11ResIDs[m_iUpscaling].description;
}

HRESULT CDX11VideoProcessor::MemCopyToTexSrcVideo(const BYTE* srcData, const int srcPitch)
{
	HRESULT hr = S_FALSE;
	D3D11_MAPPED_SUBRESOURCE mappedResource = {};

	if (m_TexSrcVideo.pTexture2) {
		hr = m_pDeviceContext->Map(m_TexSrcVideo.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (SUCCEEDED(hr)) {
			CopyFrameAsIs(m_srcHeight, (BYTE*)mappedResource.pData, mappedResource.RowPitch, srcData, srcPitch);
			m_pDeviceContext->Unmap(m_TexSrcVideo.pTexture, 0);

			hr = m_pDeviceContext->Map(m_TexSrcVideo.pTexture2, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
			if (SUCCEEDED(hr)) {
				const UINT cromaH = m_srcHeight / m_srcParams.pDX11Planes->div_chroma_h;
				const UINT cromaPitch = (m_TexSrcVideo.pTexture3) ? srcPitch / m_srcParams.pDX11Planes->div_chroma_w : srcPitch;
				srcData += srcPitch * m_srcHeight;
				CopyFrameAsIs(cromaH, (BYTE*)mappedResource.pData, mappedResource.RowPitch, srcData, cromaPitch);
				m_pDeviceContext->Unmap(m_TexSrcVideo.pTexture2, 0);

				if (m_TexSrcVideo.pTexture3) {
					hr = m_pDeviceContext->Map(m_TexSrcVideo.pTexture3, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
					if (SUCCEEDED(hr)) {
						srcData += cromaPitch * cromaH;
						CopyFrameAsIs(cromaH, (BYTE*)mappedResource.pData, mappedResource.RowPitch, srcData, cromaPitch);
						m_pDeviceContext->Unmap(m_TexSrcVideo.pTexture3, 0);
					}
				}
			}
		}
	} else {
		hr = m_pDeviceContext->Map(m_TexSrcVideo.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (SUCCEEDED(hr)) {
			ASSERT(m_pConvertFn);
			const BYTE* src = (srcPitch < 0) ? srcData + srcPitch * (1 - (int)m_srcLines) : srcData;
			m_pConvertFn(m_srcLines, (BYTE*)mappedResource.pData, mappedResource.RowPitch, src, srcPitch);
			m_pDeviceContext->Unmap(m_TexSrcVideo.pTexture, 0);
		}
	}

	return hr;
}

HRESULT CDX11VideoProcessor::SetDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext)
{
	DLog(L"CDX11VideoProcessor::SetDevice()");

	m_pDXGISwapChain1.Release();
	m_pDXGIFactory2.Release();
	ReleaseDevice();

	CheckPointer(pDevice, E_POINTER);
	CheckPointer(pContext, E_POINTER);

	m_pDevice = pDevice;
	m_pDeviceContext = pContext;

	HRESULT hr = m_D3D11VP.InitVideoDevice(m_pDevice, m_pDeviceContext);
	DLogIf(FAILED(hr), L"CDX11VideoProcessor::SetDevice() : InitVideoDevice failed with error %s", HR2Str(hr));

	D3D11_SAMPLER_DESC SampDesc = {};
	SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SampDesc.MinLOD = 0;
	SampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateSamplerState(&SampDesc, &m_pSamplerPoint));

	SampDesc.Filter = D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT; // linear interpolation for magnification
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateSamplerState(&SampDesc, &m_pSamplerLinear));

	SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateSamplerState(&SampDesc, &m_pSamplerDither));

	D3D11_BLEND_DESC bdesc = {};
	bdesc.RenderTarget[0].BlendEnable = TRUE;
	bdesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	bdesc.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_ALPHA;
	bdesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	bdesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	bdesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	bdesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	bdesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateBlendState(&bdesc, &m_pAlphaBlendState));

	bdesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateBlendState(&bdesc, &m_pAlphaBlendStateInv));

	EXECUTE_ASSERT(S_OK == CreateVertexBuffer(m_pDevice, &m_pFullFrameVertexBuffer, 1, 1, CRect(0, 0, 1, 1), 0));

	LPVOID data;
	DWORD size;
	EXECUTE_ASSERT(S_OK == GetDataFromResource(data, size, IDF_VSH11_SIMPLE));
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateVertexShader(data, size, nullptr, &m_pVS_Simple));

	D3D11_INPUT_ELEMENT_DESC Layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateInputLayout(Layout, std::size(Layout), data, size, &m_pVSimpleInputLayout));

	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPS_Simple, IDF_PSH11_SIMPLE));

	D3D11_BUFFER_DESC BufferDesc = {};
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(PS_EXTSHADER_CONSTANTS);
	BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateBuffer(&BufferDesc, nullptr, &m_pPostScaleConstants));

	CComPtr<IDXGIDevice> pDXGIDevice;
	hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDevice));
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::SetDevice() : QueryInterface(IDXGIDevice) failed with error %s", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}

	CComPtr<IDXGIAdapter> pDXGIAdapter;
	hr = pDXGIDevice->GetAdapter(&pDXGIAdapter);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::SetDevice() : GetAdapter(IDXGIAdapter) failed with error %s", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}

	CComPtr<IDXGIFactory1> pDXGIFactory1;
	hr = pDXGIAdapter->GetParent(IID_PPV_ARGS(&pDXGIFactory1));
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::SetDevice() : GetParent(IDXGIFactory1) failed with error %s", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}

	hr = pDXGIFactory1->QueryInterface(IID_PPV_ARGS(&m_pDXGIFactory2));
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::SetDevice() : QueryInterface(IDXGIFactory2) failed with error %s", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}

	if (m_pFilter->m_inputMT.IsValid()) {
		if (!InitMediaType(&m_pFilter->m_inputMT)) {
			ReleaseDevice();
			return E_FAIL;
		}
	}

	hr = InitDX9Device(m_hWnd, nullptr);
	ASSERT(S_OK == hr);
	if (m_pD3DDevEx) {
		// set a special blend mode for alpha channels for ISubRenderCallback rendering
		// this is necessary for the second alpha blending
		m_pD3DDevEx->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
		m_pD3DDevEx->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
		m_pD3DDevEx->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO);
		m_pD3DDevEx->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
	}

	if (m_hWnd) {
		hr = InitSwapChain();
		if (FAILED(hr)) {
			ReleaseDevice();
			return hr;
		}
	}

	DXGI_ADAPTER_DESC dxgiAdapterDesc = {};
	hr = pDXGIAdapter->GetDesc(&dxgiAdapterDesc);
	if (SUCCEEDED(hr)) {
		m_VendorId = dxgiAdapterDesc.VendorId;
		m_strAdapterDescription.Format(L"%s (%04X:%04X)", dxgiAdapterDesc.Description, dxgiAdapterDesc.VendorId, dxgiAdapterDesc.DeviceId);
		DLog(L"Graphics adapter: %s", m_strAdapterDescription);
	}

	HRESULT hr2 = m_TexStats.Create(m_pDevice, DXGI_FORMAT_B8G8R8A8_UNORM, STATS_W, STATS_H, Tex2D_DefaultShaderRTarget);
	if (S_OK == hr2) {
		hr2 = m_Font3D.InitDeviceObjects(m_pDevice, m_pDeviceContext);
	}
	if (S_OK == hr2) {
		hr2 = m_Rect3D.InitDeviceObjects(m_pDevice, m_pDeviceContext);
	}
	ASSERT(S_OK == hr2);

	HRESULT hr3 = m_TexDither.Create(m_pDevice, DXGI_FORMAT_R16G16B16A16_FLOAT, dither_size, dither_size, Tex2D_DynamicShaderWrite);
	if (S_OK == hr3) {
		LPVOID data;
		DWORD size;
		hr3 = GetDataFromResource(data, size, IDF_DITHER_32X32_FLOAT16);
		if (S_OK == hr3) {
			D3D11_MAPPED_SUBRESOURCE mappedResource;
			hr3 = m_pDeviceContext->Map(m_TexDither.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
			if (S_OK == hr3) {
				uint16_t* src = (uint16_t*)data;
				BYTE* dst = (BYTE*)mappedResource.pData;
				for (UINT y = 0; y < dither_size; y++) {
					uint16_t* pUInt16 = reinterpret_cast<uint16_t*>(dst);
					for (UINT x = 0; x < dither_size; x++) {
						*pUInt16++ = src[x];
						*pUInt16++ = src[x];
						*pUInt16++ = src[x];
						*pUInt16++ = src[x];
					}
					src += dither_size;
					dst += mappedResource.RowPitch;
				}
				m_pDeviceContext->Unmap(m_TexDither.pTexture, 0);
			}
		}
		if (S_OK == hr3) {
			m_pPSFinalPass.Release();
			hr3 = CreatePShaderFromResource(&m_pPSFinalPass, IDF_PSH11_FINAL_PASS);
		}

		if (FAILED(hr3)) {
			m_TexDither.Release();
		}
	}

	return hr;
}

HRESULT CDX11VideoProcessor::InitSwapChain()
{
	DLog(L"CDX11VideoProcessor::InitSwapChain()");
	CheckPointer(m_pDXGIFactory2, E_FAIL);

	m_pDXGISwapChain1.Release();

	DXGI_SWAP_CHAIN_DESC1 desc1 = {};
	desc1.Width  = m_windowRect.Width();
	desc1.Height = m_windowRect.Height();
	desc1.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // the most common swap chain format
	desc1.SampleDesc.Count = 1;
	desc1.SampleDesc.Quality = 0;
	desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	if (m_iSwapEffect == SWAPEFFECT_Flip && IsWindows8OrGreater()) {
		desc1.BufferCount = 2;
		desc1.Scaling = DXGI_SCALING_NONE;
#if VER_PRODUCTBUILD >= 10000
		desc1.SwapEffect = IsWindows10OrGreater() ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
#else
		desc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
#endif
	} else { // default SWAPEFFECT_Discard
		desc1.BufferCount = 1;
		desc1.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	}
	desc1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	HRESULT hr = m_pDXGIFactory2->CreateSwapChainForHwnd(m_pDevice, m_hWnd, &desc1, nullptr, nullptr, &m_pDXGISwapChain1);

	DLogIf(FAILED(hr), L"CDX11VideoProcessor::InitSwapChain() : CreateSwapChainForHwnd() failed with error %s", HR2Str(hr));

	if (m_pDXGISwapChain1) {
		m_pShaderResourceSubPic.Release();
		m_pTextureSubPic.Release();

		m_pSurface9SubPic.Release();

		if (m_pD3DDevEx) {
			HANDLE sharedHandle = nullptr;
			HRESULT hr2 = m_pD3DDevEx->CreateRenderTarget(
				m_d3dpp.BackBufferWidth,
				m_d3dpp.BackBufferHeight,
				D3DFMT_A8R8G8B8,
				D3DMULTISAMPLE_NONE,
				0,
				FALSE,
				&m_pSurface9SubPic,
				&sharedHandle);
			DLogIf(FAILED(hr2), L"CDX11VideoProcessor::InitSwapChain() : CreateRenderTarget(Direct3D9) failed with error %s", HR2Str(hr2));

			if (m_pSurface9SubPic) {
				hr2 = m_pDevice->OpenSharedResource(sharedHandle, IID_PPV_ARGS(&m_pTextureSubPic));
				DLogIf(FAILED(hr2), L"CDX11VideoProcessor::InitSwapChain() : OpenSharedResource() failed with error %s", HR2Str(hr2));
			}

			if (m_pTextureSubPic) {
				ID3D11RenderTargetView* pRenderTargetView;
				if (S_OK == m_pDevice->CreateRenderTargetView(m_pTextureSubPic, nullptr, &pRenderTargetView)) {
					const FLOAT ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
					m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ClearColor);
					pRenderTargetView->Release();
				}

				D3D11_TEXTURE2D_DESC texdesc = {};
				m_pTextureSubPic->GetDesc(&texdesc);
				if (texdesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
					D3D11_SHADER_RESOURCE_VIEW_DESC shaderDesc;
					shaderDesc.Format = texdesc.Format;
					shaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
					shaderDesc.Texture2D.MostDetailedMip = 0; // = Texture2D desc.MipLevels - 1
					shaderDesc.Texture2D.MipLevels = 1;       // = Texture2D desc.MipLevels

					hr2 = m_pDevice->CreateShaderResourceView(m_pTextureSubPic, &shaderDesc, &m_pShaderResourceSubPic);
					DLogIf(FAILED(hr2), L"CDX11VideoProcessor::InitSwapChain() : CreateShaderResourceView() failed with error %s", HR2Str(hr2));
				}

				if (m_pShaderResourceSubPic) {
					hr2 = m_pD3DDevEx->ColorFill(m_pSurface9SubPic, nullptr, D3DCOLOR_ARGB(255, 0, 0, 0));
					hr2 = m_pD3DDevEx->SetRenderTarget(0, m_pSurface9SubPic);
					DLogIf(FAILED(hr2), L"CDX11VideoProcessor::InitSwapChain() : SetRenderTarget(Direct3D9) failed with error %s", HR2Str(hr2));
				}
			}
		}
	}

	return hr;
}

BOOL CDX11VideoProcessor::VerifyMediaType(const CMediaType* pmt)
{
	const auto FmtConvParams = GetFmtConvParams(pmt->subtype);
	if (FmtConvParams.VP11Format == DXGI_FORMAT_UNKNOWN && FmtConvParams.DX11Format == DXGI_FORMAT_UNKNOWN) {
		return FALSE;
	}

	const BITMAPINFOHEADER* pBIH = GetBIHfromVIHs(pmt);
	if (!pBIH) {
		return FALSE;
	}

	if (pBIH->biWidth <= 0 || !pBIH->biHeight || (!pBIH->biSizeImage && pBIH->biCompression != BI_RGB)) {
		return FALSE;
	}

	if (FmtConvParams.Subsampling == 420 && ((pBIH->biWidth & 1) || (pBIH->biHeight & 1))) {
		return FALSE;
	}
	if (FmtConvParams.Subsampling == 422 && (pBIH->biWidth & 1)) {
		return FALSE;
	}

	return TRUE;
}

BOOL CDX11VideoProcessor::InitMediaType(const CMediaType* pmt)
{
	DLog(L"CDX11VideoProcessor::InitMediaType()");

	if (!VerifyMediaType(pmt)) {
		return FALSE;
	}

	ReleaseVP();

	auto FmtConvParams = GetFmtConvParams(pmt->subtype);
	bool disableD3D11VP = false;
	switch (FmtConvParams.cformat) {
	case CF_NV12: disableD3D11VP = !m_VPFormats.bNV12; break;
	case CF_P010:
	case CF_P016: disableD3D11VP = !m_VPFormats.bP01x;  break;
	case CF_YUY2: disableD3D11VP = !m_VPFormats.bYUY2;  break;
	default:      disableD3D11VP = !m_VPFormats.bOther; break;
	}
	if (disableD3D11VP) {
		FmtConvParams.VP11Format = DXGI_FORMAT_UNKNOWN;
	}

	const GUID SubType = pmt->subtype;
	const BITMAPINFOHEADER* pBIH = nullptr;
	m_decExFmt.value = 0;

	if (pmt->formattype == FORMAT_VideoInfo2) {
		const VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pmt->pbFormat;
		pBIH = &vih2->bmiHeader;
		m_srcRect = vih2->rcSource;
		m_srcAspectRatioX = vih2->dwPictAspectRatioX;
		m_srcAspectRatioY = vih2->dwPictAspectRatioY;
		if (FmtConvParams.CSType == CS_YUV && (vih2->dwControlFlags & (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT))) {
			m_decExFmt.value = vih2->dwControlFlags;
			m_decExFmt.SampleFormat = AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT; // ignore other flags
		}
		m_bInterlaced = (vih2->dwInterlaceFlags & AMINTERLACE_IsInterlaced);
		m_rtAvgTimePerFrame = vih2->AvgTimePerFrame;
	}
	else if (pmt->formattype == FORMAT_VideoInfo) {
		const VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)pmt->pbFormat;
		pBIH = &vih->bmiHeader;
		m_srcRect = vih->rcSource;
		m_srcAspectRatioX = 0;
		m_srcAspectRatioY = 0;
		m_bInterlaced = 0;
		m_rtAvgTimePerFrame = vih->AvgTimePerFrame;
	}
	else {
		return FALSE;
	}

	UINT biWidth  = pBIH->biWidth;
	UINT biHeight = labs(pBIH->biHeight);
	UINT biSizeImage = pBIH->biSizeImage;
	if (pBIH->biSizeImage == 0 && pBIH->biCompression == BI_RGB) { // biSizeImage may be zero for BI_RGB bitmaps
		biSizeImage = biWidth * biHeight * pBIH->biBitCount / 8;
	}

	m_srcLines = biHeight * FmtConvParams.PitchCoeff / 2;
	m_srcPitch = biWidth * FmtConvParams.Packsize;
	switch (FmtConvParams.cformat) {
	case CF_Y8:
	case CF_NV12:
	case CF_RGB24:
		m_srcPitch = ALIGN(m_srcPitch, 4);
		break;
	}
	if (pBIH->biCompression == BI_RGB && pBIH->biHeight > 0) {
		m_srcPitch = -m_srcPitch;
	}

	UINT origW = biWidth;
	UINT origH = biHeight;
	if (pmt->FormatLength() == 112 + sizeof(VR_Extradata)) {
		const VR_Extradata* vrextra = reinterpret_cast<VR_Extradata*>(pmt->pbFormat + 112);
		if (vrextra->QueryWidth == pBIH->biWidth && vrextra->QueryHeight == pBIH->biHeight && vrextra->Compression == pBIH->biCompression) {
			origW  = vrextra->FrameWidth;
			origH = abs(vrextra->FrameHeight);
		}
	}

	if (m_srcRect.IsRectNull()) {
		m_srcRect.SetRect(0, 0, origW, origH);
	}
	m_srcRectWidth  = m_srcRect.Width();
	m_srcRectHeight = m_srcRect.Height();

	m_srcExFmt = SpecifyExtendedFormat(m_decExFmt, FmtConvParams, m_srcRectWidth, m_srcRectHeight);

	if (!m_srcAspectRatioX || !m_srcAspectRatioY) {
		const auto gcd = std::gcd(m_srcRectWidth, m_srcRectHeight);
		m_srcAspectRatioX = m_srcRectWidth / gcd;
		m_srcAspectRatioY = m_srcRectHeight / gcd;
	}

	UpdateUpscalingShaders();
	UpdateDownscalingShaders();

	m_pPSCorrection.Release();
	m_pPSConvertColor.Release();
	m_PSConvColorData.bEnable = false;

	switch (m_iTexFormat) {
	case TEXFMT_AUTOINT:
		m_InternalTexFmt = (FmtConvParams.CDepth > 8) ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
		break;
	case TEXFMT_8INT:    m_InternalTexFmt = DXGI_FORMAT_B8G8R8A8_UNORM;     break;
	case TEXFMT_10INT:   m_InternalTexFmt = DXGI_FORMAT_R10G10B10A2_UNORM;  break;
	case TEXFMT_16FLOAT: m_InternalTexFmt = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
	default:
		ASSERT(FALSE);
	}

	// D3D11 Video Processor
	if (FmtConvParams.VP11Format != DXGI_FORMAT_UNKNOWN && !(m_VendorId == PCIV_NVIDIA && FmtConvParams.CSType == CS_RGB)) {
		// D3D11 VP does not work correctly if RGB32 with odd frame width (source or target) on Nvidia adapters

		if (S_OK == InitializeD3D11VP(FmtConvParams, biWidth, biHeight)) {
			if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084) {
				EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_CORRECTION_ST2084));
				m_strCorrection = L"ST 2084 correction";
			}
			else if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG) {
				EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_CORRECTION_HLG));
				m_strCorrection = L"HLG correction";
			}
			else if (m_srcExFmt.VideoTransferMatrix == VIDEOTRANSFERMATRIX_YCgCo) {
				EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_CORRECTION_YCGCO));
				m_strCorrection = L"YCoCg correction";
			}
			DLogIf(m_pPSCorrection, L"CDX11VideoProcessor::InitMediaType() m_pPSCorrection created");

			m_pFilter->m_inputMT = *pmt;
			UpdateTexures(m_videoRect.Width(), m_videoRect.Height());
			UpdatePostScaleTexures();
			UpdateStatsStatic();

			if (m_pFilter->m_pSubCallBack) {
				HRESULT hr2 = m_pFilter->m_pSubCallBack->SetDevice(m_pD3DDevEx);
			}

			return TRUE;
		}

		ReleaseVP();
	}

	// Tex Video Processor
	if (FmtConvParams.DX11Format != DXGI_FORMAT_UNKNOWN && S_OK == InitializeTexVP(FmtConvParams, origW, origH)) {
		m_pFilter->m_inputMT = *pmt;
		SetShaderConvertColorParams();
		UpdateTexures(m_videoRect.Width(), m_videoRect.Height());
		UpdatePostScaleTexures();
		UpdateStatsStatic();

		if (m_pFilter->m_pSubCallBack) {
			HRESULT hr2 = m_pFilter->m_pSubCallBack->SetDevice(m_pD3DDevEx);
		}

		return TRUE;
	}

	return FALSE;
}

HRESULT CDX11VideoProcessor::InitializeD3D11VP(const FmtConvParams_t& params, const UINT width, const UINT height)
{
	if (!m_D3D11VP.IsVideoDeviceOk()) {
		return E_ABORT;
	}

	const auto& dxgiFormat = params.VP11Format;

	DLog(L"CDX11VideoProcessor::InitializeD3D11VP() started with input surface: %s, %u x %u", DXGIFormatToString(dxgiFormat), width, height);

	m_TexSrcVideo.Release();

	m_D3D11OutputFmt = m_InternalTexFmt;
	HRESULT hr = m_D3D11VP.InitVideoProcessor(dxgiFormat, width, height, m_bInterlaced, m_D3D11OutputFmt);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : InitVideoProcessor() failed with error %s", HR2Str(hr));
		return hr;
	}

	hr = m_D3D11VP.InitInputTextures(m_pDevice);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : InitInputTextures() failed with error %s", HR2Str(hr));
		return hr;
	}

	hr = m_D3D11VP.SetColorSpace(m_srcExFmt);

	hr = m_TexSrcVideo.Create(m_pDevice, dxgiFormat, width, height, Tex2D_DynamicShaderWriteNoSRV);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : m_TexSrcVideo.Create() failed with error %s", HR2Str(hr));
		return hr;
	}

	m_srcWidth       = width;
	m_srcHeight      = height;
	m_srcParams      = params;
	m_srcDXGIFormat  = dxgiFormat;
	m_srcDXVA2Format = params.DXVA2Format;
	m_pConvertFn     = GetCopyFunction(params);

	DLog(L"CDX11VideoProcessor::InitializeD3D11VP() completed successfully");

	return S_OK;
}

HRESULT CDX11VideoProcessor::InitializeTexVP(const FmtConvParams_t& params, const UINT width, const UINT height)
{
	const auto& srcDXGIFormat = params.DX11Format;

	DLog(L"CDX11VideoProcessor::InitializeTexVP() started with input surface: %s, %u x %u", DXGIFormatToString(srcDXGIFormat), width, height);

	UINT texW = (params.cformat == CF_YUY2) ? width / 2 : width;

	HRESULT hr = m_TexSrcVideo.CreateEx(m_pDevice, srcDXGIFormat, params.pDX11Planes, texW, height, Tex2D_DynamicShaderWrite);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeTexVP() : m_TexSrcVideo.CreateEx() failed with error %s", HR2Str(hr));
		return hr;
	}

	m_srcWidth       = width;
	m_srcHeight      = height;
	m_srcParams      = params;
	m_srcDXGIFormat  = srcDXGIFormat;
	m_srcDXVA2Format = params.DXVA2Format;
	m_pConvertFn     = GetCopyFunction(params);

	// set default ProcAmp ranges
	SetDefaultDXVA2ProcAmpRanges(m_DXVA2ProcAmpRanges);

	HRESULT hr2 = UpdateChromaScalingShader();
	if (FAILED(hr2)) {
		ASSERT(0);
		UINT resid = 0;
		if (params.cformat == CF_YUY2) {
			resid = IDF_PSH11_CONVERT_YUY2;
		}
		else if (params.pDX11Planes) {
			if (params.pDX11Planes->FmtPlane3) {
				resid = IDF_PSH11_CONVERT_PLANAR;
			} else {
				resid = IDF_PSH11_CONVERT_BIPLANAR;
			}
		}
		else {
			resid = IDF_PSH11_CONVERT_COLOR;
		}
		EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSConvertColor, resid));
	}

	DLog(L"CDX11VideoProcessor::InitializeTexVP() completed successfully");

	return S_OK;
}

void CDX11VideoProcessor::UpdatFrameProperties()
{
	m_srcPitch = m_srcWidth * m_srcParams.Packsize;
	m_srcLines = m_srcHeight * m_srcParams.PitchCoeff / 2;
}

BOOL CDX11VideoProcessor::GetAlignmentSize(const CMediaType& mt, SIZE& Size)
{
	if (InitMediaType(&mt)) {
		const auto& FmtParams = GetFmtConvParams(mt.subtype);

		if (FmtParams.cformat == CF_RGB24) {
			Size.cx = ALIGN(Size.cx, 4);
		}
		else if (FmtParams.cformat == CF_RGB48 || FmtParams.cformat == CF_B48R) {
			Size.cx = ALIGN(Size.cx, 2);
		}
		else if (FmtParams.cformat == CF_ARGB64 || FmtParams.cformat == CF_B64A) {
			// nothing
		}
		else {
			if (!m_TexSrcVideo.pTexture) {
				return FALSE;
			}

			UINT RowPitch = 0;
			D3D11_MAPPED_SUBRESOURCE mappedResource = {};
			if (SUCCEEDED(m_pDeviceContext->Map(m_TexSrcVideo.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
				RowPitch = mappedResource.RowPitch;
				m_pDeviceContext->Unmap(m_TexSrcVideo.pTexture, 0);
			}

			if (!RowPitch) {
				return FALSE;
			}

			Size.cx = RowPitch / FmtParams.Packsize;
		}

		if (FmtParams.cformat == CF_RGB24 || FmtParams.cformat == CF_XRGB32 || FmtParams.cformat == CF_ARGB32) {
			Size.cy = -abs(Size.cy); // only for biCompression == BI_RGB
		} else {
			Size.cy = abs(Size.cy);
		}

		return TRUE;

	}

	return FALSE;
}

void CDX11VideoProcessor::Start()
{
	m_rtStart = 0;
	//UpdatePostScaleTexures();
}

HRESULT CDX11VideoProcessor::ProcessSample(IMediaSample* pSample)
{
	REFERENCE_TIME rtStart = m_pFilter->m_FrameStats.GeTimestamp();
	const REFERENCE_TIME rtFrameDur = m_pFilter->m_FrameStats.GetAverageFrameDuration();
	const REFERENCE_TIME rtEnd = rtStart + rtFrameDur;

	m_rtStart = rtStart;
	CRefTime rtClock(rtStart);

	HRESULT hr = CopySample(pSample);
	if (FAILED(hr)) {
		m_RenderStats.failed++;
		return hr;
	}

	// always Render(1) a frame after CopySample()
	hr = Render(1);
	m_pFilter->m_DrawStats.Add(GetPreciseTick());
	if (m_pFilter->m_filterState == State_Running) {
		m_pFilter->StreamTime(rtClock);
	}

	m_RenderStats.syncoffset = rtClock - rtStart;

	if (SecondFramePossible()) {
		if (rtEnd < rtClock) {
			m_RenderStats.dropped2++;
			return S_FALSE; // skip frame
		}

		hr = Render(2);
		m_pFilter->m_DrawStats.Add(GetPreciseTick());
		if (m_pFilter->m_filterState == State_Running) {
			m_pFilter->StreamTime(rtClock);
		}

		rtStart += rtFrameDur / 2;
		m_RenderStats.syncoffset = rtClock - rtStart;
	}

	return hr;
}

HRESULT CDX11VideoProcessor::CopySample(IMediaSample* pSample)
{
	CheckPointer(m_pDXGISwapChain1, E_FAIL);

	uint64_t tick = GetPreciseTick();

	// Get frame type
	m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE; // Progressive
	if (m_bInterlaced) {
		if (CComQIPtr<IMediaSample2> pMS2 = pSample) {
			AM_SAMPLE2_PROPERTIES props;
			if (SUCCEEDED(pMS2->GetProperties(sizeof(props), (BYTE*)&props))) {
				m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST;  // Bottom-field first
				if (props.dwTypeSpecificFlags & AM_VIDEO_FLAG_WEAVE) {
					m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;                // Progressive
				} else if (props.dwTypeSpecificFlags & AM_VIDEO_FLAG_FIELD1FIRST) {
					m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST; // Top-field first
				}
			}
		}
	}

	HRESULT hr = S_OK;
	m_FieldDrawn = 0;

	if (CComQIPtr<IMediaSampleD3D11> pMSD3D11 = pSample) {
		m_iSrcFromGPU = 11;

		CComQIPtr<ID3D11Texture2D> pD3D11Texture2D;
		UINT ArraySlice = 0;
		hr = pMSD3D11->GetD3D11Texture(0, &pD3D11Texture2D, &ArraySlice);
		if (FAILED(hr)) {
			DLog(L"CDX11VideoProcessor::CopySample() : GetD3D11Texture() failed with error %s", HR2Str(hr));
			return hr;
		}

		D3D11_TEXTURE2D_DESC desc = {};
		pD3D11Texture2D->GetDesc(&desc);
		if (desc.Format != m_srcDXGIFormat) {
			return hr;
		}

		if (desc.Width != m_srcWidth || desc.Height != m_srcHeight) {
			if (m_D3D11VP.IsReady()) {
				hr = InitializeD3D11VP(m_srcParams, desc.Width, desc.Height);
			} else {
				hr = InitializeTexVP(m_srcParams, desc.Width, desc.Height);
			}
			if (FAILED(hr)) {
				return hr;
			}
			UpdatFrameProperties();
		}

		// here should be used CopySubresourceRegion instead of CopyResource
		if (m_D3D11VP.IsReady()) {
			m_pDeviceContext->CopySubresourceRegion(m_D3D11VP.GetNextInputTexture(m_SampleFormat), 0, 0, 0, 0, pD3D11Texture2D, ArraySlice, nullptr);
		} else {
			m_pDeviceContext->CopySubresourceRegion(m_TexSrcVideo.pTexture, 0, 0, 0, 0, pD3D11Texture2D, ArraySlice, nullptr);
		}
	}
	else if (CComQIPtr<IMFGetService> pService = pSample) {
		m_iSrcFromGPU = 9;

		CComPtr<IDirect3DSurface9> pSurface9;
		if (SUCCEEDED(pService->GetService(MR_BUFFER_SERVICE, IID_PPV_ARGS(&pSurface9)))) {
			D3DSURFACE_DESC desc = {};
			hr = pSurface9->GetDesc(&desc);
			if (FAILED(hr) || desc.Format != m_srcDXVA2Format) {
				return E_FAIL;
			}

			if (desc.Width != m_srcWidth || desc.Height != m_srcHeight) {
				if (m_D3D11VP.IsReady()) {
					hr = InitializeD3D11VP(m_srcParams, desc.Width, desc.Height);
				} else {
					hr = InitializeTexVP(m_srcParams, desc.Width, desc.Height);
				}
				if (FAILED(hr)) {
					return hr;
				}
				UpdatFrameProperties();
			}

			D3DLOCKED_RECT lr_src;
			hr = pSurface9->LockRect(&lr_src, nullptr, D3DLOCK_READONLY); // slow
			if (S_OK == hr) {
				hr = MemCopyToTexSrcVideo((BYTE*)lr_src.pBits, lr_src.Pitch);
				pSurface9->UnlockRect();

				if (m_D3D11VP.IsReady()) {
					// ID3D11VideoProcessor does not use textures with D3D11_CPU_ACCESS_WRITE flag
					m_pDeviceContext->CopyResource(m_D3D11VP.GetNextInputTexture(m_SampleFormat), m_TexSrcVideo.pTexture);
				}
			}
		}
	}
	else {
		m_iSrcFromGPU = 0;

		BYTE* data = nullptr;
		const long size = pSample->GetActualDataLength();
		if (size > 0 && S_OK == pSample->GetPointer(&data)) {
			hr = MemCopyToTexSrcVideo(data, m_srcPitch);

			if (m_D3D11VP.IsReady()) {
				// ID3D11VideoProcessor does not use textures with D3D11_CPU_ACCESS_WRITE flag
				m_pDeviceContext->CopyResource(m_D3D11VP.GetNextInputTexture(m_SampleFormat), m_TexSrcVideo.pTexture);
			}
		}
	}

	m_RenderStats.copyticks = GetPreciseTick() - tick;

	return hr;
}

HRESULT CDX11VideoProcessor::Render(int field)
{
	CheckPointer(m_TexSrcVideo.pTexture, E_FAIL);
	CheckPointer(m_pDXGISwapChain1, E_FAIL);

	if (field) {
		m_FieldDrawn = field;
	}

	CComPtr<ID3D11Texture2D> pBackBuffer;
	HRESULT hr = m_pDXGISwapChain1->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::Render() : GetBuffer() failed with error %s", HR2Str(hr));
		return hr;
	}

	uint64_t tick1 = GetPreciseTick();

	HRESULT hrSubPic = E_FAIL;
	if (m_pFilter->m_pSubCallBack && m_pShaderResourceSubPic) {
		const CRect rSrcPri(CPoint(0, 0), m_windowRect.Size());
		const CRect rDstVid(m_videoRect);
		const auto rtStart = m_pFilter->m_rtStartTime + m_rtStart;

		if (CComQIPtr<ISubRenderCallback4> pSubCallBack4 = m_pFilter->m_pSubCallBack) {
			hrSubPic = pSubCallBack4->RenderEx3(rtStart, 0, m_rtAvgTimePerFrame, rDstVid, rDstVid, rSrcPri);
		} else {
			hrSubPic = m_pFilter->m_pSubCallBack->Render(rtStart, rDstVid.left, rDstVid.top, rDstVid.right, rDstVid.bottom, rSrcPri.Width(), rSrcPri.Height());
		}

		if (S_OK == hrSubPic) {
			// flush Direct3D9 for immediate update Direct3D11 texture
			CComPtr<IDirect3DQuery9> pEventQuery;
			m_pD3DDevEx->CreateQuery(D3DQUERYTYPE_EVENT, &pEventQuery);
			if (pEventQuery) {
				pEventQuery->Issue(D3DISSUE_END);
				BOOL Data = FALSE;
				while (S_FALSE == pEventQuery->GetData(&Data, sizeof(Data), D3DGETDATA_FLUSH));
			}
		}
	}

	uint64_t tick2 = GetPreciseTick();

	if (!m_windowRect.IsRectEmpty()) {
		// fill the BackBuffer with black
		ID3D11RenderTargetView* pRenderTargetView;
		if (S_OK == m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView)) {
			const FLOAT ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ClearColor);
			pRenderTargetView->Release();
		}
	}

	if (!m_renderRect.IsRectEmpty()) {
		hr = Process(pBackBuffer, m_srcRect, m_videoRect, m_FieldDrawn == 2);
	}

	if (S_OK == hrSubPic) {
		const CRect rSrcPri(CPoint(0, 0), m_windowRect.Size());

		D3D11_VIEWPORT VP;
		VP.TopLeftX = 0;
		VP.TopLeftY = 0;
		VP.Width = rSrcPri.Width();
		VP.Height = rSrcPri.Height();
		VP.MinDepth = 0.0f;
		VP.MaxDepth = 1.0f;
		hrSubPic = AlphaBltSub(m_pShaderResourceSubPic, pBackBuffer, rSrcPri, VP);
		ASSERT(S_OK == hrSubPic);

		hrSubPic = m_pD3DDevEx->ColorFill(m_pSurface9SubPic, nullptr, m_pFilter->m_bSubInvAlpha ? D3DCOLOR_ARGB(0, 0, 0, 0) : D3DCOLOR_ARGB(255, 0, 0, 0));
	}

	if (m_bShowStats) {
		hr = DrawStats(pBackBuffer);
	}

#if 0
	{ // Tearing test (very non-optimal implementation, use only for tests)
		static int nTearingPos = 0;

		ID3D11RenderTargetView* pRenderTargetView;
		if (S_OK == m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView)) {
			CD3D11Rectangle d3d11rect;
			HRESULT hr2 = d3d11rect.InitDeviceObjects(m_pDevice, m_pDeviceContext);

			const SIZE szWindow = m_windowRect.Size();
			RECT rcTearing;

			rcTearing.left = nTearingPos;
			rcTearing.top = 0;
			rcTearing.right = rcTearing.left + 4;
			rcTearing.bottom = szWindow.cy;
			hr2 = d3d11rect.Set(rcTearing, szWindow, D3DCOLOR_XRGB(255, 0, 0));
			hr2 = d3d11rect.Draw(pRenderTargetView, szWindow);

			rcTearing.left = (rcTearing.right + 15) % szWindow.cx;
			rcTearing.right = rcTearing.left + 4;
			hr2 = d3d11rect.Set(rcTearing, szWindow, D3DCOLOR_XRGB(255, 0, 0));
			hr2 = d3d11rect.Draw(pRenderTargetView, szWindow);

			pRenderTargetView->Release();
			d3d11rect.InvalidateDeviceObjects();

			nTearingPos = (nTearingPos + 7) % szWindow.cx;
		}
	}
#endif
	m_RenderStats.substicks = tick2 - tick1; // after DrawStats to relate to paintticks
	uint64_t tick5 = GetPreciseTick();
	m_RenderStats.paintticks = tick5 - tick1;

	hr = m_pDXGISwapChain1->Present(1, 0);
	m_RenderStats.presentticks = GetPreciseTick() - tick5;

	return hr;
}

HRESULT CDX11VideoProcessor::FillBlack()
{
	CheckPointer(m_pDXGISwapChain1, E_ABORT);

	ID3D11Texture2D* pBackBuffer = nullptr;
	HRESULT hr = m_pDXGISwapChain1->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::FillBlack() : GetBuffer() failed with error %s", HR2Str(hr));
		return hr;
	}

	ID3D11RenderTargetView* pRenderTargetView;
	hr = m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView);
	pBackBuffer->Release();
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::FillBlack() : CreateRenderTargetView() failed with error %s", HR2Str(hr));
		return hr;
	}

	float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ClearColor);

	hr = m_pDXGISwapChain1->Present(1, 0);

	pRenderTargetView->Release();

	return hr;
}

void CDX11VideoProcessor::UpdateTexures(int w, int h)
{
	if (!m_srcWidth || !m_srcHeight) {
		return;
	}

	// TODO: try making w and h a multiple of 128.
	HRESULT hr = S_OK;

	if (m_D3D11VP.IsReady()) {
		if (m_bVPScaling) {
			if (m_iRotation == 90 || m_iRotation == 270) {
				hr = m_TexD3D11VPOutput.CheckCreate(m_pDevice, m_D3D11OutputFmt, h, w, Tex2D_DefaultShaderRTarget);
			}
			else {
				hr = m_TexD3D11VPOutput.CheckCreate(m_pDevice, m_D3D11OutputFmt, w, h, Tex2D_DefaultShaderRTarget);
			}
		}
		else {
			hr = m_TexD3D11VPOutput.CheckCreate(m_pDevice, m_D3D11OutputFmt, m_srcWidth, m_srcHeight, Tex2D_DefaultShaderRTarget);
		}
		m_TexConvertOutput.Release();
	}
	else {
		m_TexD3D11VPOutput.Release();
		hr = m_TexConvertOutput.CheckCreate(m_pDevice, m_InternalTexFmt, m_srcWidth, m_srcHeight, Tex2D_DefaultShaderRTarget);
	}
}

void CDX11VideoProcessor::UpdatePostScaleTexures()
{
	m_bFinalPass = (m_bUseDither && m_InternalTexFmt != DXGI_FORMAT_B8G8R8A8_UNORM && m_TexDither.pTexture && m_pPSFinalPass);

	UINT numPostScaleShaders = m_pPostScaleShaders.size();
	if (m_pPSCorrection) {
		numPostScaleShaders++;
	}
	if (m_bFinalPass) {
		numPostScaleShaders++;
	}
	HRESULT hr = m_TexsPostScale.CheckCreate(m_pDevice, m_InternalTexFmt, m_windowRect.Width(), m_windowRect.Height(), numPostScaleShaders);
	UpdateStatsStatic3();
}

void CDX11VideoProcessor::UpdateUpscalingShaders()
{
	m_pShaderUpscaleX.Release();
	m_pShaderUpscaleY.Release();

	if (m_iUpscaling != UPSCALE_Nearest) {
		EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderUpscaleX, s_Upscaling11ResIDs[m_iUpscaling].shaderX));
		EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderUpscaleY, s_Upscaling11ResIDs[m_iUpscaling].shaderY));
	}
}

void CDX11VideoProcessor::UpdateDownscalingShaders()
{
	m_pShaderDownscaleX.Release();
	m_pShaderDownscaleY.Release();

	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderDownscaleX, s_Downscaling11ResIDs[m_iDownscaling].shaderX));
	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderDownscaleY, s_Downscaling11ResIDs[m_iDownscaling].shaderY));
}

HRESULT CDX11VideoProcessor::UpdateChromaScalingShader()
{
	m_pPSConvertColor.Release();
	ID3DBlob* pShaderCode = nullptr;

	HRESULT hr = GetShaderConvertColor(true, m_TexSrcVideo.desc.Width, m_TexSrcVideo.desc.Height, m_srcParams, m_srcExFmt, m_iChromaScaling, &pShaderCode);
	if (S_OK == hr) {
		hr = m_pDevice->CreatePixelShader(pShaderCode->GetBufferPointer(), pShaderCode->GetBufferSize(), nullptr, &m_pPSConvertColor);
		pShaderCode->Release();
	}

	return hr;
}

HRESULT CDX11VideoProcessor::D3D11VPPass(ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect, const bool second)
{
	HRESULT hr = m_D3D11VP.SetRectangles(srcRect, dstRect);

	hr = m_D3D11VP.Process(pRenderTarget, m_SampleFormat, second);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::ProcessD3D11() : m_D3D11VP.Process() failed with error %s", HR2Str(hr));
	}

	return hr;
}

HRESULT CDX11VideoProcessor::ConvertColorPass(ID3D11Texture2D* pRenderTarget)
{
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;

	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (FAILED(hr)) {
		DLog(L"ConvertColorPass() : CreateRenderTargetView() failed with error %s", HR2Str(hr));
		return hr;
	}

	UINT width = (m_srcParams.cformat == CF_YUY2)
		? m_TexSrcVideo.desc.Width * 2
		: m_TexSrcVideo.desc.Width;

	D3D11_VIEWPORT VP;
	VP.TopLeftX = 0;
	VP.TopLeftY = 0;
	VP.Width = (FLOAT)width;
	VP.Height = (FLOAT)m_TexSrcVideo.desc.Height;
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;

	const UINT Stride = sizeof(VERTEX);
	const UINT Offset = 0;

	// Set resources
	m_pDeviceContext->IASetInputLayout(m_pVSimpleInputLayout);
	m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView.p, nullptr);
	m_pDeviceContext->RSSetViewports(1, &VP);
	m_pDeviceContext->OMSetBlendState(nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
	m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
	m_pDeviceContext->PSSetShader(m_pPSConvertColor, nullptr, 0);
	m_pDeviceContext->PSSetShaderResources(0, 1, &m_TexSrcVideo.pShaderResource.p);
	m_pDeviceContext->PSSetShaderResources(1, 1, &m_TexSrcVideo.pShaderResource2.p);
	m_pDeviceContext->PSSetShaderResources(2, 1, &m_TexSrcVideo.pShaderResource3.p);
	m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerPoint);
	m_pDeviceContext->PSSetSamplers(1, 1, (m_srcParams.Subsampling == 444) ? &m_pSamplerPoint : &m_pSamplerLinear);
	m_pDeviceContext->PSSetConstantBuffers(0, 1, &m_PSConvColorData.pConstants);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pFullFrameVertexBuffer, &Stride, &Offset);

	// Draw textured quad onto render target
	m_pDeviceContext->Draw(4, 0);

	ID3D11ShaderResourceView* views[3] = {};
	m_pDeviceContext->PSSetShaderResources(0, 3, views);

	return hr;
}

HRESULT CDX11VideoProcessor::ResizeShaderPass(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect)
{
	HRESULT hr = S_OK;
	const int w2 = dstRect.Width();
	const int h2 = dstRect.Height();
	const int k = m_bInterpolateAt50pct ? 2 : 1;

	int w1, h1;
	ID3D11PixelShader* resizerX;
	ID3D11PixelShader* resizerY;
	if (m_iRotation == 90 || m_iRotation == 270) {
		w1 = srcRect.Height();
		h1 = srcRect.Width();
		resizerX = (w1 == w2) ? nullptr : (w1 > k * w2) ? m_pShaderDownscaleY.p : m_pShaderUpscaleY.p; // use Y scaling here
		if (resizerX) {
			resizerY = (h1 == h2) ? nullptr : (h1 > k * h2) ? m_pShaderDownscaleY.p : m_pShaderUpscaleY.p;
		} else {
			resizerY = (h1 == h2) ? nullptr : (h1 > k * h2) ? m_pShaderDownscaleX.p : m_pShaderUpscaleX.p; // use X scaling here
		}
	} else {
		w1 = srcRect.Width();
		h1 = srcRect.Height();
		resizerX = (w1 == w2) ? nullptr : (w1 > k * w2) ? m_pShaderDownscaleX.p : m_pShaderUpscaleX.p;
		resizerY = (h1 == h2) ? nullptr : (h1 > k * h2) ? m_pShaderDownscaleY.p : m_pShaderUpscaleY.p;
	}

	if (resizerX && resizerY) {
		// two pass resize

		D3D11_TEXTURE2D_DESC desc;
		pRenderTarget->GetDesc(&desc);

		// check intermediate texture
		const UINT texWidth  = desc.Width;
		const UINT texHeight = h1;

		if (m_TexResize.pTexture) {
			if (texWidth != m_TexResize.desc.Width || texHeight != m_TexResize.desc.Height) {
				m_TexResize.Release(); // need new texture
			}
		}

		if (!m_TexResize.pTexture) {
			// use only float textures here
			hr = m_TexResize.Create(m_pDevice, DXGI_FORMAT_R16G16B16A16_FLOAT, texWidth, texHeight, Tex2D_DefaultShaderRTarget);
			if (FAILED(hr)) {
				DLog(L"CDX11VideoProcessor::ResizeShaderPass() : m_TexResize.Create() failed with error %s", HR2Str(hr));
				return hr;
			}
		}

		CRect resizeRect(dstRect.left, 0, dstRect.right, texHeight);

		// First resize pass
		hr = TextureResizeShader(Tex, m_TexResize.pTexture, srcRect, resizeRect, resizerX, m_iRotation);
		// Second resize pass
		hr = TextureResizeShader(m_TexResize, pRenderTarget, resizeRect, dstRect, resizerY, 0);
	}
	else {
		if (resizerX) {
			// one pass resize for width
			hr = TextureResizeShader(Tex, pRenderTarget, srcRect, dstRect, resizerX, m_iRotation);
		}
		else if (resizerY) {
			// one pass resize for height
			hr = TextureResizeShader(Tex, pRenderTarget, srcRect, dstRect, resizerY, m_iRotation);
		}
		else {
			// no resize
			hr = TextureCopyRect(Tex, pRenderTarget, srcRect, dstRect, m_pPS_Simple, nullptr, m_iRotation);
		}
	}

	DLogIf(FAILED(hr), L"CDX11VideoProcessor::ResizeShaderPass() : failed with error %s", HR2Str(hr));

	return hr;
}

HRESULT CDX11VideoProcessor::FinalPass(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect)
{
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;
	CComPtr<ID3D11Buffer> pVertexBuffer;
	CComPtr<ID3D11Buffer> pConstantBuffer;

	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (FAILED(hr)) {
		DLog(L"FinalPass() : CreateRenderTargetView() failed with error %s", HR2Str(hr));
		return hr;
	}

	hr = CreateVertexBuffer(m_pDevice, &pVertexBuffer, Tex.desc.Width, Tex.desc.Height, srcRect, 0);
	if (FAILED(hr)) {
		DLog(L"FinalPass() : Create vertex buffer failed with error %s", HR2Str(hr));
		return hr;
	}

	const FLOAT constants[4] = { (float)Tex.desc.Width / dither_size, (float)Tex.desc.Height / dither_size, 0, 0 };
	D3D11_BUFFER_DESC BufferDesc = {};
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(constants);
	BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData = { &constants, 0, 0 };

	hr = m_pDevice->CreateBuffer(&BufferDesc, &InitData, &pConstantBuffer);
	if (FAILED(hr)) {
		DLog(L"FinalPass() : Create constant buffer failed with error %s", HR2Str(hr));
		return hr;
	}

	D3D11_VIEWPORT VP;
	VP.TopLeftX = (FLOAT)dstRect.left;
	VP.TopLeftY = (FLOAT)dstRect.top;
	VP.Width    = (FLOAT)dstRect.Width();
	VP.Height   = (FLOAT)dstRect.Height();
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;

	// Set resources
	m_pDeviceContext->PSSetShaderResources(1, 1, &m_TexDither.pShaderResource.p);
	m_pDeviceContext->PSSetSamplers(1, 1, &m_pSamplerDither);

	TextureBlt11(m_pDeviceContext, pRenderTargetView, VP, m_pVSimpleInputLayout, m_pVS_Simple, m_pPSFinalPass, Tex.pShaderResource, m_pSamplerPoint, pConstantBuffer, pVertexBuffer);

	ID3D11ShaderResourceView* views[1] = {};
	m_pDeviceContext->PSSetShaderResources(1, 1, views);

	return hr;
}

HRESULT CDX11VideoProcessor::Process(ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect, const bool second)
{
	HRESULT hr = S_OK;

	CRect rSrc = srcRect;
	Tex2D_t* pInputTexture = nullptr;

	if (m_D3D11VP.IsReady()) {
		if (m_bVPScaling) {
			RECT rect = { 0, 0, m_TexD3D11VPOutput.desc.Width, m_TexD3D11VPOutput.desc.Height };
			hr = D3D11VPPass(m_TexD3D11VPOutput.pTexture, rSrc, rect, second);
			rSrc = rect;
		}
		else {
			hr = D3D11VPPass(m_TexD3D11VPOutput.pTexture, rSrc, rSrc, second);
		}

		pInputTexture = &m_TexD3D11VPOutput;
	}
	else if (m_PSConvColorData.bEnable) {
		ConvertColorPass(m_TexConvertOutput.pTexture);
		pInputTexture = &m_TexConvertOutput;
	}
	else {
		pInputTexture = &m_TexSrcVideo;
	}

	if (m_pPSCorrection || m_pPostScaleShaders.size() || m_bFinalPass) {
		Tex2D_t* Tex = m_TexsPostScale.GetFirstTex();
		CRect rect;
		rect.IntersectRect(dstRect, CRect(0, 0, Tex->desc.Width, Tex->desc.Height));

		hr = ResizeShaderPass(*pInputTexture, Tex->pTexture, rSrc, dstRect);

		ID3D11PixelShader* pPixelShader = nullptr;
		ID3D11Buffer* pConstantBuffer = nullptr;

		if (m_pPSCorrection) {
			pPixelShader = m_pPSCorrection;
		}

		if (m_pPostScaleShaders.size()) {
			if (m_pPSCorrection) {
				pInputTexture = Tex;
				Tex = m_TexsPostScale.GetNextTex();
				hr = TextureCopyRect(*pInputTexture, Tex->pTexture, rect, rect, pPixelShader, nullptr, 0);
			}

			if (m_pPostScaleShaders.size()) {
				static __int64 counter = 0;
				static long start = GetTickCount();

				long stop = GetTickCount();
				long diff = stop - start;
				if (diff >= 10 * 60 * 1000) {
					start = stop;    // reset after 10 min (ps float has its limits in both range and accuracy)
				}

				PS_EXTSHADER_CONSTANTS ConstData = {
					{1.0f / Tex->desc.Width, 1.0f / Tex->desc.Height },
					{(float)Tex->desc.Width, (float)Tex->desc.Height},
					counter++,
					(float)diff / 1000,
					0, 0
				};

				m_pDeviceContext->UpdateSubresource(m_pPostScaleConstants, 0, nullptr, &ConstData, 0, 0);

				for (UINT idx = 0; idx < m_pPostScaleShaders.size() - 1; idx++) {
					pInputTexture = Tex;
					Tex = m_TexsPostScale.GetNextTex();
					pPixelShader = m_pPostScaleShaders[idx].shader;
					hr = TextureCopyRect(*pInputTexture, Tex->pTexture, rect, rect, pPixelShader, m_pPostScaleConstants, 0);
				}
				pPixelShader = m_pPostScaleShaders.back().shader;
				pConstantBuffer = m_pPostScaleConstants;
			}
		}

		if (m_bFinalPass) {
			if (m_pPSCorrection || m_pPostScaleShaders.size()) {
				pInputTexture = Tex;
				Tex = m_TexsPostScale.GetNextTex();
				hr = TextureCopyRect(*pInputTexture, Tex->pTexture, rect, rect, pPixelShader, nullptr, 0);
			}

			hr = FinalPass(*Tex, pRenderTarget, rect, rect);
		}
		else {
			hr = TextureCopyRect(*Tex, pRenderTarget, rect, rect, pPixelShader, pConstantBuffer, 0);
		}
	}
	else {
		hr = ResizeShaderPass(*pInputTexture, pRenderTarget, rSrc, dstRect);
	}

	DLogIf(FAILED(hr), L"CDX9VideoProcessor::Process() : failed with error %s", HR2Str(hr));

	return hr;
}

void CDX11VideoProcessor::SetVideoRect(const CRect& videoRect)
{
	m_videoRect = videoRect;
	UpdateRenderRect();
	UpdateTexures(m_videoRect.Width(), m_videoRect.Height());
}

HRESULT CDX11VideoProcessor::SetWindowRect(const CRect& windowRect)
{
	m_windowRect = windowRect;
	UpdateRenderRect();

	HRESULT hr = S_OK;
	const UINT w = m_windowRect.Width();
	const UINT h = m_windowRect.Height();

	if (m_pDXGISwapChain1) {
		hr = m_pDXGISwapChain1->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
	}

	UpdatePostScaleTexures();

	return hr;
}

HRESULT CDX11VideoProcessor::GetVideoSize(long *pWidth, long *pHeight)
{
	CheckPointer(pWidth, E_POINTER);
	CheckPointer(pHeight, E_POINTER);

	if (m_iRotation == 90 || m_iRotation == 270) {
		*pWidth  = m_srcRectHeight;
		*pHeight = m_srcRectWidth;
	} else {
		*pWidth  = m_srcRectWidth;
		*pHeight = m_srcRectHeight;
	}

	return S_OK;
}

HRESULT CDX11VideoProcessor::GetAspectRatio(long *plAspectX, long *plAspectY)
{
	CheckPointer(plAspectX, E_POINTER);
	CheckPointer(plAspectY, E_POINTER);

	if (m_iRotation == 90 || m_iRotation == 270) {
		*plAspectX = m_srcAspectRatioY;
		*plAspectY = m_srcAspectRatioX;
	} else {
		*plAspectX = m_srcAspectRatioX;
		*plAspectY = m_srcAspectRatioY;
	}

	return S_OK;
}

HRESULT CDX11VideoProcessor::GetCurentImage(long *pDIBImage)
{
	CRect rSrcRect(m_srcRect);
	int w, h;
	if (m_iRotation == 90 || m_iRotation == 270) {
		w = rSrcRect.Height();
		h = rSrcRect.Width();
	} else {
		w = rSrcRect.Width();
		h = rSrcRect.Height();
	}
	CRect rDstRect(0, 0, w, h);

	BITMAPINFOHEADER* pBIH = (BITMAPINFOHEADER*)pDIBImage;
	memset(pBIH, 0, sizeof(BITMAPINFOHEADER));
	pBIH->biSize = sizeof(BITMAPINFOHEADER);
	pBIH->biWidth = w;
	pBIH->biHeight = h;
	pBIH->biPlanes = 1;
	pBIH->biBitCount = 32;
	pBIH->biSizeImage = DIBSIZE(*pBIH);

	UINT dst_pitch = pBIH->biSizeImage / h;

	HRESULT hr = S_OK;
	CComPtr<ID3D11Texture2D> pRGB32Texture2D;
	D3D11_TEXTURE2D_DESC texdesc = CreateTex2DDesc(DXGI_FORMAT_B8G8R8X8_UNORM, w, h, Tex2D_DefaultRTarget);

	hr = m_pDevice->CreateTexture2D(&texdesc, nullptr, &pRGB32Texture2D);
	if (FAILED(hr)) {
		return hr;
	}

	UpdateTexures(w, h);

	hr = Process(pRGB32Texture2D, rSrcRect, rDstRect, false);

	UpdateTexures(m_videoRect.Width(), m_videoRect.Height());

	if (FAILED(hr)) {
		return hr;
	}

	CComPtr<ID3D11Texture2D> pRGB32Texture2D_Shared;
	texdesc = CreateTex2DDesc(DXGI_FORMAT_B8G8R8X8_UNORM, w, h, Tex2D_StagingRead);

	hr = m_pDevice->CreateTexture2D(&texdesc, nullptr, &pRGB32Texture2D_Shared);
	if (FAILED(hr)) {
		return hr;
	}
	m_pDeviceContext->CopyResource(pRGB32Texture2D_Shared, pRGB32Texture2D);

	D3D11_MAPPED_SUBRESOURCE mr = {};
	if (S_OK == m_pDeviceContext->Map(pRGB32Texture2D_Shared, 0, D3D11_MAP_READ, 0, &mr)) {
		CopyFrameAsIs(h, (BYTE*)(pBIH + 1), dst_pitch, (BYTE*)mr.pData + mr.RowPitch * (h - 1), -(int)mr.RowPitch);
		m_pDeviceContext->Unmap(pRGB32Texture2D_Shared, 0);
	} else {
		return E_FAIL;
	}

	return S_OK;
}

HRESULT CDX11VideoProcessor::GetDisplayedImage(BYTE **ppDib, unsigned* pSize)
{
	if (!m_pDXGISwapChain1 || !m_pDevice || !m_pDeviceContext) {
		return E_ABORT;
	}

	CComPtr<ID3D11Texture2D> pBackBuffer;
	HRESULT hr = m_pDXGISwapChain1->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::GetDisplayedImage() failed with error %s", HR2Str(hr));
		return hr;
	}

	D3D11_TEXTURE2D_DESC desc;
	pBackBuffer->GetDesc(&desc);

	D3D11_TEXTURE2D_DESC desc2 = CreateTex2DDesc(DXGI_FORMAT_B8G8R8A8_UNORM, desc.Width, desc.Height, Tex2D_StagingRead);
	CComPtr<ID3D11Texture2D> pTexture2DShared;
	hr = m_pDevice->CreateTexture2D(&desc2, nullptr, &pTexture2DShared);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::GetDisplayedImage() failed with error %s", HR2Str(hr));
		return hr;
	}

	*pSize = desc.Width * desc.Height * 4 + sizeof(BITMAPINFOHEADER);
	BYTE* p = (BYTE*)LocalAlloc(LMEM_FIXED, *pSize); // only this allocator can be used
	if (!p) {
		return E_OUTOFMEMORY;
	}

	BITMAPINFOHEADER* pBIH = (BITMAPINFOHEADER*)p;
	ZeroMemory(pBIH, sizeof(BITMAPINFOHEADER));
	pBIH->biSize      = sizeof(BITMAPINFOHEADER);
	pBIH->biWidth     = desc.Width;
	pBIH->biHeight    = desc.Height;
	pBIH->biBitCount  = 32;
	pBIH->biPlanes    = 1;
	pBIH->biSizeImage = DIBSIZE(*pBIH);

	UINT dst_pitch = pBIH->biSizeImage / desc.Height;

	m_pDeviceContext->CopyResource(pTexture2DShared, pBackBuffer);

	D3D11_MAPPED_SUBRESOURCE mappedResource = {};
	hr = m_pDeviceContext->Map(pTexture2DShared, 0, D3D11_MAP_READ, 0, &mappedResource);
	if (SUCCEEDED(hr)) {
		CopyFrameAsIs(desc.Height, (BYTE*)(pBIH + 1), dst_pitch, (BYTE*)mappedResource.pData + mappedResource.RowPitch * (desc.Height - 1), -(int)mappedResource.RowPitch);
		m_pDeviceContext->Unmap(pTexture2DShared, 0);
		*ppDib = p;
	} else {
		LocalFree(p);
	}

	return hr;
}

HRESULT CDX11VideoProcessor::GetVPInfo(CStringW& str)
{
	str = L"DirectX 11";
	str.AppendFormat(L"\nGraphics adapter: %s", m_strAdapterDescription);
	str.Append(L"\nVideoProcessor  : ");
	if (m_D3D11VP.IsReady()) {
		D3D11_VIDEO_PROCESSOR_CAPS caps;
		UINT rateConvIndex;
		D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS rateConvCaps;
		m_D3D11VP.GetVPParams(caps, rateConvIndex, rateConvCaps);

		str.AppendFormat(L"D3D11, RateConversion_%u", rateConvIndex);

		str.Append(L"\nDeinterlaceTech.:");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BLEND)               str.Append(L" Blend,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BOB)                 str.Append(L" Bob,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_ADAPTIVE)            str.Append(L" Adaptive,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_MOTION_COMPENSATION) str.Append(L" Motion Compensation,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_INVERSE_TELECINE)                str.Append(L" Inverse Telecine,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_FRAME_RATE_CONVERSION)           str.Append(L" Frame Rate Conversion");
		str.TrimRight(',');
		str.AppendFormat(L"\nReference Frames: Past %u, Future %u", rateConvCaps.PastFrames, rateConvCaps.FutureFrames);
	} else {
		str.Append(L"Shaders");
	}

	str.AppendFormat(L"\nDisplay Mode    : %u x %u", m_DisplayMode.Width, m_DisplayMode.Height);
	if (m_dRefreshRate > 0.0) {
		str.AppendFormat(L", %.03f", m_dRefreshRate);
	} else {
		str.AppendFormat(L", %u", m_DisplayMode.RefreshRate);
	}
	if (m_DisplayMode.ScanLineOrdering == D3DSCANLINEORDERING_INTERLACED) {
		str.AppendChar('i');
	}
	str.Append(L" Hz");
	if (m_bPrimaryDisplay) {
		str.Append(L" [Primary]");
	}

	if (m_pPostScaleShaders.size()) {
		str.Append(L"\n\nPost scale pixel shaders:");
		for (const auto& pshader : m_pPostScaleShaders) {
			str.AppendFormat(L"\n  %s", pshader.name);
		}
	}

#ifdef _DEBUG
	str.Append(L"\n\nDEBUG info:");
	str.AppendFormat(L"\nSource rect   : %d,%d,%d,%d - %dx%d", m_srcRect.left, m_srcRect.top, m_srcRect.right, m_srcRect.bottom, m_srcRect.Width(), m_srcRect.Height());
	str.AppendFormat(L"\nVideo rect    : %d,%d,%d,%d - %dx%d", m_videoRect.left, m_videoRect.top, m_videoRect.right, m_videoRect.bottom, m_videoRect.Width(), m_videoRect.Height());
	str.AppendFormat(L"\nWindow rect   : %d,%d,%d,%d - %dx%d", m_windowRect.left, m_windowRect.top, m_windowRect.right, m_windowRect.bottom, m_windowRect.Width(), m_windowRect.Height());
#endif

	return S_OK;
}

void CDX11VideoProcessor::SetTexFormat(int value)
{
	switch (value) {
	case TEXFMT_AUTOINT:
	case TEXFMT_8INT:
	case TEXFMT_10INT:
	case TEXFMT_16FLOAT:
		m_iTexFormat = value;
		break;
	default:
		DLog(L"CDX11VideoProcessor::SetTexFormat() unknown value %d", value);
		ASSERT(FALSE);
		return;
	}
}

void CDX11VideoProcessor::SetVPEnableFmts(const VPEnableFormats_t& VPFormats)
{
	m_VPFormats = VPFormats;
}

void CDX11VideoProcessor::SetVPScaling(bool value)
{
	m_bVPScaling = value;
	UpdateTexures(m_videoRect.Width(), m_videoRect.Height());
}

void CDX11VideoProcessor::SetChromaScaling(int value)
{
	if (value < 0 || value >= CHROMA_COUNT) {
		DLog(L"CDX11VideoProcessor::SetChromaScaling() unknown value %d", value);
		ASSERT(FALSE);
		return;
	}
	m_iChromaScaling = value;

	if (m_pDevice) {
		EXECUTE_ASSERT(S_OK == UpdateChromaScalingShader());
		UpdateStatsStatic();
	}
}

void CDX11VideoProcessor::SetUpscaling(int value)
{
	if (value < 0 || value >= UPSCALE_COUNT) {
		DLog(L"CDX11VideoProcessor::SetUpscaling() unknown value %d", value);
		ASSERT(FALSE);
		return;
	}
	m_iUpscaling = value;

	if (m_pDevice) {
		UpdateUpscalingShaders();
	}
};

void CDX11VideoProcessor::SetDownscaling(int value)
{
	if (value < 0 || value >= DOWNSCALE_COUNT) {
		DLog(L"CDX11VideoProcessor::SetDownscaling() unknown value %d", value);
		ASSERT(FALSE);
		return;
	}
	m_iDownscaling = value;

	if (m_pDevice) {
		UpdateDownscalingShaders();
	}
};

void CDX11VideoProcessor::SetDither(bool value)
{
	m_bUseDither = value;
	UpdatePostScaleTexures();
}

void CDX11VideoProcessor::SetRotation(int value)
{
	m_iRotation = value;
	if (m_D3D11VP.IsReady()) {
		//m_D3D11VP.SetRotation(m_bVPScaling ? static_cast<D3D11_VIDEO_PROCESSOR_ROTATION>(value / 90) : D3D11_VIDEO_PROCESSOR_ROTATION_IDENTITY);
	}
}

void CDX11VideoProcessor::Flush()
{
	if (m_D3D11VP.IsReady()) {
		m_D3D11VP.ResetFrameOrder();
	}
}

void CDX11VideoProcessor::UpdateDiplayInfo()
{
	const HMONITOR hMon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
	const HMONITOR hMonPrimary = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY);

	MONITORINFOEXW mi = { sizeof(mi) };
	GetMonitorInfoW(hMon, (MONITORINFO*)&mi);
	m_dRefreshRate = GetRefreshRate(mi.szDevice);
	if (hMon == hMonPrimary) {
		m_bPrimaryDisplay = true;
		m_dRefreshRatePrimary = m_dRefreshRate;
	} else {
		m_bPrimaryDisplay = false;
		GetMonitorInfoW(hMonPrimary, (MONITORINFO*)&mi);
		m_dRefreshRatePrimary = GetRefreshRate(mi.szDevice);
	}
}

void CDX11VideoProcessor::ClearPostScaleShaders()
{
	for (auto& pScreenShader : m_pPostScaleShaders) {
		pScreenShader.shader.Release();
	}
	m_pPostScaleShaders.clear();
	UpdateStatsStatic3();
	DLog(L"CDX11VideoProcessor::ClearPostScaleShaders().");
}

HRESULT CDX11VideoProcessor::AddPostScaleShader(const CStringW& name, const CStringA& srcCode)
{
	HRESULT hr = S_OK;

	if (m_pDevice) {
		ID3DBlob* pShaderCode = nullptr;
		hr = CompileShader(srcCode, nullptr, "ps_4_0", &pShaderCode);
		if (S_OK == hr) {
			m_pPostScaleShaders.emplace_back();
			hr = m_pDevice->CreatePixelShader((const DWORD*)pShaderCode->GetBufferPointer(), pShaderCode->GetBufferSize(), nullptr, &m_pPostScaleShaders.back().shader);
			if (S_OK == hr) {
				m_pPostScaleShaders.back().name = name;
				UpdatePostScaleTexures();
				DLog(L"CDX11VideoProcessor::AddPostScaleShader() : \"%s\" pixel shader added successfully.", name);
			}
			else {
				DLog(L"CDX11VideoProcessor::AddPostScaleShader() : create pixel shader \"%s\" FAILED!", name);
				m_pPostScaleShaders.pop_back();
			}
			pShaderCode->Release();
		}
	}

	return hr;
}

void CDX11VideoProcessor::UpdateStatsStatic()
{
	if (m_srcParams.cformat) {
		m_strStatsStatic1.Format(L"MPC VR v%S, Direct3D 11", MPCVR_VERSION_STR);
		m_strStatsStatic1.AppendFormat(L"\nGraph. Adapter: %s", m_strAdapterDescription);

		m_strStatsStatic2.Format(L"%S %ux%u", m_srcParams.str, m_srcRectWidth, m_srcRectHeight);
		if (m_srcParams.CSType == CS_YUV) {
			LPCSTR strs[6] = {};
			GetExtendedFormatString(strs, m_srcExFmt, m_srcParams.CSType);
			m_strStatsStatic2.AppendFormat(L"\n  Range: %hS", strs[1]);
			if (m_decExFmt.NominalRange == DXVA2_NominalRange_Unknown) {
				m_strStatsStatic2.AppendChar('*');
			};
			m_strStatsStatic2.AppendFormat(L", Matrix: %hS", strs[2]);
			if (m_decExFmt.VideoTransferMatrix == DXVA2_VideoTransferMatrix_Unknown) {
				m_strStatsStatic2.AppendChar('*');
			};
			m_strStatsStatic2.AppendFormat(L", Lighting: %hS", strs[3]);
			if (m_decExFmt.VideoLighting == DXVA2_VideoLighting_Unknown) {
				m_strStatsStatic2.AppendChar('*');
			};
			m_strStatsStatic2.AppendFormat(L"\n  Primaries: %hS", strs[4]);
			if (m_decExFmt.VideoPrimaries == DXVA2_VideoPrimaries_Unknown) {
				m_strStatsStatic2.AppendChar('*');
			};
			m_strStatsStatic2.AppendFormat(L", Function: %hS", strs[5]);
			if (m_decExFmt.VideoTransferFunction == DXVA2_VideoTransFunc_Unknown) {
				m_strStatsStatic2.AppendChar('*');
			};
			if (m_srcParams.Subsampling == 420) {
				m_strStatsStatic2.AppendFormat(L"\n  ChromaLocation: %hS", strs[0]);
				if (m_decExFmt.VideoChromaSubsampling == DXVA2_VideoChromaSubsampling_Unknown) {
					m_strStatsStatic2.AppendChar('*');
				};
			}
		}
		m_strStatsStatic2.Append(L"\nVideoProcessor: ");
		if (m_D3D11VP.IsReady()) {
			m_strStatsStatic2.AppendFormat(L"D3D11 VP, output to %s", DXGIFormatToString(m_D3D11OutputFmt));
		} else {
			m_strStatsStatic2.Append(L"Shaders");
			if (m_srcParams.Subsampling == 420 || m_srcParams.Subsampling == 422) {
				m_strStatsStatic2.AppendFormat(L", Chroma Scaling: %s", (m_iChromaScaling == CHROMA_CatmullRom) ? L"Catmull-Rom" : L"Bilinear");
			}
		}
		m_strStatsStatic2.AppendFormat(L"\nInternalFormat: %s", DXGIFormatToString(m_InternalTexFmt));

		DXGI_SWAP_CHAIN_DESC1 swapchain_desc;
		if (m_pDXGISwapChain1 && S_OK == m_pDXGISwapChain1->GetDesc1(&swapchain_desc)) {
			m_strStatsStatic4.SetString(L"\nPresentation  : ");
			switch (swapchain_desc.SwapEffect) {
			case DXGI_SWAP_EFFECT_DISCARD:
				m_strStatsStatic4.Append(L"Discard");
				break;
			case DXGI_SWAP_EFFECT_SEQUENTIAL:
				m_strStatsStatic4.Append(L"Sequential");
				break;
			case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
				m_strStatsStatic4.Append(L"Flip sequential");
				break;
#if VER_PRODUCTBUILD >= 10000
			case DXGI_SWAP_EFFECT_FLIP_DISCARD:
				m_strStatsStatic4.Append(L"Flip discard");
				break;
#endif
			}
		}
	} else {
		m_strStatsStatic1 = L"Error";
		m_strStatsStatic2.Empty();
		m_strStatsStatic3.Empty();
		m_strStatsStatic4.Empty();
	}
}

void CDX11VideoProcessor::UpdateStatsStatic3()
{
	if (m_strCorrection || m_pPostScaleShaders.size() || m_bFinalPass) {
		m_strStatsStatic3 = L"\nPostProcessing:";
		if (m_strCorrection) {
			m_strStatsStatic3.AppendFormat(L" %s,", m_strCorrection);
		}
		if (m_pPostScaleShaders.size()) {
			m_strStatsStatic3.AppendFormat(L" shaders[%u],", (UINT)m_pPostScaleShaders.size());
		}
		if (m_bFinalPass) {
			m_strStatsStatic3.Append(L" dither");
		}
		m_strStatsStatic3.TrimRight(',');
	}
	else {
		m_strStatsStatic3.Empty();
	}
}

HRESULT CDX11VideoProcessor::DrawStats(ID3D11Texture2D* pRenderTarget)
{
	if (m_windowRect.IsRectEmpty()) {
		return E_ABORT;
	}

	CStringW str = m_strStatsStatic1;
	str.AppendFormat(L"\nFrame rate    : %7.03f", m_pFilter->m_FrameStats.GetAverageFps());
	if (m_SampleFormat != D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE) {
		str.AppendChar(L'i');
	}
	str.AppendFormat(L",%7.03f", m_pFilter->m_DrawStats.GetAverageFps());
	str.Append(L"\nInput format  :");
	if (m_iSrcFromGPU == 11) {
		str.Append(L" D3D11_");
	}
	else if (m_iSrcFromGPU == 9) {
		str.Append(L" DXVA2_");
	}
	str.Append(m_strStatsStatic2);

	const int srcW = m_srcRect.Width();
	const int srcH = m_srcRect.Height();
	const int dstW = m_videoRect.Width();
	const int dstH = m_videoRect.Height();
	if (m_iRotation) {
		str.AppendFormat(L"\nScaling       : %dx%d r%d°> %dx%d", srcW, srcH, m_iRotation, dstW, dstH);
	} else {
		str.AppendFormat(L"\nScaling       : %dx%d -> %dx%d", srcW, srcH, dstW, dstH);
	}
	if (srcW != dstW || srcH != dstH) {
		if (m_D3D11VP.IsReady() && m_bVPScaling) {
			str.Append(L" D3D11");
		} else {
			if (m_strShaderX) {
				str.AppendFormat(L" %s", m_strShaderX);
				if (m_strShaderY && m_strShaderY != m_strShaderX) {
					str.AppendFormat(L"/%s", m_strShaderY);
				}
			} else if (m_strShaderY) {
				str.AppendFormat(L" %s", m_strShaderY);
			}
		}
	}

	str.Append(m_strStatsStatic3);
	str.Append(m_strStatsStatic4);

	str.AppendFormat(L"\nFrames: %5u, skipped: %u/%u, failed: %u",
		m_pFilter->m_FrameStats.GetFrames(), m_pFilter->m_DrawStats.m_dropped, m_RenderStats.dropped2, m_RenderStats.failed);
	str.AppendFormat(L"\nTimes(ms): Copy%3llu, Paint%3llu [DX9Subs%3llu], Present%3llu",
		m_RenderStats.copyticks    * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.paintticks   * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.substicks    * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.presentticks * 1000 / GetPreciseTicksPerSecondI());
#if 0
	str.AppendFormat(L"\n1:%6.03f, 2:%6.03f, 3:%6.03f, 4:%6.03f, 5:%6.03f, 6:%6.03f ms",
		m_RenderStats.t1 * 1000 / GetPreciseTicksPerSecond(),
		m_RenderStats.t2 * 1000 / GetPreciseTicksPerSecond(),
		m_RenderStats.t3 * 1000 / GetPreciseTicksPerSecond(),
		m_RenderStats.t4 * 1000 / GetPreciseTicksPerSecond(),
		m_RenderStats.t5 * 1000 / GetPreciseTicksPerSecond(),
		m_RenderStats.t6 * 1000 / GetPreciseTicksPerSecond());
#else
	str.AppendFormat(L"\nSync offset   : %+3lld ms", (m_RenderStats.syncoffset + 5000) / 10000);
#endif

	ID3D11RenderTargetView* pRenderTargetView = nullptr;
	HRESULT hr = m_pDevice->CreateRenderTargetView(m_TexStats.pTexture, nullptr, &pRenderTargetView);
	if (S_OK == hr) {
		const FLOAT ClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.75f };
		m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ClearColor);

		const SIZE rtSize{ m_TexStats.desc.Width, m_TexStats.desc.Height };
		hr = m_Font3D.Draw2DText(pRenderTargetView, rtSize, 5, 5, D3DCOLOR_XRGB(255, 255, 255), str);
		static int col = STATS_W;
		if (--col < 0) {
			col = STATS_W;
		}
		m_Rect3D.Set({ col, STATS_H - 11, col + 5, STATS_H - 1 }, rtSize, D3DCOLOR_XRGB(128, 255, 128));
		m_Rect3D.Draw(pRenderTargetView, rtSize);

		pRenderTargetView->Release();

		D3D11_VIEWPORT VP;
		VP.TopLeftX = STATS_X;
		VP.TopLeftY = STATS_Y;
		VP.Width    = STATS_W;
		VP.Height   = STATS_H;
		VP.MinDepth = 0.0f;
		VP.MaxDepth = 1.0f;
		hr = AlphaBlt(m_TexStats.pShaderResource, pRenderTarget, VP);
	}

	return hr;
}

// IUnknown
STDMETHODIMP CDX11VideoProcessor::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv) {
		return E_POINTER;
	}
	if (riid == IID_IUnknown) {
		*ppv = static_cast<IUnknown*>(static_cast<IMFVideoProcessor*>(this));
	}
	else if (riid == IID_IMFVideoProcessor) {
		*ppv = static_cast<IMFVideoProcessor*>(this);
	}
	else {
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	AddRef();
	return S_OK;
}

STDMETHODIMP_(ULONG) CDX11VideoProcessor::AddRef()
{
	return InterlockedIncrement(&m_nRefCount);
}

STDMETHODIMP_(ULONG) CDX11VideoProcessor::Release()
{
	ULONG uCount = InterlockedDecrement(&m_nRefCount);
	if (uCount == 0) {
		delete this;
	}
	// For thread safety, return a temporary variable.
	return uCount;
}

// IMFVideoProcessor

STDMETHODIMP CDX11VideoProcessor::GetProcAmpRange(DWORD dwProperty, DXVA2_ValueRange *pPropRange)
{
	CheckPointer(pPropRange, E_POINTER);
	if (m_srcParams.cformat == CF_NONE) {
		return MF_E_TRANSFORM_TYPE_NOT_SET;
	}

	switch (dwProperty) {
	case DXVA2_ProcAmp_Brightness: *pPropRange = m_DXVA2ProcAmpRanges[0]; break;
	case DXVA2_ProcAmp_Contrast:   *pPropRange = m_DXVA2ProcAmpRanges[1]; break;
	case DXVA2_ProcAmp_Hue:        *pPropRange = m_DXVA2ProcAmpRanges[2]; break;
	case DXVA2_ProcAmp_Saturation: *pPropRange = m_DXVA2ProcAmpRanges[3]; break;
	default:
		return E_INVALIDARG;
	}

	return S_OK;
}

STDMETHODIMP CDX11VideoProcessor::GetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *Values)
{
	CheckPointer(Values, E_POINTER);
	if (m_srcParams.cformat == CF_NONE) {
		return MF_E_TRANSFORM_TYPE_NOT_SET;
	}

	if (dwFlags&DXVA2_ProcAmp_Brightness) { Values->Brightness = m_DXVA2ProcAmpValues.Brightness; }
	if (dwFlags&DXVA2_ProcAmp_Contrast)   { Values->Contrast   = m_DXVA2ProcAmpValues.Contrast  ; }
	if (dwFlags&DXVA2_ProcAmp_Hue)        { Values->Hue        = m_DXVA2ProcAmpValues.Hue       ; }
	if (dwFlags&DXVA2_ProcAmp_Saturation) { Values->Saturation = m_DXVA2ProcAmpValues.Saturation; }

	return S_OK;
}

STDMETHODIMP CDX11VideoProcessor::SetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *pValues)
{
	CheckPointer(pValues, E_POINTER);
	if (m_srcParams.cformat == CF_NONE) {
		return MF_E_TRANSFORM_TYPE_NOT_SET;
	}

	if (dwFlags&DXVA2_ProcAmp_Brightness) {
		m_DXVA2ProcAmpValues.Brightness.ll = std::clamp(pValues->Brightness.ll, m_DXVA2ProcAmpRanges[0].MinValue.ll, m_DXVA2ProcAmpRanges[0].MaxValue.ll);
	}
	if (dwFlags&DXVA2_ProcAmp_Contrast) {
		m_DXVA2ProcAmpValues.Contrast.ll = std::clamp(pValues->Contrast.ll, m_DXVA2ProcAmpRanges[1].MinValue.ll, m_DXVA2ProcAmpRanges[1].MaxValue.ll);
	}
	if (dwFlags&DXVA2_ProcAmp_Hue) {
		m_DXVA2ProcAmpValues.Hue.ll = std::clamp(pValues->Hue.ll, m_DXVA2ProcAmpRanges[2].MinValue.ll, m_DXVA2ProcAmpRanges[2].MaxValue.ll);
	}
	if (dwFlags&DXVA2_ProcAmp_Saturation) {
		m_DXVA2ProcAmpValues.Saturation.ll = std::clamp(pValues->Saturation.ll, m_DXVA2ProcAmpRanges[3].MinValue.ll, m_DXVA2ProcAmpRanges[3].MaxValue.ll);
	}

	if (dwFlags&DXVA2_ProcAmp_Mask) {
		CAutoLock cRendererLock(&m_pFilter->m_RendererLock);

		m_D3D11VP.SetProcAmpValues(&m_DXVA2ProcAmpValues);

		if (!m_D3D11VP.IsReady()) {
			SetShaderConvertColorParams();
		}
	}

	return S_OK;
}

STDMETHODIMP CDX11VideoProcessor::GetBackgroundColor(COLORREF *lpClrBkg)
{
	CheckPointer(lpClrBkg, E_POINTER);
	*lpClrBkg = RGB(0, 0, 0);
	return S_OK;
}
