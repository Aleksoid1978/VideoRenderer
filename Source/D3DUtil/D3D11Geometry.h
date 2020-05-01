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

struct POINTVERTEX11 {
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT4 Color;
};

// CD3D11Quadrilateral

class CD3D11Quadrilateral
{
protected:
	ID3D11Device* m_pDevice = nullptr;
	ID3D11DeviceContext* m_pDeviceContext = nullptr;

	bool m_bAlphaBlend = false;
	ID3D11BlendState* m_pBlendState = nullptr;

	POINTVERTEX11 m_Vertices[4] = {};
	ID3D11Buffer* m_pVertexBuffer = nullptr;
	ID3D11InputLayout* m_pInputLayout = nullptr;

	ID3D11VertexShader* m_pVertexShader = nullptr;
	ID3D11PixelShader* m_pPixelShader = nullptr;

public:
	~CD3D11Quadrilateral();

	HRESULT InitDeviceObjects(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
	void InvalidateDeviceObjects();

	HRESULT Set(const float x1, const float y1, const float x2, const float y2, const float x3, const float y3, const float x4, const float y4, const D3DCOLOR color);
	HRESULT Draw(ID3D11RenderTargetView* pRenderTargetView, const SIZE& rtSize);
};

// CD3D11Rectangle

class CD3D11Rectangle : public CD3D11Quadrilateral
{
private:
	using CD3D11Quadrilateral::Set;

public:
	HRESULT Set(const RECT& rect, const SIZE& rtSize, const D3DCOLOR color);
};

// CD3D11Stripe

class CD3D11Stripe : public CD3D11Quadrilateral
{
private:
	using CD3D11Quadrilateral::Set;

public:
	HRESULT Set(const int x1, const int y1, const int x2, const int y2, const int thickness, const D3DCOLOR color);
};

// CD3D11Dots

class CD3D11Dots
{
protected:
	ID3D11Device* m_pDevice = nullptr;
	ID3D11DeviceContext* m_pDeviceContext = nullptr;

	ID3D11InputLayout*  m_pInputLayout  = nullptr;
	ID3D11VertexShader* m_pVertexShader = nullptr;
	ID3D11PixelShader*  m_pPixelShader  = nullptr;

	ID3D11Buffer* m_pVertexBuffer = nullptr;

	SIZE m_RTSize = {};
	bool m_bAlphaBlend = false;
	std::vector<POINTVERTEX11> m_Vertices;

	virtual inline bool CheckNumPoints(const UINT num)
	{
		return (num > 0);
	}

	virtual inline void DrawPrimitive()
	{
		m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
		m_pDeviceContext->Draw(m_Vertices.size(), 0);
	}

public:
	~CD3D11Dots();

	HRESULT InitDeviceObjects(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
	void InvalidateDeviceObjects();

	void ClearPoints(SIZE& newRTSize);
	bool AddPoints(POINT* poins, const UINT size, const D3DCOLOR color);
	bool AddGFPoints(
		int Xstart, int Xstep,
		int Yaxis, int* Ydata, UINT Yoffset,
		const UINT size, const D3DCOLOR color);

	HRESULT UpdateVertexBuffer();
	void Draw();
};

// CD3D11Lines

class CD3D11Lines : public CD3D11Dots
{
private:
	using CD3D11Dots::AddGFPoints;

protected:
	inline bool CheckNumPoints(const UINT num) override
	{
		return (num >= 2 && !(num & 1));
	}

	inline void DrawPrimitive() override
	{
		m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
		m_pDeviceContext->Draw(m_Vertices.size(), 0);
	}
};

// CD3D9Polyline

class CD3D11Polyline : public CD3D11Dots
{
protected:
	inline bool CheckNumPoints(const UINT num) override
	{
		return (num >= 2 || m_Vertices.size() && num > 0);
	}

	inline void DrawPrimitive() override
	{
		m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
		m_pDeviceContext->Draw(m_Vertices.size(), 0);
	}
};
