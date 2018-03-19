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
#include "Helper.h"
#include "DX11VideoProcessor.h"
#include "./Include/ID3DVideoMemoryConfiguration.h"


// CDX11VideoProcessor

CDX11VideoProcessor::CDX11VideoProcessor()
{
}

CDX11VideoProcessor::~CDX11VideoProcessor()
{
	ClearD3D11();

	if (m_hD3D11Lib) {
		FreeLibrary(m_hD3D11Lib);
	}
}

HRESULT CDX11VideoProcessor::Init()
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

	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1 };
	D3D_FEATURE_LEVEL featurelevel;

	ID3D11Device *pDevice = nullptr;
	ID3D11DeviceContext *pImmediateContext = nullptr;
	HRESULT hr = pfnD3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
#ifdef _DEBUG
		D3D11_CREATE_DEVICE_DEBUG, // need SDK for Windows 8
#else
		0,
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

	return SetDevice(pDevice, pImmediateContext);
}

void CDX11VideoProcessor::ClearD3D11()
{
	m_pSrcTexture2D.Release();
	m_pSrcTexture2D_Decode.Release();
	m_pVideoProcessor.Release();
	m_pVideoProcessorEnum.Release();
	m_pVideoDevice.Release();
	m_pVideoContext1.Release();
	m_pVideoContext.Release();
	m_pImmediateContext.Release();
	m_pDXGISwapChain.Release();
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

	m_pVideoContext->QueryInterface(__uuidof(ID3D11VideoContext1), (void**)&m_pVideoContext1);

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
		hr = InitSwapChain(m_hWnd, m_windowRect.Width(), m_windowRect.Height(), true);
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
		m_strAdapterDescription.Format(L"%S (%04X:%04X)", dxgiAdapterDesc.Description, dxgiAdapterDesc.VendorId, dxgiAdapterDesc.DeviceId);
	}

	return hr;
}

HRESULT CDX11VideoProcessor::InitSwapChain(HWND hwnd, UINT width, UINT height, const bool bReinit/* = false*/)
{
	CheckPointer(hwnd, E_FAIL);
	CheckPointer(m_pVideoDevice, E_FAIL);

	m_hWnd = hwnd;

	if (!width || !height) {
		RECT rc;
		GetClientRect(hwnd, &rc);
		width = rc.right - rc.left;
		height = rc.bottom - rc.top;
	}

	HRESULT hr = S_OK;
	if (!bReinit && m_pDXGISwapChain) {
		hr = m_pDXGISwapChain->ResizeBuffers(1, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
		if (SUCCEEDED(hr)) {
			return hr;
		}
	}

	m_pDXGISwapChain.Release();

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = 1;
	CComPtr<IDXGISwapChain1> pDXGISwapChain1;
	hr = m_pDXGIFactory2->CreateSwapChainForHwnd(m_pDevice, hwnd, &desc, nullptr, nullptr, &pDXGISwapChain1);
	if (FAILED(hr)) {
		return hr;
	}

	hr = pDXGISwapChain1->QueryInterface(__uuidof(IDXGISwapChain), (void**)&m_pDXGISwapChain);
	return hr;
}

BOOL CDX11VideoProcessor::InitMediaType(const CMediaType* pmt)
{
	m_mt = *pmt;

	if (m_mt.formattype == FORMAT_VideoInfo2) {
		const VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)m_mt.pbFormat;
		m_nativeVideoRect = m_srcRect = vih2->rcSource;
		m_trgRect = vih2->rcTarget;
		m_srcWidth = vih2->bmiHeader.biWidth;
		m_srcHeight = labs(vih2->bmiHeader.biHeight);
		m_srcAspectRatioX = vih2->dwPictAspectRatioX;
		m_srcAspectRatioY = vih2->dwPictAspectRatioY;
		m_srcExFmt.value = 0;

		m_bInterlaced = (vih2->dwInterlaceFlags & AMINTERLACE_IsInterlaced);
		m_srcD3DFormat = MediaSubtype2D3DFormat(pmt->subtype);
		m_srcDXGIFormat = MediaSubtype2DXGIFormat(pmt->subtype);

		if (m_srcD3DFormat == D3DFMT_X8R8G8B8 || m_srcD3DFormat == D3DFMT_A8R8G8B8) {
			m_srcPitch = m_srcWidth * 4;
		}
		else {
			if (vih2->dwControlFlags & (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT)) {
				m_srcExFmt.value = vih2->dwControlFlags;
				m_srcExFmt.SampleFormat = AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT; // ignore other flags
			}

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
			}
		}

		if (FAILED(Initialize(m_srcWidth, m_srcHeight, m_srcDXGIFormat))) {
			return FALSE;
		}

		return TRUE;
	}

	return FALSE;
}

HRESULT CDX11VideoProcessor::Initialize(const UINT width, const UINT height, const DXGI_FORMAT dxgiFormat)
{
	CheckPointer(m_pVideoDevice, E_FAIL);

	if (dxgiFormat == m_D3D11_Src_Format && width == m_D3D11_Src_Width && height == m_D3D11_Src_Height) {
		return S_OK;
	}

	HRESULT hr = S_OK;

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
	DXGI_FORMAT VP_Output_Format = DXGI_FORMAT_B8G8R8X8_UNORM;

	hr = m_pVideoProcessorEnum->CheckVideoProcessorFormat(VP_Output_Format, &uiFlags);
	if (FAILED(hr) || 0 == (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
		return MF_E_UNSUPPORTED_D3D_TYPE;
	}

	UINT index = 0;
	if (m_bInterlaced) {
		D3D11_VIDEO_PROCESSOR_CAPS caps = {};
		hr = m_pVideoProcessorEnum->GetVideoProcessorCaps(&caps);
		if (FAILED(hr)) {
			return hr;
		}

		UINT proccaps = D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BLEND + D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BOB + D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_ADAPTIVE + D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_MOTION_COMPENSATION;
		D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS convCaps = {};
		for (index = 0; index < caps.RateConversionCapsCount; index++) {
			hr = m_pVideoProcessorEnum->GetVideoProcessorRateConversionCaps(index, &convCaps);
			if (S_OK == hr) {
				// Check the caps to see which deinterlacer is supported
				if ((convCaps.ProcessorCaps & proccaps) != 0) {
					break;
				}
			}
		}
		if (index >= caps.RateConversionCapsCount) {
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
	hr = m_pDevice->CreateTexture2D(&desc, NULL, &m_pSrcTexture2D);
	if (FAILED(hr)) {
		return hr;
	}

	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = 0;
	hr = m_pDevice->CreateTexture2D(&desc, NULL, &m_pSrcTexture2D_Decode);
	if (FAILED(hr)) {
		return hr;
	}

	m_D3D11_Src_Format = dxgiFormat;
	m_D3D11_Src_Width = width;
	m_D3D11_Src_Height = height;

	return S_OK;
}

HRESULT CDX11VideoProcessor::CopySample(IMediaSample* pSample)
{
	CheckPointer(m_pSrcTexture2D, E_FAIL);
	CheckPointer(m_pDXGISwapChain, E_FAIL);

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

	if (CComQIPtr<IMediaSampleD3D11> pMSD3D11 = pSample) {
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
			//IDirect3DDevice9* pD3DDev;
			//pSurface->GetDevice(&pD3DDev);
			//if (FAILED(hr)) {
			//	return hr;
			//}
			D3DSURFACE_DESC desc = {};
			hr = pSurface->GetDesc(&desc);
			if (FAILED(hr)) {
				return hr;
			}
			hr = Initialize(desc.Width, desc.Height, m_srcDXGIFormat);
			if (FAILED(hr)) {
				return hr;
			}

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
	else if (m_mt.formattype == FORMAT_VideoInfo2) {
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
	
	return hr;
}

HRESULT CDX11VideoProcessor::Render(const FILTER_STATE filterState)
{
	CheckPointer(m_pSrcTexture2D, E_FAIL);
	CheckPointer(m_pDXGISwapChain, E_FAIL);

	HRESULT hr = S_OK;

	if (filterState == State_Running) {
		CComPtr<ID3D11Texture2D> pBackBuffer;
		hr = m_pDXGISwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
		if (FAILED(hr)) {
			return hr;
		}

		hr = ProcessDX11(pBackBuffer);
	}

	hr = m_pDXGISwapChain->Present(0, 0);

	return hr;
}

static bool ClipToTexture(ID3D11Texture2D* pTexture, CRect& s, CRect& d)
{
	D3D11_TEXTURE2D_DESC desc = {};
	pTexture->GetDesc(&desc);
	if (!desc.Width || !desc.Height) {
		return false;
	}

	const int w = desc.Width, h = desc.Height;
	const int sw = s.Width(), sh = s.Height();
	const int dw = d.Width(), dh = d.Height();

	if (d.left >= w || d.right < 0 || d.top >= h || d.bottom < 0
			|| sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) {
		s.SetRectEmpty();
		d.SetRectEmpty();
		return true;
	}

	if (d.right > w) {
		s.right -= (d.right - w) * sw / dw;
		d.right = w;
	}
	if (d.bottom > h) {
		s.bottom -= (d.bottom - h) * sh / dh;
		d.bottom = h;
	}
	if (d.left < 0) {
		s.left += (0 - d.left) * sw / dw;
		d.left = 0;
	}
	if (d.top < 0) {
		s.top += (0 - d.top) * sh / dh;
		d.top = 0;
	}

	return true;
}

HRESULT CDX11VideoProcessor::ProcessDX11(ID3D11Texture2D* pRenderTarget)
{
	CRect rSrcRect(m_nativeVideoRect);
	CRect rDstRect(m_videoRect);
	ClipToTexture(pRenderTarget, rSrcRect, rDstRect);

	// input format
	m_pVideoContext->VideoProcessorSetStreamFrameFormat(m_pVideoProcessor, 0, m_SampleFormat);

	// Output rate (repeat frames)
	m_pVideoContext->VideoProcessorSetStreamOutputRate(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, TRUE, NULL);
	
	// disable automatic video quality by driver
	m_pVideoContext->VideoProcessorSetStreamAutoProcessingMode(m_pVideoProcessor, 0, FALSE);

	// Source rect
	m_pVideoContext->VideoProcessorSetStreamSourceRect(m_pVideoProcessor, 0, TRUE, rSrcRect);

	// Dest rect
	m_pVideoContext->VideoProcessorSetStreamDestRect(m_pVideoProcessor, 0, TRUE, rDstRect);
	m_pVideoContext->VideoProcessorSetOutputTargetRect(m_pVideoProcessor, TRUE, m_windowRect);

	// Output background color (black)
	static const D3D11_VIDEO_COLOR backgroundColor = { 0.0f, 0.0f, 0.0f, 1.0f};
	m_pVideoContext->VideoProcessorSetOutputBackgroundColor(m_pVideoProcessor, FALSE, &backgroundColor);

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
	} else {
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

	D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = {};
	inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
	CComPtr<ID3D11VideoProcessorInputView> pInputView;
	HRESULT hr = m_pVideoDevice->CreateVideoProcessorInputView(m_pSrcTexture2D_Decode, m_pVideoProcessorEnum, &inputViewDesc, &pInputView);
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
