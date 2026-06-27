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

#ifndef CRYOMNI3D_EGYPT_DIALOGUE_H
#define CRYOMNI3D_EGYPT_DIALOGUE_H

#include "common/array.h"
#include "common/str.h"

namespace CryOmni3D {
namespace Egypt {

struct EgyptDialogNode {
	Common::String label;
	Common::String text;
	Common::Array<Common::String> commands;
};

struct EgyptDialogChoice {
	Common::String targetLabel;
	Common::String displayText;
};

// Outcome produced by executing a single dialogue node's commands.
// The caller displays node.text (if non-empty) then acts on this.
enum EgyptDialogResult {
	kDlgEnd,      // end command — close dialogue
	kDlgJump,     // goto single label  (outNextLabel is set)
	kDlgChoices,  // goto with Ramose labels (outChoices is populated)
	kDlgShow      // show command — for future use (treated as end for now)
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
