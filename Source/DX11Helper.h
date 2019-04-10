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

#include <atlcomcli.h>
#include <d3d11.h>

enum Tex2DType {
	Tex2D_Default,
	Tex2D_DefaultRTarget,
	Tex2D_DefaultShaderRTarget,
	Tex2D_DefaultShaderRTargetGDI,
	Tex2D_DynamicShaderWrite,
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
	HRESULT Create(ID3D11Device* pDevice, const DXGI_FORMAT format, const UINT width, const UINT height, const Tex2DType type) {
		Release();

		HRESULT hr = CreateTex2D(pDevice, format, width, height, type, &pTexture);
		if (S_OK == hr) {
			pTexture->GetDesc(&desc);
		}

		return hr;
	}
	void Release() {
		pTexture.Release();
		desc = {};
	}
};

struct Tex2DShader_t : Tex2D_t
{
	ID3D11ShaderResourceView* pShaderResource;
	// TODO
};
