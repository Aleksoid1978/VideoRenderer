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
	unsigned m_frames = 0;
	REFERENCE_TIME m_times[30] = {};
	unsigned m_index = 0;

public:
	void Reset() {
		m_frames = 0;
		ZeroMemory(m_times, sizeof(m_times));
		m_index = 0;
	};

	void Add(REFERENCE_TIME time) {
		m_times[m_index] = time;
		m_index++;
		if (m_index >= _countof(m_times)) {
			m_index = 0;
		}
		m_frames++;
	}

	unsigned GetFrames() { return m_frames; }

	double GetAverageFps() {
		if (m_frames >= _countof(m_times)) {
			unsigned last_index = ((m_index == 0) ? _countof(m_times) : m_index) - 1;
			return (double)(UNITS * (_countof(m_times) - 1)) / (m_times[last_index] - m_times[m_index]);
		}

		if (m_frames > 1) {
			return (double)(UNITS * (m_frames - 1)) / (m_times[m_frames - 1] - m_times[0]);
		}

		return 0.0;
	}
};
