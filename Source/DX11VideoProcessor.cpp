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

#include "stdafx.h"
#include <uuids.h>
#include <d3d9.h>
#include <mfapi.h> // for MR_BUFFER_SERVICE
#include <Mferror.h>
#include <Mfidl.h>
#include <dwmapi.h>
#include <cmath>
#include "Helper.h"
#include "Time.h"
#include "resource.h"
#include "VideoRenderer.h"
#include "DX11VideoProcessor.h"
#include "./Include/ID3DVideoMemoryConfiguration.h"

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

HRESULT CreateVertexBuffer(ID3D11Device* pDevice, ID3D11Buffer** ppVertexBuffer, const UINT srcW, const UINT srcH, const RECT& srcRect)
{
	ASSERT(ppVertexBuffer);
	ASSERT(*ppVertexBuffer == nullptr);

	const float src_dx = 1.0f / srcW;
	const float src_dy = 1.0f / srcH;
	const float src_l = src_dx * srcRect.left;
	const float src_r = src_dx * srcRect.right;
	const float src_t = src_dy * srcRect.top;
	const float src_b = src_dy * srcRect.bottom;

	VERTEX Vertices[6] = {
		// Vertices for drawing whole texture
		// |\
		// |_\ lower left triangle
		{ DirectX::XMFLOAT3(-1, -1, 0), DirectX::XMFLOAT2(src_l, src_b) },
		{ DirectX::XMFLOAT3(-1, +1, 0), DirectX::XMFLOAT2(src_l, src_t) },
		{ DirectX::XMFLOAT3(+1, -1, 0), DirectX::XMFLOAT2(src_r, src_b) },
		// ___
		// \ |
		//  \| upper right triangle
		{ DirectX::XMFLOAT3(+1, -1, 0), DirectX::XMFLOAT2(src_r, src_b) },
		{ DirectX::XMFLOAT3(-1, +1, 0), DirectX::XMFLOAT2(src_l, src_t) },
		{ DirectX::XMFLOAT3(+1, +1, 0), DirectX::XMFLOAT2(src_r, src_t) },
	};

	D3D11_BUFFER_DESC BufferDesc = { sizeof(Vertices), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
	D3D11_SUBRESOURCE_DATA InitData = { Vertices, 0, 0 };

	HRESULT hr = pDevice->CreateBuffer(&BufferDesc, &InitData, ppVertexBuffer);
	DLogIf(FAILED(hr), L"CreateVertexBuffer2() : CreateBuffer() failed with error %s", HR2Str(hr));

	return hr;
}

void TextureBlt11(
	ID3D11DeviceContext* pDeviceContext,
	ID3D11RenderTargetView* pRenderTargetView, D3D11_VIEWPORT& viewport,
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
	pDeviceContext->RSSetViewports(1, &viewport);
	pDeviceContext->OMSetBlendState(nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
	pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
	pDeviceContext->VSSetShader(pVertexShader, nullptr, 0);
	pDeviceContext->PSSetShader(pPixelShader, nullptr, 0);
	pDeviceContext->PSSetShaderResources(0, 1, &pShaderResourceViews);
	pDeviceContext->PSSetSamplers(0, 1, &pSampler);
	pDeviceContext->PSSetConstantBuffers(0, pConstantBuffer ? 1 : 0, &pConstantBuffer);
	pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pDeviceContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &Stride, &Offset);

	// Draw textured quad onto render target
	pDeviceContext->Draw(6, 0);
}

HRESULT CDX11VideoProcessor::TextureCopyRect(Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& destRect, ID3D11PixelShader* pPixelShader, ID3D11Buffer* pConstantBuffer)
{
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;
	CComPtr<ID3D11Buffer> pVertexBuffer;

	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (FAILED(hr)) {
		DLog(L"TextureCopyRect() : CreateRenderTargetView() failed with error %s", HR2Str(hr));
		return hr;
	}

	hr = CreateVertexBuffer(m_pDevice, &pVertexBuffer, Tex.desc.Width, Tex.desc.Height, srcRect);
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

	TextureBlt11(m_pDeviceContext, pRenderTargetView, VP, m_pVS_Simple, pPixelShader, Tex.pShaderResource, m_pSamplerPoint, pConstantBuffer, pVertexBuffer);

	return hr;
}

HRESULT CDX11VideoProcessor::TextureResizeShader(Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget, const CRect& srcRect, const CRect& dstRect, ID3D11PixelShader* pPixelShader)
{
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;
	CComPtr<ID3D11Buffer> pVertexBuffer;
	CComPtr<ID3D11Buffer> pConstantBuffer;

	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (FAILED(hr)) {
		DLog(L"TextureResizeShader() : CreateRenderTargetView() failed with error %s", HR2Str(hr));
		return hr;
	}

	hr = CreateVertexBuffer(m_pDevice, &pVertexBuffer, Tex.desc.Width, Tex.desc.Height, srcRect);
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

	TextureBlt11(m_pDeviceContext, pRenderTargetView, VP, m_pVS_Simple, pPixelShader, Tex.pShaderResource, m_pSamplerPoint, pConstantBuffer, pVertexBuffer);

	return hr;
}

// CDX11VideoProcessor

static UINT GetAdapter(HWND hWnd, IDXGIFactory1* pDXGIFactory, IDXGIAdapter** ppDXGIAdapter)
{
	*ppDXGIAdapter = nullptr;

	CheckPointer(pDXGIFactory, 0);

	const HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);

	UINT i = 0;
	IDXGIAdapter* pDXGIAdapter = nullptr;
	while (SUCCEEDED(pDXGIFactory->EnumAdapters(i, &pDXGIAdapter))) {
		UINT k = 0;
		IDXGIOutput* pDXGIOutput = nullptr;
		while (SUCCEEDED(pDXGIAdapter->EnumOutputs(k, &pDXGIOutput))) {
			DXGI_OUTPUT_DESC desc = {};
			if (SUCCEEDED(pDXGIOutput->GetDesc(&desc))) {
				if (desc.Monitor == hMonitor) {
					SAFE_RELEASE(pDXGIOutput);
					*ppDXGIAdapter = pDXGIAdapter;
					return i;
				}
			}
			SAFE_RELEASE(pDXGIOutput);
			k++;
		}

		SAFE_RELEASE(pDXGIAdapter);
		i++;
	}

	return 0;
}

CDX11VideoProcessor::CDX11VideoProcessor(CMpcVideoRenderer* pFilter)
	: m_pFilter(pFilter)
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
		D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1
	};
	D3D_FEATURE_LEVEL featurelevel;

	ID3D11Device *pDevice = nullptr;
	ID3D11DeviceContext *pDeviceContext = nullptr;

	HRESULT hr = m_D3D11CreateDevice(
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
		&pDeviceContext);
	SAFE_RELEASE(pDXGIAdapter);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::Init() : D3D11CreateDevice() failed with error %s", HR2Str(hr));
		return hr;
	}

	DLog(L"CDX11VideoProcessor::Init() : D3D11CreateDevice() successfully with feature level %d.%d", (featurelevel >> 12), (featurelevel >> 8) & 0xF);

	hr = SetDevice(pDevice, pDeviceContext);
	if (S_OK == hr) {
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
	m_pFilter->ResetStreamingTimes2();
	m_RenderStats.Reset();

	m_pSrcTexture2D.Release();
	m_TexSrcCPU.Release();
	m_TexVideo.Release();
	m_TexCorrection.Release();
	m_TexConvert.Release();
	m_TexResize.Release();

	SAFE_RELEASE(m_PSConvColorData.pConstants);

	m_pInputView.Release();
	m_pVideoProcessor.Release();
	m_pVideoProcessorEnum.Release();

	m_srcDXGIFormat = DXGI_FORMAT_UNKNOWN;
	m_srcWidth  = 0;
	m_srcHeight = 0;
	m_TextureWidth  = 0;
	m_TextureHeight = 0;
}

void CDX11VideoProcessor::ReleaseDevice()
{
	ReleaseVP();
	m_pVideoDevice.Release();

	m_TexOSD.Release();
	m_pPSCorrection.Release();
	m_pPSConvertColor.Release();

	m_pShaderUpscaleX.Release();
	m_pShaderUpscaleY.Release();
	m_pShaderDownscaleX.Release();
	m_pShaderDownscaleY.Release();
	m_strShaderUpscale   = nullptr;
	m_strShaderDownscale = nullptr;

	m_pVideoContext.Release();

	m_pInputLayout.Release();
	m_pVS_Simple.Release();
	m_pPS_Simple.Release();
	SAFE_RELEASE(m_pSamplerPoint);
	m_pAlphaBlendState.Release();
	SAFE_RELEASE(m_pFullFrameVertexBuffer);

	m_pDeviceContext.Release();

#if (0 && _DEBUG)
	if (m_pDevice) {
		ID3D11Debug* pDebugDevice = nullptr;
		HRESULT hr2 = m_pDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)(&pDebugDevice));
		if (S_OK == hr2) {
			hr2 = pDebugDevice->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
			ASSERT(S_OK == hr2);
		}
		SAFE_RELEASE(pDebugDevice);
	}
#endif

	m_pDevice.Release();
}

HRESULT CDX11VideoProcessor::GetDataFromResource(LPVOID& data, DWORD& size, UINT resid)
{
	static const HMODULE hModule = (HMODULE)&__ImageBase;

	HRSRC hrsrc = FindResourceW(hModule, MAKEINTRESOURCEW(resid), L"SHADER");
	if (!hrsrc) {
		return E_INVALIDARG;
	}
	HGLOBAL hGlobal = LoadResource(hModule, hrsrc);
	if (!hGlobal) {
		return E_FAIL;
	}
	size = SizeofResource(hModule, hrsrc);
	if (!size) {
		return E_FAIL;
	}
	data = LockResource(hGlobal);
	if (!data) {
		return E_FAIL;
	}

	return S_OK;
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

	HRESULT hr = m_pDevice->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&m_pVideoDevice);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::SetDevice() : QueryInterface(ID3D11VideoDevice) failed with error %s", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}

	hr = m_pDeviceContext->QueryInterface(__uuidof(ID3D11VideoContext), (void**)&m_pVideoContext);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::SetDevice() : QueryInterface(ID3D11VideoContext) failed with error %s", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}

	D3D11_SAMPLER_DESC SampDesc = {};
	SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SampDesc.MinLOD = 0;
	SampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateSamplerState(&SampDesc, &m_pSamplerPoint));

	D3D11_BLEND_DESC bdesc = {};
	bdesc.RenderTarget[0].BlendEnable = TRUE;
	bdesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	bdesc.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_ALPHA;
	bdesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	bdesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	bdesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	bdesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	bdesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateBlendState(&bdesc, &m_pAlphaBlendState));

	EXECUTE_ASSERT(S_OK == CreateVertexBuffer(m_pDevice, &m_pFullFrameVertexBuffer, 1, 1, CRect(0, 0, 1, 1)));

	LPVOID data;
	DWORD size;
	EXECUTE_ASSERT(S_OK == GetDataFromResource(data, size, IDF_VSH11_SIMPLE));
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateVertexShader(data, size, nullptr, &m_pVS_Simple));

	D3D11_INPUT_ELEMENT_DESC Layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateInputLayout(Layout, std::size(Layout), data, size, &m_pInputLayout));
	m_pDeviceContext->IASetInputLayout(m_pInputLayout);

	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPS_Simple, IDF_PSH11_SIMPLE));

	CComPtr<IDXGIDevice> pDXGIDevice;
	hr = m_pDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice);
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
	hr = pDXGIAdapter->GetParent(__uuidof(IDXGIFactory1), (void**)&pDXGIFactory1);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::SetDevice() : GetParent(IDXGIFactory1) failed with error %s", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}

	hr = pDXGIFactory1->QueryInterface(__uuidof(IDXGIFactory2), (void**)&m_pDXGIFactory2);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::SetDevice() : QueryInterface(IDXGIFactory2) failed with error %s", HR2Str(hr));
		ReleaseDevice();
		return hr;
	}

	if (m_inputMT.IsValid()) {
		if (!InitMediaType(&m_inputMT)) {
			ReleaseDevice();
			return E_FAIL;
		}
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
		DLog(L"Graphics adapter: %s", m_strAdapterDescription.GetString());
	}

#ifdef DEBUG
	D3D11_VIDEO_PROCESSOR_CONTENT_DESC ContentDesc = { D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE, {}, 1920, 1080, {}, 1920, 1080, D3D11_VIDEO_USAGE_PLAYBACK_NORMAL };
	CComPtr<ID3D11VideoProcessorEnumerator> pVideoProcEnum;
	if (S_OK == m_pVideoDevice->CreateVideoProcessorEnumerator(&ContentDesc, &pVideoProcEnum)) {
		CString input = L"Supported input DXGI formats (for 1080p):";
		CString output = L"Supported output DXGI formats (for 1080p):";
		for (int fmt = DXGI_FORMAT_R32G32B32A32_TYPELESS; fmt <= DXGI_FORMAT_B4G4R4A4_UNORM; fmt++) {
			UINT uiFlags;
			if (S_OK == pVideoProcEnum->CheckVideoProcessorFormat((DXGI_FORMAT)fmt, &uiFlags)) {
				if (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) {
					input.AppendFormat(L"\n  %s", DXGIFormatToString((DXGI_FORMAT)fmt));
				}
				if (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) {
					output.AppendFormat(L"\n  %s", DXGIFormatToString((DXGI_FORMAT)fmt));
				}
			}
		}
		DLog(input);
		DLog(output);
	}
#endif

	HRESULT hr2 = m_TexOSD.Create(m_pDevice, DXGI_FORMAT_B8G8R8A8_UNORM, STATS_W, STATS_H, Tex2D_DefaultShaderRTargetGDI);
	ASSERT(S_OK == hr);

	return hr;
}

HRESULT CDX11VideoProcessor::InitSwapChain()
{
	DLog(L"CDX11VideoProcessor::InitSwapChain()");
	CheckPointer(m_pDXGIFactory2, E_FAIL);

	m_pDXGISwapChain1.Release();

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width  = m_windowRect.Width();
	desc.Height = m_windowRect.Height();
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // the most common swap chain format
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	if (m_iSwapEffect == SWAPEFFECT_Flip) {
		desc.BufferCount = 2;
		desc.Scaling = DXGI_SCALING_NONE;
#if VER_PRODUCTBUILD >= 10000
		desc.SwapEffect = IsWindows10OrGreater() ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
#else
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
#endif
	} else { // default SWAPEFFECT_Discard
		desc.BufferCount = 1;
		desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	}
	desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	HRESULT hr = m_pDXGIFactory2->CreateSwapChainForHwnd(m_pDevice, m_hWnd, &desc, nullptr, nullptr, &m_pDXGISwapChain1);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitSwapChain() : CreateSwapChainForHwnd() failed with error %s", HR2Str(hr));
	}

	return hr;
}

BOOL CDX11VideoProcessor::VerifyMediaType(const CMediaType* pmt)
{
	auto FmtConvParams = GetFmtConvParams(pmt->subtype);
	if (!FmtConvParams || FmtConvParams->VP11Format == DXGI_FORMAT_UNKNOWN && FmtConvParams->DX11Format == DXGI_FORMAT_UNKNOWN) {
		return FALSE;
	}

	const BITMAPINFOHEADER* pBIH = GetBIHfromVIHs(pmt);
	if (!pBIH) {
		return FALSE;
	}

	if (pBIH->biWidth <= 0 || !pBIH->biHeight || (!pBIH->biSizeImage && pBIH->biCompression != BI_RGB)) {
		return FALSE;
	}

	if (FmtConvParams->Subsampling == 420 && ((pBIH->biWidth & 1) || (pBIH->biHeight & 1))) {
		return FALSE;
	}
	if (FmtConvParams->Subsampling == 422 && (pBIH->biWidth & 1)) {
		return FALSE;
	}

	return TRUE;
}

BOOL CDX11VideoProcessor::GetAlignmentSize(const CMediaType& mt, SIZE& Size)
{
	if (InitMediaType(&mt)) {
		if (m_TexSrcCPU.pTexture) {
			UINT RowPitch = 0;
			D3D11_MAPPED_SUBRESOURCE mappedResource = {};
			if (SUCCEEDED(m_pDeviceContext->Map(m_TexSrcCPU.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
				RowPitch = mappedResource.RowPitch;
				m_pDeviceContext->Unmap(m_TexSrcCPU.pTexture, 0);
			}

			if (RowPitch) {
				auto FmtConvParams = GetFmtConvParams(mt.subtype);

				Size.cx = RowPitch / FmtConvParams->Packsize;

				if (FmtConvParams->CSType == CS_RGB) {
					Size.cy = -abs(Size.cy);
				} else {
					Size.cy = abs(Size.cy); // need additional checks
				}

				return TRUE;
			}
		}
	}

	return FALSE;
}

BOOL CDX11VideoProcessor::InitMediaType(const CMediaType* pmt)
{
	DLog(L"CDX11VideoProcessor::InitMediaType()");

	if (!VerifyMediaType(pmt)) {
		return FALSE;
	}

	ReleaseVP();

	auto FmtConvParams = GetFmtConvParams(pmt->subtype);
	const GUID SubType = pmt->subtype;
	const BITMAPINFOHEADER* pBIH = nullptr;

	if (pmt->formattype == FORMAT_VideoInfo2) {
		const VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pmt->pbFormat;
		pBIH = &vih2->bmiHeader;
		m_srcRect = vih2->rcSource;
		m_trgRect = vih2->rcTarget;
		m_srcAspectRatioX = vih2->dwPictAspectRatioX;
		m_srcAspectRatioY = vih2->dwPictAspectRatioY;
		if (FmtConvParams->CSType == CS_YUV && (vih2->dwControlFlags & (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT))) {
			m_srcExFmt.value = vih2->dwControlFlags;
			m_srcExFmt.SampleFormat = AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT; // ignore other flags
		} else {
			m_srcExFmt.value = 0; // ignore color info for RGB
		}
		m_bInterlaced = (vih2->dwInterlaceFlags & AMINTERLACE_IsInterlaced);
	}
	else if (pmt->formattype == FORMAT_VideoInfo) {
		const VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)pmt->pbFormat;
		pBIH = &vih->bmiHeader;
		m_srcRect = vih->rcSource;
		m_trgRect = vih->rcTarget;
		m_srcAspectRatioX = 0;
		m_srcAspectRatioY = 0;
		m_srcExFmt.value = 0;
		m_bInterlaced = 0;
	}
	else {
		return FALSE;
	}

	UINT biWidth  = pBIH->biWidth;
	UINT biHeight = labs(pBIH->biHeight);
	if (pmt->FormatLength() == 112 + sizeof(VR_Extradata)) {
		const VR_Extradata* vrextra = (VR_Extradata*)(pmt->pbFormat + 112);
		if (vrextra->QueryWidth == pBIH->biWidth && vrextra->QueryHeight == pBIH->biHeight && vrextra->Compression == pBIH->biCompression) {
			biWidth  = vrextra->FrameWidth;
			biHeight = abs(vrextra->FrameHeight);
		}
	}

	UINT biSizeImage = pBIH->biSizeImage;
	if (pBIH->biSizeImage == 0 && pBIH->biCompression == BI_RGB) { // biSizeImage may be zero for BI_RGB bitmaps
		biSizeImage = biWidth * biHeight * pBIH->biBitCount / 8;
	}

	if (FmtConvParams->CSType == CS_YUV && m_srcExFmt.VideoTransferMatrix == DXVA2_VideoTransferMatrix_Unknown) {
		if (biWidth <= 1024 && biHeight <= 576) { // SD
			m_srcExFmt.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT601;
		} else { // HD
			m_srcExFmt.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT709;
		}
	}

	if (m_srcRect.IsRectNull()) {
		m_srcRect.SetRect(0, 0, biWidth, biHeight);
	}
	if (m_trgRect.IsRectNull()) {
		m_trgRect.SetRect(0, 0, biWidth, biHeight);
	}

	m_srcRectWidth  = m_srcRect.Width();
	m_srcRectHeight = m_srcRect.Height();

	if (!m_srcAspectRatioX || !m_srcAspectRatioY) {
		const auto gcd = std::gcd(m_srcRectWidth, m_srcRectHeight);
		m_srcAspectRatioX = m_srcRectWidth / gcd;
		m_srcAspectRatioY = m_srcRectHeight / gcd;
	}

	m_pConvertFn = FmtConvParams->Func;
	m_srcPitch   = biSizeImage * 2 / (biHeight * FmtConvParams->PitchCoeff);
	m_srcPitch  &= ~1u;
	if (SubType == MEDIASUBTYPE_NV12 && biSizeImage % 4) {
		m_srcPitch = ALIGN(m_srcPitch, 4);
	}
	if (pBIH->biCompression == BI_RGB && pBIH->biHeight > 0) {
		m_srcPitch = -m_srcPitch;
	}

	UpdateUpscalingShaders();
	UpdateDownscalingShaders();

	m_pPSCorrection.Release();
	m_pPSConvertColor.Release();
	m_PSConvColorData.bEnable = false;

	// D3D11 Video Processor
	if (FmtConvParams->VP11Format != DXGI_FORMAT_UNKNOWN && !(m_VendorId == PCIV_NVIDIA && FmtConvParams->CSType == CS_RGB)) {
		// D3D11 VP does not work correctly if RGB32 with odd frame width (source or target) on Nvidia adapters

		if (S_OK == InitializeD3D11VP(FmtConvParams->VP11Format, biWidth, biHeight, false)) {
			UpdateVideoTex();

			if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084) {
				EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_CORRECTION_ST2084));
			}
			else if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG || m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG_temp) {
				EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_CORRECTION_HLG));
			}
			else if (m_srcExFmt.VideoTransferMatrix == VIDEOTRANSFERMATRIX_YCgCo) {
				EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_CORRECTION_YCGCO));
			}

			m_srcSubType = SubType;
			m_inputMT = *pmt;
			UpdateStatsStatic();

			return TRUE;
		}

		ReleaseVP();
	}

	// Tex Video Processor
	if (FmtConvParams->DX11Format != DXGI_FORMAT_UNKNOWN && S_OK == InitializeTexVP(FmtConvParams->DX11Format, biWidth, biHeight)) {
		if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084) {
			EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSConvertColor, IDF_PSH11_CONVERT_COLOR_ST2084));
		}
		else if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG || m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG_temp) {
			EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSConvertColor, IDF_PSH11_CONVERT_COLOR_HLG));
		}
		else {
			EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSConvertColor, IDF_PSH11_CONVERT_COLOR));
		}

		UpdateCorrectionTex(m_videoRect.Width(), m_videoRect.Height());

		mp_csp_params csp_params;
		set_colorspace(m_srcExFmt, csp_params.color);
		csp_params.gray = FmtConvParams->CSType == CS_GRAY;

		mp_cmat cmatrix;
		mp_get_csp_matrix(csp_params, cmatrix);
		PS_COLOR_TRANSFORM cbuffer = {
			{cmatrix.m[0][0], cmatrix.m[0][1], cmatrix.m[0][2], 0},
			{cmatrix.m[1][0], cmatrix.m[1][1], cmatrix.m[1][2], 0},
			{cmatrix.m[2][0], cmatrix.m[2][1], cmatrix.m[2][2], 0},
			{cmatrix.c[0], cmatrix.c[1], cmatrix.c[2], 0},
		};

		if (SubType == MEDIASUBTYPE_Y410 || SubType == MEDIASUBTYPE_Y416) {
			std::swap(cbuffer.cm_r.x, cbuffer.cm_r.y);
			std::swap(cbuffer.cm_g.x, cbuffer.cm_g.y);
			std::swap(cbuffer.cm_b.x, cbuffer.cm_b.y);
		}

		SAFE_RELEASE(m_PSConvColorData.pConstants);

		D3D11_BUFFER_DESC BufferDesc = {};
		BufferDesc.Usage = D3D11_USAGE_DEFAULT;
		BufferDesc.ByteWidth = sizeof(cbuffer);
		BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		BufferDesc.CPUAccessFlags = 0;
		D3D11_SUBRESOURCE_DATA InitData = { &cbuffer, 0, 0 };
		HRESULT hr = m_pDevice->CreateBuffer(&BufferDesc, &InitData, &m_PSConvColorData.pConstants);
		if (FAILED(hr)) {
			DLog(L"CDX11VideoProcessor::InitMediaType() : CreateBuffer() failed with error %s", HR2Str(hr));
			return FALSE;
		}
		m_PSConvColorData.bEnable = true;

		m_srcSubType = SubType;
		m_inputMT = *pmt;
		UpdateStatsStatic();

		return TRUE;
	}

	return FALSE;
}

HRESULT CDX11VideoProcessor::InitializeD3D11VP(const DXGI_FORMAT dxgiFormat, const UINT width, const UINT height, bool only_update_texture)
{
	DLog(L"CDX11VideoProcessor::InitializeD3D11VP() started with input surface: %s, %u x %u", DXGIFormatToString(dxgiFormat), width, height);

	CheckPointer(m_pVideoDevice, E_FAIL);

	if (only_update_texture) {
		if (dxgiFormat != m_srcDXGIFormat) {
			DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : incorrect texture format!");
			ASSERT(0);
			return E_FAIL;
		}
		if (width < m_srcWidth || height < m_srcHeight) {
			DLog(L"CDX9VideoProcessor::InitializeDXVA2VP() : texture size less than frame size!");
			return E_FAIL;
		}
		m_TexSrcCPU.Release();
		m_pInputView.Release();
		m_pSrcTexture2D.Release();
		m_pVideoProcessor.Release();
		m_pVideoProcessorEnum.Release();

		m_TextureWidth = 0;
		m_TextureHeight = 0;
	}

	HRESULT hr = S_OK;
	D3D11_VIDEO_PROCESSOR_CONTENT_DESC ContentDesc;
	ZeroMemory(&ContentDesc, sizeof(ContentDesc));
	ContentDesc.InputFrameFormat = m_bInterlaced ? D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST : D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
	ContentDesc.InputWidth = width;
	ContentDesc.InputHeight = height;
	ContentDesc.OutputWidth = ContentDesc.InputWidth;
	ContentDesc.OutputHeight = ContentDesc.InputHeight;
	ContentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

	hr = m_pVideoDevice->CreateVideoProcessorEnumerator(&ContentDesc, &m_pVideoProcessorEnum);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : CreateVideoProcessorEnumerator() failed with error %s", HR2Str(hr));
		return hr;
	}

	UINT uiFlags;
	hr = m_pVideoProcessorEnum->CheckVideoProcessorFormat(dxgiFormat, &uiFlags);
	if (FAILED(hr) || 0 == (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)) {
		return MF_E_UNSUPPORTED_D3D_TYPE;
	}

	if (m_InternalTexFmt != DXGI_FORMAT_B8G8R8A8_UNORM) {
		hr = m_pVideoProcessorEnum->CheckVideoProcessorFormat(m_InternalTexFmt, &uiFlags);
		if (FAILED(hr) || 0 == (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
			DLog(L"CDX11VideoProcessor::InitializeD3D11VP() - %s is not supported for D3D11 VP output. DXGI_FORMAT_B8G8R8A8_UNORM will be used.", DXGIFormatToString(m_InternalTexFmt));
			m_InternalTexFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
		}
	}
	if (m_InternalTexFmt == DXGI_FORMAT_B8G8R8A8_UNORM) {
		hr = m_pVideoProcessorEnum->CheckVideoProcessorFormat(m_InternalTexFmt, &uiFlags);
		if (FAILED(hr) || 0 == (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
			return MF_E_UNSUPPORTED_D3D_TYPE;
		}
	}

	D3D11_VIDEO_PROCESSOR_CAPS m_VPCaps = {};
	hr = m_pVideoProcessorEnum->GetVideoProcessorCaps(&m_VPCaps);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : GetVideoProcessorCaps() failed with error %s", HR2Str(hr));
		return hr;
	}

	for (UINT i = 0; i < std::size(m_VPFilterRange); i++) {
		if (m_VPCaps.FilterCaps & (1 << i)) {
			hr = m_pVideoProcessorEnum->GetVideoProcessorFilterRange((D3D11_VIDEO_PROCESSOR_FILTER)i, &m_VPFilterRange[i]);
			if (FAILED(hr)) {
				DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : GetVideoProcessorFilterRange(%u) failed with error %s", i, HR2Str(hr));
				return hr;
			}
			m_VPFilterSettings[i].Enabled = FALSE;
			m_VPFilterSettings[i].Level = m_VPFilterRange[i].Default;
		}
	}

	UINT index = 0;
	if (m_bInterlaced) {
		UINT proccaps = D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BLEND + D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BOB + D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_ADAPTIVE + D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_MOTION_COMPENSATION;
		D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS convCaps = {};
		for (index = 0; index < m_VPCaps.RateConversionCapsCount; index++) {
			hr = m_pVideoProcessorEnum->GetVideoProcessorRateConversionCaps(index, &convCaps);
			if (S_OK == hr) {
				// Check the caps to see which deinterlacer is supported
				if ((convCaps.ProcessorCaps & proccaps) != 0) {
					break;
				}
			}
		}
		if (index >= m_VPCaps.RateConversionCapsCount) {
			DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : deinterlace caps don't support");
			return E_FAIL;
		}
	}

	hr = m_pVideoDevice->CreateVideoProcessor(m_pVideoProcessorEnum, index, &m_pVideoProcessor);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : CreateVideoProcessor() failed with error %s", HR2Str(hr));
		return hr;
	}

	hr = m_TexSrcCPU.Create(m_pDevice, dxgiFormat, width, height, Tex2D_DynamicDecoderWrite);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : m_TexSrcCPU.Create() failed with error %s", HR2Str(hr));
		return hr;
	}

	hr = CreateTex2D(m_pDevice, dxgiFormat, width, height, Tex2D_Default, &m_pSrcTexture2D);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : CreateTex2D(m_pSrcTexture2D) failed with error %s", HR2Str(hr));
		return hr;
	}

	D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = {};
	inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
	hr = m_pVideoDevice->CreateVideoProcessorInputView(m_pSrcTexture2D, m_pVideoProcessorEnum, &inputViewDesc, &m_pInputView);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : CreateVideoProcessorInputView() failed with error %s", HR2Str(hr));
		return hr;
	}

#ifdef _DEBUG
	{
		const DXGI_FORMAT output_formats[] = {
			DXGI_FORMAT_B8G8R8X8_UNORM,
			DXGI_FORMAT_B8G8R8A8_UNORM,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_R10G10B10A2_UNORM,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
		};
		HRESULT hr2 = S_OK;

		for (const auto& fmt : output_formats) {
			CComPtr<ID3D11Texture2D> pTestTexture2D;
			hr2 = CreateTex2D(m_pDevice, fmt, width, height, Tex2D_DefaultRTarget, &pTestTexture2D);
			if (S_OK == hr2) {
				D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC OutputViewDesc = {};
				OutputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
				CComPtr<ID3D11VideoProcessorOutputView> pTestOutputView;
				hr2 = m_pVideoDevice->CreateVideoProcessorOutputView(pTestTexture2D, m_pVideoProcessorEnum, &OutputViewDesc, &pTestOutputView);
			}

			DLog(L"VideoProcessorOutputView with %s format is %s", DXGIFormatToString(fmt), S_OK == hr2 ? L"OK" : L"FAILED");
		}
	}
#endif

	m_TextureWidth  = width;
	m_TextureHeight = height;
	if (!only_update_texture) {
		m_srcDXGIFormat = dxgiFormat;
		m_srcWidth  = width;
		m_srcHeight = height;
	}

	DLog(L"CDX11VideoProcessor::InitializeD3D11VP() completed successfully");

	return S_OK;
}

HRESULT CDX11VideoProcessor::InitializeTexVP(const DXGI_FORMAT dxgiFormat, const UINT width, const UINT height)
{
	DLog(L"CDX11VideoProcessor::InitializeTexVP() started with input surface: %s, %u x %u", DXGIFormatToString(dxgiFormat), width, height);

	HRESULT hr = S_OK;

	hr = m_TexSrcCPU.Create(m_pDevice, dxgiFormat, width, height, Tex2D_DynamicShaderWrite);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeTexVP() : m_TexSrcCPU.Create() failed with error %s", HR2Str(hr));
		return hr;
	}

	hr = m_TexConvert.Create(m_pDevice, m_InternalTexFmt, width, height, Tex2D_DefaultShaderRTarget);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeTexVP() : m_TexConvert.Create() failed with error %s", HR2Str(hr));
		return hr;
	}

	m_TextureWidth  = width;
	m_TextureHeight = height;
	m_srcDXGIFormat = dxgiFormat;
	m_srcWidth      = width;
	m_srcHeight     = height;

	DLog(L"CDX11VideoProcessor::InitializeTexVP() completed successfully");

	return S_OK;
}

void CDX11VideoProcessor::Start()
{
	if (m_VendorId == PCIV_INTEL) {
		resetmt = true;
	}
}

void CDX11VideoProcessor::Stop()
{
}

HRESULT CDX11VideoProcessor::ProcessSample(IMediaSample* pSample)
{
	REFERENCE_TIME rtStart, rtEnd;
	pSample->GetTime(&rtStart, &rtEnd);

	const REFERENCE_TIME rtFrameDur = m_pFilter->m_FrameStats.GetAverageFrameDuration();
	rtEnd = rtStart + rtFrameDur;
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

#define BREAK_ON_ERROR(hr) { if (FAILED(hr)) { break; }}

HRESULT CDX11VideoProcessor::CopySample(IMediaSample* pSample)
{
	CheckPointer(m_pDXGISwapChain1, E_FAIL);

	int64_t ticks = GetPreciseTick();

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
		m_bSrcFromGPU = true;

		CComQIPtr<ID3D11Texture2D> pD3D11Texture2D;
		UINT ArraySlice = 0;
		hr = pMSD3D11->GetD3D11Texture(0, &pD3D11Texture2D, &ArraySlice);
		if (FAILED(hr)) {
			DLog(L"CDX11VideoProcessor::CopySample() : GetD3D11Texture() failed with error %s", HR2Str(hr));
			return hr;
		}

		D3D11_TEXTURE2D_DESC desc = {};
		pD3D11Texture2D->GetDesc(&desc);

		if (desc.Format != m_srcDXGIFormat || desc.Width != m_TextureWidth || desc.Height != m_TextureHeight) {
			hr = InitializeD3D11VP(desc.Format, desc.Width, desc.Height, true);
			if (FAILED(hr)) {
				return hr;
			}
		}

		m_pDeviceContext->CopySubresourceRegion(m_pSrcTexture2D, 0, 0, 0, 0, pD3D11Texture2D, ArraySlice, nullptr);
	}
	else {
		m_bSrcFromGPU = false;

		if (resetmt && m_pVideoProcessor) {
			// stupid hack for Intel
			resetmt = false;
			hr = InitializeD3D11VP(m_srcDXGIFormat, m_srcWidth, m_srcHeight, true);
			if (FAILED(hr)) {
				return hr;
			}
		}

		BYTE* data = nullptr;
		const long size = pSample->GetActualDataLength();
		if (size > 0 && S_OK == pSample->GetPointer(&data)) {
			D3D11_MAPPED_SUBRESOURCE mappedResource = {};
			hr = m_pDeviceContext->Map(m_TexSrcCPU.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
			if (SUCCEEDED(hr)) {
				ASSERT(m_pConvertFn);
				BYTE* src = (m_srcPitch < 0) ? data + m_srcPitch * (1 - (int)m_srcHeight) : data;
				m_pConvertFn(m_srcHeight, (BYTE*)mappedResource.pData, mappedResource.RowPitch, src, m_srcPitch);
				m_pDeviceContext->Unmap(m_TexSrcCPU.pTexture, 0);
				if (m_pVideoProcessor) {
					// ID3D11VideoProcessor does not use textures with D3D11_CPU_ACCESS_WRITE flag
					m_pDeviceContext->CopyResource(m_pSrcTexture2D, m_TexSrcCPU.pTexture);
				}
			}
		}
	}

	m_RenderStats.copyticks = GetPreciseTick() - ticks;

	return hr;
}

HRESULT CDX11VideoProcessor::Render(int field)
{
	CheckPointer(m_TexSrcCPU.pTexture, E_FAIL);
	CheckPointer(m_pDXGISwapChain1, E_FAIL);

	int64_t ticks = GetPreciseTick();
	if (field) {
		m_FieldDrawn = field;
	}

	CComPtr<ID3D11Texture2D> pBackBuffer;
	HRESULT hr = m_pDXGISwapChain1->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::Render() : GetBuffer() failed with error %s", HR2Str(hr));
		return hr;
	}

	ID3D11RenderTargetView* pRenderTargetView;
	if (S_OK == m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView)) {
		const FLOAT ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ClearColor);
		pRenderTargetView->Release();
	}

	if (!m_videoRect.IsRectEmpty() && !m_windowRect.IsRectEmpty()) {
		m_srcRenderRect = m_srcRect;
		m_dstRenderRect = m_videoRect;
		D3D11_TEXTURE2D_DESC desc = {};
		pBackBuffer->GetDesc(&desc);
		if (desc.Width && desc.Height) {
			ClipToSurface(desc.Width, desc.Height, m_srcRenderRect, m_dstRenderRect);
		}

		if (m_pVideoProcessor) {
			hr = ProcessD3D11(pBackBuffer, m_srcRenderRect, m_dstRenderRect, m_FieldDrawn == 2);
		} else {
			hr = ProcessTex(pBackBuffer, m_srcRenderRect, m_dstRenderRect);
		}
		if (S_OK == hr && m_bShowStats) {
			hr = DrawStats(pBackBuffer);
		}
	}

	m_RenderStats.renderticks = GetPreciseTick() - ticks;

	hr = m_pDXGISwapChain1->Present(0, 0);

	return hr;
}

HRESULT CDX11VideoProcessor::FillBlack()
{
	CheckPointer(m_pDXGISwapChain1, E_ABORT);

	ID3D11Texture2D* pBackBuffer = nullptr;
	HRESULT hr = m_pDXGISwapChain1->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
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

	m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);

	float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ClearColor);

	hr = m_pDXGISwapChain1->Present(0, 0);

	pRenderTargetView->Release();

	return hr;
}

void CDX11VideoProcessor::UpdateVideoTex()
{
	if (m_bVPScaling) {
		m_TexVideo.Release();
	} else {
		m_TexVideo.Create(m_pDevice, m_InternalTexFmt, m_TextureWidth, m_TextureHeight, Tex2D_DefaultShaderRTarget);
	}
}

void CDX11VideoProcessor::UpdateCorrectionTex(const int w, const int h)
{
	if (m_pPSCorrection) {
		if (w != m_TexCorrection.desc.Width || h != m_TexCorrection.desc.Width) {
			HRESULT hr = m_TexCorrection.Create(m_pDevice, m_InternalTexFmt, w, h, Tex2D_DefaultShaderRTarget);
			DLogIf(FAILED(hr), "CDX11VideoProcessor::UpdateCorrectionTex() : m_TexCorrection.Create() failed with error %s", HR2Str(hr));
		}
		// else do nothing
	} else {
		m_TexCorrection.Release();
	}
}

void CDX11VideoProcessor::UpdateUpscalingShaders()
{
	struct {
		UINT shaderX;
		UINT shaderY;
		wchar_t* const description;
	} static const resIDs[UPSCALE_COUNT] = {
		{IDF_PSH11_RESIZER_MITCHELL4_X, IDF_PSH11_RESIZER_MITCHELL4_Y, L"Mitchell-Netravali"},
		{IDF_PSH11_RESIZER_CATMULL4_X,  IDF_PSH11_RESIZER_CATMULL4_Y , L"Catmull-Rom"       },
		{IDF_PSH11_RESIZER_LANCZOS2_X,  IDF_PSH11_RESIZER_LANCZOS2_Y , L"Lanczos2"          },
		{IDF_PSH11_RESIZER_LANCZOS3_X,  IDF_PSH11_RESIZER_LANCZOS3_Y , L"Lanczos3"          },
	};

	m_pShaderUpscaleX.Release();
	m_pShaderUpscaleY.Release();

	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderUpscaleX, resIDs[m_iUpscaling].shaderX));
	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderUpscaleY, resIDs[m_iUpscaling].shaderY));
	m_strShaderUpscale = resIDs[m_iUpscaling].description;
}

void CDX11VideoProcessor::UpdateDownscalingShaders()
{
	struct {
		UINT shaderX;
		UINT shaderY;
		wchar_t* const description;
	} static const resIDs[DOWNSCALE_COUNT] = {
		{IDF_PSH11_DOWNSCALER_BOX_X,      IDF_PSH11_DOWNSCALER_BOX_Y     , L"Box"     },
		{IDF_PSH11_DOWNSCALER_BILINEAR_X, IDF_PSH11_DOWNSCALER_BILINEAR_Y, L"Bilinear"},
		{IDF_PSH11_DOWNSCALER_HAMMING_X,  IDF_PSH11_DOWNSCALER_HAMMING_Y , L"Hamming" },
		{IDF_PSH11_DOWNSCALER_BICUBIC_X,  IDF_PSH11_DOWNSCALER_BICUBIC_Y , L"Bicubic" },
		{IDF_PSH11_DOWNSCALER_LANCZOS_X,  IDF_PSH11_DOWNSCALER_LANCZOS_Y , L"Lanczos" }
	};

	m_pShaderDownscaleX.Release();
	m_pShaderDownscaleY.Release();

	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderDownscaleX, resIDs[m_iDownscaling].shaderX));
	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderDownscaleY, resIDs[m_iDownscaling].shaderY));
	m_strShaderDownscale = resIDs[m_iDownscaling].description;
}

HRESULT CDX11VideoProcessor::ProcessD3D11(ID3D11Texture2D* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect, const bool second)
{
	HRESULT hr = S_OK;

	if (m_pPSCorrection && m_TexCorrection.pTexture) {
		CRect rCorrection(0, 0, m_TexCorrection.desc.Width, m_TexCorrection.desc.Height);
		if (m_bVPScaling) {
			hr = D3D11VPPass(m_TexCorrection.pTexture, rSrcRect, rCorrection, second);
		} else {
			hr = D3D11VPPass(m_TexVideo.pTexture, rSrcRect, rSrcRect, second);
			hr = ResizeShader2Pass(m_TexVideo, m_TexCorrection.pTexture, rSrcRect, rCorrection);
		}
		hr = TextureCopyRect(m_TexCorrection, pRenderTarget, rCorrection, rDstRect, m_pPSCorrection, nullptr);
	}
	else {
		if (m_bVPScaling) {
			hr = D3D11VPPass(pRenderTarget, rSrcRect, rDstRect, second);
		} else {
			hr = D3D11VPPass(m_TexVideo.pTexture, rSrcRect, rSrcRect, second);
			hr = ResizeShader2Pass(m_TexVideo, pRenderTarget, rSrcRect, rDstRect);
		}
	}

	return hr;
}

HRESULT CDX11VideoProcessor::ProcessTex(ID3D11Texture2D* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect)
{
	// Convert color pass
	HRESULT hr = TextureCopyRect(m_TexSrcCPU, m_TexConvert.pTexture, rSrcRect, rSrcRect, m_pPSConvertColor, m_PSConvColorData.pConstants);

	// Resize
	hr = ResizeShader2Pass(m_TexConvert, pRenderTarget, rSrcRect, rDstRect);

	return hr;
}

HRESULT CDX11VideoProcessor::D3D11VPPass(ID3D11Texture2D* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect, const bool second)
{
	if (!second) {
		// input format
		m_pVideoContext->VideoProcessorSetStreamFrameFormat(m_pVideoProcessor, 0, m_SampleFormat);
		// Output rate (repeat frames)
		m_pVideoContext->VideoProcessorSetStreamOutputRate(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, TRUE, nullptr);
		// disable automatic video quality by driver
		m_pVideoContext->VideoProcessorSetStreamAutoProcessingMode(m_pVideoProcessor, 0, FALSE);

		m_pVideoContext->VideoProcessorSetStreamSourceRect(m_pVideoProcessor, 0, TRUE, rSrcRect);
		m_pVideoContext->VideoProcessorSetStreamDestRect(m_pVideoProcessor, 0, TRUE, rDstRect);
		m_pVideoContext->VideoProcessorSetOutputTargetRect(m_pVideoProcessor, FALSE, nullptr);

		// filters
		m_pVideoContext->VideoProcessorSetStreamFilter(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_FILTER_BRIGHTNESS, m_VPFilterSettings[0].Enabled, m_VPFilterSettings[0].Level);
		m_pVideoContext->VideoProcessorSetStreamFilter(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_FILTER_CONTRAST,   m_VPFilterSettings[1].Enabled, m_VPFilterSettings[1].Level);
		m_pVideoContext->VideoProcessorSetStreamFilter(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_FILTER_HUE,        m_VPFilterSettings[2].Enabled, m_VPFilterSettings[2].Level);
		m_pVideoContext->VideoProcessorSetStreamFilter(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_FILTER_SATURATION, m_VPFilterSettings[3].Enabled, m_VPFilterSettings[3].Level);

		// Output background color (black)
		static const D3D11_VIDEO_COLOR backgroundColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		m_pVideoContext->VideoProcessorSetOutputBackgroundColor(m_pVideoProcessor, FALSE, &backgroundColor);

		// Stream color space
		D3D11_VIDEO_PROCESSOR_COLOR_SPACE colorSpace = {};
		if (m_srcExFmt.value) {
			colorSpace.RGB_Range = m_srcExFmt.NominalRange == DXVA2_NominalRange_16_235 ? 1 : 0;
			colorSpace.YCbCr_Matrix = m_srcExFmt.VideoTransferMatrix == DXVA2_VideoTransferMatrix_BT601 ? 0 : 1;
		} else {
			// for RGB
			colorSpace.RGB_Range = 0;
		}
		m_pVideoContext->VideoProcessorSetStreamColorSpace(m_pVideoProcessor, 0, &colorSpace);

		// Output color space
		colorSpace.RGB_Range = 0;
		m_pVideoContext->VideoProcessorSetOutputColorSpace(m_pVideoProcessor, &colorSpace);
	}

	D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC OutputViewDesc = {};
	OutputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
	CComPtr<ID3D11VideoProcessorOutputView> pOutputView;
	HRESULT hr = m_pVideoDevice->CreateVideoProcessorOutputView(pRenderTarget, m_pVideoProcessorEnum, &OutputViewDesc, &pOutputView);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::ProcessD3D11() : CreateVideoProcessorOutputView() failed with error %s", HR2Str(hr));
		return hr;
	}

	D3D11_VIDEO_PROCESSOR_STREAM StreamData = {};
	StreamData.Enable = TRUE;
	StreamData.InputFrameOrField = second ? 1 : 0;
	StreamData.pInputSurface = m_pInputView;
	hr = m_pVideoContext->VideoProcessorBlt(m_pVideoProcessor, pOutputView, 0, 1, &StreamData);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::ProcessD3D11() : VideoProcessorBlt() failed with error %s", HR2Str(hr));
	}

	return hr;
}

HRESULT CDX11VideoProcessor::ResizeShader2Pass(Tex2D_t& Tex, ID3D11Texture2D* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect)
{
	HRESULT hr = S_OK;

	D3D11_TEXTURE2D_DESC RTDesc;
	pRenderTarget->GetDesc(&RTDesc);

	const int w1 = rSrcRect.Width();
	const int h1 = rSrcRect.Height();
	const int w2 = rDstRect.Width();
	const int h2 = rDstRect.Height();
	const int k = m_bInterpolateAt50pct ? 2 : 1;

	ID3D11PixelShader* resizerX = (w1 == w2) ? nullptr : (w1 > k * w2) ? m_pShaderDownscaleX : m_pShaderUpscaleX;
	ID3D11PixelShader* resizerY = (h1 == h2) ? nullptr : (h1 > k * h2) ? m_pShaderDownscaleY : m_pShaderUpscaleY;

	// two pass resize
	if (resizerX && resizerY) {
		// check intermediate texture
		const UINT texWidth = w2;
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
				DLog(L"CDX11VideoProcessor::ResizeShader2Pass() : m_TexResize.Create() failed with error %s", HR2Str(hr));
				return hr;
			}
		}

		CRect resizeRect(0, 0, texWidth, texHeight);

		// First resize pass
		hr = TextureResizeShader(Tex, m_TexResize.pTexture, rSrcRect, resizeRect, resizerX);
		// Second resize pass
		hr = TextureResizeShader(m_TexResize, pRenderTarget, resizeRect, rDstRect, resizerY);
	}
	else {
		if (resizerX) {
			// one pass resize for width
			hr = TextureResizeShader(Tex, pRenderTarget, rSrcRect, rDstRect, resizerX);
		}
		else if (resizerY) {
			// one pass resize for height
			hr = TextureResizeShader(Tex, pRenderTarget, rSrcRect, rDstRect, resizerY);
		}
		else {
			// no resize
			hr = TextureCopyRect(Tex, pRenderTarget, rSrcRect, rDstRect, m_pPS_Simple, nullptr);
		}
	}

	DLogIf(FAILED(hr), L"CDX11VideoProcessor::ResizeShader2Pass() : failed with error %s", HR2Str(hr));

	return hr;
}

void CDX11VideoProcessor::SetVideoRect(const CRect& videoRect)
{
	UpdateCorrectionTex(videoRect.Width(), videoRect.Height());
	m_videoRect = videoRect;
}

HRESULT CDX11VideoProcessor::SetWindowRect(const CRect& windowRect)
{
	m_windowRect = windowRect;

	HRESULT hr = S_OK;
	const UINT w = m_windowRect.Width();
	const UINT h = m_windowRect.Height();

	if (m_pDXGISwapChain1) {
		hr = m_pDXGISwapChain1->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
	}

	return hr;
}

HRESULT CDX11VideoProcessor::GetVideoSize(long *pWidth, long *pHeight)
{
	CheckPointer(pWidth, E_POINTER);
	CheckPointer(pHeight, E_POINTER);

	*pWidth  = m_srcRectWidth;
	*pHeight = m_srcRectHeight;

	return S_OK;
}

HRESULT CDX11VideoProcessor::GetAspectRatio(long *plAspectX, long *plAspectY)
{
	CheckPointer(plAspectX, E_POINTER);
	CheckPointer(plAspectY, E_POINTER);

	*plAspectX = m_srcAspectRatioX;
	*plAspectY = m_srcAspectRatioY;

	return S_OK;
}

HRESULT CDX11VideoProcessor::GetCurentImage(long *pDIBImage)
{
	//if (m_SrcSamples.Empty()) {
	//	return E_FAIL;
	//}

	CRect rSrcRect(m_srcRect);
	int w = rSrcRect.Width();
	int h = rSrcRect.Height();
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
	hr = CreateTex2D(m_pDevice, DXGI_FORMAT_B8G8R8X8_UNORM, w, h, Tex2D_DefaultRTarget, &pRGB32Texture2D);
	if (FAILED(hr)) {
		return hr;
	}

	if (m_pVideoContext) {
		hr = ProcessD3D11(pRGB32Texture2D, rSrcRect, rDstRect, false);
	} else {
		hr = ProcessTex(pRGB32Texture2D, rSrcRect, rDstRect);
	}
	if (FAILED(hr)) {
		return hr;
	}

	CComPtr<ID3D11Texture2D> pRGB32Texture2D_Shared;
	hr = CreateTex2D(m_pDevice, DXGI_FORMAT_B8G8R8X8_UNORM, w, h, Tex2D_StagingRead, &pRGB32Texture2D_Shared);
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

HRESULT CDX11VideoProcessor::GetVPInfo(CStringW& str)
{
	str = L"DirectX 11";
	str.AppendFormat(L"\nGraphics adapter: %s", m_strAdapterDescription);
	str.AppendFormat(L"\nVideoProcessor  : %s", m_pVideoProcessor ? L"D3D11" : L"Shaders");

#ifdef _DEBUG
	str.AppendFormat(L"\nSource rect   : %d,%d,%d,%d - %dx%d", m_srcRect.left, m_srcRect.top, m_srcRect.right, m_srcRect.bottom, m_srcRect.Width(), m_srcRect.Height());
	str.AppendFormat(L"\nTarget rect   : %d,%d,%d,%d - %dx%d", m_trgRect.left, m_trgRect.top, m_trgRect.right, m_trgRect.bottom, m_trgRect.Width(), m_trgRect.Height());
	str.AppendFormat(L"\nVideo rect    : %d,%d,%d,%d - %dx%d", m_videoRect.left, m_videoRect.top, m_videoRect.right, m_videoRect.bottom, m_videoRect.Width(), m_videoRect.Height());
	str.AppendFormat(L"\nWindow rect   : %d,%d,%d,%d - %dx%d", m_windowRect.left, m_windowRect.top, m_windowRect.right, m_windowRect.bottom, m_windowRect.Width(), m_windowRect.Height());
	str.AppendFormat(L"\nSrcRender rect: %d,%d,%d,%d - %dx%d", m_srcRenderRect.left, m_srcRenderRect.top, m_srcRenderRect.right, m_srcRenderRect.bottom, m_srcRenderRect.Width(), m_srcRenderRect.Height());
	str.AppendFormat(L"\nDstRender rect: %d,%d,%d,%d - %dx%d", m_dstRenderRect.left, m_dstRenderRect.top, m_dstRenderRect.right, m_dstRenderRect.bottom, m_dstRenderRect.Width(), m_dstRenderRect.Height());
#endif

	return S_OK;
}

void CDX11VideoProcessor::SetTexFormat(int value)
{
	if (value < 0 || value >= SURFMT_COUNT) {
		DLog("CDX11VideoProcessor::SetTexFormat() unknown value %d", value);
		ASSERT(FALSE);
		return;
	}

	m_iTexFormat = value;

	switch (m_iTexFormat) {
	default:
	case SURFMT_8INT:
		m_InternalTexFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
		break;
	case SURFMT_10INT:
	case SURFMT_16FLOAT:
		m_InternalTexFmt = DXGI_FORMAT_R10G10B10A2_UNORM;
		break;
	// TODO
	//case SURFMT_16FLOAT:
	//	m_InternalTexFmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
	//	break;
	}
}

void CDX11VideoProcessor::SetVPScaling(bool value)
{
	m_bVPScaling = value;

	if (m_pDevice) {
		UpdateVideoTex();
	}
}

void CDX11VideoProcessor::SetUpscaling(int value)
{
	if (value < 0 || value >= UPSCALE_COUNT) {
		DLog("CDX11VideoProcessor::SetUpscaling() unknown value %d", value);
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
		DLog("CDX11VideoProcessor::SetDownscaling() unknown value %d", value);
		ASSERT(FALSE);
		return;
	}
	m_iDownscaling = value;

	if (m_pDevice) {
		UpdateDownscalingShaders();
	}
};

void CDX11VideoProcessor::UpdateStatsStatic()
{
	auto FmtConvParams = GetFmtConvParams(m_srcSubType);
	if (FmtConvParams) {
		m_strStatsStatic1 = L"Direct3D 11";
		m_strStatsStatic1.AppendFormat(L"\nGraph. Adapter: %s", m_strAdapterDescription);

		m_strStatsStatic2.Format(L" %S %ux%u", FmtConvParams->str, m_srcRectWidth, m_srcRectHeight);
		m_strStatsStatic2.AppendFormat(L"\nInternalFormat: %s", DXGIFormatToString(m_InternalTexFmt));
		m_strStatsStatic2.AppendFormat(L"\nVideoProcessor: %s", m_pVideoProcessor ? L"D3D11" : L"Shaders");
	} else {
		m_strStatsStatic1 = L"Error";
		m_strStatsStatic2.Empty();
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
	if (m_bSrcFromGPU) {
		str.Append(L" GPU");
	}
	str.Append(m_strStatsStatic2);

	const int srcW = m_srcRenderRect.Width();
	const int srcH = m_srcRenderRect.Height();
	const int dstW = m_dstRenderRect.Width();
	const int dstH = m_dstRenderRect.Height();
	str.AppendFormat(L"\nScaling       : %dx%d -> %dx%d", srcW, srcH, dstW, dstH);
	if (srcW != dstW || srcH != dstH) {
		if (m_pVideoProcessor && m_bVPScaling) {
			str.Append(L" D3D11");
		} else {
			const int k = m_bInterpolateAt50pct ? 2 : 1;
			const wchar_t* strX = (srcW > k * dstW) ? m_strShaderDownscale : m_strShaderUpscale;
			const wchar_t* strY = (srcH > k * dstH) ? m_strShaderDownscale : m_strShaderUpscale;
			str.AppendFormat(L" %s", strX);
			if (strY != strX) {
				str.AppendFormat(L"/%s", strY);
			}
		}
	}

	str.AppendFormat(L"\nFrames: %5u, skiped: %u/%u, failed: %u",
		m_pFilter->m_FrameStats.GetFrames(), m_pFilter->m_DrawStats.m_dropped, m_RenderStats.dropped2, m_RenderStats.failed);
	str.AppendFormat(L"\nCopyTime:%3llu ms, RenderTime:%3llu ms",
		m_RenderStats.copyticks * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.renderticks * 1000 / GetPreciseTicksPerSecondI());
	str.AppendFormat(L"\nSync offset   : %+3lld ms", (m_RenderStats.syncoffset + 5000) / 10000);

	CComPtr<IDXGISurface1> pDxgiSurface1;
	HRESULT hr = m_TexOSD.pTexture->QueryInterface(IID_IDXGISurface1, (void**)&pDxgiSurface1);
	if (S_OK == hr) {
		HDC hdc;
		hr = pDxgiSurface1->GetDC(FALSE, &hdc);
		if (S_OK == hr) {
			m_StatsDrawing.DrawTextW(hdc, str);
			pDxgiSurface1->ReleaseDC(0);


			ID3D11RenderTargetView* pRenderTargetView = nullptr;
			hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
			if (FAILED(hr)) {
				DLog(L"CDX11VideoProcessor::DrawStats() : CreateRenderTargetView() failed with error %s", HR2Str(hr));
				return hr;
			}

			D3D11_TEXTURE2D_DESC RTDesc;
			pRenderTarget->GetDesc(&RTDesc);

			D3D11_VIEWPORT VP;
			VP.TopLeftX = STATS_X;
			VP.TopLeftY = STATS_Y;
			VP.Width    = STATS_W;
			VP.Height   = STATS_H;
			VP.MinDepth = 0.0f;
			VP.MaxDepth = 1.0f;
			m_pDeviceContext->RSSetViewports(1, &VP);

			// Set resources
			UINT Stride = sizeof(VERTEX);
			UINT Offset = 0;
			m_pDeviceContext->OMSetBlendState(m_pAlphaBlendState, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
			m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
			m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
			m_pDeviceContext->PSSetShader(m_pPS_Simple, nullptr, 0);
			m_pDeviceContext->PSSetShaderResources(0, 1, &m_TexOSD.pShaderResource.p);
			m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerPoint);
			m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pFullFrameVertexBuffer, &Stride, &Offset);

			// Draw textured quad onto render target
			m_pDeviceContext->Draw(6, 0);

			pRenderTargetView->Release();
		}
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
		*ppv = NULL;
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

// D3D11<->DXVA2

void FilterRangeD3D11toDXVA2(DXVA2_ValueRange& _dxva2_, const D3D11_VIDEO_PROCESSOR_FILTER_RANGE& _d3d11_, bool norm)
{
	float k = _d3d11_.Multiplier;
	if (norm) {
		ASSERT(_d3d11_.Default > 0);
		k /= _d3d11_.Default;
	}
	_dxva2_.MinValue     = DXVA2FloatToFixed(k * _d3d11_.Minimum);
	_dxva2_.MaxValue     = DXVA2FloatToFixed(k * _d3d11_.Maximum);
	_dxva2_.DefaultValue = DXVA2FloatToFixed(k * _d3d11_.Default);
	_dxva2_.StepSize     = DXVA2FloatToFixed(k);
}

void LevelD3D11toDXVA2(DXVA2_Fixed32& fixed, const int level, const D3D11_VIDEO_PROCESSOR_FILTER_RANGE& range, bool norm)
{
	float k = range.Multiplier;
	if (norm) {
		k /= range.Default;
	}
	fixed = DXVA2FloatToFixed(k * level);
}

int ValueDXVA2toD3D11(const DXVA2_Fixed32 fixed, const D3D11_VIDEO_PROCESSOR_FILTER_RANGE& range, bool norm)
{
	int level = (int)std::round(DXVA2FixedToFloat(fixed) * (norm ? range.Default : 1) / range.Multiplier);
	return std::clamp(level, range.Minimum, range.Maximum);
}

// IMFVideoProcessor

STDMETHODIMP CDX11VideoProcessor::GetProcAmpRange(DWORD dwProperty, DXVA2_ValueRange *pPropRange)
{
	CheckPointer(m_pVideoProcessor, MF_E_INVALIDREQUEST);
	CheckPointer(pPropRange, E_POINTER);

	switch (dwProperty) {
	case DXVA2_ProcAmp_Brightness: FilterRangeD3D11toDXVA2(*pPropRange, m_VPFilterRange[0], false); break;
	case DXVA2_ProcAmp_Contrast:   FilterRangeD3D11toDXVA2(*pPropRange, m_VPFilterRange[1], true);  break;
	case DXVA2_ProcAmp_Hue:        FilterRangeD3D11toDXVA2(*pPropRange, m_VPFilterRange[2], false); break;
	case DXVA2_ProcAmp_Saturation: FilterRangeD3D11toDXVA2(*pPropRange, m_VPFilterRange[3], true);  break;
	default:
		return E_INVALIDARG;
	}

	return S_OK;
}

STDMETHODIMP CDX11VideoProcessor::GetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *Values)
{
	CheckPointer(m_pVideoProcessor, MF_E_INVALIDREQUEST);
	CheckPointer(Values, E_POINTER);

	if (dwFlags&DXVA2_ProcAmp_Brightness) {
		LevelD3D11toDXVA2(Values->Brightness, m_VPFilterSettings[0].Level, m_VPFilterRange[0], false);
	}
	if (dwFlags&DXVA2_ProcAmp_Contrast) {
		LevelD3D11toDXVA2(Values->Contrast, m_VPFilterSettings[1].Level, m_VPFilterRange[1], true);
	}
	if (dwFlags&DXVA2_ProcAmp_Hue) {
		LevelD3D11toDXVA2(Values->Hue, m_VPFilterSettings[2].Level, m_VPFilterRange[2], false);
	}
	if (dwFlags&DXVA2_ProcAmp_Saturation) {
		LevelD3D11toDXVA2(Values->Saturation, m_VPFilterSettings[3].Level, m_VPFilterRange[3], true);
	}

	return S_OK;
}

STDMETHODIMP CDX11VideoProcessor::SetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *pValues)
{
	CheckPointer(m_pVideoProcessor, MF_E_INVALIDREQUEST);
	CheckPointer(pValues, E_POINTER);

	if (dwFlags&DXVA2_ProcAmp_Brightness) {
		m_VPFilterSettings[0].Level = ValueDXVA2toD3D11(pValues->Brightness, m_VPFilterRange[0], false);
		m_VPFilterSettings[0].Enabled = (m_VPFilterSettings[0].Level != m_VPFilterRange[0].Default);
	}
	if (dwFlags&DXVA2_ProcAmp_Contrast) {
		m_VPFilterSettings[1].Level = ValueDXVA2toD3D11(pValues->Contrast, m_VPFilterRange[1], true);
		m_VPFilterSettings[1].Enabled = (m_VPFilterSettings[1].Level != m_VPFilterRange[1].Default);
	}
	if (dwFlags&DXVA2_ProcAmp_Hue) {
		m_VPFilterSettings[2].Level = ValueDXVA2toD3D11(pValues->Hue, m_VPFilterRange[2], false);
		m_VPFilterSettings[2].Enabled = (m_VPFilterSettings[2].Level != m_VPFilterRange[2].Default);
	}
	if (dwFlags&DXVA2_ProcAmp_Saturation) {
		m_VPFilterSettings[3].Level = ValueDXVA2toD3D11(pValues->Saturation, m_VPFilterRange[3], true);
		m_VPFilterSettings[3].Enabled = (m_VPFilterSettings[3].Level != m_VPFilterRange[3].Default);
	}

	return S_OK;
}

STDMETHODIMP CDX11VideoProcessor::GetBackgroundColor(COLORREF *lpClrBkg)
{
	CheckPointer(m_pVideoProcessor, MF_E_INVALIDREQUEST);
	CheckPointer(lpClrBkg, E_POINTER);
	*lpClrBkg = RGB(0, 0, 0);
	return S_OK;
}
