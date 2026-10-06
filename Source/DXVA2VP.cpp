/*
* (C) 2019-2026 see Authors.txt
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

#include "stdafx.h"
#include "Helper.h"
#include "DX9Helper.h"

#include "DXVA2VP.h"
#include "IVideoRenderer.h"

// CDXVA2VP

// https://msdn.microsoft.com/en-us/library/cc307964(v=vs.85).aspx

int GetBitDepth(const D3DFORMAT format)
{
	switch (format) {
	case D3DFMT_X8R8G8B8:
	case D3DFMT_A8R8G8B8:
	case D3DFMT_YV12:
	case D3DFMT_NV12:
	case D3DFMT_YUY2:
	case D3DFMT_UYVY:
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

BOOL CDXVA2VP::CreateDXVA2VPDevice(const GUID& devguid, const DXVA2_VideoDesc& videodesc, UINT preferredDeintTech, D3DFORMAT& outputFmt)
{
	DLog(L"CDXVA2VP::CreateDXVA2VPDevice() started for device {}", DXVA2VPDeviceToString(devguid));
	CheckPointer(m_pDXVA2_VPService, FALSE);

	HRESULT hr = S_OK;

	// Query the supported render target format.
	UINT count;
	D3DFORMAT* formats = nullptr;
	hr = m_pDXVA2_VPService->GetVideoProcessorRenderTargets(devguid, &videodesc, &count, &formats);
	if (FAILED(hr)) {
		DLog(L"CDXVA2VP::CreateDXVA2VPDevice() : GetVideoProcessorRenderTargets() failed with error {}", HR2Str(hr));
		return FALSE;
	}
#ifdef _DEBUG
	{
		std::wstring dbgstr = L"DXVA2-VP output formats:";
		for (UINT j = 0; j < count; j++) {
			dbgstr.append(L"\n  ");
			dbgstr.append(D3DFormatToString(formats[j]));
		}
		DLog(dbgstr);
	}
#endif

	if (outputFmt == D3DFMT_UNKNOWN) {
		outputFmt = (GetBitDepth(videodesc.Format) > 8) ? D3DFMT_A2R10G10B10 : D3DFMT_X8R8G8B8;
	}
	UINT index;
	for (index = 0; index < count; index++) {
		if (formats[index] == outputFmt) {
			break;
		}
	}
	if (index >= count && outputFmt == D3DFMT_A16B16G16R16F) {
		outputFmt = D3DFMT_A2R10G10B10;
		for (index = 0; index < count; index++) {
			if (formats[index] == outputFmt) {
				break;
			}
		}
	}
	if (index >= count && outputFmt != D3DFMT_X8R8G8B8) {
		outputFmt = D3DFMT_X8R8G8B8;
		for (index = 0; index < count; index++) {
			if (formats[index] == outputFmt) {
				break;
			}
		}
	}
	CoTaskMemFree(formats);
	if (index >= count) {
		outputFmt = D3DFMT_UNKNOWN;
		DLog(L"CDXVA2VP::CreateDXVA2VPDevice() : FAILED. Device doesn't support desired output format");
		return FALSE;
	}
	DLog(L"CDXVA2VP::CreateDXVA2VPDevice() : select {} for output", D3DFormatToString(outputFmt));

	// Query video processor capabilities.
	hr = m_pDXVA2_VPService->GetVideoProcessorCaps(devguid, &videodesc, outputFmt, &m_DXVA2VPcaps);
	if (FAILED(hr)) {
		DLog(L"CDXVA2VP::CreateDXVA2VPDevice() : GetVideoProcessorCaps() failed with error {}", HR2Str(hr));
		return FALSE;
	}
	if (preferredDeintTech) {
		if (!(m_DXVA2VPcaps.DeinterlaceTechnology & preferredDeintTech)) {
			DLog(L"CDXVA2VP::CreateDXVA2VPDevice() : skip this device, need improved deinterlacing");
			return FALSE;
		}
		if (m_DXVA2VPcaps.NumForwardRefSamples > 0) {
			DLog(L"CDXVA2VP::CreateDXVA2VPDevice() : skip this device, ForwardRefSamples are not supported");
			return FALSE;
		}
	}
	// Check to see if the device is hardware device.
	if (!(m_DXVA2VPcaps.DeviceCaps & DXVA2_VPDev_HardwareDevice)) {
		DLog(L"CDXVA2VP::CreateDXVA2VPDevice() : The DXVA2 device isn't a hardware device");
		return FALSE;
	}
	// Check to see if the device supports all the VP operations we want.
	const UINT VIDEO_REQUIED_OP = DXVA2_VideoProcess_YUV2RGB | DXVA2_VideoProcess_StretchX | DXVA2_VideoProcess_StretchY;
	if ((m_DXVA2VPcaps.VideoProcessorOperations & VIDEO_REQUIED_OP) != VIDEO_REQUIED_OP) {
		DLog(L"CDXVA2VP::CreateDXVA2VPDevice() : The DXVA2 device doesn't support the YUV2RGB & Stretch operations");
		return FALSE;
	}

	// Finally create a video processor device.
	hr = m_pDXVA2_VPService->CreateVideoProcessor(devguid, &videodesc, outputFmt, 0, &m_pDXVA2_VP);
	if (FAILED(hr)) {
		DLog(L"CDXVA2VP::CreateDXVA2VPDevice() : CreateVideoProcessor failed with error {}", HR2Str(hr));
		return FALSE;
	}

#ifdef _DEBUG
	{
		std::wstring dbgstr = L"VideoProcessorCaps:";
		dbgstr.append(L"\n  VP Operations         :");
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_YUV2RGB)            { dbgstr.append(L" YUV2RGB,"); }
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_StretchX)           { dbgstr.append(L" StretchX,"); }
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_StretchY)           { dbgstr.append(L" StretchY,"); }
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_AlphaBlend)         { dbgstr.append(L" AlphaBlend,"); }
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_SubRects)           { dbgstr.append(L" SubRects,"); }
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_SubStreamsExtended) { dbgstr.append(L" SubStreamsExtended,"); }
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_YUV2RGBExtended)    { dbgstr.append(L" YUV2RGBExtended,"); }
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_AlphaBlendExtended) { dbgstr.append(L" AlphaBlendExtended,"); }
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_Constriction)       { dbgstr.append(L" Constriction,"); }
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_NoiseFilter)        { dbgstr.append(L" NoiseFilter,"); }
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_DetailFilter)       { dbgstr.append(L" DetailFilter,"); }
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_PlanarAlpha)        { dbgstr.append(L" PlanarAlpha,"); }
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_LinearScaling)      { dbgstr.append(L" LinearScaling,"); }
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_GammaCompensated)   { dbgstr.append(L" GammaCompensated,"); }
		if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_MaintainsOriginalFieldData) { dbgstr.append(L" MaintainsOriginalFieldData"); }
		str_trim_end(dbgstr, ',');
		dbgstr.append(L"\n  Deinterlace Technology:");
		if (m_DXVA2VPcaps.DeinterlaceTechnology & DXVA2_DeinterlaceTech_BOBLineReplicate)       { dbgstr.append(L" BOBLineReplicate,"); }
		if (m_DXVA2VPcaps.DeinterlaceTechnology & DXVA2_DeinterlaceTech_BOBVerticalStretch)     { dbgstr.append(L" BOBVerticalStretch,"); }
		if (m_DXVA2VPcaps.DeinterlaceTechnology & DXVA2_DeinterlaceTech_BOBVerticalStretch4Tap) { dbgstr.append(L" BOBVerticalStretch4Tap,"); }
		if (m_DXVA2VPcaps.DeinterlaceTechnology & DXVA2_DeinterlaceTech_MedianFiltering)        { dbgstr.append(L" MedianFiltering,"); }
		if (m_DXVA2VPcaps.DeinterlaceTechnology & DXVA2_DeinterlaceTech_EdgeFiltering)          { dbgstr.append(L" EdgeFiltering,"); }
		if (m_DXVA2VPcaps.DeinterlaceTechnology & DXVA2_DeinterlaceTech_FieldAdaptive)          { dbgstr.append(L" FieldAdaptive,"); }
		if (m_DXVA2VPcaps.DeinterlaceTechnology & DXVA2_DeinterlaceTech_PixelAdaptive)          { dbgstr.append(L" PixelAdaptive,"); }
		if (m_DXVA2VPcaps.DeinterlaceTechnology & DXVA2_DeinterlaceTech_MotionVectorSteered)    { dbgstr.append(L" MotionVectorSteered,"); }
		if (m_DXVA2VPcaps.DeinterlaceTechnology & DXVA2_DeinterlaceTech_InverseTelecine)        { dbgstr.append(L" InverseTelecine"); }
		str_trim_end(dbgstr, ',');
		DLog(dbgstr);
		}
#endif

	// Query ProcAmp ranges.
	for (UINT i = 0; i < std::size(m_DXVA2ProcAmpRanges); i++) {
		if (m_DXVA2VPcaps.ProcAmpControlCaps & (1 << i)) {
			hr = m_pDXVA2_VPService->GetProcAmpRange(devguid, &videodesc, outputFmt, 1 << i, &m_DXVA2ProcAmpRanges[i]);
			if (FAILED(hr)) {
				DLog(L"CDXVA2VP::CreateDXVA2VPDevice() : GetProcAmpRange() failed with error {}", HR2Str(hr));
				return FALSE;
			}
			DLog(L"CDXVA2VP::CreateDXVA2VPDevice() : ProcAmpRange({}) : {:7.2f}, {:6.2f}, {:6.2f}, {:4.2f}",
				i, DXVA2FixedToFloat(m_DXVA2ProcAmpRanges[i].MinValue), DXVA2FixedToFloat(m_DXVA2ProcAmpRanges[i].MaxValue),
				DXVA2FixedToFloat(m_DXVA2ProcAmpRanges[i].DefaultValue), DXVA2FixedToFloat(m_DXVA2ProcAmpRanges[i].StepSize));
		}
	}

	DXVA2_ValueRange range;
	// Query Noise Filter ranges.
	DXVA2_Fixed32 NFilterValues[6] = {};
	if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_NoiseFilter) {
		for (UINT i = 0; i < 6u; i++) {
			if (S_OK == m_pDXVA2_VPService->GetFilterPropertyRange(devguid, &videodesc, outputFmt, DXVA2_NoiseFilterLumaLevel + i, &range)) {
				NFilterValues[i] = range.DefaultValue;
			}
		}
	}
	// Query Detail Filter ranges.
	DXVA2_Fixed32 DFilterValues[6] = {};
	if (m_DXVA2VPcaps.VideoProcessorOperations & DXVA2_VideoProcess_DetailFilter) {
		for (UINT i = 0; i < 6u; i++) {
			if (S_OK == m_pDXVA2_VPService->GetFilterPropertyRange(devguid, &videodesc, outputFmt, DXVA2_DetailFilterLumaLevel + i, &range)) {
				DFilterValues[i] = range.DefaultValue;
			}
		}
	}

	m_BltParams.BackgroundColor = { 128 * 0x100, 128 * 0x100, 16 * 0x100, 0xFFFF }; // black
	m_BltParams.ProcAmpValues.Brightness = m_DXVA2ProcAmpRanges[0].DefaultValue;
	m_BltParams.ProcAmpValues.Contrast   = m_DXVA2ProcAmpRanges[1].DefaultValue;
	m_BltParams.ProcAmpValues.Hue        = m_DXVA2ProcAmpRanges[2].DefaultValue;
	m_BltParams.ProcAmpValues.Saturation = m_DXVA2ProcAmpRanges[3].DefaultValue;
	m_BltParams.Alpha = DXVA2_Fixed32OpaqueAlpha();
	m_BltParams.NoiseFilterLuma.Level        = NFilterValues[0];
	m_BltParams.NoiseFilterLuma.Threshold    = NFilterValues[1];
	m_BltParams.NoiseFilterLuma.Radius       = NFilterValues[2];
	m_BltParams.NoiseFilterChroma.Level      = NFilterValues[3];
	m_BltParams.NoiseFilterChroma.Threshold  = NFilterValues[4];
	m_BltParams.NoiseFilterChroma.Radius     = NFilterValues[5];
	m_BltParams.DetailFilterLuma.Level       = DFilterValues[0];
	m_BltParams.DetailFilterLuma.Threshold   = DFilterValues[1];
	m_BltParams.DetailFilterLuma.Radius      = DFilterValues[2];
	m_BltParams.DetailFilterChroma.Level     = DFilterValues[3];
	m_BltParams.DetailFilterChroma.Threshold = DFilterValues[4];
	m_BltParams.DetailFilterChroma.Radius    = DFilterValues[5];

	DLog(L"CDXVA2VP::CreateDXVA2VPDevice() : create {} processor ", GUIDtoWString(devguid));

	return TRUE;
}

HRESULT CDXVA2VP::InitVideoService(IDirect3DDevice9* pDevice, DWORD vendorId)
{
	ReleaseVideoService();

	// Create DXVA2 Video Processor Service.
	HRESULT hr = DXVA2CreateVideoService(pDevice, IID_IDirectXVideoProcessorService, (VOID**)&m_pDXVA2_VPService);
	DLogIf(FAILED(hr), L"CDXVA2VP::InitVideoService() : DXVA2CreateVideoService() failed with error {}", HR2Str(hr));

	m_VendorId = vendorId;

	return hr;
}

void CDXVA2VP::ReleaseVideoService()
{
	ReleaseVideoProcessor();

	m_pDXVA2_VPService.Release();
}


// Which DestFormat.NominalRange gets full range RGB out of this driver.
//
// The specification says a 16-235 source with a 0-255 destination is expanded, and some drivers do
// not do it: the picture then comes out with black lifted and white lowered.  Asking those drivers
// for 16-235 is what produces the expansion, which is the opposite of what it reads like.  Rather
// than keep a list of which vendors are which, put a known value through the processor and look at
// what comes back.
//
// Y 200 limited range is 214 when it has been expanded and 200 when it has not, so one Blt per
// candidate answers it, with a margin no rounding or dither can blur.  Mid grey would be the worst
// choice: it is where a range error moves the value least.  Anything unexpected leaves the answer
// Unknown and the caller keeps its old behaviour.
//
// It only judges 16-235 sources, where expanded is right.  A full range source keeps whatever it
// had before, since there the right answer is the unexpanded one and this has not been measured.
DXVA2_NominalRange CDXVA2VP::ProbeDestNominalRange(const D3DFORMAT inputFmt, const D3DFORMAT outputFmt,
	const DXVA2_ExtendedFormat& exFmt, const UINT width, const UINT height)
{
	if (m_ProbedFormat == inputFmt) {
		return m_ProbedDestRange; // already asked, for this format
	}
	m_ProbedFormat = inputFmt;
	m_ProbedDestRange = DXVA2_NominalRange_Unknown;

	if (!m_pDXVA2_VP || !m_pDXVA2_VPService || exFmt.NominalRange == DXVA2_NominalRange_0_255) {
		return m_ProbedDestRange;
	}

	const UINT size = width;
	const UINT sizeY = height;
	CComPtr<IDirect3DSurface9> pIn, pOut, pSys;
	HRESULT hr = m_pDXVA2_VPService->CreateSurface(size, sizeY, 0, inputFmt, m_DXVA2VPcaps.InputPool, 0,
		DXVA2_VideoProcessorRenderTarget, &pIn, nullptr);
	if (FAILED(hr)) {
		DLog(L"CDXVA2VP::ProbeDestNominalRange() : input surface failed with error {}", HR2Str(hr));
		return m_ProbedDestRange;
	}
	CComPtr<IDirect3DDevice9> pDevice;
	if (FAILED(pIn->GetDevice(&pDevice))) {
		return m_ProbedDestRange;
	}
	// A render target made by the device, not by the processor service: the processor is happy to
	// write into either, but GetRenderTargetData only reads back the device's own, which is how
	// Process() and the HDR measurement already do it.
	hr = pDevice->CreateRenderTarget(size, sizeY, outputFmt, D3DMULTISAMPLE_NONE, 0, FALSE, &pOut, nullptr);
	if (FAILED(hr)) {
		DLog(L"CDXVA2VP::ProbeDestNominalRange() : output surface failed with error {}", HR2Str(hr));
		return m_ProbedDestRange;
	}

	// Written by hand.  ColorFill does not work on these formats; GetNextInputSurface calls it on
	// the same surfaces and ignores the result, which is why that is easy to miss.  Luma is Y 200,
	// 0xC800 in the 16 bit layouts (800 of 1023 shifted up), chroma is neutral.  Y 200 limited is
	// 0.8402 of the range at 8 bits and 0.8402 at 10, so one expectation covers both.
	constexpr BYTE y8 = 200, c8 = 0x80;
	constexpr uint16_t y16 = 0xC800, c16 = 0x8000;
	bool planar = false, wide = false, uyvy = false;
	switch (inputFmt) {
	case D3DFMT_NV12: planar = true; break;            // planar 4:2:0, chroma plane after the luma
	case D3DFMT_P010:
	case D3DFMT_P016: planar = true; wide = true; break;
	case D3DFMT_YUY2: break;                           // packed Y0 U Y1 V
	case D3DFMT_UYVY: uyvy = true; break;              // packed U Y0 V Y1
	default:
		DLog(L"CDXVA2VP::ProbeDestNominalRange() : no fill for {}, leaving the answer open", D3DFormatToString(inputFmt));
		return m_ProbedDestRange;
	}

	D3DLOCKED_RECT lrIn = {};
	if (FAILED(pIn->LockRect(&lrIn, nullptr, D3DLOCK_NOSYSLOCK))) {
		DLog(L"CDXVA2VP::ProbeDestNominalRange() : could not lock the input surface");
		return m_ProbedDestRange;
	}
	const UINT rows = planar ? sizeY + sizeY / 2 : sizeY;
	for (UINT y = 0; y < rows; y++) {
		BYTE* row = (BYTE*)lrIn.pBits + (size_t)y * lrIn.Pitch;
		if (planar) {
			const bool luma = y < sizeY;
			if (wide) {
				uint16_t* p16 = (uint16_t*)row;
				for (UINT x = 0; x < size; x++) { p16[x] = luma ? y16 : c16; }
			} else {
				memset(row, luma ? y8 : c8, size);
			}
		} else {
			for (UINT x = 0; x < size * 2; x += 2) {   // two bytes per pixel, luma and chroma alternating
				row[x + (uyvy ? 1 : 0)] = y8;
				row[x + (uyvy ? 0 : 1)] = c8;
			}
		}
	}
	pIn->UnlockRect();

	if (FAILED(pDevice->CreateOffscreenPlainSurface(size, sizeY, outputFmt, D3DPOOL_SYSTEMMEM, &pSys, nullptr))) {
		return m_ProbedDestRange;
	}
	DXVA2_VideoSample sample = {};
	sample.Start = 0;
	sample.End = 1;
	sample.SampleFormat = exFmt;
	sample.SampleFormat.SampleFormat = DXVA2_SampleProgressiveFrame;
	sample.SrcSurface = pIn;
	sample.SrcRect = { 0, 0, (LONG)size, (LONG)sizeY };
	sample.DstRect = sample.SrcRect;
	sample.PlanarAlpha = DXVA2_Fixed32OpaqueAlpha();

	DXVA2_VideoProcessBltParams blt = m_BltParams;
	blt.TargetFrame = 0;
	blt.TargetRect = sample.SrcRect;
	blt.ConstrictionSize = {};
	blt.DestFormat.value = 0;
	blt.DestFormat.SampleFormat = DXVA2_SampleProgressiveFrame;

	// Y 200 in 16-235 is 214 once expanded to 0-255, and 200 if it is passed through.  Half way
	// between the two is the only judgement here.
	constexpr int expanded = 214;
	constexpr int asIs = 200;
	const DXVA2_NominalRange candidates[] = { DXVA2_NominalRange_0_255, DXVA2_NominalRange_16_235 };
	int measured[2] = { -1, -1 };

	for (int i = 0; i < 2; i++) {
		blt.DestFormat.NominalRange = candidates[i];
		const HRESULT hrBlt = m_pDXVA2_VP->VideoProcessBlt(pOut, &blt, &sample, 1, nullptr);
		if (FAILED(hrBlt)) {
			DLog(L"CDXVA2VP::ProbeDestNominalRange() : Blt failed for candidate {} with error {}", i, HR2Str(hrBlt));
			return m_ProbedDestRange;
		}
		if (FAILED(pDevice->GetRenderTargetData(pOut, pSys))) {
			DLog(L"CDXVA2VP::ProbeDestNominalRange() : read back failed");
			return m_ProbedDestRange;
		}
		D3DLOCKED_RECT lr = {};
		if (FAILED(pSys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
			return m_ProbedDestRange;
		}
		// the middle of the surface, away from any edge the processor may treat differently
		const BYTE* row = (const BYTE*)lr.pBits + (sizeY / 2) * lr.Pitch;
		const BYTE* px = row + (size / 2) * 4;
		measured[i] = (px[0] + px[1] + px[2]) / 3;   // grey, so any channel would do
		pSys->UnlockRect();
	}

	const int mid = (expanded + asIs) / 2;
	const bool full0 = measured[0] > mid, full1 = measured[1] > mid;
	if (full0 != full1) {
		m_ProbedDestRange = full0 ? DXVA2_NominalRange_0_255 : DXVA2_NominalRange_16_235;
	}
	DLog(L"CDXVA2VP::ProbeDestNominalRange() : asking for 0-255 gives {}, for 16-235 gives {}, using {}",
		measured[0], measured[1],
		m_ProbedDestRange == DXVA2_NominalRange_0_255 ? L"0-255"
			: m_ProbedDestRange == DXVA2_NominalRange_16_235 ? L"16-235" : L"neither, keeping the default");
	return m_ProbedDestRange;
}

HRESULT CDXVA2VP::InitVideoProcessor(
	const D3DFORMAT inputFmt, const UINT width, const UINT height,
	const DXVA2_ExtendedFormat exFmt, const int deinterlacing,
	D3DFORMAT& outputFmt)
{
	CheckPointer(m_pDXVA2_VPService, E_FAIL);

	ReleaseVideoProcessor();
	HRESULT hr = S_OK;

	// Initialize the video descriptor.
	DXVA2_VideoDesc videodesc = {};
	videodesc.SampleWidth = width;
	videodesc.SampleHeight = height;
	//videodesc.SampleFormat.value = m_srcExFmt.value; // do not need to fill it here
	videodesc.SampleFormat.SampleFormat = deinterlacing ? DXVA2_SampleFieldInterleavedOddFirst : DXVA2_SampleProgressiveFrame;
	if (inputFmt == D3DFMT_X8R8G8B8 || inputFmt == D3DFMT_A8R8G8B8) {
		videodesc.Format = D3DFMT_YUY2; // hack
	} else {
		videodesc.Format = inputFmt;
	}
	videodesc.InputSampleFreq.Numerator = 60;
	videodesc.InputSampleFreq.Denominator = 1;
	videodesc.OutputFrameFreq.Numerator = 60;
	videodesc.OutputFrameFreq.Denominator = 1;

	// Query the video processor GUID.
	UINT count;
	GUID* guids = nullptr;
	hr = m_pDXVA2_VPService->GetVideoProcessorDeviceGuids(&videodesc, &count, &guids);
	if (FAILED(hr)) {
		DLog(L"CDXVA2VP::InitVideoProcessor() : GetVideoProcessorDeviceGuids() failed with error {}", HR2Str(hr));
		return hr;
	}

	// We check the creation of the input surface, because Y410 surface (Intel) may not be generated for some unknown reason
	CComPtr<IDirect3DSurface9> pTestInputSurface;
	hr = m_pDXVA2_VPService->CreateSurface(
		width, height,
		0, inputFmt,
		D3DPOOL_DEFAULT, 0,
		DXVA2_VideoProcessorRenderTarget,
		&pTestInputSurface,
		nullptr
	);
	if (FAILED(hr)) {
		DLog(L"CDXVA2VP::InitVideoProcessor() : Create test input surface failed with error {}", HR2Str(hr));
		return hr;
	}

	D3DFORMAT TestOutputFmt = outputFmt;

	if (deinterlacing) {
		const UINT preferredDeintTech = DXVA2_DeinterlaceTech_EdgeFiltering // Intel
			| DXVA2_DeinterlaceTech_FieldAdaptive
			| DXVA2_DeinterlaceTech_PixelAdaptive // Nvidia, AMD
			| DXVA2_DeinterlaceTech_MotionVectorSteered;

		for (UINT i = 0; i < count; i++) {
			auto& devguid = guids[i];
			if (CreateDXVA2VPDevice(devguid, videodesc, preferredDeintTech, TestOutputFmt)) {
				m_DXVA2VPGuid = devguid;
				break; // found!
			}
			m_pDXVA2_VP.Release();
		}

		if (!m_pDXVA2_VP && CreateDXVA2VPDevice(DXVA2_VideoProcBobDevice, videodesc, 0, TestOutputFmt)) {
			m_DXVA2VPGuid = DXVA2_VideoProcBobDevice;
		}
	}

	CoTaskMemFree(guids);

	if (!m_pDXVA2_VP && CreateDXVA2VPDevice(DXVA2_VideoProcProgressiveDevice, videodesc, 0, TestOutputFmt)) { // Progressive or fall-back for interlaced
		m_DXVA2VPGuid = DXVA2_VideoProcProgressiveDevice;
	}

	if (!m_pDXVA2_VP) {
		m_DXVA2VPcaps = {};
		return E_FAIL;
	}

	outputFmt = TestOutputFmt;

	m_NumRefSamples = 1 + m_DXVA2VPcaps.NumBackwardRefSamples;
	if (deinterlacing == DEINT_HackFutureFrames) {
		m_NumRefSamples += m_DXVA2VPcaps.NumForwardRefSamples;
	}
	ASSERT(m_NumRefSamples <= MAX_DEINTERLACE_SURFACES);

	m_VideoSamples.SetProps(m_NumRefSamples, exFmt);

	m_BltParams.DestFormat.value = 0; // output to RGB
	m_BltParams.DestFormat.SampleFormat = DXVA2_SampleProgressiveFrame; // output to progressive RGB

	// output to full range RGB
	m_BltParams.DestFormat.NominalRange = DXVA2_NominalRange_0_255;

	switch (m_VendorId) {
	case PCIV_AMDATI:
		if (exFmt.NominalRange == DXVA2_NominalRange_16_235) {
			// hack for AMD 
			// AMD Vega 8 (Rizen 5 Mobile 3500U), driver 25.8.1
			m_BltParams.DestFormat.NominalRange = DXVA2_NominalRange_16_235;
		}
		break;
	case PCIV_NVIDIA:
		if (exFmt.NominalRange == DXVA2_NominalRange_0_255) {
			// hack for Nvidia
			// Nvidia RTX 5060, driver 591.74
			m_BltParams.DestFormat.NominalRange = DXVA2_NominalRange_16_235;
		}
		break;
	//case PCIV_INTEL:
		// for Intel, no hack is required (and the hack doesn't change anything)
		// Intel UHD 750 (i5-11500), driver 30.0.101.1273
	}

	// Ask this processor which request actually produces full range RGB.  When the probe cannot
	// say, the vendor choice above stays.
	const DXVA2_NominalRange probed = ProbeDestNominalRange(inputFmt, outputFmt, exFmt, width, height);
	if (probed != DXVA2_NominalRange_Unknown) {
		m_BltParams.DestFormat.NominalRange = probed;
	}

	m_srcFormat   = inputFmt;
	m_srcWidth    = width;
	m_srcHeight   = height;

	return hr;
}

void CDXVA2VP::ReleaseVideoProcessor()
{
	m_VideoSamples.Clear();

	m_pDXVA2_VP.Release();

	m_DXVA2VPcaps = {};
	m_NumRefSamples = 1;

	m_srcFormat   = D3DFMT_UNKNOWN;
	m_srcWidth    = 0;
	m_srcHeight   = 0;
}

HRESULT CDXVA2VP::AddMediaSampleAndSurface(IMediaSample* pSample, IDirect3DSurface9* pSurface, const UINT frameNum, const DXVA2_SampleFormat sampleFmt)
{
	CheckPointer(pSurface, E_POINTER);

	m_VideoSamples.AddExternalSampleInfo(pSample, frameNum, sampleFmt, pSurface);

	return E_ABORT;
}

IDirect3DSurface9* CDXVA2VP::GetNextInputSurface(const UINT frameNum, const DXVA2_SampleFormat sampleFmt)
{
	CComPtr<IDirect3DSurface9> pSurface;

	if (m_VideoSamples.Size() < m_VideoSamples.MaxSize()) {
		HRESULT hr = m_pDXVA2_VPService->CreateSurface(
			m_srcWidth,
			m_srcHeight,
			0,
			m_srcFormat,
			m_DXVA2VPcaps.InputPool,
			0,
			DXVA2_VideoProcessorRenderTarget,
			&pSurface,
			nullptr
		);
		if (S_OK == hr) {
			IDirect3DDevice9* pDevice;
			if (S_OK == pSurface->GetDevice(&pDevice)) {
				hr = pDevice->ColorFill(pSurface, nullptr, D3DCOLOR_XYUV(0, 128, 128));
				pDevice->Release();
			}
		}
		else {
			DLog(L"CDXVA2VP::GetNextInputSurface() : CreateSurface failed with error {}", HR2Str(hr));
			return nullptr;
		}
	}

	return m_VideoSamples.GetNextInternalSurface(frameNum, sampleFmt, pSurface);
}

void CDXVA2VP::CleanSamples()
{
	m_VideoSamples.Clean();
	m_VideoSamples.Clear();
}

void CDXVA2VP::SetRectangles(const CRect& srcRect, const CRect& dstRect)
{
	m_BltParams.TargetRect = dstRect;
	m_BltParams.ConstrictionSize.cx = dstRect.Width();
	m_BltParams.ConstrictionSize.cy = dstRect.Height();

	// Initialize main stream video samples
	m_VideoSamples.SetRects(srcRect, dstRect);
}

void CDXVA2VP::SetProcAmpValues(DXVA2_ProcAmpValues& PropValues)
{
	m_BltParams.ProcAmpValues.Brightness.ll = std::clamp(PropValues.Brightness.ll, m_DXVA2ProcAmpRanges[0].MinValue.ll, m_DXVA2ProcAmpRanges[0].MaxValue.ll);
	m_BltParams.ProcAmpValues.Contrast.ll   = std::clamp(PropValues.Contrast.ll,   m_DXVA2ProcAmpRanges[1].MinValue.ll, m_DXVA2ProcAmpRanges[1].MaxValue.ll);
	m_BltParams.ProcAmpValues.Hue.ll        = std::clamp(PropValues.Hue.ll,        m_DXVA2ProcAmpRanges[2].MinValue.ll, m_DXVA2ProcAmpRanges[2].MaxValue.ll);
	m_BltParams.ProcAmpValues.Saturation.ll = std::clamp(PropValues.Saturation.ll, m_DXVA2ProcAmpRanges[3].MinValue.ll, m_DXVA2ProcAmpRanges[3].MaxValue.ll);
	m_bUpdateFilters = true;
}

void CDXVA2VP::GetProcAmpRanges(DXVA2_ValueRange(&PropRanges)[4])
{
	PropRanges[0] = m_DXVA2ProcAmpRanges[0];
	PropRanges[1] = m_DXVA2ProcAmpRanges[1];
	PropRanges[2] = m_DXVA2ProcAmpRanges[2];
	PropRanges[3] = m_DXVA2ProcAmpRanges[3];
}

HRESULT CDXVA2VP::Process(IDirect3DSurface9* pRenderTarget, const DXVA2_SampleFormat sampleFormat, const bool second)
{
	if (!m_VideoSamples.Size()) {
		return E_ABORT;
	}

	// Initialize VPBlt parameters
	m_BltParams.TargetFrame = m_VideoSamples.GetTargetFrameTime(m_DXVA2VPcaps.NumBackwardRefSamples, second);

	HRESULT hr = m_pDXVA2_VP->VideoProcessBlt(pRenderTarget, &m_BltParams, m_VideoSamples.Data(), m_VideoSamples.Size(), nullptr);
	DLogIf(FAILED(hr), L"CDXVA2VP::Process() : VideoProcessBlt() failed with error {}", HR2Str(hr));

	return hr;
}
