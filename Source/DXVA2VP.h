/*
* (C) 2019-2025 see Authors.txt
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

class VideoSampleBuffer9
{
private:
	enum SurfaceLocation {
		Surface_Unknown,
		Surface_Internal,
		Surface_External
	};

	struct DXVA2_SampleInfo {
		CComPtr<IMediaSample> pSample;
		REFERENCE_TIME Start;
		REFERENCE_TIME End;
		DXVA2_SampleFormat SampleFormat;
		CComPtr<IDirect3DSurface9> pSrcSurface;
	};

	std::deque<DXVA2_SampleInfo> m_Samples;

	SurfaceLocation m_Location = Surface_Unknown;
	UINT m_maxSize = 1;
	DXVA2_ExtendedFormat m_exFmt = {};

	std::vector<DXVA2_VideoSample> m_DXVA2Samples;

	void SetEmptyDXVA2Samples() {
		m_DXVA2Samples.assign(m_maxSize, DXVA2_VideoSample{ 0, 0, m_exFmt, nullptr, {}, {}, {}, DXVA2_Fixed32OpaqueAlpha(), 0 });
	}

	void UpdateDXVA2Samples()
	{
		ASSERT(m_DXVA2Samples.size() >= m_Samples.size());
		size_t idx = 0;
		for (const auto& sample : m_Samples) {
			auto& dxva2Sample = m_DXVA2Samples[idx];
			dxva2Sample.Start      = sample.Start;
			dxva2Sample.End        = sample.End;
			dxva2Sample.SampleFormat.SampleFormat = sample.SampleFormat;
			dxva2Sample.SrcSurface = sample.pSrcSurface.p;
			idx++;
		}
	}

	void SetLocation(const SurfaceLocation location)
	{
		if (m_Location != location) {
			m_Samples.clear();
			if (location == Surface_Unknown) {
				m_DXVA2Samples.clear();
			}
			else {
				SetEmptyDXVA2Samples();
			}
			m_Location = location;
		}
	}

public:
	const DXVA2_VideoSample* Data() {
		return m_DXVA2Samples.data();
	}

	const UINT Size() {
		ASSERT(m_DXVA2Samples.size() >= m_Samples.size());
		return m_Samples.size();
	}

	const UINT MaxSize() {
		return m_maxSize;
	}

	/*
	void CleanDXVA2Samples() {
		for (auto& dxva2sample : m_DXVA2Samples) {
			dxva2sample.Start = 0;
			dxva2sample.End = 0;
			dxva2sample.SampleFormat.SampleFormat = DXVA2_SampleUnknown;
			if (dxva2sample.SrcSurface) {
				IDirect3DDevice9* pDevice;
				if (S_OK == dxva2sample.SrcSurface->GetDevice(&pDevice)) {
					pDevice->ColorFill(dxva2sample.SrcSurface, nullptr, D3DCOLOR_XYUV(0, 128, 128));
					pDevice->Release();
				}
			}
		}
	}
	*/

	void Clean() {
		SetLocation(Surface_Unknown);
	}

	void Clear() {
		m_Samples.clear();
		m_DXVA2Samples.clear();
	}

	void SetProps(const UINT maxSize, const DXVA2_ExtendedFormat exFmt)
	{
		Clear();
		m_Location = Surface_Unknown;
		m_maxSize  = maxSize;
		m_exFmt    = exFmt;

		// replace values that are not included in the DXVA2 specification to obtain a more stable result for subsequent correction
		if (m_exFmt.VideoTransferMatrix > DXVA2_VideoTransferMatrix_SMPTE240M) { m_exFmt.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT709; }
		if (m_exFmt.VideoPrimaries > DXVA2_VideoPrimaries_SMPTE_C) { m_exFmt.VideoPrimaries = DXVA2_VideoPrimaries_BT709; }
		if (m_exFmt.VideoTransferFunction > DXVA2_VideoTransFunc_28) { m_exFmt.VideoTransferFunction = DXVA2_VideoTransFunc_709; }
	}

	IDirect3DSurface9* GetNextInternalSurface(const UINT frameNum, const DXVA2_SampleFormat sampleFmt, IDirect3DSurface9* pSurface)
	{
		if (!m_maxSize) {
			return nullptr;
		}
		SetLocation(Surface_Internal);

		if (m_Samples.size() < m_maxSize) {
			m_Samples.emplace_back(DXVA2_SampleInfo{ nullptr, 0, 0, DXVA2_SampleUnknown, pSurface });
		}
		else if (m_Samples.size() > 1) {
			m_Samples.emplace_back(std::move(m_Samples.front()));
			m_Samples.pop_front();
		}

		auto& sample = m_Samples.back();
		sample.Start = frameNum * 170000i64;
		sample.End   = sample.Start + 170000i64;
		sample.SampleFormat = sampleFmt;

		UpdateDXVA2Samples();

		return sample.pSrcSurface;
	}

	void AddExternalSampleInfo(IMediaSample* pSample, const UINT frameNum, const DXVA2_SampleFormat sampleFmt, IDirect3DSurface9* pSrcSurface)
	{
		if (!m_maxSize) {
			return;
		}
		SetLocation(Surface_External);

		if (m_Samples.size() >= m_maxSize) {
			m_Samples.pop_front();
		}

		REFERENCE_TIME start = frameNum * 170000i64;
		m_Samples.emplace_back(DXVA2_SampleInfo{ pSample, start, start + 170000i64, sampleFmt, pSrcSurface });

		UpdateDXVA2Samples();
	}

	void SetRects(const CRect& SrcRect, const CRect& DstRect)
	{
		for (auto& dxva2sample : m_DXVA2Samples) {
			dxva2sample.SrcRect = SrcRect;
			dxva2sample.DstRect = DstRect;
		}
	}

	REFERENCE_TIME GetTargetFrameTime(size_t idx, const bool second)
	{
		if (idx >= m_Samples.size()) {
			idx = m_Samples.size() - 1;
		}

		auto time = m_DXVA2Samples[idx].Start;
		if (second) {
			time += 170000i64 / 2;
		}

		return time;
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
	VideoSampleBuffer9 m_VideoSamples;

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

	HRESULT InitVideoProcessor(
		const D3DFORMAT inputFmt, const UINT width, const UINT height,
		const DXVA2_ExtendedFormat exFmt, const int deinterlacing,
		D3DFORMAT& outputFmt);
	void ReleaseVideoProcessor();

	bool IsReady() { return (m_pDXVA2_VP != nullptr); }
	UINT GetNumRefSamples() { return m_NumRefSamples; }
	void GetVPParams(GUID& guid, DXVA2_VideoProcessorCaps& caps) { guid = m_DXVA2VPGuid; caps = m_DXVA2VPcaps; }

	HRESULT AddMediaSampleAndSurface(IMediaSample* pSample, IDirect3DSurface9* pSurface, const UINT frameNum, const DXVA2_SampleFormat sampleFmt);
	IDirect3DSurface9* GetNextInputSurface(const UINT frameNum, const DXVA2_SampleFormat sampleFmt);
	void CleanSamples();

	void SetRectangles(const CRect& srcRect, const CRect& dstRect);
	void SetProcAmpValues(DXVA2_ProcAmpValues& PropValues);
	void GetProcAmpRanges(DXVA2_ValueRange(&PropRanges)[4]);

	HRESULT Process(IDirect3DSurface9* pRenderTarget, const DXVA2_SampleFormat sampleFormat, const bool second);
};
