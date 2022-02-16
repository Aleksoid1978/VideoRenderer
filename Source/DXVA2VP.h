/*
* (C) 2019-2020 see Authors.txt
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

class VideoSampleBuffer
{
private:
	std::vector<DXVA2_VideoSample> m_DXVA2Samples;

	void ReleaseSurfaces() {
		for (auto& dxva2sample : m_DXVA2Samples) {
			SAFE_RELEASE(dxva2sample.SrcSurface);
		}
	}

public:
	~VideoSampleBuffer() {
		ReleaseSurfaces();
	}

	const DXVA2_VideoSample* Data() {
		return m_DXVA2Samples.data();
	}

	const UINT Size() {
		return m_DXVA2Samples.size();
	}

	void Clean() {
		for (auto& dxva2sample : m_DXVA2Samples) {
			dxva2sample.Start = 0;
			dxva2sample.End = 0;
			dxva2sample.SampleFormat.SampleFormat = DXVA2_SampleUnknown;
			IDirect3DDevice9* pDevice;
			if (dxva2sample.SrcSurface && S_OK == dxva2sample.SrcSurface->GetDevice(&pDevice)) {
				pDevice->ColorFill(dxva2sample.SrcSurface, nullptr, D3DCOLOR_XYUV(0, 128, 128));
				pDevice->Release();
			}
		}
	}

	void Clear() {
		ReleaseSurfaces();
		m_DXVA2Samples.clear();
	}

	void Resize(const unsigned len, DXVA2_ExtendedFormat exFmt) {
		Clear();
		if (len) {
			// replace values that are not included in the DXVA2 specification to obtain a more stable result for subsequent correction
			if (exFmt.VideoTransferMatrix > DXVA2_VideoTransferMatrix_SMPTE240M) { exFmt.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT709; }
			if (exFmt.VideoPrimaries > DXVA2_VideoPrimaries_SMPTE_C) { exFmt.VideoPrimaries = DXVA2_VideoPrimaries_BT709; }
			if (exFmt.VideoTransferFunction > DXVA2_VideoTransFunc_28) { exFmt.VideoTransferFunction = DXVA2_VideoTransFunc_709; }

			const DXVA2_VideoSample dxva2sample = { 0, 0, exFmt, nullptr, {}, {}, {}, DXVA2_Fixed32OpaqueAlpha(), 0 };
			m_DXVA2Samples.resize(len, dxva2sample);
		}
	}

	void SetRects(const CRect& SrcRect, const CRect& DstRect) {
		for (auto& dxva2sample : m_DXVA2Samples) {
			dxva2sample.SrcRect = SrcRect;
			dxva2sample.DstRect = DstRect;
		}
	}

	void Add(IDirect3DSurface9* pSurface, const REFERENCE_TIME start, const REFERENCE_TIME end, const DXVA2_SampleFormat sampleFmt)
	{
		ASSERT(m_DXVA2Samples.size());
		ASSERT(pSurface);

		pSurface->AddRef();
		SAFE_RELEASE(m_DXVA2Samples.front().SrcSurface);

		for (size_t i = 1; i < m_DXVA2Samples.size(); i++) {
			auto pre = i - 1;
			m_DXVA2Samples[pre].Start                     = m_DXVA2Samples[i].Start;
			m_DXVA2Samples[pre].End                       = m_DXVA2Samples[i].End;
			m_DXVA2Samples[pre].SampleFormat.SampleFormat = m_DXVA2Samples[i].SampleFormat.SampleFormat;
			m_DXVA2Samples[pre].SrcSurface                = m_DXVA2Samples[i].SrcSurface;
		}

		auto& sample = m_DXVA2Samples.back();
		sample.Start = start;
		sample.End = end;
		sample.SampleFormat.SampleFormat = sampleFmt;
		sample.SrcSurface = pSurface;
	}

	void RotateAndSet(const REFERENCE_TIME start, const REFERENCE_TIME end, const DXVA2_SampleFormat sampleFmt)
	{
		ASSERT(m_DXVA2Samples.size());

		if (m_DXVA2Samples.size() > 1) {
			IDirect3DSurface9* pSurface = m_DXVA2Samples.front().SrcSurface;

			for (size_t i = 1; i < m_DXVA2Samples.size(); i++) {
				auto pre = i - 1;
				m_DXVA2Samples[pre].Start                     = m_DXVA2Samples[i].Start;
				m_DXVA2Samples[pre].End                       = m_DXVA2Samples[i].End;
				m_DXVA2Samples[pre].SampleFormat.SampleFormat = m_DXVA2Samples[i].SampleFormat.SampleFormat;
				m_DXVA2Samples[pre].SrcSurface                = m_DXVA2Samples[i].SrcSurface;
			}

			m_DXVA2Samples.back().SrcSurface = pSurface;
		}

		auto& sample = m_DXVA2Samples.back();
		sample.Start = start;
		sample.End = end;
		sample.SampleFormat.SampleFormat = sampleFmt;
	}

	IDirect3DSurface9** GetSurface()
	{
		if (m_DXVA2Samples.size()) {
			return &m_DXVA2Samples.back().SrcSurface;
		} else {
			return nullptr;
		}
	}

	REFERENCE_TIME GetFrameStart() {
		return m_DXVA2Samples.back().Start;
	}

	REFERENCE_TIME GetFrameEnd() {
		return m_DXVA2Samples.back().End;
	}
};

// DXVA2 Video Processor
class CDXVA2VP
{
private:
	CComPtr<IDirectXVideoProcessorService> m_pDXVA2_VPService;
	DWORD m_VendorId = 0;
	CComPtr<IDirectXVideoProcessor> m_pDXVA2_VP;
	GUID m_DXVA2VPGuid = GUID_NULL;
	DXVA2_VideoProcessBltParams m_BltParams = {};
	VideoSampleBuffer m_VideoSamples;

	DXVA2_VideoProcessorCaps m_DXVA2VPcaps = {};
	UINT m_NumRefSamples = 1;

	// ProcAmp
	DXVA2_ValueRange m_DXVA2ProcAmpRanges[4] = {};
	bool m_bUpdateFilters = false;

	D3DFORMAT m_srcFormat = D3DFMT_UNKNOWN;
	UINT m_srcWidth    = 0;
	UINT m_srcHeight   = 0;

	BOOL CreateDXVA2VPDevice(const GUID& devguid, const DXVA2_VideoDesc& videodesc, UINT preferredDeintTech, D3DFORMAT& outputFmt);

public:
	HRESULT InitVideoService(IDirect3DDevice9* pDevice, DWORD vendorId);
	void ReleaseVideoService();

	HRESULT InitVideoProcessor(const D3DFORMAT inputFmt, const UINT width, const UINT height, const DXVA2_ExtendedFormat exFmt, const bool interlaced, D3DFORMAT& outputFmt);
	void ReleaseVideoProcessor();

	bool IsReady() { return (m_pDXVA2_VP != nullptr); }
	UINT GetNumRefSamples() { return m_NumRefSamples; }
	void GetVPParams(GUID& guid, DXVA2_VideoProcessorCaps& caps) { guid = m_DXVA2VPGuid; caps = m_DXVA2VPcaps; }

	HRESULT SetInputSurface(IDirect3DSurface9* pSurface, const REFERENCE_TIME start, const REFERENCE_TIME end, const DXVA2_SampleFormat sampleFmt);
	IDirect3DSurface9* GetInputSurface();
	IDirect3DSurface9* GetNextInputSurface(const REFERENCE_TIME start, const REFERENCE_TIME end, const DXVA2_SampleFormat sampleFmt);
	void ClearInputSurfaces(const DXVA2_ExtendedFormat exFmt);
	void CleanSamplesData();

	void SetRectangles(const CRect& srcRect, const CRect& dstRect);
	void SetProcAmpValues(DXVA2_ProcAmpValues& PropValues);
	void GetProcAmpRanges(DXVA2_ValueRange(&PropRanges)[4]);

	HRESULT Process(IDirect3DSurface9* pRenderTarget, const DXVA2_SampleFormat sampleFormat, const bool second);
};
