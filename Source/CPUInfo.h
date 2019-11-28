/*
 * (C) 2016-2019 see Authors.txt
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

namespace CPUInfo {
	enum PROCESSOR_TYPE {
		PROCESSOR_UNKNOWN = 0,
		PROCESSOR_INTEL,
		PROCESSOR_AMD,
	};

	enum PROCESSOR_FEATURES {
		CPU_SSE   = (1 << 0),
		CPU_SSE2  = (1 << 1),
		CPU_SSE3  = (1 << 2),
		CPU_SSSE3 = (1 << 3),
		CPU_SSE41 = (1 << 4),
		CPU_SSE42 = (1 << 5),
		CPU_AVX   = (1 << 6),
		CPU_AVX2  = (1 << 7),
	};

	const int GetType();
	const int GetFeatures();
	const DWORD GetProcessorNumber();

	const bool HaveSSSE3();
	const bool HaveSSE41();
	const bool HaveSSE42();
	const bool HaveAVX();
	const bool HaveAVX2();
} // namespace CPUInfo
