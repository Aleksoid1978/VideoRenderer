/*
 * (C) 2018 see Authors.txt
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

struct VRFrameInfo {
	unsigned Width;
	unsigned Height;
	D3DFORMAT D3dFormat;
	DXVA2_ExtendedFormat ExtFormat;
};

interface __declspec(uuid("1AB00F10-5F55-42AC-B53F-38649F11BE3E"))
IVideoRenderer : public IUnknown {
	STDMETHOD(get_AdapterDecription) (CStringW& str) PURE;
	STDMETHOD_(bool, get_UsedD3D11()) PURE;
	STDMETHOD(get_FrameInfo) (VRFrameInfo* pFrameInfo) PURE;
	STDMETHOD(get_DXVA2VPCaps) (DXVA2_VideoProcessorCaps* pDXVA2VPCaps) PURE;

	STDMETHOD_(bool, GetActive()) PURE;
	STDMETHOD_(bool, GetOptionUseD3D11()) PURE;
	STDMETHOD_(void, SetOptionUseD3D11(bool value)) PURE;
	STDMETHOD_(bool, GetOptionDeintDouble()) PURE;
	STDMETHOD_(void, SetOptionDeintDouble(bool value)) PURE;
	STDMETHOD_(bool, GetOptionAllow10Bit()) PURE;
	STDMETHOD_(void, SetOptionAllow10Bit(bool value)) PURE;
	STDMETHOD_(bool, GetOptionShowStatistics()) PURE;
	STDMETHOD_(void, SetOptionShowStatistics(bool value)) PURE;
	STDMETHOD(SaveSettings()) PURE;
};
