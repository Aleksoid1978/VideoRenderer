/*
 * (C) 2018-2020 see Authors.txt
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
#include <evr.h> // for MR_VIDEO_ACCELERATION_SERVICE, because the <mfapi.h> does not contain it
#include <Mferror.h>
#include "Helper.h"
#include "PropPage.h"
#include "VideoRendererInputPin.h"
#include "../Include/Version.h"
#include "../Include/SubRenderIntf.h"
#include "VideoRenderer.h"

#define OPT_REGKEY_VIDEORENDERER L"Software\\MPC-BE Filters\\MPC Video Renderer"
#define OPT_UseD3D11             L"UseD3D11"
#define OPT_ShowStatistics       L"ShowStatistics"
#define OPT_TextureFormat        L"TextureFormat"
#define OPT_VPEnableNV12         L"VPEnableNV12"
#define OPT_VPEnableP01x         L"VPEnableP01x"
#define OPT_VPEnableYUY2         L"VPEnableYUY2"
#define OPT_VPEnableOther        L"VPEnableOther"
#define OPT_DoubleFrateDeint     L"DoubleFramerateDeinterlace"
#define OPT_VPScaling            L"VPScaling"
#define OPT_ChromaScaling        L"ChromaScaling"
#define OPT_Upscaling            L"Upscaling"
#define OPT_Downscaling          L"Downscaling"
#define OPT_InterpolateAt50pct   L"InterpolateAt50pct"
#define OPT_Dither               L"Dither"
#define OPT_SwapEffect           L"SwapEffect"

static const wchar_t g_szClassName[] = L"VRWindow";

//
// CMpcVideoRenderer
//

CMpcVideoRenderer::CMpcVideoRenderer(LPUNKNOWN pUnk, HRESULT* phr)
	: CBaseVideoRenderer2(__uuidof(this), L"MPC Video Renderer", pUnk, phr)
	, m_DX9_VP(this)
	, m_DX11_VP(this)
{
#ifdef _DEBUG
	DbgSetModuleLevel(LOG_TRACE, DWORD_MAX);
	DbgSetModuleLevel(LOG_ERROR, DWORD_MAX);
#endif

	DLog(L"Windows %s", GetWindowsVersion());
	DLog(GetNameAndVersion());

	ASSERT(S_OK == *phr);
	m_pInputPin = new CVideoRendererInputPin(this, phr, L"In", this);
	ASSERT(S_OK == *phr);

	// read settings

	CRegKey key;
	if (ERROR_SUCCESS == key.Open(HKEY_CURRENT_USER, OPT_REGKEY_VIDEORENDERER, KEY_READ)) {
		DWORD dw;
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_UseD3D11, dw)) {
			m_Sets.bUseD3D11 = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_ShowStatistics, dw)) {
			m_Sets.bShowStats = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_TextureFormat, dw)) {
			switch (dw) {
			case TEXFMT_AUTOINT:
			case TEXFMT_8INT:
			case TEXFMT_10INT:
			case TEXFMT_16FLOAT:
				m_Sets.iTextureFmt = dw;
				break;
			default:
				m_Sets.iTextureFmt = TEXFMT_AUTOINT;
			}
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_VPEnableNV12, dw)) {
			m_Sets.VPFmts.bNV12 = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_VPEnableP01x, dw)) {
			m_Sets.VPFmts.bP01x = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_VPEnableYUY2, dw)) {
			m_Sets.VPFmts.bYUY2 = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_VPEnableOther, dw)) {
			m_Sets.VPFmts.bOther = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_DoubleFrateDeint, dw)) {
			m_Sets.bDeintDouble = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_VPScaling, dw)) {
			m_Sets.bVPScaling = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_ChromaScaling, dw)) {
			m_Sets.iChromaScaling = discard((int)dw, (int)CHROMA_Bilinear, (int)CHROMA_Bilinear, (int)CHROMA_CatmullRom);
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_Upscaling, dw)) {
			m_Sets.iUpscaling = discard((int)dw, (int)UPSCALE_CatmullRom, (int)UPSCALE_Nearest, (int)UPSCALE_Lanczos3);
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_Downscaling, dw)) {
			m_Sets.iDownscaling = discard((int)dw, (int)DOWNSCALE_Hamming, (int)DOWNSCALE_Box, (int)DOWNSCALE_Lanczos);
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_InterpolateAt50pct, dw)) {
			m_Sets.bInterpolateAt50pct = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_Dither, dw)) {
			m_Sets.bUseDither = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_SwapEffect, dw)) {
			m_Sets.iSwapEffect = discard((int)dw, (int)SWAPEFFECT_Discard, (int)SWAPEFFECT_Discard, (int)SWAPEFFECT_Flip);
		}
	}

	// configure the video processors
	m_DX9_VP.SetShowStats(m_Sets.bShowStats);
	m_DX9_VP.SetTexFormat(m_Sets.iTextureFmt);
	m_DX9_VP.SetVPEnableFmts(m_Sets.VPFmts);
	m_DX9_VP.SetDeintDouble(m_Sets.bDeintDouble);
	m_DX9_VP.SetVPScaling(m_Sets.bVPScaling);
	m_DX9_VP.SetChromaScaling(m_Sets.iChromaScaling);
	m_DX9_VP.SetUpscaling(m_Sets.iUpscaling);
	m_DX9_VP.SetDownscaling(m_Sets.iDownscaling);
	m_DX9_VP.SetInterpolateAt50pct(m_Sets.bInterpolateAt50pct);
	m_DX9_VP.SetDither(m_Sets.bUseDither);
	m_DX9_VP.SetSwapEffect(m_Sets.iSwapEffect);

	m_DX11_VP.SetShowStats(m_Sets.bShowStats);
	m_DX11_VP.SetTexFormat(m_Sets.iTextureFmt);
	m_DX11_VP.SetVPEnableFmts(m_Sets.VPFmts);
	m_DX11_VP.SetDeintDouble(m_Sets.bDeintDouble);
	m_DX11_VP.SetVPScaling(m_Sets.bVPScaling);
	m_DX11_VP.SetChromaScaling(m_Sets.iChromaScaling);
	m_DX11_VP.SetUpscaling(m_Sets.iUpscaling);
	m_DX11_VP.SetDownscaling(m_Sets.iDownscaling);
	m_DX11_VP.SetInterpolateAt50pct(m_Sets.bInterpolateAt50pct);
	m_DX11_VP.SetDither(m_Sets.bUseDither);
	m_DX11_VP.SetSwapEffect(m_Sets.iSwapEffect);

	// other
	m_bUsedD3D11 = m_Sets.bUseD3D11 && IsWindows7SP1OrGreater();

	// initialize the video processor
	if (m_bUsedD3D11) {
		*phr = m_DX11_VP.Init(m_hWnd);
		if (*phr == S_OK) {
			DLog(L"Direct3D11 initialization successfully!");
			return;
		}

		m_bUsedD3D11 = false;
	}

	m_evDX9Init.Reset();
	m_evDX9InitHwnd.Reset();
	m_evDX9Resize.Reset();
	m_evQuit.Reset();
	m_evThreadFinishJob.Reset();

	m_DX9Thread = std::thread([this] { DX9Thread(); });

	m_evDX9Init.Set();
	WaitForSingleObject(m_evThreadFinishJob, INFINITE);
	*phr = m_hrThread;
	DLogIf(S_OK == *phr, L"Direct3D9 initialization successfully!");

	return;
}

CMpcVideoRenderer::~CMpcVideoRenderer()
{
	DLog(L"~CMpcVideoRenderer()");

	if (m_DX9Thread.joinable()) {
		m_evQuit.Set();
		m_DX9Thread.join();
	}

	if (m_hWnd) {
		BOOL ret = DestroyWindow(m_hWnd);
		DLogIf(!ret, L"DestroyWindow(m_hWnd) failed with error %s", HR2Str(HRESULT_FROM_WIN32(GetLastError())));
	}

	UnregisterClassW(g_szClassName, g_hInst);
}

void CMpcVideoRenderer::DX9Thread()
{
	HANDLE hEvts[] = { m_evDX9Init, m_evDX9InitHwnd, m_evDX9Resize, m_evQuit };

	for (;;) {
		const auto dwObject = WaitForMultipleObjects(std::size(hEvts), hEvts, FALSE, INFINITE);
		m_hrThread = E_FAIL;
		switch (dwObject) {
			case WAIT_OBJECT_0:
				m_hrThread = m_DX9_VP.Init(::GetForegroundWindow(), nullptr);
				m_evThreadFinishJob.Set();
				break;
			case WAIT_OBJECT_0 + 1:
				{
					bool bChangeDevice = false;
					m_hrThread = m_DX9_VP.Init(m_hWnd, &bChangeDevice);
					if (bChangeDevice) {
						OnDisplayChange();
					}
				}
				m_evThreadFinishJob.Set();
				break;
			case WAIT_OBJECT_0 + 2:
				m_hrThread = m_DX9_VP.SetWindowRect(m_windowRect);
				m_evThreadFinishJob.Set();
				break;
			default:
				return;
		}
	}
}

void CMpcVideoRenderer::NewSegment(REFERENCE_TIME startTime)
{
	DLog(L"CMpcVideoRenderer::NewSegment()");

	m_rtStartTime = startTime;
}

HRESULT CMpcVideoRenderer::BeginFlush()
{
	DLog(L"CMpcVideoRenderer::BeginFlush()");

	m_bFlushing = true;
	return __super::BeginFlush();
}

HRESULT CMpcVideoRenderer::EndFlush()
{
	DLog(L"CMpcVideoRenderer::EndFlush()");

	if (m_bUsedD3D11) {
		m_DX11_VP.Flush();
	} else {
		m_DX9_VP.Flush();
	}

	HRESULT hr = __super::EndFlush();

	m_bFlushing = false;

	return hr;
}

long CMpcVideoRenderer::CalcImageSize(CMediaType& mt, bool redefine_mt)
{
	BITMAPINFOHEADER* pBIH = GetBIHfromVIHs(&mt);
	if (!pBIH) {
		ASSERT(FALSE); // excessive checking
		return 0;
	}

	if (redefine_mt) {
		CSize Size(pBIH->biWidth, pBIH->biHeight);
		BOOL ret = FALSE;
		if (m_bUsedD3D11) {
			ret = m_DX11_VP.GetAlignmentSize(mt, Size);
		} else {
			ret = m_DX9_VP.GetAlignmentSize(mt, Size);
		}

		if (ret && (Size.cx != pBIH->biWidth || Size.cy != pBIH->biHeight)) {
			BYTE* pbFormat = mt.ReallocFormatBuffer(112 + sizeof(VR_Extradata));
			if (pbFormat) {
				// update pointer after realoc
				pBIH = GetBIHfromVIHs(&mt);
				// copy data to VR_Extradata
				VR_Extradata* vrextra = reinterpret_cast<VR_Extradata*>(pbFormat + 112);
				vrextra->QueryWidth  = Size.cx;
				vrextra->QueryHeight = Size.cy;
				vrextra->FrameWidth  = pBIH->biWidth;
				vrextra->FrameHeight = pBIH->biHeight;
				vrextra->Compression = pBIH->biCompression;
			}

			// new media type must have non-empty rcSource
			RECT& rcSource = ((VIDEOINFOHEADER*)mt.pbFormat)->rcSource;
			if (IsRectEmpty(&rcSource)) {
				rcSource = { 0, 0, pBIH->biWidth, abs(pBIH->biHeight) };
			}

			DLog(L"CMpcVideoRenderer::CalcImageSize() buffer size changed from %dx%d to %dx%d", pBIH->biWidth, pBIH->biHeight, Size.cx, Size.cy);
			// overwrite buffer size
			pBIH->biWidth  = Size.cx;
			pBIH->biHeight = Size.cy;
			pBIH->biSizeImage = DIBSIZE(*pBIH);
		}
	}

	return pBIH->biSizeImage ? pBIH->biSizeImage : DIBSIZE(*pBIH);
}

// CBaseRenderer

HRESULT CMpcVideoRenderer::CheckMediaType(const CMediaType* pmt)
{
	CheckPointer(pmt, E_POINTER);
	CheckPointer(pmt->pbFormat, E_POINTER);

	if (pmt->majortype == MEDIATYPE_Video && (pmt->formattype == FORMAT_VideoInfo2 || pmt->formattype == FORMAT_VideoInfo)) {
		for (const auto& sudPinType : sudPinTypesIn) {
			if (pmt->subtype == *sudPinType.clsMinorType) {
				CAutoLock cRendererLock(&m_RendererLock);

				if (m_bUsedD3D11) {
					if (!m_DX11_VP.VerifyMediaType(pmt)) {
						return VFW_E_UNSUPPORTED_VIDEO;
					}
				} else {
					if (!m_DX9_VP.VerifyMediaType(pmt)) {
						return VFW_E_UNSUPPORTED_VIDEO;
					}
				}

				return S_OK;
			}
		}
	}

	return E_FAIL;
}

HRESULT CMpcVideoRenderer::SetMediaType(const CMediaType *pmt)
{
	DLog(L"CMpcVideoRenderer::SetMediaType()\n%s", MediaType2Str(pmt));

	CheckPointer(pmt, E_POINTER);
	CheckPointer(pmt->pbFormat, E_POINTER);

	CAutoLock cVideoLock(&m_InterfaceLock);
	CAutoLock cRendererLock(&m_RendererLock);

	CMediaType mt(*pmt);

	auto inputPin = static_cast<CVideoRendererInputPin*>(m_pInputPin);
	if (!inputPin->FrameInVideoMem()) {
		CMediaType mtNew(*pmt);
		long ret = CalcImageSize(mtNew, true);

		if (mtNew != mt) {
			if (S_OK == m_pInputPin->GetConnected()->QueryAccept(&mtNew)) {
				DLog(L"CMpcVideoRenderer::SetMediaType() : upstream filter accepted new media type. QueryAccept return S_OK");
				inputPin->SetNewMediaType(mtNew);
				mt = mtNew;
			}
		}
	}

	if (m_bUsedD3D11) {
		if (!m_DX11_VP.InitMediaType(&mt)) {
			return VFW_E_UNSUPPORTED_VIDEO;
		}
	} else {
		if (!m_DX9_VP.InitMediaType(&mt)) {
			return VFW_E_UNSUPPORTED_VIDEO;
		}
	}

	if (!m_videoRect.IsRectNull()) {
		if (m_bUsedD3D11) {
			m_DX11_VP.SetVideoRect(m_videoRect);
		} else {
			m_DX9_VP.SetVideoRect(m_videoRect);
		}
	}

	return S_OK;
}

HRESULT CMpcVideoRenderer::DoRenderSample(IMediaSample* pSample)
{
	CheckPointer(pSample, E_POINTER);

	HRESULT hr = S_OK;

	if (m_bUsedD3D11) {
		hr = m_DX11_VP.ProcessSample(pSample);
	} else {
		hr = m_DX9_VP.ProcessSample(pSample);
	}

	if (m_Stepping && !(--m_Stepping)) {
		this->NotifyEvent(EC_STEP_COMPLETE, 0, 0);
	}

	return hr;
}

HRESULT CMpcVideoRenderer::Receive(IMediaSample* pSample)
{
	// îverride CBaseRenderer::Receive() for the implementation of the search during the pause

	if (m_bFlushing) {
		DLog(L"CMpcVideoRenderer::Receive() - flushing, skip sample");
		return S_OK;
	}

	ASSERT(pSample);

	// It may return VFW_E_SAMPLE_REJECTED code to say don't bother

	HRESULT hr = PrepareReceive(pSample);
	ASSERT(m_bInReceive == SUCCEEDED(hr));
	if (FAILED(hr)) {
		if (hr == VFW_E_SAMPLE_REJECTED) {
			return NOERROR;
		}
		return hr;
	}

	// We realize the palette in "PrepareRender()" so we have to give away the
	// filter lock here.
	if (m_State == State_Paused) {
		// no need to use InterlockedExchange
		m_bInReceive = FALSE;
		{
			// We must hold both these locks
			CAutoLock cRendererLock(&m_InterfaceLock);
			if (m_State == State_Stopped)
				return NOERROR;

			m_bInReceive = TRUE;
			CAutoLock cSampleLock(&m_RendererLock);
		}
		Ready();
	}

	if (m_State == State_Paused) {
		m_bInReceive = FALSE;

		CAutoLock cSampleLock(&m_RendererLock);
		DoRenderSample(m_pMediaSample);
	}

	// Having set an advise link with the clock we sit and wait. We may be
	// awoken by the clock firing or by a state change. The rendering call
	// will lock the critical section and check we can still render the data

	hr = WaitForRenderTime();
	if (FAILED(hr)) {
		m_bInReceive = FALSE;
		return NOERROR;
	}

	//  Set this here and poll it until we work out the locking correctly
	//  It can't be right that the streaming stuff grabs the interface
	//  lock - after all we want to be able to wait for this stuff
	//  to complete
	m_bInReceive = FALSE;

	// We must hold both these locks
	CAutoLock cRendererLock(&m_InterfaceLock);

	// since we gave away the filter wide lock, the sate of the filter could
	// have chnaged to Stopped
	if (m_State == State_Stopped)
		return NOERROR;

	CAutoLock cSampleLock(&m_RendererLock);

	// Deal with this sample

	if (m_State == State_Running) {
		Render(m_pMediaSample);
	}

	ClearPendingSample();
	SendEndOfStream();
	CancelNotification();
	return NOERROR;
}

STDMETHODIMP CMpcVideoRenderer::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	return
		QI(IKsPropertySet)
		QI(IMFGetService)
		QI(IBasicVideo)
		QI(IBasicVideo2)
		QI(IVideoWindow)
		QI(ISpecifyPropertyPages)
		QI(IVideoRenderer)
		QI(ISubRender)
		QI(IExFilterConfig)
		__super::NonDelegatingQueryInterface(riid, ppv);
}

// IMediaFilter
STDMETHODIMP CMpcVideoRenderer::Run(REFERENCE_TIME rtStart)
{
	DLog(L"CMpcVideoRenderer::Run()");

	if (m_State == State_Running) {
		return NOERROR;
	}

	m_filterState = State_Running;

	if (m_bUsedD3D11) {
		if (!m_bCheckSubInvAlpha) {
			// only one check for XySubFilter in the graph after playback starts
			m_bCheckSubInvAlpha = true;
			m_bSubInvAlpha = false;
			IEnumFilters *pEnumFilters = nullptr;
			if (m_pGraph && SUCCEEDED(m_pGraph->EnumFilters(&pEnumFilters))) {
				for (IBaseFilter *pBaseFilter = nullptr; S_OK == pEnumFilters->Next(1, &pBaseFilter, 0); ) {
					GUID clsid;
					if (SUCCEEDED(pBaseFilter->GetClassID(&clsid)) && (clsid == CLSID_XySubFilter || clsid == CLSID_XySubFilter_AutoLoader)) {
						m_bSubInvAlpha = true;
					}
					SAFE_RELEASE(pBaseFilter);

					if (m_bSubInvAlpha) {
						break;
					}
				}
				SAFE_RELEASE(pEnumFilters);
			}
		}

		m_DX11_VP.Start();
	} else {
		m_DX9_VP.Start();
	}

	return CBaseVideoRenderer2::Run(rtStart);
}

STDMETHODIMP CMpcVideoRenderer::Pause()
{
	DLog(L"CMpcVideoRenderer::Pause()");

	m_filterState = State_Paused;

	return CBaseVideoRenderer2::Pause();
}

STDMETHODIMP CMpcVideoRenderer::Stop()
{
	DLog(L"CMpcVideoRenderer::Stop()");

	m_filterState = State_Stopped;

	return CBaseVideoRenderer2::Stop();
}

// IKsPropertySet
STDMETHODIMP CMpcVideoRenderer::Set(REFGUID PropSet, ULONG Id, LPVOID pInstanceData, ULONG InstanceLength, LPVOID pPropertyData, ULONG DataLength)
{
	if (PropSet == AM_KSPROPSETID_CopyProt) {
		if (Id == AM_PROPERTY_COPY_MACROVISION) {
			DLog(L"Oops, no-no-no, no macrovision please");
			return S_OK;
		}
	}
	else if (PropSet == AM_KSPROPSETID_FrameStep) {
		if (Id == AM_PROPERTY_FRAMESTEP_STEP) {
			m_Stepping = 1;
			return S_OK;
		}
		if (Id == AM_PROPERTY_FRAMESTEP_CANSTEP || Id == AM_PROPERTY_FRAMESTEP_CANSTEPMULTIPLE) {
			return S_OK;
		}
	}
	else {
		return E_PROP_SET_UNSUPPORTED;
	}

	return E_PROP_ID_UNSUPPORTED;
}

STDMETHODIMP CMpcVideoRenderer::Get(REFGUID PropSet, ULONG Id, LPVOID pInstanceData, ULONG InstanceLength, LPVOID pPropertyData, ULONG DataLength, ULONG* pBytesReturned)
{
	return E_PROP_SET_UNSUPPORTED;
}

STDMETHODIMP CMpcVideoRenderer::QuerySupported(REFGUID PropSet, ULONG Id, ULONG* pTypeSupport)
{
	if (PropSet == AM_KSPROPSETID_CopyProt) {
		if (Id == AM_PROPERTY_COPY_MACROVISION) {
			*pTypeSupport = KSPROPERTY_SUPPORT_SET;
			return S_OK;
		}
		if (Id == AM_PROPERTY_COPY_ANALOG_COMPONENT) {
			*pTypeSupport = KSPROPERTY_SUPPORT_GET;
			return S_OK;
		}
	}
	else {
		return E_PROP_SET_UNSUPPORTED;
	}

	return E_PROP_ID_UNSUPPORTED;
}

// IMFGetService
STDMETHODIMP CMpcVideoRenderer::GetService(REFGUID guidService, REFIID riid, LPVOID *ppvObject)
{
	if (guidService == MR_VIDEO_ACCELERATION_SERVICE) {
		if (riid == __uuidof(IDirect3DDeviceManager9)) {
			if (m_bUsedD3D11) {
				return m_DX11_VP.GetDeviceManager9()->QueryInterface(riid, ppvObject);
			} else {
				return m_DX9_VP.GetDeviceManager9()->QueryInterface(riid, ppvObject);
			}
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
	if (guidService == MR_VIDEO_MIXER_SERVICE) {
		if (riid == IID_IMFVideoProcessor) {
			if (m_bUsedD3D11) {
				return m_DX11_VP.QueryInterface(riid, ppvObject);
			} else {
				return m_DX9_VP.QueryInterface(riid, ppvObject);
			}
		}
	}

	return E_NOINTERFACE;
}

// IBasicVideo
STDMETHODIMP CMpcVideoRenderer::GetSourcePosition(long *pLeft, long *pTop, long *pWidth, long *pHeight)
{
	CheckPointer(pLeft,E_POINTER);
	CheckPointer(pTop,E_POINTER);
	CheckPointer(pWidth,E_POINTER);
	CheckPointer(pHeight,E_POINTER);

	CRect rect;
	{
		CAutoLock cVideoLock(&m_InterfaceLock);
		if (m_bUsedD3D11) {
			m_DX11_VP.GetSourceRect(rect);
		} else {
			m_DX9_VP.GetSourceRect(rect);
		}
	}

	*pLeft = rect.left;
	*pTop = rect.top;
	*pWidth = rect.Width();
	*pHeight = rect.Height();

	return S_OK;

	return E_NOTIMPL;
}

STDMETHODIMP CMpcVideoRenderer::SetDestinationPosition(long Left, long Top, long Width, long Height)
{
	const CRect videoRect(Left, Top, Left + Width, Top + Height);
	if (videoRect.IsRectNull()) {
		return S_OK;
	}

	if (videoRect != m_videoRect) {
		m_videoRect = videoRect;

		CAutoLock cRendererLock(&m_RendererLock);
		if (m_bUsedD3D11) {
			m_DX11_VP.SetVideoRect(videoRect);
		} else {
			m_DX9_VP.SetVideoRect(videoRect);
		}
	}

	if (!bUseInMPCBE) {
		Redraw();
	}

	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::GetDestinationPosition(long *pLeft, long *pTop, long *pWidth, long *pHeight)
{
	CheckPointer(pLeft,E_POINTER);
	CheckPointer(pTop,E_POINTER);
	CheckPointer(pWidth,E_POINTER);
	CheckPointer(pHeight,E_POINTER);

	CRect rect;
	{
		CAutoLock cVideoLock(&m_InterfaceLock);
		if (m_bUsedD3D11) {
			m_DX11_VP.GetVideoRect(rect);
		} else {
			m_DX9_VP.GetVideoRect(rect);
		}
	}

	*pLeft = rect.left;
	*pTop = rect.top;
	*pWidth = rect.Width();
	*pHeight = rect.Height();

	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::GetVideoSize(long *pWidth, long *pHeight)
{
	// retrieves the native video's width and height.
	if (m_bUsedD3D11) {
		return m_DX11_VP.GetVideoSize(pWidth, pHeight);
	} else {
		return m_DX9_VP.GetVideoSize(pWidth, pHeight);
	}
}

STDMETHODIMP CMpcVideoRenderer::GetCurrentImage(long *pBufferSize, long *pDIBImage)
{
	CheckPointer(pBufferSize, E_POINTER);

	CAutoLock cVideoLock(&m_InterfaceLock);
	CAutoLock cRendererLock(&m_RendererLock);
	HRESULT hr;

	CRect rect;
	if (m_bUsedD3D11) {
		m_DX11_VP.GetSourceRect(rect);
	} else {
		m_DX9_VP.GetSourceRect(rect);
	}

	const int w = rect.Width();
	const int h = rect.Height();

	// VFW_E_NOT_PAUSED ?

	if (w <= 0 || h <= 0) {
		return E_FAIL;
	}
	long size = w * h * 4 + sizeof(BITMAPINFOHEADER);

	if (pDIBImage == nullptr) {
		*pBufferSize = size;
		return S_OK;
	}

	if (size > *pBufferSize) {
		return E_OUTOFMEMORY;
	}

	if (m_bUsedD3D11) {
		hr = m_DX11_VP.GetCurentImage(pDIBImage);
	} else {
		hr = m_DX9_VP.GetCurentImage(pDIBImage);
	}

	return hr;
}

// IBasicVideo2
STDMETHODIMP CMpcVideoRenderer::GetPreferredAspectRatio(long *plAspectX, long *plAspectY)
{
	if (m_bUsedD3D11) {
		return m_DX11_VP.GetAspectRatio(plAspectX, plAspectY);
	} else {
		return m_DX9_VP.GetAspectRatio(plAspectX, plAspectY);
	}
}

// IVideoWindow
STDMETHODIMP CMpcVideoRenderer::put_Owner(OAHWND Owner)
{
	if (m_hWndParent != (HWND)Owner) {
		m_hWndParent = (HWND)Owner;

		if (m_hWnd) {
			DestroyWindow(m_hWnd);
			m_hWnd = nullptr;
		}

		WNDCLASSEXW wc = { sizeof(wc) };
		if (!GetClassInfoExW(g_hInst, g_szClassName, &wc)) {
			wc.lpfnWndProc = ::DefWindowProcW;
			wc.hInstance = g_hInst;
			wc.lpszClassName = g_szClassName;
			if (!RegisterClassExW(&wc)) {
				return E_FAIL;
			}
		}

		m_hWnd = CreateWindowExW(
			0,
			g_szClassName,
			nullptr,
			WS_VISIBLE | WS_CHILDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			m_hWndParent,
			nullptr,
			g_hInst,
			nullptr
		);

		if (!m_hWnd) {
			return E_FAIL;
		}

		if (!m_windowRect.IsRectNull()) {
			SetWindowPos(m_hWnd, nullptr, m_windowRect.left, m_windowRect.top, m_windowRect.Width(), m_windowRect.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
		}

		HRESULT hr;
		if (m_bUsedD3D11) {
			hr = m_DX11_VP.Init(m_hWnd);
		} else {
			m_evDX9InitHwnd.Set();
			WaitForSingleObject(m_evThreadFinishJob, INFINITE);
			hr = m_hrThread;
		}

		return hr;
	}
	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::get_Owner(OAHWND *Owner)
{
	CheckPointer(Owner, E_POINTER);
	*Owner = (OAHWND)m_hWnd;
	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::SetWindowPosition(long Left, long Top, long Width, long Height)
{
	const CRect windowRect(Left, Top, Left + Width, Top + Height);
	if (windowRect == m_windowRect) {
		return S_OK;
	}

	m_windowRect = windowRect;

	CAutoLock cRendererLock(&m_RendererLock);
	if (m_hWnd) {
		SetWindowPos(m_hWnd, nullptr, Left, Top, Width, Height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
		if (Left < 0) {
			m_windowRect.OffsetRect(-Left, 0);
		}
		if (Top < 0) {
			m_windowRect.OffsetRect(0, -Top);
		}
	}

	if (m_bUsedD3D11) {
		m_DX11_VP.SetWindowRect(m_windowRect);
	} else {
		m_evDX9Resize.Set();
		WaitForSingleObject(m_evThreadFinishJob, INFINITE);
	}

	m_windowRect = windowRect;

	if (!bUseInMPCBE) {
		Redraw();
	}

	return S_OK;
}

// ISpecifyPropertyPages
STDMETHODIMP CMpcVideoRenderer::GetPages(CAUUID* pPages)
{
	CheckPointer(pPages, E_POINTER);

	static const GUID guidQualityPPage = { 0x565DCEF2, 0xAFC5, 0x11D2, 0x88, 0x53, 0x00, 0x00, 0xF8, 0x08, 0x83, 0xE3 };

	pPages->cElems = 3;
	pPages->pElems = reinterpret_cast<GUID*>(CoTaskMemAlloc(sizeof(GUID) * pPages->cElems));
	if (pPages->pElems == nullptr) {
		return E_OUTOFMEMORY;
	}

	pPages->pElems[0] = __uuidof(CVRMainPPage);
	pPages->pElems[1] = __uuidof(CVRInfoPPage);
	pPages->pElems[2] = guidQualityPPage;

	return S_OK;
}

// IVideoRenderer

STDMETHODIMP CMpcVideoRenderer::GetVideoProcessorInfo(CStringW& str)
{
	if (m_bUsedD3D11) {
		return m_DX11_VP.GetVPInfo(str);
	} else {
		return m_DX9_VP.GetVPInfo(str);
	}
}

STDMETHODIMP_(bool) CMpcVideoRenderer::GetActive()
{
	return m_pInputPin && m_pInputPin->GetConnected();
}

STDMETHODIMP_(void) CMpcVideoRenderer::GetSettings(Settings_t& setings)
{
	setings = m_Sets;
}

STDMETHODIMP_(void) CMpcVideoRenderer::SetSettings(const Settings_t setings)
{
	m_Sets.bUseD3D11   = setings.bUseD3D11;
	m_Sets.iSwapEffect = setings.iSwapEffect;

	CAutoLock cRendererLock(&m_RendererLock);

	if (setings.bShowStats != m_Sets.bShowStats) {
		if (m_bUsedD3D11) {
			m_DX11_VP.SetShowStats(setings.bShowStats);
		} else {
			m_DX9_VP.SetShowStats(setings.bShowStats);
		}
		m_Sets.bShowStats = setings.bShowStats;
	}

	if (setings.bDeintDouble != m_Sets.bDeintDouble) {
		if (m_bUsedD3D11) {
			m_DX11_VP.SetDeintDouble(setings.bDeintDouble);
		} else {
			m_DX9_VP.SetDeintDouble(setings.bDeintDouble);
		}
		m_Sets.bDeintDouble = setings.bDeintDouble;
	}

	if (setings.bVPScaling != m_Sets.bVPScaling) {
		if (m_bUsedD3D11) {
			m_DX11_VP.SetVPScaling(setings.bVPScaling);
		} else {
			m_DX9_VP.SetVPScaling(setings.bVPScaling);
		}
		m_Sets.bVPScaling = setings.bVPScaling;
	}

	if (setings.iChromaScaling != m_Sets.iChromaScaling) {
		if (m_bUsedD3D11) {
			m_DX11_VP.SetChromaScaling(setings.iChromaScaling);
		} else {
			m_DX9_VP.SetChromaScaling(setings.iChromaScaling);
		}
		m_Sets.iChromaScaling = setings.iChromaScaling;
	}

	if (setings.iUpscaling != m_Sets.iUpscaling) {
		if (m_bUsedD3D11) {
			m_DX11_VP.SetUpscaling(setings.iUpscaling);
		} else {
			m_DX9_VP.SetUpscaling(setings.iUpscaling);
		}
		m_Sets.iUpscaling = setings.iUpscaling;
	}

	if (setings.iDownscaling != m_Sets.iDownscaling) {
		if (m_bUsedD3D11) {
			m_DX11_VP.SetDownscaling(setings.iDownscaling);
		} else {
			m_DX9_VP.SetDownscaling(setings.iDownscaling);
		}
		m_Sets.iDownscaling = setings.iDownscaling;
	}

	if (setings.bInterpolateAt50pct != m_Sets.bInterpolateAt50pct) {
		if (m_bUsedD3D11) {
			m_DX11_VP.SetInterpolateAt50pct(setings.bInterpolateAt50pct);
		} else {
			m_DX9_VP.SetInterpolateAt50pct(setings.bInterpolateAt50pct);
		}
		m_Sets.bInterpolateAt50pct = setings.bInterpolateAt50pct;
	}

	if (setings.bUseDither != m_Sets.bUseDither) {
		if (m_bUsedD3D11) {
			m_DX11_VP.SetDither(setings.bUseDither);
		} else {
			m_DX9_VP.SetDither(setings.bUseDither);
		}
		m_Sets.bUseDither = setings.bUseDither;
	}

	if (m_Sets.iTextureFmt != setings.iTextureFmt
		|| setings.VPFmts.bNV12  != m_Sets.VPFmts.bNV12
		|| setings.VPFmts.bP01x  != m_Sets.VPFmts.bP01x
		|| setings.VPFmts.bYUY2  != m_Sets.VPFmts.bYUY2
		|| setings.VPFmts.bOther != m_Sets.VPFmts.bOther) {

		m_Sets.iTextureFmt = setings.iTextureFmt;
		m_Sets.VPFmts      = setings.VPFmts;

		if (m_inputMT.IsValid()) {
			BOOL ret;
			if (m_bUsedD3D11) {
				m_DX11_VP.SetTexFormat(m_Sets.iTextureFmt);
				m_DX11_VP.SetVPEnableFmts(m_Sets.VPFmts);
				ret = m_DX11_VP.InitMediaType(&m_inputMT);
			} else {
				m_DX9_VP.SetTexFormat(m_Sets.iTextureFmt);
				m_DX9_VP.SetVPEnableFmts(m_Sets.VPFmts);
				ret = m_DX9_VP.InitMediaType(&m_inputMT);
			}
		}
	}
}

STDMETHODIMP CMpcVideoRenderer::SaveSettings()
{
	CRegKey key;
	if (ERROR_SUCCESS == key.Create(HKEY_CURRENT_USER, OPT_REGKEY_VIDEORENDERER)) {
		key.SetDWORDValue(OPT_UseD3D11,           m_Sets.bUseD3D11);
		key.SetDWORDValue(OPT_ShowStatistics,     m_Sets.bShowStats);
		key.SetDWORDValue(OPT_TextureFormat,      m_Sets.iTextureFmt);
		key.SetDWORDValue(OPT_VPEnableNV12,       m_Sets.VPFmts.bNV12);
		key.SetDWORDValue(OPT_VPEnableP01x,       m_Sets.VPFmts.bP01x);
		key.SetDWORDValue(OPT_VPEnableYUY2,       m_Sets.VPFmts.bYUY2);
		key.SetDWORDValue(OPT_VPEnableOther,      m_Sets.VPFmts.bOther);
		key.SetDWORDValue(OPT_DoubleFrateDeint,   m_Sets.bDeintDouble);
		key.SetDWORDValue(OPT_VPScaling,          m_Sets.bVPScaling);
		key.SetDWORDValue(OPT_ChromaScaling,      m_Sets.iChromaScaling);
		key.SetDWORDValue(OPT_Upscaling,          m_Sets.iUpscaling);
		key.SetDWORDValue(OPT_Downscaling,        m_Sets.iDownscaling);
		key.SetDWORDValue(OPT_InterpolateAt50pct, m_Sets.bInterpolateAt50pct);
		key.SetDWORDValue(OPT_Dither,             m_Sets.bUseDither);
		key.SetDWORDValue(OPT_SwapEffect,         m_Sets.iSwapEffect);
	}

	return S_OK;
}

// ISubRender
STDMETHODIMP CMpcVideoRenderer::SetCallback(ISubRenderCallback* cb)
{
	bUseInMPCBE = false;

	m_pSubCallBack = cb;
	if (CComQIPtr<ISubRenderOptions> pSubRenderOptions = m_pSubCallBack) {
		LPWSTR name = nullptr;
		int nLen;
		if (S_OK == pSubRenderOptions->GetString("name", &name, &nLen) && name && nLen) {
			bUseInMPCBE = (wcscmp(name, L"MPC-BE") == 0);
			LocalFree(name);
		}
	}

	return S_OK;
}

// IExFilterConfig

STDMETHODIMP CMpcVideoRenderer::GetBool(LPCSTR field, bool* value)
{
	CheckPointer(value, E_POINTER);

	if (!strcmp(field, "statsEnable")) {
		*value = m_Sets.bShowStats;
		return S_OK;
	}

	return E_INVALIDARG;
}

STDMETHODIMP CMpcVideoRenderer::GetInt(LPCSTR field, int* value)
{
	CheckPointer(value, E_POINTER);

	if (!strcmp(field, "renderType")) {
		if (m_inputMT.IsValid()) {
			if (m_bUsedD3D11) {
				*value = 11; // Direct3D 11
			} else {
				*value = 9; // Direct3D 9
			}
		} else {
			*value = 0; // not initialized
		}
		return S_OK;
	}
	if (!strcmp(field, "playbackState")) {
		*value = m_filterState;
		return S_OK;
	}
	if (!strcmp(field, "rotation")) {
		if (m_bUsedD3D11) {
			*value = m_DX11_VP.GetRotation();
		} else {
			*value = m_DX9_VP.GetRotation();
		}
		return S_OK;
	}

	return E_INVALIDARG;
}

STDMETHODIMP CMpcVideoRenderer::GetInt64(LPCSTR field, __int64 *value)
{
	CheckPointer(value, E_POINTER);

	if (!strcmp(field, "version")) {
		*value  = ((uint64_t)MPCVR_VERSION_MAJOR << 48)
				| ((uint64_t)MPCVR_VERSION_MINOR << 32)
				| ((uint64_t)MPCVR_VERSION_BUILD << 16)
				| ((uint64_t)MPCVR_REV_NUM);
		return S_OK;
	}

	return E_INVALIDARG;
}

STDMETHODIMP CMpcVideoRenderer::SetBool(LPCSTR field, bool value)
{
	if (!strcmp(field, "statsEnable")) {
		m_Sets.bShowStats = value;
		if (m_bUsedD3D11) {
			m_DX11_VP.SetShowStats(m_Sets.bShowStats);
		} else {
			m_DX9_VP.SetShowStats(m_Sets.bShowStats);
		}

		SaveSettings();
		if (m_filterState == State_Paused) {
			Redraw();
		}
		return S_OK;
	}

	if (!strcmp(field, "cmd_redraw") && value) {
		Redraw();
		return S_OK;
	}

	if (!strcmp(field, "cmd_clearPostScaleShaders") && value) {
		CAutoLock cRendererLock(&m_RendererLock);
		if (m_bUsedD3D11) {
			m_DX11_VP.ClearPostScaleShaders();
		} else {
			m_DX9_VP.ClearPostScaleShaders();
		}
		return S_OK;
	}

	return E_INVALIDARG;
}

STDMETHODIMP CMpcVideoRenderer::SetInt(LPCSTR field, int value)
{
	if (!strcmp(field, "rotation")) {
		// Allowed angles are multiples of 90.
		if (value % 90 == 0) {
			// The allowed rotation angle is reduced to 0, 90, 180, 270.
			value %= 360;
			if (value < 0) {
				value += 360;
			}

			CAutoLock cRendererLock(&m_RendererLock);
			if (m_bUsedD3D11) {
				m_DX11_VP.SetRotation(value);
			} else {
				m_DX9_VP.SetRotation(value);
			}

			return S_OK;
		}
	}

	return E_INVALIDARG;
}

STDMETHODIMP CMpcVideoRenderer::SetBin(LPCSTR field, LPVOID value, int size)
{
	if (size > 0) {
		if (!strcmp(field, "cmd_addPostScaleShader")) {
			BYTE* p = (BYTE*)value;
			const BYTE* end = p + size;
			uint32_t chunkcode;
			int32_t chunksize;
			CStringW shaderName;
			CStringA shaderCode;

			while (p + 8 < end) {
				memcpy(&chunkcode, p, 4);
				p += 4;
				memcpy(&chunksize, p, 4);
				p += 4;
				if (chunksize <= 0 || p + chunksize > end) {
					break;
				}

				switch (chunkcode) {
				case FCC('NAME'):
					shaderName.SetString((LPCWSTR)p, chunksize / sizeof(wchar_t));
					break;
				case FCC('CODE'):
					shaderCode.SetString((LPCSTR)p, chunksize);
					break;
				}
				p += chunksize;
			}

			if (shaderCode.GetLength()) {
				CAutoLock cRendererLock(&m_RendererLock);
				if (m_bUsedD3D11) {
					return m_DX11_VP.AddPostScaleShader(shaderName, shaderCode);
				} else {
					return m_DX9_VP.AddPostScaleShader(shaderName, shaderCode);
				}
			}
		}
	}

	return E_INVALIDARG;
}

HRESULT CMpcVideoRenderer::Redraw()
{
	CAutoLock cRendererLock(&m_RendererLock);
	const auto bFrameDrawn = m_DrawStats.GetFrames() > 0;

	HRESULT hr = S_OK;
	if (m_bUsedD3D11) {
		if (bFrameDrawn && m_filterState != State_Stopped) {
			hr = m_DX11_VP.Render(0);
		} else {
			hr = m_DX11_VP.FillBlack();
		}
	} else {
		if (bFrameDrawn && m_filterState != State_Stopped) {
			hr = m_DX9_VP.Render(0);
		} else {
			hr = m_DX9_VP.FillBlack();
		}
	}

	return hr;
}
