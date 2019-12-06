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

class CD3D11Quadrilateral
{
protected:
	ID3D11Device* m_pDevice = nullptr;
	ID3D11DeviceContext* m_pDeviceContext = nullptr;

	bool m_bAlphaBlend = false;
	ID3D11BlendState* m_pBlendState = nullptr;

	struct VERTEX {
		DirectX::XMFLOAT3 Pos;
		DirectX::XMFLOAT4 Color;
	};
	VERTEX m_Vertices[6] = {};
	ID3D11Buffer* m_pVertexBuffer = nullptr;
	ID3D11InputLayout* m_pInputLayout = nullptr;

	ID3D11VertexShader* m_pVertexShader = nullptr;
	ID3D11PixelShader* m_pPixelShader = nullptr;

public:
	~CD3D11Quadrilateral();

	HRESULT InitDeviceObjects(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
	void InvalidateDeviceObjects();

	HRESULT Set(const float x1, const float y1, const float x2, const float y2, const float x3, const float y3, const float x4, const float y4, const D3DCOLOR color);
	HRESULT Draw(ID3D11RenderTargetView* pRenderTargetView, UINT w, UINT h);
};


class CD3D11Rectangle : public CD3D11Quadrilateral
{
private:
	using CD3D11Quadrilateral::Set;

public:
	HRESULT Set(const int left, const int top, const int right, const int bottom, const D3DCOLOR color)
	{
		return CD3D11Quadrilateral::Set(left, top, right, top,  right, bottom, left, bottom, color);
	}
	HRESULT Set(const RECT& rect, const D3DCOLOR color)
	{
		return CD3D11Quadrilateral::Set(rect.left, rect.top, rect.right, rect.top,  rect.right, rect.bottom, rect.left, rect.bottom, color);
	}
};


class CD3D11Line : public CD3D11Quadrilateral
{
private:
	using CD3D11Quadrilateral::Set;

public:
	HRESULT Set(const int x1, const int y1, const int x2, const int y2, const int thickness, const D3DCOLOR color)
	{
		const float a = atan2f(y1 - y2, x2 - x1);
		const float xt = thickness * sinf(a);
		const float yt = thickness * cosf(a);
		const float x3 = x2 + xt;
		const float y3 = y2 + yt;
		const float x4 = x1 + xt;
		const float y4 = y1 + yt;

		return CD3D11Quadrilateral::Set(x1, y1, x2, y2, x3, y3, x4, y4, color);
	}
};
