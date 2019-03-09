/*
 * (C) 2018-2019 see Authors.txt
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

enum :int {
	SURFMT_8INT = 0,
	SURFMT_10INT,
	SURFMT_16FLOAT
};

enum :int {
	//UPSCALE_Nearest = 0,
	//UPSCALE_Bilinear,
	UPSCALE_Mitchell,
	UPSCALE_CatmullRom,
	UPSCALE_Lanczos2,
	UPSCALE_Lanczos3,
	UPSCALE_COUNT,
};

enum :int {
	DOWNSCALE_Box = 0,
	DOWNSCALE_Bilinear,
	DOWNSCALE_Hamming,
	DOWNSCALE_Bicubic,
	DOWNSCALE_Lanczos,
	DOWNSCALE_COUNT,
};

struct VRFrameInfo {
	GUID Subtype;
	unsigned Width;
	unsigned Height;
	DXVA2_ExtendedFormat ExtFormat;
};

interface __declspec(uuid("1AB00F10-5F55-42AC-B53F-38649F11BE3E"))
IVideoRenderer : public IUnknown {
	STDMETHOD(get_AdapterDecription) (CStringW& str) PURE;
	STDMETHOD_(bool, get_UsedD3D11()) PURE;
	STDMETHOD(get_FrameInfo) (VRFrameInfo* pFrameInfo) PURE;
	STDMETHOD(get_DXVA2VPCaps) (DXVA2_VideoProcessorCaps* pDXVA2VPCaps) PURE;

	STDMETHOD_(bool, GetActive()) PURE;

	STDMETHOD_(void, GetSettings(
		bool &bUseD3D11,
		bool &bShowStats,
		bool &bDeintDouble,
		int  &iSurfaceFmt,
		int  &iUpscaling,
		int  &iDownscaling,
		bool &bInterpolateAt50pct
	)) PURE;
	STDMETHOD_(void, SetSettings(
		bool bUseD3D11,
		bool bShowStats,
		bool bDeintDouble,
		int  iSurfaceFmt,
		int  iUpscaling,
		int  iDownscaling,
		bool bInterpolateAt50pct
	)) PURE;

	STDMETHOD(SaveSettings()) PURE;
};
