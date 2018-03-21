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
#include <VersionHelpers.h>
#include <evr.h> // for MR_VIDEO_ACCELERATION_SERVICE, because the <mfapi.h> does not contain it
#include <Mferror.h>
#include <Dvdmedia.h>
#include "Helper.h"
#include "PropPage.h"
#include "./Include/ID3DVideoMemoryConfiguration.h"

#define OPT_REGKEY_VIDEORENDERER L"Software\\MPC-BE Filters\\MPC Video Renderer"
#define OPT_UseD3D11             L"UseD3D11"
#define OPT_DoubleFrateDeint     L"DoubleFramerateDeinterlace"

class CVideoRendererInputPin : public CRendererInputPin
	, public IMFGetService
	, public IDirectXVideoMemoryConfiguration
	, public ID3D11DecoderConfiguration
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

	// ID3D11DecoderConfiguration
	STDMETHODIMP ActivateD3D11Decoding(ID3D11Device *pDevice, ID3D11DeviceContext *pContext, HANDLE hMutex, UINT nFlags);
	UINT STDMETHODCALLTYPE GetD3D11AdapterIndex();

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
		(riid == __uuidof(ID3D11DecoderConfiguration)) ? GetInterface((ID3D11DecoderConfiguration*)this, ppv) :
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

// ID3D11DecoderConfiguration
STDMETHODIMP CVideoRendererInputPin::ActivateD3D11Decoding(ID3D11Device *pDevice, ID3D11DeviceContext *pContext, HANDLE hMutex, UINT nFlags)
{
	return m_pBaseRenderer->m_bUsedD3D11 ? m_pBaseRenderer->m_DX11_VP.SetDevice(pDevice, pContext) : E_FAIL;
}

UINT STDMETHODCALLTYPE CVideoRendererInputPin::GetD3D11AdapterIndex()
{
	return 0;
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

	CRegKey key;
	if (ERROR_SUCCESS == key.Open(HKEY_CURRENT_USER, OPT_REGKEY_VIDEORENDERER, KEY_READ)) {
		DWORD dw;
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_UseD3D11, dw)) {
			m_bOptionUseD3D11 = !!dw;
		}
		if (ERROR_SUCCESS == key.QueryDWORDValue(OPT_DoubleFrateDeint, dw)) {
			m_bOptionDoubleFrateDeint = !!dw;
		}
	}

	*phr = m_DX9_VP.Init(m_hWnd);
	if (FAILED(*phr)) {
		return;
	}

	m_bUsedD3D11 = m_bOptionUseD3D11 && IsWindows8OrGreater();
	if (m_bUsedD3D11) {
		if (FAILED(m_DX11_VP.Init())) {
			m_bUsedD3D11 = false;
		}
	}
}

CMpcVideoRenderer::~CMpcVideoRenderer()
{
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

		if (m_bUsedD3D11) {
			if (!m_DX11_VP.InitMediaType(pmt)) {
				return VFW_E_UNSUPPORTED_VIDEO;
			}
		} else {
			if (!m_DX9_VP.InitMediaType(pmt)) {
				return VFW_E_UNSUPPORTED_VIDEO;
			}
		}
	}

	return hr;
}

HRESULT CMpcVideoRenderer::DoRenderSample(IMediaSample* pSample)
{
	CheckPointer(pSample, E_POINTER);
	std::unique_lock<std::mutex> lock(m_mutex);

	HRESULT hr = S_OK;

	if (m_bUsedD3D11) {
		hr = m_DX11_VP.CopySample(pSample);
	} else {
		hr = m_DX9_VP.CopySample(pSample);
	}
	if (FAILED(hr)) {
		return hr;
	}

	if (m_bUsedD3D11) {
		return m_DX11_VP.Render(m_filterState);
	} else {
		return m_DX9_VP.Render(m_filterState);
	}
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

	if (!m_bUsedD3D11) {
		m_DX9_VP.StopInputBuffer();
	}

	m_filterState = State_Stopped;

	return CBaseRenderer::Stop();
}

// IMFGetService
STDMETHODIMP CMpcVideoRenderer::GetService(REFGUID guidService, REFIID riid, LPVOID *ppvObject)
{
	if (guidService == MR_VIDEO_ACCELERATION_SERVICE) {
		if (riid == __uuidof(IDirect3DDeviceManager9)) {
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

	return E_NOINTERFACE;
}

// IBasicVideo
STDMETHODIMP CMpcVideoRenderer::SetDestinationPosition(long Left, long Top, long Width, long Height)
{
	CRect videoRect(Left, Top, Left + Width, Top + Height);

	std::unique_lock<std::mutex> lock(m_mutex);
	if (m_bUsedD3D11) {
		m_DX11_VP.SetVideoRect(videoRect);
		m_DX11_VP.Render(m_filterState);
	} else {
		m_DX9_VP.SetVideoRect(videoRect);
		m_DX9_VP.Render(m_filterState);
	}

	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::GetVideoSize(long *pWidth, long *pHeight)
{
	if (m_bUsedD3D11) {
		return m_DX11_VP.GetVideoSize(pWidth, pHeight);
	} else {
		return m_DX9_VP.GetVideoSize(pWidth, pHeight);
	}
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
		HRESULT hr = m_DX9_VP.Init(m_hWnd);
		if (S_OK == hr && m_bUsedD3D11) {
			hr = m_DX11_VP.InitSwapChain(m_hWnd);
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

	std::unique_lock<std::mutex> lock(m_mutex);
	if (m_bUsedD3D11) {
		m_DX11_VP.SetWindowRect(windowRect);
		m_DX11_VP.Render(m_filterState);
	} else {
		m_DX9_VP.SetWindowRect(windowRect);
		m_DX9_VP.Render(m_filterState);
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

STDMETHODIMP_(bool) CMpcVideoRenderer::GetOptionUseD3D11()
{
	return m_bOptionUseD3D11;
}

STDMETHODIMP CMpcVideoRenderer::SetOptionUseD3D11(bool value)
{
	m_bOptionUseD3D11 = value;
	return S_OK;
}

STDMETHODIMP_(bool) CMpcVideoRenderer::GetOptionDoubleFrateDeint()
{
	return m_bOptionDoubleFrateDeint;
}

STDMETHODIMP CMpcVideoRenderer::SetOptionDoubleFrateDeint(bool value)
{
	m_bOptionDoubleFrateDeint = value;
	return S_OK;
}

STDMETHODIMP CMpcVideoRenderer::SaveSettings()
{
	CRegKey key;
	if (ERROR_SUCCESS == key.Create(HKEY_CURRENT_USER, OPT_REGKEY_VIDEORENDERER)) {
		key.SetDWORDValue(OPT_UseD3D11, m_bOptionUseD3D11);
		key.SetDWORDValue(OPT_DoubleFrateDeint, m_bOptionDoubleFrateDeint);
	}

	return S_OK;
}
