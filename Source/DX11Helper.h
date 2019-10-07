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

enum Tex2DType {
	Tex2D_Default,
	Tex2D_DefaultRTarget,
	Tex2D_DefaultShaderRTarget,
	Tex2D_DefaultShaderRTargetGDI,
	Tex2D_DynamicShaderWrite,
	Tex2D_DynamicShaderWriteNoSRV,
	Tex2D_StagingRead,
};

inline HRESULT CreateTex2D(ID3D11Device* pDevice, const DXGI_FORMAT format, const UINT width, const UINT height, const Tex2DType type, ID3D11Texture2D** ppTexture2D)
{
	D3D11_TEXTURE2D_DESC desc;
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc = { 1, 0 };

	switch (type) {
	default:
	case Tex2D_Default:
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		break;
	case Tex2D_DefaultRTarget:
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		break;
	case Tex2D_DefaultShaderRTarget:
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		break;
	case Tex2D_DefaultShaderRTargetGDI:
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
		break;
	case Tex2D_DynamicShaderWrite:
	case Tex2D_DynamicShaderWriteNoSRV:
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		break;
	case Tex2D_StagingRead:
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;
		break;
	}

	return pDevice->CreateTexture2D(&desc, nullptr, ppTexture2D);
}

struct Tex2D_t
{
	CComPtr<ID3D11Texture2D> pTexture;
	D3D11_TEXTURE2D_DESC desc = {};
	CComPtr<ID3D11ShaderResourceView> pShaderResource;

	HRESULT Create(ID3D11Device* pDevice, const DXGI_FORMAT format, const UINT width, const UINT height, const Tex2DType type) {
		Release();

		HRESULT hr = CreateTex2D(pDevice, format, width, height, type, &pTexture);
		if (S_OK == hr) {
			pTexture->GetDesc(&desc);

			if (type != Tex2D_DynamicShaderWriteNoSRV && (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
				D3D11_SHADER_RESOURCE_VIEW_DESC shaderDesc;
				shaderDesc.Format = format;
				shaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				shaderDesc.Texture2D.MostDetailedMip = 0; // = Texture2D desc.MipLevels - 1
				shaderDesc.Texture2D.MipLevels = 1;       // = Texture2D desc.MipLevels

				hr = pDevice->CreateShaderResourceView(pTexture, &shaderDesc, &pShaderResource);
				if (FAILED(hr)) {
					Release();
				}
			}
		}

		return hr;
	}

	virtual void Release() {
		pShaderResource.Release();
		pTexture.Release();
		desc = {};
	}
};

struct Tex11Video_t : Tex2D_t
{
	CComPtr<ID3D11Texture2D> pTexture2;
	CComPtr<ID3D11Texture2D> pTexture3;
	CComPtr<ID3D11ShaderResourceView> pShaderResource2;
	CComPtr<ID3D11ShaderResourceView> pShaderResource3;

	HRESULT CreateEx(ID3D11Device* pDevice, const DXGI_FORMAT format, const DX11PlanarPrms_t* pPlanes, const UINT width, const UINT height, const Tex2DType type) {
		Release();

		HRESULT hr = CreateTex2D(pDevice, format, width, height, type, &pTexture);
		if (S_OK == hr) {
			pTexture->GetDesc(&desc);

			if (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
				D3D11_SHADER_RESOURCE_VIEW_DESC shaderDesc;
				shaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				shaderDesc.Texture2D.MostDetailedMip = 0;
				shaderDesc.Texture2D.MipLevels = 1;

				if (pPlanes) {
					// 1 texture, 2 SRV
					shaderDesc.Format = pPlanes->FmtPlane1;
					hr = pDevice->CreateShaderResourceView(pTexture, &shaderDesc, &pShaderResource);
					if (S_OK == hr) {
						shaderDesc.Format = pPlanes->FmtPlane2;
						hr = pDevice->CreateShaderResourceView(pTexture, &shaderDesc, &pShaderResource2);
					}
				} else {
					// 1 texture, 1 SRV
					shaderDesc.Format = format;
					hr = pDevice->CreateShaderResourceView(pTexture, &shaderDesc, &pShaderResource);
				}
			}
		}
		else if (pPlanes) {
			// 2 textures, 2 SRV
			hr = Create(pDevice, pPlanes->FmtPlane1, width, height, type);
			if (S_OK == hr) {
				const UINT chromaWidth = width / pPlanes->div_chroma_w;
				const UINT chromaHeight = height / pPlanes->div_chroma_h;
				hr = CreateTex2D(pDevice, pPlanes->FmtPlane2, chromaWidth, chromaHeight, type, &pTexture2);
				if (S_OK == hr) {
					D3D11_SHADER_RESOURCE_VIEW_DESC shaderDesc;
					shaderDesc.Format = pPlanes->FmtPlane2;
					shaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
					shaderDesc.Texture2D.MostDetailedMip = 0;
					shaderDesc.Texture2D.MipLevels = 1;
					hr = pDevice->CreateShaderResourceView(pTexture2, &shaderDesc, &pShaderResource2);

					if (S_OK == hr && pPlanes->FmtPlane3) {
						// 3 textures, 3 SRV
						hr = CreateTex2D(pDevice, pPlanes->FmtPlane3, chromaWidth, chromaHeight, type, &pTexture3);
						if (S_OK == hr) {
							shaderDesc.Format = pPlanes->FmtPlane3;
							hr = pDevice->CreateShaderResourceView(pTexture3, &shaderDesc, &pShaderResource3);
						}
					}
				}
			}
		}

		if (FAILED(hr)) {
			Release();
		}

		return hr;
	}

	void Release() override {
		pShaderResource3.Release();
		pTexture3.Release();
		pShaderResource2.Release();
		pTexture2.Release();
		Tex2D_t::Release();
	}
};

UINT GetAdapter(HWND hWnd, IDXGIFactory1* pDXGIFactory, IDXGIAdapter** ppDXGIAdapter);

HRESULT Dump4ByteTexture2D(ID3D11DeviceContext* pDeviceContext, ID3D11Texture2D* pTexture2D, const wchar_t* filename);
