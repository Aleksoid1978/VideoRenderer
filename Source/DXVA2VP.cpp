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

#include "stdafx.h"
#include "Helper.h"
#include "DX9Helper.h"

#include "DXVA2VP.h"

// TODO
// CDXVA2VP

BOOL CDXVA2VP::CreateDXVA2VPDevice(const GUID devguid, const DXVA2_VideoDesc& videodesc, UINT preferredDeintTech, D3DFORMAT& outputFmt)
{
	DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() started for device %s", DXVA2VPDeviceToString(devguid));
	CheckPointer(m_pDXVA2_VPService, FALSE);

	HRESULT hr = S_OK;

	// Query the supported render target format.
	UINT count;
	D3DFORMAT* formats = nullptr;
	hr = m_pDXVA2_VPService->GetVideoProcessorRenderTargets(devguid, &videodesc, &count, &formats);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : GetVideoProcessorRenderTargets() failed with error %s", HR2Str(hr));
		return FALSE;
	}
#ifdef _DEBUG
	{
		CStringW dbgstr = L"DXVA2-VP output formats:";
		for (UINT j = 0; j < count; j++) {
			dbgstr.AppendFormat(L"\n  %s", D3DFormatToString(formats[j]));
		}
		DLog(dbgstr);
	}
#endif
	outputFmt = D3DFMT_UNKNOWN;
	for (UINT i = 0; i < count; i++) {
		const auto& fmt = formats[i];

		//if (fmt == m_InternalTexFmt) {
		//	outputFmt = m_InternalTexFmt;
		//	break;
		//}
		//
		//if (fmt == D3DFMT_A2R10G10B10 && m_InternalTexFmt == D3DFMT_A16B16G16R16F) {
		//	outputFmt = D3DFMT_A2R10G10B10;
		//}
		//else if (fmt == D3DFMT_X8R8G8B8 && outputFmt == D3DFMT_UNKNOWN) {
		//	outputFmt = D3DFMT_X8R8G8B8;
		//}
	}
	outputFmt = D3DFMT_X8R8G8B8;
	CoTaskMemFree(formats);
	if (outputFmt == D3DFMT_UNKNOWN) {
		DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : FAILED. Device doesn't support desired output format");
		return FALSE;
	}
	DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : select %s for output", D3DFormatToString(outputFmt));

	// Query video processor capabilities.
	hr = m_pDXVA2_VPService->GetVideoProcessorCaps(devguid, &videodesc, outputFmt, &m_DXVA2VPcaps);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP() : GetVideoProcessorCaps() failed with error %s", HR2Str(hr));
		return FALSE;
	}
	if (preferredDeintTech && !(m_DXVA2VPcaps.DeinterlaceTechnology & preferredDeintTech)) {
		DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : skip this device, need improved deinterlacing");
		return FALSE;
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
		DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : CreateVideoProcessor failed with error %s", HR2Str(hr));
		return FALSE;
	}

	// Query ProcAmp ranges.
	for (UINT i = 0; i < std::size(m_DXVA2ProcAmpRanges); i++) {
		if (m_DXVA2VPcaps.ProcAmpControlCaps & (1 << i)) {
			hr = m_pDXVA2_VPService->GetProcAmpRange(devguid, &videodesc, outputFmt, 1 << i, &m_DXVA2ProcAmpRanges[i]);
			if (FAILED(hr)) {
				DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : GetProcAmpRange() failed with error %s", HR2Str(hr));
				return FALSE;
			}
			DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : ProcAmpRange(%u) : %7.2f, %6.2f, %6.2f, %4.2f",
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
	m_BltParams.Alpha = DXVA2_Fixed32OpaqueAlpha();
	m_BltParams.NoiseFilterLuma.Level = NFilterValues[0];
	m_BltParams.NoiseFilterLuma.Threshold = NFilterValues[1];
	m_BltParams.NoiseFilterLuma.Radius = NFilterValues[2];
	m_BltParams.NoiseFilterChroma.Level = NFilterValues[3];
	m_BltParams.NoiseFilterChroma.Threshold = NFilterValues[4];
	m_BltParams.NoiseFilterChroma.Radius = NFilterValues[5];
	m_BltParams.DetailFilterLuma.Level = DFilterValues[0];
	m_BltParams.DetailFilterLuma.Threshold = DFilterValues[1];
	m_BltParams.DetailFilterLuma.Radius = DFilterValues[2];
	m_BltParams.DetailFilterChroma.Level = DFilterValues[3];
	m_BltParams.DetailFilterChroma.Threshold = DFilterValues[4];
	m_BltParams.DetailFilterChroma.Radius = DFilterValues[5];

	DLog(L"CDX9VideoProcessor::CreateDXVA2VPDevice() : create %s processor ", CStringFromGUID(devguid));

	return TRUE;
}

HRESULT CDXVA2VP::InitVideoService(IDirect3DDevice9* pDevice)
{
	// Create DXVA2 Video Processor Service.
	HRESULT hr = DXVA2CreateVideoService(pDevice, IID_IDirectXVideoProcessorService, (VOID**)&m_pDXVA2_VPService);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::Init() : DXVA2CreateVideoService() failed with error %s", HR2Str(hr));
		return FALSE;
	}

	return hr;
}

void CDXVA2VP::ReleaseVideoService()
{
	ReleaseVideoProcessor();

	m_pDXVA2_VPService.Release();
}

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

HRESULT CDXVA2VP::InitVideoProcessor(const D3DFORMAT inputFmt, const UINT width, const UINT height, const bool interlaced, D3DFORMAT& outputFmt)
{
	CheckPointer(m_pDXVA2_VPService, E_FAIL);

	ReleaseVideoProcessor();
	HRESULT hr = S_OK;

	// Initialize the video descriptor.
	DXVA2_VideoDesc videodesc = {};
	videodesc.SampleWidth = width;
	videodesc.SampleHeight = height;
	//videodesc.SampleFormat.value = m_srcExFmt.value; // do not need to fill it here
	videodesc.SampleFormat.SampleFormat = m_bInterlaced ? DXVA2_SampleFieldInterleavedOddFirst : DXVA2_SampleProgressiveFrame;
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
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP() : GetVideoProcessorDeviceGuids() failed with error %s", HR2Str(hr));
		return hr;
	}
	UINT NumRefSamples = 1;
	if (m_bInterlaced) {
		UINT preferredDeintTech = DXVA2_DeinterlaceTech_EdgeFiltering // Intel
			| DXVA2_DeinterlaceTech_FieldAdaptive
			| DXVA2_DeinterlaceTech_PixelAdaptive // Nvidia, AMD
			| DXVA2_DeinterlaceTech_MotionVectorSteered;

		for (UINT i = 0; i < count; i++) {
			auto& devguid = guids[i];
			if (CreateDXVA2VPDevice(devguid, videodesc, preferredDeintTech, outputFmt)) {
				m_DXVA2VPGuid = devguid;
				break; // found!
			}
			m_pDXVA2_VP.Release();
		}

		if (!m_pDXVA2_VP && CreateDXVA2VPDevice(DXVA2_VideoProcBobDevice, videodesc, 0, outputFmt)) {
			m_DXVA2VPGuid = DXVA2_VideoProcBobDevice;
		}
	}

	CoTaskMemFree(guids);

	if (!m_pDXVA2_VP && CreateDXVA2VPDevice(DXVA2_VideoProcProgressiveDevice, videodesc, 0, outputFmt)) { // Progressive or fall-back for interlaced
		m_DXVA2VPGuid = DXVA2_VideoProcProgressiveDevice;
	}

	if (!m_pDXVA2_VP) {
		return E_FAIL;
	}

	NumRefSamples = 1 + m_DXVA2VPcaps.NumBackwardRefSamples + m_DXVA2VPcaps.NumForwardRefSamples;
	ASSERT(NumRefSamples <= MAX_DEINTERLACE_SURFACES);

	m_SrcSamples.Resize(NumRefSamples, 0);

	for (unsigned i = 0; i < NumRefSamples; ++i) {
		auto& vsample = m_SrcSamples.GetAt(i);

		hr = m_pDXVA2_VPService->CreateSurface(
			width,
			height,
			0,
			inputFmt,
			m_DXVA2VPcaps.InputPool,
			0,
			DXVA2_VideoProcessorRenderTarget,
			&vsample.pSrcSurface,
			nullptr
		);
		if (FAILED(hr)) {
			m_SrcSamples.Clear();

			return hr;
		}

		IDirect3DDevice9* pDevice = nullptr;
		if (S_OK == vsample.pSrcSurface->GetDevice(&pDevice)) {
			// fill the surface in black, to avoid the "green screen"
			pDevice->ColorFill(vsample.pSrcSurface, nullptr, D3DCOLOR_XYUV(0, 128, 128));
			pDevice->Release();
		}
	}

	m_SrcSamples.UpdateVideoSamples();

	m_BltParams.DestFormat.value = 0; // output to RGB
	m_BltParams.DestFormat.SampleFormat = DXVA2_SampleProgressiveFrame; // output to progressive RGB
	m_BltParams.DestFormat.NominalRange = DXVA2_NominalRange_0_255; // output to full range RGB

	m_srcFormat   = inputFmt;
	m_srcWidth    = width;
	m_srcHeight   = height;
	m_bInterlaced = interlaced;

	return hr;
}

void CDXVA2VP::ReleaseVideoProcessor()
{
	m_pDXVA2_VP.Release();

	m_DXVA2VPcaps = {};

	// set default ProcAmp ranges and values
	SetDefaultDXVA2ProcAmpRanges(m_DXVA2ProcAmpRanges);
	SetDefaultDXVA2ProcAmpValues(m_BltParams.ProcAmpValues);

	m_srcFormat   = D3DFMT_UNKNOWN;
	m_srcWidth    = 0;
	m_srcHeight   = 0;
	m_bInterlaced = false;
}

HRESULT CDXVA2VP::SetInputSurface(IDirect3DSurface9* pTexture2D)
{
	CheckPointer(pTexture2D, E_POINTER);

	HRESULT hr = S_OK;

	return hr;
}

HRESULT CDXVA2VP::SetProcessParams(const RECT* pSrcRect, const RECT* pDstRect, const DXVA2_ExtendedFormat exFmt)
{
	return S_OK;
}

void CDXVA2VP::SetProcAmpValues(DXVA2_ProcAmpValues *pValues)
{
	m_bUpdateFilters = true;
}

HRESULT CDXVA2VP::Process(IDirect3DSurface9* pRenderTarget, const DXVA2_SampleFormat sampleFormat, const bool second)
{
	HRESULT hr = S_OK;

	return hr;
}

