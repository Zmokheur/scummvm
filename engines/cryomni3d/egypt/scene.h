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

#ifndef CRYOMNI3D_EGYPT_SCENE_H
#define CRYOMNI3D_EGYPT_SCENE_H

#include "common/array.h"
#include "common/str.h"

namespace CryOmni3D {
namespace Egypt {

struct EgyptZone {
	uint id = 0;
	uint left = 0;
	uint top = 0;
	uint right = 0;
	uint bottom = 0;
	uint actionId = 0;
	Common::String commandName;
	Common::String command;
	Common::String param;
	Common::String label;
	Common::String extraParam;
	Common::String targetWarp;
};

struct EgyptScene {
	Common::String name;
	Common::String warpName;
	Common::String contextName;
	Common::Array<EgyptZone> zones;
	Common::Array<Common::String> scriptLines;
	Common::Array<uint> activeZones;
	bool hasWarpInit = false;
	bool hasEndInit = false;
	bool hasEndWarp = false;
};

struct EgyptCentrage {
	Common::String name;
	char op = '\0';
	double alpha = 0.0;
	bool hasBeta = false;
	double beta = 0.0;
};

struct EgyptWarpHeader {
	Common::String tag;
	uint16 width = 0;
	uint16 height = 0;
	byte audioFlags = 0;
	byte bpp = 0;
	uint32 frameSize = 0;
	Common::String firstChunkTag;
	uint32 firstChunkSize = 0;
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
