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

#ifndef CRYOMNI3D_EGYPT_WARP_H
#define CRYOMNI3D_EGYPT_WARP_H

#include "common/str.h"

namespace CryOmni3D {
namespace Egypt {

struct EgyptWarpRequest {
	bool active = false;
	Common::String fromScene;
	Common::String fromContext;
	Common::String toScene;
	Common::String toContext;
	Common::String hnmName;
	Common::String zoneCommand;
	Common::String zoneParam;
	Common::String zoneExtra;
	uint zoneId = 0;
	uint zoneclic = 0;
	bool viaHnm = false;
	bool sourceOrientationAvailable = false;
	double sourceAlpha = 0.0;
	double sourceBeta = 0.0;
};

struct EgyptResolvedCentrage {
	bool matched = false;
	double rawFinalAlpha = 0.0;
	double normalizedFinalAlpha = 0.0;
	double finalBeta = 0.0;
};

bool isEgyptContextName(const Common::String &name);

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
