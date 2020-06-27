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

#include <d3d11.h>
#include "D3DCommon.h"

class CD3D11Font
{
	// Font properties
	std::wstring m_strFontName;
	DWORD m_dwFontHeight = 0;
	DWORD m_dwFontFlags  = 0;

	WCHAR m_Characters[128];
	FloatRect m_fTexCoords[128] = {};

	D3DCOLOR m_Color = D3DCOLOR_XRGB(255, 255, 255);

	ID3D11Device* m_pDevice = nullptr;
	ID3D11DeviceContext* m_pDeviceContext = nullptr;

	ID3D11InputLayout*        m_pInputLayout    = nullptr;
	ID3D11VertexShader*       m_pVertexShader   = nullptr;
	ID3D11PixelShader*        m_pPixelShader    = nullptr;
	ID3D11Texture2D*          m_pTexture        = nullptr;
	ID3D11ShaderResourceView* m_pShaderResource = nullptr;
	ID3D11Buffer*             m_pVertexBuffer   = nullptr;
	//ID3D11Buffer*             m_pIndexBuffer    = nullptr;
	ID3D11Buffer*             m_pPixelBuffer    = nullptr;
	ID3D11SamplerState*       m_pSamplerState   = nullptr;
	ID3D11BlendState*         m_pBlendState     = nullptr;

	UINT  m_uTexWidth = 0;                   // Texture dimensions
	UINT  m_uTexHeight = 0;
	float m_fTextScale = 1.0f;

public:
	// Constructor / destructor
	CD3D11Font();
	~CD3D11Font();

	// Initializing and destroying device-dependent objects
	HRESULT InitDeviceObjects(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
	void InvalidateDeviceObjects();

	HRESULT CreateFontBitmap(const WCHAR* strFontName, const DWORD dwHeight, const DWORD dwFlags);

	// Function to get extent of text
	HRESULT GetTextExtent(const WCHAR* strText, SIZE* pSize);

	// 2D text drawing function
	HRESULT Draw2DText(ID3D11RenderTargetView* pRenderTargetView, const SIZE& rtSize, float sx, float sy, D3DCOLOR color, const WCHAR* strText);
};
