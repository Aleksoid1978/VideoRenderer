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

#include <gdiplus.h>
#include <dwrite.h>
#include <d2d1.h>

// CStatsDrawingGdiplus

class CStatsDrawingGdiplus
{
private:
	const int m_Width;
	const int m_Height;

	// GDI+ handling
	ULONG_PTR m_gdiplusToken;
	Gdiplus::GdiplusStartupInput m_gdiplusStartupInput;

	Gdiplus::Bitmap*     m_bitmap;
	Gdiplus::Graphics*   m_graphics;
	Gdiplus::FontFamily* m_fontFamily;
	Gdiplus::Font*       m_font;
	Gdiplus::SolidBrush* m_solidBrushText;
	Gdiplus::SolidBrush* m_solidBrush;

public:
	CStatsDrawingGdiplus(int width, int height)
		: m_Width(width)
		, m_Height(height)
	{
		using namespace Gdiplus;
		// GDI+ handling
		GdiplusStartup(&m_gdiplusToken, &m_gdiplusStartupInput, nullptr);

		m_bitmap = new Bitmap(m_Width, m_Height, PixelFormat32bppARGB);
		m_graphics = new Graphics(m_bitmap);

		m_fontFamily = new FontFamily(L"Consolas");
		m_font = new Font(m_fontFamily, 14, FontStyleRegular, UnitPixel);
		m_solidBrushText = new SolidBrush(Color(255, 255, 255));
		m_solidBrush = new SolidBrush(Color(128, 255, 128));
	}
	~CStatsDrawingGdiplus() {
		SAFE_DELETE(m_solidBrushText);
		SAFE_DELETE(m_solidBrush);
		SAFE_DELETE(m_font);
		SAFE_DELETE(m_fontFamily);
		SAFE_DELETE(m_graphics);
		SAFE_DELETE(m_bitmap);

		// GDI+ handling
		Gdiplus::GdiplusShutdown(m_gdiplusToken);
	}

	void DrawTextW(BYTE* dst, int dst_pitch, const WCHAR* str) {
		using namespace Gdiplus;

		Status status = Gdiplus::Ok;
		status = m_graphics->Clear(Color(192, 0, 0, 0));
		status = m_graphics->DrawString(str, -1, m_font, { 5.0f, 5.0f }, m_solidBrushText);

		static int col = m_Width;
		if (--col < 0) {
			col = m_Width;
		}
		status = m_graphics->FillRectangle(m_solidBrush, col, m_Height - 11, 5, 10);

		m_graphics->Flush();

		BitmapData bitmapData;
		Rect rc(0, 0, m_Width, m_Height);
		if (Ok == m_bitmap->LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &bitmapData)) {
			CopyFrameAsIs(bitmapData.Height, dst, dst_pitch, (BYTE*)bitmapData.Scan0, bitmapData.Stride);
			m_bitmap->UnlockBits(&bitmapData);
		}
	}
};

// CStatsDrawingDWrite

class CStatsDrawingDWrite
{
private:
	const int m_Width;
	const int m_Height;

	CComPtr<IDWriteFactory>    m_pDWriteFactory;
	CComPtr<IDWriteTextFormat> m_pTextFormat;

	CComPtr<ID2D1Factory>         m_pD2D1Factory;
	CComPtr<ID2D1RenderTarget>    m_pD2D1RenderTarget;
	CComPtr<ID2D1SolidColorBrush> m_pD2D1Brush;

public:
	CStatsDrawingDWrite(int width, int height)
		: m_Width(width)
		, m_Height(height)
	{
		D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
		options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
		HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, &m_pD2D1Factory);

		hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(m_pDWriteFactory), reinterpret_cast<IUnknown**>(&m_pDWriteFactory));
		if (S_OK == hr) {
			hr = m_pDWriteFactory->CreateTextFormat(
				L"Consolas",
				nullptr,
				DWRITE_FONT_WEIGHT_NORMAL,
				DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL,
				14,
				L"", //locale
				&m_pTextFormat);
		}
	}

	~CStatsDrawingDWrite()
	{
		m_pTextFormat.Release();
		m_pDWriteFactory.Release();

		ReleaseRenderTarget();
		m_pD2D1Factory.Release();
	}

	HRESULT SetRenderTarget(IDXGISurface* pDxgiSurface)
	{
		ReleaseRenderTarget();
		HRESULT hr = E_ABORT;

		if (m_pD2D1Factory) {
			D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
				D2D1_RENDER_TARGET_TYPE_DEFAULT,
				D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
				96, 96);

			hr = m_pD2D1Factory->CreateDxgiSurfaceRenderTarget(pDxgiSurface, &props, &m_pD2D1RenderTarget);
			if (S_OK == hr) {
				hr = m_pD2D1RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_pD2D1Brush);
			}
		}

		return hr;
	}

	void ReleaseRenderTarget()
	{
		m_pD2D1Brush.Release();
		m_pD2D1RenderTarget.Release();
	}

	void DrawTextW(const CStringW& str)
	{
		if (!m_pD2D1RenderTarget) {
			return;
		}

		CComPtr<IDWriteTextLayout> pTextLayout;
		if (S_OK == m_pDWriteFactory->CreateTextLayout(str, str.GetLength(), m_pTextFormat, m_Width - 5, m_Height - 5, &pTextLayout)) {
			m_pD2D1RenderTarget->BeginDraw();
			m_pD2D1RenderTarget->DrawTextLayout(D2D1::Point2F(5.0f, 5.0f), pTextLayout, m_pD2D1Brush);
			static int col = m_Width;
			if (--col < 0) {
				col = m_Width;
			}
			D2D1_RECT_F rect = { col, m_Height - 11, col + 5, m_Height - 1 };
			m_pD2D1RenderTarget->FillRectangle(rect, m_pD2D1Brush);
			m_pD2D1RenderTarget->EndDraw();
		}



		//BitmapData bitmapData;
		//Rect rc(0, 0, STATS_W, STATS_H);
		//if (Ok == m_bitmap->LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &bitmapData)) {
		//	CopyFrameAsIs(bitmapData.Height, dst, dst_pitch, (BYTE*)bitmapData.Scan0, bitmapData.Stride);
		//	m_bitmap->UnlockBits(&bitmapData);
		//}
	}
};
