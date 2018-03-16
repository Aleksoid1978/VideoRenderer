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
#include <evr.h> // for MR_VIDEO_ACCELERATION_SERVICE, because the <mfapi.h> does not contain it
#include <Mferror.h>
#include <Dvdmedia.h>
#include "Helper.h"
#include "PropPage.h"

#if D3D11_ENABLE
#include "./Include/ID3DVideoMemoryConfiguration.h"
#endif

class CVideoRendererInputPin : public CRendererInputPin,
	public IMFGetService,
	public IDirectXVideoMemoryConfiguration
#if D3D11_ENABLE
	, public ID3D11DecoderConfiguration
#endif
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

#if D3D11_ENABLE
	// ID3D11DecoderConfiguration
	STDMETHODIMP ActivateD3D11Decoding(ID3D11Device *pDevice, ID3D11DeviceContext *pContext, HANDLE hMutex, UINT nFlags);
	UINT STDMETHODCALLTYPE GetD3D11AdapterIndex();
#endif

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
#if D3D11_ENABLE
		(riid == __uuidof(ID3D11DecoderConfiguration)) ? GetInterface((ID3D11DecoderConfiguration*)this, ppv) :
#endif
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

#if D3D11_ENABLE
// ID3D11DecoderConfiguration
STDMETHODIMP CVideoRendererInputPin::ActivateD3D11Decoding(ID3D11Device *pDevice, ID3D11DeviceContext *pContext, HANDLE hMutex, UINT nFlags)
{
	return m_pBaseRenderer->m_D3D11_VP.SetDevice(pDevice, pContext);
}

UINT STDMETHODCALLTYPE CVideoRendererInputPin::GetD3D11AdapterIndex()
{
	return 0;
}
#endif

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

	HRESULT (__stdcall * pfnDirect3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex**);
	(FARPROC &)pfnDirect3DCreate9Ex = GetProcAddress(m_hD3D9Lib, "Direct3DCreate9Ex");

	HRESULT hr = pfnDirect3DCreate9Ex(D3D_SDK_VERSION, &m_pD3DEx);
	if (!m_pD3DEx) {
		hr = pfnDirect3DCreate9Ex(D3D9b_SDK_VERSION, &m_pD3DEx);
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

	(FARPROC &)pfnDXVA2CreateDirect3DDeviceManager9 = GetProcAddress(m_hDxva2Lib, "DXVA2CreateDirect3DDeviceManager9");
	(FARPROC &)pfnDXVA2CreateVideoService = GetProcAddress(m_hDxva2Lib, "DXVA2CreateVideoService");
	pfnDXVA2CreateDirect3DDeviceManager9(&m_nResetTocken, &m_pD3DDeviceManager);
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

	m_pDXVA2_VP.Release();
	m_pDXVA2_VPService.Release();

	if (m_hDxva2Lib) {
		FreeLibrary(m_hDxva2Lib);
	}

	m_pD3DDevEx.Release();
	m_pD3DEx.Release();
	if (m_hD3D9Lib) {
		FreeLibrary(m_hD3D9Lib);
	}
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

	m_SrcSamples.Clear();
	m_DXVA2Samples.clear();
	m_pDXVA2_VP.Release();
	m_pDXVA2_VPService.Release();

	const UINT currentAdapter = GetAdapter(m_hWnd, m_pD3DEx);
	bool bTryToReset = (currentAdapter == m_CurrentAdapter) && m_pD3DDevEx;
	if (!bTryToReset) {
		m_pD3DDevEx.Release();
		m_CurrentAdapter = currentAdapter;
	}

	D3DADAPTER_IDENTIFIER9 AdapID9 = {};
	if (S_OK == m_pD3DEx->GetAdapterIdentifier(m_CurrentAdapter, 0, &AdapID9)) {
		m_VendorId = AdapID9.VendorId;
		m_strAdapterDescription.Format(L"%S (%04X:%04X)", AdapID9.Description, AdapID9.VendorId, AdapID9.DeviceId);
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

BOOL CMpcVideoRenderer::InitVideoProc(const UINT width, const UINT height, const D3DFORMAT d3dformat)
{
	const bool bReinitialize = !m_pDXVA2_VP
		|| width != m_DXVA2_VP_Width
		|| height != m_DXVA2_VP_Height
		|| d3dformat!= m_DXVA2_VP_Format;
	if (bReinitialize && !InitializeDXVA2VP(width, height, d3dformat)) {
		return FALSE;
	}

	return TRUE;
}

BOOL CMpcVideoRenderer::InitializeDXVA2VP(const UINT width, const UINT height, const D3DFORMAT d3dformat)
{
	DLog("CMpcVideoRenderer::InitializeDXVA2VP()");
	if (!m_hDxva2Lib) {
		return FALSE;
	}

	m_SrcSamples.Clear();
	m_DXVA2Samples.clear();
	m_pDXVA2_VP.Release();

	HRESULT hr = S_OK;
	if (!m_pDXVA2_VPService) {
		HRESULT(WINAPI *pfnDXVA2CreateVideoService)(IDirect3DDevice9* pDD, REFIID riid, void** ppService);
		(FARPROC &)pfnDXVA2CreateVideoService = GetProcAddress(m_hDxva2Lib, "DXVA2CreateVideoService");
		if (!pfnDXVA2CreateVideoService) {
			DLog("CMpcVideoRenderer::InitializeDXVA2VP() : DXVA2CreateVideoService() not found");
			return FALSE;
		}

		// Create DXVA2 Video Processor Service.
		hr = pfnDXVA2CreateVideoService(m_pD3DDevEx, IID_IDirectXVideoProcessorService, (VOID**)&m_pDXVA2_VPService);
		if (FAILED(hr)) {
			DLog(L"CMpcVideoRenderer::InitializeDXVA2VP() : DXVA2CreateVideoService() failed with error 0x%08x", hr);
			return FALSE;
		}
	}

	DLog(L"CMpcVideoRenderer::InitializeDXVA2VP() : Input surface: %s, %u x %u", D3DFormatToString(d3dformat), width, height);

	// Initialize the video descriptor.
	DXVA2_VideoDesc videodesc = {};
	videodesc.SampleWidth = width;
	videodesc.SampleHeight = height;
	//videodesc.SampleFormat.value = 0; // do not need to fill it here
	videodesc.SampleFormat.SampleFormat = m_bInterlaced ? DXVA2_SampleFieldInterleavedOddFirst : DXVA2_SampleProgressiveFrame;
	if (d3dformat == D3DFMT_X8R8G8B8) {
		videodesc.Format = D3DFMT_YUY2; // hack
	} else {
		videodesc.Format = d3dformat;
	}
	videodesc.InputSampleFreq.Numerator = 60;
	videodesc.InputSampleFreq.Denominator = 1;
	videodesc.OutputFrameFreq.Numerator = 60;
	videodesc.OutputFrameFreq.Denominator = 1;

	// Query the video processor GUID.
	UINT count;
	GUID* guids = NULL;
	hr = m_pDXVA2_VPService->GetVideoProcessorDeviceGuids(&videodesc, &count, &guids);
	if (FAILED(hr)) {
		DLog(L"CMpcVideoRenderer::InitializeDXVA2VP() : GetVideoProcessorDeviceGuids() failed with error 0x%08x", hr);
		return FALSE;
	}
	UINT NumRefSamples = 1;
	if (m_bInterlaced) {
		UINT PreferredDeintTech = DXVA2_DeinterlaceTech_EdgeFiltering // Intel
								| DXVA2_DeinterlaceTech_FieldAdaptive
								| DXVA2_DeinterlaceTech_PixelAdaptive // Nvidia, AMD
								| DXVA2_DeinterlaceTech_MotionVectorSteered;

		for (UINT i = 0; i < count; i++) {
			auto& devguid = guids[i];
			if (CreateDXVA2VPDevice(devguid, videodesc) && m_DXVA2VPcaps.DeinterlaceTechnology & PreferredDeintTech) {
				m_DXVA2VPGuid = devguid;
				break; // found!
			}
			m_pDXVA2_VP.Release();
		}

		if (!m_pDXVA2_VP && CreateDXVA2VPDevice(DXVA2_VideoProcBobDevice, videodesc)) {
			m_DXVA2VPGuid = DXVA2_VideoProcBobDevice;
		}
	}

	CoTaskMemFree(guids);

	if (!m_pDXVA2_VP && CreateDXVA2VPDevice(DXVA2_VideoProcProgressiveDevice, videodesc)) { // Progressive or fall-back for interlaced
		m_DXVA2VPGuid = DXVA2_VideoProcProgressiveDevice;
	}

	if (!m_pDXVA2_VP) {
		return FALSE;
	}

	NumRefSamples = 1 + m_DXVA2VPcaps.NumBackwardRefSamples + m_DXVA2VPcaps.NumForwardRefSamples;
	ASSERT(NumRefSamples <= MAX_DEINTERLACE_SURFACES);

	m_SrcSamples.Resize(NumRefSamples);
	m_DXVA2Samples.resize(NumRefSamples);

	for (unsigned i = 0; i < NumRefSamples; ++i) {
		hr = m_pDXVA2_VPService->CreateSurface(
			width,
			height,
			0,
			d3dformat,
			m_DXVA2VPcaps.InputPool,
			0,
			DXVA2_VideoProcessorRenderTarget,
			&m_SrcSamples.GetAt(i).pSrcSurface,
			nullptr
		);
		if (FAILED(hr)) {
			m_SrcSamples.Clear();
			m_DXVA2Samples.clear();
			return FALSE;
		}

		if (m_VendorId == PCIV_AMDATI) {
			// fix AMD driver bug, fill the surface in black
			m_pD3DDevEx->ColorFill(m_SrcSamples.GetAt(i).pSrcSurface, nullptr, D3DCOLOR_XYUV(0, 128, 128));
		}

		m_DXVA2Samples[i].SampleFormat.value = m_srcExFmt.value;
		m_DXVA2Samples[i].SampleFormat.SampleFormat = DXVA2_SampleUnknown; // samples that are not used yet
		m_DXVA2Samples[i].SrcRect = {0, 0, m_nativeVideoRect.Width(), m_nativeVideoRect.Height()};
		m_DXVA2Samples[i].PlanarAlpha = DXVA2_Fixed32OpaqueAlpha();
	}

	m_DXVA2_VP_Format = d3dformat;
	m_DXVA2_VP_Width = width;
	m_DXVA2_VP_Height = height;

	return TRUE;
}

BOOL CMpcVideoRenderer::CreateDXVA2VPDevice(const GUID devguid, const DXVA2_VideoDesc& videodesc)
{
	if (!m_pDXVA2_VPService) {
		return FALSE;
	}

	HRESULT hr = S_OK;
	// Query the supported render target format.
	UINT i, count;
	D3DFORMAT* formats = nullptr;
	hr = m_pDXVA2_VPService->GetVideoProcessorRenderTargets(devguid, &videodesc, &count, &formats);
	if (FAILED(hr)) {
		DLog(L"CMpcVideoRenderer::InitializeDXVA2VP() : GetVideoProcessorRenderTargets() failed with error 0x%08x", hr);
		return FALSE;
	}
#if _DEBUG
	{
		CStringW dbgstr = L"DXVA2-VP output formats:";
		for (UINT j = 0; j < count; j++) {
			dbgstr.AppendFormat(L"\n%s", D3DFormatToString(formats[j]));
		}
		DLog(dbgstr);
	}
#endif
	for (i = 0; i < count; i++) {
		if (formats[i] == D3DFMT_X8R8G8B8) {
			break;
		}
	}
	CoTaskMemFree(formats);
	if (i >= count) {
		DLog(L"CMpcVideoRenderer::InitializeDXVA2VP() : GetVideoProcessorRenderTargets() doesn't support D3DFMT_X8R8G8B8");
		return FALSE;
	}

	// Query video processor capabilities.
	hr = m_pDXVA2_VPService->GetVideoProcessorCaps(devguid, &videodesc, D3DFMT_X8R8G8B8, &m_DXVA2VPcaps);
	if (FAILED(hr)) {
		DLog(L"CMpcVideoRenderer::InitializeDXVA2VP() : GetVideoProcessorCaps() failed with error 0x%08x", hr);
		return FALSE;
	}
	// Check to see if the device is hardware device.
	if (!(m_DXVA2VPcaps.DeviceCaps & DXVA2_VPDev_HardwareDevice)) {
		DLog(L"CMpcVideoRenderer::InitializeDXVA2VP() : The DXVA2 device isn't a hardware device");
		return FALSE;
	}
	// Check to see if the device supports all the VP operations we want.
	const UINT VIDEO_REQUIED_OP = DXVA2_VideoProcess_YUV2RGB | DXVA2_VideoProcess_StretchX | DXVA2_VideoProcess_StretchY;
	if ((m_DXVA2VPcaps.VideoProcessorOperations & VIDEO_REQUIED_OP) != VIDEO_REQUIED_OP) {
		DLog(L"CMpcVideoRenderer::InitializeDXVA2VP() : The DXVA2 device doesn't support the YUV2RGB & VP operations");
		return FALSE;
	}

	// Query ProcAmp ranges.
	DXVA2_ValueRange range;
	for (i = 0; i < ARRAYSIZE(m_DXVA2ProcAmpValues); i++) {
		if (m_DXVA2VPcaps.ProcAmpControlCaps & (1 << i)) {
			hr = m_pDXVA2_VPService->GetProcAmpRange(devguid, &videodesc, D3DFMT_X8R8G8B8, 1 << i, &range);
			if (FAILED(hr)) {
				DLog(L"CMpcVideoRenderer::InitializeDXVA2VP() : GetProcAmpRange() failed with error 0x%08x", hr);
				return FALSE;
			}
			// Set to default value
			m_DXVA2ProcAmpValues[i] = range.DefaultValue;
		}
	}

	// Finally create a video processor device.
	hr = m_pDXVA2_VPService->CreateVideoProcessor(devguid, &videodesc, D3DFMT_X8R8G8B8, 0, &m_pDXVA2_VP);
	if (FAILED(hr)) {
		DLog(L"CMpcVideoRenderer::InitializeDXVA2VP() : CreateVideoProcessor failed with error 0x%08x", hr);
		return FALSE;
	}

	DLog(L"CMpcVideoRenderer::InitializeDXVA2VP() : create %s processor ", CStringFromGUID(devguid));

	return TRUE;
}

HRESULT CMpcVideoRenderer::CopySample(IMediaSample* pSample)
{
	HRESULT hr = S_OK;

	if (CComQIPtr<IMFGetService> pService = pSample) {
		CComPtr<IDirect3DSurface9> pSurface;
		if (SUCCEEDED(pService->GetService(MR_BUFFER_SERVICE, IID_PPV_ARGS(&pSurface)))) {
			D3DSURFACE_DESC desc;
			hr = pSurface->GetDesc(&desc);
			if (FAILED(hr)) {
				return hr;
			}
			if (!InitVideoProc(desc.Width, desc.Height, desc.Format)) {
				return E_FAIL;
			}

			m_SrcSamples.Next();
			hr = m_pD3DDevEx->StretchRect(pSurface, nullptr, m_SrcSamples.Get().pSrcSurface, nullptr, D3DTEXF_NONE);
			if (FAILED(hr)) {
				// sometimes StretchRect does not work on non-primary display on Intel GPU
				D3DLOCKED_RECT lr_dst;
				hr = m_SrcSamples.Get().pSrcSurface->LockRect(&lr_dst, nullptr, D3DLOCK_NOSYSLOCK);
				if (S_OK == hr) {
					D3DLOCKED_RECT lr_src;
					hr = pSurface->LockRect(&lr_src, nullptr, D3DLOCK_READONLY);
					if (S_OK == hr) {
						memcpy((BYTE*)lr_dst.pBits, (BYTE*)lr_src.pBits, lr_src.Pitch * desc.Height * 3 / 2);
						hr = pSurface->UnlockRect();
					}
					hr = m_SrcSamples.Get().pSrcSurface->UnlockRect();
				}
			}
		}
	}
	else if (m_mt.formattype == FORMAT_VideoInfo2) {
		BYTE* data = nullptr;
		const long size = pSample->GetActualDataLength();
		if (size > 0 && S_OK == pSample->GetPointer(&data)) {
			if (!InitVideoProc(m_srcWidth, m_srcHeight, m_srcD3DFormat)) {
				return E_FAIL;
			}

			m_SrcSamples.Next();
			D3DLOCKED_RECT lr;
			hr = m_SrcSamples.Get().pSrcSurface->LockRect(&lr, nullptr, D3DLOCK_NOSYSLOCK);
			if (FAILED(hr)) {
				return hr;
			}

			CopyFrameData(m_srcD3DFormat, m_srcWidth, m_srcHeight, (BYTE*)lr.pBits, lr.Pitch, data, m_srcPitch, size);

			hr = m_SrcSamples.Get().pSrcSurface->UnlockRect();
		}
	}

	const REFERENCE_TIME start_100ns = m_frame * 170000i64;
	const REFERENCE_TIME end_100ns = start_100ns + 170000i64;
	m_SrcSamples.Get().Start = start_100ns;
	m_SrcSamples.Get().End = end_100ns;
	m_SrcSamples.Get().SampleFormat = m_SampleFormat;

	m_frame++;

	return hr;
}

HRESULT CMpcVideoRenderer::Render()
{
	if (m_SrcSamples.Empty()) return E_POINTER;

	HRESULT hr = m_pD3DDevEx->BeginScene();

	CComPtr<IDirect3DSurface9> pBackBuffer;
	hr = m_pD3DDevEx->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);

	hr = m_pD3DDevEx->SetRenderTarget(0, pBackBuffer);
	m_pD3DDevEx->ColorFill(pBackBuffer, nullptr, 0);

	if (m_filterState == State_Running) {
		hr = ProcessDXVA2(pBackBuffer);
	}

	hr = m_pD3DDevEx->EndScene();

	const CRect rSrcPri(CPoint(0, 0), m_windowRect.Size());
	const CRect rDstPri(m_windowRect);

	hr = m_pD3DDevEx->PresentEx(rSrcPri, rDstPri, nullptr, nullptr, 0);

	return hr;
}

HRESULT CMpcVideoRenderer::ProcessDXVA2(IDirect3DSurface9* pRenderTarget)
{
	// https://msdn.microsoft.com/en-us/library/cc307964(v=vs.85).aspx

	HRESULT hr = S_OK;
	ASSERT(m_SrcSamples.Size() == m_DXVA2Samples.size());

	const CRect rDstVid(m_videoRect);

	// Initialize VPBlt parameters.
	DXVA2_VideoProcessBltParams blt = {};
	blt.TargetFrame = m_SrcSamples.Get().Start; // Hmm
	blt.TargetRect = rDstVid;
	// DXVA2_VideoProcess_Constriction
	blt.ConstrictionSize.cx = rDstVid.Width();
	blt.ConstrictionSize.cy = rDstVid.Height();
	blt.BackgroundColor = { 128 * 0x100, 128 * 0x100, 16 * 0x100, 0xFFFF }; // black
	// DXVA2_VideoProcess_YUV2RGBExtended
	//blt.DestFormat.value = 0; // output to RGB
	blt.DestFormat.SampleFormat = DXVA2_SampleProgressiveFrame; // output to progressive RGB
	// DXVA2_ProcAmp_Brightness/Contrast/Hue/Saturation
	blt.ProcAmpValues.Brightness = m_DXVA2ProcAmpValues[0];
	blt.ProcAmpValues.Contrast   = m_DXVA2ProcAmpValues[1];
	blt.ProcAmpValues.Hue        = m_DXVA2ProcAmpValues[2];
	blt.ProcAmpValues.Saturation = m_DXVA2ProcAmpValues[3];
	// DXVA2_VideoProcess_AlphaBlend
	blt.Alpha = DXVA2_Fixed32OpaqueAlpha();

	// Initialize main stream video samples
	for (unsigned i = 0; i < m_DXVA2Samples.size(); i++) {
		auto & SrcSample = m_SrcSamples.GetAt(i);

		m_DXVA2Samples[i].Start = SrcSample.Start;
		m_DXVA2Samples[i].End = SrcSample.End;
		m_DXVA2Samples[i].SampleFormat.SampleFormat = SrcSample.SampleFormat;
		m_DXVA2Samples[i].SrcSurface = SrcSample.pSrcSurface;
		m_DXVA2Samples[i].DstRect = rDstVid;
	}

#ifdef _DEBUG
	if (m_frame < 5) {
		CStringW dbgstr = L"DXVA2Samples:";
		for (unsigned i = 0; i < m_DXVA2Samples.size(); i++) {
			auto& sample = m_DXVA2Samples[i];
			dbgstr.AppendFormat(L"\n%u: samplefmt = %u, sampledata = %u, surface = %s, srcrect = [%d, %d, %d, %d], dstrect = [%d, %d, %d, %d],  start = %I64d, end = %I64d",
				i, sample.SampleFormat.SampleFormat, sample.SampleData, sample.SrcSurface ? L"ok" : L"invalid",
				sample.SrcRect.left, sample.SrcRect.top, sample.SrcRect.right, sample.SrcRect.bottom,
				sample.DstRect.left, sample.DstRect.top, sample.DstRect.right, sample.DstRect.bottom,
				sample.Start, sample.End);
		}
		DLog(dbgstr);
	}
#endif

	hr = m_pDXVA2_VP->VideoProcessBlt(pRenderTarget, &blt, m_DXVA2Samples.data(), m_DXVA2Samples.size(), nullptr);
	if (FAILED(hr)) {
		DLog(L"CMpcVideoRenderer::ProcessDXVA2() : VideoProcessBlt() failed with error 0x%08x", hr);
	}

	return hr;
}

BOOL CMpcVideoRenderer::InitMediaType(const CMediaType* pmt)
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

#if (!D3D11_ENABLE)
		m_srcD3DFormat = MediaSubtype2D3DFormat(pmt->subtype);
		m_srcExFmt.value = 0;
		m_bInterlaced = (vih2->dwInterlaceFlags & AMINTERLACE_IsInterlaced);

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

		if (!InitVideoProc(m_srcWidth, m_srcHeight, m_srcD3DFormat)) {
			return FALSE;
		}
#endif

		return TRUE;
	}

	return FALSE;
}

// CBaseRenderer

HRESULT CMpcVideoRenderer::CheckMediaType(const CMediaType* pmt)
{
	CheckPointer(pmt, E_POINTER);
	CheckPointer(pmt->pbFormat, E_POINTER);

	if (pmt->majortype == MEDIATYPE_Video && pmt->formattype == FORMAT_VideoInfo2) {
		for (unsigned i = 0; i < _countof(sudPinTypesIn); i++) {
			if (pmt->subtype == *sudPinTypesIn[i].clsMinorType) {
				std::unique_lock<std::mutex> lock(m_mutex);

				if (!InitMediaType(pmt)) {
					return VFW_E_UNSUPPORTED_VIDEO;
				}
#if D3D11_ENABLE
				if (!m_D3D11_VP.InitMediaType(pmt)) {
					return VFW_E_UNSUPPORTED_VIDEO;
				}
#endif
				return S_OK;
			}
		}
	}

	return E_FAIL;
}

HRESULT CMpcVideoRenderer::SetMediaType(const CMediaType *pmt)
{
	CheckPointer(pmt, E_POINTER);
	CheckPointer(pmt->pbFormat, E_POINTER);

	HRESULT hr = __super::SetMediaType(pmt);
	if (S_OK == hr) {
		std::unique_lock<std::mutex> lock(m_mutex);

		if (!InitMediaType(pmt)) {
			return VFW_E_UNSUPPORTED_VIDEO;
		}
#if D3D11_ENABLE
		if (!m_D3D11_VP.InitMediaType(pmt)) {
			return VFW_E_UNSUPPORTED_VIDEO;
		}
#endif
	}

	return hr;
}

HRESULT CMpcVideoRenderer::DoRenderSample(IMediaSample* pSample)
{
	CheckPointer(pSample, E_POINTER);
	std::unique_lock<std::mutex> lock(m_mutex);

	HRESULT hr = S_OK;
#if D3D11_ENABLE
	hr = m_D3D11_VP.CopySample(pSample);
#else
	// Get frame type
	m_SampleFormat = DXVA2_SampleProgressiveFrame; // Progressive
	if (m_bInterlaced) {
		if (CComQIPtr<IMediaSample2> pMS2 = pSample) {
			AM_SAMPLE2_PROPERTIES props;
			if (SUCCEEDED(pMS2->GetProperties(sizeof(props), (BYTE*)&props))) {
				m_SampleFormat = DXVA2_SampleFieldInterleavedOddFirst;      // Bottom-field first
				if (props.dwTypeSpecificFlags & AM_VIDEO_FLAG_WEAVE) {
					m_SampleFormat = DXVA2_SampleProgressiveFrame;          // Progressive
				} else if (props.dwTypeSpecificFlags & AM_VIDEO_FLAG_FIELD1FIRST) {
					m_SampleFormat = DXVA2_SampleFieldInterleavedEvenFirst; // Top-field first
				}
			}
		}
	}

	hr = CopySample(pSample);
#endif
	if (FAILED(hr)) {
		return hr;
	}

#if D3D11_ENABLE
	return m_D3D11_VP.Render(m_filterState);
#else
	return Render();
#endif
}

STDMETHODIMP CMpcVideoRenderer::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	HRESULT hr;
	if (riid == __uuidof(IMFGetService)) {
		hr = GetInterface((IMFGetService*)this, ppv);
	}
	else if (riid == __uuidof(IBasicVideo)) {
		hr = GetInterface((IBasicVideo*)this, ppv);
	}
	else if (riid == __uuidof(IBasicVideo2)) {
		hr = GetInterface((IBasicVideo2*)this, ppv);
	}
	else if (riid == __uuidof(IVideoWindow)) {
		hr = GetInterface((IVideoWindow*)this, ppv);
	}
	else if (riid == __uuidof(ISpecifyPropertyPages)) {
		hr = GetInterface((ISpecifyPropertyPages*)this, ppv);
	}
	else if (riid == __uuidof(IVideoRenderer)) {
		hr = GetInterface((IVideoRenderer*)this, ppv);
	}
	else {
		hr = __super::NonDelegatingQueryInterface(riid, ppv);
	}

	return hr;
}

// IMediaFilter
STDMETHODIMP CMpcVideoRenderer::Run(REFERENCE_TIME rtStart)
{
	DLog(L"CMpcVideoRenderer::Run()");

	if (m_State == State_Running) {
		return NOERROR;
	}

	m_filterState = State_Running;

	return CBaseRenderer::Run(rtStart);
}

STDMETHODIMP CMpcVideoRenderer::Stop()
{
	DLog(L"CMpcVideoRenderer::Stop()");

#if (!D3D11_ENABLE)
	if (m_VendorId == PCIV_AMDATI) {
		// fix AMD driver bug, fill the surface in black
		for (unsigned i = 0; i < m_SrcSamples.Size(); i++) {
			m_pD3DDevEx->ColorFill(m_SrcSamples.GetAt(i).pSrcSurface, nullptr, D3DCOLOR_XYUV(0, 128, 128));
		}
	}
#endif

	m_filterState = State_Stopped;

	return CBaseRenderer::Stop();
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
	}

	return E_NOINTERFACE;
}

// IBasicVideo
STDMETHODIMP CMpcVideoRenderer::SetDestinationPosition(long Left, long Top, long Width, long Height)
{
	m_videoRect.SetRect(Left, Top, Left + Width, Top + Height);
#if D3D11_ENABLE
	m_D3D11_VP.SetVideoRect(m_videoRect);
#endif

	std::unique_lock<std::mutex> lock(m_mutex);
#if D3D11_ENABLE
	m_D3D11_VP.Render(m_filterState);
#else
	Render();
#endif

	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::GetVideoSize(long *pWidth, long *pHeight)
{
	CheckPointer(pWidth, E_POINTER);
	CheckPointer(pHeight, E_POINTER);

	*pWidth = m_srcWidth;
	*pHeight = m_srcHeight;

	return S_OK;
}

// IBasicVideo2
STDMETHODIMP CMpcVideoRenderer::GetPreferredAspectRatio(long *plAspectX, long *plAspectY)
{
	CheckPointer(plAspectX, E_POINTER);
	CheckPointer(plAspectY, E_POINTER);

	*plAspectX = m_srcAspectRatioX;
	*plAspectY = m_srcAspectRatioY;

	return S_OK;
}

// IVideoWindow
STDMETHODIMP CMpcVideoRenderer::put_Owner(OAHWND Owner)
{
	if (m_hWnd != (HWND)Owner) {
		m_hWnd = (HWND)Owner;
		HRESULT hr = InitDirect3D9();
#if D3D11_ENABLE
		hr = m_D3D11_VP.InitSwapChain(m_hWnd, m_windowRect.Width(), m_windowRect.Height(), true);
#endif
		return hr;
	}
	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::SetWindowPosition(long Left, long Top, long Width, long Height)
{
	m_windowRect.SetRect(Left, Top, Left + Width, Top + Height);
#if D3D11_ENABLE
	m_D3D11_VP.InitSwapChain(m_hWnd, m_windowRect.Width(), m_windowRect.Height());
	m_D3D11_VP.SetWindowRect(m_windowRect);
#endif

	std::unique_lock<std::mutex> lock(m_mutex);
#if D3D11_ENABLE
	m_D3D11_VP.Render(m_filterState);
#else
	Render();
#endif

	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::get_Owner(OAHWND *Owner)
{
	CheckPointer(Owner, E_POINTER);
	*Owner = (OAHWND)m_hWnd;
	return S_OK;
}

// ISpecifyPropertyPages
STDMETHODIMP CMpcVideoRenderer::GetPages(CAUUID* pPages)
{
	CheckPointer(pPages, E_POINTER);

	pPages->cElems = 1;
	pPages->pElems = reinterpret_cast<GUID*>(CoTaskMemAlloc(sizeof(GUID)));
	if (pPages->pElems == nullptr) {
		return E_OUTOFMEMORY;
	}

	pPages->pElems[0] = __uuidof(CVRMainPPage);

	return S_OK;
}

// IVideoRenderer
STDMETHODIMP CMpcVideoRenderer::get_String(int id, LPWSTR* pstr, int* chars)
{
	CheckPointer(pstr, E_POINTER);
	CheckPointer(chars, E_POINTER);

	LPWSTR string = nullptr;
	int len = 0;

	switch (id) {
	case ID_AdapterDesc:
		string = m_strAdapterDescription.GetBuffer();
		len = m_strAdapterDescription.GetLength();
		break;
	default:
		return E_INVALIDARG;
	}

	const size_t sz = (len + 1) * sizeof(WCHAR);
	LPWSTR buf = (LPWSTR)LocalAlloc(LPTR, sz);
	if (!buf) {
		return E_OUTOFMEMORY;
	}
	wcscpy_s(buf, len + 1, string);
	*chars = len;
	*pstr = buf;

	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::get_Binary(int id, LPVOID* pbin, int* size)
{
	CheckPointer(pbin, E_POINTER);
	CheckPointer(size, E_POINTER);

	LPVOID binary = nullptr;
	size_t sz = 0;

	switch (id) {
	case ID_DXVA2VPCaps:
		binary = &m_DXVA2VPcaps;
		sz = sizeof(m_DXVA2VPcaps);
		break;
	default:
		return E_INVALIDARG;
	}

	LPVOID buf = (LPVOID)LocalAlloc(LPTR, sz);
	if (!buf) {
		return E_OUTOFMEMORY;
	}
	memcpy(buf, binary, sz);
	*size = sz;
	*pbin = buf;

	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::get_FrameInfo(VRFrameInfo* pFrameInfo)
{
#if D3D11_ENABLE
	return m_D3D11_VP.GetFrameInfo(pFrameInfo);
#else
	CheckPointer(pFrameInfo, E_POINTER);

	pFrameInfo->Width = m_srcWidth;
	pFrameInfo->Height = m_srcHeight;
	pFrameInfo->D3dFormat = m_srcD3DFormat;
	pFrameInfo->ExtFormat.value = m_srcExFmt.value;

	return S_OK;
#endif
}

STDMETHODIMP CMpcVideoRenderer::get_VPDeviceGuid(GUID* pVPDevGuid)
{
	CheckPointer(pVPDevGuid, E_POINTER);

	*pVPDevGuid = m_DXVA2VPGuid;

	return S_OK;
}
