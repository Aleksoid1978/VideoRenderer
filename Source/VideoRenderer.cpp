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
#include "VideoRenderer.h"

#define WM_SWITCH_FULLSCREEN (WM_APP + 0x1000)

#define OPT_REGKEY_VIDEORENDERER L"Software\\MPC-BE Filters\\MPC Video Renderer"
#define OPT_UseD3D11             L"UseD3D11"
#define OPT_ShowStatistics       L"ShowStatistics"
#define OPT_ResizeStatistics     L"ResizeStatistics"
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
#define OPT_ExclusiveFullscreen  L"ExclusiveFullscreen"
#define OPT_ExclusiveDelay       L"ExclusiveDelay"

static const wchar_t g_szClassName[] = L"VRWindow";

LPCTSTR g_pszOldParentWndProc = L"OldParentWndProc";
LPCTSTR g_pszThis = L"This";
static void RemoveParentWndProc(HWND hWnd)
{
	DLog(L"RemoveParentWndProc()");
	auto pfnOldProc = (WNDPROC)GetPropW(hWnd, g_pszOldParentWndProc);
	if (pfnOldProc) {
		SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)pfnOldProc);
		RemovePropW(hWnd, g_pszOldParentWndProc);
		RemovePropW(hWnd, g_pszThis);
	}
}

static LRESULT CALLBACK ParentWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	auto pfnOldProc = (WNDPROC)GetPropW(hWnd, g_pszOldParentWndProc);
	auto pThis = (CMpcVideoRenderer*)GetPropW(hWnd, g_pszThis);

	switch (Msg) {
		case WM_DESTROY:
			SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)pfnOldProc);
			RemovePropW(hWnd, g_pszOldParentWndProc);
			RemovePropW(hWnd, g_pszThis);
			break;
		case WM_DISPLAYCHANGE:
			DLog(L"ParentWndProc() - WM_DISPLAYCHANGE");
			pThis->OnDisplayModeChange(true);
			break;
		case WM_MOVE:
			if (pThis->m_bIsFullscreen) {
				// I don't know why, but without this, the filter freezes when switching from fullscreen to window in DX9 mode.
				SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)pfnOldProc);
				SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)ParentWndProc);
			} else {
				pThis->OnWindowMove();
			}
			break;
		case WM_NCACTIVATE:
			if (!wParam && pThis->m_bIsFullscreen) {
				return 0;
			}
			break;

	}

	return CallWindowProcW(pfnOldProc, hWnd, Msg, wParam, lParam);
}

//
// CMpcVideoRenderer
//

CMpcVideoRenderer::CMpcVideoRenderer(LPUNKNOWN pUnk, HRESULT* phr)
	: CBaseVideoRenderer2(__uuidof(this), L"MPC Video Renderer", pUnk, phr)
{
	DLog(L"CMpcVideoRenderer::CMpcVideoRenderer()");
	DLog(L"Windows {}", GetWindowsVersion());
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
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_ResizeStatistics, dw)) {
			m_Sets.iResizeStats = !!dw;
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
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_ExclusiveFullscreen, dw)) {
			m_Sets.bExclusiveFS = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_ExclusiveDelay, dw)) {
			m_Sets.bExclusiveDelay = !!dw;
		}
	}

	HRESULT hr = S_FALSE;

	if (m_Sets.bUseD3D11 && IsWindows7SP1OrGreater()) {
		m_VideoProcessor = new CDX11VideoProcessor(this, hr);
		if (SUCCEEDED(hr)) {
			// configure the video processor
			m_VideoProcessor->SetShowStats(m_Sets.bShowStats);
			m_VideoProcessor->SetTexFormat(m_Sets.iTextureFmt);
			m_VideoProcessor->SetVPEnableFmts(m_Sets.VPFmts);
			m_VideoProcessor->SetDeintDouble(m_Sets.bDeintDouble);
			m_VideoProcessor->SetVPScaling(m_Sets.bVPScaling);
			m_VideoProcessor->SetChromaScaling(m_Sets.iChromaScaling);
			m_VideoProcessor->SetUpscaling(m_Sets.iUpscaling);
			m_VideoProcessor->SetDownscaling(m_Sets.iDownscaling);
			m_VideoProcessor->SetInterpolateAt50pct(m_Sets.bInterpolateAt50pct);
			m_VideoProcessor->SetDither(m_Sets.bUseDither);
			m_VideoProcessor->SetSwapEffect(m_Sets.iSwapEffect);

			hr = m_VideoProcessor->Init(m_hWnd);
		}

		if (FAILED(hr)) {
			SAFE_DELETE(m_VideoProcessor);
		}
		DLogIf(S_OK == hr, L"Direct3D11 initialization successfully!");
	}

	if (!m_VideoProcessor) {
		m_VideoProcessor = new CDX9VideoProcessor(this, hr);
		if (SUCCEEDED(hr)) {
			// configure the video processor
			m_VideoProcessor->SetShowStats(m_Sets.bShowStats);
			m_VideoProcessor->SetTexFormat(m_Sets.iTextureFmt);
			m_VideoProcessor->SetVPEnableFmts(m_Sets.VPFmts);
			m_VideoProcessor->SetDeintDouble(m_Sets.bDeintDouble);
			m_VideoProcessor->SetVPScaling(m_Sets.bVPScaling);
			m_VideoProcessor->SetChromaScaling(m_Sets.iChromaScaling);
			m_VideoProcessor->SetUpscaling(m_Sets.iUpscaling);
			m_VideoProcessor->SetDownscaling(m_Sets.iDownscaling);
			m_VideoProcessor->SetInterpolateAt50pct(m_Sets.bInterpolateAt50pct);
			m_VideoProcessor->SetDither(m_Sets.bUseDither);
			m_VideoProcessor->SetSwapEffect(m_Sets.iSwapEffect);

			hr = m_VideoProcessor->Init(::GetForegroundWindow());
		}

		DLogIf(S_OK == hr, L"Direct3D9 initialization successfully!");
	}

	*phr = hr;

	return;
}

CMpcVideoRenderer::~CMpcVideoRenderer()
{
	DLog(L"CMpcVideoRenderer::~CMpcVideoRenderer()");

	EndFullScreenTimer();

	if (m_hWndWindow) {
		::SendMessageW(m_hWndWindow, WM_CLOSE, 0, 0);
	}

	UnregisterClassW(g_szClassName, g_hInst);

	if (m_hWndParentMain) {
		RemoveParentWndProc(m_hWndParentMain);
	}

	if (m_bIsFullscreen && m_hWndParentMain) {
		PostMessageW(m_hWndParentMain, WM_SWITCH_FULLSCREEN, 0, 0);
	}

	SAFE_DELETE(m_VideoProcessor);
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

	m_bValidBuffer = false;

	m_VideoProcessor->Flush();

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

		BOOL ret = m_VideoProcessor->GetAlignmentSize(mt, Size);

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

			DLog(L"CMpcVideoRenderer::CalcImageSize() buffer size changed from {}x{} to {}x{}", pBIH->biWidth, pBIH->biHeight, Size.cx, Size.cy);
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

				if (!m_VideoProcessor->VerifyMediaType(pmt)) {
					return VFW_E_UNSUPPORTED_VIDEO;
				}

				return S_OK;
			}
		}
	}

	return E_FAIL;
}

HRESULT CMpcVideoRenderer::SetMediaType(const CMediaType *pmt)
{
	DLog(L"CMpcVideoRenderer::SetMediaType()\n{}", MediaType2Str(pmt));

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

	if (!m_VideoProcessor->InitMediaType(&mt)) {
		return VFW_E_UNSUPPORTED_VIDEO;
	}

	if (!m_videoRect.IsRectNull()) {
		m_VideoProcessor->SetVideoRect(m_videoRect);
	}

	return S_OK;
}

HRESULT CMpcVideoRenderer::DoRenderSample(IMediaSample* pSample)
{
	CheckPointer(pSample, E_POINTER);

	HRESULT hr = m_VideoProcessor->ProcessSample(pSample);

	if (SUCCEEDED(hr)) {
		m_bValidBuffer = true;
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

void CMpcVideoRenderer::UpdateDiplayInfo()
{
	const HMONITOR hMonPrimary = MonitorFromPoint(CPoint(0, 0), MONITOR_DEFAULTTOPRIMARY);

	MONITORINFOEXW mi = { sizeof(mi) };
	GetMonitorInfoW(m_hMon, (MONITORINFO*)&mi);

	bool ret = GetDisplayConfig(mi.szDevice, m_DisplayConfig);
	if (m_hMon == hMonPrimary) {
		m_bPrimaryDisplay = true;
	} else {
		m_bPrimaryDisplay = false;
	}

	m_VideoProcessor->SetDisplayInfo(m_DisplayConfig, m_bPrimaryDisplay, m_bIsFullscreen);
}

void CMpcVideoRenderer::OnDisplayModeChange(const bool bReset/* = false*/)
{
	if (bReset && m_VideoProcessor->Type() == VP_DX9) {
		CAutoLock cRendererLock(&m_RendererLock);

		m_VideoProcessor->Reset();
	}

	m_hMon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
	UpdateDiplayInfo();
}

void CMpcVideoRenderer::OnWindowMove()
{
	if (GetActive()) {
		const HMONITOR hMon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
		if (hMon != m_hMon) {
			m_hMon = hMon;
			UpdateDiplayInfo();
		}
	}
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

	CAutoLock cVideoLock(&m_InterfaceLock);
	m_filterState = State_Running;

	if (m_VideoProcessor->Type() == VP_DX11) {
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
	}
	m_VideoProcessor->Start();

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
	DLog(L"IKsPropertySet::Set({}, {}, ...)", GUIDtoWString(PropSet), Id);

	if (PropSet == AM_KSPROPSETID_CopyProt) {
		if (Id == AM_PROPERTY_COPY_MACROVISION || Id == AM_PROPERTY_COPY_DIGITAL_CP) {
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
	DLog(L"IKsPropertySet::Get({}, {}, ...)", GUIDtoWString(PropSet), Id);

	if (PropSet == AM_KSPROPSETID_CopyProt) {
		if (Id == AM_PROPERTY_COPY_ANALOG_COMPONENT) {
			return S_FALSE;
		}
	}
	else {
		return E_PROP_SET_UNSUPPORTED;
	}

	return E_PROP_ID_UNSUPPORTED;
}

STDMETHODIMP CMpcVideoRenderer::QuerySupported(REFGUID PropSet, ULONG Id, ULONG* pTypeSupport)
{
	DLog(L"IKsPropertySet::QuerySupported({}, {}, ...)", GUIDtoWString(PropSet), Id);

	if (PropSet == AM_KSPROPSETID_CopyProt) {
		if (Id == AM_PROPERTY_COPY_MACROVISION || Id == AM_PROPERTY_COPY_DIGITAL_CP) {
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
			return m_VideoProcessor->GetDeviceManager9()->QueryInterface(riid, ppvObject);
		}
	}
	if (guidService == MR_VIDEO_MIXER_SERVICE) {
		if (riid == IID_IMFVideoProcessor || riid == IID_IMFVideoMixerBitmap) {
			return m_VideoProcessor->QueryInterface(riid, ppvObject);
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

		m_VideoProcessor->GetSourceRect(rect);
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

		m_VideoProcessor->SetVideoRect(videoRect);
	}

	if (m_bForceRedrawing) {
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

		m_VideoProcessor->GetVideoRect(rect);
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
	return m_VideoProcessor->GetVideoSize(pWidth, pHeight);
}

STDMETHODIMP CMpcVideoRenderer::GetCurrentImage(long *pBufferSize, long *pDIBImage)
{
	CheckPointer(pBufferSize, E_POINTER);

	CAutoLock cVideoLock(&m_InterfaceLock);
	CAutoLock cRendererLock(&m_RendererLock);
	HRESULT hr;

	CSize framesize;
	long aspectX, aspectY;
	int iRotation;

	m_VideoProcessor->GetVideoSize(&framesize.cx, &framesize.cy);
	m_VideoProcessor->GetAspectRatio(&aspectX, &aspectY);
	iRotation = m_VideoProcessor->GetRotation();

	if (aspectX > 0 && aspectY > 0) {
		if (iRotation == 90 || iRotation == 270) {
			framesize.cy = MulDiv(framesize.cx, aspectY, aspectX);
		} else {
			framesize.cx = MulDiv(framesize.cy, aspectX, aspectY);
		}
	}

	const auto w = framesize.cx;
	const auto h = framesize.cy;

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

	hr = m_VideoProcessor->GetCurentImage(pDIBImage);

	return hr;
}

// IBasicVideo2
STDMETHODIMP CMpcVideoRenderer::GetPreferredAspectRatio(long *plAspectX, long *plAspectY)
{
	return m_VideoProcessor->GetAspectRatio(plAspectX, plAspectY);
}

static VOID CALLBACK TimerCallbackFunc(PVOID lpParameter, BOOLEAN TimerOrWaitFired)
{
	if (auto pRenderer = (CMpcVideoRenderer*)lpParameter) {
		pRenderer->SwitchFullScreen(true);
	}
}

BOOL CMpcVideoRenderer::StartFullScreenTimer()
{
	BOOL ret = FALSE;
	if (!m_hFullScreenTimerHandle) {
		ret = CreateTimerQueueTimer(
			&m_hFullScreenTimerHandle,
			nullptr,
			TimerCallbackFunc,
			this,
			2000,
			0,
			WT_EXECUTEINTIMERTHREAD);
	}

	return ret;
}

void CMpcVideoRenderer::EndFullScreenTimer()
{
	if (m_hFullScreenTimerHandle) {
		DeleteTimerQueueTimer(nullptr, m_hFullScreenTimerHandle, INVALID_HANDLE_VALUE);
		m_hFullScreenTimerHandle = nullptr;
	}
}

void CMpcVideoRenderer::SwitchFullScreen(const bool bCheck)
{
	if (bCheck) {
		if (m_bIsFullscreen) {
			return;
		}

		const HMONITOR hMon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { mi.cbSize = sizeof(mi) };
		::GetMonitorInfoW(hMon, &mi);
		const CRect rcMonitor(mi.rcMonitor);

		if (m_windowRect.Width() != rcMonitor.Width() || m_windowRect.Height() != rcMonitor.Height()) {
			return;
		}
	}

	DLog(L"CMpcVideoRenderer::SwitchFullScreen() : Switch to fullscreen");
	m_bIsFullscreen = true;

	if (m_hWnd) {
		Init(false);
		Redraw();

		if (m_hWndParentMain) {
			PostMessageW(m_hWndParentMain, WM_SWITCH_FULLSCREEN, 1, 0);
		}
	}
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CMpcVideoRenderer* pThis = (CMpcVideoRenderer*)GetWindowLongPtrW(hwnd, 0);
	if (!pThis) {
		if ((uMsg != WM_NCCREATE)
				|| (nullptr == (pThis = (CMpcVideoRenderer*)((LPCREATESTRUCTW)lParam)->lpCreateParams))) {
			return DefWindowProcW(hwnd, uMsg, wParam, lParam);
		}

		SetWindowLongPtrW(hwnd, 0, (LONG_PTR)pThis);
	}

	return pThis->OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

HRESULT CMpcVideoRenderer::Init(const bool bCreateWindow/* = false*/)
{
	CAutoLock cRendererLock(&m_RendererLock);

	HRESULT hr = S_OK;

	auto hwnd = m_hWndParent;
	while ((GetParent(hwnd)) && (GetParent(hwnd) == GetAncestor(hwnd, GA_PARENT))) {
		hwnd = GetParent(hwnd);
	}

	if (hwnd != m_hWndParentMain) {
		if (m_hWndParentMain) {
			RemoveParentWndProc(m_hWndParentMain);
		}

		m_hWndParentMain = hwnd;
		auto pfnOldProc = (WNDPROC)GetWindowLongPtrW(m_hWndParentMain, GWLP_WNDPROC);
		SetWindowLongPtrW(m_hWndParentMain, GWLP_WNDPROC, (LONG_PTR)ParentWndProc);
		SetPropW(m_hWndParentMain, g_pszOldParentWndProc, (HANDLE)pfnOldProc);
		SetPropW(m_hWndParentMain, g_pszThis, (HANDLE)this);
	}

	if (bCreateWindow) {
		if (m_hWndWindow) {
			::SendMessageW(m_hWndWindow, WM_CLOSE, 0, 0);
			m_hWndWindow = nullptr;
		}

		WNDCLASSEXW wc = { sizeof(wc) };
		if (!GetClassInfoExW(g_hInst, g_szClassName, &wc)) {
			wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
			wc.lpfnWndProc = WndProc;
			wc.hInstance = g_hInst;
			wc.lpszClassName = g_szClassName;
			wc.cbWndExtra = sizeof(this);
			if (!RegisterClassExW(&wc)) {
				hr = HRESULT_FROM_WIN32(GetLastError());
				DLog(L"CMpcVideoRenderer::Init() : RegisterClassExW() failed with error {}", HR2Str(hr));
				return hr;
			}
		}

		m_hWndWindow = CreateWindowExW(
			0,
			g_szClassName,
			nullptr,
			WS_VISIBLE | WS_CHILDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			m_hWndParent,
			nullptr,
			g_hInst,
			this
		);

		if (!m_hWndWindow) {
			hr = HRESULT_FROM_WIN32(GetLastError());
			DLog(L"CMpcVideoRenderer::Init() : CreateWindowExW() failed with error {}", HR2Str(hr));
			return E_FAIL;
		}

		if (!m_windowRect.IsRectNull()) {
			SetWindowPos(m_hWndWindow, nullptr, m_windowRect.left, m_windowRect.top, m_windowRect.Width(), m_windowRect.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}

	m_hWnd = m_bIsFullscreen ? m_hWndParentMain : m_hWndWindow;
	bool bChangeDevice = false;

	hr = m_VideoProcessor->Init(m_hWnd, &bChangeDevice);

	if (bChangeDevice) {
		DoAfterChangingDevice();
	}

	return hr;
}

// IVideoWindow
STDMETHODIMP CMpcVideoRenderer::put_Owner(OAHWND Owner)
{
	if (m_hWndParent != (HWND)Owner) {
		m_hWndParent = (HWND)Owner;
		return Init(true);
	}
	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::get_Owner(OAHWND *Owner)
{
	CheckPointer(Owner, E_POINTER);
	*Owner = (OAHWND)m_hWndParent;
	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::put_MessageDrain(OAHWND Drain)
{
	if (m_pInputPin == nullptr || m_pInputPin->IsConnected() == FALSE) {
		return VFW_E_NOT_CONNECTED;
	}
	m_hWndDrain = (HWND)Drain;
	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::get_MessageDrain(OAHWND* Drain)
{
	CheckPointer(Drain, E_POINTER);
	if (m_pInputPin == nullptr || m_pInputPin->IsConnected() == FALSE) {
		return VFW_E_NOT_CONNECTED;
	}
	*Drain = (OAHWND)m_hWndDrain;
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

	if (m_VideoProcessor->Type() == VP_DX9 && (m_Sets.bExclusiveFS || m_bIsFullscreen)) {
		const HMONITOR hMon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { mi.cbSize = sizeof(mi) };
		::GetMonitorInfoW(hMon, &mi);
		const CRect rcMonitor(mi.rcMonitor);

		EndFullScreenTimer();

		if (!m_bIsFullscreen && m_windowRect.Width() == rcMonitor.Width() && m_windowRect.Height() == rcMonitor.Height()) {
			if (m_Sets.bExclusiveDelay) {
				StartFullScreenTimer();
			} else {
				SwitchFullScreen(false);
			}
		} else if (m_bIsFullscreen && (m_windowRect.Width() != rcMonitor.Width() || m_windowRect.Height() != rcMonitor.Height())) {
			DLog(L"CMpcVideoRenderer::SetWindowPosition() : Switch from fullscreen");
			m_bIsFullscreen = false;

			if (m_hWnd) {
				Init(false);
				Redraw();

				if (m_hWndParentMain) {
					PostMessageW(m_hWndParentMain, WM_SWITCH_FULLSCREEN, 0, 0);
				}
			}
		}
	}

	if (m_hWndWindow && !m_bIsFullscreen) {
		SetWindowPos(m_hWndWindow, nullptr, Left, Top, Width, Height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
		if (Left < 0) {
			m_windowRect.OffsetRect(-Left, 0);
		}
		if (Top < 0) {
			m_windowRect.OffsetRect(0, -Top);
		}
	}

	m_VideoProcessor->SetWindowRect(m_windowRect);

	m_windowRect = windowRect;

	if (m_bForceRedrawing) {
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

STDMETHODIMP CMpcVideoRenderer::GetVideoProcessorInfo(std::wstring& str)
{
	return m_VideoProcessor->GetVPInfo(str);
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
	m_Sets.bUseD3D11       = setings.bUseD3D11;
	m_Sets.bExclusiveFS    = setings.bExclusiveFS;
	m_Sets.bExclusiveDelay = setings.bExclusiveDelay;

	CAutoLock cRendererLock(&m_RendererLock);

	if (setings.bShowStats != m_Sets.bShowStats) {
		m_VideoProcessor->SetShowStats(setings.bShowStats);
		m_Sets.bShowStats = setings.bShowStats;
	}

	if (setings.iResizeStats != m_Sets.iResizeStats) {
		// TODO
		m_Sets.iResizeStats = setings.iResizeStats;
	}

	if (setings.bDeintDouble != m_Sets.bDeintDouble) {
		m_VideoProcessor->SetDeintDouble(setings.bDeintDouble);
		m_Sets.bDeintDouble = setings.bDeintDouble;
	}

	if (setings.bVPScaling != m_Sets.bVPScaling) {
		m_VideoProcessor->SetVPScaling(setings.bVPScaling);
		m_Sets.bVPScaling = setings.bVPScaling;
	}

	if (setings.iChromaScaling != m_Sets.iChromaScaling) {
		m_VideoProcessor->SetChromaScaling(setings.iChromaScaling);
		m_Sets.iChromaScaling = setings.iChromaScaling;
	}

	if (setings.iUpscaling != m_Sets.iUpscaling) {
		m_VideoProcessor->SetUpscaling(setings.iUpscaling);
		m_Sets.iUpscaling = setings.iUpscaling;
	}

	if (setings.iDownscaling != m_Sets.iDownscaling) {
		m_VideoProcessor->SetDownscaling(setings.iDownscaling);
		m_Sets.iDownscaling = setings.iDownscaling;
	}

	if (setings.bInterpolateAt50pct != m_Sets.bInterpolateAt50pct) {
		m_VideoProcessor->SetInterpolateAt50pct(setings.bInterpolateAt50pct);
		m_Sets.bInterpolateAt50pct = setings.bInterpolateAt50pct;
	}

	if (setings.bUseDither != m_Sets.bUseDither) {
		m_VideoProcessor->SetDither(setings.bUseDither);
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
			m_VideoProcessor->SetTexFormat(m_Sets.iTextureFmt);
			m_VideoProcessor->SetVPEnableFmts(m_Sets.VPFmts);
			BOOL ret = m_VideoProcessor->InitMediaType(&m_inputMT);
		}
	}

	if (m_Sets.iSwapEffect != setings.iSwapEffect) {
		m_Sets.iSwapEffect = setings.iSwapEffect;
		if (m_hWnd) {
			m_VideoProcessor->SetSwapEffect(m_Sets.iSwapEffect);

			if (!m_bIsFullscreen) {
				Init(true);
				Redraw();
			}
		}
	}
}

STDMETHODIMP CMpcVideoRenderer::SaveSettings()
{
	CRegKey key;
	if (ERROR_SUCCESS == key.Create(HKEY_CURRENT_USER, OPT_REGKEY_VIDEORENDERER)) {
		key.SetDWORDValue(OPT_UseD3D11,            m_Sets.bUseD3D11);
		key.SetDWORDValue(OPT_ShowStatistics,      m_Sets.bShowStats);
		key.SetDWORDValue(OPT_ResizeStatistics,    m_Sets.iResizeStats);
		key.SetDWORDValue(OPT_TextureFormat,       m_Sets.iTextureFmt);
		key.SetDWORDValue(OPT_VPEnableNV12,        m_Sets.VPFmts.bNV12);
		key.SetDWORDValue(OPT_VPEnableP01x,        m_Sets.VPFmts.bP01x);
		key.SetDWORDValue(OPT_VPEnableYUY2,        m_Sets.VPFmts.bYUY2);
		key.SetDWORDValue(OPT_VPEnableOther,       m_Sets.VPFmts.bOther);
		key.SetDWORDValue(OPT_DoubleFrateDeint,    m_Sets.bDeintDouble);
		key.SetDWORDValue(OPT_VPScaling,           m_Sets.bVPScaling);
		key.SetDWORDValue(OPT_ChromaScaling,       m_Sets.iChromaScaling);
		key.SetDWORDValue(OPT_Upscaling,           m_Sets.iUpscaling);
		key.SetDWORDValue(OPT_Downscaling,         m_Sets.iDownscaling);
		key.SetDWORDValue(OPT_InterpolateAt50pct,  m_Sets.bInterpolateAt50pct);
		key.SetDWORDValue(OPT_Dither,              m_Sets.bUseDither);
		key.SetDWORDValue(OPT_SwapEffect,          m_Sets.iSwapEffect);
		key.SetDWORDValue(OPT_ExclusiveFullscreen, m_Sets.bExclusiveFS);
		key.SetDWORDValue(OPT_ExclusiveDelay,      m_Sets.bExclusiveDelay);
	}

	return S_OK;
}

// ISubRender
STDMETHODIMP CMpcVideoRenderer::SetCallback(ISubRenderCallback* cb)
{
	m_pSubCallBack = cb;

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

	if (!strcmp(field, "flip")) {
		*value = m_VideoProcessor->GetFlip();
		return S_OK;
	}

	return E_INVALIDARG;
}

STDMETHODIMP CMpcVideoRenderer::GetInt(LPCSTR field, int* value)
{
	CheckPointer(value, E_POINTER);

	if (!strcmp(field, "renderType")) {
		if (m_inputMT.IsValid()) {
			*value = m_VideoProcessor->Type();
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
		*value = m_VideoProcessor->GetRotation();
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

STDMETHODIMP CMpcVideoRenderer::GetBin(LPCSTR field, LPVOID* value, unsigned* size)
{
	if (!strcmp(field, "displayedImage")) {
		CAutoLock cRendererLock(&m_RendererLock);

		HRESULT hr = m_VideoProcessor->GetDisplayedImage((BYTE**)value, size);

		return hr;
	}

	return E_INVALIDARG;
}

STDMETHODIMP CMpcVideoRenderer::SetBool(LPCSTR field, bool value)
{
	if (!strcmp(field, "cmd_redraw") && value) {
		Redraw();
		return S_OK;
	}

	if (!strcmp(field, "cmd_clearPostScaleShaders") && value) {
		CAutoLock cRendererLock(&m_RendererLock);

		m_VideoProcessor->ClearPostScaleShaders();
		return S_OK;
	}

	if (!strcmp(field, "statsEnable")) {
		m_Sets.bShowStats = value;
		m_VideoProcessor->SetShowStats(m_Sets.bShowStats);

		SaveSettings();
		if (m_filterState == State_Paused) {
			Redraw();
		}
		return S_OK;
	}

	if (!strcmp(field, "lessRedraws")) {
		m_bForceRedrawing = !value;
		return S_OK;
	}

	if (!strcmp(field, "flip")) {
		CAutoLock cRendererLock(&m_RendererLock);

		m_VideoProcessor->SetFlip(value);
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

			m_VideoProcessor->SetRotation(value);
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
			std::wstring shaderName;
			std::string shaderCode;

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
					shaderName.assign((LPCWSTR)p, chunksize / sizeof(wchar_t));
					break;
				case FCC('CODE'):
					shaderCode.assign((LPCSTR)p, chunksize);
					break;
				}
				p += chunksize;
			}

			if (shaderCode.size()) {
				CAutoLock cRendererLock(&m_RendererLock);

				return m_VideoProcessor->AddPostScaleShader(shaderName, shaderCode);
			}
		}
	}

	return E_INVALIDARG;
}

HRESULT CMpcVideoRenderer::Redraw()
{
	CAutoLock cRendererLock(&m_RendererLock);
	const auto bDrawFrame = m_bValidBuffer && m_filterState != State_Stopped;

	HRESULT hr = S_OK;
	if (bDrawFrame) {
		hr = m_VideoProcessor->Render(0);
	} else {
		hr = m_VideoProcessor->FillBlack();
	}

	return hr;
}

void CMpcVideoRenderer::DoAfterChangingDevice()
{
	if (m_pInputPin->IsConnected() == TRUE && m_pSink) {
		DLog(L"CMpcVideoRenderer::DoAfterChangingDevice()");
		m_bValidBuffer = false;
		auto pPin = (IPin*)m_pInputPin;
		m_pInputPin->AddRef();
		EXECUTE_ASSERT(S_OK == m_pSink->Notify(EC_DISPLAY_CHANGED, (LONG_PTR)pPin, 0));
		SetAbortSignal(TRUE);
		SAFE_RELEASE(m_pMediaSample);
		m_pInputPin->Release();
	}
}

LRESULT CMpcVideoRenderer::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (m_hWndDrain && !InSendMessage() && !m_bIsFullscreen) {
		switch (uMsg) {
			case WM_CHAR:
			case WM_DEADCHAR:
			case WM_KEYDOWN:
			case WM_KEYUP:
			case WM_LBUTTONDBLCLK:
			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
			case WM_MBUTTONDBLCLK:
			case WM_MBUTTONDOWN:
			case WM_MBUTTONUP:
			case WM_MOUSEACTIVATE:
			case WM_MOUSEMOVE:
			case WM_NCLBUTTONDBLCLK:
			case WM_NCLBUTTONDOWN:
			case WM_NCLBUTTONUP:
			case WM_NCMBUTTONDBLCLK:
			case WM_NCMBUTTONDOWN:
			case WM_NCMBUTTONUP:
			case WM_NCMOUSEMOVE:
			case WM_NCRBUTTONDBLCLK:
			case WM_NCRBUTTONDOWN:
			case WM_NCRBUTTONUP:
			case WM_RBUTTONDBLCLK:
			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
			case WM_XBUTTONDOWN:
			case WM_XBUTTONUP:
			case WM_XBUTTONDBLCLK:
			case WM_MOUSEWHEEL:
			case WM_MOUSEHWHEEL:
			case WM_SYSCHAR:
			case WM_SYSDEADCHAR:
			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP:
				PostMessageW(m_hWndDrain, uMsg, wParam, lParam);
				return 0L;
		}
	}

	return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
