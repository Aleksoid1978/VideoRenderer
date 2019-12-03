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

#define FONTBITMAP_MODE 0

struct Grid_t {
	UINT stepX = 0;
	UINT stepY = 0;
	UINT columns = 0;
	UINT lines = 0;
};

#if FONTBITMAP_MODE == 0

class CFontBitmapGDI
{
private:
	HDC     m_hDC     = nullptr;
	HFONT   m_hFont   = nullptr;

	HBITMAP m_hBitmap = nullptr;
	DWORD* m_pBitmapBits = nullptr;
	UINT m_bmWidth = 0;
	UINT m_bmHeight = 0;
	std::vector<SIZE> m_charSizes;

	HRESULT CalcGrid(Grid_t& grid, const WCHAR* chars, UINT lenght, UINT bmWidth, UINT bmHeight, bool bSetCharSizes)
	{
		if (bSetCharSizes) {
			m_charSizes.reserve(lenght);
		}

		SIZE size;
		LONG maxWidth = 0;
		LONG maxHeight = 0;
		for (UINT i = 0; i < lenght; i++) {
			if (GetTextExtentPoint32W(m_hDC, &chars[i], 1, &size) == 0) {
				ASSERT(0);
				return E_FAIL;
			}
			if (bSetCharSizes) {
				m_charSizes.emplace_back(size);
			}
			if (size.cx > maxWidth) {
				maxWidth = size.cx;
			}
			if (size.cy > maxHeight) {
				maxHeight = size.cy;
			}
		}

		grid.stepX = (int)ceil(maxWidth) + 2;
		grid.stepY = (int)ceil(maxHeight);

		grid.columns = bmWidth / grid.stepX;
		grid.lines = bmHeight / grid.stepY;

		return S_OK;
	}

public:
	CFontBitmapGDI(const WCHAR* fontName, const int fontHeight)
	{
		m_hDC = CreateCompatibleDC(nullptr);
		SetMapMode(m_hDC, MM_TEXT);

		// Create a font.  By specifying ANTIALIASED_QUALITY, we might get an
		// antialiased font, but this is not guaranteed.
		int nHeight    = -(int)(fontHeight);
		DWORD dwBold   = /*(m_dwFontFlags & D3DFONT_BOLD)   ? FW_BOLD :*/ FW_NORMAL;
		DWORD dwItalic = /*(m_dwFontFlags & D3DFONT_ITALIC) ? TRUE    :*/ FALSE;

		m_hFont = CreateFontW(nHeight, 0, 0, 0, dwBold, dwItalic,
			FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
			VARIABLE_PITCH, fontName);
	}

	~CFontBitmapGDI()
	{
		DeleteObject(m_hBitmap);
		DeleteObject(m_hFont);
		DeleteDC(m_hDC);
	}

	HRESULT CheckBitmapDimensions(const WCHAR* chars, const UINT lenght, const UINT bmWidth, const UINT bmHeight)
	{
		HFONT hFontOld = (HFONT)SelectObject(m_hDC, m_hFont);

		Grid_t grid;
		HRESULT hr = CalcGrid(grid, chars, lenght, bmWidth, bmHeight, false);

		SelectObject(m_hDC, hFontOld);

		if (FAILED(hr)) {
			return hr;
		}

		return (lenght <= grid.lines * grid.columns) ? S_OK : D3DERR_MOREDATA;
	}

	HRESULT DrawCharacters(const WCHAR* chars, FLOAT* fTexCoords, const UINT lenght, const UINT bmWidth, const UINT bmHeight)
	{
		HFONT hFontOld = (HFONT)SelectObject(m_hDC, m_hFont);

		Grid_t grid;
		HRESULT hr = CalcGrid(grid, chars, lenght, bmWidth, bmHeight, true);

		if (FAILED(hr)) {
			SelectObject(m_hDC, hFontOld);
			return hr;
		}

		// Prepare to create a bitmap
		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth       =  (LONG)bmWidth;
		bmi.bmiHeader.biHeight      = -(LONG)bmHeight;
		bmi.bmiHeader.biPlanes      = 1;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biBitCount    = 32;

		// Create a bitmap for the font
		m_pBitmapBits = nullptr;
		DeleteObject(m_hBitmap);
		m_hBitmap = CreateDIBSection(m_hDC, &bmi, DIB_RGB_COLORS, (void**)&m_pBitmapBits, nullptr, 0);
		m_bmWidth = bmWidth;
		m_bmHeight = bmHeight;

		HGDIOBJ m_hBitmapOld = SelectObject(m_hDC, m_hBitmap);

		SetTextColor(m_hDC, RGB(255, 255, 255));
		SetBkColor(m_hDC, 0x00000000);
		SetTextAlign(m_hDC, TA_TOP);

		UINT idx = 0;
		for (UINT y = 0; y < grid.lines; y++) {
			for (UINT x = 0; x < grid.columns; x++) {
				if (idx >= lenght) {
					break;
				}
				POINT point = { x*grid.stepX + 1, y*grid.stepY };
				BOOL ret = ExtTextOutW(m_hDC, point.x, point.y, ETO_OPAQUE, nullptr, &chars[idx], 1, nullptr);

				*fTexCoords++ = (float)point.x / bmWidth;
				*fTexCoords++ = (float)point.y / bmHeight;
				*fTexCoords++ = (float)(point.x + m_charSizes[idx].cx) / bmWidth;
				*fTexCoords++ = (float)(point.y + m_charSizes[idx].cy) / bmHeight;

				ASSERT(ret == TRUE);
				idx++;
			}
		}
		GdiFlush();

		SelectObject(m_hDC, m_hBitmapOld);
		SelectObject(m_hDC, hFontOld);

		return S_OK;
	}

	bool CopyBitmapToA8L8(BYTE* pDst, int dst_pitch)
	{
		ASSERT(pDst && dst_pitch);

		if (m_hBitmap && m_pBitmapBits) {
			for (UINT y = 0; y < m_bmHeight; y++) {
				uint16_t* pDst16 = (uint16_t*)pDst;

				for (UINT x = 0; x < m_bmWidth; x++) {
					// 4-bit measure of pixel intensity
					DWORD pix = m_pBitmapBits[m_bmWidth*y + x];
					DWORD r = (pix & 0x00ff0000) >> 16;
					DWORD g = (pix & 0x0000ff00) >> 8;
					DWORD b = (pix & 0x000000ff);
					DWORD l = ((r * 1063 + g * 3576 + b * 361) / 5000);
					*pDst16++ = (uint16_t)((l << 8) + l);
				}
				pDst += dst_pitch;
			}

			return true;
		}

		return false;
	}
};

#elif FONTBITMAP_MODE == 1

#include <gdiplus.h>

class CFontBitmapGDIPlus
{
private:
	// GDI+ handling
	ULONG_PTR m_gdiplusToken;
	Gdiplus::GdiplusStartupInput m_gdiplusStartupInput;

	Gdiplus::FontFamily* m_pFontFamily;
	Gdiplus::Font*       m_pFont;
	Gdiplus::SolidBrush* m_pSolidBrush;

	Gdiplus::Bitmap*     m_pBitmap = nullptr;
	std::vector<SIZE> m_charSizes;

	HRESULT CalcGrid(Gdiplus::Graphics* pGraphics, Grid_t& grid, const WCHAR* chars, UINT lenght, UINT bmWidth, UINT bmHeight, bool bSetCharSizes)
	{
		if (bSetCharSizes) {
			m_charSizes.reserve(lenght);
		}

		Gdiplus::RectF rect;
		float maxWidth = 0;
		float maxHeight = 0;
		for (UINT i = 0; i < lenght; i++) {
			if (pGraphics->MeasureString(&chars[i], 1, m_pFont, Gdiplus::PointF(0, 0), &rect) != Gdiplus::Ok) {
				ASSERT(0);
				return E_FAIL;
			}
			if (bSetCharSizes) {
				SIZE size = { (LONG)ceil(rect.Width), (LONG)ceil(rect.Height) };
				m_charSizes.emplace_back(size);
			}
			if (rect.Width > maxWidth) {
				maxWidth = rect.Width;
			}
			if (rect.Height > maxHeight) {
				maxHeight = rect.Height;
			}
			ASSERT(rect.X == 0 && rect.Y == 0);
		}

		grid.stepX = (int)ceil(maxWidth) + 2;
		grid.stepY = (int)ceil(maxHeight);

		grid.columns = bmWidth / grid.stepX;
		grid.lines = bmHeight / grid.stepY;

		return S_OK;
	}

public:
	CFontBitmapGDIPlus(const WCHAR* fontName, const int fontHeight)
	{
		using namespace Gdiplus;
		// GDI+ handling
		GdiplusStartup(&m_gdiplusToken, &m_gdiplusStartupInput, nullptr);

		m_pFontFamily = new FontFamily(fontName);
		m_pFont = new Font(m_pFontFamily, fontHeight, FontStyleRegular, UnitPixel);
		m_pSolidBrush = new SolidBrush(Color::White);
	}

	~CFontBitmapGDIPlus()
	{
		SAFE_DELETE(m_pBitmap);

		SAFE_DELETE(m_pSolidBrush);
		SAFE_DELETE(m_pFont);
		SAFE_DELETE(m_pFontFamily);

		// GDI+ handling
		Gdiplus::GdiplusShutdown(m_gdiplusToken);
	}

	HRESULT CheckBitmapDimensions(const WCHAR* chars, UINT lenght, UINT bmWidth, UINT bmHeight)
	{
		using namespace Gdiplus;

		Bitmap* pTestBitmap = new Bitmap(32, 32, PixelFormat32bppARGB); // bitmap dimensions are not important here
		Graphics* pGraphics = new Graphics(pTestBitmap);

		Grid_t grid;
		HRESULT hr = CalcGrid(pGraphics, grid, chars, lenght, bmWidth, bmHeight, false);

		SAFE_DELETE(pGraphics);
		SAFE_DELETE(pTestBitmap);

		if (FAILED(hr)) {
			return hr;
		}

		return (lenght <= grid.lines * grid.columns) ? S_OK : D3DERR_MOREDATA;
	}

	HRESULT DrawCharacters(const WCHAR* chars, FLOAT* fTexCoords, const UINT lenght, const UINT bmWidth, const UINT bmHeight)
	{
		using namespace Gdiplus;

		Status status = Gdiplus::Ok;

		SAFE_DELETE(m_pBitmap);
		m_pBitmap = new Bitmap(bmWidth, bmHeight, PixelFormat32bppARGB);
		Graphics* pGraphics = new Graphics(m_pBitmap);

		Grid_t grid;
		CalcGrid(pGraphics, grid, chars, lenght, bmWidth, bmHeight, true);

		UINT idx = 0;
		for (UINT y = 0; y < grid.lines; y++) {
			for (UINT x = 0; x < grid.columns; x++) {
				if (idx >= lenght) {
					break;
				}
				PointF point(x*grid.stepX + 1, y*grid.stepY);
				status = pGraphics->DrawString(&chars[idx], 1, m_pFont, point, m_pSolidBrush);

				*fTexCoords++ = point.X / bmWidth;
				*fTexCoords++ = point.Y / bmHeight;
				*fTexCoords++ = (point.X + m_charSizes[idx].cx) / bmWidth;
				*fTexCoords++ = (point.Y + m_charSizes[idx].cy) / bmHeight;

				ASSERT(Gdiplus::Ok == status);
				idx++;
			}
		}
		pGraphics->Flush();

		SAFE_DELETE(pGraphics);

		if (Gdiplus::Ok == status) {
			return (idx == lenght) ? S_OK : S_FALSE;
		} else {
			return E_FAIL;
		}
	}

	bool CopyBitmapToA8L8(BYTE* pDst, int dst_pitch)
	{
		ASSERT(pDst && dst_pitch);

		if (m_pBitmap) {
			Gdiplus::BitmapData bitmapData;
			const UINT w = m_pBitmap->GetWidth();
			const UINT h = m_pBitmap->GetHeight();
			Gdiplus::Rect rect(0, 0, w, h);

			if (Gdiplus::Ok == m_pBitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData)) {
				BYTE* pSrc = (BYTE*)bitmapData.Scan0;

				for (UINT y = 0; y < h; y++) {
					uint32_t* pSrc32 = (uint32_t*)pSrc;
					uint16_t* pDst16 = (uint16_t*)pDst;

					for (UINT x = 0; x < w; x++) {
						// 4-bit measure of pixel intensity
						uint32_t pix = *pSrc32++;
						uint32_t a = (pix & 0xff000000) >> 24;
						uint32_t r = (pix & 0x00ff0000) >> 16;
						uint32_t g = (pix & 0x0000ff00) >> 8;
						uint32_t b = (pix & 0x000000ff);
						uint32_t l = ((r * 1063 + g * 3576 + b * 361) / 5000);
						*pDst16++ = (uint16_t)((a << 8) + l);
					}
					pSrc += bitmapData.Stride;
					pDst += dst_pitch;
				}
				m_pBitmap->UnlockBits(&bitmapData);

				return true;
			}
		}

		return false;
	}
};

#elif FONTBITMAP_MODE == 2

#include <dwrite.h>
#include <d2d1.h>

class CFontBitmapDWrite // TODO
{
private:

	HRESULT CalcGrid(Grid_t& grid, const WCHAR* chars, UINT lenght, UINT bmWidth, UINT bmHeight, bool bSetCharSizes)
	{
		return E_NOTIMPL;
	}

public:
	CFontBitmapDWrite(const WCHAR* fontName, const int fontHeight)
	{
	}

	~CFontBitmapDWrite()
	{
	}

	HRESULT CheckBitmapDimensions(const WCHAR* chars, const UINT lenght, const UINT bmWidth, const UINT bmHeight)
	{
		return E_NOTIMPL;
	}

	HRESULT DrawCharacters(const WCHAR* chars, FLOAT* fTexCoords, const UINT lenght, const UINT bmWidth, const UINT bmHeight)
	{
		return E_NOTIMPL;
	}

	bool CopyBitmapToA8L8(BYTE* pDst, int dst_pitch)
	{
		return false;
	}
};

#endif
