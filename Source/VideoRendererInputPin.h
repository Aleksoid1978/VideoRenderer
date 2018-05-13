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

#pragma once

#include <d3d11.h>
#include "./Include/ID3DVideoMemoryConfiguration.h"

class CMpcVideoRenderer;

class CVideoRendererInputPin
	: public CRendererInputPin
	, public IMFGetService
	, public IDirectXVideoMemoryConfiguration
	, public ID3D11DecoderConfiguration
{
private:
	CMpcVideoRenderer* m_pBaseRenderer;

public:
	CVideoRendererInputPin(CBaseRenderer *pRenderer, HRESULT *phr, LPCWSTR Name, CMpcVideoRenderer* pBaseRenderer);

	// CUnknown
	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	// CBaseInputPin
	STDMETHODIMP GetAllocator(IMemAllocator **ppAllocator);
	STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES* pProps);

	// IMFGetService
	STDMETHODIMP GetService(REFGUID guidService, REFIID riid, LPVOID *ppvObject);

	// IDirectXVideoMemoryConfiguration
	STDMETHODIMP GetAvailableSurfaceTypeByIndex(DWORD dwTypeIndex, DXVA2_SurfaceType *pdwType);
	STDMETHODIMP SetSurfaceType(DXVA2_SurfaceType dwType);

	// ID3D11DecoderConfiguration
	STDMETHODIMP ActivateD3D11Decoding(ID3D11Device *pDevice, ID3D11DeviceContext *pContext, HANDLE hMutex, UINT nFlags);
	UINT STDMETHODCALLTYPE GetD3D11AdapterIndex();
};

