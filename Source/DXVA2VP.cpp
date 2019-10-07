/*
* (C) 2019 see Authors.txt
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

#include "stdafx.h"
#include "Helper.h"
#include "DX9Helper.h"

#include "DXVA2VP.h"

// TODO
// CDXVA2VP

HRESULT CDXVA2VP::InitVideoService(IDirect3DDevice9* pDevice)
{
	// Create DXVA2 Video Processor Service.
	HRESULT hr = DXVA2CreateVideoService(pDevice, IID_IDirectXVideoProcessorService, (VOID**)&m_pDXVA2_VPService);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::Init() : DXVA2CreateVideoService() failed with error %s", HR2Str(hr));
		return FALSE;
	}

	return hr;
}

void CDXVA2VP::ReleaseVideoService()
{
	ReleaseVideoProcessor();

	m_pDXVA2_VPService.Release();
}

int GetBitDepth(const D3DFORMAT format)
{
	switch (format) {
	case D3DFMT_X8R8G8B8:
	case D3DFMT_A8R8G8B8:
	case D3DFMT_YV12:
	case D3DFMT_NV12:
	case D3DFMT_YUY2:
	case D3DFMT_AYUV:
	default:
		return 8;
	case D3DFMT_P010:
	case D3DFMT_P210:
	case D3DFMT_Y410:
		return 10;
	case D3DFMT_P016:
	case D3DFMT_P216:
	case D3DFMT_Y416:
		return 16;
	}
}

HRESULT CDXVA2VP::InitVideoProcessor(const D3DFORMAT inputFmt, const UINT width, const UINT height, const bool interlaced, D3DFORMAT& outputFmt)
{
	ReleaseVideoProcessor();
	HRESULT hr = S_OK;


	m_srcFormat   = inputFmt;
	m_srcWidth    = width;
	m_srcHeight   = height;
	m_bInterlaced = interlaced;

	return hr;
}

void CDXVA2VP::ReleaseVideoProcessor()
{
	m_pDXVA2_VP.Release();

	m_DXVA2VPcaps = {};

	m_srcFormat   = D3DFMT_UNKNOWN;
	m_srcWidth    = 0;
	m_srcHeight   = 0;
	m_bInterlaced = false;
}

HRESULT CDXVA2VP::SetInputSurface(IDirect3DSurface9* pTexture2D)
{
	CheckPointer(pTexture2D, E_POINTER);

	HRESULT hr = S_OK;

	return hr;
}

HRESULT CDXVA2VP::SetProcessParams(const RECT* pSrcRect, const RECT* pDstRect, const DXVA2_ExtendedFormat exFmt)
{
	return S_OK;
}

void CDXVA2VP::SetProcAmpValues(DXVA2_ProcAmpValues *pValues)
{
	m_bUpdateFilters = true;
}

HRESULT CDXVA2VP::Process(IDirect3DSurface9* pRenderTarget, const DXVA2_SampleFormat sampleFormat, const bool second)
{
	HRESULT hr = S_OK;

	return hr;
}

