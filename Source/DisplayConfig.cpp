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

#include "stdafx.h"
#include "DisplayConfig.h"

double get_refresh_rate(const DISPLAYCONFIG_PATH_INFO& path, DISPLAYCONFIG_MODE_INFO* modes)
{
	double freq = 0.0;

	if (path.targetInfo.modeInfoIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID) {
		DISPLAYCONFIG_MODE_INFO* mode = &modes[path.targetInfo.modeInfoIdx];
		if (mode->infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET) {
			DISPLAYCONFIG_RATIONAL* vSyncFreq = &mode->targetMode.targetVideoSignalInfo.vSyncFreq;
			if (vSyncFreq->Denominator != 0 && vSyncFreq->Numerator / vSyncFreq->Denominator > 1) {
				freq = (double)vSyncFreq->Numerator / (double)vSyncFreq->Denominator;
			}
		}
	}

	if (freq == 0.0) {
		const DISPLAYCONFIG_RATIONAL* refreshRate = &path.targetInfo.refreshRate;
		if (refreshRate->Denominator != 0 && refreshRate->Numerator / refreshRate->Denominator > 1) {
			freq = (double)refreshRate->Numerator / (double)refreshRate->Denominator;
		}
	}

	return freq;
};


double GetRefreshRate(const wchar_t* displayName)
{
	UINT32 num_paths;
	UINT32 num_modes;
	std::vector<DISPLAYCONFIG_PATH_INFO> paths;
	std::vector<DISPLAYCONFIG_MODE_INFO> modes;
	LONG res;

	// The display configuration could change between the call to
	// GetDisplayConfigBufferSizes and the call to QueryDisplayConfig, so call
	// them in a loop until the correct buffer size is chosen
	do {
		res = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &num_paths, &num_modes);
		if (res == ERROR_SUCCESS) {
			paths.resize(num_paths);
			modes.resize(num_modes);
			res = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &num_paths, paths.data(), &num_modes, modes.data(), nullptr);
		}
	} while (res == ERROR_INSUFFICIENT_BUFFER);

	if (res == ERROR_SUCCESS) {
		// num_paths and num_modes could decrease in a loop
		paths.resize(num_paths);
		modes.resize(num_modes);

		for (const auto& path : paths) {
			// Send a GET_SOURCE_NAME request
			DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {
				{DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), path.sourceInfo.adapterId, path.sourceInfo.id}, {},
			};
			if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS) {
				if (wcscmp(displayName, source.viewGdiDeviceName) == 0) {
					return get_refresh_rate(path, modes.data());
				}
			}
		}
	}

	return 0.0;
}

static bool is_valid_refresh_rate(const DISPLAYCONFIG_RATIONAL& rr)
{
	// DisplayConfig sometimes reports a rate of 1 when the rate is not known
	return rr.Denominator != 0 && rr.Numerator / rr.Denominator > 1;
}

bool GetDisplayConfig(const wchar_t* displayName, DisplayConfig_t& displayConfig)
{
	UINT32 num_paths;
	UINT32 num_modes;
	std::vector<DISPLAYCONFIG_PATH_INFO> paths;
	std::vector<DISPLAYCONFIG_MODE_INFO> modes;
	LONG res;

	// The display configuration could change between the call to
	// GetDisplayConfigBufferSizes and the call to QueryDisplayConfig, so call
	// them in a loop until the correct buffer size is chosen
	do {
		res = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &num_paths, &num_modes);
		if (ERROR_SUCCESS == res) {
			paths.resize(num_paths);
			modes.resize(num_modes);
			res = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &num_paths, paths.data(), &num_modes, modes.data(), nullptr);
		}
	} while (ERROR_INSUFFICIENT_BUFFER == res);

	if (res == ERROR_SUCCESS) {
		// num_paths and num_modes could decrease in a loop
		paths.resize(num_paths);
		modes.resize(num_modes);

		for (const auto& path : paths) {
			// Send a GET_SOURCE_NAME request
			DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {
				{DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), path.sourceInfo.adapterId, path.sourceInfo.id}, {},
			};
			res = DisplayConfigGetDeviceInfo(&source.header);
			if (ERROR_SUCCESS == res) {
				if (wcscmp(displayName, source.viewGdiDeviceName) == 0) {
					displayConfig = {};

					if (path.sourceInfo.modeInfoIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID) {
						const auto& mode = modes[path.sourceInfo.modeInfoIdx];
						if (mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
							displayConfig.width  = mode.sourceMode.width;;
							displayConfig.height = mode.sourceMode.height;
						}
					}
					if (path.targetInfo.modeInfoIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID) {
						const auto& mode = modes[path.targetInfo.modeInfoIdx];
						if (mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET) {
							displayConfig.refreshRate      = mode.targetMode.targetVideoSignalInfo.vSyncFreq;
							displayConfig.scanLineOrdering = mode.targetMode.targetVideoSignalInfo.scanLineOrdering;
						}
						displayConfig.modeTarget = mode;

						DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color_info = {
							{DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(color_info), mode.adapterId, mode.id}, {}
						};
						res = DisplayConfigGetDeviceInfo(&color_info.header);
						if (ERROR_SUCCESS == res) {
							displayConfig.colorEncoding = color_info.colorEncoding;
							displayConfig.bitsPerChannel = color_info.bitsPerColorChannel;
							displayConfig.advancedColorValue = color_info.value;
						}
					}

					if (!is_valid_refresh_rate(displayConfig.refreshRate)) {
						displayConfig.refreshRate = path.targetInfo.refreshRate;
						displayConfig.scanLineOrdering = path.targetInfo.scanLineOrdering;
						if (!is_valid_refresh_rate(displayConfig.refreshRate)) {
							displayConfig.refreshRate = { 0, 1 };
						}
					}

					displayConfig.outputTechnology = path.targetInfo.outputTechnology;
					memcpy(displayConfig.displayName, source.viewGdiDeviceName, sizeof(displayConfig.displayName));

					DISPLAYCONFIG_TARGET_DEVICE_NAME name= {
						{DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(name), path.sourceInfo.adapterId, path.targetInfo.id}, {},
					};
					res = DisplayConfigGetDeviceInfo(&name.header);
					if (ERROR_SUCCESS == res) {
						memcpy(displayConfig.monitorName, name.monitorFriendlyDeviceName, sizeof(displayConfig.monitorName));
					} else {
						ZeroMemory(displayConfig.monitorName, sizeof(displayConfig.monitorName));
					}

					return true;
				}
			}
		}
	}

	return false;
}

bool GetDisplayConfigs(std::vector<DisplayConfig_t>& displayConfigs)
{
	UINT32 num_paths;
	UINT32 num_modes;
	std::vector<DISPLAYCONFIG_PATH_INFO> paths;
	std::vector<DISPLAYCONFIG_MODE_INFO> modes;
	LONG res;

	// The display configuration could change between the call to
	// GetDisplayConfigBufferSizes and the call to QueryDisplayConfig, so call
	// them in a loop until the correct buffer size is chosen
	do {
		res = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &num_paths, &num_modes);
		if (res == ERROR_SUCCESS) {
			paths.resize(num_paths);
			modes.resize(num_modes);
			res = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &num_paths, paths.data(), &num_modes, modes.data(), nullptr);
		}
	} while (res == ERROR_INSUFFICIENT_BUFFER);

	if (res != ERROR_SUCCESS) {
		return false;
	}

	displayConfigs.clear();

	// num_paths and num_modes could decrease in a loop
	paths.resize(num_paths);
	modes.resize(num_modes);

	for (const auto& path : paths) {
		// Send a GET_SOURCE_NAME request
		DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {
			{DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), path.sourceInfo.adapterId, path.sourceInfo.id}, {},
		};
		if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS) {
			DisplayConfig_t dc = {};
			if (path.sourceInfo.modeInfoIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID) {
				const auto& mode = modes[path.sourceInfo.modeInfoIdx];
				if (mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
					dc.width  = mode.sourceMode.width;;
					dc.height = mode.sourceMode.height;
				}
			}
			if (path.targetInfo.modeInfoIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID) {
				const auto& mode = modes[path.targetInfo.modeInfoIdx];
				if (mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET) {
					dc.refreshRate      = mode.targetMode.targetVideoSignalInfo.vSyncFreq;
					dc.scanLineOrdering = mode.targetMode.targetVideoSignalInfo.scanLineOrdering;
				}
				dc.modeTarget = mode;

				DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color_info = {
							{DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(color_info), mode.adapterId, mode.id}, {}
				};
				res = DisplayConfigGetDeviceInfo(&color_info.header);
				if (ERROR_SUCCESS == res) {
					dc.colorEncoding = color_info.colorEncoding;
					dc.bitsPerChannel = color_info.bitsPerColorChannel;
					dc.advancedColorValue = color_info.value;
				}
			}

			if (!is_valid_refresh_rate(dc.refreshRate)) {
				dc.refreshRate = path.targetInfo.refreshRate;
				dc.scanLineOrdering = path.targetInfo.scanLineOrdering;
				if (!is_valid_refresh_rate(dc.refreshRate)) {
					dc.refreshRate = { 0, 1 };
				}
			}

			dc.outputTechnology = path.targetInfo.outputTechnology;
			memcpy(dc.displayName, source.viewGdiDeviceName, sizeof(dc.displayName));

			displayConfigs.emplace_back(dc);
		}
	}

	return displayConfigs.size() > 0;
}

const wchar_t* ColorEncodingToString(DISPLAYCONFIG_COLOR_ENCODING colorEncoding)
{
	const wchar_t* str;

	switch (colorEncoding) {
	case DISPLAYCONFIG_COLOR_ENCODING_RGB:      str = L"RGB";      break;
	case DISPLAYCONFIG_COLOR_ENCODING_YCBCR444: str = L"YCbCr444"; break;
	case DISPLAYCONFIG_COLOR_ENCODING_YCBCR422: str = L"YCbCr422"; break;
	case DISPLAYCONFIG_COLOR_ENCODING_YCBCR420: str = L"YCbCr420"; break;
	default:
		str = nullptr;
	}

	return str;
}

const wchar_t* OutputTechnologyToString(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY outputTechnology)
{
	const wchar_t* str;

	switch (outputTechnology) {
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15:                 str = L"VGA";         break;
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI:                  str = L"DVI";         break;
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI:                 str = L"HDMI";        break;
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL:
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED: str = L"DisplayPort"; break;
	case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL:             str = L"Internal";    break;
	default:
		str = nullptr;
	}

	return str;
}

std::wstring DisplayConfigToString(const DisplayConfig_t& dc)
{
	std::wstring str;
	if (dc.width && dc.height && dc.refreshRate.Numerator) {
		double freq = (double)dc.refreshRate.Numerator / (double)dc.refreshRate.Denominator;
		str = fmt::format(L"{} {}x{} {:.3f}", dc.monitorName, dc.width, dc.height, freq);
		if (dc.scanLineOrdering >= DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED) {
			str += 'i';
		}
		str.append(L" Hz");
	}
	return str;
}

std::wstring D3DDisplayModeToString(const D3DDISPLAYMODEEX& dm)
{
	std::wstring str = fmt::format(L"{}x{} {}", dm.Width, dm.Height, dm.RefreshRate);
	if (dm.ScanLineOrdering == D3DSCANLINEORDERING_INTERLACED) {
		str += 'i';
	}
	str.append(L" Hz");
	return str;
}
