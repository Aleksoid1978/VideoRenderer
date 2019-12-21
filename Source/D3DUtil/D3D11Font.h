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

#include <d3d11.h>
#include "D3DCommon.h"

class CD3D11Font
{
	// Font properties
	WCHAR m_strFontName[80];
	DWORD m_dwFontHeight;
	DWORD m_dwFontFlags;

	WCHAR m_Characters[128];
	FloatRect m_fTexCoords[128] = {};

	ID3D11Device* m_pDevice = nullptr;
	ID3D11DeviceContext* m_pDeviceContext = nullptr;

	ID3D11InputLayout*        m_pInputLayout    = nullptr;
	ID3D11VertexShader*       m_pVertexShader   = nullptr;
	ID3D11PixelShader*        m_pPixelShader    = nullptr;
	ID3D11Texture2D*          m_pTexture        = nullptr;
	ID3D11ShaderResourceView* m_pShaderResource = nullptr;
	ID3D11Buffer*             m_pVertexBuffer   = nullptr;
	ID3D11Buffer*             m_pIndexBuffer    = nullptr;

	UINT  m_uTexWidth = 0;                   // Texture dimensions
	UINT  m_uTexHeight = 0;
	FLOAT m_fTextScale = 1.0f;

public:
	// Constructor / destructor
	CD3D11Font(const WCHAR* strFontName, DWORD dwHeight, DWORD dwFlags = 0L);
	~CD3D11Font();

	// Initializing and destroying device-dependent objects
	HRESULT InitDeviceObjects(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
	void InvalidateDeviceObjects();

	// Function to get extent of text
	HRESULT GetTextExtent(const WCHAR* strText, SIZE* pSize);

	// 2D text drawing function
	HRESULT Draw2DText(ID3D11RenderTargetView* pRenderTargetView, UINT w, UINT h, FLOAT sx, FLOAT sy, D3DCOLOR color, const WCHAR* strText);
};
