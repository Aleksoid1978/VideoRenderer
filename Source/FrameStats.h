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

#pragma once

#include "Time.h"

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
			return UNITS;
		}

		if (m_frames > 10) {
			REFERENCE_TIME frame_duration10 = (m_timestamps[m_index] - m_timestamps[GetPrev10Index(m_index)]) / 10;
			if (abs(frame_duration - frame_duration10) > 10000) {
				frame_duration = frame_duration10;
			}
		}

		return frame_duration;
	}

	double GetAverageFps() {
		return (double)UNITS / GetAverageFrameDuration();
	}
};

class CDrawStats : public CFrameTimestamps<uint64_t, 31>
{
public:
	unsigned m_dropped = 0;

	void Reset() {
		CFrameTimestamps::Reset();
		m_dropped = 0;;
	};

	double GetAverageFps() {
		return GetPreciseTicksPerSecond() / GetAverageFrameDuration();
	}
};

struct CRenderStats {
	//unsigned dropped1 = 0;
	unsigned dropped2 = 0;
	unsigned failed = 0;
	//unsigned skipped_interval = 0;

	uint64_t copyticks = 0;
	uint64_t renderticks = 0;
	REFERENCE_TIME syncoffset = 0;

	uint64_t copy1 = 0;
	uint64_t copy2 = 0;
	uint64_t copy3 = 0;

	//void NewInterval() {
	//	skipped_interval = INT_MAX; // needed for forced rendering of the first frame after start or seeking
	//}
	void Reset() {
		ZeroMemory(this, sizeof(this));
		//NewInterval();
	}
};
