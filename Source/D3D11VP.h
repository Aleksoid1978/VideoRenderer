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

#include <d3d11.h>

class CD3D11VP
{
private:
	// D3D11 Video Processor
	CComPtr<ID3D11VideoContext> m_pVideoContext;
	CComPtr<ID3D11VideoDevice> m_pVideoDevice;
	CComPtr<ID3D11VideoProcessor> m_pVideoProcessor;
	CComPtr<ID3D11VideoProcessorEnumerator> m_pVideoProcessorEnum;
	CComPtr<ID3D11VideoProcessorInputView> m_pInputView;

	CComPtr<ID3D11PixelShader> m_pPSCorrection;

	D3D11_VIDEO_PROCESSOR_CAPS m_VPCaps = {};

	// ProcAmp
	D3D11_VIDEO_PROCESSOR_FILTER_RANGE m_VPFilterRange[4] = {};
	int m_VPFilterLevels[4] = {};
	bool m_bUpdateFilters = false;

	DXGI_FORMAT m_srcFormat = DXGI_FORMAT_UNKNOWN;
	UINT m_srcWidth    = 0;
	UINT m_srcHeight   = 0;
	bool m_bInterlaced = false;

public:
	HRESULT InitVideDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext);
	void ReleaseVideoDevice();

	HRESULT InitVideoProcessor(const DXGI_FORMAT inputFmt, const UINT width, const UINT height, const bool interlaced, DXGI_FORMAT& outputFmt);
	void ReleaseVideoProcessor();

	bool IsReady() { return (m_pVideoProcessor != nullptr); }

	HRESULT SetInputTexture(ID3D11Texture2D* pTexture2D);
	HRESULT SetProcessParams(const RECT* pSrcRect, const RECT* pDstRect, const DXVA2_ExtendedFormat exFmt);
	void SetProcAmpValues(DXVA2_ProcAmpValues *pValues);

	HRESULT Process(ID3D11Texture2D* pRenderTarget, const D3D11_VIDEO_FRAME_FORMAT sampleFormat, const bool second);


};
