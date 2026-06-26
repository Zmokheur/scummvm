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
#include "graphics/surface.h"

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

bool CryOmni3DEngine_Egypt::loadSceneOverlay(const Common::String &sceneName) {
	_sceneOverlayData.clear();

	Common::Path sprPath;
	const int currentLevel = getScriptVariableValue("Level");
	if (currentLevel >= 1 && currentLevel <= 6) {
		Common::Path p(Common::String::format("SPRITE/LEVEL%d/%s.SPR", currentLevel, sceneName.c_str()));
		if (Common::File::exists(p))
			sprPath = p;
	}
	if (sprPath.empty()) {
		for (int level = 1; level <= 6; ++level) {
			Common::Path p(Common::String::format("SPRITE/LEVEL%d/%s.SPR", level, sceneName.c_str()));
			if (Common::File::exists(p)) {
				sprPath = p;
				break;
			}
		}
	}

	if (sprPath.empty()) {
		warning("Egypt: no overlay SPR found for scene %s", sceneName.c_str());
		return false;
	}

	Common::File file;
	if (!file.open(sprPath)) {
		warning("Egypt: failed to open overlay SPR %s",
		        sprPath.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	if (!Image::Cpx5Decoder::decompress(file, _sceneOverlayData)) {
		warning("Egypt: failed to decompress overlay SPR %s",
		        sprPath.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	warning("Egypt: loaded overlay SPR %s (%u bytes)",
	        sprPath.toString(Common::Path::kNativeSeparator).c_str(),
	        (uint)_sceneOverlayData.size());
	return true;
}

void CryOmni3DEngine_Egypt::decodeOverlayFrame(uint frameIndex) {
	_pendingOverlayPixels.clear();
	_hasPendingOverlay = false;

	if (_sceneOverlayData.empty()) return;

	const byte *data = _sceneOverlayData.data();
	const uint dataSize = _sceneOverlayData.size();

	if (dataSize < 8) return;

	const uint32 firstPixelOffset = READ_LE_UINT32(data);
	if (firstPixelOffset == 0 || (firstPixelOffset % 8) != 0 || firstPixelOffset > dataSize) {
		warning("Egypt: overlay SPR table has invalid first offset 0x%08x", firstPixelOffset);
		return;
	}

	const uint frameCount = firstPixelOffset / 8;
	if (frameIndex >= frameCount) {
		warning("Egypt: decodeOverlayFrame %u out of range (%u frames)", frameIndex, frameCount);
		return;
	}

	const uint32 pixelOffset = READ_LE_UINT32(data + frameIndex * 8);
	const int16 overlayHeight = (int16)READ_LE_UINT16(data + frameIndex * 8 + 4);

	if (pixelOffset >= dataSize) {
		warning("Egypt: overlay frame %u pixel offset 0x%08x out of range", frameIndex, pixelOffset);
		return;
	}

	const byte *ptr = data + pixelOffset;
	const byte *end = data + dataSize;

	// Check for TXEN/NEXT header (confirmed from EXE at 0x819bd7)
	int16 txenY = 0;
	int16 txenX = 0;
	if (ptr + 12 <= end) {
		const uint32 magic = READ_LE_UINT32(ptr);
		if (magic == 0x4e455854u /* TXEN */ || magic == 0x5458454eu /* NEXT */) {
			txenY = (int16)READ_LE_UINT16(ptr + 4);
			txenX = (int16)READ_LE_UINT16(ptr + 6);
			ptr += 12;
		}
	}

	// RLE rows from bottom of overlay region going up (confirmed from EXE at 0x8196e7)
	// First row lands at panorama y = 767 - txenY
	int panY = 767 - (int)txenY;
	uint rowsLeft = (overlayHeight > 0) ? (uint)overlayHeight : 0u;

	while (ptr + 2 <= end) {
		const uint16 token = READ_LE_UINT16(ptr); ptr += 2;
		if (token == 0)
			break;
		if (token == 0xFFFF) {
			if (rowsLeft == 0) break;
			panY--;
			rowsLeft--;
			continue;
		}
		// Pixel run: x_abs is absolute within the row (esi + x_abs*2 in EXE at 0x81973d)
		if (ptr + 2 > end) break;
		const uint16 xAbs = READ_LE_UINT16(ptr); ptr += 2;
		const int panXBase = (int)txenX + (int)xAbs;

		for (uint16 i = 0; i < token && ptr + 2 <= end; i++) {
			const uint16 rgb565 = READ_LE_UINT16(ptr); ptr += 2;
			const int px = panXBase + (int)i;
			if (px >= 0 && px < 2048 && panY >= 0 && panY < 768) {
				EgyptPendingOverlayPixel p;
				p.x = (uint16)px;
				p.y = (uint16)panY;
				p.rgb565 = rgb565;
				_pendingOverlayPixels.push_back(p);
			}
		}
	}

	_hasPendingOverlay = !_pendingOverlayPixels.empty();
	warning("Egypt: decoded overlay frame %u → %u pixels", frameIndex, (uint)_pendingOverlayPixels.size());
}

void CryOmni3DEngine_Egypt::applyOverlayToSurface(Graphics::Surface &surface) const {
	if (!_hasPendingOverlay || _pendingOverlayPixels.empty()) return;

	const Graphics::PixelFormat &fmt = surface.format;
	const uint surfW = surface.w;
	const uint surfH = surface.h;

	for (uint i = 0; i < _pendingOverlayPixels.size(); i++) {
		const EgyptPendingOverlayPixel &p = _pendingOverlayPixels[i];
		if (p.x >= surfW || p.y >= surfH) continue;

		// Expand RGB565 to 8-bit channels
		const uint8 r5 = (p.rgb565 >> 11) & 0x1f;
		const uint8 g6 = (p.rgb565 >>  5) & 0x3f;
		const uint8 b5 = (p.rgb565      ) & 0x1f;
		const uint8 r = (r5 << 3) | (r5 >> 2);
		const uint8 g = (g6 << 2) | (g6 >> 4);
		const uint8 b = (b5 << 3) | (b5 >> 2);

		const uint32 color = fmt.RGBToColor(r, g, b);
		void *dst = surface.getBasePtr(p.x, p.y);
		switch (fmt.bytesPerPixel) {
		case 2:
			WRITE_LE_UINT16(dst, (uint16)color);
			break;
		case 4:
			WRITE_LE_UINT32(dst, color);
			break;
		default:
			break;
		}
	}
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
