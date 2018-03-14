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
#include <d3d9.h>
#include <DXGI1_2.h>
#include <strmif.h>
#include "d3d11.h"

class CD3D11VideoProcessor
{
private:
	HMODULE m_hD3D11Lib = nullptr;
	CComPtr<ID3D11Device> m_pDevice;
	CComPtr<ID3D11DeviceContext> m_pImmediateContext;
	CComPtr<ID3D11VideoContext> m_pVideoContext;
	CComPtr<ID3D11VideoDevice> m_pVideoDevice;
	CComPtr<ID3D11VideoProcessor> m_pVideoProcessor;
	CComPtr<ID3D11VideoProcessorEnumerator> m_pVideoProcessorEnum;
	CComPtr<ID3D11Texture2D> m_pSrcTexture2D;
	CComPtr<ID3D11Texture2D> m_pSrcTexture2D_Decode;
	CComPtr<IDXGIFactory2> m_pDXGIFactory2;
	CComPtr<IDXGISwapChain> m_pDXGISwapChain;

	DXGI_FORMAT m_srcFormat = DXGI_FORMAT_UNKNOWN;
	GUID m_srcSubtype = GUID_NULL;
	UINT m_srcWidth = 0;
	UINT m_srcHeight = 0;

	D3D11_VIDEO_FRAME_FORMAT m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;

public:
	CD3D11VideoProcessor();
	~CD3D11VideoProcessor();

	HRESULT InitSwapChain(HWND hwnd, UINT width, UINT height);

	HRESULT IsMediaTypeSupported(const GUID subtype, const UINT width, const UINT height);
	HRESULT Initialize(const GUID subtype, const UINT width, const UINT height);

	HRESULT CopySample(IMediaSample* pSample, const AM_MEDIA_TYPE* pmt, IDirect3DDevice9Ex* pD3DDevEx, const bool bInterlaced);
	HRESULT Render();
};
