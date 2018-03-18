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

#include <vector>
#include <d3d9.h>
#include <mfapi.h> // for MR_BUFFER_SERVICE
#include <mfidl.h>
#include "Helper.h"
#include "DX9VideoProcessor.h"

// CDX9VideoProcessor

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

CDX9VideoProcessor::CDX9VideoProcessor()
{
}

CDX9VideoProcessor::~CDX9VideoProcessor()
{
	if (m_pD3DDeviceManager) {
		if (m_hDevice != INVALID_HANDLE_VALUE) {
			m_pD3DDeviceManager->CloseDeviceHandle(m_hDevice);
			m_hDevice = INVALID_HANDLE_VALUE;
		}
		m_pD3DDeviceManager.Release();
	}

	ClearDX9();

	if (m_hDxva2Lib) {
		FreeLibrary(m_hDxva2Lib);
	}
	if (m_hD3D9Lib) {
		FreeLibrary(m_hD3D9Lib);
	}
}

HRESULT CDX9VideoProcessor::Init(HWND hwnd)
{
	if (!m_hD3D9Lib) {
		m_hD3D9Lib = LoadLibraryW(L"d3d9.dll");
	}
	if (!m_hDxva2Lib) {
		m_hDxva2Lib = LoadLibraryW(L"dxva2.dll");
	}
	if (!m_hD3D9Lib || !m_hDxva2Lib) {
		return E_FAIL;
	}

	HRESULT(WINAPI *pfnDirect3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex**);
	(FARPROC &)pfnDirect3DCreate9Ex = GetProcAddress(m_hD3D9Lib, "Direct3DCreate9Ex");

	HRESULT hr = pfnDirect3DCreate9Ex(D3D_SDK_VERSION, &m_pD3DEx);
	if (!m_pD3DEx) {
		hr = pfnDirect3DCreate9Ex(D3D9b_SDK_VERSION, &m_pD3DEx);
	}
	if (!m_pD3DEx) {
		return E_FAIL;
	}

	HRESULT(WINAPI *pfnDXVA2CreateDirect3DDeviceManager9)(UINT* pResetToken, IDirect3DDeviceManager9** ppDeviceManager);
	(FARPROC &)pfnDXVA2CreateDirect3DDeviceManager9 = GetProcAddress(m_hDxva2Lib, "DXVA2CreateDirect3DDeviceManager9");
	pfnDXVA2CreateDirect3DDeviceManager9(&m_nResetTocken, &m_pD3DDeviceManager);
	if (!m_pD3DDeviceManager) {
		return E_FAIL;
	}

	m_hWnd = hwnd;

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
	hr = m_pD3DEx->GetAdapterDisplayModeEx(m_CurrentAdapter, &m_DisplayMode, nullptr);

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

	if (S_OK == hr) {
		hr = m_pD3DDeviceManager->ResetDevice(m_pD3DDevEx, m_nResetTocken);
	}
	if (S_OK == hr) {
		hr = m_pD3DDeviceManager->OpenDeviceHandle(&m_hDevice);
	}

	return S_OK;
}

void CDX9VideoProcessor::ClearDX9()
{
	m_pDXVA2_VP.Release();
	m_pDXVA2_VPService.Release();
	m_pD3DDevEx.Release();
	m_pD3DEx.Release();
}

BOOL CDX9VideoProcessor::CheckInput(const D3DFORMAT d3dformat, const UINT width, const UINT height)
{
	if (!m_pDXVA2_VP || d3dformat != m_srcD3DFormat || width != m_srcWidth || height != m_srcHeight) {

		return InitializeDXVA2VP(d3dformat, width, height);
	}

	return TRUE;
}

BOOL CDX9VideoProcessor::InitializeDXVA2VP(const D3DFORMAT d3dformat, const UINT width, const UINT height)
{
	DLog("CDX9VideoProcessor::InitializeDXVA2VP: begin");
	if (!m_hDxva2Lib) {
		return FALSE;
	}

	m_SrcSamples.Clear();
	m_DXVA2Samples.clear();
	m_pDXVA2_VP.Release();

	m_srcD3DFormat = d3dformat;
	m_srcWidth = width;
	m_srcHeight = height;

	HRESULT hr = S_OK;
	if (!m_pDXVA2_VPService) {
		HRESULT(WINAPI *pfnDXVA2CreateVideoService)(IDirect3DDevice9* pDD, REFIID riid, void** ppService);
		(FARPROC &)pfnDXVA2CreateVideoService = GetProcAddress(m_hDxva2Lib, "DXVA2CreateVideoService");
		if (!pfnDXVA2CreateVideoService) {
			DLog("CDX9VideoProcessor::InitializeDXVA2VP : DXVA2CreateVideoService() not found");
			return FALSE;
		}

		// Create DXVA2 Video Processor Service.
		hr = pfnDXVA2CreateVideoService(m_pD3DDevEx, IID_IDirectXVideoProcessorService, (VOID**)&m_pDXVA2_VPService);
		if (FAILED(hr)) {
			DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : DXVA2CreateVideoService() failed with error 0x%08x", hr);
			return FALSE;
		}
	}

	DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : Input surface: %s, %u x %u", D3DFormatToString(d3dformat), width, height);

	// Initialize the video descriptor.
	DXVA2_VideoDesc videodesc = {};
	videodesc.SampleWidth = width;
	videodesc.SampleHeight = height;
	//videodesc.SampleFormat.value = 0; // do not need to fill it here
	videodesc.SampleFormat.SampleFormat = m_bInterlaced ? DXVA2_SampleFieldInterleavedOddFirst : DXVA2_SampleProgressiveFrame;
	if (d3dformat == D3DFMT_X8R8G8B8) {
		videodesc.Format = D3DFMT_YUY2; // hack
	}
	else {
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
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : GetVideoProcessorDeviceGuids() failed with error 0x%08x", hr);
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
		m_DXVA2Samples[i].SrcRect = { 0, 0, (LONG)width, (LONG)height };
		m_DXVA2Samples[i].PlanarAlpha = DXVA2_Fixed32OpaqueAlpha();
	}

	return TRUE;
}

BOOL CDX9VideoProcessor::CreateDXVA2VPDevice(const GUID devguid, const DXVA2_VideoDesc& videodesc)
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
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : GetVideoProcessorRenderTargets() failed with error 0x%08x", hr);
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
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : GetVideoProcessorRenderTargets() doesn't support D3DFMT_X8R8G8B8");
		return FALSE;
	}

	// Query video processor capabilities.
	hr = m_pDXVA2_VPService->GetVideoProcessorCaps(devguid, &videodesc, D3DFMT_X8R8G8B8, &m_DXVA2VPcaps);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : GetVideoProcessorCaps() failed with error 0x%08x", hr);
		return FALSE;
	}
	// Check to see if the device is hardware device.
	if (!(m_DXVA2VPcaps.DeviceCaps & DXVA2_VPDev_HardwareDevice)) {
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : The DXVA2 device isn't a hardware device");
		return FALSE;
	}
	// Check to see if the device supports all the VP operations we want.
	const UINT VIDEO_REQUIED_OP = DXVA2_VideoProcess_YUV2RGB | DXVA2_VideoProcess_StretchX | DXVA2_VideoProcess_StretchY;
	if ((m_DXVA2VPcaps.VideoProcessorOperations & VIDEO_REQUIED_OP) != VIDEO_REQUIED_OP) {
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : The DXVA2 device doesn't support the YUV2RGB & VP operations");
		return FALSE;
	}

	// Query ProcAmp ranges.
	DXVA2_ValueRange range;
	for (i = 0; i < ARRAYSIZE(m_DXVA2ProcAmpValues); i++) {
		if (m_DXVA2VPcaps.ProcAmpControlCaps & (1 << i)) {
			hr = m_pDXVA2_VPService->GetProcAmpRange(devguid, &videodesc, D3DFMT_X8R8G8B8, 1 << i, &range);
			if (FAILED(hr)) {
				DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : GetProcAmpRange() failed with error 0x%08x", hr);
				return FALSE;
			}
			// Set to default value
			m_DXVA2ProcAmpValues[i] = range.DefaultValue;
		}
	}

	// Finally create a video processor device.
	hr = m_pDXVA2_VPService->CreateVideoProcessor(devguid, &videodesc, D3DFMT_X8R8G8B8, 0, &m_pDXVA2_VP);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : CreateVideoProcessor failed with error 0x%08x", hr);
		return FALSE;
	}

	DLog(L"CDX9VideoProcessor::InitializeDXVA2VP : create %s processor ", CStringFromGUID(devguid));

	return TRUE;
}

HRESULT CDX9VideoProcessor::CopySample(IMediaSample* pSample)
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
			if (!CheckInput(desc.Format, desc.Width, desc.Height)) {
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
	else {
		BYTE* data = nullptr;
		const long size = pSample->GetActualDataLength();
		if (size > 0 && S_OK == pSample->GetPointer(&data)) {
			if (!CheckInput(m_srcD3DFormat, m_srcWidth, m_srcHeight)) {
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
	m_SrcSamples.Get().SampleFormat = m_CurrentSampleFmt;

	m_frame++;

	return hr;
}

HRESULT CDX9VideoProcessor::Render(const FILTER_STATE filterState)
{
	if (m_SrcSamples.Empty()) return E_POINTER;

	HRESULT hr = m_pD3DDevEx->BeginScene();

	CComPtr<IDirect3DSurface9> pBackBuffer;
	hr = m_pD3DDevEx->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);

	hr = m_pD3DDevEx->SetRenderTarget(0, pBackBuffer);
	m_pD3DDevEx->ColorFill(pBackBuffer, nullptr, 0);

	if (filterState == State_Running) {
		hr = ProcessDXVA2(pBackBuffer);
	}

	hr = m_pD3DDevEx->EndScene();

	const CRect rSrcPri(CPoint(0, 0), m_windowRect.Size());
	const CRect rDstPri(m_windowRect);

	hr = m_pD3DDevEx->PresentEx(rSrcPri, rDstPri, nullptr, nullptr, 0);

	return hr;
}

HRESULT CDX9VideoProcessor::ProcessDXVA2(IDirect3DSurface9* pRenderTarget)
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
	blt.ProcAmpValues.Contrast = m_DXVA2ProcAmpValues[1];
	blt.ProcAmpValues.Hue = m_DXVA2ProcAmpValues[2];
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
		DLog(L"CDX9VideoProcessor::ProcessDXVA2 : VideoProcessBlt() failed with error 0x%08x", hr);
	}

	return hr;
}
