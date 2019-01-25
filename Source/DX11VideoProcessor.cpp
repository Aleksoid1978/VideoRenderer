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
#include <directxcolors.h>
#include <dwmapi.h>
#include "Helper.h"
#include "Time.h"
#include "resource.h"
#include "DX11VideoProcessor.h"
#include "./Include/ID3DVideoMemoryConfiguration.h"


// CDX11VideoProcessor

CDX11VideoProcessor::CDX11VideoProcessor(CBaseRenderer* pFilter)
{
	m_pFilter = pFilter;
}

CDX11VideoProcessor::~CDX11VideoProcessor()
{
	m_pTextFormat.Release();
	m_pDWriteFactory.Release();

	ReleaseD2D1RenderTarget();
	m_pD2D1Factory.Release();

	m_pDXGISwapChain1.Release();
	m_pDXGIFactory2.Release();

	ReleaseDevice();

	if (m_hD3D11Lib) {
		FreeLibrary(m_hD3D11Lib);
	}
}

HRESULT CDX11VideoProcessor::Init(const int iSurfaceFmt)
{
	if (!m_hD3D11Lib) {
		m_hD3D11Lib = LoadLibraryW(L"d3d11.dll");
	}
	if (!m_hD3D11Lib) {
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
		return E_FAIL;
	}

	switch (iSurfaceFmt) {
	default:
	case 0:
		m_VPOutputFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
		break;
	case 1:
		m_VPOutputFmt = DXGI_FORMAT_R10G10B10A2_UNORM;
		break;
	case 2:
		m_VPOutputFmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
		break;
	}

	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1 };
	D3D_FEATURE_LEVEL featurelevel;

	ID3D11Device *pDevice = nullptr;
	ID3D11DeviceContext *pImmediateContext = nullptr;

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
		&pImmediateContext);
	if (FAILED(hr)) {
		return hr;
	}

	hr = SetDevice(pDevice, pImmediateContext);

	D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
	options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

	HRESULT hr2 = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, &m_pD2D1Factory);
	if (S_OK == hr2) {
		hr2 = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(m_pDWriteFactory), reinterpret_cast<IUnknown**>(&m_pDWriteFactory));
		if (S_OK == hr2) {
			hr2 = m_pDWriteFactory->CreateTextFormat(
				L"Consolas",
				nullptr,
				DWRITE_FONT_WEIGHT_NORMAL,
				DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL,
				20,
				L"", //locale
				&m_pTextFormat);
		}
	}

#if 0
	DWM_TIMING_INFO timinginfo = { sizeof(DWM_TIMING_INFO) };
	if (S_OK == DwmGetCompositionTimingInfo(nullptr, &timinginfo)) {
		ULONGLONG t = 0;
		UINT i;
		for (i = 0; i < 10; i++) {
			t += timinginfo.qpcRefreshPeriod;
			Sleep(50);
			DwmGetCompositionTimingInfo(nullptr, &timinginfo);
		}

		double DetectedRefreshRate = GetPreciseTicksPerSecond() * i / t;
		DLog(L"RefreshRate         : %7.03f Hz", (double)timinginfo.rateRefresh.uiNumerator / timinginfo.rateRefresh.uiDenominator);
		DLog(L"DetectedRefreshRate : %7.03f Hz", DetectedRefreshRate);
	}
#endif

	return hr;
}

void CDX11VideoProcessor::ReleaseVP()
{
	m_FrameStats.Reset();
	m_DrawnFrameStats.Reset();
	m_RenderStats.Reset();

	m_pSrcTexture2D_CPU.Release();
	m_pSrcTexture2D.Release();

	if (m_pSamplerLinear) {
		m_pSamplerLinear->Release();
		m_pSamplerLinear = nullptr;
	}

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

	m_pInputLayout.Release();
	m_pVertexShader.Release();
	m_pPixelShader.Release();

#if VER_PRODUCTBUILD >= 10000
	m_pVideoContext1.Release();
#endif
	m_pVideoContext.Release();

	m_pImmediateContext.Release();
	m_pDevice.Release();
}

void CDX11VideoProcessor::ReleaseD2D1RenderTarget()
{
	m_pD2D1Brush.Release();
	m_pD2D1BrushBlack.Release();
	m_pD2D1RenderTarget.Release();
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
	ReleaseD2D1RenderTarget();
	m_pDXGISwapChain1.Release();
	m_pDXGIFactory2.Release();
	ReleaseDevice();

	CheckPointer(pDevice, E_POINTER);
	CheckPointer(pContext, E_POINTER);

	m_pDevice = pDevice;
	m_pImmediateContext = pContext;

	HRESULT hr = m_pDevice->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&m_pVideoDevice);
	if (FAILED(hr)) {
		ReleaseDevice();
		return hr;
	}

	hr = m_pImmediateContext->QueryInterface(__uuidof(ID3D11VideoContext), (void**)&m_pVideoContext);
	if (FAILED(hr)) {
		ReleaseDevice();
		return hr;
	}

#if VER_PRODUCTBUILD >= 10000
	m_pVideoContext->QueryInterface(__uuidof(ID3D11VideoContext1), (void**)&m_pVideoContext1);
#endif

	CComPtr<IDXGIDevice> pDXGIDevice;
	hr = m_pDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice);
	if (FAILED(hr)) {
		ReleaseDevice();
		return hr;
	}

	CComPtr<IDXGIAdapter> pDXGIAdapter;
	hr = pDXGIDevice->GetAdapter(&pDXGIAdapter);
	if (FAILED(hr)) {
		ReleaseDevice();
		return hr;
	}

	CComPtr<IDXGIFactory1> pDXGIFactory1;
	hr = pDXGIAdapter->GetParent(__uuidof(IDXGIFactory1), (void**)&pDXGIFactory1);
	if (FAILED(hr)) {
		ReleaseDevice();
	}

	hr = pDXGIFactory1->QueryInterface(__uuidof(IDXGIFactory2), (void**)&m_pDXGIFactory2);
	if (FAILED(hr)) {
		ReleaseDevice();
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
		DLog(L"Graphics adapter: %s", m_strAdapterDescription);
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

	LPVOID data;
	DWORD size;
	HRESULT hr2;
	if (S_OK == GetDataFromResource(data, size, IDF_VSHADER11_TEST)) {
		hr2 = m_pDevice->CreateVertexShader(data, size, nullptr, &m_pVertexShader);
	}

	D3D11_INPUT_ELEMENT_DESC Layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};
	hr2 = m_pDevice->CreateInputLayout(Layout, std::size(Layout), data, size, &m_pInputLayout);
	if (S_OK == hr2) {
		m_pImmediateContext->IASetInputLayout(m_pInputLayout);
	}
	if (S_OK == GetDataFromResource(data, size, IDF_PSHADER11_TEST)) {
		hr2 = m_pDevice->CreatePixelShader(data, size, nullptr, &m_pPixelShader);
	}

	return hr;
}

HRESULT CDX11VideoProcessor::InitSwapChain(const HWND hwnd, UINT width/* = 0*/, UINT height/* = 0*/)
{
	CheckPointer(hwnd, E_FAIL);
	CheckPointer(m_pVideoDevice, E_FAIL);

	HRESULT hr = S_OK;

	if (!width || !height) {
		RECT rc;
		GetClientRect(hwnd, &rc);
		width = rc.right - rc.left;
		height = rc.bottom - rc.top;
	}

	if (m_hWnd && m_pDXGISwapChain1) {
		const HMONITOR hCurMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
		const HMONITOR hNewMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		if (hCurMonitor == hNewMonitor) {
			if (m_VPOutputFmt == DXGI_FORMAT_B8G8R8A8_UNORM || m_VPOutputFmt == DXGI_FORMAT_R8G8B8A8_UNORM) {
				ReleaseD2D1RenderTarget();
			}
			hr = m_pDXGISwapChain1->ResizeBuffers(1, width, height, m_VPOutputFmt, 0);
			if (SUCCEEDED(hr)) {
				if ((m_VPOutputFmt == DXGI_FORMAT_B8G8R8A8_UNORM || m_VPOutputFmt == DXGI_FORMAT_R8G8B8A8_UNORM) && m_pTextFormat) {
					CComPtr<IDXGISurface> pDXGISurface;
					HRESULT hr2 = m_pDXGISwapChain1->GetBuffer(0, IID_PPV_ARGS(&pDXGISurface));
					if (S_OK == hr2) {
						FLOAT dpiX;
						FLOAT dpiY;
						m_pD2D1Factory->GetDesktopDpi(&dpiX, &dpiY);

						D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
							D2D1_RENDER_TARGET_TYPE_DEFAULT,
							D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
							dpiX,
							dpiY);

						hr2 = m_pD2D1Factory->CreateDxgiSurfaceRenderTarget(pDXGISurface, &props, &m_pD2D1RenderTarget);
						if (S_OK == hr2) {
							hr2 = m_pD2D1RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::LightYellow), &m_pD2D1Brush);
							hr2 = m_pD2D1RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &m_pD2D1BrushBlack);
						}
					}
				}

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
		return hr;
	}

	m_hWnd = hwnd;

	ReleaseD2D1RenderTarget();

	if ((m_VPOutputFmt == DXGI_FORMAT_B8G8R8A8_UNORM || m_VPOutputFmt == DXGI_FORMAT_R8G8B8A8_UNORM) && m_pTextFormat) {
		// https://msdn.microsoft.com/en-us/library/windows/desktop/dd756766(v=vs.85).aspx#supported_formats_for__id2d1hwndrendertarget
		CComPtr<IDXGISurface> pDXGISurface;
		HRESULT hr2 = m_pDXGISwapChain1->GetBuffer(0, IID_PPV_ARGS(&pDXGISurface));
		if (S_OK == hr2) {
			FLOAT dpiX;
			FLOAT dpiY;
			m_pD2D1Factory->GetDesktopDpi(&dpiX, &dpiY);

			D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
				D2D1_RENDER_TARGET_TYPE_DEFAULT,
				D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
				dpiX,
				dpiY);

			hr2 = m_pD2D1Factory->CreateDxgiSurfaceRenderTarget(pDXGISurface, &props, &m_pD2D1RenderTarget);
			if (S_OK == hr2) {
				hr2 = m_pD2D1RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::LightYellow), &m_pD2D1Brush);
				hr2 = m_pD2D1RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &m_pD2D1BrushBlack);
			}
		}
	}

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
	DLog(L"CDX11VideoProcessor::InitMediaType started");

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
		if (!FmtConvParams->bRGB && (vih2->dwControlFlags & (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT))) {
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

	m_srcWidth       = pBIH->biWidth;
	m_srcHeight      = labs(pBIH->biHeight);
	UINT biSizeImage = pBIH->biSizeImage;
	if (pBIH->biSizeImage == 0 && pBIH->biCompression == BI_RGB) { // biSizeImage may be zero for BI_RGB bitmaps
		biSizeImage = m_srcWidth * m_srcHeight * pBIH->biBitCount / 8;
	}

	if (m_srcRect.IsRectNull() && m_trgRect.IsRectNull()) {
		// Hmm
		m_srcRect.SetRect(0, 0, m_srcWidth, m_srcHeight);
		m_trgRect.SetRect(0, 0, m_srcWidth, m_srcHeight);
	}

	m_pConvertFn    = FmtConvParams->Func;
	m_srcPitch      = biSizeImage * 2 / (m_srcHeight * FmtConvParams->PitchCoeff);
	if (SubType == MEDIASUBTYPE_NV12 && biSizeImage % 4) {
		m_srcPitch = ALIGN(m_srcPitch, 4);
	}
	else if (SubType == MEDIASUBTYPE_P010 || SubType == MEDIASUBTYPE_P016) {
		m_srcPitch &= ~1u;
	}
	if (pBIH->biCompression == BI_RGB && pBIH->biHeight > 0) {
		m_srcPitch = -m_srcPitch;
	}

	// D3D11 Video Processor
	if (FmtConvParams->VP11Format != DXGI_FORMAT_UNKNOWN && S_OK == InitializeD3D11VP(FmtConvParams->VP11Format, m_srcWidth, m_srcHeight, false)) {
		m_srcSubType = SubType;
		UpdateStatsStatic();
		m_inputMT = *pmt;
		return TRUE;
	}

	// Tex Video Processor
	if (FmtConvParams->DX11Format != DXGI_FORMAT_UNKNOWN && S_OK == InitializeTexVP(FmtConvParams->DX11Format, m_srcWidth, m_srcHeight)) {
		mp_csp_params csp_params;
		set_colorspace(m_srcExFmt, csp_params.color);
		//csp_params.brightness = DXVA2FixedToFloat(m_BltParams.ProcAmpValues.Brightness) / 100;
		//csp_params.contrast = DXVA2FixedToFloat(m_BltParams.ProcAmpValues.Contrast);
		//csp_params.hue = DXVA2FixedToFloat(m_BltParams.ProcAmpValues.Hue) / 180 * acos(-1);
		//csp_params.saturation = DXVA2FixedToFloat(m_BltParams.ProcAmpValues.Saturation);
		//csp_params.gray = SubType == MEDIASUBTYPE_Y8 || SubType == MEDIASUBTYPE_Y800 || SubType == MEDIASUBTYPE_Y116;

		bool bPprocRGB = FmtConvParams->bRGB && (fabs(csp_params.brightness) > 1e-4f || fabs(csp_params.contrast - 1.0f) > 1e-4f);

		if (SubType == MEDIASUBTYPE_AYUV || SubType == MEDIASUBTYPE_Y410 || bPprocRGB) {
			//m_iConvertShader = shader_convert_color;
			//SetShaderConvertColorParams(csp_params);
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
	DLog(L"CDX11VideoProcessor::InitializeD3D11VP started with input surface: %s, %u x %u", DXGIFormatToString(dxgiFormat), width, height);

	CheckPointer(m_pVideoDevice, E_FAIL);

	if (only_update_texture) {
		if (dxgiFormat != m_srcDXGIFormat) {
			DLog(L"CDX11VideoProcessor::InitializeD3D11VP : incorrect texture format!");
			ASSERT(0);
			return E_FAIL;
		}
		if (width < m_srcWidth || height < m_srcHeight) {
			DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : texture size less than frame size!");
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
		return hr;
	}

	for (UINT i = 0; i < std::size(m_VPFilterRange); i++) {
		if (m_VPCaps.FilterCaps & (1 << i)) {
			hr = m_pVideoProcessorEnum->GetVideoProcessorFilterRange((D3D11_VIDEO_PROCESSOR_FILTER)i, &m_VPFilterRange[i]);
			if (FAILED(hr)) {
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
			return E_FAIL;
		}
	}

	hr = m_pVideoDevice->CreateVideoProcessor(m_pVideoProcessorEnum, index, &m_pVideoProcessor);
	if (FAILED(hr)) {
		return hr;
	}

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = desc.ArraySize = 1;
	desc.Format = dxgiFormat;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = 0;
	hr = m_pDevice->CreateTexture2D(&desc, nullptr, &m_pSrcTexture2D_CPU);
	if (FAILED(hr)) {
		return hr;
	}

	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = 0;
	hr = m_pDevice->CreateTexture2D(&desc, nullptr, &m_pSrcTexture2D);
	if (FAILED(hr)) {
		return hr;
	}

	D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = {};
	inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
	hr = m_pVideoDevice->CreateVideoProcessorInputView(m_pSrcTexture2D, m_pVideoProcessorEnum, &inputViewDesc, &m_pInputView);
	if (FAILED(hr)) {
		return hr;
	}

	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = desc.ArraySize = 1;
		desc.Format = dxgiFormat;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D10_BIND_RENDER_TARGET;
		CComPtr<ID3D11Texture2D> pTestTexture2D;
		hr = m_pDevice->CreateTexture2D(&desc, nullptr, &pTestTexture2D);
		if (FAILED(hr)) {
			return hr;
		}

		D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC OutputViewDesc = {};
		OutputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
		CComPtr<ID3D11VideoProcessorOutputView> pTestOutputView;
		HRESULT hr = m_pVideoDevice->CreateVideoProcessorOutputView(pTestTexture2D, m_pVideoProcessorEnum, &OutputViewDesc, &pTestOutputView);
		if (FAILED(hr)) {
			return hr;
		}
	}

	m_TextureWidth  = width;
	m_TextureHeight = height;
	if (!only_update_texture) {
		m_srcDXGIFormat = dxgiFormat;
		m_srcWidth      = width;
		m_srcHeight     = height;
	}

	DLog(L"CDX11VideoProcessor::InitializeD3D11VP completed successfully");

	return S_OK;
}

HRESULT CDX11VideoProcessor::InitializeTexVP(const DXGI_FORMAT dxgiFormat, const UINT width, const UINT height)
{
	DLog(L"CDX11VideoProcessor::InitializeTexVP started with input surface: %s, %u x %u", DXGIFormatToString(dxgiFormat), width, height);

	HRESULT hr = S_OK;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = desc.ArraySize = 1;
	desc.Format = dxgiFormat;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = 0;
	hr = m_pDevice->CreateTexture2D(&desc, nullptr, &m_pSrcTexture2D_CPU);
	if (FAILED(hr)) {
		return hr;
	}

	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = 0;
	hr = m_pDevice->CreateTexture2D(&desc, nullptr, &m_pSrcTexture2D);
	if (FAILED(hr)) {
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
		return hr;
	}

	m_srcDXGIFormat = dxgiFormat;
	m_srcWidth      = width;
	m_srcHeight     = height;

	return S_OK;
}

void CDX11VideoProcessor::Start()
{
	m_DrawnFrameStats.Reset();
	m_RenderStats.NewInterval();
}

HRESULT CDX11VideoProcessor::ProcessSample(IMediaSample* pSample)
{
	REFERENCE_TIME rtStart, rtEnd;
	pSample->GetTime(&rtStart, &rtEnd);
	m_FrameStats.Add(rtStart);

	const REFERENCE_TIME rtFrameDur = m_FrameStats.GetAverageFrameDuration();
	rtEnd = rtStart + rtFrameDur;
	CRefTime rtClock;

	m_pFilter->StreamTime(rtClock);
	if (m_RenderStats.skipped_interval < 60 && rtEnd < rtClock) {
		m_RenderStats.skipped1++;
		m_RenderStats.skipped_interval++;
		return S_FALSE; // skip frame
	}
	m_RenderStats.skipped_interval = 0;

	HRESULT hr = CopySample(pSample);
	if (FAILED(hr)) {
		m_RenderStats.failed++;
		return hr;
	}

	// always Render(1) a frame after CopySample()
	hr = Render(1);
	m_pFilter->StreamTime(rtClock);
	m_DrawnFrameStats.Add(rtClock);

	m_RenderStats.syncoffset = rtClock - rtStart;

	if (SecondFramePossible()) {
		m_pFilter->StreamTime(rtClock);
		if (rtEnd < rtClock) {
			m_RenderStats.skipped2++;
			return S_FALSE; // skip frame
		}

		hr = Render(2);
		m_pFilter->StreamTime(rtClock);
		m_DrawnFrameStats.Add(rtClock);

		rtStart += rtFrameDur / 2;
		m_RenderStats.syncoffset = rtClock - rtStart;
	}

	return hr;
}

#define BREAK_ON_ERROR(hr) { if (FAILED(hr)) { break; }}

HRESULT CDX11VideoProcessor::CopySample(IMediaSample* pSample)
{
	CheckPointer(m_pSrcTexture2D_CPU, E_FAIL);
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
		CComQIPtr<ID3D11Texture2D> pD3D11Texture2D;
		UINT ArraySlice = 0;
		hr = pMSD3D11->GetD3D11Texture(0, &pD3D11Texture2D, &ArraySlice);
		if (FAILED(hr)) {
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

		m_pImmediateContext->CopySubresourceRegion(m_pSrcTexture2D, 0, 0, 0, 0, pD3D11Texture2D, ArraySlice, nullptr);
	}
	//else if (CComQIPtr<IMFGetService> pService = pSample) {
	//	CComPtr<IDirect3DSurface9> pSurface9;
	//	if (SUCCEEDED(pService->GetService(MR_BUFFER_SERVICE, IID_PPV_ARGS(&pSurface9)))) {
	//		D3DSURFACE_DESC desc = {};
	//		hr = pSurface9->GetDesc(&desc);
	//		if (FAILED(hr)) {
	//			return hr;
	//		}
	//		hr = Initialize(desc.Width, desc.Height, m_srcDXGIFormat);
	//		if (FAILED(hr)) {
	//			return hr;
	//		}
	//
	//		if (m_bCanUseSharedHandle) {
	//			for (;;) {
	//				CComPtr<IDirect3DDevice9> pD3DDev9;
	//				pSurface9->GetDevice(&pD3DDev9);
	//				BREAK_ON_ERROR(hr);
	//
	//				if (!m_pSrcTexture9) {
	//					hr = pD3DDev9->CreateTexture(
	//						desc.Width,
	//						desc.Height,
	//						1,
	//						D3DUSAGE_RENDERTARGET,
	//						D3DFMT_A8R8G8B8,
	//						D3DPOOL_DEFAULT,
	//						&m_pSrcTexture9,
	//						&m_sharedHandle);
	//					BREAK_ON_ERROR(hr);
	//					hr = m_pSrcTexture9->GetSurfaceLevel(0, &m_pSrcSurface9);
	//					BREAK_ON_ERROR(hr);
	//				}
	//
	//				if (!m_pSrcTexture2D_RGB) {
	//					hr = m_pDevice->OpenSharedResource(m_sharedHandle, __uuidof(ID3D11Texture2D), (void**)(&m_pSrcTexture2D_RGB));
	//					BREAK_ON_ERROR(hr);
	//				}
	//
	//				hr = pD3DDev9->StretchRect(pSurface9, nullptr, m_pSrcSurface9, nullptr, D3DTEXF_NONE);
	//				BREAK_ON_ERROR(hr);
	//
	//				break;
	//			}
	//		}
	//
	//		if (!m_bCanUseSharedHandle) {
	//			D3DLOCKED_RECT lr_src;
	//			hr = pSurface9->LockRect(&lr_src, nullptr, D3DLOCK_READONLY);
	//			if (S_OK == hr) {
	//				D3D11_MAPPED_SUBRESOURCE mappedResource = {};
	//				hr = m_pImmediateContext->Map(m_pSrcTexture2D_CPU, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	//				if (SUCCEEDED(hr)) {
	//					ASSERT(m_pConvertFn);
	//					m_pConvertFn(desc.Height, (BYTE*)mappedResource.pData, mappedResource.RowPitch, (BYTE*)lr_src.pBits, lr_src.Pitch);
	//					m_pImmediateContext->Unmap(m_pSrcTexture2D_CPU, 0);
	//					m_pImmediateContext->CopyResource(m_pSrcTexture2D, m_pSrcTexture2D_CPU); // we can't use texture with D3D11_CPU_ACCESS_WRITE flag
	//				}
	//
	//				hr = pSurface9->UnlockRect();
	//			}
	//		}
	//	}
	//}
	else {
		BYTE* data = nullptr;
		const long size = pSample->GetActualDataLength();
		if (size > 0 && S_OK == pSample->GetPointer(&data)) {
			D3D11_MAPPED_SUBRESOURCE mappedResource = {};
			hr = m_pImmediateContext->Map(m_pSrcTexture2D_CPU, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
			if (SUCCEEDED(hr)) {
				ASSERT(m_pConvertFn);
				BYTE* src = (m_srcPitch < 0) ? data + m_srcPitch * (1 - (int)m_srcHeight) : data;
				m_pConvertFn(m_srcHeight, (BYTE*)mappedResource.pData, mappedResource.RowPitch, src, m_srcPitch);
				m_pImmediateContext->Unmap(m_pSrcTexture2D_CPU, 0);
				m_pImmediateContext->CopyResource(m_pSrcTexture2D, m_pSrcTexture2D_CPU); // we can't use texture with D3D11_CPU_ACCESS_WRITE flag
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
		return hr;
	}

	if (1) {
		hr = ProcessD3D11(pBackBuffer, m_FieldDrawn == 2);
	} else {
		hr = ProcessTex(pBackBuffer);
	}
	if (S_OK == hr && m_bShowStats) {
		hr = DrawStats();
	}
	m_RenderStats.renderticks = GetPreciseTick() - ticks;

	hr = m_pDXGISwapChain1->Present(0, 0);

	return hr;
}

HRESULT CDX11VideoProcessor::FillBlack()
{
	ID3D11Texture2D* pBackBuffer;
	HRESULT hr = m_pDXGISwapChain1->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
	if (FAILED(hr)) {
		return hr;
	}

	ID3D11RenderTargetView* pRenderTargetView;
	hr = m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView);
	pBackBuffer->Release();
	if (FAILED(hr)) {
		return hr;
	}
	
	m_pImmediateContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);

	float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	m_pImmediateContext->ClearRenderTargetView(pRenderTargetView, ClearColor);

	hr = m_pDXGISwapChain1->Present(0, 0);

	pRenderTargetView->Release();

	return hr;
}

HRESULT CDX11VideoProcessor::ProcessD3D11(ID3D11Texture2D* pRenderTarget, const bool second)
{
	if (m_videoRect.IsRectEmpty() || m_windowRect.IsRectEmpty()) {
		return S_OK;
	}

	if (!second) {
		CRect rSrcRect(m_srcRect);
		CRect rDstRect(m_videoRect);
		D3D11_TEXTURE2D_DESC desc = {};
		pRenderTarget->GetDesc(&desc);
		if (desc.Width && desc.Height) {
			ClipToSurface(desc.Width, desc.Height, rSrcRect, rDstRect);
		}

		// input format
		m_pVideoContext->VideoProcessorSetStreamFrameFormat(m_pVideoProcessor, 0, m_SampleFormat);

		// Output rate (repeat frames)
		m_pVideoContext->VideoProcessorSetStreamOutputRate(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, TRUE, nullptr);
	
		// disable automatic video quality by driver
		m_pVideoContext->VideoProcessorSetStreamAutoProcessingMode(m_pVideoProcessor, 0, FALSE);

		// Source rect
		m_pVideoContext->VideoProcessorSetStreamSourceRect(m_pVideoProcessor, 0, TRUE, rSrcRect);

		// Dest rect
		m_pVideoContext->VideoProcessorSetStreamDestRect(m_pVideoProcessor, 0, TRUE, rDstRect);
		m_pVideoContext->VideoProcessorSetOutputTargetRect(m_pVideoProcessor, TRUE, m_windowRect);

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
		return hr;
	}

	D3D11_VIDEO_PROCESSOR_STREAM StreamData = {};
	StreamData.Enable = TRUE;
	StreamData.InputFrameOrField = second ? 1 : 0;
	StreamData.pInputSurface = m_pInputView;
	hr = m_pVideoContext->VideoProcessorBlt(m_pVideoProcessor, pOutputView, 0, 1, &StreamData);

	return hr;
}

//
// A vertex with a position and texture coordinate
//
typedef struct _VERTEX
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT2 TexCoord;
} VERTEX;

#define NUMVERTICES 6

HRESULT CDX11VideoProcessor::ProcessTex(ID3D11Texture2D* pRenderTarget)
{
	ID3D11RenderTargetView* pRenderTargetView;
	HRESULT hr = m_pDevice->CreateRenderTargetView(pRenderTarget, nullptr, &pRenderTargetView);
	if (FAILED(hr)) {
		return hr;
	}

	D3D11_TEXTURE2D_DESC FrameDesc;
	m_pSrcTexture2D_CPU->GetDesc(&FrameDesc);

	D3D11_VIEWPORT VP;
	VP.Width = static_cast<FLOAT>(FrameDesc.Width);
	VP.Height = static_cast<FLOAT>(FrameDesc.Height);
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;
	VP.TopLeftX = 0;
	VP.TopLeftY = 0;
	m_pImmediateContext->RSSetViewports(1, &VP);

	// Vertices for drawing whole texture
	VERTEX Vertices[NUMVERTICES] =
	{
		{DirectX::XMFLOAT3(-1.0f, -1.0f, 0), DirectX::XMFLOAT2(0.0f, 1.0f)},
		{DirectX::XMFLOAT3(-1.0f, 1.0f, 0),  DirectX::XMFLOAT2(0.0f, 0.0f)},
		{DirectX::XMFLOAT3(1.0f, -1.0f, 0),  DirectX::XMFLOAT2(1.0f, 1.0f)},
		{DirectX::XMFLOAT3(1.0f, -1.0f, 0),  DirectX::XMFLOAT2(1.0f, 1.0f)},
		{DirectX::XMFLOAT3(-1.0f, 1.0f, 0),  DirectX::XMFLOAT2(0.0f, 0.0f)},
		{DirectX::XMFLOAT3(1.0f, 1.0f, 0),   DirectX::XMFLOAT2(1.0f, 0.0f)},
	};

	D3D11_SHADER_RESOURCE_VIEW_DESC ShaderDesc;
	ShaderDesc.Format = FrameDesc.Format;
	ShaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	ShaderDesc.Texture2D.MostDetailedMip = FrameDesc.MipLevels - 1;
	ShaderDesc.Texture2D.MipLevels = FrameDesc.MipLevels;

	// Create new shader resource view
	ID3D11ShaderResourceView* ShaderResource = nullptr;
	hr = m_pDevice->CreateShaderResourceView(m_pSrcTexture2D_CPU, &ShaderDesc, &ShaderResource);
	if (FAILED(hr)) {
		return hr;
	}

	// Set resources
	UINT Stride = sizeof(VERTEX);
	UINT Offset = 0;
	FLOAT blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	m_pImmediateContext->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
	m_pImmediateContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
	m_pImmediateContext->VSSetShader(m_pVertexShader, nullptr, 0);
	m_pImmediateContext->PSSetShader(m_pPixelShader, nullptr, 0);
	m_pImmediateContext->PSSetShaderResources(0, 1, &ShaderResource);
	m_pImmediateContext->PSSetSamplers(0, 1, &m_pSamplerLinear);
	m_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D11_BUFFER_DESC BufferDesc;
	ZeroMemory(&BufferDesc, sizeof(BufferDesc));
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(VERTEX) * NUMVERTICES;
	BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = Vertices;

	ID3D11Buffer* VertexBuffer = nullptr;
	// Create vertex buffer
	hr = m_pDevice->CreateBuffer(&BufferDesc, &InitData, &VertexBuffer);
	if (FAILED(hr)) {
		ShaderResource->Release();
		ShaderResource = nullptr;
		return hr;
	}
	m_pImmediateContext->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);

	// Draw textured quad onto render target
	m_pImmediateContext->Draw(NUMVERTICES, 0);

	VertexBuffer->Release();
	VertexBuffer = nullptr;

	// Release shader resource
	ShaderResource->Release();
	ShaderResource = nullptr;

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

HRESULT CDX11VideoProcessor::GetFrameInfo(VRFrameInfo* pFrameInfo)
{
	CheckPointer(pFrameInfo, E_POINTER);

	pFrameInfo->Subtype = m_srcSubType;
	pFrameInfo->Width = m_srcWidth;
	pFrameInfo->Height = m_srcHeight;
	pFrameInfo->ExtFormat.value = m_srcExFmt.value;

	return S_OK;
}

HRESULT CDX11VideoProcessor::GetAdapterDecription(CStringW& str)
{
	str = m_strAdapterDescription;
	return S_OK;
}

void CDX11VideoProcessor::UpdateStatsStatic()
{
	auto FmtConvParams = GetFmtConvParams(m_srcSubType);
	if (FmtConvParams) {
		m_strStatsStatic.Format(L"\nInput format  : %S %ux%u", FmtConvParams->str, m_srcWidth, m_srcHeight);
		m_strStatsStatic.AppendFormat(L"\nVP output fmt : %s", DXGIFormatToString(m_VPOutputFmt));
		m_strStatsStatic.AppendFormat(L"\nVideoProcessor: %s", m_pVideoProcessor ? L"D3D11" : L"Shaders");
	} else {
		m_strStatsStatic.Empty();
	}
}

HRESULT CDX11VideoProcessor::DrawStats()
{
	if (!m_pD2D1Brush || m_windowRect.IsRectEmpty()) {
		return E_ABORT;
	}

	CStringW str = L"Direct3D 11";
	str.AppendFormat(L"\nFrame  rate   : %7.03f", m_FrameStats.GetAverageFps());
	if (m_SampleFormat != D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE) {
		str.AppendChar(L'i');
	}
	str.AppendFormat(L",%7.03f", m_DrawnFrameStats.GetAverageFps());
	str.Append(m_strStatsStatic);
	str.AppendFormat(L"\nFrames: %5u, skiped: %u/%u, failed: %u",
		m_FrameStats.GetFrames(), m_RenderStats.skipped1, m_RenderStats.skipped2, m_RenderStats.failed);
	str.AppendFormat(L"\nCopyTime:%3llu ms, RenderTime:%3llu ms",
		m_RenderStats.copyticks * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.renderticks * 1000 / GetPreciseTicksPerSecondI());
	str.AppendFormat(L"\nSync offset   : %+3lld ms", (m_RenderStats.syncoffset + 5000) / 10000);

	CComPtr<IDWriteTextLayout> pTextLayout;
	if (S_OK == m_pDWriteFactory->CreateTextLayout(str, str.GetLength(), m_pTextFormat, m_windowRect.right - 10, m_windowRect.bottom - 10, &pTextLayout)) {
		m_pD2D1RenderTarget->BeginDraw();
		m_pD2D1RenderTarget->DrawTextLayout(D2D1::Point2F( 9.0f, 11.0f), pTextLayout, m_pD2D1BrushBlack);
		m_pD2D1RenderTarget->DrawTextLayout(D2D1::Point2F(11.0f, 11.0f), pTextLayout, m_pD2D1BrushBlack);
		m_pD2D1RenderTarget->DrawTextLayout(D2D1::Point2F(10.0f, 10.0f), pTextLayout, m_pD2D1Brush);
		m_pD2D1RenderTarget->EndDraw();
	}

	return S_OK;
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
