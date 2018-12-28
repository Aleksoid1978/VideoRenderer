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


class CFrameStats
{
private:
	unsigned m_frames = 0;
	REFERENCE_TIME m_times[301] = {};
	const unsigned intervals = std::size(m_times) - 1;
	unsigned m_index = intervals;

	inline unsigned GetNextIndex(unsigned idx) {
		return (idx >= intervals) ? 0 : idx + 1;
	}

	inline unsigned GetPrevIndex(unsigned idx) {
		return (idx == 0) ? intervals : idx - 1;
	}

	inline unsigned GetPrev10Index(unsigned idx) {
		if (idx < 10) {
			idx += std::size(m_times);
		}
		idx -= 10;
		return idx;
	}

public:
	void Reset() {
		m_frames = 0;
		ZeroMemory(m_times, sizeof(m_times));
		m_index = intervals;
	};

	void Add(REFERENCE_TIME time) {
		m_index = GetNextIndex(m_index);
		m_times[m_index] = time;
		m_frames++;
	}

	REFERENCE_TIME GetTime() {
		return m_times[m_index];
	}

	unsigned GetFrames() { return m_frames; }

	REFERENCE_TIME GetAverageFrameDuration() {
		REFERENCE_TIME frame_duration;
		if (m_frames >= std::size(m_times)) {
			unsigned first_index = GetNextIndex(m_index);
			frame_duration =(m_times[m_index] - m_times[first_index]) / intervals;
		}
		else if (m_frames > 1) {
			frame_duration = (m_times[m_frames - 1] - m_times[0]) / (m_frames - 1);
		}
		else {
			return UNITS;
		}

		if (m_frames > 10) {
			REFERENCE_TIME frame_duration10 = (m_times[m_index] - m_times[GetPrev10Index(m_index)]) / 10;
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


struct CRenderStats {
	unsigned skipped1 = 0;
	unsigned skipped2 = 0;
	unsigned failed = 0;
	unsigned skipped_interval = 0;

	uint64_t copyticks = 0;
	uint64_t renderticks = 0;
	REFERENCE_TIME syncoffset = 0;

	uint64_t copy1 = 0;
	uint64_t copy2 = 0;
	uint64_t copy3 = 0;

	void NewInterval() {
		skipped_interval = INT_MAX; // needed for forced rendering of the first frame after start or seeking
	}
	void Reset() {
		ZeroMemory(this, sizeof(this));
		NewInterval();
	}
};
