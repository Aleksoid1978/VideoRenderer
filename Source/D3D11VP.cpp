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

#include "stdafx.h"
#include <cmath>
#include "Helper.h"
#include "DX11Helper.h"

#include "D3D11VP.h"

#define ENABLE_FUTUREFRAMES 0

int ValueDXVA2toD3D11(const DXVA2_Fixed32 fixed, const D3D11_VIDEO_PROCESSOR_FILTER_RANGE& range)
{
	float value = DXVA2FixedToFloat(fixed) / range.Multiplier;
	const float k = range.Multiplier * range.Default;
	if (k > 1.0f) {
		value *= range.Default;
	}
	const int level = std::lround(value);
	return std::clamp(level, range.Minimum, range.Maximum);
}

void LevelD3D11toDXVA2(DXVA2_Fixed32& fixed, const int level, const D3D11_VIDEO_PROCESSOR_FILTER_RANGE& range)
{
	float value = range.Multiplier * level;
	const float k = range.Multiplier * range.Default;
	if (k > 1.0f) {
		value /= k;
	}
	fixed = DXVA2FloatToFixed(value);
}

void FilterRangeD3D11toDXVA2(DXVA2_ValueRange& _dxva2_, const D3D11_VIDEO_PROCESSOR_FILTER_RANGE& _d3d11_)
{
	float MinValue = _d3d11_.Multiplier * _d3d11_.Minimum;
	float MaxValue = _d3d11_.Multiplier * _d3d11_.Maximum;
	float DefValue = _d3d11_.Multiplier * _d3d11_.Default;
	float StepSize = 1.0f;
	if (DefValue >= 1.0f) { // capture 1.0f is important for changing StepSize
		MinValue /= DefValue;
		MaxValue /= DefValue;
		DefValue = 1.0f;
		StepSize = 0.01f;
	}
	_dxva2_.MinValue = DXVA2FloatToFixed(MinValue);
	_dxva2_.MaxValue = DXVA2FloatToFixed(MaxValue);
	_dxva2_.DefaultValue = DXVA2FloatToFixed(DefValue);
	_dxva2_.StepSize = DXVA2FloatToFixed(StepSize);
}


// CD3D11VP

HRESULT CD3D11VP::InitVideoDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext)
{
	HRESULT hr = pDevice->QueryInterface(IID_PPV_ARGS(&m_pVideoDevice));
	if (FAILED(hr)) {
		DLog(L"CD3D11VP::InitVideoDevice() : QueryInterface(ID3D11VideoDevice) failed with error {}", HR2Str(hr));
		ReleaseVideoDevice();
		return hr;
	}

	hr = pContext->QueryInterface(IID_PPV_ARGS(&m_pVideoContext));
	if (FAILED(hr)) {
		DLog(L"CD3D11VP::InitVideoDevice() : QueryInterface(ID3D11VideoContext) failed with error {}", HR2Str(hr));
		ReleaseVideoDevice();
		return hr;
	}

#ifdef _DEBUG
	D3D11_VIDEO_PROCESSOR_CONTENT_DESC ContentDesc = { D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE, {}, 1920, 1080, {}, 1920, 1080, D3D11_VIDEO_USAGE_PLAYBACK_NORMAL };
	CComPtr<ID3D11VideoProcessorEnumerator> pVideoProcEnum;
	if (S_OK == m_pVideoDevice->CreateVideoProcessorEnumerator(&ContentDesc, &pVideoProcEnum)) {
		std::wstring input = L"Supported input DXGI formats (for 1080p):";
		std::wstring output = L"Supported output DXGI formats (for 1080p):";
		for (int fmt = DXGI_FORMAT_R32G32B32A32_TYPELESS; fmt <= DXGI_FORMAT_B4G4R4A4_UNORM; fmt++) {
			UINT uiFlags;
			if (S_OK == pVideoProcEnum->CheckVideoProcessorFormat((DXGI_FORMAT)fmt, &uiFlags)) {
				if (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) {
					input.append(L"\n  ");
					input.append(DXGIFormatToString((DXGI_FORMAT)fmt));
				}
				if (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) {
					output.append(L"\n  ");
					output.append(DXGIFormatToString((DXGI_FORMAT)fmt));
				}
			}
		}
		DLog(input);
		DLog(output);
	}
#endif

	return hr;
}

void CD3D11VP::ReleaseVideoDevice()
{
	ReleaseVideoProcessor();

	m_pVideoContext.Release();
	m_pVideoDevice.Release();
}

int GetBitDepth(const DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_R8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_AYUV:
	case DXGI_FORMAT_NV12:
	case DXGI_FORMAT_420_OPAQUE:
	case DXGI_FORMAT_YUY2:
	default:
		return 8;
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_P010:
	case DXGI_FORMAT_Y410:
		return 10;
	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_P016:
	case DXGI_FORMAT_Y416:
		return 16;
	}
}

HRESULT CD3D11VP::InitVideoProcessor(const DXGI_FORMAT inputFmt, const UINT width, const UINT height, const bool interlaced, DXGI_FORMAT& outputFmt)
{
	ReleaseVideoProcessor();
	HRESULT hr = S_OK;

	// create VideoProcessorEnumerator
	D3D11_VIDEO_PROCESSOR_CONTENT_DESC ContentDesc;
	ZeroMemory(&ContentDesc, sizeof(ContentDesc));
	ContentDesc.InputFrameFormat = interlaced ? D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST : D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
	ContentDesc.InputWidth   = width;
	ContentDesc.InputHeight  = height;
	ContentDesc.OutputWidth  = ContentDesc.InputWidth;
	ContentDesc.OutputHeight = ContentDesc.InputHeight;
	ContentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

	hr = m_pVideoDevice->CreateVideoProcessorEnumerator(&ContentDesc, &m_pVideoProcessorEnum);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : CreateVideoProcessorEnumerator() failed with error {}", HR2Str(hr));
		return hr;
	}

	// get VideoProcessorCaps
	hr = m_pVideoProcessorEnum->GetVideoProcessorCaps(&m_VPCaps);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : GetVideoProcessorCaps() failed with error {}", HR2Str(hr));
		return hr;
	}
#ifdef _DEBUG
	std::wstring dbgstr = L"VideoProcessorCaps:";
	dbgstr +=fmt::format(L"\n  Device YCbCr matrix conversion: {}", (m_VPCaps.DeviceCaps  & D3D11_VIDEO_PROCESSOR_DEVICE_CAPS_YCbCr_MATRIX_CONVERSION) ? L"supported" : L"NOT supported");
	dbgstr +=fmt::format(L"\n  Device YUV nominal range      : {}", (m_VPCaps.DeviceCaps  & D3D11_VIDEO_PROCESSOR_DEVICE_CAPS_NOMINAL_RANGE) ? L"supported" : L"NOT supported");
	dbgstr +=fmt::format(L"\n  Feature LEGACY                : {}", (m_VPCaps.FeatureCaps & D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_LEGACY) ? L"Yes" : L"No");
	dbgstr.append(L"\n  Filter capabilities           :");
	if (m_VPCaps.FilterCaps & D3D11_VIDEO_PROCESSOR_FILTER_CAPS_BRIGHTNESS) { dbgstr.append(L" Brightness,"); }
	if (m_VPCaps.FilterCaps & D3D11_VIDEO_PROCESSOR_FILTER_CAPS_CONTRAST)   { dbgstr.append(L" Contrast,"); }
	if (m_VPCaps.FilterCaps & D3D11_VIDEO_PROCESSOR_FILTER_CAPS_HUE)        { dbgstr.append(L" Hue,"); }
	if (m_VPCaps.FilterCaps & D3D11_VIDEO_PROCESSOR_FILTER_CAPS_SATURATION) { dbgstr.append(L" Saturation,"); }
	if (m_VPCaps.FilterCaps & D3D11_VIDEO_PROCESSOR_FILTER_CAPS_NOISE_REDUCTION)    { dbgstr.append(L" Noise reduction,"); }
	if (m_VPCaps.FilterCaps & D3D11_VIDEO_PROCESSOR_FILTER_CAPS_EDGE_ENHANCEMENT)   { dbgstr.append(L" Edge enhancement,"); }
	if (m_VPCaps.FilterCaps & D3D11_VIDEO_PROCESSOR_FILTER_CAPS_ANAMORPHIC_SCALING) { dbgstr.append(L" Anamorphic scaling,"); }
	if (m_VPCaps.FilterCaps & D3D11_VIDEO_PROCESSOR_FILTER_CAPS_STEREO_ADJUSTMENT)  { dbgstr.append(L" Stereo adjustment"); }
	str_trim_end(dbgstr, ',');
	dbgstr +=fmt::format(L"\n  InputFormat interlaced RGB    : {}", (m_VPCaps.InputFormatCaps & D3D11_VIDEO_PROCESSOR_FORMAT_CAPS_RGB_INTERLACED) ? L"supported" : L"NOT supported");
	dbgstr +=fmt::format(L"\n  InputFormat RGB ProcAmp       : {}", (m_VPCaps.InputFormatCaps & D3D11_VIDEO_PROCESSOR_FORMAT_CAPS_RGB_PROCAMP) ? L"supported" : L"NOT supported");
	dbgstr.append(L"\n  AutoStream image processing   :");
	if (m_VPCaps.AutoStreamCaps & D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_DENOISE)             { dbgstr.append(L" Denoise,"); }
	if (m_VPCaps.AutoStreamCaps & D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_DERINGING)           { dbgstr.append(L" Deringing,"); }
	if (m_VPCaps.AutoStreamCaps & D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_EDGE_ENHANCEMENT)    { dbgstr.append(L" Edge enhancement,"); }
	if (m_VPCaps.AutoStreamCaps & D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_COLOR_CORRECTION)    { dbgstr.append(L" Color correction,"); }
	if (m_VPCaps.AutoStreamCaps & D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_FLESH_TONE_MAPPING)  { dbgstr.append(L" Flesh tone mapping,"); }
	if (m_VPCaps.AutoStreamCaps & D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_IMAGE_STABILIZATION) { dbgstr.append(L" Image stabilization,"); }
	if (m_VPCaps.AutoStreamCaps & D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_SUPER_RESOLUTION)    { dbgstr.append(L" Super resolution,"); }
	if (m_VPCaps.AutoStreamCaps & D3D11_VIDEO_PROCESSOR_AUTO_STREAM_CAPS_ANAMORPHIC_SCALING)  { dbgstr.append(L" Anamorphic scaling"); }
	str_trim_end(dbgstr, ',');
	DLog(dbgstr);
#endif

	// check input format
	UINT uiFlags;
	hr = m_pVideoProcessorEnum->CheckVideoProcessorFormat(inputFmt, &uiFlags);
	if (FAILED(hr) || 0 == (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)) {
		return E_INVALIDARG;
	}

	// select output format
	// always overriding the output format because there are problems with FLOAT
	if (GetBitDepth(inputFmt) <= 8 && (outputFmt == DXGI_FORMAT_B8G8R8A8_UNORM || outputFmt == DXGI_FORMAT_UNKNOWN)) {
		outputFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
	} else {
		outputFmt = DXGI_FORMAT_R10G10B10A2_UNORM;
	}

	hr = m_pVideoProcessorEnum->CheckVideoProcessorFormat(outputFmt, &uiFlags);
	if (FAILED(hr) || 0 == (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
		if (outputFmt != DXGI_FORMAT_B8G8R8A8_UNORM) {
			DLog(L"CDX11VideoProcessor::InitializeD3D11VP() {} is not supported for D3D11 VP output. DXGI_FORMAT_B8G8R8A8_UNORM will be used.", DXGIFormatToString(outputFmt));
			outputFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
			hr = m_pVideoProcessorEnum->CheckVideoProcessorFormat(outputFmt, &uiFlags);
			if (FAILED(hr) || 0 == (uiFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
				DLog(L"CDX11VideoProcessor::InitializeD3D11VP() DXGI_FORMAT_B8G8R8A8_UNORM is not supported for D3D11 VP output.");
				return E_INVALIDARG;
			}
		} else {
			DLog(L"CDX11VideoProcessor::InitializeD3D11VP() {} is not supported for D3D11 VP output.", DXGIFormatToString(outputFmt));
			return E_INVALIDARG;
		}
	}

	m_RateConvIndex = 0;
	if (interlaced) {
		// try to find best processor
		const UINT preferredDeintCaps = D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BLEND
			| D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BOB
			| D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_ADAPTIVE
			| D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_MOTION_COMPENSATION;
		UINT maxProcCaps = 0;

		D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS convCaps = {};
		for (UINT i = 0; i < m_VPCaps.RateConversionCapsCount; i++) {
			if (S_OK == m_pVideoProcessorEnum->GetVideoProcessorRateConversionCaps(i, &convCaps)) {
				// check only deinterlace caps
				if ((convCaps.ProcessorCaps & preferredDeintCaps) > maxProcCaps) {
					m_RateConvIndex = i;
					maxProcCaps = convCaps.ProcessorCaps & preferredDeintCaps;
				}
			}
		}

		DLogIf(!maxProcCaps, L"CDX11VideoProcessor::InitializeD3D11VP() : deinterlace caps don't support");
		if (maxProcCaps) {
			if (S_OK == m_pVideoProcessorEnum->GetVideoProcessorRateConversionCaps(m_RateConvIndex, &m_RateConvCaps)) {
#ifdef _DEBUG
				dbgstr = fmt::format(L"RateConversionCaps[{}]:", m_RateConvIndex);
				dbgstr.append(L"\n  ProcessorCaps:");
				if (m_RateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BLEND)               { dbgstr.append(L" Blend,"); }
				if (m_RateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BOB)                 { dbgstr.append(L" Bob,"); }
				if (m_RateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_ADAPTIVE)            { dbgstr.append(L" Adaptive,"); }
				if (m_RateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_MOTION_COMPENSATION) { dbgstr.append(L" Motion Compensation,"); }
				if (m_RateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_INVERSE_TELECINE)                { dbgstr.append(L" Inverse Telecine,"); }
				if (m_RateConvCaps.ProcessorCaps & D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_FRAME_RATE_CONVERSION)           { dbgstr.append(L" Frame Rate Conversion"); }
				str_trim_end(dbgstr, ',');
				dbgstr += fmt::format(L"\n  PastFrames   : {}", m_RateConvCaps.PastFrames);
				dbgstr += fmt::format(L"\n  FutureFrames : {}", m_RateConvCaps.FutureFrames);
				DLog(dbgstr);
#endif
			}
		}
	}

	hr = m_pVideoDevice->CreateVideoProcessor(m_pVideoProcessorEnum, m_RateConvIndex, &m_pVideoProcessor);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : CreateVideoProcessor() failed with error {}", HR2Str(hr));
		return hr;
	}

	for (UINT i = 0; i < std::size(m_VPFilters); i++) {
		auto& filter = m_VPFilters[i];
		filter.support = m_VPCaps.FilterCaps & (1 << i);
		HRESULT hr2 = E_FAIL;
		if (filter.support) {
			hr2 = m_pVideoProcessorEnum->GetVideoProcessorFilterRange((D3D11_VIDEO_PROCESSOR_FILTER)i, &filter.range);
			DLogIf(SUCCEEDED(hr2) ,L"CDX11VideoProcessor::InitializeD3D11VP() : FilterRange({}) : {:5d}, {:3d}, {:4d}, {:f}",
				i, filter.range.Minimum, filter.range.Default, filter.range.Maximum, filter.range.Multiplier);

			if (i == D3D11_VIDEO_PROCESSOR_FILTER_NOISE_REDUCTION || i == D3D11_VIDEO_PROCESSOR_FILTER_EDGE_ENHANCEMENT) {
				filter.value = filter.range.Default; // disable it
			}
		}
		if (FAILED(hr2)) {
			DLog(L"CDX11VideoProcessor::InitializeD3D11VP() : GetVideoProcessorFilterRange({}) failed with error {}", i, HR2Str(hr2));
			filter.support = 0;
			m_VPFilters[i].range = {};
		}
	}

	// Output rate (repeat frames)
	m_pVideoContext->VideoProcessorSetStreamOutputRate(m_pVideoProcessor, 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, TRUE, nullptr);
	// disable automatic video quality by driver
	m_pVideoContext->VideoProcessorSetStreamAutoProcessingMode(m_pVideoProcessor, 0, FALSE);
	// Output background color (black)
	static const D3D11_VIDEO_COLOR backgroundColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	m_pVideoContext->VideoProcessorSetOutputBackgroundColor(m_pVideoProcessor, FALSE, &backgroundColor);
	// other
	m_pVideoContext->VideoProcessorSetOutputTargetRect(m_pVideoProcessor, FALSE, nullptr);
	m_pVideoContext->VideoProcessorSetStreamRotation(m_pVideoProcessor, 0, m_Rotation ? TRUE : FALSE, m_Rotation);

	m_srcFormat   = inputFmt;
	m_srcWidth    = width;
	m_srcHeight   = height;
	//m_bInterlaced = interlaced;

	return hr;
}

void CD3D11VP::ReleaseVideoProcessor()
{
	m_VideoTextures.Clear();

	m_pVideoProcessor.Release();
	m_pVideoProcessorEnum.Release();

	m_VPCaps = {};
	m_RateConvIndex = 0;
	m_RateConvCaps = {};

	m_srcFormat   = DXGI_FORMAT_UNKNOWN;
	m_srcWidth    = 0;
	m_srcHeight   = 0;
	//m_bInterlaced = false;
}

HRESULT CD3D11VP::InitInputTextures(ID3D11Device* pDevice)
{
#if ENABLE_FUTUREFRAMES
	m_VideoTextures.Resize(1 + m_RateConvCaps.PastFrames + m_RateConvCaps.FutureFrames);
#else
	m_VideoTextures.Resize(1 + m_RateConvCaps.PastFrames);
#endif

	HRESULT hr = E_NOT_VALID_STATE;

	for (UINT i = 0; i < m_VideoTextures.Size(); i++) {
		ID3D11Texture2D** ppTexture = m_VideoTextures.GetTexture(i);
		D3D11_TEXTURE2D_DESC texdesc = CreateTex2DDesc(m_srcFormat, m_srcWidth, m_srcHeight, Tex2D_Default);

		hr = pDevice->CreateTexture2D(&texdesc, nullptr, ppTexture);
		if (S_OK == hr) {
			D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = {};
			inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
			hr = m_pVideoDevice->CreateVideoProcessorInputView(*ppTexture, m_pVideoProcessorEnum, &inputViewDesc, m_VideoTextures.GetInputView(i));
		}
	}

	return hr;
}

ID3D11Texture2D* CD3D11VP::GetNextInputTexture(const D3D11_VIDEO_FRAME_FORMAT vframeFormat)
{
	if (m_VideoTextures.Size()) {
		if (vframeFormat == D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE) {
			m_nInputFrameOrField++;
		} else {
			m_nInputFrameOrField += 2;
		}
		if (!m_bPresentFrame) {
			m_bPresentFrame = true;
		}
#if ENABLE_FUTUREFRAMES
		else if (m_nFutureFrames < m_RateConvCaps.FutureFrames) {
			m_nFutureFrames++;
		}
#endif
		else if (m_nPastFrames < m_RateConvCaps.PastFrames) {
			m_nPastFrames++;
		}

		m_VideoTextures.Rotate();
	}

	return *m_VideoTextures.GetTexture();
}

void CD3D11VP::ResetFrameOrder()
{
	m_nInputFrameOrField = 0;
	m_bPresentFrame      = false;
	m_nPastFrames        = 0;
	m_nFutureFrames      = 0;
}

void CD3D11VP::GetVPParams(D3D11_VIDEO_PROCESSOR_CAPS& caps, UINT& rateConvIndex, D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS& rateConvCaps)
{
	caps = m_VPCaps;
	rateConvIndex = m_RateConvIndex;
	rateConvCaps = m_RateConvCaps;
}

HRESULT CD3D11VP::SetRectangles(const RECT* pSrcRect, const RECT* pDstRect)
{
	CheckPointer(m_pVideoContext, E_ABORT);

	m_pVideoContext->VideoProcessorSetStreamSourceRect(m_pVideoProcessor, 0, pSrcRect ? TRUE : FALSE, pSrcRect);
	m_pVideoContext->VideoProcessorSetStreamDestRect(m_pVideoProcessor, 0, pDstRect ? TRUE : FALSE, pDstRect);

	return S_OK;
}

HRESULT CD3D11VP::SetColorSpace(const DXVA2_ExtendedFormat exFmt)
{
	CheckPointer(m_pVideoContext, E_ABORT);

#if 0
	CComPtr<ID3D11VideoContext1> pVideoContext1;
	if (S_OK == m_pVideoContext->QueryInterface(IID_PPV_ARGS(&pVideoContext1))) {
		DLog(L"CD3D11VP::SetColorSpace() : used ID3D11VideoContext1");

		DXGI_COLOR_SPACE_TYPE cstype = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
		if (exFmt.value) {
			bool fullrange = (exFmt.NominalRange == DXVA2_NominalRange_0_255);
			bool topleft = (exFmt.VideoChromaSubsampling == DXVA2_VideoChromaSubsampling_Cosited);

			if (exFmt.VideoTransferMatrix == VIDEOTRANSFERMATRIX_BT2020_10 || exFmt.VideoTransferMatrix == VIDEOTRANSFERMATRIX_BT2020_12) {
				if (exFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084) {
					cstype = topleft ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020 : DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020;
				}
				else if (exFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG) {
					cstype = fullrange ? DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020 : DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020;
				}
				else if (exFmt.VideoTransferFunction == DXVA2_VideoTransFunc_sRGB) {
					cstype = topleft ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020 : DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020;
				}
				else { // DXVA2_VideoTransFunc_22 and other
					cstype = topleft ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020
						: fullrange ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020 : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020;
				}
			}
			else if (exFmt.VideoTransferMatrix == DXVA2_VideoTransferMatrix_BT601) {
				cstype = fullrange ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601 : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
			}
			else { // DXVA2_VideoTransferMatrix_BT709 and other
				if (exFmt.VideoTransferFunction == DXVA2_VideoTransFunc_sRGB) {
					cstype = DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709;
				}
				else { // DXVA2_VideoTransFunc_22  and other
					cstype = fullrange ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709 : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
				}
			}
		}
		pVideoContext1->VideoProcessorSetStreamColorSpace1(m_pVideoProcessor, 0, cstype);
		pVideoContext1->VideoProcessorSetOutputColorSpace1(m_pVideoProcessor, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
		DLog(L"ID3D11VideoContext1::VideoProcessorSetStreamColorSpace1({})", cstype);
	}
	else
#endif
	{
		DLog(L"CD3D11VP::SetColorSpace() : used ID3D11VideoContext");

		D3D11_VIDEO_PROCESSOR_COLOR_SPACE colorSpace = {};
		if (exFmt.value) {
			colorSpace.RGB_Range = 0; // output RGB always full range (0-255)
			colorSpace.YCbCr_Matrix = (exFmt.VideoTransferMatrix == DXVA2_VideoTransferMatrix_BT601) ? 0 : 1;
			colorSpace.Nominal_Range = (exFmt.NominalRange == DXVA2_NominalRange_0_255)
				? D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255
				: D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
		}
		m_pVideoContext->VideoProcessorSetStreamColorSpace(m_pVideoProcessor, 0, &colorSpace);
		m_pVideoContext->VideoProcessorSetOutputColorSpace(m_pVideoProcessor, &colorSpace);
	}

	return S_OK;
}

void CD3D11VP::SetRotation(D3D11_VIDEO_PROCESSOR_ROTATION rotation)
{
	m_Rotation = rotation;

	if (m_pVideoContext) {
		m_pVideoContext->VideoProcessorSetStreamRotation(m_pVideoProcessor, 0, m_Rotation ? TRUE : FALSE, m_Rotation);
	}
}

void CD3D11VP::SetProcAmpValues(DXVA2_ProcAmpValues *pValues)
{
	m_VPFilters[0].value = ValueDXVA2toD3D11(pValues->Brightness, m_VPFilters[0].range);
	m_VPFilters[1].value = ValueDXVA2toD3D11(pValues->Contrast,   m_VPFilters[1].range);
	m_VPFilters[2].value = ValueDXVA2toD3D11(pValues->Hue,        m_VPFilters[2].range);
	m_VPFilters[3].value = ValueDXVA2toD3D11(pValues->Saturation, m_VPFilters[3].range);
	m_bUpdateFilters = true;
}

HRESULT CD3D11VP::Process(ID3D11Texture2D* pRenderTarget, const D3D11_VIDEO_FRAME_FORMAT sampleFormat, const bool second)
{
	ASSERT(m_pVideoDevice);
	ASSERT(m_pVideoContext);

	if (!second) {
		m_pVideoContext->VideoProcessorSetStreamFrameFormat(m_pVideoProcessor, 0, sampleFormat);

		if (m_bUpdateFilters) {
			for (UINT i = 0; i < std::size(m_VPFilters); i++) {
				auto& filter = m_VPFilters[i];
				BOOL bEnable = (filter.support && filter.value != filter.range.Default);
				m_pVideoContext->VideoProcessorSetStreamFilter(m_pVideoProcessor, 0, (D3D11_VIDEO_PROCESSOR_FILTER)i, bEnable, filter.value);
			}
			m_bUpdateFilters = false;
		}
	}

	D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC OutputViewDesc = {};
	OutputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
	CComPtr<ID3D11VideoProcessorOutputView> pOutputView;
	HRESULT hr = m_pVideoDevice->CreateVideoProcessorOutputView(pRenderTarget, m_pVideoProcessorEnum, &OutputViewDesc, &pOutputView);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::ProcessD3D11() : CreateVideoProcessorOutputView() failed with error {}", HR2Str(hr));
		return hr;
	}

	D3D11_VIDEO_PROCESSOR_STREAM StreamData = {};
	StreamData.Enable = TRUE;
	StreamData.InputFrameOrField = m_nInputFrameOrField;
	if (second) {
		StreamData.OutputIndex = 1;
	} else {
		StreamData.InputFrameOrField--;
	}
	if (m_VideoTextures.Size()) {
		UINT idx = m_VideoTextures.Size();
		if (m_nFutureFrames) {
			idx -= m_nFutureFrames;
			StreamData.FutureFrames = m_nFutureFrames;
			StreamData.ppFutureSurfaces = m_VideoTextures.GetInputView(idx);
		}
		idx--;
		StreamData.pInputSurface = *m_VideoTextures.GetInputView(idx);
		if (m_nPastFrames) {
			idx -= m_nPastFrames;
			StreamData.PastFrames = m_nPastFrames;
			StreamData.ppPastSurfaces = m_VideoTextures.GetInputView(idx);
		}
	}
	hr = m_pVideoContext->VideoProcessorBlt(m_pVideoProcessor, pOutputView, StreamData.InputFrameOrField, 1, &StreamData);
	if (FAILED(hr)) {
		DLog(L"CDX11VideoProcessor::ProcessD3D11() : VideoProcessorBlt() failed with error {}", HR2Str(hr));
	}

	return hr;
}
