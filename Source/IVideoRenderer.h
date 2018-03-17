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

enum :int {
	ID_AdapterDesc = 1,
	ID_DXVA2VPCaps,
};

struct VRFrameInfo {
	unsigned Width;
	unsigned Height;
	D3DFORMAT D3dFormat;
	DXVA2_ExtendedFormat ExtFormat;
};

interface __declspec(uuid("1AB00F10-5F55-42AC-B53F-38649F11BE3E"))
IVideoRenderer : public IUnknown {
	// The memory for strings and binary data is allocated by the callee
	// by using LocalAlloc. It is the caller's responsibility to release the
	// memory by calling LocalFree.
	STDMETHOD(get_String) (int id, LPWSTR* pstr, int* chars) PURE;
	STDMETHOD(get_Binary) (int id, LPVOID* pbin, int* size) PURE;
	STDMETHOD(get_FrameInfo) (VRFrameInfo* pFrameInfo) PURE;
	STDMETHOD(get_VPDeviceGuid) (GUID* pVPDevGuid) PURE;

	STDMETHOD_(bool, GetOptionUseD3D11()) PURE;
	STDMETHOD(SetOptionUseD3D11(bool value)) PURE;
};
