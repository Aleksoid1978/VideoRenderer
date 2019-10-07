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

#include <dxva2api.h>

// TODO
// DXVA2 Video Processor
class CDXVA2VP
{
private:
	CComPtr<IDirectXVideoProcessorService> m_pDXVA2_VPService;
	CComPtr<IDirectXVideoProcessor> m_pDXVA2_VP;
	GUID m_DXVA2VPGuid = GUID_NULL;

	DXVA2_VideoProcessorCaps m_DXVA2VPcaps = {};

	// ProcAmp
	DXVA2_ValueRange m_DXVA2ProcAmpRanges[4] = {};
	bool m_bUpdateFilters = false;

	D3DFORMAT m_srcFormat = D3DFMT_UNKNOWN;
	UINT m_srcWidth    = 0;
	UINT m_srcHeight   = 0;
	bool m_bInterlaced = false;

public:
	HRESULT InitVideoService(IDirect3DDevice9* pDevice);
	void ReleaseVideoService();

	HRESULT InitVideoProcessor(const D3DFORMAT inputFmt, const UINT width, const UINT height, const bool interlaced, D3DFORMAT& outputFmt);
	void ReleaseVideoProcessor();

	bool IsReady() { return (m_pDXVA2_VP != nullptr); }

	HRESULT SetInputSurface(IDirect3DSurface9* pTexture2D);
	HRESULT SetProcessParams(const RECT* pSrcRect, const RECT* pDstRect, const DXVA2_ExtendedFormat exFmt);
	void SetProcAmpValues(DXVA2_ProcAmpValues *pValues);

	HRESULT Process(IDirect3DSurface9* pRenderTarget, const DXVA2_SampleFormat sampleFormat, const bool second);
};
