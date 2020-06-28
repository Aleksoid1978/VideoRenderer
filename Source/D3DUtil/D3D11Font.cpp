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
#include "Helper.h"
#include "DX11Helper.h"
#include "resource.h"
#include "FontBitmap.h"
#include "D3D11Font.h"

#define MAX_NUM_VERTICES 400*6

struct Font11Vertex {
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT2 Tex;
};

struct PixelBufferType
{
	DirectX::XMFLOAT4 pixelColor;
};

inline auto Char2Index(WCHAR ch)
{
	if (ch >= 0x0020 && ch <= 0x007F) {
		return ch - 32;
	}
	if (ch >= 0x00A0 && ch <= 0x00BF) {
		return ch - 64;
	}
	return 0x007F - 32;
}

CD3D11Font::CD3D11Font()
{
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
}

HRESULT CD3D11Font::InitDeviceObjects(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext)
{
	InvalidateDeviceObjects();
	if (!pDevice || !pDeviceContext) {
		return E_POINTER;
	}

	// Keep a local copy of the device
	m_pDevice = pDevice;
	m_pDevice->AddRef();
	m_pDeviceContext = pDeviceContext;
	m_pDeviceContext->AddRef();

	LPVOID data;
	DWORD size;
	EXECUTE_ASSERT(S_OK == GetDataFromResource(data, size, IDF_VSH11_SIMPLE));
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateVertexShader(data, size, nullptr, &m_pVertexShader));

	static const D3D11_INPUT_ELEMENT_DESC vertexLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateInputLayout(vertexLayout, std::size(vertexLayout), data, size, &m_pInputLayout));

	EXECUTE_ASSERT(S_OK == GetDataFromResource(data, size, IDF_PSH11_FONT));
	EXECUTE_ASSERT(S_OK == m_pDevice->CreatePixelShader(data, size, nullptr, &m_pPixelShader));

	// create the vertex array
	std::vector<Font11Vertex> vertices;
	vertices.resize(MAX_NUM_VERTICES);

	D3D11_BUFFER_DESC vertexBufferDesc;
	vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexBufferDesc.ByteWidth = sizeof(Font11Vertex) * MAX_NUM_VERTICES;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;
	D3D11_SUBRESOURCE_DATA vertexData = { vertices.data(), 0, 0 };

	HRESULT hr = m_pDevice->CreateBuffer(&vertexBufferDesc, &vertexData, &m_pVertexBuffer);
	if (FAILED(hr)) {
		return hr;
	}

	/*
	// create the index array
	std::vector<uint32_t> indices;
	indices.reserve(MAX_NUM_VERTICES);
	for (uint32_t i = 0; i < indices.size(); i++) {
		indices.emplace_back(i);
	}

	D3D11_BUFFER_DESC indexBufferDesc;
	indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	indexBufferDesc.ByteWidth = sizeof(uint32_t) * MAX_NUM_VERTICES;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	indexBufferDesc.CPUAccessFlags = 0;
	indexBufferDesc.MiscFlags = 0;
	indexBufferDesc.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA indexData = { indices.data(), 0, 0 };

	hr = m_pDevice->CreateBuffer(&indexBufferDesc, &indexData, &m_pIndexBuffer);
	if (FAILED(hr)) {
		return hr;
	}
	*/

	// Setup the description of the dynamic pixel constant buffer that is in the pixel shader.
	D3D11_BUFFER_DESC pixelBufferDesc;
	pixelBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	pixelBufferDesc.ByteWidth = sizeof(PixelBufferType);
	pixelBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	pixelBufferDesc.CPUAccessFlags = 0;
	pixelBufferDesc.MiscFlags = 0;
	pixelBufferDesc.StructureByteStride = 0;

	// Create the pixel constant buffer pointer so we can access the pixel shader constant buffer from within this class.
	hr = m_pDevice->CreateBuffer(&pixelBufferDesc, nullptr, &m_pPixelBuffer);
	if (FAILED(hr)) {
		return hr;
	}

	DirectX::XMFLOAT4 colorRGBAf = D3DCOLORtoXMFLOAT4(m_Color);
	m_pDeviceContext->UpdateSubresource(m_pPixelBuffer, 0, nullptr, &colorRGBAf, 0, 0);

	D3D11_SAMPLER_DESC SampDesc = {};
	SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SampDesc.MinLOD = 0;
	SampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateSamplerState(&SampDesc, &m_pSamplerState));

	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	hr = m_pDevice->CreateBlendState(&blendDesc, &m_pBlendState);

	return hr;
}

void CD3D11Font::InvalidateDeviceObjects()
{
	SAFE_RELEASE(m_pShaderResource);
	SAFE_RELEASE(m_pTexture);

	SAFE_RELEASE(m_pVertexShader);
	SAFE_RELEASE(m_pPixelShader);
	SAFE_RELEASE(m_pSamplerState);
	SAFE_RELEASE(m_pBlendState);
	SAFE_RELEASE(m_pInputLayout);
	SAFE_RELEASE(m_pPixelBuffer);
	SAFE_RELEASE(m_pVertexBuffer);
	//SAFE_RELEASE(m_pIndexBuffer);

	SAFE_RELEASE(m_pDeviceContext);
	SAFE_RELEASE(m_pDevice);
}

HRESULT CD3D11Font::CreateFontBitmap(const WCHAR* strFontName, const DWORD dwHeight, const DWORD dwFlags)
{
	if (!m_pDevice) {
		return E_ABORT;
	}

	if (m_pTexture && dwHeight == m_dwFontHeight && dwFlags == m_dwFontFlags && m_strFontName.compare(strFontName) == 0) {
		return S_FALSE;
	}

	m_strFontName  = strFontName;
	m_dwFontHeight = dwHeight;
	m_dwFontFlags  = dwFlags;

	CFontBitmap fontBitmap;

	HRESULT hr = fontBitmap.Initialize(m_strFontName.c_str(), m_dwFontHeight, 0, m_Characters, std::size(m_Characters));
	if (FAILED(hr)) {
		return hr;
	}

	m_MaxCharMetric = fontBitmap.GetMaxCharMetric();
	m_uTexWidth     = fontBitmap.GetWidth();
	m_uTexHeight    = fontBitmap.GetHeight();
	EXECUTE_ASSERT(S_OK == fontBitmap.GetFloatCoords((FloatRect*)&m_fTexCoords, std::size(m_Characters)));

	SAFE_RELEASE(m_pShaderResource);
	SAFE_RELEASE(m_pTexture);

	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.Width = m_uTexWidth;
	texDesc.Height = m_uTexHeight;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_IMMUTABLE;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	BYTE* pData = nullptr;
	UINT uPitch = 0;
	hr = fontBitmap.Lock(&pData, uPitch);
	if (FAILED(hr)) {
		return hr;
	}

	D3D11_SUBRESOURCE_DATA subresData = { pData, uPitch, 0 };
	hr = m_pDevice->CreateTexture2D(&texDesc, &subresData, &m_pTexture);
	fontBitmap.Unlock();
	if (FAILED(hr)) {
		return hr;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	EXECUTE_ASSERT(S_OK == m_pDevice->CreateShaderResourceView(m_pTexture, &srvDesc, &m_pShaderResource));

	return hr;
}

SIZE CD3D11Font::GetMaxCharMetric()
{
	return m_MaxCharMetric;
}

HRESULT CD3D11Font::GetTextExtent(const WCHAR* strText, SIZE* pSize)
{
	if (nullptr == strText || nullptr == pSize) {
		return E_POINTER;
	}

	float fRowWidth  = 0.0f;
	float fRowHeight = (m_fTexCoords[0].bottom - m_fTexCoords[0].top)*m_uTexHeight;
	float fWidth     = 0.0f;
	float fHeight    = fRowHeight;

	while (*strText) {
		WCHAR c = *strText++;

		if (c == '\n') {
			fRowWidth = 0.0f;
			fHeight  += fRowHeight;
			continue;
		}

		const auto idx = Char2Index(c);

		const float tx1 = m_fTexCoords[idx].left;
		const float tx2 = m_fTexCoords[idx].right;

		fRowWidth += (tx2 - tx1)*m_uTexWidth;

		if (fRowWidth > fWidth) {
			fWidth = fRowWidth;
		}
	}

	pSize->cx = (LONG)fWidth;
	pSize->cy = (LONG)fHeight;

	return S_OK;
}

HRESULT CD3D11Font::Draw2DText(ID3D11RenderTargetView* pRenderTargetView, const SIZE& rtSize, float sx, float sy, D3DCOLOR color, const WCHAR* strText)
{
	if (!m_pDevice || !m_pDeviceContext) {
		return E_ABORT;
	}
	ASSERT(pRenderTargetView);

	HRESULT hr = S_OK;
	UINT Stride = sizeof(Font11Vertex);
	UINT Offset = 0;

	if (color != m_Color) {
		m_Color = color;
		DirectX::XMFLOAT4 colorRGBAf = D3DCOLORtoXMFLOAT4(m_Color);
		m_pDeviceContext->UpdateSubresource(m_pPixelBuffer, 0, nullptr, &colorRGBAf, 0, 0);
	}

	D3D11_VIEWPORT VP;
	VP.TopLeftX = 0;
	VP.TopLeftY = 0;
	VP.Width    = rtSize.cx;
	VP.Height   = rtSize.cy;
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;
	m_pDeviceContext->RSSetViewports(1, &VP);

	m_pDeviceContext->PSSetShaderResources(0, 1, &m_pShaderResource);
	m_pDeviceContext->IASetInputLayout(m_pInputLayout);
	m_pDeviceContext->VSSetShader(m_pVertexShader, nullptr, 0);
	m_pDeviceContext->PSSetShader(m_pPixelShader, nullptr, 0);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer, &Stride, &Offset);
	m_pDeviceContext->PSSetConstantBuffers(0, 1, &m_pPixelBuffer);
	m_pDeviceContext->PSSetSamplers(0, 1, &m_pSamplerState);
	m_pDeviceContext->OMSetBlendState(m_pBlendState, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);

	// Adjust for character spacing
	const float fStartX = (float)(sx * 2) / rtSize.cx - 1;
	const float fLineHeight = (m_fTexCoords[0].bottom - m_fTexCoords[0].top) * m_uTexHeight * 2 / rtSize.cy;
	float drawX = fStartX;
	float drawY = (float)(-sy * 2) / rtSize.cy + 1;

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	hr = m_pDeviceContext->Map(m_pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (S_OK == hr) {
		Font11Vertex* pVertices = static_cast<Font11Vertex*>(mappedResource.pData);
		UINT nVertices = 0;

		while (*strText) {
			const WCHAR c = *strText++;

			if (c == '\n') {
				drawX = fStartX;
				drawY -= fLineHeight;
				continue;
			}

			const auto tex = m_fTexCoords[Char2Index(c)];

			const float Width = (tex.right - tex.left) * m_uTexWidth * 2 / rtSize.cx;
			const float Height = (tex.bottom - tex.top) * m_uTexHeight * 2 / rtSize.cy;

			if (c != 0x0020 && c != 0x00A0) { // Space and No-Break Space
				const float left = drawX;
				const float right = drawX + Width;
				const float top = drawY;
				const float bottom = drawY - Height;

				*pVertices++ = { {left,  top,    0.0f}, {tex.left,  tex.top}    };
				*pVertices++ = { {right, bottom, 0.0f}, {tex.right, tex.bottom} };
				*pVertices++ = { {left,  bottom, 0.0f}, {tex.left,  tex.bottom} };
				*pVertices++ = { {left,  top,    0.0f}, {tex.left,  tex.top}    };
				*pVertices++ = { {right, top,    0.0f}, {tex.right, tex.top}    };
				*pVertices++ = { {right, bottom, 0.0f}, {tex.right, tex.bottom} };
				nVertices += 6;

				if (nVertices > (MAX_NUM_VERTICES - 6)) {
					// Unlock, render, and relock the vertex buffer
					m_pDeviceContext->Unmap(m_pVertexBuffer, 0);
					m_pDeviceContext->Draw(nVertices, 0);

					hr = m_pDeviceContext->Map(m_pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
					pVertices = static_cast<Font11Vertex*>(mappedResource.pData);
					nVertices = 0;
				}
			}

			drawX += Width;
		}
		m_pDeviceContext->Unmap(m_pVertexBuffer, 0);
		if (nVertices > 0) {
			m_pDeviceContext->Draw(nVertices, 0);
		}
	}

	return hr;
}
