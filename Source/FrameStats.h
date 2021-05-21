/*
* (C) 2018-2021 see Authors.txt
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

#include "Times.h"

#define SYNC_OFFSET_EX 0
#define TEST_TICKS 0

template<typename T, unsigned count> class CFrameTimestamps {
protected:
	unsigned m_frames = 0;
	T m_timestamps[count] = {};
	const unsigned intervals = count - 1;
	unsigned m_index = intervals;

	inline unsigned GetNextIndex(unsigned idx) {
		return (idx >= intervals) ? 0 : idx + 1;
	}

	inline unsigned GetPrevIndex(unsigned idx) {
		return (idx == 0) ? intervals : idx - 1;
	}

public:
	void Reset() {
		m_frames = 0;
		ZeroMemory(m_timestamps, sizeof(m_timestamps));
		m_index = intervals;
	};

	void Add(T timestamp) {
		m_index = GetNextIndex(m_index);
		m_timestamps[m_index] = timestamp;
		m_frames++;
	}

	REFERENCE_TIME GeTimestamp() {
		return m_timestamps[m_index];
	}

	unsigned GetFrames() {
		return m_frames;
	}

	virtual T GetAverageFrameDuration() {
		if (m_frames > intervals) {
			unsigned first_index = GetNextIndex(m_index);
			return (m_timestamps[m_index] - m_timestamps[first_index]) / intervals;
		}

		if (m_frames > 1) {
			return (m_timestamps[m_frames - 1] - m_timestamps[0]) / (m_frames - 1);
		}

		return UNITS;
	}
};


class CFrameStats : public CFrameTimestamps<REFERENCE_TIME, 301>
{
private:
	REFERENCE_TIME m_startFrameDuration = 400000;

	inline unsigned GetPrev10Index(unsigned idx) {
		if (idx < 10) {
			idx += std::size(m_timestamps);
		}
		idx -= 10;
		return idx;
	}

public:
	REFERENCE_TIME GetAverageFrameDuration() override {
		REFERENCE_TIME frame_duration;
		if (m_frames > intervals) {
			unsigned first_index = GetNextIndex(m_index);
			frame_duration =(m_timestamps[m_index] - m_timestamps[first_index]) / intervals;
		}
		else if (m_frames > 1) {
			frame_duration = (m_timestamps[m_frames - 1] - m_timestamps[0]) / (m_frames - 1);
		}
		else {
			return m_startFrameDuration;
		}

		if (m_frames > 10) {
			REFERENCE_TIME frame_duration10 = (m_timestamps[m_index] - m_timestamps[GetPrev10Index(m_index)]) / 10;
			if (abs(frame_duration - frame_duration10) > 10000) {
				frame_duration = frame_duration10;
			}
		}

		return frame_duration > 0 ? frame_duration : m_startFrameDuration;
	}

	double GetAverageFps() {
		//return (double)UNITS / GetAverageFrameDuration(); // this is the correct code, do not delete it!
		// temporary hacked output
		const auto averageFrameDuration = GetAverageFrameDuration();
		return averageFrameDuration > 0 ? (double)UNITS / averageFrameDuration : 0;
	}

	void SetStartFrameDuration(const REFERENCE_TIME startFrameDuration) {
		if (startFrameDuration > 0) {
			m_startFrameDuration = startFrameDuration;
		}
	}
};

class CDrawStats : public CFrameTimestamps<uint64_t, 31>
{
public:
	unsigned m_dropped = 0;

	void Reset() {
		CFrameTimestamps::Reset();
		m_dropped = 0;
	};

	double GetAverageFps() {
		return GetPreciseTicksPerSecond() / GetAverageFrameDuration();
	}
};

struct CRenderStats {
	//unsigned dropped1 = 0; // used m_DrawStats.m_dropped
	unsigned dropped2 = 0;
	unsigned failed = 0;
	//unsigned skipped_interval = 0;

	uint64_t copyticks = 0;
	uint64_t substicks = 0;
	uint64_t paintticks = 0;
	uint64_t presentticks = 0;
	REFERENCE_TIME syncoffset = 0;

#if TEST_TICKS
	uint64_t t1 = 0;
	uint64_t t2 = 0;
	uint64_t t3 = 0;
	uint64_t t4 = 0;
	uint64_t t5 = 0;
	uint64_t t6 = 0;
#endif

	//void NewInterval() {
	//	skipped_interval = INT_MAX; // needed for forced rendering of the first frame after start or seeking
	//}
	void Reset() {
		ZeroMemory(this, sizeof(*this));
		//NewInterval();
	}
};

template<typename T> class CMovingAverage
{
private:
	std::vector<T> fifo;
	unsigned oldestIndex = 0;
	unsigned lastIndex = 0;
	T        sum         = 0;

public:
	CMovingAverage(unsigned size) {
		size = std::max(size, 1u);
		fifo.resize(size);
	}

	void Add(T sample) {
		sum = sum + sample - fifo[oldestIndex];
		fifo[oldestIndex] = sample;
		lastIndex = oldestIndex;
		oldestIndex++;
		if (oldestIndex == fifo.size()) {
			oldestIndex = 0;
		}
	}

	T Last() {
		return fifo[lastIndex];
	}

	T Average() {
		return sum / fifo.size();
	}

	std::pair<T, T> MinMax() {
		const auto [min_e, max_e] = minmax_element(fifo.begin(), fifo.end());
		return { *min_e, *max_e };
	}

	T* Data() {
		return fifo.data();
	}

	unsigned Size() {
		return fifo.size();
	}

	unsigned OldestIndex() {
		return oldestIndex;
	}
};

