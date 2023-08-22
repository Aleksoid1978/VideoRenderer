/*
 * (C) 2016-2023 see Authors.txt
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
#include <intrin.h>
#include "CPUInfo.h"



static int nCPUType = CPUInfo::PROCESSOR_UNKNOWN;

#ifdef _WIN32
//  Windows
#define cpuid(info, x)    __cpuidex(info, x, 0)
#else
//  GCC Intrinsics
#include <cpuid.h>
void cpuid(int info[4], int InfoType) {
	__cpuid_count(InfoType, 0, info[0], info[1], info[2], info[3]);
}
#endif

static int GetCPUInfo()
{
	// based on
	// https://stackoverflow.com/a/7495023
	// https://gist.github.com/hi2p-perim/7855506
	// https://github.com/FFmpeg/FFmpeg/blob/master/libavutil/x86/cpu.c

	// SSE2 is a basic instruction set for modern compilers and Windows.
	// MMX is no longer relevant.
	// 3DNow! is not relevant and is not supported by modern processors.
	// XOP is not supported by modern processors.

	//  SIMD: 128-bit
	bool HW_SSE   = false;
	bool HW_SSE2  = false;
	bool HW_SSE3  = false;
	bool HW_SSSE3 = false;
	bool HW_SSE41 = false;
	bool HW_SSE42 = false;
	bool HW_SSE4a = false;
	bool HW_AES   = false;

	//  SIMD: 256-bit
	bool HW_AVX   = false;
	bool HW_XOP   = false;
	bool HW_FMA3  = false;
	bool HW_FMA4  = false;
	bool HW_AVX2  = false;

	int info[4];
	cpuid(info, 0);
	int nIds = info[0];

	{
		__declspec(align(4)) char vendor[0x20] = {};
		*reinterpret_cast<int*>(vendor) = info[1];
		*reinterpret_cast<int*>(vendor + 4) = info[3];
		*reinterpret_cast<int*>(vendor + 8) = info[2];

		if (strcmp(vendor, "GenuineIntel") == 0) {
			nCPUType = CPUInfo::PROCESSOR_INTEL;
		} else if (strcmp(vendor, "AuthenticAMD") == 0) {
			nCPUType = CPUInfo::PROCESSOR_AMD;
		}
	}

	cpuid(info, 0x80000000);
	unsigned nExIds = info[0];

	//  Detect Features
	if (nIds >= 0x00000001) {
		cpuid(info, 0x00000001);
		HW_SSE  = (info[3] & ((int)1 << 25)) != 0;
		HW_SSE2 = (info[3] & ((int)1 << 26)) != 0;
		HW_SSE3 = (info[2] & ((int)1 <<  0)) != 0;

		HW_SSSE3 = (info[2] & ((int)1 <<  9)) != 0;
		HW_SSE41 = (info[2] & ((int)1 << 19)) != 0;
		HW_SSE42 = (info[2] & ((int)1 << 20)) != 0;
		HW_AES   = (info[2] & ((int)1 << 25)) != 0;

		bool osUsesXSAVE_XRSTORE = (info[2] & ((int)1 << 27)) != 0;
		bool cpuAVXSuport = (info[2] & ((int)1 << 28)) != 0;

		if (osUsesXSAVE_XRSTORE && cpuAVXSuport) {
			unsigned __int64 xcrFeatureMask = _xgetbv(_XCR_XFEATURE_ENABLED_MASK);
			HW_AVX  = (xcrFeatureMask & 0x6) == 0x6;
			HW_FMA3 = (info[2] & ((int)1 << 12)) != 0;
		}
	}
	if (nIds >= 0x00000007) {
		cpuid(info, 0x00000007);
		if (HW_AVX) {
			HW_AVX2 = (info[1] & ((int)1 << 5)) != 0;
		}
	}
	if (nExIds >= 0x80000001) {
		cpuid(info, 0x80000001);
		HW_SSE4a = (info[2] & ((int)1 << 6)) != 0;
		if (HW_AVX) {
			HW_FMA4 = (info[2] & ((int)1 << 16)) != 0;
			HW_XOP  = (info[2] & ((int)1 << 11)) != 0;
		}
	}

	int features = 0;
	if (HW_SSE)   {features |= CPUInfo::CPU_SSE;  }
	if (HW_SSE2)  {features |= CPUInfo::CPU_SSE2; }
	if (HW_SSE3)  {features |= CPUInfo::CPU_SSE3; }
	if (HW_SSSE3) {features |= CPUInfo::CPU_SSSE3;}
	if (HW_SSE41) {features |= CPUInfo::CPU_SSE41;}
	if (HW_SSE42) {features |= CPUInfo::CPU_SSE42;}
	if (HW_AVX)   {features |= CPUInfo::CPU_AVX;  }
	if (HW_AVX2)  {features |= CPUInfo::CPU_AVX2; }

	return features;
}

static const int nCPUFeatures = GetCPUInfo();
static const bool bSSSE3 = !!(nCPUFeatures & CPUInfo::CPU_SSSE3);
static const bool bSSE41 = !!(nCPUFeatures & CPUInfo::CPU_SSE41);
static const bool bSSE42 = !!(nCPUFeatures & CPUInfo::CPU_SSE42);
static const bool bAVX   = !!(nCPUFeatures & CPUInfo::CPU_AVX2);
static const bool bAVX2  = !!(nCPUFeatures & CPUInfo::CPU_AVX2);

static DWORD GetProcessorNumber()
{
	SYSTEM_INFO SystemInfo = { 0 };
	GetSystemInfo(&SystemInfo);

	return SystemInfo.dwNumberOfProcessors;
}
static const DWORD dwNumberOfProcessors = GetProcessorNumber();

namespace CPUInfo {
	const int GetType()              { return nCPUType; }
	const int GetFeatures()          { return nCPUFeatures; }
	const DWORD GetProcessorNumber() { return dwNumberOfProcessors; }

	const bool HaveSSSE3()           { return bSSSE3; }
	const bool HaveSSE41()           { return bSSE41; }
	const bool HaveSSE42()           { return bSSE42; }
	const bool HaveAVX()             { return bAVX;   }
	const bool HaveAVX2()            { return bAVX2;  }
} // namespace CPUInfo
