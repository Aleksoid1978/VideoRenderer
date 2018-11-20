/*
* (C) 2018 see Authors.txt
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
#include <dvdmedia.h>
#include <d3d9.h>
#include <mfapi.h> // for MR_BUFFER_SERVICE
#include <Mferror.h>
#include <Mfidl.h>
#include <directxcolors.h>
#include <dwmapi.h>
#include "Helper.h"
#include "Time.h"
#include "DX11VideoProcessor.h"
#include "./Include/ID3DVideoMemoryConfiguration.h"


// CDX11VideoProcessor

CDX11VideoProcessor::CDX11VideoProcessor(CBaseRenderer* pFilter)
{
	m_pFilter = pFilter;
}

CDX11VideoProcessor::~CDX11VideoProcessor()
{
	ClearD3D11();

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
		ARRAYSIZE(featureLevels),
		D3D11_SDK_VERSION,
		&pDevice,
		&featurelevel,
		&pImmediateContext);
	if (FAILED(hr)) {
		return hr;
	}

	hr = SetDevice(pDevice, pImmediateContext);

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

void CDX11VideoProcessor::ClearD3D11()
{
	m_pTextFormat.Release();
	m_pDWriteFactory.Release();
	m_pD2DFactory.Release();

	m_pD2DBrush.Release();
	m_pD2DBrushBlack.Release();
	m_pD2D1RenderTarget.Release();

	m_pSrcTexture2D_RGB.Release();
	m_pSrcSurface9.Release();

	m_pSrcTexture2D.Release();
	m_pSrcTexture2D_Decode.Release();
	m_pVideoProcessor.Release();
	m_pVideoProcessorEnum.Release();
	m_pVideoDevice.Release();
#if VER_PRODUCTBUILD >= 10000
	m_pVideoContext1.Release();
#endif
	m_pVideoContext.Release();
	m_pImmediateContext.Release();
	m_pDXGISwapChain1.Release();
	m_pDXGIFactory2.Release();
	m_pDevice.Release();
}

HRESULT CDX11VideoProcessor::SetDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext)
{
	ClearD3D11();

	CheckPointer(pDevice, E_POINTER);
	CheckPointer(pContext, E_POINTER);

	m_pDevice = pDevice;
	m_pImmediateContext = pContext;

	HRESULT hr = m_pDevice->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&m_pVideoDevice);
	if (FAILED(hr)) {
		m_pDevice.Release();
		return hr;
	}

	hr = m_pImmediateContext->QueryInterface(__uuidof(ID3D11VideoContext), (void**)&m_pVideoContext);
	if (FAILED(hr)) {
		m_pDevice.Release();
		m_pImmediateContext.Release();
		m_pVideoDevice.Release();
		return hr;
	}

#if VER_PRODUCTBUILD >= 10000
	m_pVideoContext->QueryInterface(__uuidof(ID3D11VideoContext1), (void**)&m_pVideoContext1);
#endif

	CComPtr<IDXGIDevice> pDXGIDevice;
	hr = m_pDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice);
	if (FAILED(hr)) {
		m_pDevice.Release();
		m_pImmediateContext.Release();
		m_pVideoDevice.Release();
		return hr;
	}

	CComPtr<IDXGIAdapter> pDXGIAdapter;
	hr = pDXGIDevice->GetAdapter(&pDXGIAdapter);
	if (FAILED(hr)) {
		m_pImmediateContext.Release();
		m_pVideoDevice.Release();
		return hr;
	}

	CComPtr<IDXGIFactory1> pDXGIFactory1;
	hr = pDXGIAdapter->GetParent(__uuidof(IDXGIFactory1), (void**)&pDXGIFactory1);
	if (FAILED(hr)) {
		m_pDevice.Release();
		m_pImmediateContext.Release();
		m_pVideoDevice.Release();
	}

	hr = pDXGIFactory1->QueryInterface(__uuidof(IDXGIFactory2), (void**)&m_pDXGIFactory2);
	if (FAILED(hr)) {
		m_pDevice.Release();
		m_pImmediateContext.Release();
		m_pVideoDevice.Release();
	}

	if (m_mt.IsValid()) {
		m_D3D11_Src_Format = DXGI_FORMAT_UNKNOWN;
		m_D3D11_Src_Width = 0;
		m_D3D11_Src_Height = 0;

		if (!InitMediaType(&m_mt)) {
			m_pDevice.Release();
			m_pImmediateContext.Release();
			m_pVideoDevice.Release();
			return E_FAIL;
		}
	}

	if (m_hWnd) {
		hr = InitSwapChain(m_hWnd);
		if (FAILED(hr)) {
			m_pDevice.Release();
			m_pImmediateContext.Release();
			m_pVideoDevice.Release();
			return hr;
		}
	}

	DXGI_ADAPTER_DESC dxgiAdapterDesc = {};
	hr = pDXGIAdapter->GetDesc(&dxgiAdapterDesc);
	if (SUCCEEDED(hr)) {
		m_VendorId = dxgiAdapterDesc.VendorId;
		m_strAdapterDescription.Format(L"%s (%04X:%04X)", dxgiAdapterDesc.Description, dxgiAdapterDesc.VendorId, dxgiAdapterDesc.DeviceId);
	}

	m_bCanUseSharedHandle = (m_VendorId != PCIV_NVIDIA);

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
		return hr;
	}

	m_hWnd = hwnd;

	m_pTextFormat.Release();
	m_pDWriteFactory.Release();
	m_pD2DFactory.Release();
	m_pD2DBrush.Release();
	m_pD2DBrushBlack.Release();
	m_pD2D1RenderTarget.Release();

	if (m_VPOutputFmt == DXGI_FORMAT_B8G8R8A8_UNORM || m_VPOutputFmt == DXGI_FORMAT_R8G8B8A8_UNORM) {
		// https://msdn.microsoft.com/en-us/library/windows/desktop/dd756766(v=vs.85).aspx#supported_formats_for__id2d1hwndrendertarget
		D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
		options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

		HRESULT hr2 = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, &m_pD2DFactory);
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
				if (S_OK == hr2) {
					CComPtr<IDXGISurface> pDXGISurface;
					hr2 = m_pDXGISwapChain1->GetBuffer(0, IID_PPV_ARGS(&pDXGISurface));
					if (S_OK == hr2) {
						FLOAT dpiX;
						FLOAT dpiY;
						m_pD2DFactory->GetDesktopDpi(&dpiX, &dpiY);

						D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
							D2D1_RENDER_TARGET_TYPE_DEFAULT,
							D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
							dpiX,
							dpiY);

						hr2 = m_pD2DFactory->CreateDxgiSurfaceRenderTarget(pDXGISurface, &props, &m_pD2D1RenderTarget);
						if (S_OK == hr2) {
							hr2 = m_pD2D1RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::LightYellow), &m_pD2DBrush);
							hr2 = m_pD2D1RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &m_pD2DBrushBlack);
						}
					}
				}
			}
		}
	}

	return hr;
}

BOOL CDX11VideoProcessor::InitMediaType(const CMediaType* pmt)
{
	m_mt = *pmt;

	if (m_mt.formattype == FORMAT_VideoInfo2) {
		const VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)m_mt.pbFormat;
		m_srcRect = vih2->rcSource;
		m_trgRect = vih2->rcTarget;
		m_srcWidth = vih2->bmiHeader.biWidth;
		m_srcHeight = labs(vih2->bmiHeader.biHeight);
		m_srcAspectRatioX = vih2->dwPictAspectRatioX;
		m_srcAspectRatioY = vih2->dwPictAspectRatioY;
		if (vih2->dwControlFlags & (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT)) {
			m_srcExFmt.value = vih2->dwControlFlags;
			m_srcExFmt.SampleFormat = AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT; // ignore other flags
		}
		m_bInterlaced = (vih2->dwInterlaceFlags & AMINTERLACE_IsInterlaced);
	}
	else if (m_mt.formattype == FORMAT_VideoInfo) {
		const VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)m_mt.pbFormat;
		m_srcRect = vih->rcSource;
		m_trgRect = vih->rcTarget;
		m_srcWidth = vih->bmiHeader.biWidth;
		m_srcHeight = labs(vih->bmiHeader.biHeight);
		m_srcAspectRatioX = 0;
		m_srcAspectRatioY = 0;
		m_srcExFmt.value = 0;
		m_bInterlaced = 0;
	}
	else {
		return FALSE;
	}

	if (m_srcRect.IsRectNull() && m_trgRect.IsRectNull()) {
		// Hmm
		m_srcRect.SetRect(0, 0, m_srcWidth, m_srcHeight);
		m_trgRect.SetRect(0, 0, m_srcWidth, m_srcHeight);
	}

	m_srcD3DFormat = MediaSubtype2D3DFormat(m_mt.subtype);
	m_srcDXGIFormat = MediaSubtype2DXGIFormat(m_mt.subtype);

	switch (m_srcD3DFormat) {
	case D3DFMT_NV12:
	case D3DFMT_YV12:
	default:
		m_srcPitch = m_srcWidth;
		break;
	case D3DFMT_YUY2:
	case D3DFMT_P010:
		m_srcPitch = m_srcWidth * 2;
		break;
	case D3DFMT_AYUV:
		m_srcPitch = m_srcWidth * 4;
		break;
	case D3DFMT_X8R8G8B8:
	case D3DFMT_A8R8G8B8:
		m_srcPitch = m_srcWidth * 4;
		m_srcExFmt.value = 0; // ignore color info for RGB
		break;
	}

	if (FAILED(Initialize(m_srcWidth, m_srcHeight, m_srcDXGIFormat))) {
		return FALSE;
	}

	return TRUE;
}

HRESULT CDX11VideoProcessor::Initialize(const UINT width, const UINT height, const DXGI_FORMAT dxgiFormat)
{
	CheckPointer(m_pVideoDevice, E_FAIL);

	if (dxgiFormat == m_D3D11_Src_Format && width == m_D3D11_Src_Width && height == m_D3D11_Src_Height) {
		return S_OK;
	}

	HRESULT hr = S_OK;

	m_FrameStats.Reset();
	m_pSrcTexture2D_RGB.Release();
	m_pSrcSurface9.Release();

	m_pSrcTexture2D.Release();
	m_pSrcTexture2D_Decode.Release();
	m_pVideoProcessor.Release();
	m_pVideoProcessorEnum.Release();

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

	for (int i = 0; i < ARRAYSIZE(m_VPFilterRange); i++) {
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
	hr = m_pDevice->CreateTexture2D(&desc, nullptr, &m_pSrcTexture2D);
	if (FAILED(hr)) {
		return hr;
	}

	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = 0;
	hr = m_pDevice->CreateTexture2D(&desc, nullptr, &m_pSrcTexture2D_Decode);
	if (FAILED(hr)) {
		return hr;
	}

	m_D3D11_Src_Format = dxgiFormat;
	m_D3D11_Src_Width = width;
	m_D3D11_Src_Height = height;

	return S_OK;
}

void CDX11VideoProcessor::Start()
{
	m_FrameStats.Reset();
}

HRESULT CDX11VideoProcessor::CopySample(IMediaSample* pSample)
{
	CheckPointer(m_pSrcTexture2D, E_FAIL);
	CheckPointer(m_pDXGISwapChain1, E_FAIL);

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

	m_FieldDrawn = 0;

	HRESULT hr = S_OK;

	if (CComQIPtr<IMediaSampleD3D11> pMSD3D11 = pSample) {
		m_bCanUseSharedHandle = false;

		CComQIPtr<ID3D11Texture2D> pD3D11Texture2D;
		UINT ArraySlice = 0;
		hr = pMSD3D11->GetD3D11Texture(0, &pD3D11Texture2D, &ArraySlice);
		if (FAILED(hr)) {
			return hr;
		}

		D3D11_TEXTURE2D_DESC desc = {};
		pD3D11Texture2D->GetDesc(&desc);
		hr = Initialize(desc.Width, desc.Height, desc.Format);
		if (FAILED(hr)) {
			return hr;
		}

		m_pImmediateContext->CopySubresourceRegion(m_pSrcTexture2D_Decode, 0, 0, 0, 0, pD3D11Texture2D, ArraySlice, nullptr);
	} else if (CComQIPtr<IMFGetService> pService = pSample) {
		CComPtr<IDirect3DSurface9> pSurface;
		if (SUCCEEDED(pService->GetService(MR_BUFFER_SERVICE, IID_PPV_ARGS(&pSurface)))) {
			D3DSURFACE_DESC desc = {};
			hr = pSurface->GetDesc(&desc);
			if (FAILED(hr)) {
				return hr;
			}
			hr = Initialize(desc.Width, desc.Height, m_srcDXGIFormat);
			if (FAILED(hr)) {
				return hr;
			}

			if (m_bCanUseSharedHandle) {
				for (;;) {
					CComPtr<IDirect3DDevice9> pD3DDev;
					pSurface->GetDevice(&pD3DDev);
					if (FAILED(hr)) {
						m_bCanUseSharedHandle = false;
						break;
					}

					if (!m_pSrcSurface9) {
						hr = pD3DDev->CreateOffscreenPlainSurface(
							desc.Width,
							desc.Height,
							D3DFMT_A8R8G8B8,
							D3DPOOL_DEFAULT,
							&m_pSrcSurface9,
							&m_sharedHandle);
						if (FAILED(hr)) {
							m_bCanUseSharedHandle = false;
							break;
						}
					}

					if (!m_pSrcTexture2D_RGB) {
						hr = m_pDevice->OpenSharedResource(m_sharedHandle, __uuidof(ID3D11Texture2D), (void**)(&m_pSrcTexture2D_RGB));
						if (FAILED(hr)) {
							m_bCanUseSharedHandle = false;
							break;
						}
					}

					hr = pD3DDev->StretchRect(pSurface, nullptr, m_pSrcSurface9, nullptr, D3DTEXF_NONE);
					if (FAILED(hr)) {
						m_bCanUseSharedHandle = false;
					}

					break;
				}
			}

			if (!m_bCanUseSharedHandle) {
				D3DLOCKED_RECT lr_src;
				hr = pSurface->LockRect(&lr_src, nullptr, D3DLOCK_READONLY);
				if (S_OK == hr) {
					D3D11_MAPPED_SUBRESOURCE mappedResource = {};
					hr = m_pImmediateContext->Map(m_pSrcTexture2D, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
					if (SUCCEEDED(hr)) {
						CopyFrameData(m_srcD3DFormat, desc.Width, desc.Height, (BYTE*)mappedResource.pData, mappedResource.RowPitch, (BYTE*)lr_src.pBits, lr_src.Pitch, mappedResource.DepthPitch);
						m_pImmediateContext->Unmap(m_pSrcTexture2D, 0);
						m_pImmediateContext->CopyResource(m_pSrcTexture2D_Decode, m_pSrcTexture2D); // we can't use texture with D3D11_CPU_ACCESS_WRITE flag
					}

					hr = pSurface->UnlockRect();
				}
			}
		}
	}
	else if (m_mt.formattype == FORMAT_VideoInfo2) {
		m_bCanUseSharedHandle = false;

		BYTE* data = nullptr;
		const long size = pSample->GetActualDataLength();
		if (size > 0 && S_OK == pSample->GetPointer(&data)) {
			D3D11_MAPPED_SUBRESOURCE mappedResource = {};
			hr = m_pImmediateContext->Map(m_pSrcTexture2D, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
			if (SUCCEEDED(hr)) {
				CopyFrameData(m_srcD3DFormat, m_srcWidth, m_srcHeight, (BYTE*)mappedResource.pData, mappedResource.RowPitch, data, m_srcPitch, size);
				m_pImmediateContext->Unmap(m_pSrcTexture2D, 0);
				m_pImmediateContext->CopyResource(m_pSrcTexture2D_Decode, m_pSrcTexture2D); // we can't use texture with D3D11_CPU_ACCESS_WRITE flag
			}
		}
	}

	REFERENCE_TIME rtStart, rtEnd;
	pSample->GetTime(&rtStart, &rtEnd);
	m_FrameStats.Add(rtStart);

	return hr;
}

HRESULT CDX11VideoProcessor::Render(int field)
{
	CheckPointer(m_pSrcTexture2D, E_FAIL);
	CheckPointer(m_pDXGISwapChain1, E_FAIL);

	CRefTime rtClock;
	REFERENCE_TIME rtFrame = m_FrameStats.GetTime();
	REFERENCE_TIME rtFrameDur = m_FrameStats.GetAverageFrameDuration();

	if (field) {
		m_pFilter->StreamTime(rtClock);

		if (rtFrameDur > 2 && rtFrame + rtFrameDur < rtClock) {
			return S_FALSE; // skip this frame
		}
		m_FieldDrawn = field;
	}

	CComPtr<ID3D11Texture2D> pBackBuffer;
	HRESULT hr = m_pDXGISwapChain1->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
	if (FAILED(hr)) {
		return hr;
	}

	hr = ProcessDX11(pBackBuffer, m_FieldDrawn == 2);
	if (S_OK == hr && m_bShowStats) {
		hr = DrawStats();
	}

	hr = m_pDXGISwapChain1->Present(0, 0);

	m_pFilter->StreamTime(rtClock);
	if (m_FieldDrawn == 2) {
		rtFrame += rtFrameDur / 2;
	}
	m_SyncOffsetMS = std::round((double)(rtClock - rtFrame) / (UNITS / 1000)); // TODO use IDXGISwapChain::GetFrameStatistics

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

HRESULT CDX11VideoProcessor::ProcessDX11(ID3D11Texture2D* pRenderTarget, const bool second)
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
				if (m_srcExFmt.VideoTransferFunction == 15 || m_srcExFmt.VideoTransferFunction == 16) { // SMPTE ST 2084
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

	D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = {};
	inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
	CComPtr<ID3D11VideoProcessorInputView> pInputView;
	HRESULT hr = m_pVideoDevice->CreateVideoProcessorInputView(m_bCanUseSharedHandle ? m_pSrcTexture2D_RGB : m_pSrcTexture2D_Decode, m_pVideoProcessorEnum, &inputViewDesc, &pInputView);
	if (FAILED(hr)) {
		return hr;
	}

	D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC OutputViewDesc = {};
	OutputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
	CComPtr<ID3D11VideoProcessorOutputView> pOutputView;
	hr = m_pVideoDevice->CreateVideoProcessorOutputView(pRenderTarget, m_pVideoProcessorEnum, &OutputViewDesc, &pOutputView);
	if (FAILED(hr)) {
		return hr;
	}

	D3D11_VIDEO_PROCESSOR_STREAM StreamData = {};
	StreamData.Enable = TRUE;
	StreamData.InputFrameOrField = second ? 1 : 0;
	StreamData.pInputSurface = pInputView;
	hr = m_pVideoContext->VideoProcessorBlt(m_pVideoProcessor, pOutputView, 0, 1, &StreamData);

	return hr;
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

	pFrameInfo->Width = m_srcWidth;
	pFrameInfo->Height = m_srcHeight;
	pFrameInfo->D3dFormat = m_srcD3DFormat;
	pFrameInfo->ExtFormat.value = m_srcExFmt.value;

	return S_OK;
}

HRESULT CDX11VideoProcessor::GetAdapterDecription(CStringW& str)
{
	str = m_strAdapterDescription;
	return S_OK;
}

HRESULT CDX11VideoProcessor::DrawStats()
{
	if (!m_pD2DBrush || m_windowRect.IsRectEmpty()) {
		return E_ABORT;
	}

	CStringW str = L"Direct3D 11";
	str.AppendFormat(L"\nFrame  rate  : %7.03f", m_FrameStats.GetAverageFps());
	if (m_SampleFormat != D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE) {
		str.Append(L" i");
	}
	str.AppendFormat(L"\nInput format : %s", DXGIFormatToString(m_srcDXGIFormat));
	str.AppendFormat(L"\nVP output fmt: %s", DXGIFormatToString(m_VPOutputFmt));
	str.AppendFormat(L"\nSync offset  :%+4d ms", m_SyncOffsetMS);

	CComPtr<IDWriteTextLayout> pTextLayout;
	if (S_OK == m_pDWriteFactory->CreateTextLayout(str, str.GetLength(), m_pTextFormat, m_windowRect.right - 10, m_windowRect.bottom - 10, &pTextLayout)) {
		m_pD2D1RenderTarget->BeginDraw();
		m_pD2D1RenderTarget->DrawTextLayout(D2D1::Point2F( 9.0f, 11.0f), pTextLayout, m_pD2DBrushBlack);
		m_pD2D1RenderTarget->DrawTextLayout(D2D1::Point2F(11.0f, 11.0f), pTextLayout, m_pD2DBrushBlack);
		m_pD2D1RenderTarget->DrawTextLayout(D2D1::Point2F(10.0f, 10.0f), pTextLayout, m_pD2DBrush);
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
