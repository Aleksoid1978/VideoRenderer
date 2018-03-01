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

#include <d3d9.h>
#define DbgLog(_x_) DbgLogInfo _x_

#ifndef FCC
#define FCC(ch4) ((((DWORD)(ch4) & 0xFF) << 24) |     \
                  (((DWORD)(ch4) & 0xFF00) << 8) |    \
                  (((DWORD)(ch4) & 0xFF0000) >> 8) |  \
                  (((DWORD)(ch4) & 0xFF000000) >> 24))
#endif

#if _DEBUG
#define DLog(...) DbgLogInfo(LOG_TRACE, 3, __VA_ARGS__)
#define DLogIf(f,...) {if (f) DbgLogInfo(LOG_TRACE, 3, __VA_ARGS__);}
#define DLogError(...) DbgLogInfo(LOG_ERROR, 3, __VA_ARGS__)
#else
#define DLog(...) __noop
#define DLogIf(f,...) __noop
#define DLogError(...) __noop
#endif

const wchar_t* D3DFormatToString(D3DFORMAT format);
