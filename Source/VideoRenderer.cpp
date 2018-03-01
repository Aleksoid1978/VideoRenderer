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
#include <dxva2api.h>
#include <evr.h>
#include <mfapi.h>
#include <Mferror.h>
#include "Helper.h"

// dxva.dll
typedef HRESULT (__stdcall *PTR_DXVA2CreateDirect3DDeviceManager9)(UINT* pResetToken, IDirect3DDeviceManager9** ppDeviceManager);
typedef HRESULT (__stdcall *PTR_DXVA2CreateVideoService)(IDirect3DDevice9* pDD, REFIID riid, void** ppService);

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

class CVideoRendererInputPin : public CRendererInputPin,
	public IMFGetService,
	public IDirectXVideoMemoryConfiguration,
	public IMFVideoDisplayControl
{
public :
	CVideoRendererInputPin(CBaseRenderer *pRenderer, HRESULT *phr, LPCWSTR Name);
	~CVideoRendererInputPin() {
		if (m_pD3DDeviceManager) {
			if (m_hDevice != INVALID_HANDLE_VALUE) {
				m_pD3DDeviceManager->CloseDeviceHandle(m_hDevice);
				m_hDevice = INVALID_HANDLE_VALUE;
			}
			m_pD3DDeviceManager = nullptr;
		}
		if (m_pD3DDev) {
			m_pD3DDev = nullptr;
		}
		if (m_hDXVA2Lib) {
			FreeLibrary(m_hDXVA2Lib);
		}
	}

	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	STDMETHODIMP GetAllocator(IMemAllocator **ppAllocator) {
		// Renderer shouldn't manage allocator for DXVA
		return E_NOTIMPL;
	}

	STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES* pProps) {
		// 1 buffer required
		memset (pProps, 0, sizeof(ALLOCATOR_PROPERTIES));
		pProps->cbBuffer = 1;
		return S_OK;
	}

	// IMFGetService
	STDMETHODIMP GetService(REFGUID guidService, REFIID riid, LPVOID *ppvObject);

	// IDirectXVideoMemoryConfiguration
	STDMETHODIMP GetAvailableSurfaceTypeByIndex(DWORD dwTypeIndex, DXVA2_SurfaceType *pdwType);
	STDMETHODIMP SetSurfaceType(DXVA2_SurfaceType dwType);

	// IMFVideoDisplayControl
	STDMETHODIMP GetNativeVideoSize(SIZE *pszVideo, SIZE *pszARVideo) { return E_NOTIMPL; };
	STDMETHODIMP GetIdealVideoSize(SIZE *pszMin, SIZE *pszMax) { return E_NOTIMPL; };
	STDMETHODIMP SetVideoPosition(const MFVideoNormalizedRect *pnrcSource, const LPRECT prcDest) { return E_NOTIMPL; };
	STDMETHODIMP GetVideoPosition(MFVideoNormalizedRect *pnrcSource, LPRECT prcDest) { return E_NOTIMPL; };
	STDMETHODIMP SetAspectRatioMode(DWORD dwAspectRatioMode) { return E_NOTIMPL; };
	STDMETHODIMP GetAspectRatioMode(DWORD *pdwAspectRatioMode) { return E_NOTIMPL; };
	STDMETHODIMP SetVideoWindow(HWND hwndVideo) { return E_NOTIMPL; };
	STDMETHODIMP GetVideoWindow(HWND *phwndVideo);
	STDMETHODIMP RepaintVideo(void) { return E_NOTIMPL; };
	STDMETHODIMP GetCurrentImage(BITMAPINFOHEADER *pBih, BYTE **pDib, DWORD *pcbDib, LONGLONG *pTimeStamp) { return E_NOTIMPL; };
	STDMETHODIMP SetBorderColor(COLORREF Clr) { return E_NOTIMPL; };
	STDMETHODIMP GetBorderColor(COLORREF *pClr) { return E_NOTIMPL; };
	STDMETHODIMP SetRenderingPrefs(DWORD dwRenderFlags) { return E_NOTIMPL; };
	STDMETHODIMP GetRenderingPrefs(DWORD *pdwRenderFlags) { return E_NOTIMPL; };
	STDMETHODIMP SetFullscreen(BOOL fFullscreen) { return E_NOTIMPL; };
	STDMETHODIMP GetFullscreen(BOOL *pfFullscreen) { return E_NOTIMPL; };

private :
	HMODULE                               m_hDXVA2Lib;
	PTR_DXVA2CreateDirect3DDeviceManager9 pfDXVA2CreateDirect3DDeviceManager9;
	PTR_DXVA2CreateVideoService           pfDXVA2CreateVideoService;

	CComPtr<IDirect3D9>                   m_pD3D;
	CComPtr<IDirect3DDevice9>             m_pD3DDev;
	CComPtr<IDirect3DDeviceManager9>      m_pD3DDeviceManager;
	UINT                                  m_nResetTocken;
	HANDLE                                m_hDevice;
	HWND                                  m_hWnd;

	void CreateSurface();
};

CVideoRendererInputPin::CVideoRendererInputPin(CBaseRenderer *pRenderer, HRESULT *phr, LPCWSTR Name)
	: CRendererInputPin(pRenderer, phr, Name)
	, m_hDXVA2Lib(nullptr)
	, m_pD3DDev(nullptr)
	, m_pD3DDeviceManager(nullptr)
	, m_hDevice(INVALID_HANDLE_VALUE)
{
	CreateSurface();

	m_hDXVA2Lib = LoadLibraryW(L"dxva2.dll");
	if (m_hDXVA2Lib) {
		pfDXVA2CreateDirect3DDeviceManager9 = reinterpret_cast<PTR_DXVA2CreateDirect3DDeviceManager9>(GetProcAddress(m_hDXVA2Lib, "DXVA2CreateDirect3DDeviceManager9"));
		pfDXVA2CreateVideoService = reinterpret_cast<PTR_DXVA2CreateVideoService>(GetProcAddress(m_hDXVA2Lib, "DXVA2CreateVideoService"));
		pfDXVA2CreateDirect3DDeviceManager9(&m_nResetTocken, &m_pD3DDeviceManager);
	}

	// Initialize Device Manager with DX surface
	if (m_pD3DDev) {
		HRESULT hr;
		hr = m_pD3DDeviceManager->ResetDevice (m_pD3DDev, m_nResetTocken);
		hr = m_pD3DDeviceManager->OpenDeviceHandle(&m_hDevice);
	}
}

static IDirect3D9* Direct3DCreate9()
{
	typedef IDirect3D9* (WINAPI *tpDirect3DCreate9)(__in UINT SDKVersion);

	static HMODULE hModule = LoadLibraryW(L"d3d9.dll");
	static tpDirect3DCreate9 pDirect3DCreate9 = hModule ? (tpDirect3DCreate9)GetProcAddress(hModule, "Direct3DCreate9") : nullptr;
	if (pDirect3DCreate9) {
		IDirect3D9* pD3D9 = pDirect3DCreate9(D3D_SDK_VERSION);
		if (!pD3D9) {
			pD3D9 = pDirect3DCreate9(D3D9b_SDK_VERSION);
		}

		return pD3D9;
	}

	return nullptr;
}

void CVideoRendererInputPin::CreateSurface()
{
	m_pD3D.Attach(Direct3DCreate9());

	m_hWnd = nullptr;	// TODO : put true window

	D3DDISPLAYMODE d3ddm = {};
	m_pD3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &d3ddm);

	D3DPRESENT_PARAMETERS pp = {};

	pp.Windowed = TRUE;
	pp.hDeviceWindow = m_hWnd;
	pp.SwapEffect = D3DSWAPEFFECT_COPY;
	pp.Flags = D3DPRESENTFLAG_VIDEO;
	pp.BackBufferCount = 1;
	pp.BackBufferWidth = d3ddm.Width;
	pp.BackBufferHeight = d3ddm.Height;
	pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

	HRESULT hr = m_pD3D->CreateDevice(
					D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m_hWnd,
					D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED, //D3DCREATE_MANAGED
					&pp, &m_pD3DDev);

	UNREFERENCED_PARAMETER(hr);
}

STDMETHODIMP CVideoRendererInputPin::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	return
		(riid == __uuidof(IMFGetService)) ? GetInterface((IMFGetService*)this, ppv) :
		__super::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP CVideoRendererInputPin::GetService(REFGUID guidService, REFIID riid, LPVOID *ppvObject)
{
	if (m_pD3DDeviceManager != nullptr && guidService == MR_VIDEO_ACCELERATION_SERVICE) {
		if (riid == __uuidof(IDirect3DDeviceManager9)) {
			return m_pD3DDeviceManager->QueryInterface (riid, ppvObject);
		} else if (riid == __uuidof(IDirectXVideoDecoderService) || riid == __uuidof(IDirectXVideoProcessorService) ) {
			return m_pD3DDeviceManager->GetVideoService (m_hDevice, riid, ppvObject);
		} else if (riid == __uuidof(IDirectXVideoAccelerationService)) {
			// TODO : to be tested....
			return pfDXVA2CreateVideoService(m_pD3DDev, riid, ppvObject);
		} else if (riid == __uuidof(IDirectXVideoMemoryConfiguration)) {
			GetInterface ((IDirectXVideoMemoryConfiguration*)this, ppvObject);
			return S_OK;
		}
	} else if (guidService == MR_VIDEO_RENDER_SERVICE) {
		if (riid == __uuidof(IMFVideoDisplayControl)) {
			GetInterface ((IMFVideoDisplayControl*)this, ppvObject);
			return S_OK;
		}
	}
	//else if (guidService == MR_VIDEO_MIXER_SERVICE)
	//{
	//	if (riid == __uuidof(IMFVideoMixerBitmap))
	//	{
	//		GetInterface ((IMFVideoMixerBitmap*)this, ppvObject);
	//		return S_OK;
	//	}
	//}
	return E_NOINTERFACE;
}

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

STDMETHODIMP CVideoRendererInputPin::GetVideoWindow(HWND *phwndVideo)
{
	CheckPointer(phwndVideo, E_POINTER);
	*phwndVideo = m_hWnd;	// Important to implement this method (used by mpc)
	return S_OK;
}

//
// CMpcVideoRenderer
//

CMpcVideoRenderer::CMpcVideoRenderer(LPUNKNOWN pUnk, HRESULT* phr)
	: CBaseRenderer(__uuidof(this), NAME("MPC Video Renderer"), pUnk, phr)
	, m_hWnd(nullptr)
	, m_CurrentAdapter(D3DADAPTER_DEFAULT)
{
	ASSERT(S_OK == *phr);
	m_pInputPin = new CVideoRendererInputPin(this, phr, L"In");
	ASSERT(S_OK == *phr);

	m_hD3D9Lib = LoadLibraryW(L"d3d9.dll");
	if (!m_hD3D9Lib) {
		*phr = E_FAIL;
		return;
	}

	m_hDxva2Lib = LoadLibraryW(L"dxva2.dll");
	if (!m_hDxva2Lib) {
		*phr = E_FAIL;
		return;
	}

	BOOL ret = InitDirect3D9();
}

CMpcVideoRenderer::~CMpcVideoRenderer()
{
	if (m_hDxva2Lib) {
		FreeLibrary(m_hDxva2Lib);
	}
}

HRESULT CMpcVideoRenderer::SetMediaType(const CMediaType *pmt)
{
	HRESULT hr = __super::SetMediaType(pmt);

	if (S_OK == hr) {
		m_mt = *pmt;
	}

	return hr;
}

HRESULT CMpcVideoRenderer::CheckMediaType(const CMediaType* pmt)
{
	const bool validate_pmt = std::any_of(std::cbegin(sudPinTypesIn), std::cend(sudPinTypesIn), [&](const AMOVIESETUP_MEDIATYPE& type) {
		return pmt->majortype == *type.clsMajorType && pmt->subtype == *type.clsMinorType;
	});

	return validate_pmt ? S_OK : E_FAIL;
}

BOOL CMpcVideoRenderer::InitDirect3D9()
{
	if (!m_hD3D9Lib) {
		return FALSE;
	}

	HRESULT hr;

	HRESULT(__stdcall * pDirect3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex**);
	(FARPROC &)pDirect3DCreate9Ex = GetProcAddress(m_hD3D9Lib, "Direct3DCreate9Ex");

	hr = pDirect3DCreate9Ex(D3D_SDK_VERSION, &m_pD3DEx);
	if (!m_pD3DEx) {
		hr = pDirect3DCreate9Ex(D3D9b_SDK_VERSION, &m_pD3DEx);
	}
	if (!m_pD3DEx) {
		return FALSE;
	}

	hr = m_pD3DEx->GetAdapterDisplayMode(m_CurrentAdapter, &m_DisplayMode);

	D3DPRESENT_PARAMETERS pp = {};
	pp.Windowed = TRUE;
	pp.hDeviceWindow = m_hWnd;
	pp.SwapEffect = D3DSWAPEFFECT_COPY;
	pp.Flags = D3DPRESENTFLAG_VIDEO;
	pp.BackBufferCount = 1;
	pp.BackBufferWidth = m_DisplayMode.Width;
	pp.BackBufferHeight = m_DisplayMode.Height;
	pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

	hr = m_pD3DEx->CreateDeviceEx(
		m_CurrentAdapter, D3DDEVTYPE_HAL, m_hWnd,
		D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_ENABLE_PRESENTSTATS,
		&pp, nullptr, &m_pD3DDevEx);

	return (S_OK == hr);
}

BOOL CMpcVideoRenderer::InitializeDXVAHDVP(int width, int height)
{
	if (!m_hDxva2Lib) {
		return FALSE;
	}
	DLog("InitializeDXVAHDVP: start");

	HRESULT(WINAPI *pDXVAHD_CreateDevice)(IDirect3DDevice9Ex  *pD3DDevice, const DXVAHD_CONTENT_DESC *pContentDesc, DXVAHD_DEVICE_USAGE Usage, PDXVAHDSW_Plugin pPlugin, IDXVAHD_Device **ppDevice);
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
		NOTE1("InitializeDXVAHDVP: DXVAHD_CreateDevice() failed with error 0x%x.", hr);
		return FALSE;
	}

	// Get the DXVA-HD device caps.
	DXVAHD_VPDEVCAPS caps = {};
	hr = m_pDXVAHD_Device->GetVideoProcessorDeviceCaps(&caps);
	if (FAILED(hr) || caps.MaxInputStreams < 1) {
		return FALSE;
	}

	std::vector<D3DFORMAT> Formats;

	// Check the output formats.
	Formats.resize(caps.OutputFormatCount);
	hr = m_pDXVAHD_Device->GetVideoProcessorOutputFormats(caps.OutputFormatCount, Formats.data());
	if (FAILED(hr)) {
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
	Formats.resize(caps.InputFormatCount);
	hr = m_pDXVAHD_Device->GetVideoProcessorInputFormats(caps.InputFormatCount, Formats.data());
	if (FAILED(hr)) {
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
	VPCaps.resize(caps.VideoProcessorCount);

	hr = m_pDXVAHD_Device->GetVideoProcessorCaps(caps.VideoProcessorCount, VPCaps.data());
	if (FAILED(hr)) {
		return FALSE;
	}

	hr = m_pDXVAHD_Device->CreateVideoProcessor(&VPCaps[0].VPGuid, &m_pDXVAHD_VP);
	if (FAILED(hr)) {
		DLog(L"InitializeDXVAHDVP: CreateVideoProcessor() failed with error 0x%x.", hr);
		return FALSE;
	}

	// Set the initial stream states for the primary stream.
	hr = DXVAHD_SetStreamFormat(m_pDXVAHD_VP, 0, D3DFMT_X8R8G8B8);
	if (FAILED(hr)) {
		return FALSE;
	}

	hr = DXVAHD_SetFrameFormat(m_pDXVAHD_VP, 0, DXVAHD_FRAME_FORMAT_PROGRESSIVE);
	if (FAILED(hr)) {
		return FALSE;
	}

	return TRUE;
}

HRESULT CMpcVideoRenderer::DoRenderSample(IMediaSample* pSample)
{
	if (!m_pDXVAHD_VP) {
		BOOL ret = InitializeDXVAHDVP(640, 480);
	}

	return S_OK;
}
