/*
 * (C) 2018-2019 see Authors.txt
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
#include <VersionHelpers.h>
#include <evr.h> // for MR_VIDEO_ACCELERATION_SERVICE, because the <mfapi.h> does not contain it
#include <Mferror.h>
#include "Helper.h"
#include "PropPage.h"
#include "VideoRendererInputPin.h"
#include "VideoRenderer.h"

#define OPT_REGKEY_VIDEORENDERER L"Software\\MPC-BE Filters\\MPC Video Renderer"
#define OPT_UseD3D11             L"UseD3D11"
#define OPT_ShowStatistics       L"ShowStatistics"
#define OPT_DoubleFrateDeint     L"DoubleFramerateDeinterlace"
#define OPT_SurfaceFormat        L"SurfaceFormat"
#define OPT_Upscaling            L"Upscaling"
#define OPT_Downscaling          L"Downscaling"

//
// CMpcVideoRenderer
//

CMpcVideoRenderer::CMpcVideoRenderer(LPUNKNOWN pUnk, HRESULT* phr)
	: CBaseVideoRenderer2(__uuidof(this), L"MPC Video Renderer", pUnk, phr)
	, m_DX9_VP(this)
	, m_DX11_VP(this)
{
#ifdef DEBUG
	DbgSetModuleLevel(LOG_TRACE, DWORD_MAX);
	DbgSetModuleLevel(LOG_ERROR, DWORD_MAX);
#endif

	ASSERT(S_OK == *phr);
	m_pInputPin = new CVideoRendererInputPin(this, phr, L"In", this);
	ASSERT(S_OK == *phr);

	CRegKey key;
	if (ERROR_SUCCESS == key.Open(HKEY_CURRENT_USER, OPT_REGKEY_VIDEORENDERER, KEY_READ)) {
		DWORD dw;
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_UseD3D11, dw)) {
			m_bOptionUseD3D11 = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_ShowStatistics, dw)) {
			m_bOptionShowStats = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_DoubleFrateDeint, dw)) {
			m_bOptionDeintDouble = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_SurfaceFormat, dw)) {
			m_iOptionSurfaceFmt = discard((int)dw, (int)SURFMT_8INT, (int)SURFMT_8INT, (int)SURFMT_16FLOAT);
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_Upscaling, dw)) {
			m_iOptionUpscaling = discard((int)dw, (int)UPSCALE_CatmullRom, (int)UPSCALE_CatmullRom, (int)UPSCALE_Lanczos2);
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_Downscaling, dw)) {
			m_iOptionDownscaling = discard((int)dw, (int)DOWNSCALE_Hamming, (int)DOWNSCALE_Box, (int)DOWNSCALE_Lanczos);
		}
	}

	m_bUsedD3D11 = m_bOptionUseD3D11 && IsWindows8OrGreater();
	if (m_bUsedD3D11) {
		*phr = m_DX11_VP.Init(m_iOptionSurfaceFmt);
		if (S_OK == *phr) {
			m_DX11_VP.SetShowStats(m_bOptionShowStats);
			m_DX11_VP.SetDeintDouble(m_bOptionDeintDouble);
			DLog(L"Direct3D11 initialization successfully!");
			return;
		}
		m_bUsedD3D11 = false;
	}

	*phr = m_DX9_VP.Init(m_hWnd, m_iOptionSurfaceFmt, nullptr);
	if (S_OK == *phr) {
		m_DX9_VP.SetShowStats(m_bOptionShowStats);
		m_DX9_VP.SetDeintDouble(m_bOptionDeintDouble);
		m_DX9_VP.SetUpscaling(m_iOptionUpscaling);
		m_DX9_VP.SetDownscaling(m_iOptionDownscaling);
	}

	return;
}

CMpcVideoRenderer::~CMpcVideoRenderer()
{
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
	DLog(L"CMpcVideoRenderer::SetMediaType()");

	CheckPointer(pmt, E_POINTER);
	CheckPointer(pmt->pbFormat, E_POINTER);

	CAutoLock cVideoLock(&m_InterfaceLock);
	CAutoLock cRendererLock(&m_RendererLock);

	if (m_bUsedD3D11) {
		if (!m_DX11_VP.InitMediaType(pmt)) {
			return VFW_E_UNSUPPORTED_VIDEO;
		}
	} else {
		if (!m_DX9_VP.InitMediaType(pmt)) {
			return VFW_E_UNSUPPORTED_VIDEO;
		}
	}

	return S_OK;
}

HRESULT CMpcVideoRenderer::DoRenderSample(IMediaSample* pSample)
{
	CheckPointer(pSample, E_POINTER);
	CAutoLock cRendererLock(&m_RendererLock);

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
			OnReceiveFirstSample(pSample);
		}
		Ready();
	}

	if (m_State == State_Paused) {
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

	HRESULT hr;
	if (riid == __uuidof(IKsPropertySet)) {
		hr = GetInterface((IKsPropertySet*)this, ppv);
	}
	else if (riid == __uuidof(IMFGetService)) {
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
	if (m_bUsedD3D11) {
		m_DX11_VP.Start();
	} else {
		m_DX9_VP.Start();
	}

	return CBaseVideoRenderer2::Run(rtStart);
}

STDMETHODIMP CMpcVideoRenderer::Stop()
{
	DLog(L"CMpcVideoRenderer::Stop()");

	if (m_bUsedD3D11) {
		m_DX11_VP.Stop();
	} else {
		m_DX9_VP.Stop();
	}

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
		if (riid == __uuidof(IDirect3DDeviceManager9) && !m_bUsedD3D11) {
			return m_DX9_VP.GetDeviceManager9()->QueryInterface(riid, ppvObject);
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
	CRect videoRect(Left, Top, Left + Width, Top + Height);

	CAutoLock cRendererLock(&m_RendererLock);
	if (m_bUsedD3D11) {
		m_DX11_VP.SetVideoRect(videoRect);
		if (m_filterState != State_Stopped) {
			m_DX11_VP.Render(0);
		} else {
			m_DX11_VP.FillBlack();
		}
	} else {
		m_DX9_VP.SetVideoRect(videoRect);
		if (m_filterState != State_Stopped) {
			m_DX9_VP.Render(0);
		} else {
			m_DX9_VP.FillBlack();
		}
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
	if (m_hWnd != (HWND)Owner) {
		m_hWnd = (HWND)Owner;
		HRESULT hr;

		if (m_bUsedD3D11) {
			hr = m_DX11_VP.InitSwapChain(m_hWnd);
		} else {
			bool bChangeDevice = false;
			hr = m_DX9_VP.Init(m_hWnd, m_iOptionSurfaceFmt, &bChangeDevice);

			if (bChangeDevice) {
				OnDisplayChange();
			}
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
	CRect windowRect(Left, Top, Left + Width, Top + Height);

	CAutoLock cRendererLock(&m_RendererLock);
	if (m_bUsedD3D11) {
		m_DX11_VP.InitSwapChain(m_hWnd, windowRect.Width(), windowRect.Height());
		m_DX11_VP.SetWindowRect(windowRect);
		if (m_filterState != State_Stopped) {
			m_DX11_VP.Render(0);
		} else {
			m_DX11_VP.FillBlack();
		}
	} else {
		m_DX9_VP.SetWindowRect(windowRect);
		if (m_filterState != State_Stopped) {
			m_DX9_VP.Render(0);
		} else {
			m_DX9_VP.FillBlack();
		}
	}

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

STDMETHODIMP CMpcVideoRenderer::get_AdapterDecription(CStringW& str)
{
	if (m_bUsedD3D11) {
		return m_DX11_VP.GetAdapterDecription(str);
	} else {
		return m_DX9_VP.GetAdapterDecription(str);
	}
}

STDMETHODIMP CMpcVideoRenderer::get_FrameInfo(VRFrameInfo* pFrameInfo)
{
	if (m_bUsedD3D11) {
		return m_DX11_VP.GetFrameInfo(pFrameInfo);
	} else {
		return m_DX9_VP.GetFrameInfo(pFrameInfo);
	}
}

STDMETHODIMP CMpcVideoRenderer::get_DXVA2VPCaps(DXVA2_VideoProcessorCaps* pDXVA2VPCaps)
{
	if (m_bUsedD3D11) {
		return E_NOTIMPL;
	} else {
		return m_DX9_VP.GetDXVA2VPCaps(pDXVA2VPCaps);
	}
}

STDMETHODIMP_(bool) CMpcVideoRenderer::GetActive()
{
	return m_pInputPin && m_pInputPin->GetConnected();
}

STDMETHODIMP_(void) CMpcVideoRenderer::GetSettings(
	bool &bUseD3D11,
	bool &bShowStats,
	bool &bDeintDouble,
	int  &iSurfaceFmt,
	int  &iUpscaling,
	int  &iDownscaling)
{
	bUseD3D11    = m_bOptionUseD3D11;
	bShowStats   = m_bOptionShowStats;
	bDeintDouble = m_bOptionDeintDouble;
	iSurfaceFmt  = m_iOptionSurfaceFmt;
	iUpscaling   = m_iOptionUpscaling;
	iDownscaling = m_iOptionDownscaling;
}

STDMETHODIMP_(void) CMpcVideoRenderer::SetSettings(
	bool bUseD3D11,
	bool bShowStats,
	bool bDeintDouble,
	int  iSurfaceFmt,
	int  iUpscaling,
	int  iDownscaling)
{
	m_bOptionUseD3D11 = bUseD3D11;
	m_iOptionSurfaceFmt = iSurfaceFmt;

	if (bShowStats != m_bOptionShowStats) {
		m_DX11_VP.SetShowStats(bShowStats);
		m_DX9_VP.SetShowStats(bShowStats);
		m_bOptionShowStats = bShowStats;
	}

	if (bDeintDouble != m_bOptionDeintDouble) {
		m_DX11_VP.SetDeintDouble(bDeintDouble);
		m_DX9_VP.SetDeintDouble(bDeintDouble);
		m_bOptionDeintDouble = bDeintDouble;
	}

	if (iUpscaling != m_iOptionUpscaling) {
		m_DX9_VP.SetUpscaling(iUpscaling);
		m_iOptionUpscaling = iUpscaling;
	}

	if (iDownscaling != m_iOptionDownscaling) {
		m_DX9_VP.SetDownscaling(iDownscaling);
		m_iOptionDownscaling = iDownscaling;
	}
}

STDMETHODIMP CMpcVideoRenderer::SaveSettings()
{
	CRegKey key;
	if (ERROR_SUCCESS == key.Create(HKEY_CURRENT_USER, OPT_REGKEY_VIDEORENDERER)) {
		key.SetDWORDValue(OPT_UseD3D11,         m_bOptionUseD3D11);
		key.SetDWORDValue(OPT_ShowStatistics,   m_bOptionShowStats);
		key.SetDWORDValue(OPT_DoubleFrateDeint, m_bOptionDeintDouble);
		key.SetDWORDValue(OPT_SurfaceFormat,    m_iOptionSurfaceFmt);
		key.SetDWORDValue(OPT_Upscaling,        m_iOptionUpscaling);
		key.SetDWORDValue(OPT_Downscaling,      m_iOptionDownscaling);
	}

	return S_OK;
}
