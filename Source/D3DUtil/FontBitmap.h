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

#include "D3DCommon.h"

#ifndef FONTBITMAP_MODE
#define FONTBITMAP_MODE 1
// 0 - GDI, 1 - GDI+, 2 - DirectWrite
#endif

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

	HRESULT Initialize(const WCHAR* fontName, const int fontHeight, DWORD fontFlags, const WCHAR* chars, UINT lenght)
	{
		DeleteObject(m_hBitmap);
		m_pBitmapBits = nullptr;
		m_charCoords.clear();

		HRESULT hr = S_OK;

		// Create a font.  By specifying ANTIALIASED_QUALITY, we might get an
		// antialiased font, but this is not guaranteed.
		int nHeight    = -(int)(fontHeight);
		DWORD dwBold   = (fontFlags & D3DFONT_BOLD)   ? FW_BOLD : FW_NORMAL;
		DWORD dwItalic = (fontFlags & D3DFONT_ITALIC) ? TRUE    : FALSE;

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

	HRESULT GetFloatCoords(FloatRect* pTexCoords, const UINT lenght)
	{
		ASSERT(pTexCoords);

		if (!m_hBitmap || !m_pBitmapBits || lenght != m_charCoords.size()) {
			return E_ABORT;
		}

		for (const auto coord : m_charCoords) {
			*pTexCoords++ = {
				(float)coord.left   / m_bmWidth,
				(float)coord.top    / m_bmHeight,
				(float)coord.right  / m_bmWidth,
				(float)coord.bottom / m_bmHeight,
			};
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

typedef CFontBitmapGDI CFontBitmap;

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

	HRESULT Initialize(const WCHAR* fontName, const int fontHeight, DWORD fontFlags, const WCHAR* chars, UINT lenght)
	{
		SAFE_DELETE(m_pBitmap);
		m_charCoords.clear();

		auto status = Gdiplus::Ok;

		Gdiplus::FontStyle fontstyle =
			((fontFlags & (D3DFONT_BOLD | D3DFONT_ITALIC)) == (D3DFONT_BOLD | D3DFONT_ITALIC)) ? Gdiplus::FontStyleBoldItalic :
			(fontFlags & D3DFONT_BOLD) ? Gdiplus::FontStyleBold :
			(fontFlags & D3DFONT_ITALIC) ? Gdiplus::FontStyleItalic :
			Gdiplus::FontStyleRegular;

		Gdiplus::FontFamily fontFamily(fontName);
		Gdiplus::Font font(&fontFamily, fontHeight, fontstyle, Gdiplus::UnitPixel);

		auto pStringFormat = Gdiplus::StringFormat::GenericTypographic();
		Gdiplus::StringFormat stringFormat(pStringFormat);
		auto flags = stringFormat.GetFormatFlags() | Gdiplus::StringFormatFlags::StringFormatFlagsMeasureTrailingSpaces;
		stringFormat.SetFormatFlags(flags);

		Gdiplus::Bitmap testBitmap(32, 32, PixelFormat32bppARGB); // bitmap dimensions are not important here
		Gdiplus::Graphics testGraphics(&testBitmap);
		testGraphics.SetTextRenderingHint(m_TextRenderingHint);

		std::vector<SIZE> charSizes;
		charSizes.reserve(lenght);
		Gdiplus::RectF rect;
		float maxWidth = 0;
		float maxHeight = 0;

		Gdiplus::PointF origin;

		for (UINT i = 0; i < lenght; i++) {
			status = testGraphics.MeasureString(&chars[i], 1, &font, origin, &stringFormat, &rect);
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
			Gdiplus::Graphics graphics(m_pBitmap);
			graphics.SetTextRenderingHint(m_TextRenderingHint);
			Gdiplus::SolidBrush brushWhite(Gdiplus::Color::White);

			m_charCoords.reserve(lenght);

			UINT idx = 0;
			for (UINT y = 0; y < lines; y++) {
				for (UINT x = 0; x < columns; x++) {
					if (idx >= lenght) {
						break;
					}
					UINT X = x * stepX + 1;
					UINT Y = y * stepY;
					status = graphics.DrawString(&chars[idx], 1, &font, Gdiplus::PointF(X, Y), &stringFormat, &brushWhite);
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
			graphics.Flush();
		}

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

	HRESULT GetFloatCoords(FloatRect* pTexCoords, const UINT lenght)
	{
		ASSERT(pTexCoords);

		if (!m_pBitmap || lenght != m_charCoords.size()) {
			return E_ABORT;
		}

		auto w = m_pBitmap->GetWidth();
		auto h = m_pBitmap->GetHeight();

		for (const auto coord : m_charCoords) {
			*pTexCoords++ = {
				(float)coord.left   / w,
				(float)coord.top    / h,
				(float)coord.right  / w,
				(float)coord.bottom / h,
			};
		}

		return S_OK;
	}

	HRESULT CopyBitmapToA8L8(BYTE* pDst, int dst_pitch)
	{
		ASSERT(pDst && dst_pitch);

		if (!m_pBitmap) {
			return E_ABORT;
		}

#if _DEBUG && DUMP_BITMAP
		SaveBitmap(L"C:\\TEMP\\font_gdiplus_bitmap.png");
#endif

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
					*pDst16++ = A8R8G8B8toA8L8(*pSrc32++);
				}
				pSrc += bitmapData.Stride;
				pDst += dst_pitch;
			}
			m_pBitmap->UnlockBits(&bitmapData);

			return S_OK;
		}

		return E_FAIL;
	}

private:
	HRESULT SaveBitmap(const WCHAR* filename)
	{
		if (!m_pBitmap) {
			return E_ABORT;
		}

		const WCHAR* mimetype = nullptr;

		if (auto ext = wcsrchr(filename, '.')) {
			// the "count" parameter for "_wcsnicmp" function must be greater than the length of the short string to be compared
			if (_wcsnicmp(ext, L".bmp", 8) == 0) {
				mimetype = L"image/bmp";
			}
			else if (_wcsnicmp(ext, L".png", 8) == 0) {
				mimetype = L"image/png";
			}
		}

		if (!mimetype) {
			return E_INVALIDARG;
		}

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
		CLSID clsidEncoder = CLSID_NULL;

		for (UINT j = 0; j < num; ++j) {
			if (wcscmp(pImageCodecInfo[j].MimeType, mimetype) == 0) {
				clsidEncoder = pImageCodecInfo[j].Clsid;
				break;
			}
		}
		free(pImageCodecInfo);
		if (clsidEncoder == CLSID_NULL) {
			return E_FAIL;
		}

		Gdiplus::Status status = m_pBitmap->Save(filename, &clsidEncoder, nullptr);

		return (Gdiplus::Ok == status) ? S_OK : E_FAIL;
	}
};

typedef CFontBitmapGDIPlus CFontBitmap;

#elif FONTBITMAP_MODE == 2

#include <dwrite.h>
#include <d2d1.h>
#include <wincodec.h>

class CFontBitmapDWrite
{
private:
	CComPtr<ID2D1Factory>       m_pD2D1Factory;
	CComPtr<IDWriteFactory>     m_pDWriteFactory;
	CComPtr<IWICImagingFactory> m_pWICFactory;

	CComPtr<IWICBitmap> m_pWICBitmap;
	std::vector<RECT> m_charCoords;

public:
	CFontBitmapDWrite()
	{
		HRESULT hr = D2D1CreateFactory(
			D2D1_FACTORY_TYPE_SINGLE_THREADED,
#ifdef _DEBUG
			{ D2D1_DEBUG_LEVEL_INFORMATION },
#endif
			&m_pD2D1Factory);

		hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(m_pDWriteFactory), reinterpret_cast<IUnknown**>(&m_pDWriteFactory));

		hr = CoCreateInstance(
			CLSID_WICImagingFactory,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_IWICImagingFactory,
			(LPVOID*)&m_pWICFactory
		);
	}

	~CFontBitmapDWrite()
	{
		m_pWICFactory.Release();
		m_pDWriteFactory.Release();
		m_pD2D1Factory.Release();
	}

	HRESULT Initialize(const WCHAR* fontName, const int fontHeight, DWORD fontFlags, const WCHAR* chars, UINT lenght)
	{
		m_pWICBitmap.Release();
		m_charCoords.clear();

		CComPtr<IDWriteTextFormat> pTextFormat;
		HRESULT hr = m_pDWriteFactory->CreateTextFormat(
			fontName,
			nullptr,
			(fontFlags & D3DFONT_BOLD)   ? DWRITE_FONT_WEIGHT_BOLD  : DWRITE_FONT_WEIGHT_NORMAL,
			(fontFlags & D3DFONT_ITALIC) ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			fontHeight,
			L"", //locale
			&pTextFormat);
		if (FAILED(hr)) {
			return hr;
		}

		std::vector<SIZE> charSizes;
		charSizes.reserve(lenght);
		DWRITE_TEXT_METRICS textMetrics;
		float maxWidth = 0;
		float maxHeight = 0;

		for (UINT i = 0; i < lenght; i++) {
			IDWriteTextLayout* pTextLayout;
			HRESULT hr = m_pDWriteFactory->CreateTextLayout(&chars[i], 1, pTextFormat, 0, 0, &pTextLayout);
			if (S_OK == hr) {
				hr = pTextLayout->GetMetrics(&textMetrics);
				pTextLayout->Release();
			}
			if (FAILED(hr)) {
				break;
			}

			SIZE size = { (LONG)ceil(textMetrics.widthIncludingTrailingWhitespace), (LONG)ceil(textMetrics.height) };
			charSizes.emplace_back(size);

			if (textMetrics.width > maxWidth) {
				maxWidth = textMetrics.width;
			}
			if (textMetrics.height > maxHeight) {
				maxHeight = textMetrics.height;
			}
			ASSERT(textMetrics.left == 0 && textMetrics.top == 0);
		}

		if (S_OK == hr) {
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

			hr = m_pWICFactory->CreateBitmap(bmWidth, bmHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &m_pWICBitmap);

			CComPtr<ID2D1RenderTarget>    pD2D1RenderTarget;
			CComPtr<ID2D1SolidColorBrush> pD2D1Brush;

			D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
				D2D1_RENDER_TARGET_TYPE_DEFAULT,
				D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN),
				96, 96);

			hr = m_pD2D1Factory->CreateWicBitmapRenderTarget(m_pWICBitmap, &props, &pD2D1RenderTarget);
			if (S_OK == hr) {
				hr = pD2D1RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pD2D1Brush);
			}

			m_charCoords.reserve(lenght);
			pD2D1RenderTarget->BeginDraw();
			UINT idx = 0;
			for (UINT y = 0; y < lines; y++) {
				for (UINT x = 0; x < columns; x++) {
					if (idx >= lenght) {
						break;
					}
					UINT X = x * stepX + 1;
					UINT Y = y * stepY;
					IDWriteTextLayout* pTextLayout;
					hr = m_pDWriteFactory->CreateTextLayout(&chars[idx], 1, pTextFormat, 0, 0, &pTextLayout);
					if (S_OK == hr) {
						pD2D1RenderTarget->DrawTextLayout({ (FLOAT)X, (FLOAT)Y }, pTextLayout, pD2D1Brush);
						pTextLayout->Release();
					}
					if (FAILED(hr)) {
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
			pD2D1RenderTarget->EndDraw();

			pD2D1Brush.Release();
			pD2D1RenderTarget.Release();
		}

		pTextFormat.Release();

		return hr;
	}

	UINT GetWidth()
	{
		if (m_pWICBitmap) {
			UINT w, h;
			if (S_OK == m_pWICBitmap->GetSize(&w, &h)) {
				return w;
			}
		}

		return 0;
	}

	UINT GetHeight()
	{
		if (m_pWICBitmap) {
			UINT w, h;
			if (S_OK == m_pWICBitmap->GetSize(&w, &h)) {
				return h;
			}
		}

		return 0;
	}

	HRESULT GetFloatCoords(FloatRect* pTexCoords, const UINT lenght)
	{
		ASSERT(pTexCoords);

		if (!m_pWICBitmap || lenght != m_charCoords.size()) {
			return E_ABORT;
		}

		UINT w, h;
		HRESULT hr = m_pWICBitmap->GetSize(&w, &h);
		if (FAILED(hr)) {
			return hr;
		}

		for (const auto coord : m_charCoords) {
			*pTexCoords++ = {
				(float)coord.left   / w,
				(float)coord.top    / h,
				(float)coord.right  / w,
				(float)coord.bottom / h,
			};
		}

		return S_OK;
	}

	HRESULT CopyBitmapToA8L8(BYTE* pDst, int dst_pitch)
	{
		ASSERT(pDst && dst_pitch);

		if (!m_pWICBitmap) {
			return E_ABORT;
		}

		UINT w, h;
		HRESULT hr = m_pWICBitmap->GetSize(&w, &h);
		if (FAILED(hr)) {
			return hr;
		}

#if _DEBUG && DUMP_BITMAP
		SaveBitmap(L"C:\\TEMP\\font_directwrite_bitmap.png");
#endif

		WICRect rcLock = { 0, 0, w, h };
		IWICBitmapLock *pLock = nullptr;
		hr = m_pWICBitmap->Lock(&rcLock, WICBitmapLockRead, &pLock);
		if (S_OK == hr) {
			UINT cbStride = 0;
			UINT cbBufferSize = 0;
			BYTE* pSrc = nullptr;
			hr = pLock->GetStride(&cbStride);
			if (S_OK == hr) {
				hr = pLock->GetDataPointer(&cbBufferSize, &pSrc);
			}

			if (S_OK == hr) {
				for (UINT y = 0; y < h; y++) {
					uint32_t* pSrc32 = (uint32_t*)pSrc;
					uint16_t* pDst16 = (uint16_t*)pDst;

					for (UINT x = 0; x < w; x++) {
						*pDst16++ = A8R8G8B8toA8L8(*pSrc32++);
					}
					pSrc += cbStride;
					pDst += dst_pitch;
				}
			}
			pLock->Release();
		}

		return hr;
	}

private:
	HRESULT SaveBitmap(const WCHAR* filename)
	{
		if (!m_pWICBitmap) {
			return E_ABORT;
		}

		UINT w, h;
		HRESULT hr = m_pWICBitmap->GetSize(&w, &h);
		if (FAILED(hr)) {
			return hr;
		}

		GUID guidContainerFormat = GUID_NULL;

		if (auto ext = wcsrchr(filename, '.')) {
			// the "count" parameter for "_wcsnicmp" function must be greater than the length of the short string to be compared
			if (_wcsnicmp(ext, L".bmp", 8) == 0) {
				guidContainerFormat = GUID_ContainerFormatBmp;
			}
			else if (_wcsnicmp(ext, L".png", 8) == 0) {
				guidContainerFormat = GUID_ContainerFormatPng;
			}
		}

		if (guidContainerFormat == GUID_NULL) {
			return E_INVALIDARG;
		}

		CComPtr<IWICStream> pStream;
		CComPtr<IWICBitmapEncoder> pEncoder;
		CComPtr<IWICBitmapFrameEncode> pFrameEncode;
		WICPixelFormatGUID format = GUID_WICPixelFormatDontCare;

		hr = m_pWICFactory->CreateStream(&pStream);
		if (SUCCEEDED(hr)) {
			hr = pStream->InitializeFromFilename(filename, GENERIC_WRITE);
		}
		if (SUCCEEDED(hr)) {
			hr = m_pWICFactory->CreateEncoder(guidContainerFormat, NULL, &pEncoder);
		}
		if (SUCCEEDED(hr)) {
			hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);
		}
		if (SUCCEEDED(hr)) {
			hr = pEncoder->CreateNewFrame(&pFrameEncode, nullptr);
		}
		if (SUCCEEDED(hr)) {
			hr = pFrameEncode->Initialize(nullptr);
		}
		if (SUCCEEDED(hr)) {
			hr = pFrameEncode->SetSize(w, h);
		}
		if (SUCCEEDED(hr)) {
			hr = pFrameEncode->SetPixelFormat(&format);
		}
		if (SUCCEEDED(hr)) {
			hr = pFrameEncode->WriteSource(m_pWICBitmap, nullptr);
		}
		if (SUCCEEDED(hr)) {
			hr = pFrameEncode->Commit();
		}
		if (SUCCEEDED(hr)) {
			hr = pEncoder->Commit();
		}

		return hr;
	}
};

typedef CFontBitmapDWrite CFontBitmap;

#endif
