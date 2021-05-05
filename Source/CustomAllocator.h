/*
* (C) 2018-2021 see Authors.txt
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

#include "MediaSampleSideData.h"

class CVideoRendererInputPin;

class CCustomMediaSample : public CMediaSampleSideData
{
public:
	CCustomMediaSample(LPCTSTR pName, CBaseAllocator *pAllocator, HRESULT *phr, LPBYTE pBuffer, LONG length);
	~CCustomMediaSample() = default;

	STDMETHODIMP_(ULONG) AddRef() { return __super::AddRef(); }
};

class CCustomAllocator : public CMemAllocator
{
protected:
	HRESULT Alloc();

	CVideoRendererInputPin* m_pVideoRendererInputPin = nullptr;
	CMediaType* m_pNewMT = nullptr;
	long m_cbBuffer = 0;

public:
	CCustomAllocator(LPCTSTR pName, LPUNKNOWN pUnk, CVideoRendererInputPin* pVideoRendererInputPin, HRESULT *phr);
	~CCustomAllocator();

	STDMETHODIMP SetProperties(__in ALLOCATOR_PROPERTIES* pRequest, __out ALLOCATOR_PROPERTIES* pActual);
	STDMETHODIMP GetBuffer(IMediaSample** ppBuffer, REFERENCE_TIME* pStartTime, REFERENCE_TIME* pEndTime, DWORD dwFlags);

	void SetNewMediaType(const CMediaType& mt);
	void ClearNewMediaType();
};
