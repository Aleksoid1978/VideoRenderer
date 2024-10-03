/*
 * (C) 2020-2024 see Authors.txt
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

#include <Utils/Util.h>

#if !defined(NTDDI_WIN11_GE)
enum {
	DISPLAYCONFIG_DEVICE_INFO_SET_RESERVED1 = 14,
	DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2 = 15,
	DISPLAYCONFIG_DEVICE_INFO_SET_HDR_STATE = 16,
	DISPLAYCONFIG_DEVICE_INFO_SET_WCG_STATE = 17,
};
#endif

#if !defined(NTDDI_WIN11_GA) || (NTDDI_VERSION < NTDDI_WIN11_GA)
typedef enum _DISPLAYCONFIG_ADVANCED_COLOR_MODE {
	DISPLAYCONFIG_ADVANCED_COLOR_MODE_SDR,
	DISPLAYCONFIG_ADVANCED_COLOR_MODE_WCG,
	DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR
} DISPLAYCONFIG_ADVANCED_COLOR_MODE;

typedef struct _DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 {
	DISPLAYCONFIG_DEVICE_INFO_HEADER header;
	union {
		struct {
			UINT32 advancedColorSupported : 1;
			UINT32 advancedColorActive : 1;
			UINT32 reserved1 : 1;
			UINT32 advancedColorLimitedByPolicy : 1;
			UINT32 highDynamicRangeSupported : 1;
			UINT32 highDynamicRangeUserEnabled : 1;
			UINT32 wideColorSupported : 1;
			UINT32 wideColorUserEnabled : 1;
			UINT32 reserved : 24;
		};
		UINT32 value;
	};
	DISPLAYCONFIG_COLOR_ENCODING colorEncoding;
	UINT32 bitsPerColorChannel;
	DISPLAYCONFIG_ADVANCED_COLOR_MODE activeColorMode;
} DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2;

typedef struct _DISPLAYCONFIG_SET_HDR_STATE {
	DISPLAYCONFIG_DEVICE_INFO_HEADER header;
	union {
		struct {
			UINT32 enableHdr : 1;
			UINT32 reserved : 31;
		};
		UINT32 value;
	};
} DISPLAYCONFIG_SET_HDR_STATE;
#endif

struct DisplayConfig_t {
	union {
		struct {
			UINT32 advancedColorSupported : 1;     // A type of advanced color is supported
			UINT32 advancedColorEnabled : 1;       // A type of advanced color is enabled
			UINT32 wideColorEnforced : 1;          // Wide color gamut is enabled
			UINT32 advancedColorForceDisabled : 1; // Advanced color is force disabled due to system/OS policy
			UINT32 reserved : 28;
		};
		UINT32 value;
	} advancedColor;

	struct {
		union {
			struct {
				UINT32 advancedColorSupported : 1;
				UINT32 advancedColorActive : 1;
				UINT32 reserved1 : 1;
				UINT32 advancedColorLimitedByPolicy : 1;
				UINT32 highDynamicRangeSupported : 1;
				UINT32 highDynamicRangeUserEnabled : 1;
				UINT32 wideColorSupported : 1;
				UINT32 wideColorUserEnabled : 1;
				UINT32 reserved : 24;
			};
			UINT32 value;
		};

		DISPLAYCONFIG_ADVANCED_COLOR_MODE activeColorMode;
	} windows1124H2Colors;

	UINT32 width;
	UINT32 height;
	UINT32 bitsPerChannel;
	DISPLAYCONFIG_COLOR_ENCODING colorEncoding;
	DISPLAYCONFIG_RATIONAL refreshRate;
	DISPLAYCONFIG_SCANLINE_ORDERING scanLineOrdering;
	DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY outputTechnology;
	WCHAR displayName[CCHDEVICENAME];
	WCHAR monitorName[64];
	DISPLAYCONFIG_MODE_INFO modeTarget;

	bool HDRSupported() const {
		if (IsWindows11_24H2OrGreater()) {
			return windows1124H2Colors.highDynamicRangeSupported;
		}

		return advancedColor.advancedColorSupported && !advancedColor.wideColorEnforced && !advancedColor.advancedColorForceDisabled;
	}
	bool HDREnabled() const {
		if (IsWindows11_24H2OrGreater()) {
			return windows1124H2Colors.highDynamicRangeSupported && windows1124H2Colors.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE::DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR;
		}

		return advancedColor.advancedColorEnabled && !advancedColor.wideColorEnforced && !advancedColor.advancedColorForceDisabled;
	}
};

double GetRefreshRate(const wchar_t* displayName);

bool GetDisplayConfig(const wchar_t* displayName, DisplayConfig_t& displayConfig);

bool GetDisplayConfigs(std::vector<DisplayConfig_t>& displayConfigs);

const wchar_t* ColorEncodingToString(DISPLAYCONFIG_COLOR_ENCODING colorEncoding);
const wchar_t* OutputTechnologyToString(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY outputTechnology);

std::wstring DisplayConfigToString(const DisplayConfig_t& dc);

std::wstring D3DDisplayModeToString(const D3DDISPLAYMODEEX& dm);
