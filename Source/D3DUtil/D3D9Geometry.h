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

#include <d3d9.h>
#include <vector>
#include "Helper.h"

struct POINTVERTEX {
	FLOAT x, y, z, rhw;
	DWORD color;
};

class CD3D9Quadrilateral
{
protected:
	bool m_bAlphaBlend = false;
	POINTVERTEX m_Vertices[6] = {};
	IDirect3DVertexBuffer9* m_pVertexBuffer = nullptr;

public:
	~CD3D9Quadrilateral()
	{
		InvalidateDeviceObjects();
	}

	HRESULT InitDeviceObjects(IDirect3DDevice9* pD3DDev)
	{
		InvalidateDeviceObjects();
		if (!pD3DDev) {
			return E_POINTER;
		}
		HRESULT hr = pD3DDev->CreateVertexBuffer(6 * sizeof(POINTVERTEX), 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_DEFAULT, &m_pVertexBuffer, nullptr);

		return hr;
	}

	void InvalidateDeviceObjects()
	{
		SAFE_RELEASE(m_pVertexBuffer);
	}

	HRESULT Set(const float x1, const float y1, const float x2, const float y2, const float x3, const float y3, const float x4, const float y4, const D3DCOLOR color)
	{
		HRESULT hr = S_OK;

		m_bAlphaBlend = (color >> 24) < 0xFF;

		m_Vertices[0] = { x1, y1, 0.5f, 1.0f, color };
		m_Vertices[1] = { x2, y2, 0.5f, 1.0f, color };
		m_Vertices[2] = { x3, y3, 0.5f, 1.0f, color };
		m_Vertices[3] = { x1, y1, 0.5f, 1.0f, color };
		m_Vertices[4] = { x3, y3, 0.5f, 1.0f, color };
		m_Vertices[5] = { x4, y4, 0.5f, 1.0f, color };

		if (m_pVertexBuffer) {
			VOID* pVertices;
			hr = m_pVertexBuffer->Lock(0, sizeof(m_Vertices), (void**)&pVertices, 0);
			if (S_OK == hr) {
				memcpy(pVertices, m_Vertices, sizeof(m_Vertices));
				m_pVertexBuffer->Unlock();
			};
		}

		return hr;
	}

	HRESULT Draw(IDirect3DDevice9* pD3DDev)
	{
		if (m_bAlphaBlend) {
			pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			pD3DDev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			pD3DDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		}
		else {
			pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		}
		pD3DDev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);

		HRESULT hr = pD3DDev->SetStreamSource(0, m_pVertexBuffer, 0, sizeof(POINTVERTEX));
		if (S_OK == hr) {
			hr = pD3DDev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
			hr =  pD3DDev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
		}

		return hr;
	}
};


class CD3D9Rectangle : public CD3D9Quadrilateral
{
private:
	using CD3D9Quadrilateral::Set;

public:
	HRESULT Set(const RECT& rect, const D3DCOLOR color)
	{
		return CD3D9Quadrilateral::Set(rect.left, rect.top, rect.right, rect.top, rect.right, rect.bottom, rect.left, rect.bottom, color);
	}
};


class CD3D9Stripe : public CD3D9Quadrilateral
{
private:
	using CD3D9Quadrilateral::Set;

public:
	HRESULT Set(const int x1, const int y1, const int x2, const int y2, const int thickness, const D3DCOLOR color)
	{
		const float a = x2 - x1;
		const float b = y1 - y2;
		const float c = sqrtf(a*a + b*b);
		const float xt = thickness * b / c;
		const float yt = thickness * a / c;

		const float x3 = x2 + xt;
		const float y3 = y2 + yt;
		const float x4 = x1 + xt;
		const float y4 = y1 + yt;

		return CD3D9Quadrilateral::Set(x1, y1, x2, y2, x3, y3, x4, y4, color);
	}
};

class CD3D9Dots
{
protected:
	IDirect3DDevice9* m_pD3DDev = nullptr;

	bool m_bAlphaBlend = false;
	std::vector<POINTVERTEX> m_Vertices;
	IDirect3DVertexBuffer9* m_pVertexBuffer = nullptr;

	virtual inline bool CheckBadPoints(const std::vector<POINT>& poins)
	{
		return (poins.size() == 0);
	}

	virtual inline HRESULT DrawPrimitive()
	{
		return m_pD3DDev->DrawPrimitive(D3DPT_POINTLIST, 0, m_Vertices.size());
	}

public:
	~CD3D9Dots()
	{
		InvalidateDeviceObjects();
	}

	HRESULT InitDeviceObjects(IDirect3DDevice9* pD3DDev)
	{
		InvalidateDeviceObjects();
		if (!pD3DDev) {
			return E_POINTER;
		}

		m_pD3DDev = pD3DDev;

		return S_OK;
	}

	void InvalidateDeviceObjects()
	{
		m_pD3DDev = nullptr;
		SAFE_RELEASE(m_pVertexBuffer);
	}

	HRESULT Set(const std::vector<POINT>& poins, const D3DCOLOR color)
	{
		if (CheckBadPoints(poins)) {
			return E_INVALIDARG;
		}
		HRESULT hr = S_OK;

		if (m_Vertices.size() != poins.size()) {
			SAFE_RELEASE(m_pVertexBuffer);
		}

		m_bAlphaBlend = (color >> 24) < 0xFF;

		m_Vertices.clear();
		m_Vertices.reserve(poins.size());
		for (const auto& point : poins) {
			m_Vertices.emplace_back(POINTVERTEX{ (FLOAT)point.x, (FLOAT)point.y, 0.5f, 1.0f, color });
		}

		UINT vertexSize = m_Vertices.size() * sizeof(POINTVERTEX);

		if (!m_pVertexBuffer) {
			hr = m_pD3DDev->CreateVertexBuffer(vertexSize, 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_DEFAULT, &m_pVertexBuffer, nullptr);
		}

		if (m_pVertexBuffer) {
			VOID* pVertices;
			hr = m_pVertexBuffer->Lock(0, vertexSize, (void**)&pVertices, 0);
			if (S_OK == hr) {
				memcpy(pVertices, m_Vertices.data(), vertexSize);
				m_pVertexBuffer->Unlock();
			};
		}

		return hr;
	}

	HRESULT Draw()
	{
		if (m_bAlphaBlend) {
			m_pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			m_pD3DDev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			m_pD3DDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		}
		else {
			m_pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		}
		m_pD3DDev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);

		HRESULT hr = m_pD3DDev->SetStreamSource(0, m_pVertexBuffer, 0, sizeof(POINTVERTEX));
		if (S_OK == hr) {
			hr = m_pD3DDev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
			hr = DrawPrimitive();
		}

		return hr;
	}
};

class CD3D9Lines : public CD3D9Dots
{
protected:
	inline bool CheckBadPoints(const std::vector<POINT>& poins) override
	{
		return (poins.size() < 2 && poins.size() & 1);
	}

	inline HRESULT DrawPrimitive() override
	{
		return m_pD3DDev->DrawPrimitive(D3DPT_LINELIST, 0, m_Vertices.size());
	}
};

class CD3D9Polyline : public CD3D9Dots
{
protected:
	inline bool CheckBadPoints(const std::vector<POINT>& poins) override
	{
		return (poins.size() < 2);
	}

	inline HRESULT DrawPrimitive() override
	{
		return m_pD3DDev->DrawPrimitive(D3DPT_LINESTRIP, 0, m_Vertices.size());
	}
};
