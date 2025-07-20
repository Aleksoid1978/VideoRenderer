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
	DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() started for device {}", DXVA2VPDeviceToString(devguid));
	CheckPointer(m_pDXVA2_VPService, FALSE);

	HRESULT hr = S_OK;

	// Query the supported render target format.
	UINT count;
	D3DFORMAT* formats = nullptr;
	hr = m_pDXVA2_VPService->GetVideoProcessorRenderTargets(devguid, &videodesc, &count, &formats);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : GetVideoProcessorRenderTargets() failed with error {}", HR2Str(hr));
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
		DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : FAILED. Device doesn't support desired output format");
		return FALSE;
	}
	DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : select {} for output", D3DFormatToString(outputFmt));

	// Query video processor capabilities.
	hr = m_pDXVA2_VPService->GetVideoProcessorCaps(devguid, &videodesc, outputFmt, &m_DXVA2VPcaps);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : GetVideoProcessorCaps() failed with error {}", HR2Str(hr));
		return FALSE;
	}
	if (preferredDeintTech) {
		if (!(m_DXVA2VPcaps.DeinterlaceTechnology & preferredDeintTech)) {
			DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : skip this device, need improved deinterlacing");
			return FALSE;
		}
		if (m_DXVA2VPcaps.NumForwardRefSamples > 0) {
			DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : skip this device, ForwardRefSamples are not supported");
			return FALSE;
		}
	}
	// Check to see if the device is hardware device.
	if (!(m_DXVA2VPcaps.DeviceCaps & DXVA2_VPDev_HardwareDevice)) {
		DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : The DXVA2 device isn't a hardware device");
		return FALSE;
	}
	// Check to see if the device supports all the VP operations we want.
	const UINT VIDEO_REQUIED_OP = DXVA2_VideoProcess_YUV2RGB | DXVA2_VideoProcess_StretchX | DXVA2_VideoProcess_StretchY;
	if ((m_DXVA2VPcaps.VideoProcessorOperations & VIDEO_REQUIED_OP) != VIDEO_REQUIED_OP) {
		DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : The DXVA2 device doesn't support the YUV2RGB & Stretch operations");
		return FALSE;
	}

	// Finally create a video processor device.
	hr = m_pDXVA2_VPService->CreateVideoProcessor(devguid, &videodesc, outputFmt, 0, &m_pDXVA2_VP);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : CreateVideoProcessor failed with error {}", HR2Str(hr));
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
				DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : GetProcAmpRange() failed with error {}", HR2Str(hr));
				return FALSE;
			}
			DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : ProcAmpRange({}) : {:7.2f}, {:6.2f}, {:6.2f}, {:4.2f}",
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

	DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : create {} processor ", GUIDtoWString(devguid));

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
	if (exFmt.NominalRange == DXVA2_NominalRange_0_255 && (m_VendorId == PCIV_NVIDIA || m_VendorId == PCIV_AMDATI)) {
		// hack for Nvidia and AMD, nothing helps Intel
		m_BltParams.DestFormat.NominalRange = DXVA2_NominalRange_16_235;
	} else {
		// output to full range RGB
		m_BltParams.DestFormat.NominalRange = DXVA2_NominalRange_0_255;
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
