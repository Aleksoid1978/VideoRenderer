/*
* (C) 2018-2022 see Authors.txt
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
#include <mfapi.h> // for MR_BUFFER_SERVICE
#include <Mferror.h>
#include <Mfidl.h>
#include <dwmapi.h>
#include "Helper.h"
#include "Times.h"
#include "resource.h"
#include "VideoRenderer.h"
#include "../Include/Version.h"
#include "DX11VideoProcessor.h"
#include "../Include/ID3DVideoMemoryConfiguration.h"
#include "Shaders.h"
#include "Utils/CPUInfo.h"

#include "../external/minhook/include/MinHook.h"

bool g_bPresent = false;
bool bCreateSwapChain = false;

typedef BOOL(WINAPI* pSetWindowPos)(
	_In_ HWND hWnd,
	_In_opt_ HWND hWndInsertAfter,
	_In_ int X,
	_In_ int Y,
	_In_ int cx,
	_In_ int cy,
	_In_ UINT uFlags);

pSetWindowPos pOrigSetWindowPosDX11 = nullptr;
static BOOL WINAPI pNewSetWindowPosDX11(
	_In_ HWND hWnd,
	_In_opt_ HWND hWndInsertAfter,
	_In_ int X,
	_In_ int Y,
	_In_ int cx,
	_In_ int cy,
	_In_ UINT uFlags)
{
	if (g_bPresent) {
		DLog(L"call SetWindowPos() function during Present()");
		uFlags |= SWP_ASYNCWINDOWPOS;
	}
	return pOrigSetWindowPosDX11(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

typedef LONG(WINAPI* pSetWindowLongA)(
	_In_ HWND hWnd,
	_In_ int nIndex,
	_In_ LONG dwNewLong);

pSetWindowLongA pOrigSetWindowLongADX11 = nullptr;
static LONG WINAPI pNewSetWindowLongADX11(
	_In_ HWND hWnd,
	_In_ int nIndex,
	_In_ LONG dwNewLong)
{
	if (bCreateSwapChain) {
		DLog(L"Blocking call SetWindowLongA() function during create fullscreen swap chain");
		return 0L;
	}
	return pOrigSetWindowLongADX11(hWnd, nIndex, dwNewLong);
}

template <typename T>
inline bool HookFunc(T** ppSystemFunction, PVOID pHookFunction)
{
	return MH_CreateHook(*ppSystemFunction, pHookFunction, reinterpret_cast<LPVOID*>(ppSystemFunction)) == MH_OK;
}

static const ScalingShaderResId s_Upscaling11ResIDs[UPSCALE_COUNT] = {
	{0,                            0,                            L"Nearest-neighbor"  },
	{IDF_PSH11_INTERP_MITCHELL4_X, IDF_PSH11_INTERP_MITCHELL4_Y, L"Mitchell-Netravali"},
	{IDF_PSH11_INTERP_CATMULL4_X,  IDF_PSH11_INTERP_CATMULL4_Y,  L"Catmull-Rom"       },
	{IDF_PSH11_INTERP_LANCZOS2_X,  IDF_PSH11_INTERP_LANCZOS2_Y,  L"Lanczos2"          },
	{IDF_PSH11_INTERP_LANCZOS3_X,  IDF_PSH11_INTERP_LANCZOS3_Y,  L"Lanczos3"          },
	{IDF_PSH11_INTERP_JINC2,       IDF_PSH11_INTERP_JINC2,       L"Jinc2*"            },
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

HRESULT CreateVertexBuffer(ID3D11Device* pDevice, ID3D11Buffer** ppVertexBuffer,
	const UINT srcW, const UINT srcH, const RECT& srcRect,
	const int iRotation, const bool bFlip)
{
	ASSERT(ppVertexBuffer);
	ASSERT(*ppVertexBuffer == nullptr);

	const float src_dx = 1.0f / srcW;
	const float src_dy = 1.0f / srcH;
	float src_l = src_dx * srcRect.left;
	float src_r = src_dx * srcRect.right;
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

	if (bFlip) {
		std::swap(src_l, src_r);
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
	DLogIf(FAILED(hr), L"CreateVertexBuffer() : CreateBuffer() failed with error {}", HR2Str(hr));

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

HRESULT CDX11VideoProcessor::AlphaBlt(
	ID3D11ShaderResourceView* pShaderResource,
	ID3D11Texture2D* pRenderTarget,
	ID3D11Buffer* pVertexBuffer,
	D3D11_VIEWPORT* pViewPort,
	ID3D11SamplerState* pSampler)
{
	ID3D11RenderTargetView* pRenderTargetView;
	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);

	if (S_OK == hr) {
		UINT Stride = sizeof(VERTEX);
		UINT Offset = 0;

		// Set resources
		m_pDeviceContext->IASetInputLayout(m_pVSimpleInputLayout);
		m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
		m_pDeviceContext->RSSetViewports(1, pViewPort);
		m_pDeviceContext->OMSetBlendState(m_pAlphaBlendState, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
		m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
		m_pDeviceContext->PSSetShader(m_pPS_Simple, nullptr, 0);
		m_pDeviceContext->PSSetShaderResources(0, 1, &pShaderResource);
		m_pDeviceContext->PSSetSamplers(0, 1, &pSampler);
		m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		m_pDeviceContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &Stride, &Offset);

		// Draw textured quad onto render target
		m_pDeviceContext->Draw(4, 0);

		pRenderTargetView->Release();
	}
	DLogIf(FAILED(hr), L"AlphaBlt() : CreateRenderTargetView() failed with error {}", HR2Str(hr));

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
		hr = CreateVertexBuffer(m_pDevice, &pVertexBuffer, m_d3dpp.BackBufferWidth, m_d3dpp.BackBufferHeight, srcRect, 0, false);

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
	DLogIf(FAILED(hr), L"CDX11VideoProcessor:AlphaBlt() : CreateRenderTargetView() failed with error {}", HR2Str(hr));

	return hr;
}

HRESULT CDX11VideoProcessor::TextureCopyRect(
	const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget,
	const CRect& srcRect, const CRect& destRect,
	ID3D11PixelShader* pPixelShader, ID3D11Buffer* pConstantBuffer,
	const int iRotation, const bool bFlip)
{
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;
	CComPtr<ID3D11Buffer> pVertexBuffer;

	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (FAILED(hr)) {
		DLog(L"TextureCopyRect() : CreateRenderTargetView() failed with error {}", HR2Str(hr));
		return hr;
	}

	hr = CreateVertexBuffer(m_pDevice, &pVertexBuffer, Tex.desc.Width, Tex.desc.Height, srcRect, iRotation, bFlip);
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

HRESULT CDX11VideoProcessor::TextureResizeShader(
	const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget,
	const CRect& srcRect, const CRect& dstRect,
	ID3D11PixelShader* pPixelShader,
	const int iRotation, const bool bFlip)
{
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;
	CComPtr<ID3D11Buffer> pVertexBuffer;
	CComPtr<ID3D11Buffer> pConstantBuffer;

	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (FAILED(hr)) {
		DLog(L"TextureResizeShader() : CreateRenderTargetView() failed with error {}", HR2Str(hr));
		return hr;
	}

	hr = CreateVertexBuffer(m_pDevice, &pVertexBuffer, Tex.desc.Width, Tex.desc.Height, srcRect, iRotation, bFlip);
	if (FAILED(hr)) {
		return hr;
	}

	const FLOAT constants[][4] = {
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
		DLog(L"TextureResizeShader() : Create constant buffer failed with error {}", HR2Str(hr));
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

CDX11VideoProcessor::CDX11VideoProcessor(CMpcVideoRenderer* pFilter, const Settings_t& config, HRESULT& hr)
	: CVideoProcessor(pFilter)
{
	m_bShowStats                      = config.bShowStats;
	m_iResizeStats                    = config.iResizeStats;
	m_iTexFormat                      = config.iTexFormat;
	m_VPFormats                       = config.VPFmts;
	m_bDeintDouble                    = config.bDeintDouble;
	m_bVPScaling                      = config.bVPScaling;
	m_iChromaScaling                  = config.iChromaScaling;
	m_iUpscaling                      = config.iUpscaling;
	m_iDownscaling                    = config.iDownscaling;
	m_bInterpolateAt50pct             = config.bInterpolateAt50pct;
	m_bUseDither                      = config.bUseDither;
	m_iSwapEffect                     = config.iSwapEffect;
	m_bVBlankBeforePresent            = config.bVBlankBeforePresent;
	m_bHdrPassthrough                 = config.bHdrPassthrough;
	m_iHdrToggleDisplay               = config.iHdrToggleDisplay;
	m_bConvertToSdr                   = config.bConvertToSdr;
	m_bD3D11Subtitle                  = config.bD3D11Subtitle;
	m_nCurrentAdapter = -1;
	m_pDisplayMode = &m_DisplayMode;

	hr = CreateDXGIFactory1(IID_IDXGIFactory1, (void**)&m_pDXGIFactory1);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::CDX11VideoProcessor() : CreateDXGIFactory1() failed with error {}", HR2Str(hr));
		return;
	}

	// set default ProcAmp ranges and values
	SetDefaultDXVA2ProcAmpRanges(m_DXVA2ProcAmpRanges);
	SetDefaultDXVA2ProcAmpValues(m_DXVA2ProcAmpValues);

	pOrigSetWindowPosDX11 = SetWindowPos;
	auto ret = HookFunc(&pOrigSetWindowPosDX11, pNewSetWindowPosDX11);
	DLogIf(!ret, L"CDX11VideoProcessor::CDX11VideoProcessor() : hook for SetWindowPos() fail");

	pOrigSetWindowLongADX11 = SetWindowLongA;
	ret = HookFunc(&pOrigSetWindowLongADX11, pNewSetWindowLongADX11);
	DLogIf(!ret, L"CDX11VideoProcessor::CDX11VideoProcessor() : hook for SetWindowLongA() fail");

	MH_EnableHook(MH_ALL_HOOKS);

	CComPtr<IDXGIAdapter> pDXGIAdapter;
	for (UINT adapter = 0; m_pDXGIFactory1->EnumAdapters(adapter, &pDXGIAdapter) != DXGI_ERROR_NOT_FOUND; ++adapter) {
		CComPtr<IDXGIOutput> pDXGIOutput;
		for (UINT output = 0; pDXGIAdapter->EnumOutputs(output, &pDXGIOutput) != DXGI_ERROR_NOT_FOUND; ++output) {
			DXGI_OUTPUT_DESC desc{};
			if (SUCCEEDED(pDXGIOutput->GetDesc(&desc))) {
				DisplayConfig_t displayConfig = {};
				if (GetDisplayConfig(desc.DeviceName, displayConfig)) {
					m_hdrModeStartState[desc.DeviceName] = displayConfig.advancedColor.advancedColorEnabled;
				}
			}

			pDXGIOutput.Release();
		}

		pDXGIAdapter.Release();
	}
}

static bool ToggleHDR(const DisplayConfig_t& displayConfig, const BOOL bEnableAdvancedColor)
{
	DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setColorState = {};
	setColorState.header.type         = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
	setColorState.header.size         = sizeof(setColorState);
	setColorState.header.adapterId    = displayConfig.modeTarget.adapterId;
	setColorState.header.id           = displayConfig.modeTarget.id;
	setColorState.enableAdvancedColor = bEnableAdvancedColor;

	const auto ret = DisplayConfigSetDeviceInfo(&setColorState.header);
	DLogIf(ERROR_SUCCESS != ret, L"ToggleHDR() : DisplayConfigSetDeviceInfo({}) failed with error {}", bEnableAdvancedColor, HR2Str(HRESULT_FROM_WIN32(ret)));

	return ret == ERROR_SUCCESS;
}

CDX11VideoProcessor::~CDX11VideoProcessor()
{
	for (const auto& [displayName, state] : m_hdrModeSavedState) {
		DisplayConfig_t displayConfig = {};
		if (GetDisplayConfig(displayName.c_str(), displayConfig)) {
			const auto& ac = displayConfig.advancedColor;

			if (ac.advancedColorSupported && ac.advancedColorEnabled != state) {
				const auto ret = ToggleHDR(displayConfig, state);
				DLogIf(!ret, L"CDX11VideoProcessor::~CDX11VideoProcessor() : Toggle HDR {} for '{}' failed", state ? L"ON" : L"OFF", displayName);
			}
		}
	}

	ReleaseSwapChain();
	m_pDXGIFactory2.Release();

	ReleaseDevice();

	m_pDXGIFactory1.Release();

	MH_RemoveHook(SetWindowPos);
	MH_RemoveHook(SetWindowLongA);
}

HRESULT CDX11VideoProcessor::Init(const HWND hwnd, bool* pChangeDevice/* = nullptr*/)
{
	DLog(L"CDX11VideoProcessor::Init()");

	const bool bWindowChanged = (m_hWnd != hwnd);
	m_hWnd = hwnd;
	m_bHdrPassthroughSupport = false;
	m_bHdrDisplayModeEnabled = false;
	m_bitsPerChannelSupport = 8;

	MONITORINFOEXW mi = { sizeof(mi) };
	GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO*)&mi);
	DisplayConfig_t displayConfig = {};

	if (GetDisplayConfig(mi.szDevice, displayConfig)) {
		const auto& ac = displayConfig.advancedColor;
		m_bHdrPassthroughSupport = ac.advancedColorSupported && ac.advancedColorEnabled;
		m_bHdrDisplayModeEnabled = ac.advancedColorEnabled;
		m_bitsPerChannelSupport = displayConfig.bitsPerChannel;
	}

	if (m_bIsFullscreen != m_pFilter->m_bIsFullscreen) {
		m_srcVideoTransferFunction = 0;
	}

	IDXGIAdapter* pDXGIAdapter = nullptr;
	const UINT currentAdapter = GetAdapter(hwnd, m_pDXGIFactory1, &pDXGIAdapter);
	CheckPointer(pDXGIAdapter, E_FAIL);
	if (m_nCurrentAdapter == currentAdapter) {
		SAFE_RELEASE(pDXGIAdapter);
		if (hwnd) {
			HRESULT hr = InitDX9Device(hwnd, pChangeDevice);
			ASSERT(S_OK == hr);
			if (m_pD3DDevEx) {
				// set a special blend mode for alpha channels for ISubRenderCallback rendering
				// this is necessary for the second alpha blending
				m_pD3DDevEx->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
				m_pD3DDevEx->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
				m_pD3DDevEx->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO);
				m_pD3DDevEx->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);

				SetCallbackDevice(pChangeDevice ? *pChangeDevice : false);
			}
		}

		if (!m_pDXGISwapChain1 || m_bIsFullscreen != m_pFilter->m_bIsFullscreen || bWindowChanged) {
			InitSwapChain();
			UpdateStatsStatic();
			m_pFilter->OnDisplayModeChange();
		}

		return S_OK;
	}
	m_nCurrentAdapter = currentAdapter;

	if (m_bDecoderDevice && m_pDXGISwapChain1) {
		return S_OK;
	}

	ReleaseSwapChain();
	m_pDXGIFactory2.Release();
	ReleaseDevice();

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
	};
	D3D_FEATURE_LEVEL featurelevel;

	ID3D11Device *pDevice = nullptr;

	HRESULT hr = D3D11CreateDevice(
		pDXGIAdapter,
		D3D_DRIVER_TYPE_UNKNOWN,
		nullptr,
#ifdef _DEBUG
		D3D11_CREATE_DEVICE_DEBUG,
#else
		0,
#endif
		featureLevels,
		std::size(featureLevels),
		D3D11_SDK_VERSION,
		&pDevice,
		&featurelevel,
		nullptr);
#ifdef _DEBUG
	if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING || (hr == E_FAIL && !IsWindows8OrGreater())) {
		DLog(L"WARNING: D3D11 debugging messages will not be displayed");
		hr = D3D11CreateDevice(
			pDXGIAdapter,
			D3D_DRIVER_TYPE_UNKNOWN,
			nullptr,
			0,
			featureLevels,
			std::size(featureLevels),
			D3D11_SDK_VERSION,
			&pDevice,
			&featurelevel,
			nullptr);
	}
#endif
	
	SAFE_RELEASE(pDXGIAdapter);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::Init() : D3D11CreateDevice() failed with error {}", HR2Str(hr));
		return hr;
	}

	DLog(L"CDX11VideoProcessor::Init() : D3D11CreateDevice() successfully with feature level {}.{}", (featurelevel >> 12), (featurelevel >> 8) & 0xF);

	hr = SetDevice(pDevice, nullptr, false);
	pDevice->Release();

	if (S_OK == hr) {
		if (pChangeDevice) {
			*pChangeDevice = true;
		}
	}

	if (m_VendorId == PCIV_INTEL && CPUInfo::HaveSSE41()) {
		m_pCopyGpuFn = CopyGpuFrame_SSE41;
	} else {
		m_pCopyGpuFn = CopyFrameAsIs;
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
	m_TexConvertOutput.Release();
	m_TexResize.Release();
	m_TexsPostScale.Release();

	m_PSConvColorData.Release();

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

	m_StatsBackground.InvalidateDeviceObjects();
	m_Font3D.InvalidateDeviceObjects();
	m_Rect3D.InvalidateDeviceObjects();

	m_Underlay.InvalidateDeviceObjects();
	m_Lines.InvalidateDeviceObjects();
	m_SyncLine.InvalidateDeviceObjects();

	m_TexDither.Release();
	m_bAlphaBitmapEnable = false;
	m_pAlphaBitmapVertex.Release();
	m_TexAlphaBitmap.Release();

	ClearPreScaleShaders();
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

#if TEST_SHADER
	m_pPS_TEST.Release();
#endif

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
	m_bCallbackDeviceIsSet = false;

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

void CDX11VideoProcessor::ReleaseSwapChain()
{
	if (m_pDXGISwapChain1) {
		m_pDXGISwapChain1->SetFullscreenState(FALSE, nullptr);
	}
	m_pDXGIOutput.Release();
	m_pDXGISwapChain4.Release();
	m_pDXGISwapChain1.Release();
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

	csp_params.input_bits = csp_params.texture_bits = m_srcParams.CDepth;

	m_PSConvColorData.bEnable =
		m_srcParams.CSType == CS_YUV ||
		m_srcParams.cformat == CF_GBRP8 || m_srcParams.cformat == CF_GBRP16 ||
		csp_params.gray ||
		fabs(csp_params.brightness) > 1e-4f || fabs(csp_params.contrast - 1.0f) > 1e-4f;

	mp_cmat cmatrix;
	mp_get_csp_matrix(&csp_params, &cmatrix);

	PS_COLOR_TRANSFORM cbuffer = {
		{cmatrix.m[0][0], cmatrix.m[0][1], cmatrix.m[0][2], 0},
		{cmatrix.m[1][0], cmatrix.m[1][1], cmatrix.m[1][2], 0},
		{cmatrix.m[2][0], cmatrix.m[2][1], cmatrix.m[2][2], 0},
		{cmatrix.c[0],    cmatrix.c[1],    cmatrix.c[2],    0},
	};

	if (m_srcParams.cformat == CF_AYUV) {
		std::swap(cbuffer.cm_r.x, cbuffer.cm_r.z);
		std::swap(cbuffer.cm_g.x, cbuffer.cm_g.z);
		std::swap(cbuffer.cm_b.x, cbuffer.cm_b.z);
	}
	else if (m_srcParams.cformat == CF_Y410 || m_srcParams.cformat == CF_Y416) {
		std::swap(cbuffer.cm_r.x, cbuffer.cm_r.y);
		std::swap(cbuffer.cm_g.x, cbuffer.cm_g.y);
		std::swap(cbuffer.cm_b.x, cbuffer.cm_b.y);
	}
	else if (m_srcParams.cformat == CF_GBRP8 || m_srcParams.cformat == CF_GBRP16) {
		std::swap(cbuffer.cm_r.x, cbuffer.cm_r.y); std::swap(cbuffer.cm_r.y, cbuffer.cm_r.z);
		std::swap(cbuffer.cm_g.x, cbuffer.cm_g.y); std::swap(cbuffer.cm_g.y, cbuffer.cm_g.z);
		std::swap(cbuffer.cm_b.x, cbuffer.cm_b.y); std::swap(cbuffer.cm_b.y, cbuffer.cm_b.z);
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

void CDX11VideoProcessor::UpdateTexParams(int cdepth)
{
	switch (m_iTexFormat) {
	case TEXFMT_AUTOINT:
		m_InternalTexFmt = (cdepth > 8) ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
		break;
	case TEXFMT_8INT:    m_InternalTexFmt = DXGI_FORMAT_B8G8R8A8_UNORM;     break;
	case TEXFMT_10INT:   m_InternalTexFmt = DXGI_FORMAT_R10G10B10A2_UNORM;  break;
	case TEXFMT_16FLOAT: m_InternalTexFmt = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
	default:
		ASSERT(FALSE);
	}
}

void CDX11VideoProcessor::UpdateRenderRect()
{
	m_renderRect.IntersectRect(m_videoRect, m_windowRect);

	const int w2 = m_videoRect.Width();
	const int h2 = m_videoRect.Height();
	const int k = m_bInterpolateAt50pct ? 2 : 1;
	int w1, h1;
	if (m_iRotation == 90 || m_iRotation == 270) {
		w1 = m_srcRectHeight;
		h1 = m_srcRectWidth;
	} else {
		w1 = m_srcRectWidth;
		h1 = m_srcRectHeight;
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

void CDX11VideoProcessor::SetGraphSize()
{
	if (m_pDeviceContext && !m_windowRect.IsRectEmpty()) {
		SIZE rtSize = m_windowRect.Size();

		CalcStatsFont();
		if (S_OK == m_Font3D.CreateFontBitmap(L"Consolas", m_StatsFontH, 0)) {
			SIZE charSize = m_Font3D.GetMaxCharMetric();
			m_StatsRect.right  = m_StatsRect.left + 60 * charSize.cx + 5 + 3;
			m_StatsRect.bottom = m_StatsRect.top + 18 * charSize.cy + 5 + 3;
		}
		m_StatsBackground.Set(m_StatsRect, rtSize, D3DCOLOR_ARGB(80, 0, 0, 0));

		CalcGraphParams();
		m_Underlay.Set(m_GraphRect, rtSize, D3DCOLOR_ARGB(80, 0, 0, 0));

		m_Lines.ClearPoints(rtSize);
		POINT points[2];
		const int linestep = 20 * m_Yscale;
		for (int y = m_GraphRect.top + (m_Yaxis - m_GraphRect.top) % (linestep); y < m_GraphRect.bottom; y += linestep) {
			points[0] = { m_GraphRect.left,  y };
			points[1] = { m_GraphRect.right, y };
			m_Lines.AddPoints(points, std::size(points), (y == m_Yaxis) ? D3DCOLOR_XRGB(150, 150, 255) : D3DCOLOR_XRGB(100, 100, 255));
		}
		m_Lines.UpdateVertexBuffer();
	}
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
				const int cromaPitch = (m_TexSrcVideo.pTexture3) ? srcPitch / m_srcParams.pDX11Planes->div_chroma_w : srcPitch;
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

HRESULT CDX11VideoProcessor::SetDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext, const bool bDecoderDevice)
{
	DLog(L"CDX11VideoProcessor::SetDevice()");

	ReleaseSwapChain();
	m_pDXGIFactory2.Release();
	ReleaseDevice();

	CheckPointer(pDevice, E_POINTER);

	HRESULT hr = pDevice->QueryInterface(IID_PPV_ARGS(&m_pDevice));
	if (FAILED(hr)) {
		return hr;
	}
	if (pContext) {
		hr = pContext->QueryInterface(IID_PPV_ARGS(&m_pDeviceContext));
		if (FAILED(hr)) {
			return hr;
		}
	} else {
		m_pDevice->GetImmediateContext1(&m_pDeviceContext);
	}
	CComQIPtr<ID3D10Multithread> p_mt(m_pDeviceContext);

	//for d3d11 subtitles
	BOOL multi = p_mt->SetMultithreadProtected(true);
	hr = m_D3D11VP.InitVideoDevice(m_pDevice, m_pDeviceContext);
	DLogIf(FAILED(hr), L"CDX11VideoProcessor::SetDevice() : InitVideoDevice failed with error {}", HR2Str(hr));

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

	EXECUTE_ASSERT(S_OK == CreateVertexBuffer(m_pDevice, &m_pFullFrameVertexBuffer, 1, 1, CRect(0, 0, 1, 1), 0, false));

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

#if TEST_SHADER
	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPS_TEST, IDF_PSH11_TEST));
#endif

	D3D11_BUFFER_DESC BufferDesc = {};
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(PS_EXTSHADER_CONSTANTS);
	BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateBuffer(&BufferDesc, nullptr, &m_pPostScaleConstants));

	CComPtr<IDXGIDevice> pDXGIDevice;
	hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDevice));
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::SetDevice() : QueryInterface(IDXGIDevice) failed with error {}", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}

	CComPtr<IDXGIAdapter> pDXGIAdapter;
	hr = pDXGIDevice->GetAdapter(&pDXGIAdapter);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::SetDevice() : GetAdapter(IDXGIAdapter) failed with error {}", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}

	CComPtr<IDXGIFactory1> pDXGIFactory1;
	hr = pDXGIAdapter->GetParent(IID_PPV_ARGS(&pDXGIFactory1));
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::SetDevice() : GetParent(IDXGIFactory1) failed with error {}", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}

	hr = pDXGIFactory1->QueryInterface(IID_PPV_ARGS(&m_pDXGIFactory2));
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::SetDevice() : QueryInterface(IDXGIFactory2) failed with error {}", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}

	if (m_pFilter->m_inputMT.IsValid()) {
		if (!InitMediaType(&m_pFilter->m_inputMT)) {
			ReleaseDevice();
			return E_FAIL;
		}
	}

	bool changeDevice = false;
	hr = InitDX9Device(m_hWnd, &changeDevice);
	ASSERT(S_OK == hr);
	if (m_pD3DDevEx) {
		// set a special blend mode for alpha channels for ISubRenderCallback rendering
		// this is necessary for the second alpha blending
		m_pD3DDevEx->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
		m_pD3DDevEx->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
		m_pD3DDevEx->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO);
		m_pD3DDevEx->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);

		SetCallbackDevice(changeDevice);
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
		m_strAdapterDescription = fmt::format(L"{} ({:04X}:{:04X})", dxgiAdapterDesc.Description, dxgiAdapterDesc.VendorId, dxgiAdapterDesc.DeviceId);
		DLog(L"Graphics DXGI adapter: {}", m_strAdapterDescription);
	}

	HRESULT hr2 = m_Font3D.InitDeviceObjects(m_pDevice, m_pDeviceContext);
	DLogIf(FAILED(hr2), L"m_Font3D.InitDeviceObjects() failed with error {}", HR2Str(hr2));
	if (SUCCEEDED(hr2)) {
		hr2 = m_StatsBackground.InitDeviceObjects(m_pDevice, m_pDeviceContext);
		hr2 = m_Rect3D.InitDeviceObjects(m_pDevice, m_pDeviceContext);
		hr2 = m_Underlay.InitDeviceObjects(m_pDevice, m_pDeviceContext);
		hr2 = m_Lines.InitDeviceObjects(m_pDevice, m_pDeviceContext);
		hr2 = m_SyncLine.InitDeviceObjects(m_pDevice, m_pDeviceContext);
		DLogIf(FAILED(hr2), L"Geometric primitives InitDeviceObjects() failed with error {}", HR2Str(hr2));
	}
	ASSERT(S_OK == hr2);

	HRESULT hr3 = m_TexDither.Create(m_pDevice, DXGI_FORMAT_R16G16B16A16_FLOAT, dither_size, dither_size, Tex2D_DynamicShaderWrite);
	if (S_OK == hr3) {
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
		if (FAILED(hr3)) {
			m_TexDither.Release();
		}
	}

	m_bDecoderDevice = bDecoderDevice;

	m_pFilter->OnDisplayModeChange();
	UpdateStatsStatic();
	SetGraphSize();

	return hr;
}

HRESULT CDX11VideoProcessor::InitSwapChain()
{
	DLog(L"CDX11VideoProcessor::InitSwapChain() - {}", m_pFilter->m_bIsFullscreen ? L"fullscreen" : L"window");
	CheckPointer(m_pDXGIFactory2, E_FAIL);

	ReleaseSwapChain();

	auto bFullscreenChange = m_bIsFullscreen != m_pFilter->m_bIsFullscreen;
	m_bIsFullscreen = m_pFilter->m_bIsFullscreen;

	if (bFullscreenChange) {
		HandleHDRToggle();
	}

	const auto bHdrOutput = m_bHdrPassthroughSupport && m_bHdrPassthrough && SourceIsHDR();
	const auto b10BitOutput = bHdrOutput || Preferred10BitOutput();
	m_SwapChainFmt = b10BitOutput ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
	m_dwStatsTextColor = (m_bHdrDisplayModeEnabled && bHdrOutput) ? D3DCOLOR_XRGB(170, 170, 170) : D3DCOLOR_XRGB(255, 255, 255);

	HRESULT hr = S_OK;
	DXGI_SWAP_CHAIN_DESC1 desc1 = {};

	if (m_bIsFullscreen) {
		MONITORINFOEXW mi = { sizeof(mi) };
		GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &mi);
		const CRect rc(mi.rcMonitor);

		desc1.Width = rc.Width();
		desc1.Height = rc.Height();
		desc1.Format = m_SwapChainFmt;
		desc1.SampleDesc.Count = 1;
		desc1.SampleDesc.Quality = 0;
		desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		if((m_iSwapEffect == SWAPEFFECT_Flip && IsWindows8OrGreater()) || bHdrOutput) {
			desc1.BufferCount = bHdrOutput ? 6 : 2;
			desc1.Scaling = DXGI_SCALING_NONE;
			desc1.SwapEffect = IsWindows10OrGreater() ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		} else { // default SWAPEFFECT_Discard
			desc1.BufferCount = 1;
			desc1.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		}
		desc1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

		DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc = {};
		fullscreenDesc.RefreshRate.Numerator = 0;
		fullscreenDesc.RefreshRate.Denominator = 1;
		fullscreenDesc.Windowed = FALSE;

		bCreateSwapChain = true;
		hr = m_pDXGIFactory2->CreateSwapChainForHwnd(m_pDevice, m_hWnd, &desc1, &fullscreenDesc, nullptr, &m_pDXGISwapChain1);
		bCreateSwapChain = false;
		DLogIf(FAILED(hr), L"CDX11VideoProcessor::InitSwapChain() : CreateSwapChainForHwnd(fullscreen) failed with error {}", HR2Str(hr));
	}
	else {
		desc1.Width = std::max(8, m_windowRect.Width());
		desc1.Height = std::max(8, m_windowRect.Height());
		desc1.Format = m_SwapChainFmt;
		desc1.SampleDesc.Count = 1;
		desc1.SampleDesc.Quality = 0;
		desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		if ((m_iSwapEffect == SWAPEFFECT_Flip && IsWindows8OrGreater()) || bHdrOutput) {
			desc1.BufferCount = bHdrOutput ? 6 : 2;
			desc1.Scaling = DXGI_SCALING_NONE;
			desc1.SwapEffect = IsWindows10OrGreater() ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		} else { // default SWAPEFFECT_Discard
			desc1.BufferCount = 1;
			desc1.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		}
		desc1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

		DLogIf(m_windowRect.Width() < 8 || m_windowRect.Height() < 8,
			L"CDX11VideoProcessor::InitSwapChain() : Invalid window size {}x{}, use {}x{}",
			m_windowRect.Width(), m_windowRect.Height(), desc1.Width, desc1.Height);

		hr = m_pDXGIFactory2->CreateSwapChainForHwnd(m_pDevice, m_hWnd, &desc1, nullptr, nullptr, &m_pDXGISwapChain1);
		DLogIf(FAILED(hr), L"CDX11VideoProcessor::InitSwapChain() : CreateSwapChainForHwnd() failed with error {}", HR2Str(hr));
	}

	if (m_pDXGISwapChain1) {
		m_UsedSwapEffect = desc1.SwapEffect;

		HRESULT hr2 = m_pDXGISwapChain1->GetContainingOutput(&m_pDXGIOutput);

		m_currentSwapChainColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
		if (bHdrOutput) {
			hr2 = m_pDXGISwapChain1->QueryInterface(IID_PPV_ARGS(&m_pDXGISwapChain4));
		}

		m_pShaderResourceSubPic.Release();
		m_pTextureSubPic.Release();

		m_pSurface9SubPic.Release();

		if (m_bD3D11Subtitle) {
			D3D11_TEXTURE2D_DESC bufferDesc;
			bufferDesc.ArraySize = 1;
			bufferDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			bufferDesc.CPUAccessFlags = 0;
			bufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			bufferDesc.Height = m_d3dpp.BackBufferHeight;
			bufferDesc.MipLevels = 1;
			bufferDesc.MiscFlags = 0;
			bufferDesc.SampleDesc = { 1, 0 };
			bufferDesc.Usage = D3D11_USAGE_DEFAULT;
			bufferDesc.Width = m_d3dpp.BackBufferWidth;
			HRESULT hr = m_pDevice->CreateTexture2D(&bufferDesc, 0, &m_pTextureSubPic);

			//Creating a view of the texture to be used when binding it as a render target
			D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
			renderTargetViewDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			renderTargetViewDesc.Texture2D.MipSlice = 0;
			DLogIf(FAILED(hr), L"CDX11VideoProcessor::InitSwapChain() : CreateRenderTargetView failed for subic with error {}", HR2Str(hr));

			//Creating a view of the texture to be used when binding it on a shader to sample
			D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
			shaderResourceViewDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
			shaderResourceViewDesc.Texture2D.MipLevels = 1;
			hr = m_pDevice->CreateShaderResourceView(m_pTextureSubPic, &shaderResourceViewDesc, &m_pShaderResourceSubPic);
			DLogIf(FAILED(hr), L"CDX11VideoProcessor::InitSwapChain() : CreateShaderResourceView() failed for subpic with error {}", HR2Str(hr));
		}
		if (m_pD3DDevEx && !m_bD3D11Subtitle) {
			HANDLE sharedHandle = nullptr;
			hr2 = m_pD3DDevEx->CreateRenderTarget(
				m_d3dpp.BackBufferWidth,
				m_d3dpp.BackBufferHeight,
				D3DFMT_A8R8G8B8,
				D3DMULTISAMPLE_NONE,
				0,
				FALSE,
				&m_pSurface9SubPic,
				&sharedHandle);
			DLogIf(FAILED(hr2), L"CDX11VideoProcessor::InitSwapChain() : CreateRenderTarget(Direct3D9) failed with error {}", HR2Str(hr2));

			if (m_pSurface9SubPic) {
				hr2 = m_pDevice->OpenSharedResource(sharedHandle, IID_PPV_ARGS(&m_pTextureSubPic));
				DLogIf(FAILED(hr2), L"CDX11VideoProcessor::InitSwapChain() : OpenSharedResource() failed with error {}", HR2Str(hr2));
			}

			if (m_pTextureSubPic) {
				D3D11_TEXTURE2D_DESC texdesc = {};
				m_pTextureSubPic->GetDesc(&texdesc);
				if (texdesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
					D3D11_SHADER_RESOURCE_VIEW_DESC shaderDesc;
					shaderDesc.Format = texdesc.Format;
					shaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
					shaderDesc.Texture2D.MostDetailedMip = 0; // = Texture2D desc.MipLevels - 1
					shaderDesc.Texture2D.MipLevels = 1;       // = Texture2D desc.MipLevels

					hr2 = m_pDevice->CreateShaderResourceView(m_pTextureSubPic, &shaderDesc, &m_pShaderResourceSubPic);
					DLogIf(FAILED(hr2), L"CDX11VideoProcessor::InitSwapChain() : CreateShaderResourceView() failed with error {}", HR2Str(hr2));
				}

				if (m_pShaderResourceSubPic) {
					hr2 = m_pD3DDevEx->ColorFill(m_pSurface9SubPic, nullptr, D3DCOLOR_ARGB(255, 0, 0, 0));
					hr2 = m_pD3DDevEx->SetRenderTarget(0, m_pSurface9SubPic);
					DLogIf(FAILED(hr2), L"CDX11VideoProcessor::InitSwapChain() : SetRenderTarget(Direct3D9) failed with error {}", HR2Str(hr2));
				}
			}
		}
	}

	return hr;
}

BOOL CDX11VideoProcessor::VerifyMediaType(const CMediaType* pmt)
{
	const auto& FmtParams = GetFmtConvParams(pmt);
	if (FmtParams.VP11Format == DXGI_FORMAT_UNKNOWN && FmtParams.DX11Format == DXGI_FORMAT_UNKNOWN) {
		return FALSE;
	}

	const BITMAPINFOHEADER* pBIH = GetBIHfromVIHs(pmt);
	if (!pBIH) {
		return FALSE;
	}

	if (pBIH->biWidth <= 0 || !pBIH->biHeight || (!pBIH->biSizeImage && pBIH->biCompression != BI_RGB)) {
		return FALSE;
	}

	if (FmtParams.Subsampling == 420 && ((pBIH->biWidth & 1) || (pBIH->biHeight & 1))) {
		return FALSE;
	}
	if (FmtParams.Subsampling == 422 && (pBIH->biWidth & 1)) {
		return FALSE;
	}

	return TRUE;
}

bool CDX11VideoProcessor::HandleHDRToggle()
{
	m_bHdrDisplaySwitching = true;
	bool bRet = false;
	if (m_bHdrPassthrough && SourceIsHDR()) {
		MONITORINFOEXW mi = { sizeof(mi) };
		GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO*)&mi);
		DisplayConfig_t displayConfig = {};

		if (GetDisplayConfig(mi.szDevice, displayConfig)) {
			const auto& ac = displayConfig.advancedColor;

			if (ac.advancedColorSupported && m_iHdrToggleDisplay) {
				BOOL bHDREnabled = FALSE;
				const auto& it = m_hdrModeStartState.find(mi.szDevice);
				if (it != m_hdrModeStartState.cend()) {
					bHDREnabled = it->second;
				}

				const bool bNeedToggleOn  = !ac.advancedColorEnabled &&
											(m_iHdrToggleDisplay == HDRTD_Always || m_iHdrToggleDisplay == HDRTD_Fullscreen && m_bIsFullscreen);
				const bool bNeedToggleOff = ac.advancedColorEnabled &&
											!bHDREnabled && m_iHdrToggleDisplay == HDRTD_Fullscreen && !m_bIsFullscreen;
				DLog(L"HandleHDRToggle() : %d, %d", bNeedToggleOn, bNeedToggleOff);
				if (bNeedToggleOn) {
					bRet = ToggleHDR(displayConfig, TRUE);
					DLogIf(!bRet, L"CDX11VideoProcessor::HandleHDRToggle() : Toggle HDR ON failed");

					if (bRet) {
						std::wstring deviceName(mi.szDevice);
						const auto& it = m_hdrModeSavedState.find(deviceName);
						if (it == m_hdrModeSavedState.cend()) {
							m_hdrModeSavedState[std::move(deviceName)] = FALSE;
						}
					}
				} else if (bNeedToggleOff) {
					bRet = ToggleHDR(displayConfig, FALSE);
					DLogIf(!bRet, L"CDX11VideoProcessor::HandleHDRToggle() : Toggle HDR OFF failed");

					if (bRet) {
						std::wstring deviceName(mi.szDevice);
						const auto& it = m_hdrModeSavedState.find(deviceName);
						if (it == m_hdrModeSavedState.cend()) {
							m_hdrModeSavedState[std::move(deviceName)] = TRUE;
						}
					}
				}
			}
		}
	} else if (m_iHdrToggleDisplay) {
		MONITORINFOEXW mi = { sizeof(mi) };
		GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO*)&mi);
		DisplayConfig_t displayConfig = {};

		if (GetDisplayConfig(mi.szDevice, displayConfig)) {
			const auto& ac = displayConfig.advancedColor;

			if (ac.advancedColorSupported && ac.advancedColorEnabled &&
					(m_iHdrToggleDisplay == HDRTD_Always || m_iHdrToggleDisplay == HDRTD_Fullscreen && m_bIsFullscreen)) {
				bRet = ToggleHDR(displayConfig, FALSE);
				DLogIf(!bRet, L"CDX11VideoProcessor::HandleHDRToggle() : Toggle HDR OFF failed");

				if (bRet) {
					std::wstring deviceName(mi.szDevice);
					const auto& it = m_hdrModeSavedState.find(deviceName);
					if (it == m_hdrModeSavedState.cend()) {
						m_hdrModeSavedState[std::move(deviceName)] = TRUE;
					}
				}
			}
		}
	}
	m_bHdrDisplaySwitching = false;

	if (bRet) {
		MONITORINFOEXW mi = { sizeof(mi) };
		GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO*)&mi);
		DisplayConfig_t displayConfig = {};

		if (GetDisplayConfig(mi.szDevice, displayConfig)) {
			const auto& ac = displayConfig.advancedColor;
			m_bHdrPassthroughSupport = ac.advancedColorSupported && ac.advancedColorEnabled;
			m_bHdrDisplayModeEnabled = ac.advancedColorEnabled;
			m_bitsPerChannelSupport = displayConfig.bitsPerChannel;
		}
	}

	return bRet;
}

BOOL CDX11VideoProcessor::InitMediaType(const CMediaType* pmt)
{
	DLog(L"CDX11VideoProcessor::InitMediaType()");

	if (!VerifyMediaType(pmt)) {
		return FALSE;
	}

	ReleaseVP();

	auto FmtParams = GetFmtConvParams(pmt);
	bool disableD3D11VP = false;
	switch (FmtParams.cformat) {
	case CF_NV12: disableD3D11VP = !m_VPFormats.bNV12; break;
	case CF_P010:
	case CF_P016: disableD3D11VP = !m_VPFormats.bP01x;  break;
	case CF_YUY2: disableD3D11VP = !m_VPFormats.bYUY2;  break;
	default:      disableD3D11VP = !m_VPFormats.bOther; break;
	}
	if (disableD3D11VP) {
		FmtParams.VP11Format = DXGI_FORMAT_UNKNOWN;
	}

	const BITMAPINFOHEADER* pBIH = nullptr;
	m_decExFmt.value = 0;

	if (pmt->formattype == FORMAT_VideoInfo2) {
		const VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pmt->pbFormat;
		pBIH = &vih2->bmiHeader;
		m_srcRect = vih2->rcSource;
		m_srcAspectRatioX = vih2->dwPictAspectRatioX;
		m_srcAspectRatioY = vih2->dwPictAspectRatioY;
		if (FmtParams.CSType == CS_YUV && (vih2->dwControlFlags & (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT))) {
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

	m_pFilter->m_FrameStats.SetStartFrameDuration(m_rtAvgTimePerFrame);

	UINT biWidth  = pBIH->biWidth;
	UINT biHeight = labs(pBIH->biHeight);
	UINT biSizeImage = pBIH->biSizeImage;
	if (pBIH->biSizeImage == 0 && pBIH->biCompression == BI_RGB) { // biSizeImage may be zero for BI_RGB bitmaps
		biSizeImage = biWidth * biHeight * pBIH->biBitCount / 8;
	}

	m_srcLines = biHeight * FmtParams.PitchCoeff / 2;
	m_srcPitch = biWidth * FmtParams.Packsize;
	switch (FmtParams.cformat) {
	case CF_Y8:
	case CF_NV12:
	case CF_RGB24:
	case CF_BGR48:
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

	m_srcExFmt = SpecifyExtendedFormat(m_decExFmt, FmtParams, m_srcRectWidth, m_srcRectHeight);

	const auto frm_gcd = std::gcd(m_srcRectWidth, m_srcRectHeight);
	const auto srcFrameARX = m_srcRectWidth / frm_gcd;
	const auto srcFrameARY = m_srcRectHeight / frm_gcd;

	if (!m_srcAspectRatioX || !m_srcAspectRatioY) {
		m_srcAspectRatioX = srcFrameARX;
		m_srcAspectRatioY = srcFrameARY;
		m_srcAnamorphic = false;
	}
	else {
		const auto ar_gcd = std::gcd(m_srcAspectRatioX, m_srcAspectRatioY);
		m_srcAspectRatioX /= ar_gcd;
		m_srcAspectRatioY /= ar_gcd;
		m_srcAnamorphic = (srcFrameARX != m_srcAspectRatioX || srcFrameARY != m_srcAspectRatioY);
	}

	UpdateUpscalingShaders();
	UpdateDownscalingShaders();

	m_pPSCorrection.Release();
	m_pPSConvertColor.Release();
	m_PSConvColorData.bEnable = false;

	UpdateTexParams(FmtParams.CDepth);

	if (m_bHdrAllowSwitchDisplay && m_srcVideoTransferFunction != m_srcExFmt.VideoTransferFunction) {
		auto ret = HandleHDRToggle();
		if (!ret && (m_bHdrPassthrough && m_bHdrPassthroughSupport && SourceIsHDR() && !m_pDXGISwapChain4)) {
			ret = true;
		}
		if (ret) {
			ReleaseSwapChain();
			Init(m_hWnd);
		}
	}

	if (Preferred10BitOutput() && m_SwapChainFmt == DXGI_FORMAT_B8G8R8A8_UNORM) {
		ReleaseSwapChain();
		Init(m_hWnd);
	}

	m_srcVideoTransferFunction = m_srcExFmt.VideoTransferFunction;

	// D3D11 Video Processor
	if (FmtParams.VP11Format != DXGI_FORMAT_UNKNOWN && !(m_VendorId == PCIV_NVIDIA && FmtParams.CSType == CS_RGB)) {
		// D3D11 VP does not work correctly if RGB32 with odd frame width (source or target) on Nvidia adapters

		if (S_OK == InitializeD3D11VP(FmtParams, origW, origH)) {
			bool bTransFunc22 = m_srcExFmt.VideoTransferFunction == DXVA2_VideoTransFunc_22
								|| m_srcExFmt.VideoTransferFunction == DXVA2_VideoTransFunc_709
								|| m_srcExFmt.VideoTransferFunction == DXVA2_VideoTransFunc_240M;

			if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084 && !(m_bHdrPassthroughSupport && m_bHdrPassthrough) && m_bConvertToSdr) {
				EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_CONVERT_PQ_TO_SDR));
				m_strCorrection = L"PQ to SDR";
			}
			else if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG) {
				if (m_bHdrPassthroughSupport && m_bHdrPassthrough) {
					EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_CONVERT_HLG_TO_PQ));
					m_strCorrection = L"HLG to PQ";
				}
				else if (m_bConvertToSdr) {
					EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_CONVERT_HLG_TO_SDR));
					m_strCorrection = L"HLG to SDR";
				}
				else if (m_srcExFmt.VideoPrimaries == VIDEOPRIMARIES_BT2020) {
					// HLG compatible with SDR
					EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_FIX_BT2020));
					m_strCorrection = L"Fix BT.2020";
				}
			}
			else if (m_srcExFmt.VideoTransferMatrix == VIDEOTRANSFERMATRIX_YCgCo) {
				EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_FIX_YCGCO));
				m_strCorrection = L"Fix YCoCg";
			}
			else if (bTransFunc22 && m_srcExFmt.VideoPrimaries == VIDEOPRIMARIES_BT2020) {
				EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_FIX_BT2020));
				m_strCorrection = L"Fix BT.2020";
			}

			DLogIf(m_pPSCorrection, L"CDX11VideoProcessor::InitMediaType() m_pPSCorrection created");

			m_pFilter->m_inputMT = *pmt;
			UpdateTexures();
			UpdatePostScaleTexures();
			UpdateStatsStatic();
			SetCallbackDevice();

			return TRUE;
		}

		ReleaseVP();
	}

	// Tex Video Processor
	if (FmtParams.DX11Format != DXGI_FORMAT_UNKNOWN && S_OK == InitializeTexVP(FmtParams, origW, origH)) {
		m_pFilter->m_inputMT = *pmt;
		SetShaderConvertColorParams();
		UpdateTexures();
		UpdatePostScaleTexures();
		UpdateStatsStatic();
		SetCallbackDevice();

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

	DLog(L"CDX11VideoProcessor::InitializeD3D11VP() started with input surface: {}, {} x {}", DXGIFormatToString(dxgiFormat), width, height);

	m_TexSrcVideo.Release();

	m_D3D11OutputFmt = m_InternalTexFmt;
	HRESULT hr = m_D3D11VP.InitVideoProcessor(dxgiFormat, width, height, m_bInterlaced, m_D3D11OutputFmt);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : InitVideoProcessor() failed with error {}", HR2Str(hr));
		return hr;
	}

	hr = m_D3D11VP.InitInputTextures(m_pDevice);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : InitInputTextures() failed with error {}", HR2Str(hr));
		return hr;
	}

	hr = m_D3D11VP.SetColorSpace(m_srcExFmt, m_bHdrDisplayModeEnabled && SourceIsHDR());

	hr = m_TexSrcVideo.Create(m_pDevice, dxgiFormat, width, height, Tex2D_DynamicShaderWriteNoSRV);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : m_TexSrcVideo.Create() failed with error {}", HR2Str(hr));
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

	DLog(L"CDX11VideoProcessor::InitializeTexVP() started with input surface: {}, {} x {}", DXGIFormatToString(srcDXGIFormat), width, height);

	HRESULT hr = m_TexSrcVideo.CreateEx(m_pDevice, srcDXGIFormat, params.pDX11Planes, width, height, Tex2D_DynamicShaderWrite);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeTexVP() : m_TexSrcVideo.CreateEx() failed with error {}", HR2Str(hr));
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

	HRESULT hr2 = UpdateConvertColorShader();
	if (FAILED(hr2)) {
		ASSERT(0);
		UINT resid = 0;
		if (params.cformat == CF_YUY2) {
			resid = IDF_PSH11_CONVERT_YUY2;
		}
		else if (params.pDX11Planes) {
			if (params.pDX11Planes->FmtPlane3) {
				if (params.cformat == CF_YV12 || params.cformat == CF_YV16 || params.cformat == CF_YV24) {
					resid = IDF_PSH11_CONVERT_PLANAR_YV;
				} else {
					resid = IDF_PSH11_CONVERT_PLANAR;
				}
			} else {
				resid = IDF_PSH11_CONVERT_BIPLANAR;
			}
		}
		else {
			resid = IDF_PSH11_CONVERT_COLOR;
		}
		EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSConvertColor, resid));
	}

	SAFE_RELEASE(m_PSConvColorData.pVertexBuffer);
	EXECUTE_ASSERT(S_OK == CreateVertexBuffer(m_pDevice, &m_PSConvColorData.pVertexBuffer, m_srcWidth, m_srcHeight, m_srcRect, 0, false));

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
		const auto& FmtParams = GetFmtConvParams(&mt);

		if (FmtParams.cformat == CF_RGB24) {
			Size.cx = ALIGN(Size.cx, 4);
		}
		else if (FmtParams.cformat == CF_RGB48 || FmtParams.cformat == CF_BGR48) {
			Size.cx = ALIGN(Size.cx, 2);
		}
		else if (FmtParams.cformat == CF_BGRA64 || FmtParams.cformat == CF_B64A) {
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

HRESULT CDX11VideoProcessor::ProcessSample(IMediaSample* pSample)
{
	REFERENCE_TIME rtStart, rtEnd;
	if (FAILED(pSample->GetTime(&rtStart, &rtEnd))) {
		rtStart = m_pFilter->m_FrameStats.GeTimestamp();
	}
	const REFERENCE_TIME rtFrameDur = m_pFilter->m_FrameStats.GetAverageFrameDuration();
	rtEnd = rtStart + rtFrameDur;

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

	int so = (int)std::clamp(m_RenderStats.syncoffset, -UNITS, UNITS);
#if SYNC_OFFSET_EX
	m_SyncDevs.Add(so - m_Syncs.Last());
#endif
	m_Syncs.Add(so);

	if (m_bDoubleFrames) {
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

		so = (int)std::clamp(m_RenderStats.syncoffset, -UNITS, UNITS);
#if SYNC_OFFSET_EX
		m_SyncDevs.Add(so - m_Syncs.Last());
#endif
		m_Syncs.Add(so);
	}

	return hr;
}

HRESULT CDX11VideoProcessor::CopySample(IMediaSample* pSample)
{
	CheckPointer(m_pDXGISwapChain1, E_FAIL);

	uint64_t tick = GetPreciseTick();

	// Get frame type
	m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE; // Progressive
	m_bDoubleFrames = false;
	if (m_bInterlaced) {
		if (CComQIPtr<IMediaSample2> pMS2 = pSample) {
			AM_SAMPLE2_PROPERTIES props;
			if (SUCCEEDED(pMS2->GetProperties(sizeof(props), (BYTE*)&props))) {
				if ((props.dwTypeSpecificFlags & AM_VIDEO_FLAG_WEAVE) == 0) {
					if (props.dwTypeSpecificFlags & AM_VIDEO_FLAG_FIELD1FIRST) {
						m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST; // Top-field first
					} else {
						m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST; // Bottom-field first
					}
					m_bDoubleFrames = m_bDeintDouble && m_D3D11VP.IsReady();
				}
			}
		}
	}

	HRESULT hr = S_OK;
	m_FieldDrawn = 0;
	bool updateStats = false;

	m_hdr10 = {};
	if (m_bHdrPassthrough && SourceIsHDR()) {
		if (CComQIPtr<IMediaSideData> pMediaSideData = pSample) {
			MediaSideDataHDR* hdr = nullptr;
			size_t size = 0;
			hr = pMediaSideData->GetSideData(IID_MediaSideDataHDR, (const BYTE**)&hdr, &size);
			if (SUCCEEDED(hr) && size == sizeof(MediaSideDataHDR)) {
				m_hdr10.bValid = true;

				m_hdr10.hdr10.RedPrimary[0]   = static_cast<UINT16>(std::lround(hdr->display_primaries_x[2] * 50000.0));
				m_hdr10.hdr10.RedPrimary[1]   = static_cast<UINT16>(std::lround(hdr->display_primaries_y[2] * 50000.0));
				m_hdr10.hdr10.GreenPrimary[0] = static_cast<UINT16>(std::lround(hdr->display_primaries_x[0] * 50000.0));
				m_hdr10.hdr10.GreenPrimary[1] = static_cast<UINT16>(std::lround(hdr->display_primaries_y[0] * 50000.0));
				m_hdr10.hdr10.BluePrimary[0]  = static_cast<UINT16>(std::lround(hdr->display_primaries_x[1] * 50000.0));
				m_hdr10.hdr10.BluePrimary[1]  = static_cast<UINT16>(std::lround(hdr->display_primaries_y[1] * 50000.0));
				m_hdr10.hdr10.WhitePoint[0]   = static_cast<UINT16>(std::lround(hdr->white_point_x * 50000.0));
				m_hdr10.hdr10.WhitePoint[1]   = static_cast<UINT16>(std::lround(hdr->white_point_y * 50000.0));

				m_hdr10.hdr10.MaxMasteringLuminance = static_cast<UINT>(std::lround(hdr->max_display_mastering_luminance * 10000.0));
				m_hdr10.hdr10.MinMasteringLuminance = static_cast<UINT>(std::lround(hdr->min_display_mastering_luminance * 10000.0));
			}

			MediaSideDataHDRContentLightLevel* hdrCLL = nullptr;
			size = 0;
			hr = pMediaSideData->GetSideData(IID_MediaSideDataHDRContentLightLevel, (const BYTE**)&hdrCLL, &size);
			if (SUCCEEDED(hr) && size == sizeof(MediaSideDataHDRContentLightLevel)) {
				m_hdr10.hdr10.MaxContentLightLevel      = hdrCLL->MaxCLL;
				m_hdr10.hdr10.MaxFrameAverageLightLevel = hdrCLL->MaxFALL;
			}
		}
	}

	if (CComQIPtr<IMediaSampleD3D11> pMSD3D11 = pSample) {
		if (m_iSrcFromGPU != 11) {
			m_iSrcFromGPU = 11;
			updateStats = true;
		}

		CComQIPtr<ID3D11Texture2D> pD3D11Texture2D;
		UINT ArraySlice = 0;
		hr = pMSD3D11->GetD3D11Texture(0, &pD3D11Texture2D, &ArraySlice);
		if (FAILED(hr)) {
			DLog(L"CDX11VideoProcessor::CopySample() : GetD3D11Texture() failed with error {}", HR2Str(hr));
			return hr;
		}

		D3D11_TEXTURE2D_DESC desc = {};
		pD3D11Texture2D->GetDesc(&desc);
		if (desc.Format != m_srcDXGIFormat) {
			return E_UNEXPECTED;
		}

#if 0 // fix for issue #16. disable reinitialization code.
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
			updateStats = true;
		}
#endif

		// here should be used CopySubresourceRegion instead of CopyResource
		D3D11_BOX srcBox = { 0, 0, 0, m_srcWidth, m_srcHeight, 1 };
		if (m_D3D11VP.IsReady()) {
			m_pDeviceContext->CopySubresourceRegion(m_D3D11VP.GetNextInputTexture(m_SampleFormat), 0, 0, 0, 0, pD3D11Texture2D, ArraySlice, &srcBox);
		} else {
			m_pDeviceContext->CopySubresourceRegion(m_TexSrcVideo.pTexture, 0, 0, 0, 0, pD3D11Texture2D, ArraySlice, &srcBox);
		}
	}
	else if (CComQIPtr<IMFGetService> pService = pSample) {
		if (m_iSrcFromGPU != 9) {
			m_iSrcFromGPU = 9;
			updateStats = true;
		}

		CComPtr<IDirect3DSurface9> pSurface9;
		if (SUCCEEDED(pService->GetService(MR_BUFFER_SERVICE, IID_PPV_ARGS(&pSurface9)))) {
			D3DSURFACE_DESC desc = {};
			hr = pSurface9->GetDesc(&desc);
			if (FAILED(hr) || desc.Format != m_srcDXVA2Format) {
				return E_UNEXPECTED;
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
				updateStats = true;
			}

			D3DLOCKED_RECT lr_src;
			hr = pSurface9->LockRect(&lr_src, nullptr, D3DLOCK_READONLY); // may be slow
			if (S_OK == hr) {
				BYTE* srcData = (BYTE*)lr_src.pBits;
				D3D11_MAPPED_SUBRESOURCE mappedResource = {};

				if (m_TexSrcVideo.pTexture2) {
					// for Windows 7
					hr = m_pDeviceContext->Map(m_TexSrcVideo.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
					if (SUCCEEDED(hr)) {
						m_pCopyGpuFn(m_srcHeight, (BYTE*)mappedResource.pData, mappedResource.RowPitch, srcData, lr_src.Pitch);
						m_pDeviceContext->Unmap(m_TexSrcVideo.pTexture, 0);

						hr = m_pDeviceContext->Map(m_TexSrcVideo.pTexture2, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
						if (SUCCEEDED(hr)) {
							srcData += lr_src.Pitch * m_srcHeight;
							m_pCopyGpuFn(m_srcHeight / 2, (BYTE*)mappedResource.pData, mappedResource.RowPitch, srcData, lr_src.Pitch);
							m_pDeviceContext->Unmap(m_TexSrcVideo.pTexture2, 0);
						}
					}
				}
				else {
					hr = m_pDeviceContext->Map(m_TexSrcVideo.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
					if (SUCCEEDED(hr)) {
						m_pCopyGpuFn(m_srcLines, (BYTE*)mappedResource.pData, mappedResource.RowPitch, srcData, lr_src.Pitch);
						m_pDeviceContext->Unmap(m_TexSrcVideo.pTexture, 0);
					}
				}

				pSurface9->UnlockRect();

				if (m_D3D11VP.IsReady()) {
					// ID3D11VideoProcessor does not use textures with D3D11_CPU_ACCESS_WRITE flag
					m_pDeviceContext->CopyResource(m_D3D11VP.GetNextInputTexture(m_SampleFormat), m_TexSrcVideo.pTexture);
				}
			}
		}
	}
	else {
		if (m_iSrcFromGPU != 0) {
			m_iSrcFromGPU = 0;
			updateStats = true;
		}

		BYTE* data = nullptr;
		const long size = pSample->GetActualDataLength();
		if (size > 0 && S_OK == pSample->GetPointer(&data)) {
			// do not use UpdateSubresource for D3D11 VP here
			// because it can cause green screens and freezes on some configurations
			hr = MemCopyToTexSrcVideo(data, m_srcPitch);
			if (m_D3D11VP.IsReady()) {
				// ID3D11VideoProcessor does not use textures with D3D11_CPU_ACCESS_WRITE flag
				m_pDeviceContext->CopyResource(m_D3D11VP.GetNextInputTexture(m_SampleFormat), m_TexSrcVideo.pTexture);
			}
		}
	}

	if (updateStats) {
		UpdateStatsStatic();
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
		DLog(L"CDX11VideoProcessor::Render() : GetBuffer() failed with error {}", HR2Str(hr));
		return hr;
	}

	uint64_t tick1 = GetPreciseTick();

	HRESULT hrSubPic = E_FAIL;

	if (m_pFilter->m_pSubCallBack && m_pShaderResourceSubPic && !m_bD3D11Subtitle) {
		const CRect rSrcPri(CPoint(0, 0), m_windowRect.Size());
		const CRect rDstVid(m_videoRect);
		const auto rtStart = m_pFilter->m_rtStartTime + m_rtStart;

		if (m_bSubPicWasRendered) {
			m_bSubPicWasRendered = false;
			m_pD3DDevEx->ColorFill(m_pSurface9SubPic, nullptr, m_pFilter->m_bSubInvAlpha ? D3DCOLOR_ARGB(0, 0, 0, 0) : D3DCOLOR_ARGB(255, 0, 0, 0));
		}

		if (CComQIPtr<ISubRenderCallback4> pSubCallBack4 = m_pFilter->m_pSubCallBack) {
			hrSubPic = pSubCallBack4->RenderEx3(rtStart, 0, m_rtAvgTimePerFrame, rDstVid, rDstVid, rSrcPri);
		} else {
			hrSubPic = m_pFilter->m_pSubCallBack->Render(rtStart, rDstVid.left, rDstVid.top, rDstVid.right, rDstVid.bottom, rSrcPri.Width(), rSrcPri.Height());
		}

		if (S_OK == hrSubPic) {
			m_bSubPicWasRendered = true;

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

	/*we render directly onto the back buffer with d3d11*/
	if (m_pFilter->m_pSub11CallBack && m_bD3D11Subtitle) {
		const CRect rSrcPri(CPoint(0, 0), m_windowRect.Size());
		const CRect rDstVid(m_videoRect);
		const auto rtStart = m_pFilter->m_rtStartTime + m_rtStart;

		if (S_OK == hr) {
			const FLOAT ClearColorInv[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			const FLOAT ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			D3D11_VIEWPORT VP;
			VP.TopLeftX = 0;
			VP.TopLeftY = 0;
			VP.Width = rSrcPri.Width();
			VP.Height = rSrcPri.Height();
			VP.MinDepth = 0.0f;
			VP.MaxDepth = 1.0f;
			ID3D11RenderTargetView* pRenderTargetView;
			HRESULT hr = m_pDevice->CreateRenderTargetView(m_pTextureSubPic, nullptr, &pRenderTargetView);
			m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, m_pFilter->m_bSubInvAlpha ? ClearColorInv : ClearColor);
			// Set resources
			m_pDeviceContext->IASetInputLayout(m_pVSimpleInputLayout);
			//m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
			m_pDeviceContext->RSSetViewports(1, &VP);
			m_pDeviceContext->OMSetBlendState(m_pFilter->m_bSubInvAlpha ? m_pAlphaBlendStateInv : m_pAlphaBlendState, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
			m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
			m_pDeviceContext->PSSetShader(m_pPS_Simple, nullptr, 0);
			m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerPoint);
			m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			
			//everything is set directly on the renderer so we dont have to compile any shaders on the player side
			//the player will create the vertex buffer and draw
			hrSubPic = m_pFilter->m_pSub11CallBack->Render11(rtStart, 0, m_rtAvgTimePerFrame, rDstVid, rDstVid, rSrcPri);

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

	if (S_OK == hrSubPic ) {//&& !m_bD3D11Subtitle) {
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
	}

	if (m_bShowStats) {
		hr = DrawStats(pBackBuffer);
	}

	if (m_bAlphaBitmapEnable) {
		D3D11_TEXTURE2D_DESC desc;
		pBackBuffer->GetDesc(&desc);
		D3D11_VIEWPORT VP = {
			m_AlphaBitmapNRectDest.left * desc.Width,
			m_AlphaBitmapNRectDest.top * desc.Height,
			(m_AlphaBitmapNRectDest.right - m_AlphaBitmapNRectDest.left) * desc.Width,
			(m_AlphaBitmapNRectDest.bottom - m_AlphaBitmapNRectDest.top) * desc.Height,
			0.0f,
			1.0f
		};
		hr = AlphaBlt(m_TexAlphaBitmap.pShaderResource, pBackBuffer, m_pAlphaBitmapVertex, &VP, m_pSamplerLinear);
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
	uint64_t tick3 = GetPreciseTick();
	m_RenderStats.paintticks = tick3 - tick1;

	if (m_pDXGISwapChain4) {
		const DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
		if (m_currentSwapChainColorSpace != colorSpace) {
			if (m_hdr10.bValid) {
				hr = m_pDXGISwapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(DXGI_HDR_METADATA_HDR10), &m_hdr10.hdr10);
				DLogIf(FAILED(hr), L"CDX11VideoProcessor::Render() : SetHDRMetaData(hdr) failed with error {}", HR2Str(hr));

				m_lastHdr10 = m_hdr10;
				UpdateStatsStatic();
			} else if (m_lastHdr10.bValid) {
				hr = m_pDXGISwapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(DXGI_HDR_METADATA_HDR10), &m_lastHdr10.hdr10);
				DLogIf(FAILED(hr), L"CDX11VideoProcessor::Render() : SetHDRMetaData(lastHdr) failed with error {}", HR2Str(hr));
			} else {
				m_lastHdr10.bValid = true;

				m_lastHdr10.hdr10.RedPrimary[0]   = 34000; // Display P3 primaries
				m_lastHdr10.hdr10.RedPrimary[1]   = 16000;
				m_lastHdr10.hdr10.GreenPrimary[0] = 13250;
				m_lastHdr10.hdr10.GreenPrimary[1] = 34500;
				m_lastHdr10.hdr10.BluePrimary[0]  = 7500;
				m_lastHdr10.hdr10.BluePrimary[1]  = 3000;
				m_lastHdr10.hdr10.WhitePoint[0]   = 15635;
				m_lastHdr10.hdr10.WhitePoint[1]   = 16450;
				m_lastHdr10.hdr10.MaxMasteringLuminance = 1000 * 10000; // 1000 nits
				m_lastHdr10.hdr10.MinMasteringLuminance = 100;          // 0.01 nits
				hr = m_pDXGISwapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(DXGI_HDR_METADATA_HDR10), &m_lastHdr10.hdr10);
				DLogIf(FAILED(hr), L"CDX11VideoProcessor::Render() : SetHDRMetaData(Display P3 standard) failed with error {}", HR2Str(hr));

				UpdateStatsStatic();
			}

			UINT colorSpaceSupport = 0;
			if (SUCCEEDED(m_pDXGISwapChain4->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport))
					&& (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) {
				hr = m_pDXGISwapChain4->SetColorSpace1(colorSpace);
				DLogIf(FAILED(hr), L"CDX11VideoProcessor::Render() : SetColorSpace1() failed with error {}", HR2Str(hr));
				if (SUCCEEDED(hr)) {
					m_currentSwapChainColorSpace = colorSpace;
				}
			}
		} else if (m_hdr10.bValid) {
			if (memcmp(&m_hdr10.hdr10, &m_lastHdr10.hdr10, sizeof(m_hdr10.hdr10)) != 0) {
				hr = m_pDXGISwapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(DXGI_HDR_METADATA_HDR10), &m_hdr10.hdr10);
				DLogIf(FAILED(hr), L"CDX11VideoProcessor::Render() : SetHDRMetaData(hdr) failed with error {}", HR2Str(hr));

				m_lastHdr10 = m_hdr10;
				UpdateStatsStatic();
			}
		}
	}

	if (m_bVBlankBeforePresent && m_pDXGIOutput) {
		hr = m_pDXGIOutput->WaitForVBlank();
		DLogIf(FAILED(hr), L"WaitForVBlank failed with error {}", HR2Str(hr));
	}

	g_bPresent = true;
	hr = m_pDXGISwapChain1->Present(1, 0);
	g_bPresent = false;
	DLogIf(FAILED(hr), L"CDX11VideoProcessor::Render() : Present() failed with error {}", HR2Str(hr));

	m_RenderStats.presentticks = GetPreciseTick() - tick3;

	if (hr == DXGI_ERROR_INVALID_CALL && m_pFilter->m_bIsD3DFullscreen) {
		InitSwapChain();
	}

	return hr;
}

HRESULT CDX11VideoProcessor::FillBlack()
{
	CheckPointer(m_pDXGISwapChain1, E_ABORT);

	ID3D11Texture2D* pBackBuffer = nullptr;
	HRESULT hr = m_pDXGISwapChain1->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::FillBlack() : GetBuffer() failed with error {}", HR2Str(hr));
		return hr;
	}

	ID3D11RenderTargetView* pRenderTargetView;
	hr = m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView);
	pBackBuffer->Release();
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::FillBlack() : CreateRenderTargetView() failed with error {}", HR2Str(hr));
		return hr;
	}

	float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ClearColor);

	if (m_bShowStats) {
		hr = DrawStats(pBackBuffer);
	}

	if (m_bAlphaBitmapEnable) {
		D3D11_TEXTURE2D_DESC desc;
		pBackBuffer->GetDesc(&desc);
		D3D11_VIEWPORT VP = {
			m_AlphaBitmapNRectDest.left * desc.Width,
			m_AlphaBitmapNRectDest.top * desc.Height,
			(m_AlphaBitmapNRectDest.right - m_AlphaBitmapNRectDest.left) * desc.Width,
			(m_AlphaBitmapNRectDest.bottom - m_AlphaBitmapNRectDest.top) * desc.Height,
			0.0f,
			1.0f
		};
		hr = AlphaBlt(m_TexAlphaBitmap.pShaderResource, pBackBuffer, m_pAlphaBitmapVertex, &VP, m_pSamplerLinear);
	}

	g_bPresent = true;
	hr = m_pDXGISwapChain1->Present(1, 0);
	g_bPresent = false;

	pRenderTargetView->Release();

	return hr;
}

void CDX11VideoProcessor::UpdateTexures()
{
	if (!m_srcWidth || !m_srcHeight) {
		return;
	}

	// TODO: try making w and h a multiple of 128.
	HRESULT hr = S_OK;

	if (m_D3D11VP.IsReady()) {
		if (m_bVPScaling) {
			CSize texsize = m_videoRect.Size();
			hr = m_TexConvertOutput.CheckCreate(m_pDevice, m_D3D11OutputFmt, texsize.cx, texsize.cy, Tex2D_DefaultShaderRTarget);
		} else {
			hr = m_TexConvertOutput.CheckCreate(m_pDevice, m_D3D11OutputFmt, m_srcRectWidth, m_srcRectHeight, Tex2D_DefaultShaderRTarget);
		}
	}
	else {
		hr = m_TexConvertOutput.CheckCreate(m_pDevice, m_InternalTexFmt, m_srcRectWidth, m_srcRectHeight, Tex2D_DefaultShaderRTarget);
	}
}

void CDX11VideoProcessor::UpdatePostScaleTexures()
{
	bool needDither = (m_SwapChainFmt == DXGI_FORMAT_B8G8R8A8_UNORM && m_InternalTexFmt != DXGI_FORMAT_B8G8R8A8_UNORM
		|| m_SwapChainFmt == DXGI_FORMAT_R10G10B10A2_UNORM && m_InternalTexFmt == DXGI_FORMAT_R16G16B16A16_FLOAT);

	m_bFinalPass = (m_bUseDither && needDither && m_TexDither.pTexture);

	UINT numPostScaleShaders = m_pPostScaleShaders.size();
	if (m_pPSCorrection) {
		numPostScaleShaders++;
	}
	if (m_bFinalPass) {
		m_pPSFinalPass.Release();
		m_bFinalPass = SUCCEEDED(CreatePShaderFromResource(
			&m_pPSFinalPass,
			(m_SwapChainFmt == DXGI_FORMAT_R10G10B10A2_UNORM) ? IDF_PSH11_FINAL_PASS_10 : IDF_PSH11_FINAL_PASS
		));
		if (m_bFinalPass) {
			numPostScaleShaders++;
		}
	}
	HRESULT hr = m_TexsPostScale.CheckCreate(m_pDevice, m_InternalTexFmt, m_windowRect.Width(), m_windowRect.Height(), numPostScaleShaders);
	//UpdateStatsPostProc();
}

void CDX11VideoProcessor::UpdateUpscalingShaders()
{
	m_pShaderUpscaleX.Release();
	m_pShaderUpscaleY.Release();

	if (m_iUpscaling != UPSCALE_Nearest) {
		EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderUpscaleX, s_Upscaling11ResIDs[m_iUpscaling].shaderX));
		if (m_iUpscaling == UPSCALE_Jinc2) {
			m_pShaderUpscaleY = m_pShaderUpscaleX;
		} else {
			EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderUpscaleY, s_Upscaling11ResIDs[m_iUpscaling].shaderY));
		}
	}
}

void CDX11VideoProcessor::UpdateDownscalingShaders()
{
	m_pShaderDownscaleX.Release();
	m_pShaderDownscaleY.Release();

	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderDownscaleX, s_Downscaling11ResIDs[m_iDownscaling].shaderX));
	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderDownscaleY, s_Downscaling11ResIDs[m_iDownscaling].shaderY));
}

HRESULT CDX11VideoProcessor::UpdateConvertColorShader()
{
	m_pPSConvertColor.Release();
	ID3DBlob* pShaderCode = nullptr;

	int convertType = (m_bConvertToSdr && !(m_bHdrPassthroughSupport && m_bHdrPassthrough)) ? SHADER_CONVERT_TO_SDR
		: (m_bHdrPassthroughSupport && m_bHdrPassthrough && m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG) ? SHADER_CONVERT_TO_PQ
		: SHADER_CONVERT_NONE;

	HRESULT hr = GetShaderConvertColor(true,
		m_srcWidth,
		m_TexSrcVideo.desc.Width, m_TexSrcVideo.desc.Height,
		m_srcRect, m_srcParams, m_srcExFmt,
		m_iChromaScaling, convertType,
		&pShaderCode);
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
		DLog(L"CDX11VideoProcessor::ProcessD3D11() : m_D3D11VP.Process() failed with error {}", HR2Str(hr));
	}

	return hr;
}

HRESULT CDX11VideoProcessor::ConvertColorPass(ID3D11Texture2D* pRenderTarget)
{
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;

	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (FAILED(hr)) {
		DLog(L"ConvertColorPass() : CreateRenderTargetView() failed with error {}", HR2Str(hr));
		return hr;
	}

	D3D11_VIEWPORT VP;
	VP.TopLeftX = 0;
	VP.TopLeftY = 0;
	VP.Width = (FLOAT)m_TexConvertOutput.desc.Width;
	VP.Height = (FLOAT)m_TexConvertOutput.desc.Height;
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
	m_pDeviceContext->PSSetSamplers(1, 1, &m_pSamplerLinear);
	m_pDeviceContext->PSSetConstantBuffers(0, 1, &m_PSConvColorData.pConstants);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_PSConvColorData.pVertexBuffer, &Stride, &Offset);

	// Draw textured quad onto render target
	m_pDeviceContext->Draw(4, 0);

	ID3D11ShaderResourceView* views[3] = {};
	m_pDeviceContext->PSSetShaderResources(0, 3, views);

	return hr;
}

HRESULT CDX11VideoProcessor::ResizeShaderPass(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect, const int rotation)
{
	HRESULT hr = S_OK;
	const int w2 = dstRect.Width();
	const int h2 = dstRect.Height();
	const int k = m_bInterpolateAt50pct ? 2 : 1;

	int w1, h1;
	ID3D11PixelShader* resizerX;
	ID3D11PixelShader* resizerY;
	if (rotation == 90 || rotation == 270) {
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

		if (resizerX == resizerY) {
			// one pass resize
			hr = TextureResizeShader(Tex, pRenderTarget, srcRect, dstRect, resizerX, rotation, m_bFlip);
			DLogIf(FAILED(hr), L"CDX11VideoProcessor::ResizeShaderPass() : failed with error {}", HR2Str(hr));

			return hr;
		}

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
				DLog(L"CDX11VideoProcessor::ResizeShaderPass() : m_TexResize.Create() failed with error {}", HR2Str(hr));
				return hr;
			}
		}

		CRect resizeRect(dstRect.left, 0, dstRect.right, texHeight);

		// First resize pass
		hr = TextureResizeShader(Tex, m_TexResize.pTexture, srcRect, resizeRect, resizerX, rotation, m_bFlip);
		// Second resize pass
		hr = TextureResizeShader(m_TexResize, pRenderTarget, resizeRect, dstRect, resizerY, 0, false);
	}
	else {
		if (resizerX) {
			// one pass resize for width
			hr = TextureResizeShader(Tex, pRenderTarget, srcRect, dstRect, resizerX, rotation, m_bFlip);
		}
		else if (resizerY) {
			// one pass resize for height
			hr = TextureResizeShader(Tex, pRenderTarget, srcRect, dstRect, resizerY, rotation, m_bFlip);
		}
		else {
			// no resize
			hr = TextureCopyRect(Tex, pRenderTarget, srcRect, dstRect, m_pPS_Simple, nullptr, rotation, m_bFlip);
		}
	}

	DLogIf(FAILED(hr), L"CDX11VideoProcessor::ResizeShaderPass() : failed with error {}", HR2Str(hr));

	return hr;
}

HRESULT CDX11VideoProcessor::FinalPass(const Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect)
{
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;
	CComPtr<ID3D11Buffer> pVertexBuffer;
	CComPtr<ID3D11Buffer> pConstantBuffer;

	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (FAILED(hr)) {
		DLog(L"FinalPass() : CreateRenderTargetView() failed with error {}", HR2Str(hr));
		return hr;
	}

	hr = CreateVertexBuffer(m_pDevice, &pVertexBuffer, Tex.desc.Width, Tex.desc.Height, srcRect, 0, false);
	if (FAILED(hr)) {
		DLog(L"FinalPass() : Create vertex buffer failed with error {}", HR2Str(hr));
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
		DLog(L"FinalPass() : Create constant buffer failed with error {}", HR2Str(hr));
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
	m_bDitherUsed = false;
	int rotation = m_iRotation;

	CRect rSrc = srcRect;
	Tex2D_t* pInputTexture = nullptr;
	bool bNeedPostProc = m_pPSCorrection || m_pPostScaleShaders.size();
	if (m_D3D11VP.IsReady()) {
		if (!(m_iSwapEffect == SWAPEFFECT_Discard && (m_VendorId == PCIV_AMDATI || m_VendorId == PCIV_INTEL))) {
			bool bNeedShaderTransform = (m_TexConvertOutput.desc.Width != dstRect.Width() || m_TexConvertOutput.desc.Height != dstRect.Height() || m_bFlip
										|| dstRect.right > m_windowRect.right || dstRect.bottom > m_windowRect.bottom);
			if (!bNeedShaderTransform && !bNeedPostProc && !m_bFinalPass) {
				m_bVPScalingUseShaders = false;

				hr = D3D11VPPass(pRenderTarget, rSrc, dstRect, second);
				return hr;
			}
		}

		CRect rect(0, 0, m_TexConvertOutput.desc.Width, m_TexConvertOutput.desc.Height);
		hr = D3D11VPPass(m_TexConvertOutput.pTexture, rSrc, rect, second);
		pInputTexture = &m_TexConvertOutput;
		rSrc = rect;
		rotation = 0;
	}
	else if (m_PSConvColorData.bEnable) {
		ConvertColorPass(m_TexConvertOutput.pTexture);
		pInputTexture = &m_TexConvertOutput;
		rSrc.SetRect(0, 0, m_TexConvertOutput.desc.Width, m_TexConvertOutput.desc.Height);
	}
	else {
		pInputTexture = &m_TexSrcVideo;
	}

	if (bNeedPostProc || m_bFinalPass) {
		Tex2D_t* Tex = m_TexsPostScale.GetFirstTex();
		CRect rect;
		rect.IntersectRect(dstRect, CRect(0, 0, Tex->desc.Width, Tex->desc.Height));

		if (m_D3D11VP.IsReady()) {
			m_bVPScalingUseShaders = rSrc.Width() != dstRect.Width() || rSrc.Height() != dstRect.Height();
		}

		if (rSrc != dstRect) {
			hr = ResizeShaderPass(*pInputTexture, Tex->pTexture, rSrc, dstRect, rotation);
		} else {
			Tex = pInputTexture;
		}

		ID3D11PixelShader* pPixelShader = nullptr;
		ID3D11Buffer* pConstantBuffer = nullptr;

		if (m_pPSCorrection) {
			pPixelShader = m_pPSCorrection;
		}

		if (m_pPostScaleShaders.size()) {
			if (m_pPSCorrection) {
				pInputTexture = Tex;
				Tex = m_TexsPostScale.GetNextTex();
				hr = TextureCopyRect(*pInputTexture, Tex->pTexture, rect, rect, pPixelShader, nullptr, 0, false);
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
					hr = TextureCopyRect(*pInputTexture, Tex->pTexture, rect, rect, pPixelShader, m_pPostScaleConstants, 0, false);
				}
				pPixelShader = m_pPostScaleShaders.back().shader;
				pConstantBuffer = m_pPostScaleConstants;
			}
		}

		if (m_bFinalPass) {
			if (m_pPSCorrection || m_pPostScaleShaders.size()) {
				pInputTexture = Tex;
				Tex = m_TexsPostScale.GetNextTex();
				hr = TextureCopyRect(*pInputTexture, Tex->pTexture, rect, rect, pPixelShader, pConstantBuffer, 0, false);
			}

			hr = FinalPass(*Tex, pRenderTarget, rect, rect);
			m_bDitherUsed = true;
		}
		else {
			hr = TextureCopyRect(*Tex, pRenderTarget, rect, rect, pPixelShader, pConstantBuffer, 0, false);
		}
	}
	else {
		hr = ResizeShaderPass(*pInputTexture, pRenderTarget, rSrc, dstRect, rotation);
	}

	DLogIf(FAILED(hr), L"CDX9VideoProcessor::Process() : failed with error {}", HR2Str(hr));

	return hr;
}

void CDX11VideoProcessor::SetVideoRect(const CRect& videoRect)
{
	m_videoRect = videoRect;
	UpdateRenderRect();
	UpdateTexures();
}

HRESULT CDX11VideoProcessor::SetWindowRect(const CRect& windowRect)
{
	m_windowRect = windowRect;
	UpdateRenderRect();

	HRESULT hr = S_OK;
	const UINT w = m_windowRect.Width();
	const UINT h = m_windowRect.Height();

	if (m_pDXGISwapChain1 && !m_bIsFullscreen) {
		hr = m_pDXGISwapChain1->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
	}

	SetGraphSize();

	UpdatePostScaleTexures();

	return hr;
}

HRESULT CDX11VideoProcessor::Reset()
{
	DLog(L"CDX11VideoProcessor::Reset()");

	if (m_bHdrPassthrough && SourceIsHDR()) {
		MONITORINFOEXW mi = { sizeof(mi) };
		GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO*)&mi);
		DisplayConfig_t displayConfig = {};

		if (GetDisplayConfig(mi.szDevice, displayConfig)) {
			const auto& ac = displayConfig.advancedColor;
			const auto bHdrPassthroughSupport = ac.advancedColorSupported && ac.advancedColorEnabled;

			if (bHdrPassthroughSupport && !m_bHdrPassthroughSupport || !ac.advancedColorEnabled && m_bHdrPassthroughSupport) {
				m_hdrModeSavedState.erase(mi.szDevice);

				if (m_pFilter->m_inputMT.IsValid()) {
					ReleaseSwapChain();
					if (m_iSwapEffect == SWAPEFFECT_Discard && !ac.advancedColorEnabled) {
						m_pFilter->Init(true);
					} else {
						Init(m_hWnd);
					}
					m_bHdrAllowSwitchDisplay = false;
					InitMediaType(&m_pFilter->m_inputMT);
					m_bHdrAllowSwitchDisplay = true;
				}
			}
		}
	}

	return S_OK;
}

HRESULT CDX11VideoProcessor::GetCurentImage(long *pDIBImage)
{
	CSize framesize(m_srcRectWidth, m_srcRectHeight);
	if (m_srcAnamorphic) {
		framesize.cx = MulDiv(framesize.cy, m_srcAspectRatioX, m_srcAspectRatioY);
	}
	if (m_iRotation == 90 || m_iRotation == 270) {
		std::swap(framesize.cx, framesize.cy);
	}
	const auto w = framesize.cx;
	const auto h = framesize.cy;
	const CRect imageRect(0, 0, w, h);

	BITMAPINFOHEADER* pBIH = (BITMAPINFOHEADER*)pDIBImage;
	ZeroMemory(pBIH, sizeof(BITMAPINFOHEADER));
	pBIH->biSize      = sizeof(BITMAPINFOHEADER);
	pBIH->biWidth     = w;
	pBIH->biHeight    = h;
	pBIH->biPlanes    = 1;
	pBIH->biBitCount  = 32;
	pBIH->biSizeImage = DIBSIZE(*pBIH);

	UINT dst_pitch = pBIH->biSizeImage / h;

	HRESULT hr = S_OK;
	CComPtr<ID3D11Texture2D> pRGB32Texture2D;
	D3D11_TEXTURE2D_DESC texdesc = CreateTex2DDesc(DXGI_FORMAT_B8G8R8X8_UNORM, w, h, Tex2D_DefaultRTarget);

	hr = m_pDevice->CreateTexture2D(&texdesc, nullptr, &pRGB32Texture2D);
	if (FAILED(hr)) {
		return hr;
	}

	const auto backupVidRect = m_videoRect;
	const auto backupWndRect = m_windowRect;
	m_videoRect  = imageRect;
	m_windowRect = imageRect;
	UpdateTexures();
	UpdatePostScaleTexures();

	hr = Process(pRGB32Texture2D, m_srcRect, imageRect, false);

	m_videoRect  = backupVidRect;
	m_windowRect = backupWndRect;
	UpdateTexures();
	UpdatePostScaleTexures();

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
		DLog(L"CDX11VideoProcessor::GetDisplayedImage() failed with error {}", HR2Str(hr));
		return hr;
	}

	D3D11_TEXTURE2D_DESC desc;
	pBackBuffer->GetDesc(&desc);

	if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format != DXGI_FORMAT_R10G10B10A2_UNORM) {
		DLog(L"CDX11VideoProcessor::GetDisplayedImage() backbuffer format not supported");
		return E_FAIL;
	}

	D3D11_TEXTURE2D_DESC desc2 = CreateTex2DDesc(desc.Format, desc.Width, desc.Height, Tex2D_StagingRead);
	CComPtr<ID3D11Texture2D> pTexture2DShared;
	hr = m_pDevice->CreateTexture2D(&desc2, nullptr, &pTexture2DShared);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::GetDisplayedImage() failed with error {}", HR2Str(hr));
		return hr;
	}

	m_pDeviceContext->CopyResource(pTexture2DShared, pBackBuffer);

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

	D3D11_MAPPED_SUBRESOURCE mappedResource = {};
	hr = m_pDeviceContext->Map(pTexture2DShared, 0, D3D11_MAP_READ, 0, &mappedResource);
	if (SUCCEEDED(hr)) {
		if (desc2.Format == DXGI_FORMAT_R10G10B10A2_UNORM) {
			ConvertXRGB10toXRGB8(desc.Height, (BYTE*)(pBIH + 1), dst_pitch, (BYTE*)mappedResource.pData + mappedResource.RowPitch * (desc.Height - 1), -(int)mappedResource.RowPitch);
		}
		else {
			CopyFrameAsIs(desc.Height, (BYTE*)(pBIH + 1), dst_pitch, (BYTE*)mappedResource.pData + mappedResource.RowPitch * (desc.Height - 1), -(int)mappedResource.RowPitch);
		}
		m_pDeviceContext->Unmap(pTexture2DShared, 0);
		*ppDib = p;
	} else {
		LocalFree(p);
	}

	return hr;
}

HRESULT CDX11VideoProcessor::GetVPInfo(std::wstring& str)
{
	str = L"DirectX 11";
	str += fmt::format(L"\nGraphics adapter: {}", m_strAdapterDescription);
	str.append(L"\nVideoProcessor  : ");
	if (m_D3D11VP.IsReady()) {
		D3D11_VIDEO_PROCESSOR_CAPS caps;
		UINT rateConvIndex;
		D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS rateConvCaps;
		m_D3D11VP.GetVPParams(caps, rateConvIndex, rateConvCaps);

		str += fmt::format(L"D3D11, RateConversion_{}", rateConvIndex);

		str.append(L"\nDeinterlaceTech.:");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BLEND)               str.append(L" Blend,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BOB)                 str.append(L" Bob,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_ADAPTIVE)            str.append(L" Adaptive,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_MOTION_COMPENSATION) str.append(L" Motion Compensation,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_INVERSE_TELECINE)                str.append(L" Inverse Telecine,");
		if (rateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_FRAME_RATE_CONVERSION)           str.append(L" Frame Rate Conversion");
		str_trim_end(str, ',');
		str += fmt::format(L"\nReference Frames: Past {}, Future {}", rateConvCaps.PastFrames, rateConvCaps.FutureFrames);
	} else {
		str.append(L"Shaders");
	}

	str.append(m_strStatsDispInfo);

	if (m_pPostScaleShaders.size()) {
		str.append(L"\n\nPost scale pixel shaders:");
		for (const auto& pshader : m_pPostScaleShaders) {
			str += fmt::format(L"\n  {}", pshader.name);
		}
	}

#ifdef _DEBUG
	str.append(L"\n\nDEBUG info:");
	str += fmt::format(L"\nSource tex size: {}x{}", m_srcWidth, m_srcHeight);
	str += fmt::format(L"\nSource rect    : {},{},{},{} - {}x{}", m_srcRect.left, m_srcRect.top, m_srcRect.right, m_srcRect.bottom, m_srcRect.Width(), m_srcRect.Height());
	str += fmt::format(L"\nVideo rect     : {},{},{},{} - {}x{}", m_videoRect.left, m_videoRect.top, m_videoRect.right, m_videoRect.bottom, m_videoRect.Width(), m_videoRect.Height());
	str += fmt::format(L"\nWindow rect    : {},{},{},{} - {}x{}", m_windowRect.left, m_windowRect.top, m_windowRect.right, m_windowRect.bottom, m_windowRect.Width(), m_windowRect.Height());

	if (m_pDevice) {
		std::vector<std::pair<const DXGI_FORMAT, UINT>> formatsYUV = {
			{ DXGI_FORMAT_NV12,               0 },
			{ DXGI_FORMAT_P010,               0 },
			{ DXGI_FORMAT_P016,               0 },
			{ DXGI_FORMAT_YUY2,               0 },
			{ DXGI_FORMAT_Y210,               0 },
			{ DXGI_FORMAT_Y216,               0 },
			{ DXGI_FORMAT_AYUV,               0 },
			{ DXGI_FORMAT_Y410,               0 },
			{ DXGI_FORMAT_Y416,               0 },
		};
		std::vector<std::pair<const DXGI_FORMAT, UINT>> formatsRGB = {
			{ DXGI_FORMAT_B8G8R8X8_UNORM,     0 },
			{ DXGI_FORMAT_B8G8R8A8_UNORM,     0 },
			{ DXGI_FORMAT_R10G10B10A2_UNORM,  0 },
			{ DXGI_FORMAT_R16G16B16A16_UNORM, 0 },
		};
		for (auto& [format, formatSupport] : formatsYUV) {
			m_pDevice->CheckFormatSupport(format, &formatSupport);
		}
		for (auto& [format, formatSupport] : formatsRGB) {
			m_pDevice->CheckFormatSupport(format, &formatSupport);
		}

		int count = 0;
		str += L"\nD3D11 VP input formats  :";
		for (const auto& [format, formatSupport] : formatsYUV) {
			if (formatSupport & D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT) {
				str.append(L" ");
				str.append(DXGIFormatToString(format));
				count++;
			}
		}
		if (count) {
			str += L"\n ";
		}
		for (const auto& [format, formatSupport] : formatsRGB) {
			if (formatSupport & D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT) {
				str.append(L" ");
				str.append(DXGIFormatToString(format));
			}
		}

		count = 0;
		str += L"\nShader Texture2D formats:";
		for (const auto& [format, formatSupport] : formatsYUV) {
			if (formatSupport & (D3D11_FORMAT_SUPPORT_TEXTURE2D|D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)) {
				str.append(L" ");
				str.append(DXGIFormatToString(format));
				count++;
			}
		}
		if (count) {
			str += L"\n ";
		}
		for (const auto& [format, formatSupport] : formatsRGB) {
			if (formatSupport & (D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)) {
				str.append(L" ");
				str.append(DXGIFormatToString(format));
			}
		}
	}
#endif

	return S_OK;
}

void CDX11VideoProcessor::Configure(const Settings_t& config)
{
	bool changeWindow            = false;
	bool changeDevice            = false;
	bool changeVP                = false;
	bool changeHDR               = false;
	bool changeTextures          = false;
	bool changeConvertShader     = false;
	bool changeUpscalingShader   = false;
	bool changeDowndcalingShader = false;
	bool changeNumTextures       = false;
	bool changeResizeStats       = false;
	bool changeD3D11Subtitle     = false;
	// settings that do not require preparation
	m_bShowStats                      = config.bShowStats;
	m_bDeintDouble                    = config.bDeintDouble;
	m_bInterpolateAt50pct             = config.bInterpolateAt50pct;
	m_bVBlankBeforePresent            = config.bVBlankBeforePresent;


	if (m_bD3D11Subtitle != config.bD3D11Subtitle) {
		m_bD3D11Subtitle = config.bD3D11Subtitle;
		//TODO Change device
		changeD3D11Subtitle = true;
	}
	// checking what needs to be changed

	if (config.iResizeStats != m_iResizeStats) {
		m_iResizeStats = config.iResizeStats;
		changeResizeStats = true;
	}

	if (config.iTexFormat != m_iTexFormat) {
		m_iTexFormat = config.iTexFormat;
		changeTextures = true;
	}

	if (m_srcParams.cformat == CF_NV12) {
		changeVP = config.VPFmts.bNV12 != m_VPFormats.bNV12;
	}
	else if (m_srcParams.cformat == CF_P010 || m_srcParams.cformat == CF_P016) {
		changeVP = config.VPFmts.bP01x != m_VPFormats.bP01x;
	}
	else if (m_srcParams.cformat == CF_YUY2) {
		changeVP = config.VPFmts.bYUY2 != m_VPFormats.bYUY2;
	}
	else {
		changeVP = config.VPFmts.bOther != m_VPFormats.bOther;
	}
	m_VPFormats = config.VPFmts;

	if (config.bVPScaling != m_bVPScaling) {
		m_bVPScaling = config.bVPScaling;
		changeTextures = true;
		changeVP = true; // temporary solution
	}
	if (config.iChromaScaling != m_iChromaScaling) {
		m_iChromaScaling = config.iChromaScaling;
		changeConvertShader = m_PSConvColorData.bEnable && (m_srcParams.Subsampling == 420 || m_srcParams.Subsampling == 422);
	}

	if (config.iUpscaling != m_iUpscaling) {
		m_iUpscaling = config.iUpscaling;
		changeUpscalingShader = true;
	}
	if (config.iDownscaling != m_iDownscaling) {
		m_iDownscaling = config.iDownscaling;
		changeDowndcalingShader = true;
	}

	if (config.bUseDither != m_bUseDither) {
		m_bUseDither = config.bUseDither;
		changeNumTextures = m_InternalTexFmt != DXGI_FORMAT_B8G8R8A8_UNORM;
	}

	if (config.iSwapEffect != m_iSwapEffect) {
		m_iSwapEffect = config.iSwapEffect;
		changeWindow = !m_pFilter->m_bIsFullscreen;
	}

	if (config.bHdrPassthrough != m_bHdrPassthrough) {
		m_bHdrPassthrough = config.bHdrPassthrough;
		changeHDR = true;
	}

	if (config.iHdrToggleDisplay != m_iHdrToggleDisplay) {
		if (config.iHdrToggleDisplay == HDRTD_Off || m_iHdrToggleDisplay == HDRTD_Off) {
			changeHDR = true;
		}
		m_iHdrToggleDisplay = config.iHdrToggleDisplay;
	}

	if (config.bConvertToSdr != m_bConvertToSdr) {
		m_bConvertToSdr = config.bConvertToSdr;
		if (SourceIsHDR()) {
			if (m_D3D11VP.IsReady()) {
				changeNumTextures = true;
				changeVP = true; // temporary solution
			} else {
				changeConvertShader = true;
			}
		}
	}

	// apply new settings

	if (changeWindow) {
		ReleaseSwapChain();
		EXECUTE_ASSERT(S_OK == m_pFilter->Init(true));

		if (changeHDR && (SourceIsHDR()) || m_iHdrToggleDisplay) {
			m_srcVideoTransferFunction = 0;
			InitMediaType(&m_pFilter->m_inputMT);
		}
		return;
	}

	if (changeHDR) {
		if (SourceIsHDR() || m_iHdrToggleDisplay) {
			if (m_iSwapEffect == SWAPEFFECT_Discard) {
				ReleaseSwapChain();
				m_pFilter->Init(true);
			}

			m_srcVideoTransferFunction = 0;
			InitMediaType(&m_pFilter->m_inputMT);

			return;
		}
	}

	if (changeVP) {
		InitMediaType(&m_pFilter->m_inputMT);
		m_pFilter->m_bValidBuffer = false;
		return; // need some test
	}

	if (changeTextures) {
		UpdateTexParams(m_srcParams.CDepth);
		if (m_D3D11VP.IsReady()) {
			// update m_D3D11OutputFmt
			EXECUTE_ASSERT(S_OK == InitializeD3D11VP(m_srcParams, m_srcWidth, m_srcHeight));
		}
		UpdateTexures();
		UpdatePostScaleTexures();
	}

	if (changeConvertShader) {
		UpdateConvertColorShader();
	}

	if (changeUpscalingShader) {
		UpdateUpscalingShaders();
	}
	if (changeDowndcalingShader) {
		UpdateDownscalingShaders();
	}

	if (changeNumTextures) {
		UpdatePostScaleTexures();
	}

	if (changeResizeStats) {
		SetGraphSize();
	}

	if (changeD3D11Subtitle) {
		if (m_pFilter->m_pSubCallBack)
			m_pFilter->m_pSubCallBack->SetDevice(nullptr);
		if (m_pFilter->m_pSub11CallBack)
			m_pFilter->m_pSub11CallBack->SetDevice(nullptr);
		m_bCallbackDeviceIsSet = false;
		ReleaseSwapChain();
		EXECUTE_ASSERT(S_OK == m_pFilter->Init(true));
		return;
	}
	

	UpdateStatsStatic();
}

void CDX11VideoProcessor::SetRotation(int value)
{
	m_iRotation = value;
	if (m_D3D11VP.IsReady()) {
		m_D3D11VP.SetRotation(static_cast<D3D11_VIDEO_PROCESSOR_ROTATION>(value / 90));
	}
}

void CDX11VideoProcessor::Flush()
{
	if (m_D3D11VP.IsReady()) {
		m_D3D11VP.ResetFrameOrder();
	}

	m_rtStart = 0;
}

void CDX11VideoProcessor::ClearPreScaleShaders()
{
	for (auto& pExtShader : m_pPreScaleShaders) {
		pExtShader.shader.Release();
	}
	m_pPreScaleShaders.clear();
	DLog(L"CDX11VideoProcessor::ClearPreScaleShaders().");
}


void CDX11VideoProcessor::ClearPostScaleShaders()
{
	for (auto& pExtShader : m_pPostScaleShaders) {
		pExtShader.shader.Release();
	}
	m_pPostScaleShaders.clear();
	//UpdateStatsPostProc();
	DLog(L"CDX11VideoProcessor::ClearPostScaleShaders().");
}

HRESULT CDX11VideoProcessor::AddPreScaleShader(const std::wstring& name, const std::string& srcCode)
{
#ifdef _DEBUG
	if (!m_pDevice) {
		return E_ABORT;
	}

	ID3DBlob* pShaderCode = nullptr;
	HRESULT hr = CompileShader(srcCode, nullptr, "ps_4_0", &pShaderCode);
	if (S_OK == hr) {
		m_pPreScaleShaders.emplace_back();
		hr = m_pDevice->CreatePixelShader((const DWORD*)pShaderCode->GetBufferPointer(), pShaderCode->GetBufferSize(), nullptr, &m_pPreScaleShaders.back().shader);
		if (S_OK == hr) {
			m_pPreScaleShaders.back().name = name;
			//UpdatePreScaleTexures(); //TODO
			DLog(L"CDX11VideoProcessor::AddPreScaleShader() : \"{}\" pixel shader added successfully.", name);
		}
		else {
			DLog(L"CDX11VideoProcessor::AddPreScaleShader() : create pixel shader \"{}\" FAILED!", name);
			m_pPreScaleShaders.pop_back();
		}
		pShaderCode->Release();
	}

	if (S_OK == hr && m_D3D11VP.IsReady() && m_bVPScaling) {
		return S_FALSE;
	}

	return hr;
#else
	return E_NOTIMPL;
#endif
}

HRESULT CDX11VideoProcessor::AddPostScaleShader(const std::wstring& name, const std::string& srcCode)
{
	if (!m_pDevice) {
		return E_ABORT;
	}

	ID3DBlob* pShaderCode = nullptr;
	HRESULT hr = CompileShader(srcCode, nullptr, "ps_4_0", &pShaderCode);
	if (S_OK == hr) {
		m_pPostScaleShaders.emplace_back();
		hr = m_pDevice->CreatePixelShader((const DWORD*)pShaderCode->GetBufferPointer(), pShaderCode->GetBufferSize(), nullptr, &m_pPostScaleShaders.back().shader);
		if (S_OK == hr) {
			m_pPostScaleShaders.back().name = name;
			UpdatePostScaleTexures();
			DLog(L"CDX11VideoProcessor::AddPostScaleShader() : \"{}\" pixel shader added successfully.", name);
		}
		else {
			DLog(L"CDX11VideoProcessor::AddPostScaleShader() : create pixel shader \"{}\" FAILED!", name);
			m_pPostScaleShaders.pop_back();
		}
		pShaderCode->Release();
	}

	return hr;
}

void CDX11VideoProcessor::UpdateStatsPresent()
{
	DXGI_SWAP_CHAIN_DESC1 swapchain_desc;
	if (m_pDXGISwapChain1 && S_OK == m_pDXGISwapChain1->GetDesc1(&swapchain_desc)) {
		m_strStatsPresent.assign(L"\nPresentation  : ");
		if (m_bVBlankBeforePresent && m_pDXGIOutput) {
			m_strStatsPresent.append(L"wait VBlank, ");
		}
		switch (swapchain_desc.SwapEffect) {
		case DXGI_SWAP_EFFECT_DISCARD:
			m_strStatsPresent.append(L"Discard");
			break;
		case DXGI_SWAP_EFFECT_SEQUENTIAL:
			m_strStatsPresent.append(L"Sequential");
			break;
		case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
			m_strStatsPresent.append(L"Flip sequential");
			break;
		case DXGI_SWAP_EFFECT_FLIP_DISCARD:
			m_strStatsPresent.append(L"Flip discard");
			break;
		}
		m_strStatsPresent.append(L", ");
		m_strStatsPresent.append(DXGIFormatToString(swapchain_desc.Format));
	}
}

void CDX11VideoProcessor::UpdateStatsStatic()
{
	if (m_srcParams.cformat) {
		m_strStatsHeader = fmt::format(L"MPC VR {}, Direct3D 11", _CRT_WIDE(VERSION_STR));

		UpdateStatsInputFmt();

		m_strStatsVProc.assign(L"\nVideoProcessor: ");
		if (m_D3D11VP.IsReady()) {
			m_strStatsVProc += fmt::format(L"D3D11 VP, output to {}", DXGIFormatToString(m_D3D11OutputFmt));
		} else {
			m_strStatsVProc.append(L"Shaders");
			if (m_srcParams.Subsampling == 420 || m_srcParams.Subsampling == 422) {
				m_strStatsVProc.append(L", Chroma scaling: ");
				switch (m_iChromaScaling) {
				case CHROMA_Nearest:
					m_strStatsVProc.append(L"Nearest-neighbor");
					break;
				case CHROMA_Bilinear:
					m_strStatsVProc.append(L"Bilinear");
					break;
				case CHROMA_CatmullRom:
					m_strStatsVProc.append(L"Catmull-Rom");
					break;
				}
			}
		}
		m_strStatsVProc += fmt::format(L"\nInternalFormat: {}", DXGIFormatToString(m_InternalTexFmt));

		if (SourceIsHDR()) {
			m_strStatsHDR.assign(L"\nHDR processing: ");
			if (m_bHdrPassthroughSupport && m_bHdrPassthrough) {
				m_strStatsHDR.append(L"Passthrough");
				if (m_lastHdr10.bValid) {
					m_strStatsHDR += fmt::format(L", {} nits", m_lastHdr10.hdr10.MaxMasteringLuminance / 10000);
				}
			} else if (m_bConvertToSdr) {
				m_strStatsHDR.append(L"Convert to SDR");
			} else {
				m_strStatsHDR.append(L"Not used");
			}
		} else {
			m_strStatsHDR.clear();
		}

		UpdateStatsPresent();
	}
	else {
		m_strStatsHeader = L"Error";
		m_strStatsVProc.clear();
		m_strStatsInputFmt.clear();
		//m_strStatsPostProc.clear();
		m_strStatsHDR.clear();
		m_strStatsPresent.clear();
	}
}

/*
void CDX11VideoProcessor::UpdateStatsPostProc()
{
	if (m_strCorrection || m_pPostScaleShaders.size() || m_bFinalPass) {
		m_strStatsPostProc.assign(L"\nPostProcessing:");
		if (m_strCorrection) {
			m_strStatsPostProc += fmt::format(L" {},", m_strCorrection);
		}
		if (m_pPostScaleShaders.size()) {
			m_strStatsPostProc += fmt::format(L" shaders[{}],", m_pPostScaleShaders.size());
		}
		if (m_bFinalPass) {
			m_strStatsPostProc.append(L" dither");
		}
		str_trim_end(m_strStatsPostProc, ',');
	}
	else {
		m_strStatsPostProc.clear();
	}
}
*/

HRESULT CDX11VideoProcessor::DrawStats(ID3D11Texture2D* pRenderTarget)
{
	if (m_windowRect.IsRectEmpty()) {
		return E_ABORT;
	}

	std::wstring str;
	str.reserve(700);
	str.assign(m_strStatsHeader);
	str.append(m_strStatsDispInfo);
	str += fmt::format(L"\nGraph. Adapter: {}", m_strAdapterDescription);

	wchar_t frametype = (m_SampleFormat != D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE) ? 'i' : 'p';
	str += fmt::format(
		L"\nFrame rate    : {:7.3f}{},{:7.3f}",
		m_pFilter->m_FrameStats.GetAverageFps(),
		frametype,
		m_pFilter->m_DrawStats.GetAverageFps()
	);

	str.append(m_strStatsInputFmt);
	str.append(m_strStatsVProc);

	const int dstW = m_videoRect.Width();
	const int dstH = m_videoRect.Height();
	if (m_iRotation) {
		str += fmt::format(L"\nScaling       : {}x{} r{}\u00B0> {}x{}", m_srcRectWidth, m_srcRectHeight, m_iRotation, dstW, dstH);
	} else {
		str += fmt::format(L"\nScaling       : {}x{} -> {}x{}", m_srcRectWidth, m_srcRectHeight, dstW, dstH);
	}
	if (m_srcRectWidth != dstW || m_srcRectHeight != dstH) {
		if (m_D3D11VP.IsReady() && m_bVPScaling && !m_bVPScalingUseShaders) {
			str.append(L" D3D11");
		} else {
			str += L' ';
			if (m_strShaderX) {
				str.append(m_strShaderX);
				if (m_strShaderY && m_strShaderY != m_strShaderX) {
					str += L'/';
					str.append(m_strShaderY);
				}
			} else if (m_strShaderY) {
				str.append(m_strShaderY);
			}
		}
	}

	if (m_strCorrection || m_pPostScaleShaders.size() || m_bDitherUsed) {
		str.append(L"\nPostProcessing:");
		if (m_strCorrection) {
			str += fmt::format(L" {},", m_strCorrection);
		}
		if (m_pPostScaleShaders.size()) {
			str += fmt::format(L" shaders[{}],", m_pPostScaleShaders.size());
		}
		if (m_bDitherUsed) {
			str.append(L" dither");
		}
		str_trim_end(str, ',');
	}
	str.append(m_strStatsHDR);
	str.append(m_strStatsPresent);

	str += fmt::format(L"\nFrames: {:5}, skipped: {}/{}, failed: {}",
		m_pFilter->m_FrameStats.GetFrames(), m_pFilter->m_DrawStats.m_dropped, m_RenderStats.dropped2, m_RenderStats.failed);
	str += fmt::format(L"\nTimes(ms): Copy{:3}, Paint{:3} [DX9Subs{:3}], Present{:3}",
		m_RenderStats.copyticks    * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.paintticks   * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.substicks    * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.presentticks * 1000 / GetPreciseTicksPerSecondI());
	str += fmt::format(L"\nSync offset   : {:+3} ms", (m_RenderStats.syncoffset + 5000) / 10000);

#if SYNC_OFFSET_EX
	{
		const auto [so_min, so_max] = m_Syncs.MinMax();
		const auto [sod_min, sod_max] = m_SyncDevs.MinMax();
		str += fmt::format(L", range[{:+3.0f};{:+3.0f}], max change{:+3.0f}/{:+3.0f}",
			so_min / 10000.0f,
			so_max / 10000.0f,
			sod_min / 10000.0f,
			sod_max / 10000.0f);
	}
#endif
#if TEST_TICKS
	str += fmt::format(L"\n1:{:6.3f}, 2:{:6.3f}, 3:{:6.3f}, 4:{:6.3f}, 5:{:6.3f}, 6:{:6.3f} ms",
		m_RenderStats.t1 * 1000 / GetPreciseTicksPerSecond(),
		m_RenderStats.t2 * 1000 / GetPreciseTicksPerSecond(),
		m_RenderStats.t3 * 1000 / GetPreciseTicksPerSecond(),
		m_RenderStats.t4 * 1000 / GetPreciseTicksPerSecond(),
		m_RenderStats.t5 * 1000 / GetPreciseTicksPerSecond(),
		m_RenderStats.t6 * 1000 / GetPreciseTicksPerSecond());
#endif

	ID3D11RenderTargetView* pRenderTargetView = nullptr;
	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (S_OK == hr) {
		SIZE rtSize = m_windowRect.Size();

		m_StatsBackground.Draw(pRenderTargetView, rtSize);

		hr = m_Font3D.Draw2DText(pRenderTargetView, rtSize, m_StatsTextPoint.x, m_StatsTextPoint.y, m_dwStatsTextColor, str.c_str());
		static int col = m_StatsRect.right;
		if (--col < m_StatsRect.left) {
			col = m_StatsRect.right;
		}
		m_Rect3D.Set({ col, m_StatsRect.bottom - 11, col + 5, m_StatsRect.bottom - 1 }, rtSize, D3DCOLOR_XRGB(128, 255, 128));
		m_Rect3D.Draw(pRenderTargetView, rtSize);


		if (CheckGraphPlacement()) {
			m_Underlay.Draw(pRenderTargetView, rtSize);

			m_Lines.Draw();

			m_SyncLine.ClearPoints(rtSize);
			m_SyncLine.AddGFPoints(m_GraphRect.left, m_Xstep, m_Yaxis, m_Yscale,
				m_Syncs.Data(), m_Syncs.OldestIndex(), m_Syncs.Size(), D3DCOLOR_XRGB(100, 200, 100));
			m_SyncLine.UpdateVertexBuffer();
			m_SyncLine.Draw();
		}
		pRenderTargetView->Release();
	}

	return hr;
}

// IMFVideoProcessor

STDMETHODIMP CDX11VideoProcessor::SetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *pValues)
{
	CheckPointer(pValues, E_POINTER);
	if (m_srcParams.cformat == CF_NONE) {
		return MF_E_TRANSFORM_TYPE_NOT_SET;
	}

	if (dwFlags & DXVA2_ProcAmp_Brightness) {
		m_DXVA2ProcAmpValues.Brightness.ll = std::clamp(pValues->Brightness.ll, m_DXVA2ProcAmpRanges[0].MinValue.ll, m_DXVA2ProcAmpRanges[0].MaxValue.ll);
	}
	if (dwFlags & DXVA2_ProcAmp_Contrast) {
		m_DXVA2ProcAmpValues.Contrast.ll = std::clamp(pValues->Contrast.ll, m_DXVA2ProcAmpRanges[1].MinValue.ll, m_DXVA2ProcAmpRanges[1].MaxValue.ll);
	}
	if (dwFlags & DXVA2_ProcAmp_Hue) {
		m_DXVA2ProcAmpValues.Hue.ll = std::clamp(pValues->Hue.ll, m_DXVA2ProcAmpRanges[2].MinValue.ll, m_DXVA2ProcAmpRanges[2].MaxValue.ll);
	}
	if (dwFlags & DXVA2_ProcAmp_Saturation) {
		m_DXVA2ProcAmpValues.Saturation.ll = std::clamp(pValues->Saturation.ll, m_DXVA2ProcAmpRanges[3].MinValue.ll, m_DXVA2ProcAmpRanges[3].MaxValue.ll);
	}

	if (dwFlags & DXVA2_ProcAmp_Mask) {
		CAutoLock cRendererLock(&m_pFilter->m_RendererLock);

		m_D3D11VP.SetProcAmpValues(&m_DXVA2ProcAmpValues);

		if (!m_D3D11VP.IsReady()) {
			SetShaderConvertColorParams();
		}
	}

	return S_OK;
}

// IMFVideoMixerBitmap

STDMETHODIMP CDX11VideoProcessor::SetAlphaBitmap(const MFVideoAlphaBitmap *pBmpParms)
{
	CheckPointer(pBmpParms, E_POINTER);
	CAutoLock cRendererLock(&m_pFilter->m_RendererLock);

	HRESULT hr = S_FALSE;

	if (pBmpParms->GetBitmapFromDC && pBmpParms->bitmap.hdc) {
		HBITMAP hBitmap = (HBITMAP)GetCurrentObject(pBmpParms->bitmap.hdc, OBJ_BITMAP);
		if (!hBitmap) {
			return E_INVALIDARG;
		}
		DIBSECTION info = {0};
		if (!::GetObjectW(hBitmap, sizeof(DIBSECTION), &info)) {
			return E_INVALIDARG;
		}
		BITMAP& bm = info.dsBm;
		if (!bm.bmWidth || !bm.bmHeight || bm.bmBitsPixel != 32 || !bm.bmBits) {
			return E_INVALIDARG;
		}

		hr = m_TexAlphaBitmap.CheckCreate(m_pDevice, DXGI_FORMAT_B8G8R8A8_UNORM, bm.bmWidth, bm.bmHeight, Tex2D_DefaultShader);
		DLogIf(FAILED(hr), L"CDX11VideoProcessor::SetAlphaBitmap() : CheckCreate() failed with error {}", HR2Str(hr));
		if (S_OK == hr) {
			m_pDeviceContext->UpdateSubresource(m_TexAlphaBitmap.pTexture, 0, nullptr, bm.bmBits, bm.bmWidthBytes, 0);
		}
	} else {
		return E_INVALIDARG;
	}

	m_bAlphaBitmapEnable = SUCCEEDED(hr) && m_TexAlphaBitmap.pShaderResource;

	if (m_bAlphaBitmapEnable) {
		m_AlphaBitmapRectSrc = { 0, 0, (LONG)m_TexAlphaBitmap.desc.Width, (LONG)m_TexAlphaBitmap.desc.Height };
		m_AlphaBitmapNRectDest = { 0, 0, 1, 1 };

		m_pAlphaBitmapVertex.Release();

		hr = UpdateAlphaBitmapParameters(&pBmpParms->params);
	}

	return hr;
}

STDMETHODIMP CDX11VideoProcessor::UpdateAlphaBitmapParameters(const MFVideoAlphaBitmapParams *pBmpParms)
{
	CheckPointer(pBmpParms, E_POINTER);
	CAutoLock cRendererLock(&m_pFilter->m_RendererLock);

	if (m_bAlphaBitmapEnable) {
		if (pBmpParms->dwFlags & MFVideoAlphaBitmap_SrcRect) {
			m_AlphaBitmapRectSrc = pBmpParms->rcSrc;
			m_pAlphaBitmapVertex.Release();
		}
		if (!m_pAlphaBitmapVertex) {
			HRESULT hr = CreateVertexBuffer(m_pDevice, &m_pAlphaBitmapVertex, m_TexAlphaBitmap.desc.Width, m_TexAlphaBitmap.desc.Height, m_AlphaBitmapRectSrc, 0, false);
			if (FAILED(hr)) {
				m_bAlphaBitmapEnable = false;
				return hr;
			}
		}
		if (pBmpParms->dwFlags & MFVideoAlphaBitmap_DestRect) {
			m_AlphaBitmapNRectDest = pBmpParms->nrcDest;
		}
		DWORD validFlags = MFVideoAlphaBitmap_SrcRect|MFVideoAlphaBitmap_DestRect;

		return ((pBmpParms->dwFlags & validFlags) == validFlags) ? S_OK : S_FALSE;
	} else {
		return MF_E_NOT_INITIALIZED;
	}
}

void CDX11VideoProcessor::SetCallbackDevice(const bool bChangeDevice/* = false*/)
{
	if ((!m_bCallbackDeviceIsSet || bChangeDevice) &&  m_bD3D11Subtitle && m_pDevice && m_pFilter->m_pSub11CallBack) {
		m_bCallbackDeviceIsSet = SUCCEEDED(m_pFilter->m_pSub11CallBack->SetDevice(m_pDevice));
	}
	else if ((!m_bCallbackDeviceIsSet || bChangeDevice) && m_pD3DDevEx && !m_bD3D11Subtitle &&m_pFilter->m_pSubCallBack) {
		m_bCallbackDeviceIsSet = SUCCEEDED(m_pFilter->m_pSubCallBack->SetDevice(m_pD3DDevEx));
	}
}
