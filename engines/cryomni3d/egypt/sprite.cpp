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

#include "common/endian.h"
#include "common/file.h"
#include "common/textconsole.h"

#include "cryomni3d/egypt/engine.h"
#include "cryomni3d/image/cpx5.h"

#include "graphics/cursorman.h"

namespace CryOmni3D {
namespace Egypt {

namespace {

static const Graphics::PixelFormat kEgyptSpriteFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);

} // End of anonymous namespace

bool CryOmni3DEngine_Egypt::loadInterfaceSprites(const Common::Path &filename) {
	Common::File file;
	if (!file.open(filename)) {
		warning("Egypt: failed to open interface sprite file %s",
		        filename.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	Common::Array<byte> decompressed;
	if (!Image::Cpx5Decoder::decompress(file, decompressed))
		return false;

	if (decompressed.size() < 4) {
		warning("Egypt: decompressed interface sprite data is too short");
		return false;
	}

	const uint32 firstPixelOffset = READ_LE_UINT32(decompressed.data());
	if (firstPixelOffset == 0 || (firstPixelOffset % 8) != 0 || firstPixelOffset > decompressed.size()) {
		warning("Egypt: invalid interface sprite table offset 0x%08x", firstPixelOffset);
		return false;
	}

	const uint spriteCount = firstPixelOffset / 8;
	for (uint i = 0; i < spriteCount; ++i) {
		const uint entryOffset = i * 8;
		const uint32 pixelOffset = READ_LE_UINT32(decompressed.data() + entryOffset);
		// SPR table stores [height][width] at offsets +4/+6 (confirmed from EXE 0x819b60:
		// SECOND_FIELD at +6 is used as row stride, outer loop runs FIRST_FIELD times).
		const uint16 height = READ_LE_UINT16(decompressed.data() + entryOffset + 4);
		const uint16 width  = READ_LE_UINT16(decompressed.data() + entryOffset + 6);
		const uint32 pixelDataSize = (uint32)width * (uint32)height * 2;

		if (width == 0 || height == 0 || pixelOffset + pixelDataSize > decompressed.size()) {
			warning("Egypt: invalid interface sprite %u offset=0x%08x size=%ux%u",
			        i, pixelOffset, width, height);
			return false;
		}

		EgyptInterfaceSprite *sprite = new EgyptInterfaceSprite();
		sprite->surface.create(width, height, kEgyptSpriteFormat);
		sprite->hotspotX = width / 2;
		sprite->hotspotY = height / 2;
		memcpy(sprite->surface.getPixels(), decompressed.data() + pixelOffset, pixelDataSize);

		sprite->mask.resize(width * height);
		for (uint pixel = 0; pixel < width * height; ++pixel) {
			const uint16 color = READ_LE_UINT16(decompressed.data() + pixelOffset + pixel * 2);
			sprite->mask[pixel] = (color == 0) ? kCursorMaskTransparent : kCursorMaskOpaque;
		}

		_interfaceSprites.push_back(sprite);
	}

	return true;
}

bool CryOmni3DEngine_Egypt::setInterfaceCursor(uint spriteId) const {
	if (spriteId >= _interfaceSprites.size())
		return false;

	const EgyptInterfaceSprite &sprite = *_interfaceSprites[spriteId];
	CursorMan.replaceCursor(sprite.surface, sprite.hotspotX, sprite.hotspotY, 0, false,
	                        sprite.mask.empty() ? nullptr : sprite.mask.data());
	return true;
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
