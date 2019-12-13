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
#include <DirectXMath.h>
#include "Helper.h"
#include "resource.h"
#include "FontBitmap.h"
#include "D3D11Font.h"

#define MAX_NUM_VERTICES 400*6

struct FONT2DVERTEX {
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT4 Color;
	DirectX::XMFLOAT2 Tex;
};

CD3D11Font::CD3D11Font(const WCHAR* strFontName, DWORD dwHeight, DWORD dwFlags)
	: m_dwFontHeight(dwHeight)
	, m_dwFontFlags(dwFlags)
{
	wcsncpy_s(m_strFontName, strFontName, std::size(m_strFontName));
	m_strFontName[std::size(m_strFontName) - 1] = '\0';

	UINT idx = 0;
	for (WCHAR ch = 0x0020; ch < 0x007F; ch++) {
		m_Characters[idx++] = ch;
	}
	m_Characters[idx++] = 0x25CA; // U+25CA Lozenge
	for (WCHAR ch = 0x00A0; ch <= 0x00BF; ch++) {
		m_Characters[idx++] = ch;
	}
	ASSERT(idx == std::size(m_Characters));
}

CD3D11Font::~CD3D11Font()
{
	InvalidateDeviceObjects();
	DeleteDeviceObjects();
}

HRESULT CD3D11Font::InitDeviceObjects(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext)
{
	InvalidateDeviceObjects();
	if (!pDevice || !pDeviceContext) {
		return E_POINTER;
	}

	m_pDevice = pDevice;
	m_pDevice->AddRef();
	m_pDeviceContext = pDeviceContext;
	m_pDeviceContext->AddRef();

	HRESULT hr = S_OK;

	// Keep a local copy of the device

#if FONTBITMAP_MODE == 0
	CFontBitmapGDI fontBitmap;
#elif FONTBITMAP_MODE == 1
	CFontBitmapGDIPlus fontBitmap;
#elif FONTBITMAP_MODE == 2
	CFontBitmapDWrite fontBitmap;
#endif

	hr = fontBitmap.Initialize(m_strFontName, m_dwFontHeight, 0, m_Characters, std::size(m_Characters));
	if (FAILED(hr)) {
		return hr;
	}

	m_uTexWidth = fontBitmap.GetWidth();
	m_uTexHeight = fontBitmap.GetHeight();

	return hr;
}

HRESULT CD3D11Font::RestoreDeviceObjects()
{
	return E_NOTIMPL;
}

void CD3D11Font::InvalidateDeviceObjects()
{
	SAFE_RELEASE(m_pDeviceContext);
	SAFE_RELEASE(m_pDevice);
}

void CD3D11Font::DeleteDeviceObjects()
{
}

HRESULT CD3D11Font::GetTextExtent(const WCHAR* strText, SIZE* pSize)
{
	return E_NOTIMPL;
}

HRESULT CD3D11Font::Draw2DText(FLOAT sx, FLOAT sy, D3DCOLOR color, const WCHAR* strText, DWORD dwFlags)
{
	return E_NOTIMPL;
}
