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
#include "Include/Version.h"
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

// D3D11<->DXVA2

void FilterRangeD3D11toDXVA2(DXVA2_ValueRange& _dxva2_, const D3D11_VIDEO_PROCESSOR_FILTER_RANGE& _d3d11_)
{
	float MinValue = _d3d11_.Multiplier * _d3d11_.Minimum;
	float MaxValue = _d3d11_.Multiplier * _d3d11_.Maximum;
	float DefValue = _d3d11_.Multiplier * _d3d11_.Default;
	float StepSize = 1.0f;
	if (DefValue >= 1.0f) { // capture 1.0f is important for changing StepSize
		MinValue /= DefValue;
		MaxValue /= DefValue;
		DefValue /= DefValue;
		StepSize = 0.01f;
	}
	_dxva2_.MinValue = DXVA2FloatToFixed(MinValue);
	_dxva2_.MaxValue = DXVA2FloatToFixed(MaxValue);
	_dxva2_.DefaultValue = DXVA2FloatToFixed(DefValue);
	_dxva2_.StepSize = DXVA2FloatToFixed(StepSize);
}

void LevelD3D11toDXVA2(DXVA2_Fixed32& fixed, const int level, const D3D11_VIDEO_PROCESSOR_FILTER_RANGE& range)
{
	float value = range.Multiplier * level;
	const float k = range.Multiplier * range.Default;
	if (k > 1.0f) {
		value /= k;
	}
	fixed = DXVA2FloatToFixed(value);
}

int ValueDXVA2toD3D11(const DXVA2_Fixed32 fixed, const D3D11_VIDEO_PROCESSOR_FILTER_RANGE& range)
{
	float value = DXVA2FixedToFloat(fixed) / range.Multiplier;
	const float k = range.Multiplier * range.Default;
	if (k > 1.0f) {
		value *= range.Default;
	}
	const int level = (int)std::round(value);
	return std::clamp(level, range.Minimum, range.Maximum);
}

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
	pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
	pDeviceContext->RSSetViewports(1, &viewport);
	pDeviceContext->OMSetBlendState(nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
	pDeviceContext->VSSetShader(pVertexShader, nullptr, 0);
	pDeviceContext->PSSetShader(pPixelShader, nullptr, 0);
	pDeviceContext->PSSetShaderResources(0, 1, &pShaderResourceViews);
	pDeviceContext->PSSetSamplers(0, 1, &pSampler);
	pDeviceContext->PSSetConstantBuffers(0, 1, &pConstantBuffer);
	pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pDeviceContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &Stride, &Offset);

	// Draw textured quad onto render target
	pDeviceContext->Draw(6, 0);

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
		m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
		m_pDeviceContext->RSSetViewports(1, &viewport);
		m_pDeviceContext->OMSetBlendState(m_pAlphaBlendState, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
		m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
		m_pDeviceContext->PSSetShader(m_pPS_Simple, nullptr, 0);
		m_pDeviceContext->PSSetShaderResources(0, 1, &pShaderResource);
		m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerPoint);
		m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pFullFrameVertexBuffer, &Stride, &Offset);

		// Draw textured quad onto render target
		m_pDeviceContext->Draw(6, 0);

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
		hr = CreateVertexBuffer(m_pDevice, &pVertexBuffer, m_d3dpp.BackBufferWidth, m_d3dpp.BackBufferHeight, srcRect);

		if (S_OK == hr) {
			// Set resources
			m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
			m_pDeviceContext->RSSetViewports(1, &viewport);
			m_pDeviceContext->OMSetBlendState(m_pAlphaBlendState, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
			m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
			m_pDeviceContext->PSSetShader(m_pPS_Simple, nullptr, 0);
			m_pDeviceContext->PSSetShaderResources(0, 1, &pShaderResource);
			m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerPoint);
			m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			m_pDeviceContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &Stride, &Offset);

			// Draw textured quad onto render target
			m_pDeviceContext->Draw(6, 0);

			pVertexBuffer->Release();
		}
		pRenderTargetView->Release();
	}
	DLogIf(FAILED(hr), L"CDX11VideoProcessor:AlphaBlt() : CreateRenderTargetView() failed with error %s", HR2Str(hr));

	return hr;
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

HRESULT CDX11VideoProcessor::TextureConvertColor(TexVideo_t& texVideo, ID3D11Texture2D* pRenderTarget)
{
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;

	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (FAILED(hr)) {
		DLog(L"TextureCopyRect() : CreateRenderTargetView() failed with error %s", HR2Str(hr));
		return hr;
	}

	UINT width = (m_srcParams.cformat == CF_YUY2) ? texVideo.desc.Width * 2 : texVideo.desc.Width;

	D3D11_VIEWPORT VP;
	VP.TopLeftX = 0;
	VP.TopLeftY = 0;
	VP.Width = (FLOAT)width;
	VP.Height = (FLOAT)texVideo.desc.Height;
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;

	const UINT Stride = sizeof(VERTEX);
	const UINT Offset = 0;

	// Set resources
	m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView.p, nullptr);
	m_pDeviceContext->RSSetViewports(1, &VP);
	m_pDeviceContext->OMSetBlendState(nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
	m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
	m_pDeviceContext->PSSetShader(m_pPSConvertColor, nullptr, 0);
	m_pDeviceContext->PSSetShaderResources(0, 1, &texVideo.pShaderResource.p);
	m_pDeviceContext->PSSetShaderResources(1, 1, &texVideo.pShaderResource2.p);
	m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerPoint);
	m_pDeviceContext->PSSetSamplers(1, 1, &m_pSamplerLinear);
	m_pDeviceContext->PSSetConstantBuffers(0, 1, &m_PSConvColorData.pConstants);
	m_pDeviceContext->PSSetConstantBuffers(4, 1, &m_PSConvColorData.pConstants4);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pFullFrameVertexBuffer, &Stride, &Offset);

	// Draw textured quad onto render target
	m_pDeviceContext->Draw(6, 0);

	ID3D11ShaderResourceView* views[2] = {};
	m_pDeviceContext->PSSetShaderResources(0, 2, views);

	return hr;;
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

	HRESULT hr = m_D3D11CreateDevice(
		pDXGIAdapter,
		D3D_DRIVER_TYPE_UNKNOWN,
		nullptr,
#if DIRECTWRITE_ENABLE
		D3D11_CREATE_DEVICE_BGRA_SUPPORT |
#endif
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
	m_TexSrcVideo.Release();
	m_TexCorrection.Release();
	m_TexConvert.Release();
	m_TexResize.Release();

	SAFE_RELEASE(m_PSConvColorData.pConstants);
	SAFE_RELEASE(m_PSConvColorData.pConstants4);

	m_pInputView.Release();
	m_pVideoProcessor.Release();
	m_pVideoProcessorEnum.Release();

	m_srcParams      = {};
	m_srcDXGIFormat  = DXGI_FORMAT_UNKNOWN;
	m_srcDXVA2Format = D3DFMT_UNKNOWN;
	m_pConvertFn     = nullptr;
	m_srcWidth       = 0;
	m_srcHeight      = 0;
	m_TextureWidth   = 0;
	m_TextureHeight  = 0;
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
	SAFE_RELEASE(m_pSamplerLinear);
	m_pAlphaBlendState.Release();
	SAFE_RELEASE(m_pFullFrameVertexBuffer);

#if DIRECTWRITE_ENABLE
	m_pTextFormat.Release();
	m_pDWriteFactory.Release();

	m_pD2D1Brush.Release();
	m_pD2D1RenderTarget.Release();
	m_pD2D1Factory.Release();
#endif

	m_pShaderResourceSubPic.Release();
	m_pTextureSubPic.Release();

	m_pSurface9SubPic.Release();

	m_pDeviceContext.Release();
	ReleaseDX9Device();

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

void CDX11VideoProcessor::SetShaderConvertColorParams()
{
	mp_csp_params csp_params;
	set_colorspace(m_srcExFmt, csp_params.color);
	csp_params.brightness = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Brightness) / 255;
	csp_params.contrast   = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Contrast);
	csp_params.hue        = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Hue) / 180 * acos(-1);
	csp_params.saturation = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Saturation);
	csp_params.gray       = m_srcParams.CSType == CS_GRAY;

	m_PSConvColorData.bEnable = m_srcParams.CSType == CS_YUV || fabs(csp_params.brightness) > 1e-4f || fabs(csp_params.contrast - 1.0f) > 1e-4f;;

	mp_cmat cmatrix;
	mp_get_csp_matrix(csp_params, cmatrix);

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

	SAFE_RELEASE(m_PSConvColorData.pConstants);
	SAFE_RELEASE(m_PSConvColorData.pConstants4);

	D3D11_BUFFER_DESC BufferDesc = {};
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(cbuffer);
	BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData = { &cbuffer, 0, 0 };
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateBuffer(&BufferDesc, &InitData, &m_PSConvColorData.pConstants));

	if (m_srcParams.cformat == CF_YUY2) {
		DirectX::XMFLOAT4 cbuffer4 = { (float)m_srcWidth, (float)m_srcHeight, 1.0f / m_srcWidth, 1.0f / m_srcHeight };
		BufferDesc.ByteWidth = sizeof(cbuffer4);
		InitData = { &cbuffer4, 0, 0 };
		EXECUTE_ASSERT(S_OK == m_pDevice->CreateBuffer(&BufferDesc, &InitData, &m_PSConvColorData.pConstants4));
	}
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
	SampDesc.Filter = D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT; // linear interpolation for magnification
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateSamplerState(&SampDesc, &m_pSamplerLinear));

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
		DLog(L"Graphics adapter: %s", m_strAdapterDescription.GetString());
	}

#ifdef _DEBUG
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

#if DIRECTWRITE_ENABLE
	HRESULT hr2 = m_TexOSD.Create(m_pDevice, DXGI_FORMAT_B8G8R8A8_UNORM, STATS_W, STATS_H, Tex2D_DefaultShaderRTarget);
	ASSERT(S_OK == hr2);

	hr2 = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(m_pDWriteFactory), reinterpret_cast<IUnknown**>(&m_pDWriteFactory));
	if (S_OK == hr2) {
		hr2 = m_pDWriteFactory->CreateTextFormat(
			L"Consolas",
			nullptr,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			16,
			L"", //locale
			&m_pTextFormat);
	}

	D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
	options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
	hr2 = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, &m_pD2D1Factory);
	if (S_OK == hr2) {
		CComPtr<IDXGISurface> pDxgiSurface;
		hr2 = m_TexOSD.pTexture->QueryInterface(IID_IDXGISurface, (void**)&pDxgiSurface);
		if (S_OK == hr2) {
			FLOAT dpiX;
			FLOAT dpiY;
			m_pD2D1Factory->GetDesktopDpi(&dpiX, &dpiY);

			D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
				D2D1_RENDER_TARGET_TYPE_DEFAULT,
				D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
				dpiX,
				dpiY);

			hr2 = m_pD2D1Factory->CreateDxgiSurfaceRenderTarget(pDxgiSurface, &props, &m_pD2D1RenderTarget);
			if (S_OK == hr2) {
				hr2 = m_pD2D1RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_pD2D1Brush);
			}
		}
	}
#else
	HRESULT hr2 = m_TexOSD.Create(m_pDevice, DXGI_FORMAT_B8G8R8A8_UNORM, STATS_W, STATS_H, Tex2D_DynamicShaderWrite);
	ASSERT(S_OK == hr2);
#endif

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

				D3D11_TEXTURE2D_DESC desc = {};
				m_pTextureSubPic->GetDesc(&desc);
				if (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
					D3D11_SHADER_RESOURCE_VIEW_DESC shaderDesc;
					shaderDesc.Format = desc.Format;
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

BOOL CDX11VideoProcessor::GetAlignmentSize(const CMediaType& mt, SIZE& Size)
{
	if (InitMediaType(&mt)) {
		if (m_TexSrcVideo.pTexture) {
			UINT RowPitch = 0;
			D3D11_MAPPED_SUBRESOURCE mappedResource = {};
			if (SUCCEEDED(m_pDeviceContext->Map(m_TexSrcVideo.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
				RowPitch = mappedResource.RowPitch;
				m_pDeviceContext->Unmap(m_TexSrcVideo.pTexture, 0);
			}

			if (RowPitch) {
				const auto FmtConvParams = GetFmtConvParams(mt.subtype);

				Size.cx = RowPitch / FmtConvParams.Packsize;

				if (FmtConvParams.CSType == CS_RGB) {
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
	if (FmtConvParams.cformat == CF_NV12 && !m_bVPEnableNV12
			|| (FmtConvParams.cformat == CF_P010 || FmtConvParams.cformat == CF_P016) && !m_bVPEnableP01x
			|| FmtConvParams.cformat == CF_YUY2 && !m_bVPEnableYUY2) {
		FmtConvParams.VP11Format = DXGI_FORMAT_UNKNOWN;
	}

	const GUID SubType = pmt->subtype;
	const BITMAPINFOHEADER* pBIH = nullptr;

	if (pmt->formattype == FORMAT_VideoInfo2) {
		const VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pmt->pbFormat;
		pBIH = &vih2->bmiHeader;
		m_srcRect = vih2->rcSource;
		m_trgRect = vih2->rcTarget;
		m_srcAspectRatioX = vih2->dwPictAspectRatioX;
		m_srcAspectRatioY = vih2->dwPictAspectRatioY;
		if (FmtConvParams.CSType == CS_YUV && (vih2->dwControlFlags & (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT))) {
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

	if (FmtConvParams.CSType == CS_YUV) {
		if (m_srcExFmt.NominalRange == DXVA2_NominalRange_Unknown) {
			m_srcExFmt.NominalRange = DXVA2_NominalRange_16_235;
		}
		if (m_srcExFmt.VideoTransferMatrix == DXVA2_VideoTransferMatrix_Unknown) {
			if (biWidth <= 1024 && biHeight <= 576) { // SD
				m_srcExFmt.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT601;
			} else { // HD
				m_srcExFmt.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT709;
			}
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

	m_srcPitch   = biSizeImage * 2 / (biHeight * FmtConvParams.PitchCoeff);
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

	switch (m_iTexFormat) {
	case TEXFMT_AUTO:
		m_InternalTexFmt = (FmtConvParams.CDepth > 8) ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
		break;
	case TEXFMT_8INT:    m_InternalTexFmt = DXGI_FORMAT_B8G8R8A8_UNORM; break;
	case TEXFMT_10INT:
	case TEXFMT_16FLOAT: m_InternalTexFmt = DXGI_FORMAT_R10G10B10A2_UNORM; break;
	default:
		ASSERT(FALSE);
	}

	// D3D11 Video Processor
	if (FmtConvParams.VP11Format != DXGI_FORMAT_UNKNOWN && !(m_VendorId == PCIV_NVIDIA && FmtConvParams.CSType == CS_RGB)) {
		// D3D11 VP does not work correctly if RGB32 with odd frame width (source or target) on Nvidia adapters

		if (S_OK == InitializeD3D11VP(FmtConvParams, biWidth, biHeight, false)) {
			UpdateConvertTexD3D11VP();

			if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084) {
				EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_CORRECTION_ST2084));
			}
			else if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG || m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG_temp) {
				EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_CORRECTION_HLG));
			}
			else if (m_srcExFmt.VideoTransferMatrix == VIDEOTRANSFERMATRIX_YCgCo) {
				EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_PSH11_CORRECTION_YCGCO));
			}

			m_inputMT = *pmt;
			UpdateStatsStatic();

			if (m_pFilter->m_pSubCallBack) {
				HRESULT hr = m_pFilter->m_pSubCallBack->SetDevice(m_pD3DDevEx);
			}

			return TRUE;
		}

		ReleaseVP();
	}

	// Tex Video Processor
	if (FmtConvParams.DX11Format != DXGI_FORMAT_UNKNOWN && S_OK == InitializeTexVP(FmtConvParams, biWidth, biHeight)) {
		UINT resid = 0;
		if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084) {
			resid = (FmtConvParams.cformat == CF_YUY2) ? IDF_PSH11_CONVERT_YUY2_ST2084
				: (FmtConvParams.cformat == CF_NV12 || FmtConvParams.cformat == CF_P010 || FmtConvParams.cformat == CF_P016) ? IDF_PSH11_CONVERT_NV12_ST2084
				: IDF_PSH11_CONVERT_COLOR_ST2084;
		}
		else if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG || m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG_temp) {
			resid = (FmtConvParams.cformat == CF_YUY2) ? IDF_PSH11_CONVERT_YUY2_HLG
				: (FmtConvParams.cformat == CF_NV12 || FmtConvParams.cformat == CF_P010 || FmtConvParams.cformat == CF_P016) ? IDF_PSH11_CONVERT_NV12_HLG
				: IDF_PSH11_CONVERT_COLOR_HLG;
		}
		else {
			resid = (FmtConvParams.cformat == CF_YUY2) ? IDF_PSH11_CONVERT_YUY2
				: (FmtConvParams.cformat == CF_NV12 || FmtConvParams.cformat == CF_P010 || FmtConvParams.cformat == CF_P016) ? IDF_PSH11_CONVERT_NV12
				: IDF_PSH11_CONVERT_COLOR;
		}
		EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSConvertColor, resid));

		UpdateCorrectionTex(m_videoRect.Width(), m_videoRect.Height());
		m_inputMT = *pmt;
		SetShaderConvertColorParams();
		UpdateStatsStatic();

		if (m_pFilter->m_pSubCallBack) {
			HRESULT hr = m_pFilter->m_pSubCallBack->SetDevice(m_pD3DDevEx);
		}

		return TRUE;
	}

	return FALSE;
}

HRESULT CDX11VideoProcessor::InitializeD3D11VP(const FmtConvParams_t& params, const UINT width, const UINT height, bool only_update_texture)
{
	auto& dxgiFormat = params.VP11Format;

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
		m_TexSrcVideo.Release();
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

	if (!only_update_texture) {
		m_VPCaps = {};
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
					m_VPCaps.FilterCaps = 0;
					break;
				}
				DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : FilterRange(%u) : %4d, %3d, %3d, %f",
					i, m_VPFilterRange[i].Minimum, m_VPFilterRange[i].Maximum, m_VPFilterRange[i].Default, m_VPFilterRange[i].Multiplier);
			}
		}

		if (m_VPCaps.FilterCaps) {
			m_VPFilterLevels[0] = ValueDXVA2toD3D11(m_DXVA2ProcAmpValues.Brightness, m_VPFilterRange[0]);
			m_VPFilterLevels[1] = ValueDXVA2toD3D11(m_DXVA2ProcAmpValues.Contrast,   m_VPFilterRange[1]);
			m_VPFilterLevels[2] = ValueDXVA2toD3D11(m_DXVA2ProcAmpValues.Hue,        m_VPFilterRange[2]);
			m_VPFilterLevels[3] = ValueDXVA2toD3D11(m_DXVA2ProcAmpValues.Saturation, m_VPFilterRange[3]);

			FilterRangeD3D11toDXVA2(m_DXVA2ProcAmpRanges[0], m_VPFilterRange[0]);
			FilterRangeD3D11toDXVA2(m_DXVA2ProcAmpRanges[1], m_VPFilterRange[1]);
			FilterRangeD3D11toDXVA2(m_DXVA2ProcAmpRanges[2], m_VPFilterRange[2]);
			FilterRangeD3D11toDXVA2(m_DXVA2ProcAmpRanges[3], m_VPFilterRange[3]);

			m_bUpdateFilters = true;
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

	hr = m_TexSrcVideo.Create(m_pDevice, dxgiFormat, width, height, Tex2D_DynamicDecoderWrite);
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
		m_srcParams      = params;
		m_srcDXGIFormat  = dxgiFormat;
		m_srcDXVA2Format = params.DXVA2Format;
		m_pConvertFn     = GetCopyFunction(params);
		m_srcWidth       = width;
		m_srcHeight      = height;
	}

	DLog(L"CDX11VideoProcessor::InitializeD3D11VP() completed successfully");

	return S_OK;
}

HRESULT CDX11VideoProcessor::InitializeTexVP(const FmtConvParams_t& params, const UINT width, const UINT height)
{
	auto& srcDXGIFormat = params.DX11Format;

	DLog(L"CDX11VideoProcessor::InitializeTexVP() started with input surface: %s, %u x %u", DXGIFormatToString(srcDXGIFormat), width, height);

	UINT texW = (params.cformat == CF_YUY2) ? width / 2 : width;

	HRESULT hr = m_TexSrcVideo.Create(m_pDevice, srcDXGIFormat, texW, height, Tex2D_DynamicShaderWrite);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeTexVP() : m_TexSrcVideo.Create() failed with error %s", HR2Str(hr));
		return hr;
	}

	hr = m_TexConvert.Create(m_pDevice, m_InternalTexFmt, width, height, Tex2D_DefaultShaderRTarget);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeTexVP() : m_TexConvert.Create() failed with error %s", HR2Str(hr));
		return hr;
	}

	m_TextureWidth   = width;
	m_TextureHeight  = height;
	m_srcParams      = params;
	m_srcDXGIFormat  = srcDXGIFormat;
	m_srcDXVA2Format = params.DXVA2Format;
	m_pConvertFn     = GetCopyFunction(params);
	m_srcWidth       = width;
	m_srcHeight      = height;

	// set default ProcAmp ranges
	SetDefaultDXVA2ProcAmpRanges(m_DXVA2ProcAmpRanges);

	DLog(L"CDX11VideoProcessor::InitializeTexVP() completed successfully");

	return S_OK;
}

void CDX11VideoProcessor::Start()
{
	m_rtStart = 0;
}

void CDX11VideoProcessor::Stop()
{
}

HRESULT CDX11VideoProcessor::ProcessSample(IMediaSample* pSample)
{
	REFERENCE_TIME rtStart, rtEnd;
	pSample->GetTime(&rtStart, &rtEnd);

	m_rtStart = rtStart;

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
		if (desc.Format != m_srcDXGIFormat) {
			return hr;
		}

		if (desc.Width != m_TextureWidth || desc.Height != m_TextureHeight) {
			if (m_pVideoProcessor) {
				hr = InitializeD3D11VP(m_srcParams, desc.Width, desc.Height, true);
			} else {
				hr = InitializeTexVP(m_srcParams, desc.Width, desc.Height);
			}
			if (FAILED(hr)) {
				return hr;
			}
		}

		// here should be used CopySubresourceRegion instead of CopyResource
		if (m_pVideoProcessor) {
			m_pDeviceContext->CopySubresourceRegion(m_pSrcTexture2D, 0, 0, 0, 0, pD3D11Texture2D, ArraySlice, nullptr);
		} else {
			m_pDeviceContext->CopySubresourceRegion(m_TexSrcVideo.pTexture, 0, 0, 0, 0, pD3D11Texture2D, ArraySlice, nullptr);
		}
	}
	else if (CComQIPtr<IMFGetService> pService = pSample) {
		m_bSrcFromGPU = true;

		CComPtr<IDirect3DSurface9> pSurface9;
		if (SUCCEEDED(pService->GetService(MR_BUFFER_SERVICE, IID_PPV_ARGS(&pSurface9)))) {
			D3DSURFACE_DESC desc = {};
			hr = pSurface9->GetDesc(&desc);
			if (FAILED(hr) || desc.Format != m_srcDXVA2Format) {
				return E_FAIL;
			}

			if (desc.Width != m_TextureWidth || desc.Height != m_TextureHeight) {
				if (m_pVideoProcessor) {
					hr = InitializeD3D11VP(m_srcParams, desc.Width, desc.Height, true);
				} else {
					hr = InitializeTexVP(m_srcParams, desc.Width, desc.Height);
				}
				if (FAILED(hr)) {
					return hr;
				}
			}

			D3DLOCKED_RECT lr_src;
			hr = pSurface9->LockRect(&lr_src, nullptr, D3DLOCK_READONLY); // slow
			if (S_OK == hr) {
				D3D11_MAPPED_SUBRESOURCE mappedResource = {};
				hr = m_pDeviceContext->Map(m_TexSrcVideo.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
				if (S_OK == hr) {
					ASSERT(m_pConvertFn);
					m_pConvertFn(m_TextureHeight, (BYTE*)mappedResource.pData, mappedResource.RowPitch, (BYTE*)lr_src.pBits, lr_src.Pitch); // slow
					m_pDeviceContext->Unmap(m_TexSrcVideo.pTexture, 0);
				}
				pSurface9->UnlockRect();
			}

			if (m_pVideoProcessor) {
				// ID3D11VideoProcessor does not use textures with D3D11_CPU_ACCESS_WRITE flag
				m_pDeviceContext->CopyResource(m_pSrcTexture2D, m_TexSrcVideo.pTexture);
			}
		}
	}
	else {
		m_bSrcFromGPU = false;

		BYTE* data = nullptr;
		const long size = pSample->GetActualDataLength();
		if (size > 0 && S_OK == pSample->GetPointer(&data)) {
			D3D11_MAPPED_SUBRESOURCE mappedResource = {};
			hr = m_pDeviceContext->Map(m_TexSrcVideo.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
			if (SUCCEEDED(hr)) {
				ASSERT(m_pConvertFn);
				BYTE* src = (m_srcPitch < 0) ? data + m_srcPitch * (1 - (int)m_srcHeight) : data;
				m_pConvertFn(m_srcHeight, (BYTE*)mappedResource.pData, mappedResource.RowPitch, src, m_srcPitch);
				m_pDeviceContext->Unmap(m_TexSrcVideo.pTexture, 0);
				if (m_pVideoProcessor) {
					// ID3D11VideoProcessor does not use textures with D3D11_CPU_ACCESS_WRITE flag
					m_pDeviceContext->CopyResource(m_pSrcTexture2D, m_TexSrcVideo.pTexture);
				}
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

	uint64_t tick1 = GetPreciseTick();

	if (field) {
		m_FieldDrawn = field;
	}

	CComPtr<ID3D11Texture2D> pBackBuffer;
	HRESULT hr = m_pDXGISwapChain1->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::Render() : GetBuffer() failed with error %s", HR2Str(hr));
		return hr;
	}

	if (!m_videoRect.IsRectEmpty() && !m_windowRect.IsRectEmpty()) {
		m_srcRenderRect = m_srcRect;
		m_dstRenderRect = m_videoRect;
		D3D11_TEXTURE2D_DESC desc = {};
		pBackBuffer->GetDesc(&desc);
		if (desc.Width && desc.Height) {
			ClipToSurface(desc.Width, desc.Height, m_srcRenderRect, m_dstRenderRect);
		}

		if (!(m_pVideoProcessor && m_bVPScaling) || m_pPSCorrection) {
			// fill the BackBuffer with black only when necessary
			ID3D11RenderTargetView* pRenderTargetView;
			if (S_OK == m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView)) {
				const FLOAT ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
				m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ClearColor);
				pRenderTargetView->Release();
			}
		}

		if (m_pVideoProcessor) {
			hr = ProcessD3D11(pBackBuffer, m_srcRenderRect, m_dstRenderRect, m_FieldDrawn == 2);
		} else {
			hr = ProcessTex(pBackBuffer, m_srcRenderRect, m_dstRenderRect);
		}
	}

	uint64_t tick2 = GetPreciseTick();

	if (m_pFilter->m_pSubCallBack && m_pShaderResourceSubPic) {
		HRESULT hr2 = S_OK;
		const CRect rSrcPri(CPoint(0, 0), m_windowRect.Size());
		const CRect rDstVid(m_videoRect);
		const auto rtStart = m_pFilter->m_rtStartTime + m_rtStart;

		if (CComQIPtr<ISubRenderCallback4> pSubCallBack4 = m_pFilter->m_pSubCallBack) {
			hr2 = pSubCallBack4->RenderEx3(rtStart, 0, 0, rDstVid, rDstVid, rSrcPri);
		} else {
			hr2 = m_pFilter->m_pSubCallBack->Render(rtStart, rDstVid.left, rDstVid.top, rDstVid.right, rDstVid.bottom, rSrcPri.Width(), rSrcPri.Height());
		}

		if (S_OK == hr2) {
			// flush Direct3D9 for immediate update Direct3D11 texture
			CComPtr<IDirect3DQuery9> pEventQuery;
			m_pD3DDevEx->CreateQuery(D3DQUERYTYPE_EVENT, &pEventQuery);
			if (pEventQuery) {
				pEventQuery->Issue(D3DISSUE_END);
				BOOL Data = FALSE;
				pEventQuery->GetData(&Data, sizeof(Data), D3DGETDATA_FLUSH);
			}

			D3D11_VIEWPORT VP;
			VP.TopLeftX = 0;
			VP.TopLeftY = 0;
			VP.Width = rSrcPri.Width();
			VP.Height = rSrcPri.Height();
			VP.MinDepth = 0.0f;
			VP.MaxDepth = 1.0f;
			hr2 = AlphaBltSub(m_pShaderResourceSubPic, pBackBuffer, rSrcPri, VP);
			ASSERT(S_OK == hr2);

			hr2 = m_pD3DDevEx->ColorFill(m_pSurface9SubPic, nullptr, D3DCOLOR_ARGB(255, 0, 0, 0));
		}
	}

	if (m_bShowStats) {
		uint64_t tick3 = GetPreciseTick();
		m_RenderStats.renderticks = tick2 - tick1;
		m_RenderStats.substicks = tick3 - tick2;
		hr = DrawStats(pBackBuffer);
		m_RenderStats.statsticks = GetPreciseTick() - tick3;
	}

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

	float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ClearColor);

	hr = m_pDXGISwapChain1->Present(0, 0);

	pRenderTargetView->Release();

	return hr;
}

void CDX11VideoProcessor::UpdateConvertTexD3D11VP()
{
	if (m_pVideoProcessor) {
		if (m_bVPScaling) {
			m_TexConvert.Release();
		} else {
			m_TexConvert.Create(m_pDevice, m_InternalTexFmt, m_TextureWidth, m_TextureHeight, Tex2D_DefaultShaderRTarget);
		}
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
		{IDF_PSH11_INTERP_MITCHELL4_X, IDF_PSH11_INTERP_MITCHELL4_Y, L"Mitchell-Netravali"},
		{IDF_PSH11_INTERP_CATMULL4_X,  IDF_PSH11_INTERP_CATMULL4_Y , L"Catmull-Rom"       },
		{IDF_PSH11_INTERP_LANCZOS2_X,  IDF_PSH11_INTERP_LANCZOS2_Y , L"Lanczos2"          },
		{IDF_PSH11_INTERP_LANCZOS3_X,  IDF_PSH11_INTERP_LANCZOS3_Y , L"Lanczos3"          },
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
		{IDF_PSH11_CONVOL_BOX_X,       IDF_PSH11_CONVOL_BOX_Y,       L"Box"          },
		{IDF_PSH11_CONVOL_BILINEAR_X,  IDF_PSH11_CONVOL_BILINEAR_Y,  L"Bilinear"     },
		{IDF_PSH11_CONVOL_HAMMING_X,   IDF_PSH11_CONVOL_HAMMING_Y,   L"Hamming"      },
		{IDF_PSH11_CONVOL_BICUBIC05_X, IDF_PSH11_CONVOL_BICUBIC05_Y, L"Bicubic"      },
		{IDF_PSH11_CONVOL_BICUBIC15_X, IDF_PSH11_CONVOL_BICUBIC15_Y, L"Bicubic sharp"},
		{IDF_PSH11_CONVOL_LANCZOS_X,   IDF_PSH11_CONVOL_LANCZOS_Y,   L"Lanczos"      }
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
			hr = D3D11VPPass(m_TexConvert.pTexture, rSrcRect, rSrcRect, second);
			hr = ResizeShader2Pass(m_TexConvert, m_TexCorrection.pTexture, rSrcRect, rCorrection);
		}
		hr = TextureCopyRect(m_TexCorrection, pRenderTarget, rCorrection, rDstRect, m_pPSCorrection, nullptr);
	}
	else {
		if (m_bVPScaling) {
			hr = D3D11VPPass(pRenderTarget, rSrcRect, rDstRect, second);
		} else {
			hr = D3D11VPPass(m_TexConvert.pTexture, rSrcRect, rSrcRect, second);
			hr = ResizeShader2Pass(m_TexConvert, pRenderTarget, rSrcRect, rDstRect);
		}
	}

	return hr;
}

HRESULT CDX11VideoProcessor::ProcessTex(ID3D11Texture2D* pRenderTarget, const CRect& rSrcRect, const CRect& rDstRect)
{
	HRESULT hr = S_OK;

	if (m_PSConvColorData.bEnable) {
		// Convert color pass
		hr = TextureConvertColor(m_TexSrcVideo, m_TexConvert.pTexture);
		// Resize
		hr = ResizeShader2Pass(m_TexConvert, pRenderTarget, rSrcRect, rDstRect);
	} else {
		// Resize
		hr = ResizeShader2Pass(m_TexSrcVideo, pRenderTarget, rSrcRect, rDstRect);
	}

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
		if (m_bUpdateFilters) {
			BOOL bEnable0 = (m_VPFilterLevels[0] != m_VPFilterRange[0].Default);
			BOOL bEnable1 = (m_VPFilterLevels[1] != m_VPFilterRange[1].Default);
			BOOL bEnable2 = (m_VPFilterLevels[2] != m_VPFilterRange[2].Default);
			BOOL bEnable3 = (m_VPFilterLevels[3] != m_VPFilterRange[3].Default);
			m_pVideoContext->VideoProcessorSetStreamFilter(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_FILTER_BRIGHTNESS, bEnable0, m_VPFilterLevels[0]);
			m_pVideoContext->VideoProcessorSetStreamFilter(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_FILTER_CONTRAST, bEnable1, m_VPFilterLevels[1]);
			m_pVideoContext->VideoProcessorSetStreamFilter(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_FILTER_HUE, bEnable2, m_VPFilterLevels[2]);
			m_pVideoContext->VideoProcessorSetStreamFilter(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_FILTER_SATURATION, bEnable3, m_VPFilterLevels[3]);
			m_bUpdateFilters = false;
		}

		// Output background color (black)
		static const D3D11_VIDEO_COLOR backgroundColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		m_pVideoContext->VideoProcessorSetOutputBackgroundColor(m_pVideoProcessor, FALSE, &backgroundColor);

		D3D11_VIDEO_PROCESSOR_COLOR_SPACE colorSpace = {};
		if (m_srcExFmt.value) {
			colorSpace.RGB_Range = 0; // output RGB always full range (0-255)
			colorSpace.YCbCr_Matrix = (m_srcExFmt.VideoTransferMatrix == DXVA2_VideoTransferMatrix_BT601) ? 0 : 1;
			colorSpace.Nominal_Range = (m_srcExFmt.NominalRange == DXVA2_NominalRange_0_255)
									 ? D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255
									 : D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
		}
		m_pVideoContext->VideoProcessorSetStreamColorSpace(m_pVideoProcessor, 0, &colorSpace);
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
	switch (value) {
	case TEXFMT_AUTO:
	case TEXFMT_8INT:
	case TEXFMT_10INT:
	case TEXFMT_16FLOAT:
		m_iTexFormat = value;
		break;
	default:
		DLog("CDX11VideoProcessor::SetTexFormat() unknown value %d", value);
		ASSERT(FALSE);
		return;
	}
}

void CDX11VideoProcessor::SetVPEnableFmts(bool bNV12, bool bP01x, bool bYUY2)
{
	m_bVPEnableNV12 = bNV12;
	m_bVPEnableP01x = bP01x;
	m_bVPEnableYUY2 = bYUY2;
}

void CDX11VideoProcessor::SetVPScaling(bool value)
{
	m_bVPScaling = value;

	UpdateConvertTexD3D11VP();
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
	if (m_srcParams.cformat) {
		m_strStatsStatic1.Format(L"MPC VR v%S, Direct3D 11", MPCVR_VERSION_STR);
		m_strStatsStatic1.AppendFormat(L"\nGraph. Adapter: %s", m_strAdapterDescription);

		m_strStatsStatic2.Format(L" %S %ux%u", m_srcParams.str, m_srcRectWidth, m_srcRectHeight);
		if (m_srcParams.CSType == CS_YUV) {
			LPCSTR strs[5] = {};
			//DXVA2_ExtendedFormat exFmt = SpecifyExtendedFormat(m_srcExFmt, FmtConvParams->CSType, m_srcRectWidth, m_srcRectHeight);
			GetExtendedFormatString(strs, m_srcExFmt, m_srcParams.CSType);
			m_strStatsStatic2.AppendFormat(L"\n  Range: %hS, Matrix: %hS, Lighting: %hS", strs[0], strs[1], strs[2]);
			m_strStatsStatic2.AppendFormat(L"\n  Primaries: %hS, Function: %hS", strs[3], strs[4]);
		}
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
	str.AppendFormat(L"\nTimes(ms): Copy%3llu, Render%3llu, Subs%3llu, Stats%3llu",
		m_RenderStats.copyticks * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.renderticks * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.substicks * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.statsticks * 1000 / GetPreciseTicksPerSecondI());;
#if 0
	str.AppendFormat(L"\n1:%6.03f, 2:%6.03f, 3:%6.03f, 4:%6.03f, 5:%6.03f, 6:%6.03f ms",
		m_RenderStats.t1 * 1000.0 / GetPreciseTicksPerSecondI(),
		m_RenderStats.t2 * 1000.0 / GetPreciseTicksPerSecondI(),
		m_RenderStats.t3 * 1000.0 / GetPreciseTicksPerSecondI(),
		m_RenderStats.t4 * 1000.0 / GetPreciseTicksPerSecondI(),
		m_RenderStats.t5 * 1000.0 / GetPreciseTicksPerSecondI(),
		m_RenderStats.t6 * 1000.0 / GetPreciseTicksPerSecondI());
#else
	str.AppendFormat(L"\nSync offset   : %+3lld ms", (m_RenderStats.syncoffset + 5000) / 10000);
#endif

#if DIRECTWRITE_ENABLE
	ID3D11RenderTargetView* pRenderTargetView = nullptr;
	HRESULT hr = m_pDevice->CreateRenderTargetView(m_TexOSD.pTexture, nullptr, &pRenderTargetView);
	if (S_OK == hr) {
		const FLOAT ClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.75f };
		m_pDeviceContext->ClearRenderTargetView(pRenderTargetView, ClearColor);
		m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);

		CComPtr<IDWriteTextLayout> pTextLayout;
		if (S_OK == m_pDWriteFactory->CreateTextLayout(str, str.GetLength(), m_pTextFormat, m_windowRect.right - 10, m_windowRect.bottom - 10, &pTextLayout)) {
			m_pD2D1RenderTarget->BeginDraw();
			m_pD2D1RenderTarget->DrawTextLayout(D2D1::Point2F(5.0f, 5.0f), pTextLayout, m_pD2D1Brush);
			static int col = STATS_W;
			if (--col < 0) {
				col = STATS_W;
			}
			D2D1_RECT_F rect = { col, STATS_H - 11, col + 5, STATS_H - 1 };
			m_pD2D1RenderTarget->FillRectangle(rect, m_pD2D1Brush);
			m_pD2D1RenderTarget->EndDraw();
		}

		D3D11_VIEWPORT VP;
		VP.TopLeftX = STATS_X;
		VP.TopLeftY = STATS_Y;
		VP.Width = STATS_W;
		VP.Height = STATS_H;
		VP.MinDepth = 0.0f;
		VP.MaxDepth = 1.0f;
		hr = AlphaBlt(m_TexOSD.pShaderResource, pRenderTarget, VP);

		pRenderTargetView->Release();
	}
#else
	D3D11_MAPPED_SUBRESOURCE mappedResource = {};
	HRESULT hr = m_pDeviceContext->Map(m_TexOSD.pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (SUCCEEDED(hr)) {
		m_StatsDrawing.DrawTextW((BYTE*)mappedResource.pData, mappedResource.RowPitch, str);
		m_pDeviceContext->Unmap(m_TexOSD.pTexture, 0);

		D3D11_VIEWPORT VP;
		VP.TopLeftX = STATS_X;
		VP.TopLeftY = STATS_Y;
		VP.Width = STATS_W;
		VP.Height = STATS_H;
		VP.MinDepth = 0.0f;
		VP.MaxDepth = 1.0f;
		AlphaBlt(m_TexOSD.pShaderResource, pRenderTarget, VP);
	}
#endif

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

		m_VPFilterLevels[0] = ValueDXVA2toD3D11(m_DXVA2ProcAmpValues.Brightness, m_VPFilterRange[0]);
		m_VPFilterLevels[1] = ValueDXVA2toD3D11(m_DXVA2ProcAmpValues.Contrast,   m_VPFilterRange[1]);
		m_VPFilterLevels[2] = ValueDXVA2toD3D11(m_DXVA2ProcAmpValues.Hue,        m_VPFilterRange[2]);
		m_VPFilterLevels[3] = ValueDXVA2toD3D11(m_DXVA2ProcAmpValues.Saturation, m_VPFilterRange[3]);
		m_bUpdateFilters = true;

		if (!m_pVideoProcessor) {
			SetShaderConvertColorParams();
		}
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
