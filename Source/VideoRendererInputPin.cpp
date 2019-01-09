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
#include <Mferror.h>
#include "VideoRenderer.h"
#include "VideoRendererInputPin.h"
#include "CustomAllocator.h"

//
// CVideoRendererInputPin
//

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

STDMETHODIMP CVideoRendererInputPin::GetAllocator(IMemAllocator **ppAllocator)
{
	DLog(L"CVideoRendererInputPin::GetAllocator()");

	if (m_bDXVA || m_bD3D11) {
		// Renderer shouldn't manage allocator for DXVA/D3D11
		return E_NOTIMPL;
	}

	CheckPointer(ppAllocator, E_POINTER);

	if (m_pAllocator) {
		// We already have an allocator, so return that one.
		*ppAllocator = m_pAllocator;
		(*ppAllocator)->AddRef();
		return S_OK;
	}

	// No allocator yet, so propose our custom allocator.
	HRESULT hr = S_OK;
	CCustomAllocator *pAlloc = new CCustomAllocator(L"Custom allocator", nullptr, &hr);
	if (!pAlloc) {
		return E_OUTOFMEMORY;
	}
	if (FAILED(hr)) {
		delete pAlloc;
		return hr;
	}

	// Return the IMemAllocator interface to the caller.
	return pAlloc->QueryInterface(IID_IMemAllocator, (void**)ppAllocator);
}

STDMETHODIMP CVideoRendererInputPin::GetAllocatorRequirements(ALLOCATOR_PROPERTIES* pProps)
{
	// 1 buffer required
	ZeroMemory(pProps, sizeof(ALLOCATOR_PROPERTIES));
	pProps->cbBuffer = 1;
	return S_OK;
}

STDMETHODIMP CVideoRendererInputPin::ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt)
{
	DLog(L"CVideoRendererInputPin::ReceiveConnection()");

	CAutoLock cObjectLock(m_pLock);

	if (m_Connected) {
		CMediaType mt(*pmt);

		if (FAILED(CheckMediaType(&mt))) {
			return VFW_E_TYPE_NOT_ACCEPTED;
		}

		ALLOCATOR_PROPERTIES props, actual;

		CComPtr<IMemAllocator> pMemAllocator;
		if (FAILED(GetAllocator(&pMemAllocator))
				|| FAILED(pMemAllocator->Decommit())
				|| FAILED(pMemAllocator->GetProperties(&props))) {
			return E_FAIL;
		}

		DWORD biSizeImage = 0;
		if (pmt->formattype == FORMAT_VideoInfo2) {
			const auto vih = (VIDEOINFOHEADER2*)pmt->pbFormat;
			const auto& bih = vih->bmiHeader;
			biSizeImage = bih.biSizeImage ? bih.biSizeImage : DIBSIZE(bih);
		} else if (pmt->formattype == FORMAT_VideoInfo) {
			const auto vih = (VIDEOINFOHEADER*)pmt->pbFormat;
			const auto& bih = vih->bmiHeader;
			biSizeImage = bih.biSizeImage ? bih.biSizeImage : DIBSIZE(bih);
		} else {
			return VFW_E_TYPE_NOT_ACCEPTED;
		}

		if (biSizeImage) {
			props.cbBuffer = biSizeImage;
		}

		if (FAILED(pMemAllocator->SetProperties(&props, &actual))
				|| FAILED(pMemAllocator->Commit())
				|| props.cbBuffer != actual.cbBuffer) {
			return E_FAIL;
		}

		return SetMediaType(&mt) == S_OK
			? S_OK
			: VFW_E_TYPE_NOT_ACCEPTED;
	}

	return __super::ReceiveConnection(pConnector, pmt);
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
	m_bDXVA = (dwType == DXVA2_SurfaceType_DecoderRenderTarget);
	return S_OK;
}

// ID3D11DecoderConfiguration
STDMETHODIMP CVideoRendererInputPin::ActivateD3D11Decoding(ID3D11Device *pDevice, ID3D11DeviceContext *pContext, HANDLE hMutex, UINT nFlags)
{
	const auto hr = m_pBaseRenderer->m_bUsedD3D11 ? m_pBaseRenderer->m_DX11_VP.SetDevice(pDevice, pContext) : E_FAIL;
	m_bD3D11 = (hr == S_OK);
	return hr;
}

UINT STDMETHODCALLTYPE CVideoRendererInputPin::GetD3D11AdapterIndex()
{
	return 0;
}
