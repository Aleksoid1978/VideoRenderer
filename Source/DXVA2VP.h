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

#include <atltypes.h>
#include <dxva2api.h>

struct VideoSurface {
	REFERENCE_TIME Start = 0;
	REFERENCE_TIME End = 0;
	CComPtr<IDirect3DSurface9> pSrcSurface;
	DXVA2_SampleFormat SampleFormat = DXVA2_SampleUnknown;
};

class VideoSurfaceBuffer
{
	std::vector<VideoSurface> m_Surfaces;
	unsigned m_LastPos = 0;
	std::vector<DXVA2_VideoSample> m_DXVA2Samples;

public:
	unsigned Size() const {
		return (unsigned)m_Surfaces.size();
	}

	bool Empty() const {
		return m_Surfaces.empty();
	}

	void Clear() {
		m_Surfaces.clear();
		m_DXVA2Samples.clear();
	}

	void Resize(const unsigned size, const UINT exFmtValue) {
		Clear();
		m_Surfaces.resize(size);
		m_LastPos = Size() - 1;
		m_DXVA2Samples.resize(size);

		for (auto& dxva2sample : m_DXVA2Samples) {
			dxva2sample.SampleFormat.value = exFmtValue;
			dxva2sample.PlanarAlpha = DXVA2_Fixed32OpaqueAlpha();
		}
	}

	void UpdateVideoSamples() {
		for (unsigned i = 0; i < m_DXVA2Samples.size(); i++) {
			const auto& vsurface = GetAt(i);
			m_DXVA2Samples[i].Start = vsurface.Start;
			m_DXVA2Samples[i].End = vsurface.End;
			m_DXVA2Samples[i].SampleFormat.SampleFormat = vsurface.SampleFormat;
			m_DXVA2Samples[i].SrcSurface = vsurface.pSrcSurface;
		}
	}

	VideoSurface& Get() {
		return m_Surfaces[m_LastPos];
	}

	VideoSurface& GetAt(const unsigned pos) {
		unsigned InternalPos = (m_LastPos + 1 + pos) % Size();
		return m_Surfaces[InternalPos];
	}

	void Next() {
		m_LastPos++;
		if (m_LastPos >= Size()) {
			m_LastPos = 0;
		}
	}

	void SetRects(const CRect& SrcRect, const CRect& DstRect) {
		for (auto& dxva2sample : m_DXVA2Samples) {
			dxva2sample.SrcRect = SrcRect;
			dxva2sample.DstRect = DstRect;
		}
	}

	const DXVA2_VideoSample* GetVideoSamples() {
		return m_DXVA2Samples.data();
	}
};

class VideoSampleBuffer
{
private:
	std::vector<DXVA2_VideoSample> m_DXVA2Samples;

public:
	void Resize(const unsigned len) {
		for (auto& dxva2sample : m_DXVA2Samples) {
			dxva2sample.SrcSurface->Release();
		}
		m_DXVA2Samples.clear();
		const DXVA2_VideoSample dxva2sample = { 0, 0, {}, nullptr, {}, {}, {}, DXVA2_Fixed32OpaqueAlpha(), 0 };
		m_DXVA2Samples.resize(len, dxva2sample);
	}

	void SetRects(const CRect& SrcRect, const CRect& DstRect) {
		for (auto& dxva2sample : m_DXVA2Samples) {
			dxva2sample.SrcRect = SrcRect;
			dxva2sample.DstRect = DstRect;
		}
	}

	void Add(const REFERENCE_TIME start, const REFERENCE_TIME end, const UINT exFmtValue, IDirect3DSurface9* pSurface)
	{
		ASSERT(m_DXVA2Samples.size());
		ASSERT(pSurface);

		pSurface->AddRef();

		m_DXVA2Samples.front().SrcSurface->Release();

		for (size_t i = 1; i < m_DXVA2Samples.size(); i++) {
			auto pre = i - 1;
			m_DXVA2Samples[pre].Start              = m_DXVA2Samples[i].Start;
			m_DXVA2Samples[pre].End                = m_DXVA2Samples[i].End;
			m_DXVA2Samples[pre].SampleFormat.value = m_DXVA2Samples[i].SampleFormat.value;
			m_DXVA2Samples[pre].SrcSurface         = m_DXVA2Samples[i].SrcSurface;
		}

		m_DXVA2Samples.back().Start = start;
		m_DXVA2Samples.back().End = end;
		m_DXVA2Samples.back().SampleFormat.value = exFmtValue;
		m_DXVA2Samples.back().SrcSurface = pSurface;
	}

	const DXVA2_VideoSample* GetVideoSamples() {
		return m_DXVA2Samples.data();
	}
};

// TODO
// DXVA2 Video Processor
class CDXVA2VP
{
private:
	CComPtr<IDirectXVideoProcessorService> m_pDXVA2_VPService;
	CComPtr<IDirectXVideoProcessor> m_pDXVA2_VP;
	GUID m_DXVA2VPGuid = GUID_NULL;
	DXVA2_VideoProcessBltParams m_BltParams = {};
	VideoSurfaceBuffer m_SrcSamples;

	DXVA2_VideoProcessorCaps m_DXVA2VPcaps = {};

	// ProcAmp
	DXVA2_ValueRange m_DXVA2ProcAmpRanges[4] = {};
	bool m_bUpdateFilters = false;

	D3DFORMAT m_srcFormat = D3DFMT_UNKNOWN;
	UINT m_srcWidth    = 0;
	UINT m_srcHeight   = 0;
	bool m_bInterlaced = false;

	BOOL CreateDXVA2VPDevice(const GUID devguid, const DXVA2_VideoDesc& videodesc, UINT preferredDeintTech, D3DFORMAT& outputFmt);

public:
	HRESULT InitVideoService(IDirect3DDevice9* pDevice);
	void ReleaseVideoService();

	HRESULT InitVideoProcessor(const D3DFORMAT inputFmt, const UINT width, const UINT height, const bool interlaced, D3DFORMAT& outputFmt);
	void ReleaseVideoProcessor();

	bool IsReady() { return (m_pDXVA2_VP != nullptr); }

	HRESULT SetInputSurface(IDirect3DSurface9* pTexture2D);
	HRESULT SetProcessParams(const CRect& srcRect, const CRect& dstRect, const DXVA2_ExtendedFormat exFmt);
	void SetProcAmpValues(DXVA2_ProcAmpValues *pValues);

	HRESULT Process(IDirect3DSurface9* pRenderTarget, const DXVA2_SampleFormat sampleFormat, const bool second);
};
