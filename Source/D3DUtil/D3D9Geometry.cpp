/*
 * (C) 2020 see Authors.txt
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

#include "stdafx.h"

#include <d3d9.h>
#include "Helper.h"

#include "D3D9Geometry.h"


//
// CD3D9Quadrilateral
//

CD3D9Quadrilateral::~CD3D9Quadrilateral()
{
	InvalidateDeviceObjects();
}

HRESULT CD3D9Quadrilateral::InitDeviceObjects(IDirect3DDevice9* pDevice)
{
	InvalidateDeviceObjects();
	if (!pDevice) {
		return E_POINTER;
	}
	m_pDevice = pDevice;
	m_pDevice->AddRef();

	HRESULT hr = m_pDevice->CreateVertexBuffer(6 * sizeof(POINTVERTEX9), 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_DEFAULT, &m_pVertexBuffer, nullptr);

	return hr;
}

void CD3D9Quadrilateral::InvalidateDeviceObjects()
{
	SAFE_RELEASE(m_pVertexBuffer);
	SAFE_RELEASE(m_pDevice);
}

HRESULT CD3D9Quadrilateral::Set(const float x1, const float y1, const float x2, const float y2, const float x3, const float y3, const float x4, const float y4, const D3DCOLOR color)
{
	HRESULT hr = S_OK;

	m_bAlphaBlend = (color >> 24) < 0xFF;

	m_Vertices[0] = { {x1, y1, 0.5f, 1.0f}, color };
	m_Vertices[1] = { {x2, y2, 0.5f, 1.0f}, color };
	m_Vertices[2] = { {x3, y3, 0.5f, 1.0f}, color };
	m_Vertices[3] = { {x1, y1, 0.5f, 1.0f}, color };
	m_Vertices[4] = { {x3, y3, 0.5f, 1.0f}, color };
	m_Vertices[5] = { {x4, y4, 0.5f, 1.0f}, color };

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

HRESULT CD3D9Quadrilateral::Draw()
{
	if (m_bAlphaBlend) {
		m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	}
	else {
		m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	}
	m_pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);

	HRESULT hr = m_pDevice->SetStreamSource(0, m_pVertexBuffer, 0, sizeof(POINTVERTEX9));
	if (S_OK == hr) {
		hr = m_pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
		hr = m_pDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
	}

	return hr;
}

//
// CD3D9Rectangle
//

HRESULT CD3D9Rectangle::Set(const RECT& rect, const D3DCOLOR color)
{
	return CD3D9Quadrilateral::Set(rect.left, rect.top, rect.right, rect.top, rect.right, rect.bottom, rect.left, rect.bottom, color);
}

//
// CD3D9Stripe
//

HRESULT CD3D9Stripe::Set(const int x1, const int y1, const int x2, const int y2, const int thickness, const D3DCOLOR color)
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

//
// CD3D9Dots
//

CD3D9Dots::~CD3D9Dots()
{
	InvalidateDeviceObjects();
}

HRESULT CD3D9Dots::InitDeviceObjects(IDirect3DDevice9* pDevice)
{
	InvalidateDeviceObjects();
	if (!pDevice) {
		return E_POINTER;
	}

	m_pDevice = pDevice;
	m_pDevice->AddRef();

	return S_OK;
}

void CD3D9Dots::InvalidateDeviceObjects()
{
	SAFE_RELEASE(m_pVertexBuffer);
	SAFE_RELEASE(m_pDevice);
}

void CD3D9Dots::ClearPoints()
{
	m_Vertices.clear();
	m_bAlphaBlend = false;
}

bool CD3D9Dots::AddPoints(POINT* poins, const UINT size, const D3DCOLOR color)
{
	if (!CheckNumPoints(size)) {
		return false;
	}

	m_bAlphaBlend = (color >> 24) < 0xFF;

	auto pos = m_Vertices.size();
	m_Vertices.resize(pos + size);

	while (pos < m_Vertices.size()) {
		m_Vertices[pos++] = { {(float)(*poins).x, (float)(*poins).y, 0.f, 1.f}, color };
		poins++;
	}

	return true;
}

bool CD3D9Dots::AddGFPoints(
	int Xstart, int Xstep,
	int Yaxis, int Yscale,
	int* Ydata, UINT Yoffset,
	const UINT size, const D3DCOLOR color)
{
	if (!CheckNumPoints(size)) {
		return false;
	}

	m_bAlphaBlend = (color >> 24) < 0xFF;

	auto pos = m_Vertices.size();
	m_Vertices.resize(pos + size);

	while (pos < m_Vertices.size()) {
		const float y = (float)(Yaxis - Ydata[Yoffset++] * Yscale);
		m_Vertices[pos++] = { {(float)Xstart, y, 0.f, 1.f}, color };
		Xstart += Xstep;
		if (Yoffset == size) {
			Yoffset = 0;
		}
	}

	return true;
}

HRESULT CD3D9Dots::UpdateVertexBuffer()
{
	HRESULT hr = S_FALSE;
	UINT vertexSize = m_Vertices.size() * sizeof(POINTVERTEX9);

	if (m_pVertexBuffer) {
		D3DVERTEXBUFFER_DESC desc;
		hr = m_pVertexBuffer->GetDesc(&desc);
		if (FAILED(hr) || desc.Size < vertexSize) {
			SAFE_RELEASE(m_pVertexBuffer);
		}
	}

	if (!m_pVertexBuffer) {
		hr = m_pDevice->CreateVertexBuffer(vertexSize, 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_DEFAULT, &m_pVertexBuffer, nullptr);
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

HRESULT CD3D9Dots::Draw()
{
	if (m_bAlphaBlend) {
		m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	}
	else {
		m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	}
	m_pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);

	HRESULT hr = m_pDevice->SetStreamSource(0, m_pVertexBuffer, 0, sizeof(POINTVERTEX9));
	if (S_OK == hr) {
		hr = m_pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
		hr = DrawPrimitive();
	}

	return hr;
}
