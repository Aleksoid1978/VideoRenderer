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
#include <Mferror.h>
#include "VideoRenderer.h"
#include "VideoRendererInputPin.h"

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

STDMETHODIMP CVideoRendererInputPin::GetAllocator(IMemAllocator **ppAllocator) {
	// Renderer shouldn't manage allocator for DXVA
	return E_NOTIMPL;
}

STDMETHODIMP CVideoRendererInputPin::GetAllocatorRequirements(ALLOCATOR_PROPERTIES* pProps) {
	// 1 buffer required
	ZeroMemory(pProps, sizeof(ALLOCATOR_PROPERTIES));
	pProps->cbBuffer = 1;
	return S_OK;
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
