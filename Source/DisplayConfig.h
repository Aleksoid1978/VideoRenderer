/*
 * (C) 2020 see Authors.txt
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

struct DisplayConfig_t {
	UINT32 width;
	UINT32 height;
	UINT32 bitsPerChannel;
	DISPLAYCONFIG_COLOR_ENCODING colorEncoding;
	UINT32 advancedColorValue;
	DISPLAYCONFIG_RATIONAL                refreshRate;
	DISPLAYCONFIG_SCANLINE_ORDERING       scanLineOrdering;
	DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY outputTechnology;
	WCHAR displayName[CCHDEVICENAME];
	WCHAR monitorName[64];
	DISPLAYCONFIG_MODE_INFO modeTarget;
};

double GetRefreshRate(const wchar_t* displayName);

bool GetDisplayConfig(const wchar_t* displayName, DisplayConfig_t& displayConfig);

bool GetDisplayConfigs(std::vector<DisplayConfig_t>& displayConfigs);

std::wstring DisplayConfigToString(const DisplayConfig_t& dc);

std::wstring D3DDisplayModeToString(const D3DDISPLAYMODEEX& dm);
