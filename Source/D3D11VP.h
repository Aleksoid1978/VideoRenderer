/*
* (C) 2019-2023 see Authors.txt
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

class VideoTextureBuffer
{
private:
	std::vector<ID3D11Texture2D*> m_Textures;
	std::vector<ID3D11VideoProcessorInputView*> m_InputViews;

	void ReleaseTextures() {
		for (auto& inputview : m_InputViews) {
			SAFE_RELEASE(inputview);
		}
		for (auto& texture : m_Textures) {
			SAFE_RELEASE(texture);
		}
	}

public:
	~VideoTextureBuffer() {
		ReleaseTextures();
	}

	const UINT Size() {
		return m_Textures.size();
	}

	void Clear() {
		ReleaseTextures();
		m_InputViews.clear();
		m_Textures.clear();
	}

	void Resize(const unsigned len) {
		Clear();
		if (len) {
			m_Textures.resize(len);
			m_InputViews.resize(len);
		}
	}

	void Rotate() {
		ASSERT(m_Textures.size());

		if (m_Textures.size() > 1) {
			ID3D11Texture2D* pSurface = m_Textures.front();
			ID3D11VideoProcessorInputView* pInputView = m_InputViews.front();

			for (size_t i = 1; i < m_Textures.size(); i++) {
				auto pre = i - 1;
				m_Textures[pre] = m_Textures[i];
				m_InputViews[pre] = m_InputViews[i];
			}

			m_Textures.back() = pSurface;
			m_InputViews.back() = pInputView;
		}
	}

	ID3D11Texture2D** GetTexture()
	{
		if (m_Textures.size()) {
			return &m_Textures.back();
		} else {
			return nullptr;
		}
	}

	ID3D11Texture2D** GetTexture(UINT num)
	{
		if (num < m_Textures.size()) {
			return &m_Textures[num];
		} else {
			return nullptr;
		}
	}

	ID3D11VideoProcessorInputView** GetInputView(UINT num)
	{
		if (num < m_InputViews.size()) {
			return &m_InputViews[num];
		} else {
			return nullptr;
		}
	}
};

// D3D11 Video Processor
class CD3D11VP
{
private:
	CComPtr<ID3D11VideoDevice> m_pVideoDevice;
	CComPtr<ID3D11VideoProcessor> m_pVideoProcessor;

	CComPtr<ID3D11VideoContext> m_pVideoContext;
	CComPtr<ID3D11VideoProcessorEnumerator> m_pVideoProcessorEnum;

	CComPtr<ID3D11VideoContext1> m_pVideoContext1;
	CComPtr<ID3D11VideoProcessorEnumerator1> m_pVideoProcessorEnum1;
	BOOL m_bExConvSupported = FALSE;

	D3D11_VIDEO_PROCESSOR_CAPS m_VPCaps = {};
	UINT m_RateConvIndex = 0;
	D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS m_RateConvCaps = {};

	VideoTextureBuffer m_VideoTextures;
	UINT m_nInputFrameOrField = 0;
	bool m_bPresentFrame      = false;
	UINT m_nPastFrames        = 0;
	UINT m_nFutureFrames      = 0;

	// Filters
	struct {
		int support;
		int value;
		D3D11_VIDEO_PROCESSOR_FILTER_RANGE range;
	} m_VPFilters[8] = {};
	bool m_bUpdateFilters = false;

	D3D11_VIDEO_PROCESSOR_ROTATION m_Rotation = D3D11_VIDEO_PROCESSOR_ROTATION_IDENTITY;

	DXGI_FORMAT m_srcFormat = DXGI_FORMAT_UNKNOWN;
	UINT m_srcWidth    = 0;
	UINT m_srcHeight   = 0;
	//bool m_bInterlaced = false;
	DXGI_FORMAT m_dstFormat = DXGI_FORMAT_UNKNOWN;

public:
	HRESULT InitVideoDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext);
	void ReleaseVideoDevice();

	HRESULT InitVideoProcessor(const DXGI_FORMAT inputFmt, const UINT width, const UINT height, const bool interlaced, DXGI_FORMAT& outputFmt);
	void ReleaseVideoProcessor();

	HRESULT InitInputTextures(ID3D11Device* pDevice);

	bool IsVideoDeviceOk() { return (m_pVideoDevice != nullptr); }
	bool IsReady() { return (m_pVideoProcessor != nullptr); }
	void GetVPParams(D3D11_VIDEO_PROCESSOR_CAPS& caps, UINT& rateConvIndex, D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS& rateConvCaps);
	BOOL IsPqSupported() { return m_bExConvSupported; }

	ID3D11Texture2D* GetNextInputTexture(const D3D11_VIDEO_FRAME_FORMAT vframeFormat);
	void ResetFrameOrder();

	HRESULT SetRectangles(const RECT * pSrcRect, const RECT* pDstRect);
	HRESULT SetColorSpace(const DXVA2_ExtendedFormat exFmt, const bool bHdrPassthrough);
	void SetRotation(D3D11_VIDEO_PROCESSOR_ROTATION rotation);
	void SetProcAmpValues(DXVA2_ProcAmpValues *pValues);
	HRESULT SetSuperRes(const bool enable);

	HRESULT Process(ID3D11Texture2D* pRenderTarget, const D3D11_VIDEO_FRAME_FORMAT sampleFormat, const bool second);
};
