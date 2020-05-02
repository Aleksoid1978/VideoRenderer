/*
 * (C) 2018-2020 see Authors.txt
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

#ifdef _WIN64
	#pragma warning(disable:4267) // hide warning C4267: conversion from 'size_t' to 'type', possible loss of data
#endif

#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#define VC_EXTRALEAN        // Exclude rarely-used stuff from Windows headers

#ifdef _DEBUG
#define D3D_DEBUG_INFO
#endif

#include <atlbase.h>
#include <atlstr.h>
#include <atlwin.h>
#include <atltypes.h>

#include <dmodshow.h>
#include <dvdmedia.h>
#include "../BaseClasses/streams.h"
#include <VersionHelpers.h>
#include <DirectXMath.h>

#include <algorithm>
// Workaround compilation errors when including GDI+ with NOMINMAX defined
namespace Gdiplus
{
	using std::min;
	using std::max;
};
#include <numeric>
#include <vector>
