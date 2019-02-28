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

#define STATS_W 450
#define STATS_H 200

class CStatsDrawing
{
private:
	// GDI+ handling
	ULONG_PTR m_gdiplusToken;
	Gdiplus::GdiplusStartupInput m_gdiplusStartupInput;

public:
	CStatsDrawing() {
		// GDI+ handling
		Gdiplus::GdiplusStartup(&m_gdiplusToken, &m_gdiplusStartupInput, nullptr);
	}
	~CStatsDrawing() {
		// GDI+ handling
		Gdiplus::GdiplusShutdown(m_gdiplusToken);
	}

	void DrawTextW(HDC& hdc, const WCHAR* str) {
		using namespace Gdiplus;

		Graphics   graphics(hdc);
		FontFamily fontFamily(L"Consolas");
		Font       font(&fontFamily, 20, FontStyleRegular, UnitPixel);
		PointF     pointF(5.0f, 5.0f);
		SolidBrush solidBrush(Color(255, 255, 255));

		Status status = Gdiplus::Ok;

		status = graphics.Clear(Color(192, 0, 0, 0));
		status = graphics.DrawString(str, -1, &font, pointF, &solidBrush);

		Pen pen(Color(128, 255, 128), 5);
		static int col = STATS_W;
		if (--col < 0) {
			col = STATS_W;
		}
		graphics.DrawLine(&pen, col, STATS_H - 11, col, STATS_H - 1);

		graphics.Flush();
	}

};
