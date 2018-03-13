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

#include <atltypes.h>
#include <strmif.h>
#include "d3d11.h"

class CD3D11VideoProcessor
{
private:
	HMODULE m_hD3D11Lib = nullptr;
	CComPtr<ID3D11Device> m_pDevice;
	CComPtr<ID3D11VideoDevice> m_pVideoDevice;
	CComPtr<ID3D11VideoProcessor> m_pVideoProcessor;
	CComPtr<ID3D11Texture2D> m_pSrcTexture2D;

	DXGI_FORMAT m_srcFormat = DXGI_FORMAT_UNKNOWN;
	UINT m_srcWidth = 0;
	UINT m_srcHeight = 0;

public:
	CD3D11VideoProcessor();
	~CD3D11VideoProcessor();

	HRESULT IsMediaTypeSupported(const GUID subtype, const UINT width, const UINT height);
	HRESULT Initialize(const GUID subtype, const UINT width, const UINT height);

	HRESULT CopySample(IMediaSample* pSample, const AM_MEDIA_TYPE* pmt);
};
