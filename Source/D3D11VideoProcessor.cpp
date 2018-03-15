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
#include "D3D11VideoProcessor.h"

static const struct FormatEntry {
	GUID            Subtype;
	D3DFORMAT       D3DFormat;
	DXGI_FORMAT     DXGIFormat;
}
s_DXGIFormatMapping[] = {
	{ MEDIASUBTYPE_RGB32,  D3DFMT_X8R8G8B8, DXGI_FORMAT_B8G8R8X8_UNORM },
	{ MEDIASUBTYPE_ARGB32, D3DFMT_A8R8G8B8, DXGI_FORMAT_R8G8B8A8_UNORM },
	{ MEDIASUBTYPE_AYUV,   D3DFMT_AYUV,     DXGI_FORMAT_AYUV },
	{ MEDIASUBTYPE_YUY2,   D3DFMT_YUY2,     DXGI_FORMAT_YUY2 },
	{ MEDIASUBTYPE_NV12,   D3DFMT_NV12,     DXGI_FORMAT_NV12 },
	{ MEDIASUBTYPE_P010,   D3DFMT_P010,     DXGI_FORMAT_P010 },
};

D3DFORMAT MediaSubtype2D3DFormat(GUID subtype)
{
	for (unsigned i = 0; i < ARRAYSIZE(s_DXGIFormatMapping); i++) {
		const FormatEntry& e = s_DXGIFormatMapping[i];
		if (e.Subtype == subtype) {
			return e.D3DFormat;
		}
	}
	return D3DFMT_UNKNOWN;
}

DXGI_FORMAT MediaSubtype2DXGIFormat(GUID subtype)
{
	for (unsigned i = 0; i < ARRAYSIZE(s_DXGIFormatMapping); i++) {
		const FormatEntry& e = s_DXGIFormatMapping[i];
		if (e.Subtype == subtype) {
			return e.DXGIFormat;
		}
	}
	return DXGI_FORMAT_UNKNOWN;
}

// CD3D11VideoProcessor

CD3D11VideoProcessor::CD3D11VideoProcessor()
{
	m_hD3D11Lib = LoadLibraryW(L"d3d11.dll");
	if (!m_hD3D11Lib) {
		return;
	}

	HRESULT(WINAPI *pfnD3D11CreateDevice)(
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
		return;
	}

	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1 };
	D3D_FEATURE_LEVEL featurelevel;

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
		&m_pDevice,
		&featurelevel,
		&m_pImmediateContext);
	if (FAILED(hr)) {
		return;
	}

	hr = m_pDevice->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&m_pVideoDevice);
	if (FAILED(hr)) {
		return;
	}

	hr = m_pImmediateContext->QueryInterface(__uuidof(ID3D11VideoContext), (void**)&m_pVideoContext);
	if (FAILED(hr)) {
		m_pImmediateContext.Release();
		m_pVideoDevice.Release();
		return;
	}

	CComPtr<IDXGIDevice> pDXGIDevice;
	hr = m_pDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice);
	if (FAILED(hr)) {
		m_pImmediateContext.Release();
		m_pVideoDevice.Release();
		return;
	}

	CComPtr<IDXGIAdapter> pDXGIAdapter;
	hr = pDXGIDevice->GetAdapter(&pDXGIAdapter);
	if (FAILED(hr)) {
		m_pImmediateContext.Release();
		m_pVideoDevice.Release();
		return;
	}

	DXGI_ADAPTER_DESC dxgiAdapterDesc = {};
	hr = pDXGIAdapter->GetDesc(&dxgiAdapterDesc);
	if (SUCCEEDED(hr)) {
		m_VendorId = dxgiAdapterDesc.VendorId;
		m_strAdapterDescription.Format(L"%S (%04X:%04X)", dxgiAdapterDesc.Description, dxgiAdapterDesc.VendorId, dxgiAdapterDesc.DeviceId);
	}

	CComPtr<IDXGIFactory1> pDXGIFactory1;
	hr = pDXGIAdapter->GetParent(__uuidof(IDXGIFactory1), (void**)&pDXGIFactory1);
	if (FAILED(hr)) {
		m_pImmediateContext.Release();
		m_pVideoDevice.Release();
		return;
	}

	hr = pDXGIFactory1->QueryInterface(__uuidof(IDXGIFactory2), (void**)&m_pDXGIFactory2);
	if (FAILED(hr)) {
		m_pImmediateContext.Release();
		m_pVideoDevice.Release();
	}
}

CD3D11VideoProcessor::~CD3D11VideoProcessor()
{
	m_pSrcTexture2D.Release();
	m_pSrcTexture2D_Decode.Release();
	m_pVideoProcessor.Release();
	m_pVideoProcessorEnum.Release();
	m_pVideoDevice.Release();
	m_pImmediateContext.Release();
	m_pDXGISwapChain.Release();
	m_pDXGIFactory2.Release();
	m_pDevice.Release();

	if (m_hD3D11Lib) {
		FreeLibrary(m_hD3D11Lib);
	}
}

HRESULT CD3D11VideoProcessor::InitSwapChain(HWND hwnd, UINT width, UINT height, const bool bReinit/* = false*/)
{
	CheckPointer(hwnd, E_FAIL);
	CheckPointer(m_pVideoDevice, E_FAIL);

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

BOOL CD3D11VideoProcessor::InitMediaType(const CMediaType* pmt)
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

		if (m_mt.subtype == MEDIASUBTYPE_RGB32 || m_mt.subtype == MEDIASUBTYPE_ARGB32) {
			m_srcLines = m_srcHeight;
		}
		else {
			if (vih2->dwControlFlags & (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT)) {
				m_srcExFmt.value = vih2->dwControlFlags;
				m_srcExFmt.SampleFormat = AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT; // ignore other flags
			}
			if (m_mt.subtype == MEDIASUBTYPE_NV12 || m_mt.subtype == MEDIASUBTYPE_YV12 || m_mt.subtype == MEDIASUBTYPE_P010) {
				m_srcLines = m_srcHeight * 3 / 2;
			}
			else {
				m_srcLines = m_srcHeight;
			}
		}
		m_srcPitch = (vih2->bmiHeader.biBitCount ? (m_srcWidth * m_srcHeight * vih2->bmiHeader.biBitCount / 8) : vih2->bmiHeader.biSizeImage) / m_srcLines;

		if (FAILED(Initialize(m_srcWidth, m_srcHeight, m_srcDXGIFormat))) {
			return FALSE;
		}

		return TRUE;
	}

	return FALSE;
}

HRESULT CD3D11VideoProcessor::Initialize(const UINT width, const UINT height, const DXGI_FORMAT dxgiFormat)
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
	desc.BindFlags = D3D11_BIND_DECODER;
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

HRESULT CD3D11VideoProcessor::CopySample(IMediaSample* pSample)
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

	if (CComQIPtr<IMFGetService> pService = pSample) {
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
			}
		}
	}
	
	return hr;
}

HRESULT CD3D11VideoProcessor::Render(const FILTER_STATE filterState)
{
	CheckPointer(m_pSrcTexture2D, E_FAIL);
	CheckPointer(m_pDXGISwapChain, E_FAIL);

	HRESULT hr = S_OK;

	if (filterState == State_Running) {
		// input format
		m_pVideoContext->VideoProcessorSetStreamFrameFormat(m_pVideoProcessor, 0, m_SampleFormat);

		// Output rate (repeat frames)
		m_pVideoContext->VideoProcessorSetStreamOutputRate(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, TRUE, NULL);
	
		// disable automatic video quality by driver
		m_pVideoContext->VideoProcessorSetStreamAutoProcessingMode(m_pVideoProcessor, 0, FALSE);

		// Source rect
		m_pVideoContext->VideoProcessorSetStreamSourceRect(m_pVideoProcessor, 0, TRUE, m_nativeVideoRect);

		// Dest rect
		m_pVideoContext->VideoProcessorSetStreamDestRect(m_pVideoProcessor, 0, TRUE, m_videoRect);
		m_pVideoContext->VideoProcessorSetOutputTargetRect(m_pVideoProcessor, TRUE, m_windowRect);

		// Output background color (black)
		static const D3D11_VIDEO_COLOR backgroundColor = { 0.0f, 0.0f, 0.0f, 1.0f};
		m_pVideoContext->VideoProcessorSetOutputBackgroundColor(m_pVideoProcessor, FALSE, &backgroundColor);

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

		m_pImmediateContext->CopyResource(m_pSrcTexture2D_Decode, m_pSrcTexture2D); // we can't use texture with D3D11_CPU_ACCESS_WRITE flag

		D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = {};
		inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
		CComPtr<ID3D11VideoProcessorInputView> pInputView;
		hr = m_pVideoDevice->CreateVideoProcessorInputView(m_pSrcTexture2D_Decode, m_pVideoProcessorEnum, &inputViewDesc, &pInputView);
		if (FAILED(hr)) {
			return hr;
		}

		CComPtr<ID3D11Texture2D> pDXGIBackBuffer;
		hr = m_pDXGISwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pDXGIBackBuffer);
		if (FAILED(hr)) {
			return hr;
		}

		D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC OutputViewDesc = {};
		OutputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
		CComPtr<ID3D11VideoProcessorOutputView> pOutputView;
		hr = m_pVideoDevice->CreateVideoProcessorOutputView(pDXGIBackBuffer, m_pVideoProcessorEnum, &OutputViewDesc, &pOutputView);
		if (FAILED(hr)) {
			return hr;
		}

		D3D11_VIDEO_PROCESSOR_STREAM StreamData = {};
		StreamData.Enable = TRUE;
		StreamData.pInputSurface = pInputView;
		hr = m_pVideoContext->VideoProcessorBlt(m_pVideoProcessor, pOutputView, 0, 1, &StreamData);
		if (FAILED(hr)) {
			return hr;
		}
	}

	hr = m_pDXGISwapChain->Present(0, 0);

	return hr;
}
