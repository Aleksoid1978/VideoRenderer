// Copyright (c) 2020-2024 v0lt, Aleksoid
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

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
