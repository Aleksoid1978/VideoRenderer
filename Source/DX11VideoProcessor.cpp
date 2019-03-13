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

enum Tex2DType {
	Tex2D_Default,
	Tex2D_DynamicShaderWrite,
	Tex2D_DefaultRTarget,
	Tex2D_StagingRead,
	Tex2D_DefaultShaderRTargetGDI,
};

inline HRESULT CreateTex2D(ID3D11Device* pDevice, const DXGI_FORMAT format, const UINT width, const UINT height, const Tex2DType type, ID3D11Texture2D** ppTexture2D)
{
	D3D11_TEXTURE2D_DESC desc;
	desc.Width      = width;
	desc.Height     = height;
	desc.MipLevels  = 1;
	desc.ArraySize  = 1;
	desc.Format     = format;
	desc.SampleDesc = { 1, 0 };

	switch (type) {
	default:
	case Tex2D_Default:
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		break;
	case Tex2D_DynamicShaderWrite:
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		break;
	case Tex2D_DefaultRTarget:
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		break;
	case Tex2D_StagingRead:
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;
		break;
	case Tex2D_DefaultShaderRTargetGDI:
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
		break;
	}

	return pDevice->CreateTexture2D(&desc, nullptr, ppTexture2D);
}

// CDX11VideoProcessor

CDX11VideoProcessor::CDX11VideoProcessor(CMpcVideoRenderer* pFilter)
{
	m_pFilter = pFilter;
}

CDX11VideoProcessor::~CDX11VideoProcessor()
{
	m_pDXGISwapChain1.Release();
	m_pDXGIFactory2.Release();

	ReleaseDevice();

	if (m_hD3D11Lib) {
		FreeLibrary(m_hD3D11Lib);
	}
}

HRESULT CDX11VideoProcessor::Init(const int iSurfaceFmt)
{
	DLog(L"CDX11VideoProcessor::Init()");
	if (!m_hD3D11Lib) {
		m_hD3D11Lib = LoadLibraryW(L"d3d11.dll");
	}
	if (!m_hD3D11Lib) {
		DLog(L"CDX11VideoProcessor::Init() - failed to load d3d11.dll");
		return E_FAIL;
	}

	HRESULT (WINAPI *pfnD3D11CreateDevice)(
		IDXGIAdapter            *pAdapter,
		D3D_DRIVER_TYPE         DriverType,
		HMODULE                 Software,
		UINT                    Flags,
		const D3D_FEATURE_LEVEL *pFeatureLevels,
		UINT                    FeatureLevels,
		UINT                    SDKVersion,
		ID3D11Device            **ppDevice,
		D3D_FEATURE_LEVEL       *pFeatureLevel,
		ID3D11DeviceContext     **ppImmediateContext
	);

	(FARPROC &)pfnD3D11CreateDevice = GetProcAddress(m_hD3D11Lib, "D3D11CreateDevice");
	if (!pfnD3D11CreateDevice) {
		DLog(L"CDX11VideoProcessor::Init() - failed to get D3D11CreateDevice() function");
		return E_FAIL;
	}

	switch (iSurfaceFmt) {
	default:
	case SURFMT_8INT:
		m_VPOutputFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
		break;
	case SURFMT_10INT:
	case SURFMT_16FLOAT:
		m_VPOutputFmt = DXGI_FORMAT_R10G10B10A2_UNORM;
		break;
	// TODO
	//case SURFMT_16FLOAT:
	//	m_VPOutputFmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
	//	break;
	}

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1
	};
	D3D_FEATURE_LEVEL featurelevel;

	ID3D11Device *pDevice = nullptr;
	ID3D11DeviceContext *pDeviceContext = nullptr;

	HRESULT hr = pfnD3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
#ifdef _DEBUG
		D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
#else
		D3D11_CREATE_DEVICE_BGRA_SUPPORT,
#endif
		featureLevels,
		std::size(featureLevels),
		D3D11_SDK_VERSION,
		&pDevice,
		&featurelevel,
		&pDeviceContext);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::Init() : D3D11CreateDevice() failed with error %s", HR2Str(hr));
		return hr;
	}
	DLog(L"CDX11VideoProcessor::Init() - D3D11CreateDevice() successfully with feature level %d.%d", (featurelevel >> 12), (featurelevel >> 8) & 0xF);

	hr = SetDevice(pDevice, pDeviceContext);

	return hr;
}

void CDX11VideoProcessor::ReleaseVP()
{
	m_pFilter->ResetStreamingTimes2();
	m_RenderStats.Reset();

	m_pSrcTexture2D_CPU.Release();
	m_pSrcTexture2D.Release();

	SAFE_RELEASE(m_pShaderResource);
	SAFE_RELEASE(m_pPixelShaderConstants);
	SAFE_RELEASE(m_pSamplerLinear);
	SAFE_RELEASE(m_pVertexBuffer);

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

	m_pPS_ConvertColor.Release();

#if VER_PRODUCTBUILD >= 10000
	m_pVideoContext1.Release();
#endif
	m_pVideoContext.Release();

	m_pInputLayout.Release();
	m_pVS_Simple.Release();
	m_pPS_Simple.Release();
	SAFE_RELEASE(m_pSamplerPoint);
	SAFE_RELEASE(m_pOSDVertexBuffer);

	m_pDeviceContext.Release();
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

#if VER_PRODUCTBUILD >= 10000
	m_pVideoContext->QueryInterface(__uuidof(ID3D11VideoContext1), (void**)&m_pVideoContext1);
#endif

	D3D11_SAMPLER_DESC SampDesc = {};
	SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SampDesc.MinLOD = 0;
	SampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateSamplerState(&SampDesc, &m_pSamplerPoint));

	LPVOID data;
	DWORD size;
	EXECUTE_ASSERT(S_OK == GetDataFromResource(data, size, IDF_VSHADER11_SIMPLE));
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateVertexShader(data, size, nullptr, &m_pVS_Simple));

	D3D11_INPUT_ELEMENT_DESC Layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateInputLayout(Layout, std::size(Layout), data, size, &m_pInputLayout));
	m_pDeviceContext->IASetInputLayout(m_pInputLayout);

	EXECUTE_ASSERT(S_OK == GetDataFromResource(data, size, IDF_PSHADER11_SIMPLE));
	EXECUTE_ASSERT(S_OK == m_pDevice->CreatePixelShader(data, size, nullptr, &m_pPS_Simple));

	EXECUTE_ASSERT(S_OK == GetDataFromResource(data, size, IDF_PSHADER11_CONVERTCOLOR));
	EXECUTE_ASSERT(S_OK == m_pDevice->CreatePixelShader(data, size, nullptr, &m_pPS_ConvertColor));

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
		hr = InitSwapChain(m_hWnd);
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

	m_pOSDTex2D.Release();
	HRESULT hr2 = CreateTex2D(m_pDevice, DXGI_FORMAT_B8G8R8A8_UNORM, STATS_W, STATS_H, Tex2D_DefaultShaderRTargetGDI, &m_pOSDTex2D);
	ASSERT(S_OK == hr2);

	return hr;
}

HRESULT CDX11VideoProcessor::InitSwapChain(const HWND hwnd, UINT width/* = 0*/, UINT height/* = 0*/)
{
	DLog(L"CDX11VideoProcessor::InitSwapChain()");

	CheckPointer(hwnd, E_FAIL);
	CheckPointer(m_pVideoDevice, E_FAIL);

	HRESULT hr = S_OK;

	if (!width || !height) {
		RECT rc;
		GetClientRect(hwnd, &rc);
		width = rc.right - rc.left;
		height = rc.bottom - rc.top;
	}

	hr = SetVertices(width, height);

	if (m_hWnd && m_pDXGISwapChain1) {
		const HMONITOR hCurMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
		const HMONITOR hNewMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		if (hCurMonitor == hNewMonitor) {
			hr = m_pDXGISwapChain1->ResizeBuffers(1, width, height, m_VPOutputFmt, 0);
			if (SUCCEEDED(hr)) {
				return hr;
			}
		}
	}

	m_pDXGISwapChain1.Release();

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.Format = m_VPOutputFmt;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = 1;
	desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	hr = m_pDXGIFactory2->CreateSwapChainForHwnd(m_pDevice, hwnd, &desc, nullptr, nullptr, &m_pDXGISwapChain1);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitSwapChain() : CreateSwapChainForHwnd() failed with error %s", HR2Str(hr));
		return hr;
	}

	m_hWnd = hwnd;

	return hr;
}

BOOL CDX11VideoProcessor::VerifyMediaType(const CMediaType* pmt)
{
	auto FmtConvParams = GetFmtConvParams(pmt->subtype);
	if (!FmtConvParams || FmtConvParams->VP11Format == DXGI_FORMAT_UNKNOWN && FmtConvParams->DX11Format == DXGI_FORMAT_UNKNOWN) {
		return FALSE;
	}

	const BITMAPINFOHEADER* pBIH = nullptr;

	if (pmt->formattype == FORMAT_VideoInfo2) {
		const VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pmt->pbFormat;
		pBIH = &vih2->bmiHeader;
	}
	else if (pmt->formattype == FORMAT_VideoInfo) {
		const VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)pmt->pbFormat;
		pBIH = &vih->bmiHeader;
	}
	else {
		return FALSE;
	}

	if (pBIH->biWidth <= 0 || !pBIH->biHeight || (!pBIH->biSizeImage && pBIH->biCompression != BI_RGB)) {
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

	UINT biWidth     = pBIH->biWidth;
	UINT biHeight    = labs(pBIH->biHeight);
	UINT biSizeImage = pBIH->biSizeImage;
	if (pBIH->biSizeImage == 0 && pBIH->biCompression == BI_RGB) { // biSizeImage may be zero for BI_RGB bitmaps
		biSizeImage = biWidth * biHeight * pBIH->biBitCount / 8;
	}

	if (m_srcRect.IsRectNull() && m_trgRect.IsRectNull()) {
		// Hmm
		m_srcRect.SetRect(0, 0, biWidth, biHeight);
		m_trgRect.SetRect(0, 0, biWidth, biHeight);
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

	// D3D11 Video Processor
	if (1 && FmtConvParams->VP11Format != DXGI_FORMAT_UNKNOWN && S_OK == InitializeD3D11VP(FmtConvParams->VP11Format, biWidth, biHeight, false)) {
		m_srcSubType = SubType;
		UpdateStatsStatic();
		m_inputMT = *pmt;
		return TRUE;
	}

	ReleaseVP();

	// Tex Video Processor
	if (FmtConvParams->DX11Format != DXGI_FORMAT_UNKNOWN && S_OK == InitializeTexVP(FmtConvParams->DX11Format, biWidth, biHeight)) {
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

		SAFE_RELEASE(m_pPixelShaderConstants);

		D3D11_BUFFER_DESC BufferDesc = {};
		BufferDesc.Usage = D3D11_USAGE_DEFAULT;
		BufferDesc.ByteWidth = sizeof(cbuffer);
		BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		BufferDesc.CPUAccessFlags = 0;
		D3D11_SUBRESOURCE_DATA InitData = { &cbuffer, 0, 0 };
		HRESULT hr = m_pDevice->CreateBuffer(&BufferDesc, &InitData, &m_pPixelShaderConstants);
		if (FAILED(hr)) {
			DLog(L"CDX11VideoProcessor::InitMediaType() : CreateBuffer() failed with error %s", HR2Str(hr));
			return FALSE;
		}

		m_srcSubType = SubType;
		UpdateStatsStatic();
		m_inputMT = *pmt;
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
		m_pSrcTexture2D_CPU.Release();
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

	if (m_VPOutputFmt != DXGI_FORMAT_B8G8R8A8_UNORM) {
		hr = m_pVideoProcessorEnum->CheckVideoProcessorFormat(m_VPOutputFmt, &uiFlags);
		if (FAILED(hr) || 0 == (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
			m_VPOutputFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
		}
	}
	if (m_VPOutputFmt == DXGI_FORMAT_B8G8R8A8_UNORM) {
		hr = m_pVideoProcessorEnum->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM, &uiFlags);
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

	hr = CreateTex2D(m_pDevice, dxgiFormat, width, height, Tex2D_DynamicShaderWrite, &m_pSrcTexture2D_CPU);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : CreateTex2D(m_pSrcTexture2D_CPU) failed with error %s", HR2Str(hr));
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
		m_srcWidth      = width;
		m_srcHeight     = height;
	}

	if (!m_windowRect.IsRectEmpty()) {
		SetVertices(m_windowRect.Width(), m_windowRect.Height());
	}

	DLog(L"CDX11VideoProcessor::InitializeD3D11VP() completed successfully");

	return S_OK;
}

HRESULT CDX11VideoProcessor::InitializeTexVP(const DXGI_FORMAT dxgiFormat, const UINT width, const UINT height)
{
	DLog(L"CDX11VideoProcessor::InitializeTexVP() started with input surface: %s, %u x %u", DXGIFormatToString(dxgiFormat), width, height);

	HRESULT hr = S_OK;

	hr = CreateTex2D(m_pDevice, dxgiFormat, width, height, Tex2D_DynamicShaderWrite, &m_pSrcTexture2D_CPU);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeTexVP() : CreateTex2D(m_pSrcTexture2D_CPU) failed with error %s", HR2Str(hr));
		return hr;
	}

	hr = CreateTex2D(m_pDevice, dxgiFormat, width, height, Tex2D_Default, &m_pSrcTexture2D);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeTexVP() : CreateTex2D(m_pSrcTexture2D) failed with error %s", HR2Str(hr));
		return hr;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC ShaderDesc;
	ShaderDesc.Format = dxgiFormat;
	ShaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	ShaderDesc.Texture2D.MostDetailedMip = 0; // = Texture2D desc.MipLevels - 1
	ShaderDesc.Texture2D.MipLevels = 1;       // = Texture2D desc.MipLevels
	hr = m_pDevice->CreateShaderResourceView(m_pSrcTexture2D_CPU, &ShaderDesc, &m_pShaderResource);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeTexVP() : CreateShaderResourceView() failed with error %s", HR2Str(hr));
		return hr;
	}

	// Create the sample state
	D3D11_SAMPLER_DESC SampDesc;
	ZeroMemory(&SampDesc, sizeof(SampDesc));
	SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SampDesc.MinLOD = 0;
	SampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	hr = m_pDevice->CreateSamplerState(&SampDesc, &m_pSamplerLinear);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeTexVP() : CreateSamplerState() failed with error %s", HR2Str(hr));
		return hr;
	}

	m_TextureWidth  = width;
	m_TextureHeight = height;
	m_srcDXGIFormat = dxgiFormat;
	m_srcWidth      = width;
	m_srcHeight     = height;

	if (!m_windowRect.IsRectEmpty()) {
		SetVertices(m_windowRect.Width(), m_windowRect.Height());
	}

	DLog(L"CDX11VideoProcessor::InitializeTexVP() completed successfully");

	return S_OK;
}

HRESULT CDX11VideoProcessor::SetVertices(UINT dstW, UINT dstH)
{
	CheckPointer(m_pSrcTexture2D_CPU, E_POINTER);

	const float src_dx = 1.0f / m_TextureWidth;
	const float src_dy = 1.0f / m_TextureHeight;
	const float src_l = src_dx * m_srcRect.left;
	const float src_r = src_dx * m_srcRect.right;
	const float src_t = src_dy * m_srcRect.top;
	const float src_b = src_dy * m_srcRect.bottom;

	const float dst_dx = 2.0f / dstW;
	const float dst_dy = 2.0f / dstH;
	const float dst_l = dst_dx * m_videoRect.left   - 1.0f;
	const float dst_r = dst_dx * m_videoRect.right  - 1.0f;
	const float dst_t = dst_dy * m_videoRect.top    - 1.0f;
	const float dst_b = dst_dy * m_videoRect.bottom - 1.0f;

	VERTEX Vertices[6] = {
		// Vertices for drawing whole texture
		// |\
		// |_\ lower left triangle
		{ DirectX::XMFLOAT3(dst_l, dst_t, 0), DirectX::XMFLOAT2(src_l, src_b) },
		{ DirectX::XMFLOAT3(dst_l, dst_b, 0), DirectX::XMFLOAT2(src_l, src_t) },
		{ DirectX::XMFLOAT3(dst_r, dst_t, 0), DirectX::XMFLOAT2(src_r, src_b) },
		// ___
		// \ |
		//  \| upper right triangle
		{ DirectX::XMFLOAT3(dst_r, dst_t, 0), DirectX::XMFLOAT2(src_r, src_b) },
		{ DirectX::XMFLOAT3(dst_l, dst_b, 0), DirectX::XMFLOAT2(src_l, src_t) },
		{ DirectX::XMFLOAT3(dst_r, dst_b, 0), DirectX::XMFLOAT2(src_r, src_t) },
	};

	D3D11_BUFFER_DESC BufferDesc;
	ZeroMemory(&BufferDesc, sizeof(BufferDesc));
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(Vertices);
	BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData = { Vertices, 0, 0 };

	SAFE_RELEASE(m_pVertexBuffer);
	HRESULT hr = m_pDevice->CreateBuffer(&BufferDesc, &InitData, &m_pVertexBuffer);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::SetVertices() : CreateBuffer() failed with error %s", HR2Str(hr));
		SAFE_RELEASE(m_pShaderResource);
		return hr;
	}

	// lower left triangle
	Vertices[0] = { DirectX::XMFLOAT3(-1, -1, 0), DirectX::XMFLOAT2(0, 1) };
	Vertices[1] = { DirectX::XMFLOAT3(-1, +1, 0), DirectX::XMFLOAT2(0, 0) };
	Vertices[2] = { DirectX::XMFLOAT3(+1, -1, 0), DirectX::XMFLOAT2(1, 1) };
	// upper right triangle
	Vertices[3] = { DirectX::XMFLOAT3(+1, -1, 0), DirectX::XMFLOAT2(1, 1) };
	Vertices[4] = { DirectX::XMFLOAT3(-1, +1, 0), DirectX::XMFLOAT2(0, 0) };
	Vertices[5] = { DirectX::XMFLOAT3(+1, +1, 0), DirectX::XMFLOAT2(1, 0) };

	SAFE_RELEASE(m_pOSDVertexBuffer);
	hr = m_pDevice->CreateBuffer(&BufferDesc, &InitData, &m_pOSDVertexBuffer);

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
	CRefTime rtClock;

	HRESULT hr = CopySample(pSample);
	if (FAILED(hr)) {
		m_RenderStats.failed++;
		return hr;
	}

	// always Render(1) a frame after CopySample()
	hr = Render(1);
	m_pFilter->m_DrawStats.Add(GetPreciseTick());
	m_pFilter->StreamTime(rtClock);

	m_RenderStats.syncoffset = rtClock - rtStart;

	if (SecondFramePossible()) {
		if (rtEnd < rtClock) {
			m_RenderStats.dropped2++;
			return S_FALSE; // skip frame
		}

		hr = Render(2);
		m_pFilter->m_DrawStats.Add(GetPreciseTick());
		m_pFilter->StreamTime(rtClock);

		rtStart += rtFrameDur / 2;
		m_RenderStats.syncoffset = rtClock - rtStart;
	}

	return hr;
}

#define BREAK_ON_ERROR(hr) { if (FAILED(hr)) { break; }}

HRESULT CDX11VideoProcessor::CopySample(IMediaSample* pSample)
{
	CheckPointer(m_pSrcTexture2D, E_FAIL);
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
			hr = m_pDeviceContext->Map(m_pSrcTexture2D_CPU, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
			if (SUCCEEDED(hr)) {
				ASSERT(m_pConvertFn);
				BYTE* src = (m_srcPitch < 0) ? data + m_srcPitch * (1 - (int)m_srcHeight) : data;
				m_pConvertFn(m_srcHeight, (BYTE*)mappedResource.pData, mappedResource.RowPitch, src, m_srcPitch);
				m_pDeviceContext->Unmap(m_pSrcTexture2D_CPU, 0);
				m_pDeviceContext->CopyResource(m_pSrcTexture2D, m_pSrcTexture2D_CPU); // we can't use texture with D3D11_CPU_ACCESS_WRITE flag
			}
		}
	}

	m_RenderStats.copyticks = GetPreciseTick() - ticks;

	return hr;
}

HRESULT CDX11VideoProcessor::Render(int field)
{
	CheckPointer(m_pSrcTexture2D_CPU, E_FAIL);
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

	if (!m_videoRect.IsRectEmpty() && !m_windowRect.IsRectEmpty()) {
		CRect rSrcRect(m_srcRect);
		CRect rDstRect(m_videoRect);
		D3D11_TEXTURE2D_DESC desc = {};
		pBackBuffer->GetDesc(&desc);
		if (desc.Width && desc.Height) {
			ClipToSurface(desc.Width, desc.Height, rSrcRect, rDstRect);
		}

		if (m_pVideoProcessor) {
			hr = ProcessD3D11(pBackBuffer, rSrcRect, rDstRect, m_windowRect, m_FieldDrawn == 2);
		} else {
			hr = ProcessTex(pBackBuffer, rSrcRect, rDstRect, m_windowRect);
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

HRESULT CDX11VideoProcessor::ProcessD3D11(ID3D11Texture2D* pRenderTarget, const RECT* pSrcRect, const RECT* pDstRect, const RECT* pWndRect, const bool second)
{
	if (!second) {
		// input format
		m_pVideoContext->VideoProcessorSetStreamFrameFormat(m_pVideoProcessor, 0, m_SampleFormat);

		// Output rate (repeat frames)
		m_pVideoContext->VideoProcessorSetStreamOutputRate(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, TRUE, nullptr);

		// disable automatic video quality by driver
		m_pVideoContext->VideoProcessorSetStreamAutoProcessingMode(m_pVideoProcessor, 0, FALSE);

		// Source rect
		m_pVideoContext->VideoProcessorSetStreamSourceRect(m_pVideoProcessor, 0, TRUE, pSrcRect);

		// Dest rect
		m_pVideoContext->VideoProcessorSetStreamDestRect(m_pVideoProcessor, 0, pDstRect ? TRUE : FALSE, pDstRect);
		m_pVideoContext->VideoProcessorSetOutputTargetRect(m_pVideoProcessor, pWndRect ? TRUE : FALSE, pWndRect);

		// filters
		m_pVideoContext->VideoProcessorSetStreamFilter(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_FILTER_BRIGHTNESS, m_VPFilterSettings[0].Enabled, m_VPFilterSettings[0].Level);
		m_pVideoContext->VideoProcessorSetStreamFilter(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_FILTER_CONTRAST,   m_VPFilterSettings[1].Enabled, m_VPFilterSettings[1].Level);
		m_pVideoContext->VideoProcessorSetStreamFilter(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_FILTER_HUE,        m_VPFilterSettings[2].Enabled, m_VPFilterSettings[2].Level);
		m_pVideoContext->VideoProcessorSetStreamFilter(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_FILTER_SATURATION, m_VPFilterSettings[3].Enabled, m_VPFilterSettings[3].Level);

		// Output background color (black)
		static const D3D11_VIDEO_COLOR backgroundColor = { 0.0f, 0.0f, 0.0f, 1.0f};
		m_pVideoContext->VideoProcessorSetOutputBackgroundColor(m_pVideoProcessor, FALSE, &backgroundColor);

#if VER_PRODUCTBUILD >= 10000
		if (m_pVideoContext1) {
			DXGI_COLOR_SPACE_TYPE ColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
			if (m_srcExFmt.value) {
				if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084) { // SMPTE ST 2084
					ColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020;
				} else if (m_srcExFmt.VideoTransferMatrix == DXVA2_VideoTransferMatrix_BT601) { // BT.601
					if (m_srcExFmt.NominalRange == DXVA2_NominalRange_16_235) {
						ColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
					} else {
						ColorSpace = DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601;
					}
				} else { // BT.709
					if (m_srcExFmt.NominalRange == DXVA2_NominalRange_16_235) {
						ColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
					} else {
						ColorSpace = DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601;
					}
				}
			} else {
				if (m_srcDXGIFormat == DXGI_FORMAT_B8G8R8X8_UNORM) {
					ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
				} else {
					if (m_srcWidth <= 1024 && m_srcHeight <= 576) { // SD
						ColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
					} else { // HD
						ColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
					}
				}
			}
			m_pVideoContext1->VideoProcessorSetStreamColorSpace1(m_pVideoProcessor, 0, ColorSpace);
			m_pVideoContext1->VideoProcessorSetOutputColorSpace1(m_pVideoProcessor, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
		} else
#endif
		{
			// Stream color space
			D3D11_VIDEO_PROCESSOR_COLOR_SPACE colorSpace = {};
			if (m_srcExFmt.value) {
				colorSpace.RGB_Range = m_srcExFmt.NominalRange == DXVA2_NominalRange_16_235 ? 1 : 0;
				colorSpace.YCbCr_Matrix = m_srcExFmt.VideoTransferMatrix == DXVA2_VideoTransferMatrix_BT601 ? 0 : 1;
			} else {
				colorSpace.RGB_Range = 1;
				if (m_srcWidth <= 1024 && m_srcHeight <= 576) { // SD
					colorSpace.YCbCr_Matrix = 0;
				} else { // HD
					colorSpace.YCbCr_Matrix = 1;
				}
			}
			m_pVideoContext->VideoProcessorSetStreamColorSpace(m_pVideoProcessor, 0, &colorSpace);

			// Output color space
			colorSpace.RGB_Range = 0;
			m_pVideoContext->VideoProcessorSetOutputColorSpace(m_pVideoProcessor, &colorSpace);
		}
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

HRESULT CDX11VideoProcessor::ProcessTex(ID3D11Texture2D* pRenderTarget, const RECT* pSrcRect, const RECT* pDstRect, const RECT* pWndRect)
{
	ID3D11RenderTargetView* pRenderTargetView;
	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (FAILED(hr)) {
		return hr;
	}

	D3D11_TEXTURE2D_DESC RTDesc;
	pRenderTarget->GetDesc(&RTDesc);

	D3D11_VIEWPORT VP;
	VP.Width = static_cast<FLOAT>(RTDesc.Width);
	VP.Height = static_cast<FLOAT>(RTDesc.Height);
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;
	VP.TopLeftX = 0;
	VP.TopLeftY = 0;
	m_pDeviceContext->RSSetViewports(1, &VP);

	// Set resources
	UINT Stride = sizeof(VERTEX);
	UINT Offset = 0;
	FLOAT blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	m_pDeviceContext->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
	m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
	m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
	m_pDeviceContext->PSSetShader(m_pPS_ConvertColor, nullptr, 0);
	m_pDeviceContext->PSSetShaderResources(0, 1, &m_pShaderResource);
	m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerLinear);
	m_pDeviceContext->PSSetConstantBuffers(0, 1, &m_pPixelShaderConstants);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer, &Stride, &Offset);

	// Draw textured quad onto render target
	m_pDeviceContext->Draw(6, 0);

	return S_OK;
}

HRESULT CDX11VideoProcessor::GetVideoSize(long *pWidth, long *pHeight)
{
	CheckPointer(pWidth, E_POINTER);
	CheckPointer(pHeight, E_POINTER);

	*pWidth = m_srcWidth;
	*pHeight = m_srcHeight;

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
		hr = ProcessD3D11(pRGB32Texture2D, rSrcRect, nullptr, nullptr, false);
	} else {
		hr = ProcessTex(pRGB32Texture2D, rSrcRect, rDstRect, m_windowRect);
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

	return S_OK;
}

void CDX11VideoProcessor::UpdateStatsStatic()
{
	auto FmtConvParams = GetFmtConvParams(m_srcSubType);
	if (FmtConvParams) {
		m_strStatsStatic.Format(L" %S %ux%u", FmtConvParams->str, m_srcWidth, m_srcHeight);
		m_strStatsStatic.AppendFormat(L"\nVP output fmt : %s", DXGIFormatToString(m_VPOutputFmt));
		m_strStatsStatic.AppendFormat(L"\nVideoProcessor: %s", m_pVideoProcessor ? L"D3D11" : L"Shaders");
	} else {
		m_strStatsStatic.Empty();
	}
}

HRESULT CDX11VideoProcessor::DrawStats(ID3D11Texture2D* pRenderTarget)
{
	if (m_windowRect.IsRectEmpty()) {
		return E_ABORT;
	}

	CStringW str = L"Direct3D 11";
	str.AppendFormat(L"\nFrame rate    : %7.03f", m_pFilter->m_FrameStats.GetAverageFps());
	if (m_SampleFormat != D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE) {
		str.AppendChar(L'i');
	}
	str.AppendFormat(L",%7.03f", m_pFilter->m_DrawStats.GetAverageFps());
	str.Append(L"\nInput format  :");
	if (m_bSrcFromGPU) {
		str.Append(L" GPU");
	}
	str.Append(m_strStatsStatic);
	str.AppendFormat(L"\nFrames: %5u, skiped: %u/%u, failed: %u",
		m_pFilter->m_FrameStats.GetFrames(), m_pFilter->m_DrawStats.m_dropped, m_RenderStats.dropped2, m_RenderStats.failed);
	str.AppendFormat(L"\nCopyTime:%3llu ms, RenderTime:%3llu ms",
		m_RenderStats.copyticks * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.renderticks * 1000 / GetPreciseTicksPerSecondI());
	str.AppendFormat(L"\nSync offset   : %+3lld ms", (m_RenderStats.syncoffset + 5000) / 10000);

	CComPtr<IDXGISurface1> pDxgiSurface1;
	HRESULT hr = m_pOSDTex2D->QueryInterface(IID_IDXGISurface1, (void**)&pDxgiSurface1);
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
			VP.Width    = STATS_W;
			VP.Height   = STATS_H;
			VP.MinDepth = 0.0f;
			VP.MaxDepth = 1.0f;
			VP.TopLeftX = STATS_X;
			VP.TopLeftY = STATS_Y;
			m_pDeviceContext->RSSetViewports(1, &VP);

			D3D11_SHADER_RESOURCE_VIEW_DESC ShaderDesc = {};
			ShaderDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			ShaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			ShaderDesc.Texture2D.MostDetailedMip = 0; // = Texture2D desc.MipLevels - 1
			ShaderDesc.Texture2D.MipLevels = 1;       // = Texture2D desc.MipLevels
			ID3D11ShaderResourceView* pShaderResource = nullptr;
			hr = m_pDevice->CreateShaderResourceView(m_pOSDTex2D, &ShaderDesc, &pShaderResource);
			if (FAILED(hr)) {
				DLog(L"CDX11VideoProcessor::DrawStats() : CreateShaderResourceView() failed with error %s", HR2Str(hr));
				pRenderTargetView->Release();
				return hr;
			}

			D3D11_BLEND_DESC bdesc = {};
			bdesc.RenderTarget[0].BlendEnable = TRUE;
			bdesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			bdesc.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_ALPHA;
			bdesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			bdesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			bdesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
			bdesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			bdesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			CComPtr<ID3D11BlendState> pBlendState;
			hr = m_pDevice->CreateBlendState(&bdesc, &pBlendState);
			if (FAILED(hr)) {
				DLog(L"CDX11VideoProcessor::DrawStats() : CreateBlendState() failed with error %s", HR2Str(hr));
				pShaderResource->Release();
				pRenderTargetView->Release();
				return hr;
			}

			// Set resources
			UINT Stride = sizeof(VERTEX);
			UINT Offset = 0;
			m_pDeviceContext->OMSetBlendState(pBlendState, nullptr, 0xffffffff);
			m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
			m_pDeviceContext->VSSetShader(m_pVS_Simple, nullptr, 0);
			m_pDeviceContext->PSSetShader(m_pPS_Simple, nullptr, 0);
			m_pDeviceContext->PSSetShaderResources(0, 1, &pShaderResource);
			m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerPoint);
			m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pOSDVertexBuffer, &Stride, &Offset);

			// Draw textured quad onto render target
			m_pDeviceContext->Draw(6, 0);

			pShaderResource->Release();
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
