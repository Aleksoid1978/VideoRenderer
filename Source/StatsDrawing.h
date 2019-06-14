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

#define STATS_X  10
#define STATS_Y  10
#define STATS_W 512
#define STATS_H 240

class CStatsDrawing
{
private:
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
	CStatsDrawing() {
		using namespace Gdiplus;
		// GDI+ handling
		GdiplusStartup(&m_gdiplusToken, &m_gdiplusStartupInput, nullptr);

		m_bitmap = new Bitmap(STATS_W, STATS_H, PixelFormat32bppARGB);
		m_graphics = new Graphics(m_bitmap);

		m_fontFamily = new FontFamily(L"Consolas");
		m_font = new Font(m_fontFamily, 16, FontStyleRegular, UnitPixel);
		m_solidBrushText = new SolidBrush(Color(255, 255, 255));
		m_solidBrush = new SolidBrush(Color(128, 255, 128));
	}
	~CStatsDrawing() {
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

		static int col = STATS_W;
		if (--col < 0) {
			col = STATS_W;
		}
		status = m_graphics->FillRectangle(m_solidBrush, col, STATS_H - 11, 5, 10);

		m_graphics->Flush();

		BitmapData bitmapData;
		Rect rc(0, 0, STATS_W, STATS_H);
		if (Ok == m_bitmap->LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &bitmapData)) {
			CopyFrameAsIs(bitmapData.Height, dst, dst_pitch, (BYTE*)bitmapData.Scan0, bitmapData.Stride);
			m_bitmap->UnlockBits(&bitmapData);
		}
	}
};
