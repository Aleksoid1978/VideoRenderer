/*
* (C) 2018 see Authors.txt
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

#include <atltypes.h>
#include "d3d11.h"

class CD3D11VideoProcessor
{
private:
	HMODULE m_hD3D11Lib = nullptr;
	CComPtr<ID3D11Device> m_pD3D11Device;
	CComPtr<ID3D11VideoDevice> m_pD3D11VideoDevice;

public:
	CD3D11VideoProcessor();
	~CD3D11VideoProcessor();

	HRESULT IsMediaTypeSupported(const GUID subtype, const UINT width, const UINT height);
	HRESULT Initialize(UINT width, UINT height);
};

