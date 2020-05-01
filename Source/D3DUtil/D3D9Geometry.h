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

struct POINTVERTEX9 {
	DirectX::XMFLOAT4 pos;
	DWORD color;
};

// CD3D9Quadrilateral

class CD3D9Quadrilateral
{
protected:
	IDirect3DDevice9* m_pDevice = nullptr;
	IDirect3DVertexBuffer9* m_pVertexBuffer = nullptr;

	bool m_bAlphaBlend = false;
	POINTVERTEX9 m_Vertices[6] = {};

public:
	~CD3D9Quadrilateral();

	HRESULT InitDeviceObjects(IDirect3DDevice9* pDevice);
	void InvalidateDeviceObjects();

	HRESULT Set(
		const float x1, const float y1,
		const float x2, const float y2,
		const float x3, const float y3,
		const float x4, const float y4,
		const D3DCOLOR color);

	HRESULT Draw();
};

// CD3D9Rectangle

class CD3D9Rectangle : public CD3D9Quadrilateral
{
private:
	using CD3D9Quadrilateral::Set;

public:
	HRESULT Set(const RECT& rect, const D3DCOLOR color);
};

// CD3D9Stripe

class CD3D9Stripe : public CD3D9Quadrilateral
{
private:
	using CD3D9Quadrilateral::Set;

public:
	HRESULT Set(const int x1, const int y1, const int x2, const int y2, const int thickness, const D3DCOLOR color);
};

// CD3D9Dots

class CD3D9Dots
{
protected:
	IDirect3DDevice9* m_pDevice = nullptr;
	IDirect3DVertexBuffer9* m_pVertexBuffer = nullptr;

	bool m_bAlphaBlend = false;
	std::vector<POINTVERTEX9> m_Vertices;

	virtual inline bool CheckNumPoints(const UINT num)
	{
		return (num > 0);
	}

	virtual inline HRESULT DrawPrimitive()
	{
		return m_pDevice->DrawPrimitive(D3DPT_POINTLIST, 0, m_Vertices.size());
	}

public:
	~CD3D9Dots();

	HRESULT InitDeviceObjects(IDirect3DDevice9* pDevice);
	void InvalidateDeviceObjects();

	void ClearPoints();
	bool AddPoints(POINT* poins, const UINT size, const D3DCOLOR color);
	bool AddGFPoints(
		int Xstart, int Xstep,
		int Yaxis, int* Ydata, UINT Yoffset,
		const UINT size, const D3DCOLOR color);

	HRESULT UpdateVertexBuffer();
	HRESULT Draw();
};

// CD3D9Lines

class CD3D9Lines : public CD3D9Dots
{
private:
	using CD3D9Dots::AddGFPoints;

protected:
	inline bool CheckNumPoints(const UINT num) override
	{
		return (num >= 2 && !(num & 1));
	}

	inline HRESULT DrawPrimitive() override
	{
		return m_pDevice->DrawPrimitive(D3DPT_LINELIST, 0, m_Vertices.size() / 2);
	}
};

// CD3D9Polyline

class CD3D9Polyline : public CD3D9Dots
{
protected:
	inline bool CheckNumPoints(const UINT num) override
	{
		return (num >= 2 || m_Vertices.size() && num > 0);
	}

	inline HRESULT DrawPrimitive() override
	{
		return m_pDevice->DrawPrimitive(D3DPT_LINESTRIP, 0, m_Vertices.size()-1);
	}
};
