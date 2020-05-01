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

#include "stdafx.h"

#include <d3d11.h>
#include "Helper.h"
#include "DX11Helper.h"
#include "resource.h"

#include "D3D11Geometry.h"


//
// CD3D11Quadrilateral
//

CD3D11Quadrilateral::~CD3D11Quadrilateral()
{
	InvalidateDeviceObjects();
}

HRESULT CD3D11Quadrilateral::InitDeviceObjects(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext)
{
	InvalidateDeviceObjects();
	if (!pDevice || !pDeviceContext) {
		return E_POINTER;
	}

	m_pDevice = pDevice;
	m_pDevice->AddRef();
	m_pDeviceContext = pDeviceContext;
	m_pDeviceContext->AddRef();

	D3D11_BUFFER_DESC BufferDesc = { sizeof(m_Vertices), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
	D3D11_SUBRESOURCE_DATA InitData = { m_Vertices, 0, 0 };

	HRESULT hr = pDevice->CreateBuffer(&BufferDesc, &InitData, &m_pVertexBuffer);

	LPVOID data;
	DWORD size;
	EXECUTE_ASSERT(S_OK == GetDataFromResource(data, size, IDF_VSH11_GEOMETRY));
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateVertexShader(data, size, nullptr, &m_pVertexShader));

	static const D3D11_INPUT_ELEMENT_DESC vertexLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateInputLayout(vertexLayout, std::size(vertexLayout), data, size, &m_pInputLayout));

	EXECUTE_ASSERT(S_OK == GetDataFromResource(data, size, IDF_PSH11_GEOMETRY));
	EXECUTE_ASSERT(S_OK == m_pDevice->CreatePixelShader(data, size, nullptr, &m_pPixelShader));

	D3D11_BLEND_DESC bdesc = {};
	bdesc.RenderTarget[0].BlendEnable = TRUE;
	bdesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	bdesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	bdesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	bdesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	bdesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	bdesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	bdesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	hr = m_pDevice->CreateBlendState(&bdesc, &m_pBlendState);

	return hr;
}

void CD3D11Quadrilateral::InvalidateDeviceObjects()
{
	SAFE_RELEASE(m_pVertexShader);
	SAFE_RELEASE(m_pPixelShader);
	SAFE_RELEASE(m_pBlendState);
	SAFE_RELEASE(m_pInputLayout);
	SAFE_RELEASE(m_pVertexBuffer);
	SAFE_RELEASE(m_pDeviceContext);
	SAFE_RELEASE(m_pDevice);
}

HRESULT CD3D11Quadrilateral::Set(const float x1, const float y1, const float x2, const float y2, const float x3, const float y3, const float x4, const float y4, const D3DCOLOR color)
{
	HRESULT hr = S_OK;

	m_bAlphaBlend = (color >> 24) < 0xFF;

	DirectX::XMFLOAT4 colorRGBAf = D3DCOLORtoXMFLOAT4(color);

	m_Vertices[0] = { {x4, y4, 0.5f}, colorRGBAf };
	m_Vertices[1] = { {x1, y1, 0.5f}, colorRGBAf };
	m_Vertices[2] = { {x3, y3, 0.5f}, colorRGBAf };
	m_Vertices[3] = { {x2, y2, 0.5f}, colorRGBAf };

	if (m_pVertexBuffer) {
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		hr = m_pDeviceContext->Map(m_pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (S_OK == hr) {
			memcpy(mappedResource.pData, m_Vertices, sizeof(m_Vertices));
			m_pDeviceContext->Unmap(m_pVertexBuffer, 0);
		};
	}

	return hr;
}

HRESULT CD3D11Quadrilateral::Draw(ID3D11RenderTargetView* pRenderTargetView, const SIZE& rtSize)
{
	HRESULT hr = S_OK;
	UINT Stride = sizeof(POINTVERTEX11);
	UINT Offset = 0;
	ID3D11ShaderResourceView* views[1] = {};

	m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);

	D3D11_VIEWPORT VP;
	VP.TopLeftX = 0;
	VP.TopLeftY = 0;
	VP.Width    = rtSize.cx;
	VP.Height   = rtSize.cy;
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;
	m_pDeviceContext->RSSetViewports(1, &VP);

	m_pDeviceContext->PSSetShaderResources(0, 1, views);
	m_pDeviceContext->IASetInputLayout(m_pInputLayout);

	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer, &Stride, &Offset);

	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	m_pDeviceContext->VSSetShader(m_pVertexShader, nullptr, 0);
	m_pDeviceContext->PSSetShader(m_pPixelShader, nullptr, 0);

	m_pDeviceContext->OMSetBlendState(m_bAlphaBlend ? m_pBlendState : nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);

	m_pDeviceContext->Draw(std::size(m_Vertices), 0);

	return hr;
}

//
// CD3D11Rectangle
//

HRESULT CD3D11Rectangle::Set(const RECT& rect, const SIZE& rtSize, const D3DCOLOR color)
{
	const float left   = (float)(rect.left*2)    / rtSize.cx - 1;
	const float top    = (float)(-rect.top*2)    / rtSize.cy + 1;
	const float right  = (float)(rect.right*2)   / rtSize.cx - 1;
	const float bottom = (float)(-rect.bottom*2) / rtSize.cy + 1;

	return CD3D11Quadrilateral::Set(left, top, right, top, right, bottom, left, bottom, color);
}

//
// CD3D11Stripe
//

HRESULT CD3D11Stripe::Set(const int x1, const int y1, const int x2, const int y2, const int thickness, const D3DCOLOR color)
{
	const float a = x2 - x1;
	const float b = y1 - y2;
	const float c = sqrtf(a*a + b * b);
	const float xt = thickness * b / c;
	const float yt = thickness * a / c;

	const float x3 = x2 + xt;
	const float y3 = y2 + yt;
	const float x4 = x1 + xt;
	const float y4 = y1 + yt;

	return CD3D11Quadrilateral::Set(x1, y1, x2, y2, x3, y3, x4, y4, color);
}
