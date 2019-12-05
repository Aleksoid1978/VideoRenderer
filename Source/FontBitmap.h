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

#define FONTBITMAP_MODE 1
// 0 - GDI, 1 - GDI+, 2 - DirectWrite (not done yet)

#define DUMP_BITMAP 0

struct Grid_t {
	UINT stepX = 0;
	UINT stepY = 0;
	UINT columns = 0;
	UINT lines = 0;
};

inline uint16_t A8R8G8B8toA8L8(uint32_t pix)
{
	uint32_t a = (pix & 0xff000000) >> 24;
	uint32_t r = (pix & 0x00ff0000) >> 16;
	uint32_t g = (pix & 0x0000ff00) >> 8;
	uint32_t b = (pix & 0x000000ff);
	uint32_t l = ((r * 1063 + g * 3576 + b * 361) / 5000);

	return (uint16_t)((a << 8) + l);
}

inline uint16_t X8R8G8B8toA8L8(uint32_t pix)
{
	uint32_t r = (pix & 0x00ff0000) >> 16;
	uint32_t g = (pix & 0x0000ff00) >> 8;
	uint32_t b = (pix & 0x000000ff);
	uint32_t l = ((r * 1063 + g * 3576 + b * 361) / 5000);

	return (uint16_t)((l << 8) + l); // the darker the more transparent
}

#if FONTBITMAP_MODE == 0

class CFontBitmapGDI
{
private:
	HDC     m_hDC     = nullptr;

	HBITMAP m_hBitmap = nullptr;
	DWORD* m_pBitmapBits = nullptr;
	UINT m_bmWidth = 0;
	UINT m_bmHeight = 0;
	std::vector<RECT> m_charCoords;

public:
	CFontBitmapGDI()
	{
		m_hDC = CreateCompatibleDC(nullptr);
		SetMapMode(m_hDC, MM_TEXT);
	}

	~CFontBitmapGDI()
	{
		DeleteObject(m_hBitmap);

		DeleteDC(m_hDC);
	}

	HRESULT Initialize(const WCHAR* fontName, const int fontHeight, const WCHAR* chars, UINT lenght)
	{
		DeleteObject(m_hBitmap);
		m_pBitmapBits = nullptr;
		m_charCoords.clear();
		
		HRESULT hr = S_OK;

		// Create a font.  By specifying ANTIALIASED_QUALITY, we might get an
		// antialiased font, but this is not guaranteed.
		int nHeight    = -(int)(fontHeight);
		DWORD dwBold   = /*(m_dwFontFlags & D3DFONT_BOLD)   ? FW_BOLD :*/ FW_NORMAL;
		DWORD dwItalic = /*(m_dwFontFlags & D3DFONT_ITALIC) ? TRUE    :*/ FALSE;

		HFONT hFont = CreateFontW(nHeight, 0, 0, 0, dwBold, dwItalic,
			FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
			VARIABLE_PITCH, fontName);

		HFONT hFontOld = (HFONT)SelectObject(m_hDC, hFont);

		std::vector<SIZE> charSizes;
		charSizes.reserve(lenght);
		SIZE size;
		LONG maxWidth = 0;
		LONG maxHeight = 0;
		for (UINT i = 0; i < lenght; i++) {
			if (GetTextExtentPoint32W(m_hDC, &chars[i], 1, &size) == FALSE) {
				hr = E_FAIL;
				break;
			}
			charSizes.emplace_back(size);

			if (size.cx > maxWidth) {
				maxWidth = size.cx;
			}
			if (size.cy > maxHeight) {
				maxHeight = size.cy;
			}
		}

		if (S_OK == hr) {
			UINT stepX = maxWidth + 2;
			UINT stepY = maxHeight;
			UINT bmWidth = 128;
			UINT bmHeight = 128;
			UINT columns = bmWidth / stepX;
			UINT lines = bmHeight / stepY;

			while (lenght > lines * columns) {
				if (bmWidth <= bmHeight) {
					bmWidth *= 2;
				} else {
					bmHeight += 128;
				}
				columns = bmWidth / stepX;
				lines = bmHeight / stepY;
			};

			// Prepare to create a bitmap
			BITMAPINFO bmi = {};
			bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
			bmi.bmiHeader.biWidth       =  (LONG)bmWidth;
			bmi.bmiHeader.biHeight      = -(LONG)bmHeight;
			bmi.bmiHeader.biPlanes      = 1;
			bmi.bmiHeader.biCompression = BI_RGB;
			bmi.bmiHeader.biBitCount    = 32;

			// Create a bitmap for the font
			m_hBitmap = CreateDIBSection(m_hDC, &bmi, DIB_RGB_COLORS, (void**)&m_pBitmapBits, nullptr, 0);
			m_bmWidth = bmWidth;
			m_bmHeight = bmHeight;

			HGDIOBJ m_hBitmapOld = SelectObject(m_hDC, m_hBitmap);

			SetTextColor(m_hDC, RGB(255, 255, 255));
			SetBkColor(m_hDC, 0x00000000);
			SetTextAlign(m_hDC, TA_TOP);

			UINT idx = 0;
			for (UINT y = 0; y < lines; y++) {
				for (UINT x = 0; x < columns; x++) {
					if (idx >= lenght) {
						break;
					}
					UINT X = x * stepX + 1;
					UINT Y = y * stepY;
					if (ExtTextOutW(m_hDC, X, Y, ETO_OPAQUE, nullptr, &chars[idx], 1, nullptr) == FALSE) {
						hr = E_FAIL;
						break;
					}
					RECT rect = {
						X,
						Y,
						X + charSizes[idx].cx,
						Y + charSizes[idx].cy
					};
					m_charCoords.emplace_back(rect);
					idx++;
				}
			}
			GdiFlush();

			SelectObject(m_hDC, m_hBitmapOld);
		}

		SelectObject(m_hDC, hFontOld);
		DeleteObject(hFont);

		return hr;
	}

	UINT GetWidth()
	{
		return m_bmWidth;
	}

	UINT GetHeight()
	{
		return m_bmHeight;
	}

	HRESULT GetFloatCoords(float* pTexCoords, const UINT lenght)
	{
		ASSERT(pTexCoords);

		if (!m_hBitmap || !m_pBitmapBits || lenght != m_charCoords.size()) {
			return E_ABORT;
		}

		for (const auto coord : m_charCoords) {
			*pTexCoords++ = (float)coord.left   / m_bmWidth;
			*pTexCoords++ = (float)coord.top    / m_bmHeight;
			*pTexCoords++ = (float)coord.right  / m_bmWidth;
			*pTexCoords++ = (float)coord.bottom / m_bmHeight;
		}

		return S_OK;
	}

	HRESULT CopyBitmapToA8L8(BYTE* pDst, int dst_pitch)
	{
		ASSERT(pDst && dst_pitch);

		if (!m_hBitmap || !m_pBitmapBits) {
			return E_ABORT;
		}
#if _DEBUG && DUMP_BITMAP
		SaveARGB32toBMP((BYTE*)m_pBitmapBits, m_bmWidth*4, m_bmWidth, m_bmHeight, L"c:\\temp\\font_gdi_bitmap.bmp");
#endif
		BYTE* pSrc = (BYTE*)m_pBitmapBits;
		UINT src_pitch = m_bmWidth * 4;

		for (UINT y = 0; y < m_bmHeight; y++) {
			uint32_t* pSrc32 = (uint32_t*)pSrc;
			uint16_t* pDst16 = (uint16_t*)pDst;

			for (UINT x = 0; x < m_bmWidth; x++) {
				*pDst16++ = X8R8G8B8toA8L8(*pSrc32++);
			}
			pSrc += src_pitch;
			pDst += dst_pitch;
		}

		return S_OK;
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

	const Gdiplus::TextRenderingHint m_TextRenderingHint = Gdiplus::TextRenderingHintClearTypeGridFit;
	// TextRenderingHintClearTypeGridFit gives a better result than TextRenderingHintAntiAliasGridFit
	// Perhaps this is only for normal thickness. Because subpixel anti-aliasing we lose after copying to the texture.

	Gdiplus::Bitmap* m_pBitmap = nullptr;
	std::vector<RECT> m_charCoords;

public:
	CFontBitmapGDIPlus()
	{
		// GDI+ handling
		GdiplusStartup(&m_gdiplusToken, &m_gdiplusStartupInput, nullptr);
	}

	~CFontBitmapGDIPlus()
	{
		SAFE_DELETE(m_pBitmap);

		// GDI+ handling
		Gdiplus::GdiplusShutdown(m_gdiplusToken);
	}

	HRESULT Initialize(const WCHAR* fontName, const int fontHeight, const WCHAR* chars, UINT lenght)
	{
		SAFE_DELETE(m_pBitmap);
		m_charCoords.clear();

		auto status = Gdiplus::Ok;

		auto pFontFamily = new Gdiplus::FontFamily(fontName);
		auto pFont = new Gdiplus::Font(pFontFamily, fontHeight, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

		auto pStringFormat = Gdiplus::StringFormat::GenericTypographic()->Clone();
		auto flags = pStringFormat->GetFormatFlags() | Gdiplus::StringFormatFlags::StringFormatFlagsMeasureTrailingSpaces;
		pStringFormat->SetFormatFlags(flags);

		auto pTestBitmap = new Gdiplus::Bitmap(32, 32, PixelFormat32bppARGB); // bitmap dimensions are not important here
		auto pTestGraphics = new Gdiplus::Graphics(pTestBitmap);
		pTestGraphics->SetTextRenderingHint(m_TextRenderingHint);

		std::vector<SIZE> charSizes;
		charSizes.reserve(lenght);
		Gdiplus::RectF rect;
		float maxWidth = 0;
		float maxHeight = 0;

		for (UINT i = 0; i < lenght; i++) {
			status = pTestGraphics->MeasureString(&chars[i], 1, pFont, Gdiplus::PointF(0, 0), pStringFormat, &rect);
			if (Gdiplus::Ok != status) {
				break;
			}
			SIZE size = { (LONG)ceil(rect.Width), (LONG)ceil(rect.Height) };
			charSizes.emplace_back(size);

			if (rect.Width > maxWidth) {
				maxWidth = rect.Width;
			}
			if (rect.Height > maxHeight) {
				maxHeight = rect.Height;
			}
			ASSERT(rect.X == 0 && rect.Y == 0);
		}
		SAFE_DELETE(pTestGraphics);
		SAFE_DELETE(pTestBitmap);

		if (Gdiplus::Ok == status) {
			UINT stepX = (UINT)ceil(maxWidth) + 2;
			UINT stepY = (UINT)ceil(maxHeight);
			UINT bmWidth = 128;
			UINT bmHeight = 128;
			UINT columns = bmWidth / stepX;
			UINT lines = bmHeight / stepY;

			while (lenght > lines * columns) {
				if (bmWidth <= bmHeight) {
					bmWidth *= 2;
				} else {
					bmHeight += 128;
				}
				columns = bmWidth / stepX;
				lines = bmHeight / stepY;
			};

			m_pBitmap = new Gdiplus::Bitmap(bmWidth, bmHeight, PixelFormat32bppARGB);
			auto pGraphics = new Gdiplus::Graphics(m_pBitmap);
			pGraphics->SetTextRenderingHint(m_TextRenderingHint);
			auto pBrushWhite = new Gdiplus::SolidBrush(Gdiplus::Color::White);

			m_charCoords.reserve(lenght);

			UINT idx = 0;
			for (UINT y = 0; y < lines; y++) {
				for (UINT x = 0; x < columns; x++) {
					if (idx >= lenght) {
						break;
					}
					UINT X = x * stepX + 1;
					UINT Y = y * stepY;
					status = pGraphics->DrawString(&chars[idx], 1, pFont, Gdiplus::PointF(X, Y), pStringFormat, pBrushWhite);
					if (Gdiplus::Ok != status) {
						break;
					}
					RECT rect = {
						X,
						Y,
						X + charSizes[idx].cx,
						Y + charSizes[idx].cy
					};
					m_charCoords.emplace_back(rect);
					idx++;
				}
			}
			pGraphics->Flush();

			SAFE_DELETE(pGraphics);
			SAFE_DELETE(pBrushWhite);

			if (Gdiplus::Ok == status) {

			}
		}

		SAFE_DELETE(pStringFormat);
		SAFE_DELETE(pFont);
		SAFE_DELETE(pFontFamily);

		if (Gdiplus::Ok == status) {
			ASSERT(m_charCoords.size() == lenght);
			return S_OK;
		}
		return E_FAIL;
	}

	UINT GetWidth()
	{
		return m_pBitmap ? m_pBitmap->GetWidth() : 0;
	}

	UINT GetHeight()
	{
		return m_pBitmap ? m_pBitmap->GetHeight() : 0;
	}

	HRESULT GetFloatCoords(float* pTexCoords, const UINT lenght)
	{
		ASSERT(pTexCoords);

		if (!m_pBitmap || lenght != m_charCoords.size()) {
			return E_ABORT;
		}

		auto w = m_pBitmap->GetWidth();
		auto h = m_pBitmap->GetHeight();

		for (const auto coord : m_charCoords) {
			*pTexCoords++ = (float)coord.left   / w;
			*pTexCoords++ = (float)coord.top    / h;
			*pTexCoords++ = (float)coord.right  / w;
			*pTexCoords++ = (float)coord.bottom / h;
		}

		return S_OK;
	}

	HRESULT CopyBitmapToA8L8(BYTE* pDst, int dst_pitch)
	{
		ASSERT(pDst && dst_pitch);

		if (!m_pBitmap) {
			return E_ABORT;
		}

		Gdiplus::BitmapData bitmapData;
		const UINT w = m_pBitmap->GetWidth();
		const UINT h = m_pBitmap->GetHeight();
		Gdiplus::Rect rect(0, 0, w, h);

		if (Gdiplus::Ok == m_pBitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData)) {
#if _DEBUG && DUMP_BITMAP
			SaveARGB32toBMP((BYTE*)bitmapData.Scan0, bitmapData.Stride, w, h, L"c:\\temp\\font_gdiplus_bitmap.bmp");
			SaveBitmapToPNG(L"C:\\TEMP\\font_gdiplus_bitmap.png");
#endif
			BYTE* pSrc = (BYTE*)bitmapData.Scan0;

			for (UINT y = 0; y < h; y++) {
				uint32_t* pSrc32 = (uint32_t*)pSrc;
				uint16_t* pDst16 = (uint16_t*)pDst;

				for (UINT x = 0; x < w; x++) {
					*pDst16++ = A8R8G8B8toA8L8(*pSrc32++);
				}
				pSrc += bitmapData.Stride;
				pDst += dst_pitch;
			}
			m_pBitmap->UnlockBits(&bitmapData);
		}

		return S_OK;;
	}

private:
	HRESULT SaveBitmapToPNG(const wchar_t* filename)
	{
		if (!m_pBitmap) {
			return E_POINTER;
		}

		CLSID pngClsid = CLSID_NULL;

		UINT num = 0;  // number of image encoders
		UINT size = 0; // size of the image encoder array in bytes
		Gdiplus::GetImageEncodersSize(&num, &size);
		if (size == 0) {
			return E_FAIL;  // Failure
		}

		Gdiplus::ImageCodecInfo* pImageCodecInfo = nullptr;
		pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
		if (pImageCodecInfo == nullptr) {
			return E_FAIL;
		}

		Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

		for (UINT j = 0; j < num; ++j) {
			if (wcscmp(pImageCodecInfo[j].MimeType, L"image/png") == 0) {
				pngClsid = pImageCodecInfo[j].Clsid;
				break;
			}
		}
		free(pImageCodecInfo);
		if (pngClsid == CLSID_NULL) {
			return E_FAIL;
		}

		Gdiplus::Status status = m_pBitmap->Save(filename, &pngClsid, nullptr);

		return (Gdiplus::Ok == status) ? S_OK : E_FAIL;
	}
};

#elif FONTBITMAP_MODE == 2

#include <dwrite.h>
#include <d2d1.h>

class CFontBitmapDWrite // TODO
{
private:
	CComPtr<IDWriteFactory>    m_pDWriteFactory;
	CComPtr<IDWriteTextFormat> m_pTextFormat;

	CComPtr<ID2D1Factory>         m_pD2D1Factory;
	CComPtr<ID2D1RenderTarget>    m_pD2D1RenderTarget;
	CComPtr<ID2D1SolidColorBrush> m_pD2D1Brush;

	std::vector<SIZE> m_charSizes;

	HRESULT CalcGrid(Grid_t& grid, const WCHAR* chars, UINT lenght, UINT bmWidth, UINT bmHeight, bool bSetCharSizes)
	{
		if (bSetCharSizes) {
			m_charSizes.reserve(lenght);
		}

		DWRITE_TEXT_METRICS textMetrics;
		float maxWidth = 0;
		float maxHeight = 0;
		for (UINT i = 0; i < lenght; i++) {
			IDWriteTextLayout* pTextLayout;
			HRESULT hr = m_pDWriteFactory->CreateTextLayout(&chars[i], 1, m_pTextFormat, 0, 0, &pTextLayout);
			if (S_OK == hr) {
				hr = pTextLayout->GetMetrics(&textMetrics);
				pTextLayout->Release();
			}
			if (FAILED(hr)) {
				return hr;
			}

			if (bSetCharSizes) {
				SIZE size = { (LONG)ceil(textMetrics.width), (LONG)ceil(textMetrics.height) };
				m_charSizes.emplace_back(size);
			}
			if (textMetrics.width > maxWidth) {
				maxWidth = textMetrics.width;
			}
			if (textMetrics.height > maxHeight) {
				maxHeight = textMetrics.height;
			}
			ASSERT(textMetrics.left == 0 && textMetrics.top == 0);
		}

		grid.stepX = (int)ceil(maxWidth) + 2;
		grid.stepY = (int)ceil(maxHeight);

		grid.columns = bmWidth / grid.stepX;
		grid.lines = bmHeight / grid.stepY;

		return S_OK;
	}

public:
	CFontBitmapDWrite(const WCHAR* fontName, const int fontHeight)
	{
		D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
		options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
		HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, &m_pD2D1Factory);

		hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(m_pDWriteFactory), reinterpret_cast<IUnknown**>(&m_pDWriteFactory));
		if (S_OK == hr) {
			hr = m_pDWriteFactory->CreateTextFormat(
				fontName,
				nullptr,
				DWRITE_FONT_WEIGHT_NORMAL,
				DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL,
				fontHeight,
				L"", //locale
				&m_pTextFormat);
		}
	}

	~CFontBitmapDWrite()
	{
		m_pTextFormat.Release();
		m_pDWriteFactory.Release();

		m_pD2D1Brush.Release();
		m_pD2D1RenderTarget.Release();
		m_pD2D1Factory.Release();
	}

	HRESULT CheckBitmapDimensions(const WCHAR* chars, const UINT lenght, const UINT bmWidth, const UINT bmHeight)
	{
		Grid_t grid;
		HRESULT hr = CalcGrid(grid, chars, lenght, bmWidth, bmHeight, false);
		if (FAILED(hr)) {
			return hr;
		}

		return (lenght <= grid.lines * grid.columns) ? S_OK : D3DERR_MOREDATA;
	}

	HRESULT DrawCharacters(const WCHAR* chars, FLOAT* fTexCoords, const UINT lenght, const UINT bmWidth, const UINT bmHeight)
	{
		Grid_t grid;
		HRESULT hr = CalcGrid(grid, chars, lenght, bmWidth, bmHeight, true);
		if (FAILED(hr)) {
			return hr;
		}

		D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT,
			D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
			96, 96);

		// TODO
		hr = m_pD2D1Factory->Create...RenderTarget(..., &props, &m_pD2D1RenderTarget);
		if (S_OK == hr) {
			hr = m_pD2D1RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_pD2D1Brush);
		}

		m_pD2D1RenderTarget->BeginDraw();
		UINT idx = 0;
		for (UINT y = 0; y < grid.lines; y++) {
			for (UINT x = 0; x < grid.columns; x++) {
				if (idx >= lenght) {
					break;
				}
				D2D1_POINT_2F point = { x*grid.stepX + 1, y*grid.stepY };
				IDWriteTextLayout* pTextLayout;
				hr = m_pDWriteFactory->CreateTextLayout(&chars[idx], 1, m_pTextFormat, 0, 0, &pTextLayout);
				if (S_OK == hr) {
					m_pD2D1RenderTarget->DrawTextLayout(point, pTextLayout, m_pD2D1Brush);
					pTextLayout->Release();
				}

				*fTexCoords++ = point.x / bmWidth;
				*fTexCoords++ = point.y / bmHeight;
				*fTexCoords++ = (point.x + m_charSizes[idx].cx) / bmWidth;
				*fTexCoords++ = (point.y + m_charSizes[idx].cy) / bmHeight;

				ASSERT(hr == S_OK);
				idx++;
			}
		}
		m_pD2D1RenderTarget->EndDraw();


		if (S_OK == hr) {
			return (idx == lenght) ? S_OK : S_FALSE;
		} else {
			return hr;
		}
	}

	bool CopyBitmapToA8L8(BYTE* pDst, int dst_pitch)
	{
		return false;
	}
};

#endif
