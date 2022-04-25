/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2016 see Authors.txt
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

#include <ObjBase.h>
#include <strmif.h>

interface IDirect3DDevice9;
interface ID3D11Device1;
interface ID3D11DeviceContext1;

DECLARE_INTERFACE_IID_(ISubRenderCallback, IUnknown, "CD6D2AA5-20D3-4ebe-A8A9-34D3B00CC253")
{
	// NULL means release current device, textures and other resources
	STDMETHOD(SetDevice)(IDirect3DDevice9 * dev) PURE;

	// destination video rectangle, will be inside (0, 0)-(width, height)
	// width,height is the size of the entire output window
	STDMETHOD(Render)(REFERENCE_TIME rtStart,
		int left, int top, int right, int bottom,
		int width, int height) PURE;
};

DECLARE_INTERFACE_IID_(ISubRenderCallback2, ISubRenderCallback, "E602585E-C05A-4828-AC69-AF92997F2E0C")
{
	STDMETHOD(RenderEx)(REFERENCE_TIME rtStart, REFERENCE_TIME rtStop,
		REFERENCE_TIME avgTimePerFrame,
		int left, int top, int right, int bottom,
		int width, int height) PURE;
};

DECLARE_INTERFACE_IID_(ISubRenderCallback3, ISubRenderCallback2, "BAC4273A-3EAD-47F5-9710-8488E52AC618")
{
	STDMETHOD(RenderEx2)(REFERENCE_TIME rtStart, REFERENCE_TIME rtStop,
		REFERENCE_TIME avgTimePerFrame, RECT croppedVideoRect,
		RECT originalVideoRect, RECT viewportRect,
		const double videoStretchFactor = 1.0) PURE;
};

DECLARE_INTERFACE_IID_(ISubRenderCallback4, ISubRenderCallback3, "C89CF1D4-29C5-4A96-8AAC-528EC6F7AF1E")
{
	STDMETHOD(RenderEx3)(REFERENCE_TIME rtStart, REFERENCE_TIME rtStop,
		REFERENCE_TIME avgTimePerFrame, RECT croppedVideoRect,
		RECT originalVideoRect, RECT viewportRect,
		const double videoStretchFactor = 1.0,
		int xOffsetInPixels = 0, DWORD flags = 0) PURE;
};

DECLARE_INTERFACE_IID_(ISubRender, IUnknown, "9CC7F9F7-3ED1-493c-AF65-527EA1D9947F")
{
	STDMETHOD(SetCallback)(ISubRenderCallback * cb) PURE;
};

/*D3D11 render*/
DECLARE_INTERFACE_IID_(ISubRender11Callback, IUnknown, "1B430F17-4CB2-4C6B-A850-1847F9677C75")
{
	// NULL means release current device, textures and other resources
	STDMETHOD(SetDevice)(ID3D11Device1 * dev) PURE;

	// destination video rectangle, will be inside (0, 0)-(width, height)
	// width,height is the size of the entire output window
	STDMETHOD(Render11)(REFERENCE_TIME rtStart, REFERENCE_TIME rtStop,
		REFERENCE_TIME avgTimePerFrame, RECT croppedVideoRect,
		RECT originalVideoRect, RECT viewportRect,
		const double videoStretchFactor = 1.0,
		int xOffsetInPixels = 0, DWORD flags = 0) PURE;
};

DECLARE_INTERFACE_IID_(ISubRender11, IUnknown, "524FA4AC-35CF-402B-9015-300FBC432563")
{
	STDMETHOD(SetCallback11)(ISubRender11Callback * cb) PURE;
};