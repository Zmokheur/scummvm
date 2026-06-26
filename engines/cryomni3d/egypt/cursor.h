/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef CRYOMNI3D_EGYPT_CURSOR_H
#define CRYOMNI3D_EGYPT_CURSOR_H

#include "common/array.h"
#include "graphics/managed_surface.h"

namespace CryOmni3D {
namespace Egypt {

enum EgyptCursorFrame {
	kEgyptCursorNav0 = 0,
	kEgyptCursorNav1 = 1,
	kEgyptCursorNav2 = 2,
	kEgyptCursorNav3 = 3,
	kEgyptCursorNav4 = 4,
	kEgyptCursorNav5 = 5,
	kEgyptCursorNav6 = 6,
	kEgyptCursorNav7 = 7,
	kEgyptCursorTalk = 8,
	kEgyptCursorUse = 9,
	kEgyptCursorLook = 10,
	kEgyptCursorWarpLabel = 11,
	kEgyptCursorVisit = 12,
	kEgyptCursorDefault = 13
};

static const byte kCursorMaskTransparent = 0;
static const byte kCursorMaskOpaque      = 1;

struct EgyptInterfaceSprite {
	Graphics::ManagedSurface surface;
	Common::Array<byte> mask;
	int hotspotX = 0;
	int hotspotY = 0;
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
