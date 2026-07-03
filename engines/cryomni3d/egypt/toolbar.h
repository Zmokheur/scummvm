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

#ifndef CRYOMNI3D_EGYPT_TOOLBAR_H
#define CRYOMNI3D_EGYPT_TOOLBAR_H

namespace Graphics {
struct Surface;
}

namespace CryOmni3D {
namespace Egypt {

class CryOmni3DEngine_Egypt;

// Bottom toolbar (inventory slots, eye/documentation/options buttons).
// Runs its own modal event loop; layout and behaviour follow the original
// EXE (functions 0x808080/0x808990, hit tests 0x819f60).
// Works directly on the engine's state (friend), like the Versailles
// dialogs manager does.
class Egypt_Toolbar {
public:
	explicit Egypt_Toolbar(CryOmni3DEngine_Egypt *engine) : _engine(engine) {}

	// Slides the toolbar in, runs the interaction loop, slides it out.
	// Returns true when the toolbar queued a navigation (F1-F6 slot or eye warp).
	bool display(const Graphics::Surface *original);

private:
	CryOmni3DEngine_Egypt *_engine;
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
