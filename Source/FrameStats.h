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
	REFERENCE_TIME m_times[30] = {};
	unsigned m_index = _countof(m_times) - 1;

	inline unsigned GetNextIndex(unsigned idx) {
		if (idx >= _countof(m_times) - 1) {
			return 0;
		}
		return idx + 1;
	}

public:
	void Reset() {
		m_frames = 0;
		ZeroMemory(m_times, sizeof(m_times));
		m_index = _countof(m_times) - 1;
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

	double GetAverageFps() {
		if (m_frames >= _countof(m_times)) {
			unsigned first_index = GetNextIndex(m_index);
			return (double)(UNITS * (_countof(m_times) - 1)) / (m_times[m_index] - m_times[first_index]);
		}

		if (m_frames > 1) {
			return (double)(UNITS * (m_frames - 1)) / (m_times[m_frames - 1] - m_times[0]);
		}

		return 0.0;
	}
};
