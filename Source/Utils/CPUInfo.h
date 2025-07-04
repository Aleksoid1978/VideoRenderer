//
// Copyright (c) 2020-2025 v0lt, Aleksoid
//
// SPDX-License-Identifier: MIT
//

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
