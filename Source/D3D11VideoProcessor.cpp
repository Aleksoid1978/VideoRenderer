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
#include <evr.h>
#include <Mferror.h>
#include <Mfidl.h>
#include <directxcolors.h>
#include "D3D11VideoProcessor.h"

static const struct FormatEntry {
	GUID            Subtype;
	DXGI_FORMAT     DXGIFormat;
}
s_DXGIFormatMapping[] = {
	{ MEDIASUBTYPE_RGB32,   DXGI_FORMAT_B8G8R8X8_UNORM },
	{ MEDIASUBTYPE_ARGB32,  DXGI_FORMAT_R8G8B8A8_UNORM },
	{ MEDIASUBTYPE_AYUV,    DXGI_FORMAT_AYUV },
	{ MEDIASUBTYPE_YUY2,    DXGI_FORMAT_YUY2 },
	{ MEDIASUBTYPE_NV12,    DXGI_FORMAT_NV12 },
	{ MEDIASUBTYPE_P010,    DXGI_FORMAT_P010 },
};

DXGI_FORMAT MediaSubtype2DXGIFormat(GUID subtype)
{
	DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;
	for (unsigned i = 0; i < ARRAYSIZE(s_DXGIFormatMapping); i++) {
		const FormatEntry& e = s_DXGIFormatMapping[i];
		if (e.Subtype == subtype) {
			dxgiFormat = e.DXGIFormat;
			break;
		}
	}
	return dxgiFormat;
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
		return;
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

HRESULT CD3D11VideoProcessor::InitSwapChain(HWND hwnd, UINT width, UINT height)
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
	if (m_pDXGISwapChain) {
		hr = m_pDXGISwapChain->ResizeBuffers(1, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
		if (SUCCEEDED(hr)) {
			return hr;
		}
	}

	m_pDXGISwapChain.Release();

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
	if (FAILED(hr)) {
		return hr;
	}

	return hr;
}

static HRESULT CheckInputMediaType(ID3D11VideoDevice* pVideoDevice, const GUID subtype, const UINT width, const UINT height)
{
	DXGI_FORMAT dxgiFormat = MediaSubtype2DXGIFormat(subtype);
	if (dxgiFormat == DXGI_FORMAT_UNKNOWN) {
		return E_FAIL;
	}

	HRESULT hr = S_OK;

	//Check if the format is supported
	D3D11_VIDEO_PROCESSOR_CONTENT_DESC ContentDesc;
	ZeroMemory(&ContentDesc, sizeof(ContentDesc));
	ContentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST;
	ContentDesc.InputWidth = width;
	ContentDesc.InputHeight = height;
	ContentDesc.OutputWidth = ContentDesc.InputWidth;
	ContentDesc.OutputHeight = ContentDesc.InputHeight;
	ContentDesc.InputFrameRate.Numerator = 30000;
	ContentDesc.InputFrameRate.Denominator = 1001;
	ContentDesc.OutputFrameRate.Numerator = 30000;
	ContentDesc.OutputFrameRate.Denominator = 1001;
	ContentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

	CComPtr<ID3D11VideoProcessorEnumerator> pVideoProcessorEnum;
	hr = pVideoDevice->CreateVideoProcessorEnumerator(&ContentDesc, &pVideoProcessorEnum);
	if (FAILED(hr)) {
		return hr;
	}

	UINT uiFlags;
	hr = pVideoProcessorEnum->CheckVideoProcessorFormat(dxgiFormat, &uiFlags);
	if (FAILED(hr) || 0 == (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)) {
		return MF_E_UNSUPPORTED_D3D_TYPE;
	}

	return hr;
}

HRESULT CD3D11VideoProcessor::IsMediaTypeSupported(const GUID subtype, const UINT width, const UINT height)
{
	CheckPointer(m_pVideoDevice, E_FAIL);
	return CheckInputMediaType(m_pVideoDevice, subtype, width, height);
}


HRESULT CD3D11VideoProcessor::Initialize(const GUID subtype, const UINT width, const UINT height)
{
	CheckPointer(m_pVideoDevice, E_FAIL);
	if (subtype == m_srcSubtype && width == m_srcWidth && height == m_srcHeight) {
		return S_OK;
	}

	HRESULT hr = S_OK;

	m_pSrcTexture2D.Release();
	m_pSrcTexture2D_Decode.Release();
	m_pVideoProcessor.Release();
	m_pVideoProcessorEnum.Release();

	hr = CheckInputMediaType(m_pVideoDevice, subtype, width, height);
	if (FAILED(hr)) {
		return hr;
	}
	DXGI_FORMAT dxgiFormat = MediaSubtype2DXGIFormat(subtype);

	D3D11_VIDEO_PROCESSOR_CONTENT_DESC ContentDesc;
	ZeroMemory(&ContentDesc, sizeof(ContentDesc));
	ContentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST;
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

	D3D11_VIDEO_PROCESSOR_CAPS caps = {};
	hr = m_pVideoProcessorEnum->GetVideoProcessorCaps(&caps);
	if (FAILED(hr)) {
		return hr;
	}

	UINT proccaps = D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BLEND + D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BOB + D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_ADAPTIVE + D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_MOTION_COMPENSATION;
	D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS convCaps = {};	
	UINT index;
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
	hr = m_pDevice->CreateTexture2D(&desc, NULL, &m_pSrcTexture2D_Decode);
	if (FAILED(hr)) {
		return hr;
	}

	m_srcFormat = dxgiFormat;
	m_srcSubtype = subtype;
	m_srcWidth  = width;
	m_srcHeight = height;

	return S_OK;
}

HRESULT CD3D11VideoProcessor::CopySample(IMediaSample* pSample, const AM_MEDIA_TYPE* pmt, IDirect3DDevice9Ex* pD3DDevEx)
{
	CheckPointer(m_pSrcTexture2D, E_FAIL);
	CheckPointer(m_pDXGISwapChain, E_FAIL);

	HRESULT hr = S_OK;

	if (CComQIPtr<IMFGetService> pService = pSample) {
		CComPtr<IDirect3DSurface9> pSurface;
		if (SUCCEEDED(pService->GetService(MR_BUFFER_SERVICE, IID_PPV_ARGS(&pSurface)))) {
			D3DSURFACE_DESC desc;
			hr = pSurface->GetDesc(&desc);
			if (FAILED(hr)) {
				return hr;
			}
			hr = Initialize(pmt->subtype, desc.Width, desc.Height);
			if (FAILED(hr)) {
				return hr;
			}

			D3DLOCKED_RECT lr_src;
			hr = pSurface->LockRect(&lr_src, nullptr, D3DLOCK_READONLY);
			if (S_OK == hr) {
				CComPtr<ID3D11DeviceContext> pImmediateContext;
				m_pDevice->GetImmediateContext(&pImmediateContext);
				if (pImmediateContext) {
					D3D11_MAPPED_SUBRESOURCE mappedResource = {};
					if (hr = pImmediateContext->Map(m_pSrcTexture2D, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource) == S_OK) {
						memcpy(mappedResource.pData, (BYTE*)lr_src.pBits, lr_src.Pitch * desc.Height * 3 / 2);
						pImmediateContext->Unmap(m_pSrcTexture2D, 0);
					}
				}

				hr = pSurface->UnlockRect();
			}
		}
	}
	else if (pmt->formattype == FORMAT_VideoInfo2) {
		BYTE* data = nullptr;
		const long size = pSample->GetActualDataLength();
		if (size > 0 && S_OK == pSample->GetPointer(&data)) {
			CComPtr<ID3D11DeviceContext> pImmediateContext;
			m_pDevice->GetImmediateContext(&pImmediateContext);
			if (pImmediateContext) {
				D3D11_MAPPED_SUBRESOURCE mappedResource = {};
				if (hr = pImmediateContext->Map(m_pSrcTexture2D, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource) == S_OK) {
					memcpy(mappedResource.pData, data, size);
					pImmediateContext->Unmap(m_pSrcTexture2D, 0);
				}
			}
		}
	}
	
	return hr;
}

HRESULT CD3D11VideoProcessor::Render()
{
	CheckPointer(m_pSrcTexture2D, E_FAIL);
	CheckPointer(m_pDXGISwapChain, E_FAIL);

	HRESULT hr = S_OK;

	// input format
	D3D11_VIDEO_FRAME_FORMAT FrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
	m_pVideoContext->VideoProcessorSetStreamFrameFormat(m_pVideoProcessor, 0, FrameFormat);

    // Output rate (repeat frames)
	m_pVideoContext->VideoProcessorSetStreamOutputRate(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, TRUE, NULL);

	// Output background color (black)
	D3D11_VIDEO_COLOR backgroundColor = {};
	backgroundColor.RGBA.A = 1.0F;
	backgroundColor.RGBA.R = 1.0F * static_cast<float>(GetRValue(0)) / 255.0F;
	backgroundColor.RGBA.G = 1.0F * static_cast<float>(GetGValue(0)) / 255.0F;
	backgroundColor.RGBA.B = 1.0F * static_cast<float>(GetBValue(0)) / 255.0F;
	m_pVideoContext->VideoProcessorSetOutputBackgroundColor(m_pVideoProcessor, FALSE, &backgroundColor);

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

	/*
	D3D11_VIDEO_PROCESSOR_STREAM StreamData = {};
	StreamData.pInputSurface = pInputView;
	hr = m_pVideoContext->VideoProcessorBlt(m_pVideoProcessor, pOutputView, 0, 1, &StreamData);
	if (FAILED(hr)) {
		return hr;
	}
	*/

	// just for present test ...
	CComPtr<ID3D11RenderTargetView> pRenderTargetView;
	hr = m_pDevice->CreateRenderTargetView(pDXGIBackBuffer, nullptr, &pRenderTargetView);

	m_pImmediateContext->ClearRenderTargetView(pRenderTargetView, DirectX::Colors::MediumSlateBlue); 
	hr = m_pDXGISwapChain->Present(0, 0);

	return hr;
}
