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

#include "common/textconsole.h"

#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

void CryOmni3DEngine_Egypt::setupSprites() {
	for (Common::Array<EgyptInterfaceSprite *>::iterator it = _interfaceSprites.begin();
	     it != _interfaceSprites.end(); ++it) {
		delete *it;
	}
	_interfaceSprites.clear();

	if (!loadInterfaceSprites(Common::Path("SPRITE/INTERFAC.SPR"))) {
		warning("Egypt: failed to load interface sprites from INTERFAC.SPR");
		return;
	}

	warning("Egypt: loaded %u interface sprite(s) from INTERFAC.SPR", _interfaceSprites.size());
	if (!_interfaceSprites.empty())
		setInterfaceCursor(kEgyptCursorDefault);
}

uint CryOmni3DEngine_Egypt::getCursorFrameForZone(const EgyptZone &zone) const {
	switch (zone.actionId) {
	case 1:
		return kEgyptCursorTalk;
	case 2:
	case 4:
	case 5:
		return kEgyptCursorUse;
	case 3:
		return kEgyptCursorLook;
	case 6:
		return kEgyptCursorVisit;
	case 7:
		return kEgyptCursorVisit;
	case 9:
	case 10:
		return kEgyptCursorWarpLabel;
	case 8:
	default:
		return kEgyptCursorDefault;
	}
}

uint CryOmni3DEngine_Egypt::getDefaultCursorFrame() const {
	const int heldObjectId = getScriptVariableValue("main");
	if (heldObjectId != 0)
		return getCursorFrameForHeldObject(heldObjectId, false);

	return kEgyptCursorDefault;
}

uint CryOmni3DEngine_Egypt::getCursorFrameForHeldObject(int heldObjectId, bool variant) const {
	if (heldObjectId <= 0)
		return kEgyptCursorDefault;

	int frame = heldObjectId + 0x20;
	if (variant)
		frame += 0x39;

	if (frame < 0 || (uint)frame >= _interfaceSprites.size()) {
		warning("Egypt: invalid interface cursor frame %d for held object %d",
		        frame, heldObjectId);
		return kEgyptCursorDefault;
	}

	return (uint)frame;
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
