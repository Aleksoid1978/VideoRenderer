/*
* (C) 2018 see Authors.txt
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
#include "Time.h"

// code from VirtualDub\system\source\time.cpp

uint64_t GetPreciseTick()
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return li.QuadPart;
}

namespace {
	uint64_t GetPreciseTicksPerSecondNowI()
	{
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		return freq.QuadPart;
	}

	double GetPreciseTicksPerSecondNow()
	{
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		return (double)freq.QuadPart;
	}
}

uint64_t GetPreciseTicksPerSecondI()
{
	static uint64_t ticksPerSecond = GetPreciseTicksPerSecondNowI();

	return ticksPerSecond;
}

double GetPreciseTicksPerSecond()
{
	static double ticksPerSecond = GetPreciseTicksPerSecondNow();

	return ticksPerSecond;
}

double GetPreciseSecondsPerTick()
{
	static double secondsPerTick = 1.0 / GetPreciseTicksPerSecondNow();

	return secondsPerTick;
}
