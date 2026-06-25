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
#include "cryomni3d/egypt/sprite.h"

#include "graphics/cursorman.h"

namespace CryOmni3D {
namespace Egypt {

namespace {

static const Graphics::PixelFormat kEgyptSpriteFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);

} // End of anonymous namespace

bool decompressCpx5(Common::SeekableReadStream &stream, Common::Array<byte> &output) {
	if (stream.size() < 12) {
		warning("Egypt: CPx5 stream too short");
		return false;
	}

	char magic[5];
	magic[0] = (char)stream.readByte();
	magic[1] = (char)stream.readByte();
	magic[2] = (char)stream.readByte();
	magic[3] = (char)stream.readByte();
	magic[4] = '\0';

	if (strcmp(magic, "CPx5") != 0) {
		warning("Egypt: unsupported sprite container magic %s", magic);
		return false;
	}

	const uint32 compressedSize = stream.readUint32BE();
	const uint32 decompressedSize = stream.readUint32BE();
	if (compressedSize != stream.size()) {
		warning("Egypt: CPx5 size mismatch, header=%u actual=%u", compressedSize, (uint)stream.size());
	}

	Common::Array<byte> compressedPayload;
	compressedPayload.resize(stream.size() - 12);
	if (!compressedPayload.empty())
		stream.read(compressedPayload.data(), compressedPayload.size());

	output.resize(decompressedSize);
	uint srcPos = 0;
	uint dstPos = 0;

	while (dstPos < decompressedSize) {
		if (srcPos + 4 > compressedPayload.size()) {
			warning("Egypt: CPx5 truncated while reading flags");
			return false;
		}

		const uint32 flags = READ_LE_UINT32(compressedPayload.data() + srcPos);
		srcPos += 4;

		for (int bit = 31; bit >= 0 && dstPos < decompressedSize; --bit) {
			if (((flags >> bit) & 1) == 0) {
				if (srcPos + 2 > compressedPayload.size() || dstPos + 2 > decompressedSize) {
					warning("Egypt: CPx5 truncated while reading literal");
					return false;
				}

				output[dstPos++] = compressedPayload[srcPos++];
				output[dstPos++] = compressedPayload[srcPos++];
				continue;
			}

			if (srcPos + 2 > compressedPayload.size()) {
				warning("Egypt: CPx5 truncated while reading back-reference");
				return false;
			}

			const uint16 word = READ_BE_UINT16(compressedPayload.data() + srcPos);
			srcPos += 2;

			const uint32 distance = word >> 4;
			uint32 count = word & 0x0f;
			if (count == 0)
				count = 16;

			const uint32 bytesToCopy = count * 2;
			if (distance == 0 || distance > dstPos || dstPos + bytesToCopy > decompressedSize) {
				warning("Egypt: invalid CPx5 back-reference distance=%u count=%u dst=%u/%u",
				        distance, count, dstPos, decompressedSize);
				return false;
			}

			for (uint32 i = 0; i < bytesToCopy; ++i) {
				output[dstPos] = output[dstPos - distance];
				dstPos++;
			}
		}
	}

	return true;
}

bool CryOmni3DEngine_Egypt::loadInterfaceSprites(const Common::Path &filename) {
	Common::File file;
	if (!file.open(filename)) {
		warning("Egypt: failed to open interface sprite file %s",
		        filename.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	Common::Array<byte> decompressed;
	if (!decompressCpx5(file, decompressed))
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
		const uint16 width = READ_LE_UINT16(decompressed.data() + entryOffset + 4);
		const uint16 height = READ_LE_UINT16(decompressed.data() + entryOffset + 6);
		const uint32 pixelDataSize = (uint32)width * (uint32)height * 2;

		if (width == 0 || height == 0 || pixelOffset + pixelDataSize > decompressed.size()) {
			warning("Egypt: invalid interface sprite %u offset=0x%08x size=%ux%u",
			        i, pixelOffset, width, height);
			return false;
		}

		EgyptInterfaceSprite *sprite = new EgyptInterfaceSprite();
		sprite->surface.create(width, height, kEgyptSpriteFormat);
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
