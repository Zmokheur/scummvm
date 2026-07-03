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

#ifndef CRYOMNI3D_EGYPT_SUPPORT_IMAGE_LOADER_H
#define CRYOMNI3D_EGYPT_SUPPORT_IMAGE_LOADER_H

#include "common/array.h"

namespace Common {
class Path;
}

namespace Graphics {
class ManagedSurface;
}

namespace CryOmni3D {
namespace Egypt {

// Egypt assets are optionally wrapped in a CPx5 compression container.
// These helpers centralize the "sniff CPx5 magic, decompress, else use the
// raw file" logic shared by menus, dialogues, documentation and panoramas.

// Loads a file into data, transparently decompressing a CPx5 container.
bool loadFileMaybeCpx5(const Common::Path &path, Common::Array<byte> &data);

// Loads a TGA image (optionally CPx5-wrapped) and converts it to the
// current screen format. With stretchToScreen the result is a full
// 640x480 surface (image stretched, background cleared to black);
// otherwise the surface keeps the image's own dimensions.
bool loadTgaImage(const Common::Path &path, Graphics::ManagedSurface &surface, bool stretchToScreen);

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
