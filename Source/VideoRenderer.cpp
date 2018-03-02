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
#include "VideoRenderer.h"

#include <algorithm>
#include <vector>
#include <atlstr.h>
#include <mfapi.h>
#include <Mferror.h>
#include <Dvdmedia.h>
#include "Helper.h"

#pragma region DXVAHD
HRESULT DXVAHD_SetStreamFormat(IDXVAHD_VideoProcessor *pVP, UINT stream, D3DFORMAT format)
{
	DXVAHD_STREAM_STATE_D3DFORMAT_DATA d3dformat = { format };

	HRESULT hr = pVP->SetVideoProcessStreamState(
		stream,
		DXVAHD_STREAM_STATE_D3DFORMAT,
		sizeof(d3dformat),
		&d3dformat
	);

	return hr;
}

HRESULT DXVAHD_SetFrameFormat(IDXVAHD_VideoProcessor *pVP, UINT stream, DXVAHD_FRAME_FORMAT format)
{
	DXVAHD_STREAM_STATE_FRAME_FORMAT_DATA frame_format = { format };

	HRESULT hr = pVP->SetVideoProcessStreamState(
		stream,
		DXVAHD_STREAM_STATE_FRAME_FORMAT,
		sizeof(frame_format),
		&frame_format
	);

	return hr;
}

HRESULT DXVAHD_SetTargetRect(IDXVAHD_VideoProcessor *pVP, BOOL bEnable, const RECT &rect)
{
	DXVAHD_BLT_STATE_TARGET_RECT_DATA tr = { bEnable, rect };

	HRESULT hr = pVP->SetVideoProcessBltState(
		DXVAHD_BLT_STATE_TARGET_RECT,
		sizeof(tr),
		&tr
	);

	return hr;
}

HRESULT DXVAHD_SetSourceRect(IDXVAHD_VideoProcessor *pVP, UINT stream, BOOL bEnable, const RECT& rect)
{
	DXVAHD_STREAM_STATE_SOURCE_RECT_DATA src = { bEnable, rect };

	HRESULT hr = pVP->SetVideoProcessStreamState(
		stream,
		DXVAHD_STREAM_STATE_SOURCE_RECT,
		sizeof(src),
		&src
	);

	return hr;
}

HRESULT DXVAHD_SetDestinationRect(IDXVAHD_VideoProcessor *pVP, UINT stream, BOOL bEnable, const RECT &rect)
{
	DXVAHD_STREAM_STATE_DESTINATION_RECT_DATA DstRect = { bEnable, rect };

	HRESULT hr = pVP->SetVideoProcessStreamState(
		stream,
		DXVAHD_STREAM_STATE_DESTINATION_RECT,
		sizeof(DstRect),
		&DstRect
	);

	return hr;
}
#pragma endregion DXVAHD

class CVideoRendererInputPin : public CRendererInputPin,
	public IMFGetService,
	public IDirectXVideoMemoryConfiguration
{
public :
	CVideoRendererInputPin(CBaseRenderer *pRenderer, HRESULT *phr, LPCWSTR Name, CMpcVideoRenderer* pBaseRenderer);

	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	STDMETHODIMP GetAllocator(IMemAllocator **ppAllocator) {
		// Renderer shouldn't manage allocator for DXVA
		return E_NOTIMPL;
	}

	STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES* pProps) {
		// 1 buffer required
		ZeroMemory(pProps, sizeof(ALLOCATOR_PROPERTIES));
		pProps->cbBuffer = 1;
		return S_OK;
	}

	// IMFGetService
	STDMETHODIMP GetService(REFGUID guidService, REFIID riid, LPVOID *ppvObject);

	// IDirectXVideoMemoryConfiguration
	STDMETHODIMP GetAvailableSurfaceTypeByIndex(DWORD dwTypeIndex, DXVA2_SurfaceType *pdwType);
	STDMETHODIMP SetSurfaceType(DXVA2_SurfaceType dwType);

private:
	CMpcVideoRenderer* m_pBaseRenderer;
};

CVideoRendererInputPin::CVideoRendererInputPin(CBaseRenderer *pRenderer, HRESULT *phr, LPCWSTR Name, CMpcVideoRenderer* pBaseRenderer)
	: CRendererInputPin(pRenderer, phr, Name)
	, m_pBaseRenderer(pBaseRenderer)
{
}

STDMETHODIMP CVideoRendererInputPin::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	return
		(riid == __uuidof(IMFGetService)) ? GetInterface((IMFGetService*)this, ppv) :
		__super::NonDelegatingQueryInterface(riid, ppv);
}

// IMFGetService
STDMETHODIMP CVideoRendererInputPin::GetService(REFGUID guidService, REFIID riid, LPVOID *ppvObject)
{
	if (riid == __uuidof(IDirectXVideoMemoryConfiguration)) {
		GetInterface((IDirectXVideoMemoryConfiguration*)this, ppvObject);
		return S_OK;
	}

	return m_pBaseRenderer->GetService(guidService, riid, ppvObject);
}

// IDirectXVideoMemoryConfiguration
STDMETHODIMP CVideoRendererInputPin::GetAvailableSurfaceTypeByIndex(DWORD dwTypeIndex, DXVA2_SurfaceType *pdwType)
{
	if (dwTypeIndex == 0) {
		*pdwType = DXVA2_SurfaceType_DecoderRenderTarget;
		return S_OK;
	} else {
		return MF_E_NO_MORE_TYPES;
	}
}

STDMETHODIMP CVideoRendererInputPin::SetSurfaceType(DXVA2_SurfaceType dwType)
{
	return S_OK;
}

//
// CMpcVideoRenderer
//

CMpcVideoRenderer::CMpcVideoRenderer(LPUNKNOWN pUnk, HRESULT* phr)
	: CBaseRenderer(__uuidof(this), NAME("MPC Video Renderer"), pUnk, phr)
{
#ifdef DEBUG
	DbgSetModuleLevel(LOG_TRACE, DWORD_MAX);
	DbgSetModuleLevel(LOG_ERROR, DWORD_MAX);
#endif

	ASSERT(S_OK == *phr);
	m_pInputPin = new CVideoRendererInputPin(this, phr, L"In", this);
	ASSERT(S_OK == *phr);

	m_hD3D9Lib = LoadLibraryW(L"d3d9.dll");
	if (!m_hD3D9Lib) {
		*phr = E_FAIL;
		return;
	}

	HRESULT (__stdcall * pDirect3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex**);
	(FARPROC &)pDirect3DCreate9Ex = GetProcAddress(m_hD3D9Lib, "Direct3DCreate9Ex");

	HRESULT hr = pDirect3DCreate9Ex(D3D_SDK_VERSION, &m_pD3DEx);
	if (!m_pD3DEx) {
		hr = pDirect3DCreate9Ex(D3D9b_SDK_VERSION, &m_pD3DEx);
	}
	if (!m_pD3DEx) {
		*phr = E_FAIL;
		return;
	}

	m_hDxva2Lib = LoadLibraryW(L"dxva2.dll");
	if (!m_hDxva2Lib) {
		*phr = E_FAIL;
		return;
	}

	(FARPROC &)pDXVA2CreateDirect3DDeviceManager9 = GetProcAddress(m_hDxva2Lib, "DXVA2CreateDirect3DDeviceManager9");
	(FARPROC &)pDXVA2CreateVideoService = GetProcAddress(m_hDxva2Lib, "DXVA2CreateVideoService");
	pDXVA2CreateDirect3DDeviceManager9(&m_nResetTocken, &m_pD3DDeviceManager);
	if (!m_pD3DDeviceManager) {
		*phr = E_FAIL;
		return;
	}

	hr = InitDirect3D9();

	if (S_OK == hr) {
		hr = m_pD3DDeviceManager->ResetDevice(m_pD3DDevEx, m_nResetTocken);
	}
	if (S_OK == hr) {
		hr = m_pD3DDeviceManager->OpenDeviceHandle(&m_hDevice);
	}

	*phr = hr;
}

CMpcVideoRenderer::~CMpcVideoRenderer()
{
	if (m_pD3DDeviceManager) {
		if (m_hDevice != INVALID_HANDLE_VALUE) {
			m_pD3DDeviceManager->CloseDeviceHandle(m_hDevice);
			m_hDevice = INVALID_HANDLE_VALUE;
		}
		m_pD3DDeviceManager.Release();
	}

	m_pDXVAHD_VP.Release();
	m_pDXVAHD_Device.Release();
	if (m_hDxva2Lib) {
		FreeLibrary(m_hDxva2Lib);
	}

	m_pD3DDevEx.Release();
	m_pD3DEx.Release();
	if (m_hD3D9Lib) {
		FreeLibrary(m_hD3D9Lib);
	}
}

STDMETHODIMP CMpcVideoRenderer::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	HRESULT hr;
	if (riid == __uuidof(IMFGetService)) {
		hr = GetInterface((IMFGetService*)this, ppv);
	} else if (riid == __uuidof(IMFVideoDisplayControl)) {
		hr = GetInterface((IMFVideoDisplayControl*)this, ppv);
	} else {
		hr = __super::NonDelegatingQueryInterface(riid, ppv);
	}

	return hr;
}

// IMFGetService
STDMETHODIMP CMpcVideoRenderer::GetService(REFGUID guidService, REFIID riid, LPVOID *ppvObject)
{
	if (guidService == MR_VIDEO_ACCELERATION_SERVICE) {
		if (riid == __uuidof(IDirect3DDeviceManager9)) {
			return m_pD3DDeviceManager->QueryInterface(riid, ppvObject);
		}
		/*
		} else if (riid == __uuidof(IDirectXVideoDecoderService) || riid == __uuidof(IDirectXVideoProcessorService) ) {
			return m_pD3DDeviceManager->GetVideoService(m_hDevice, riid, ppvObject);
		} else if (riid == __uuidof(IDirectXVideoAccelerationService)) {
			// TODO : to be tested....
			return pDXVA2CreateVideoService(m_pD3DDevEx, riid, ppvObject);
		}
		*/
	} else if (guidService == MR_VIDEO_RENDER_SERVICE) {
		if (riid == __uuidof(IMFVideoDisplayControl)) {
			GetInterface((IMFVideoDisplayControl*)this, ppvObject);
			return S_OK;
		}
	}

	return E_NOINTERFACE;
}

// IMFVideoDisplayControl
STDMETHODIMP CMpcVideoRenderer::SetVideoWindow(HWND hwndVideo)
{
	if (m_hWnd != hwndVideo) {
		m_hWnd = hwndVideo;
		return InitDirect3D9() ? S_OK : E_FAIL;
	}
	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::GetVideoWindow(HWND *phwndVideo)
{
	CheckPointer(phwndVideo, E_POINTER);
	*phwndVideo = m_hWnd;
	return S_OK;
}

static UINT GetAdapter(HWND hWnd, IDirect3D9Ex* pD3D)
{
	CheckPointer(hWnd, D3DADAPTER_DEFAULT);
	CheckPointer(pD3D, D3DADAPTER_DEFAULT);

	const HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
	CheckPointer(hMonitor, D3DADAPTER_DEFAULT);

	for (UINT adp = 0, num_adp = pD3D->GetAdapterCount(); adp < num_adp; ++adp) {
		const HMONITOR hAdapterMonitor = pD3D->GetAdapterMonitor(adp);
		if (hAdapterMonitor == hMonitor) {
			return adp;
		}
	}

	return D3DADAPTER_DEFAULT;
}

HRESULT CMpcVideoRenderer::InitDirect3D9()
{
	DLog(L"CMpcVideoRenderer::InitDirect3D9()");

	const UINT currentAdapter = GetAdapter(m_hWnd, m_pD3DEx);
	bool bTryToReset = (currentAdapter == m_CurrentAdapter) && m_pD3DDevEx;
	if (!bTryToReset) {
		m_pD3DDevEx.Release();
		m_CurrentAdapter = currentAdapter;
	}

	ZeroMemory(&m_DisplayMode, sizeof(D3DDISPLAYMODEEX));
	m_DisplayMode.Size = sizeof(D3DDISPLAYMODEEX);
	HRESULT hr = m_pD3DEx->GetAdapterDisplayModeEx(m_CurrentAdapter, &m_DisplayMode, nullptr);

	ZeroMemory(&m_d3dpp, sizeof(m_d3dpp));

	m_d3dpp.Windowed = TRUE;
	m_d3dpp.hDeviceWindow = m_hWnd;
	m_d3dpp.SwapEffect = D3DSWAPEFFECT_COPY;
	m_d3dpp.Flags = D3DPRESENTFLAG_VIDEO;
	m_d3dpp.BackBufferCount = 1;
	m_d3dpp.BackBufferWidth = m_DisplayMode.Width;
	m_d3dpp.BackBufferHeight = m_DisplayMode.Height;
	m_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

	if (bTryToReset) {
		bTryToReset = SUCCEEDED(hr = m_pD3DDevEx->ResetEx(&m_d3dpp, nullptr));
		DLog(L"    => ResetEx() : 0x%08x", hr);
	}

	if (!bTryToReset) {
		m_pD3DDevEx.Release();
		hr = m_pD3DEx->CreateDeviceEx(
			m_CurrentAdapter, D3DDEVTYPE_HAL, m_hWnd,
			D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_ENABLE_PRESENTSTATS,
			&m_d3dpp, nullptr, &m_pD3DDevEx);
		DLog(L"    => CreateDeviceEx() : 0x%08x", hr);
	}

	if (FAILED(hr)) {
		return hr;
	}
	if (!m_pD3DDevEx) {
		return E_FAIL;
	}

	while (hr == D3DERR_DEVICELOST) {
		DLog(L"    => D3DERR_DEVICELOST. Trying to Reset.");
		hr = m_pD3DDevEx->CheckDeviceState(m_hWnd);
	}
	if (hr == D3DERR_DEVICENOTRESET) {
		DLog(L"    => D3DERR_DEVICENOTRESET");
		hr = m_pD3DDevEx->ResetEx(&m_d3dpp, nullptr);
	}

	return hr;
}

BOOL CMpcVideoRenderer::InitializeDXVAHDVP(const UINT width, const UINT height)
{
	if (!m_hDxva2Lib) {
		return FALSE;
	}
	DLog("CMpcVideoRenderer::InitializeDXVAHDVP()");

	HRESULT(WINAPI *pDXVAHD_CreateDevice)(IDirect3DDevice9Ex *pD3DDevice, const DXVAHD_CONTENT_DESC *pContentDesc, DXVAHD_DEVICE_USAGE Usage, PDXVAHDSW_Plugin pPlugin, IDXVAHD_Device **ppDevice);
	(FARPROC &)pDXVAHD_CreateDevice = GetProcAddress(m_hDxva2Lib, "DXVAHD_CreateDevice");
	if (!pDXVAHD_CreateDevice) {
		return FALSE;
	}

	HRESULT hr = S_OK;

	DXVAHD_CONTENT_DESC desc;
	desc.InputFrameFormat = DXVAHD_FRAME_FORMAT_PROGRESSIVE;
	desc.InputFrameRate.Numerator = 60;
	desc.InputFrameRate.Denominator = 1;
	desc.InputWidth = width;
	desc.InputHeight = height;
	desc.OutputFrameRate.Numerator = 60;
	desc.OutputFrameRate.Denominator = 1;
	desc.OutputWidth = m_DisplayMode.Width;
	desc.OutputHeight = m_DisplayMode.Height;

	// Create the DXVA-HD device.
	hr = pDXVAHD_CreateDevice(m_pD3DDevEx, &desc, DXVAHD_DEVICE_USAGE_PLAYBACK_NORMAL, nullptr, &m_pDXVAHD_Device);
	if (FAILED(hr)) {
		DLog(L"InitializeDXVAHDVP() : DXVAHD_CreateDevice() failed with error 0x%08x", hr);
		return FALSE;
	}

	// Get the DXVA-HD device caps.
	hr = m_pDXVAHD_Device->GetVideoProcessorDeviceCaps(&m_DXVAHDDevCaps);
	if (FAILED(hr) || m_DXVAHDDevCaps.MaxInputStreams < 1) {
		return FALSE;
	}

	std::vector<D3DFORMAT> Formats;

	// Check the output formats.
	Formats.resize(m_DXVAHDDevCaps.OutputFormatCount);
	hr = m_pDXVAHD_Device->GetVideoProcessorOutputFormats(m_DXVAHDDevCaps.OutputFormatCount, Formats.data());
	if (FAILED(hr)) {
		DLog(L"InitializeDXVAHDVP() : GetVideoProcessorOutputFormats() failed with error 0x%08x", hr);
		return FALSE;
	}
#if _DEBUG
	{
		CStringW dbgstr = L"DXVA-HD output formats:";
		for (const auto& format : Formats) {
			dbgstr.AppendFormat(L"\n%s", D3DFormatToString(format));
		}
		DLog(dbgstr);
	}
#endif
	if (std::none_of(Formats.cbegin(), Formats.cend(), [](D3DFORMAT f) { return f == D3DFMT_X8R8G8B8; })) {
		return FALSE;
	}

	// Check the input formats.
	Formats.resize(m_DXVAHDDevCaps.InputFormatCount);
	hr = m_pDXVAHD_Device->GetVideoProcessorInputFormats(m_DXVAHDDevCaps.InputFormatCount, Formats.data());
	if (FAILED(hr)) {
		DLog(L"InitializeDXVAHDVP() : GetVideoProcessorInputFormats() failed with error 0x%08x", hr);
		return FALSE;
	}
#if _DEBUG
	{
		CStringW dbgstr = L"DXVA-HD input formats:";
		for (const auto& format : Formats) {
			dbgstr.AppendFormat(L"\n%s", D3DFormatToString(format));
		}
		DLog(dbgstr);
	}
#endif
	if (std::none_of(Formats.cbegin(), Formats.cend(), [](D3DFORMAT f) { return f == D3DFMT_X8R8G8B8; })) {
		return FALSE;
	}

	// Create the VP device.
	std::vector<DXVAHD_VPCAPS> VPCaps;
	VPCaps.resize(m_DXVAHDDevCaps.VideoProcessorCount);

	hr = m_pDXVAHD_Device->GetVideoProcessorCaps(m_DXVAHDDevCaps.VideoProcessorCount, VPCaps.data());
	if (FAILED(hr)) {
		DLog(L"InitializeDXVAHDVP() : GetVideoProcessorCaps() failed with error 0x%08x", hr);
		return FALSE;
	}

	hr = m_pDXVAHD_Device->CreateVideoProcessor(&VPCaps[0].VPGuid, &m_pDXVAHD_VP);
	if (FAILED(hr)) {
		DLog(L"InitializeDXVAHDVP() : CreateVideoProcessor() failed with error 0x%08x", hr);
		return FALSE;
	}

	// Set the initial stream states for the primary stream.
	hr = DXVAHD_SetStreamFormat(m_pDXVAHD_VP, 0, m_srcFormat);
	if (FAILED(hr)) {
		DLog(L"InitializeDXVAHDVP() : DXVAHD_SetStreamFormat() failed with error 0x%08x, format : %s", hr, D3DFormatToString(m_srcFormat));
		return FALSE;
	}

	hr = DXVAHD_SetFrameFormat(m_pDXVAHD_VP, 0, DXVAHD_FRAME_FORMAT_PROGRESSIVE);
	if (FAILED(hr)) {
		DLog(L"InitializeDXVAHDVP() : DXVAHD_SetFrameFormat() failed with error 0x%08x", hr);
		return FALSE;
	}

	return TRUE;
}

HRESULT CMpcVideoRenderer::ResizeDXVAHD(IDirect3DSurface9* pSurface, IDirect3DSurface9* pRenderTarget)
{
	HRESULT hr = S_OK;
	D3DSURFACE_DESC desc;
	if (S_OK != pSurface->GetDesc(&desc)) {
		return hr;
	};

	if (!m_pDXVAHD_VP && !InitializeDXVAHDVP(desc.Width, desc.Height)) {
		return E_FAIL;
	}

	static DWORD frame = 0;

	DXVAHD_STREAM_DATA stream_data = {};
	stream_data.Enable = TRUE;
	stream_data.OutputIndex = 0;
	stream_data.InputFrameOrField = frame;
	stream_data.pInputSurface = pSurface;

	RECT destRect = { 0, 0, m_DisplayMode.Width, m_DisplayMode.Height }; // TODO

	hr = DXVAHD_SetSourceRect(m_pDXVAHD_VP, 0, TRUE, m_srcRect);
	hr = DXVAHD_SetDestinationRect(m_pDXVAHD_VP, 0, TRUE, destRect);

	// Perform the blit.
	hr = m_pDXVAHD_VP->VideoProcessBltHD(pRenderTarget, frame, 1, &stream_data);
	if (FAILED(hr)) {
		DLog(L"CMpcVideoRenderer::ResizeDXVAHD() : VideoProcessBltHD() failed with error 0x%08x", hr);
	}
	frame++;

	return hr;
}

HRESULT CMpcVideoRenderer::ResizeDXVAHD(BYTE* data, const long size, IDirect3DSurface9* pRenderTarget)
{
	if (!m_pDXVAHD_Device && !InitializeDXVAHDVP(m_srcWidth, m_srcHeight)) {
		return E_FAIL;
	}

	HRESULT hr = S_OK;

	if (!m_pSrcSurface) {
		hr = m_pDXVAHD_Device->CreateVideoSurface(
			m_srcWidth,
			m_srcHeight,
			m_srcFormat,
			m_DXVAHDDevCaps.InputPool,
			0,
			DXVAHD_SURFACE_TYPE_VIDEO_INPUT,
			1,
			&m_pSrcSurface,
			NULL
		);
	}

	D3DLOCKED_RECT lr;
	hr = m_pSrcSurface->LockRect(&lr, NULL, D3DLOCK_NOSYSLOCK);
	if (FAILED(hr)) {
		return hr;
	}

	if (m_srcPitch == lr.Pitch) {
		memcpy(lr.pBits, data, size);
	}
	else if (m_srcPitch < lr.Pitch) {
		BYTE* src = data;
		BYTE* dst = (BYTE*)lr.pBits;
		for (UINT y = 0; y < m_srcLines; ++y) {
			memcpy(dst, src, m_srcPitch);
			src += m_srcPitch;
			dst += lr.Pitch;
		}
	}

	hr = m_pSrcSurface->UnlockRect();

	return ResizeDXVAHD(m_pSrcSurface, pRenderTarget);

	return hr;
}

HRESULT CMpcVideoRenderer::CheckMediaType(const CMediaType* pmt)
{
	if (pmt->formattype != FORMAT_VideoInfo2) {
		return E_FAIL;
	}

	for (const auto& type : sudPinTypesIn) {
		if (pmt->majortype == *type.clsMajorType && pmt->subtype == *type.clsMinorType) {
			return S_OK;
		}
	}

	return E_FAIL;
}

HRESULT CMpcVideoRenderer::SetMediaType(const CMediaType *pmt)
{
	HRESULT hr = __super::SetMediaType(pmt);

	if (S_OK == hr) {
		m_mt = *pmt;

		if (m_mt.formattype == FORMAT_VideoInfo2) {
			VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)m_mt.pbFormat;
			m_srcRect = vih2->rcSource;
			m_trgRect = vih2->rcTarget;
			m_srcWidth = vih2->bmiHeader.biWidth;
			m_srcHeight = vih2->bmiHeader.biHeight;

			if (m_mt.subtype == MEDIASUBTYPE_RGB32 || m_mt.subtype == MEDIASUBTYPE_ARGB32) {
				m_srcFormat = D3DFMT_X8R8G8B8;
				m_srcLines = m_srcHeight;
			}
			else {
				m_srcFormat = (D3DFORMAT)m_mt.subtype.Data1;
				if (m_mt.subtype == MEDIASUBTYPE_NV12 || m_mt.subtype == MEDIASUBTYPE_YV12) {
					m_srcLines = m_srcHeight * 3 / 2;
				}
				else {
					m_srcLines = m_srcHeight;
				}
			}
			m_srcPitch = vih2->bmiHeader.biSizeImage / m_srcHeight;

			m_pDXVAHD_VP.Release();
			m_pDXVAHD_Device.Release();
		}
	}

	return hr;
}

HRESULT CMpcVideoRenderer::DoRenderSample(IMediaSample* pSample)
{
	HRESULT hr = m_pD3DDevEx->BeginScene();

	CComPtr<IDirect3DSurface9> pBackBuffer;
	hr = m_pD3DDevEx->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);

	hr = m_pD3DDevEx->SetRenderTarget(0, pBackBuffer);
	hr = m_pD3DDevEx->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);

	if (CComQIPtr<IMFGetService> pService = pSample) {
		CComPtr<IDirect3DSurface9> pSurface;
		if (SUCCEEDED(pService->GetService(MR_BUFFER_SERVICE, IID_PPV_ARGS(&pSurface)))) {
			ResizeDXVAHD(pSurface, pBackBuffer);
		}
	}
	else if (m_mt.formattype == FORMAT_VideoInfo2) {
		BYTE* data = nullptr;
		const long size = pSample->GetActualDataLength();
		if (size > 0 && S_OK == pSample->GetPointer(&data)) {
			ResizeDXVAHD(data, size, pBackBuffer);
		}
	}

	hr = m_pD3DDevEx->EndScene();

	RECT destRect = { 0, 0, m_DisplayMode.Width, m_DisplayMode.Height }; // TODO
	const RECT rSrcPri(m_srcRect);
	const RECT rDstPri(destRect);
	hr = m_pD3DDevEx->PresentEx(&rSrcPri, &rDstPri, nullptr, nullptr, 0);

	return S_OK;
}
