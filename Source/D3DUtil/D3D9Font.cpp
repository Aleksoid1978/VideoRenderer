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

#include "stdafx.h"
#include <d3d9.h>
#include <DirectXMath.h>
#include "Helper.h"
#include "FontBitmap.h"
#include "D3D9Font.h"

//-----------------------------------------------------------------------------
// Custom vertex types for rendering text
//-----------------------------------------------------------------------------
#define MAX_NUM_VERTICES 400*6

struct FONT2DVERTEX {
	DirectX::XMFLOAT4 p;
	DWORD color;
	FLOAT tu, tv;
};

#define D3DFVF_FONT2DVERTEX (D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1)

inline FONT2DVERTEX InitFont2DVertex( const DirectX::XMFLOAT4& p, D3DCOLOR color,
									  FLOAT tu, FLOAT tv )
{
	FONT2DVERTEX v;
	v.p = p;
	v.color = color;
	v.tu = tu;
	v.tv = tv;
	return v;
}

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

//-----------------------------------------------------------------------------
// Name: CD3D9Font()
// Desc: Font class constructor
//-----------------------------------------------------------------------------
CD3D9Font::CD3D9Font( const WCHAR* strFontName, DWORD dwHeight, DWORD dwFlags )
	: m_dwFontHeight(dwHeight)
	, m_dwFontFlags(dwFlags)
{
	wcsncpy_s( m_strFontName, strFontName, std::size(m_strFontName) );
	m_strFontName[std::size(m_strFontName) - 1] = '\0';

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



//-----------------------------------------------------------------------------
// Name: ~CD3D9Font()
// Desc: Font class destructor
//-----------------------------------------------------------------------------
CD3D9Font::~CD3D9Font()
{
	InvalidateDeviceObjects();
	DeleteDeviceObjects();
}



//-----------------------------------------------------------------------------
// Name: InitDeviceObjects()
// Desc: Initializes device-dependent objects, including the vertex buffer used
//       for rendering text and the texture map which stores the font image.
//-----------------------------------------------------------------------------
HRESULT CD3D9Font::InitDeviceObjects( IDirect3DDevice9* pd3dDevice )
{
	HRESULT hr = S_OK;

	// Keep a local copy of the device
	m_pd3dDevice = pd3dDevice;
	D3DCAPS9 d3dCaps;
	m_pd3dDevice->GetDeviceCaps(&d3dCaps);

	// Assume we will draw fonts into texture without scaling unless the
	// required texture size is found to be larger than the device max
	m_fTextScale  = 1.0f;

#if FONTBITMAP_MODE == 0
	CFontBitmapGDI fontBitmap;
#elif FONTBITMAP_MODE == 1
	CFontBitmapGDIPlus fontBitmap;
#elif FONTBITMAP_MODE == 2
	CFontBitmapDWrite fontBitmap;
#endif

	hr = fontBitmap.Initialize(m_strFontName, m_dwFontHeight, m_dwFontFlags, m_Characters, std::size(m_Characters));
	if (FAILED(hr)) {
		return hr;
	}

	m_uTexWidth  = fontBitmap.GetWidth();
	m_uTexHeight = fontBitmap.GetHeight();
	if (m_uTexWidth > d3dCaps.MaxTextureWidth || m_uTexHeight > d3dCaps.MaxTextureHeight) {
		return E_FAIL;
	}

	hr = fontBitmap.GetFloatCoords((float*)&m_fTexCoords, std::size(m_Characters));
	if (FAILED(hr)) {
		return hr;
	}

	// Create a new texture for the font
	hr = m_pd3dDevice->CreateTexture( m_uTexWidth, m_uTexHeight, 1,
									  D3DUSAGE_DYNAMIC, D3DFMT_A8L8,
									  D3DPOOL_DEFAULT, &m_pTexture, nullptr );
	if (FAILED(hr)) {
		return hr;
	}

	// Lock the surface and write the alpha values for the set pixels
	D3DLOCKED_RECT d3dlr;
	hr = m_pTexture->LockRect(0, &d3dlr, nullptr, D3DLOCK_DISCARD);
	if (S_OK == hr) {
		hr = fontBitmap.CopyBitmapToA8L8((BYTE*)d3dlr.pBits, d3dlr.Pitch);
		m_pTexture->UnlockRect(0);
	}

	return hr;
}



//-----------------------------------------------------------------------------
// Name: RestoreDeviceObjects()
// Desc:
//-----------------------------------------------------------------------------
HRESULT CD3D9Font::RestoreDeviceObjects()
{
	// Create vertex buffer for the letters
	const UINT vertexBufferSize = sizeof(FONT2DVERTEX) * MAX_NUM_VERTICES;
	HRESULT hr = m_pd3dDevice->CreateVertexBuffer(vertexBufferSize, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, 0, D3DPOOL_DEFAULT, &m_pVB, nullptr);
	if ( FAILED(hr) ) {
		return hr;
	}

	bool bSupportsAlphaBlend = true;
	LPDIRECT3D9 pd3d9 = nullptr;
	hr = m_pd3dDevice->GetDirect3D(&pd3d9);
	if ( SUCCEEDED(hr) ) {
		D3DCAPS9 Caps;
		D3DDISPLAYMODE Mode;
		LPDIRECT3DSURFACE9 pSurf = nullptr;
		D3DSURFACE_DESC Desc;
		m_pd3dDevice->GetDeviceCaps( &Caps );
		m_pd3dDevice->GetDisplayMode( 0, &Mode );
		hr = m_pd3dDevice->GetRenderTarget(0, &pSurf);
		if ( SUCCEEDED(hr) ) {
			pSurf->GetDesc( &Desc );
			hr = pd3d9->CheckDeviceFormat(Caps.AdapterOrdinal, Caps.DeviceType, Mode.Format,
				D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_SURFACE,
				Desc.Format);
			if ( FAILED(hr) ) {
				bSupportsAlphaBlend = false;
			}
			SAFE_RELEASE( pSurf );
		}
		SAFE_RELEASE( pd3d9 );
	}

	// Create the state blocks for rendering text
	for ( UINT which=0; which<2; which++ ) {
		m_pd3dDevice->BeginStateBlock();
		m_pd3dDevice->SetTexture( 0, m_pTexture );

		if ( D3DFONT_ZENABLE & m_dwFontFlags ) {
			m_pd3dDevice->SetRenderState( D3DRS_ZENABLE, TRUE );
		} else {
			m_pd3dDevice->SetRenderState( D3DRS_ZENABLE, FALSE );
		}

		if ( bSupportsAlphaBlend ) {
			m_pd3dDevice->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
			m_pd3dDevice->SetRenderState( D3DRS_SRCBLEND,   D3DBLEND_SRCALPHA );
			m_pd3dDevice->SetRenderState( D3DRS_DESTBLEND,  D3DBLEND_INVSRCALPHA );
		} else {
			m_pd3dDevice->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
		}
		m_pd3dDevice->SetRenderState( D3DRS_ALPHATESTENABLE,  TRUE );
		m_pd3dDevice->SetRenderState( D3DRS_ALPHAREF,         0x08 );
		m_pd3dDevice->SetRenderState( D3DRS_ALPHAFUNC,  D3DCMP_GREATEREQUAL );
		m_pd3dDevice->SetRenderState( D3DRS_FILLMODE,   D3DFILL_SOLID );
		m_pd3dDevice->SetRenderState( D3DRS_CULLMODE,   D3DCULL_CCW );
		m_pd3dDevice->SetRenderState( D3DRS_STENCILENABLE,    FALSE );
		m_pd3dDevice->SetRenderState( D3DRS_CLIPPING,         TRUE );
		m_pd3dDevice->SetRenderState( D3DRS_CLIPPLANEENABLE,  FALSE );
		m_pd3dDevice->SetRenderState( D3DRS_VERTEXBLEND,      D3DVBF_DISABLE );
		m_pd3dDevice->SetRenderState( D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE );
		m_pd3dDevice->SetRenderState( D3DRS_FOGENABLE,        FALSE );
		m_pd3dDevice->SetRenderState( D3DRS_COLORWRITEENABLE,
									  D3DCOLORWRITEENABLE_RED  | D3DCOLORWRITEENABLE_GREEN |
									  D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA );
		m_pd3dDevice->SetTextureStageState( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
		m_pd3dDevice->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		m_pd3dDevice->SetTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
		m_pd3dDevice->SetTextureStageState( 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );
		m_pd3dDevice->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
		m_pd3dDevice->SetTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
		m_pd3dDevice->SetTextureStageState( 0, D3DTSS_TEXCOORDINDEX, 0 );
		m_pd3dDevice->SetTextureStageState( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );
		m_pd3dDevice->SetTextureStageState( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
		m_pd3dDevice->SetTextureStageState( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
		m_pd3dDevice->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_POINT );
		m_pd3dDevice->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT );
		m_pd3dDevice->SetSamplerState( 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE );

		if ( which==0 ) {
			m_pd3dDevice->EndStateBlock( &m_pStateBlockSaved );
		} else {
			m_pd3dDevice->EndStateBlock( &m_pStateBlockDrawText );
		}
	}

	return S_OK;
}



//-----------------------------------------------------------------------------
// Name: InvalidateDeviceObjects()
// Desc: Destroys all device-dependent objects
//-----------------------------------------------------------------------------
void CD3D9Font::InvalidateDeviceObjects()
{
	SAFE_RELEASE( m_pVB );
	SAFE_RELEASE( m_pStateBlockSaved );
	SAFE_RELEASE( m_pStateBlockDrawText );
}



//-----------------------------------------------------------------------------
// Name: DeleteDeviceObjects()
// Desc: Destroys all device-dependent objects
//-----------------------------------------------------------------------------
void CD3D9Font::DeleteDeviceObjects()
{
	SAFE_RELEASE( m_pTexture );
	m_pd3dDevice = nullptr;
}



//-----------------------------------------------------------------------------
// Name: GetTextExtent()
// Desc: Get the dimensions of a text string
//-----------------------------------------------------------------------------
HRESULT CD3D9Font::GetTextExtent( const WCHAR* strText, SIZE* pSize )
{
	if ( nullptr==strText || nullptr==pSize ) {
		return E_FAIL;
	}

	FLOAT fRowWidth  = 0.0f;
	FLOAT fRowHeight = (m_fTexCoords[0][3]-m_fTexCoords[0][1])*m_uTexHeight;
	FLOAT fWidth     = 0.0f;
	FLOAT fHeight    = fRowHeight;

	while ( *strText ) {
		WCHAR c = *strText++;

		if ( c == '\n' ) {
			fRowWidth = 0.0f;
			fHeight  += fRowHeight;
			continue;
		}

		auto idx = Char2Index(c);

		FLOAT tx1 = m_fTexCoords[idx][0];
		FLOAT tx2 = m_fTexCoords[idx][2];

		fRowWidth += (tx2-tx1)*m_uTexWidth;

		if ( fRowWidth > fWidth ) {
			fWidth = fRowWidth;
		}
	}

	pSize->cx = (LONG)fWidth;
	pSize->cy = (LONG)fHeight;

	return S_OK;
}



//-----------------------------------------------------------------------------
// Name: DrawText()
// Desc: Draws 2D text. Note that sx and sy are in pixels
//-----------------------------------------------------------------------------
HRESULT CD3D9Font::Draw2DText( FLOAT sx, FLOAT sy, D3DCOLOR color,
							const WCHAR* strText, DWORD dwFlags )
{
	if ( m_pd3dDevice == nullptr ) {
		return E_FAIL;
	}

	// Setup renderstate
	m_pStateBlockSaved->Capture();
	m_pStateBlockDrawText->Apply();
	m_pd3dDevice->SetFVF( D3DFVF_FONT2DVERTEX );
	m_pd3dDevice->SetPixelShader( nullptr );
	m_pd3dDevice->SetStreamSource( 0, m_pVB, 0, sizeof(FONT2DVERTEX) );

	// Set filter states
	if ( dwFlags & D3DFONT_FILTERED ) {
		m_pd3dDevice->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
		m_pd3dDevice->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
	}

	// Center the text block in the viewport
	if ( dwFlags & D3DFONT_CENTERED_X ) {
		D3DVIEWPORT9 vp;
		m_pd3dDevice->GetViewport( &vp );
		const WCHAR* strTextTmp = strText;
		float xFinal = 0.0f;

		while ( *strTextTmp ) {
			WCHAR c = *strTextTmp++;

			if ( c == '\n' ) {
				break;    // Isn't supported.
			}

			auto idx = Char2Index(c);

			FLOAT tx1 = m_fTexCoords[idx][0];
			FLOAT tx2 = m_fTexCoords[idx][2];

			FLOAT w = (tx2-tx1) *  m_uTexWidth / m_fTextScale;

			xFinal += w;
		}

		sx = (vp.Width-xFinal)/2.0f;
	}
	if ( dwFlags & D3DFONT_CENTERED_Y ) {
		D3DVIEWPORT9 vp;
		m_pd3dDevice->GetViewport( &vp );
		float fLineHeight = ((m_fTexCoords[0][3]-m_fTexCoords[0][1])*m_uTexHeight);
		sy = (vp.Height-fLineHeight)/2;
	}

	// Adjust for character spacing
	FLOAT fStartX = sx;

	// Fill vertex buffer
	FONT2DVERTEX* pVertices = nullptr;
	DWORD         dwNumTriangles = 0;
	m_pVB->Lock( 0, 0, (void**)&pVertices, D3DLOCK_DISCARD );

	while ( *strText ) {
		WCHAR c = *strText++;

		if ( c == '\n' ) {
			sx = fStartX;
			sy += (m_fTexCoords[0][3]-m_fTexCoords[0][1])*m_uTexHeight;
			continue;
		}

		auto idx = Char2Index(c);

		FLOAT tx1 = m_fTexCoords[idx][0];
		FLOAT ty1 = m_fTexCoords[idx][1];
		FLOAT tx2 = m_fTexCoords[idx][2];
		FLOAT ty2 = m_fTexCoords[idx][3];

		FLOAT w = (tx2-tx1) *  m_uTexWidth / m_fTextScale;
		FLOAT h = (ty2-ty1) * m_uTexHeight / m_fTextScale;

		if ( c != 0x0020 && c != 0x00A0) { // Space and No-Break Space
			*pVertices++ = InitFont2DVertex( DirectX::XMFLOAT4(sx+0-0.5f,sy+h-0.5f,0.9f,1.0f), color, tx1, ty2 );
			*pVertices++ = InitFont2DVertex( DirectX::XMFLOAT4(sx+0-0.5f,sy+0-0.5f,0.9f,1.0f), color, tx1, ty1 );
			*pVertices++ = InitFont2DVertex( DirectX::XMFLOAT4(sx+w-0.5f,sy+h-0.5f,0.9f,1.0f), color, tx2, ty2 );
			*pVertices++ = InitFont2DVertex( DirectX::XMFLOAT4(sx+w-0.5f,sy+0-0.5f,0.9f,1.0f), color, tx2, ty1 );
			*pVertices++ = InitFont2DVertex( DirectX::XMFLOAT4(sx+w-0.5f,sy+h-0.5f,0.9f,1.0f), color, tx2, ty2 );
			*pVertices++ = InitFont2DVertex( DirectX::XMFLOAT4(sx+0-0.5f,sy+0-0.5f,0.9f,1.0f), color, tx1, ty1 );
			dwNumTriangles += 2;

			if ( dwNumTriangles*3 > (MAX_NUM_VERTICES-6) ) {
				// Unlock, render, and relock the vertex buffer
				m_pVB->Unlock();
				m_pd3dDevice->DrawPrimitive( D3DPT_TRIANGLELIST, 0, dwNumTriangles );
				pVertices = nullptr;
				m_pVB->Lock( 0, 0, (void**)&pVertices, D3DLOCK_DISCARD );
				dwNumTriangles = 0L;
			}
		}

		sx += w;
	}

	// Unlock and render the vertex buffer
	m_pVB->Unlock();
	if ( dwNumTriangles > 0 ) {
		m_pd3dDevice->DrawPrimitive( D3DPT_TRIANGLELIST, 0, dwNumTriangles );
	}

	// Restore the modified renderstates
	m_pStateBlockSaved->Apply();

	return S_OK;
}
