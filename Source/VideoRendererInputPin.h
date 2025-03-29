/*
 * (C) 2018-2025 see Authors.txt
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
#include <mfidl.h>
#include <memory>
#include "../Include/ID3DVideoMemoryConfiguration.h"

class CMpcVideoRenderer;
class CCustomAllocator;

class CVideoRendererInputPin
	: public CRendererInputPin
	, public IMFGetService
	, public IDirectXVideoMemoryConfiguration
	, public ID3D11DecoderConfiguration
{
private:
	friend class CCustomAllocator;

	CMpcVideoRenderer* m_pBaseRenderer;
	bool m_bDXVA = false;
	bool m_bD3D11 = false;

	CCustomAllocator* m_pCustomAllocator = nullptr;
	std::unique_ptr<CMediaType> m_pNewMT;

	CCritSec m_csReceive;

public:
	CVideoRendererInputPin(CBaseRenderer *pRenderer, HRESULT *phr, LPCWSTR Name, CMpcVideoRenderer* pBaseRenderer);
	~CVideoRendererInputPin() = default;

	bool FrameInVideoMem() { return m_bDXVA || m_bD3D11; }

	// CUnknown
	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	// CBaseInputPin
	STDMETHODIMP GetAllocator(IMemAllocator **ppAllocator);
	STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES* pProps);
	STDMETHODIMP ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt);

	STDMETHODIMP NewSegment(REFERENCE_TIME startTime, REFERENCE_TIME stopTime, double rate) override;
	STDMETHODIMP BeginFlush() override;

	// IMFGetService
	STDMETHODIMP GetService(REFGUID guidService, REFIID riid, LPVOID *ppvObject);

	// IDirectXVideoMemoryConfiguration
	STDMETHODIMP GetAvailableSurfaceTypeByIndex(DWORD dwTypeIndex, DXVA2_SurfaceType *pdwType);
	STDMETHODIMP SetSurfaceType(DXVA2_SurfaceType dwType);

	// ID3D11DecoderConfiguration
	STDMETHODIMP ActivateD3D11Decoding(ID3D11Device *pDevice, ID3D11DeviceContext *pContext, HANDLE hMutex, UINT nFlags);
	UINT STDMETHODCALLTYPE GetD3D11AdapterIndex();

	void SetNewMediaType(const CMediaType& mt);
	void ClearNewMediaType();
};
