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

#include "common/debug.h"
#include "common/endian.h"
#include "common/file.h"
#include "common/path.h"
#include "common/textconsole.h"

#include "cryomni3d/cryomni3d.h"
#include "cryomni3d/egypt/sprite.h"
#include "cryomni3d/egypt/support/cpx5.h"

#include "graphics/cursorman.h"
#include "graphics/surface.h"

namespace CryOmni3D {
namespace Egypt {

const Graphics::PixelFormat kEgyptSpriteFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);

bool Egypt_SpriteLoader::loadInterfaceSprites(const Common::Path &filename) {
	_interfaceSprites.clear();

	Common::File file;
	if (!file.open(filename)) {
		warning("Egypt: failed to open interface sprite file %s",
		        filename.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	Common::Array<byte> decompressed;
	if (!Cpx5Decoder::decompress(file, decompressed))
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
	_interfaceSprites.resize(spriteCount);
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
			_interfaceSprites.clear();
			return false;
		}

		EgyptInterfaceSprite &sprite = _interfaceSprites[i];
		sprite.surface.create(width, height, kEgyptSpriteFormat);
		sprite.hotspotX = width / 2;
		sprite.hotspotY = height / 2;
		memcpy(sprite.surface.getPixels(), decompressed.data() + pixelOffset, pixelDataSize);

		sprite.mask.resize(width * height);
		for (uint pixel = 0; pixel < width * height; ++pixel) {
			const uint16 color = READ_LE_UINT16(decompressed.data() + pixelOffset + pixel * 2);
			sprite.mask[pixel] = (color == 0) ? kCursorMaskTransparent : kCursorMaskOpaque;
		}
	}

	return true;
}

bool Egypt_SpriteLoader::setInterfaceCursor(uint spriteId) const {
	if (spriteId >= _interfaceSprites.size())
		return false;

	const EgyptInterfaceSprite &sprite = _interfaceSprites[spriteId];
	CursorMan.replaceCursor(sprite.surface, sprite.hotspotX, sprite.hotspotY, 0, false,
	                        sprite.mask.empty() ? nullptr : sprite.mask.data());
	return true;
}

bool Egypt_SpriteLoader::loadSceneOverlay(const Common::String &sceneName, int level) {
	_sceneOverlayData.clear();

	Common::Path sprPath;
	if (level >= 1 && level <= 6) {
		Common::Path p(Common::String::format("SPRITE/LEVEL%d/%s.SPR", level, sceneName.c_str()));
		if (Common::File::exists(p))
			sprPath = p;
	}
	if (sprPath.empty()) {
		for (int lvl = 1; lvl <= 6; ++lvl) {
			Common::Path p(Common::String::format("SPRITE/LEVEL%d/%s.SPR", lvl, sceneName.c_str()));
			if (Common::File::exists(p)) {
				sprPath = p;
				break;
			}
		}
	}

	if (sprPath.empty()) {
		debugC(kDebugFile, "Egypt: no overlay SPR found for scene %s", sceneName.c_str());
		return false;
	}

	Common::File file;
	if (!file.open(sprPath)) {
		warning("Egypt: failed to open overlay SPR %s",
		        sprPath.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	if (!Cpx5Decoder::decompress(file, _sceneOverlayData)) {
		warning("Egypt: failed to decompress overlay SPR %s",
		        sprPath.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	debugC(kDebugFile, "Egypt: loaded overlay SPR %s (%u bytes)",
	        sprPath.toString(Common::Path::kNativeSeparator).c_str(),
	        (uint)_sceneOverlayData.size());
	return true;
}

void Egypt_SpriteLoader::resetSceneState() {
	_sceneOverlayData.clear();
	_pendingOverlayPixels.clear();
	_hasPendingOverlay = false;
	_sceneSprPixels.clear();
	_sceneSprDirty = false;
}

void Egypt_SpriteLoader::decodeOverlayFrame(uint frameIndex) {
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

	if (pixelOffset >= dataSize) {
		warning("Egypt: overlay frame %u pixel offset 0x%08x out of range", frameIndex, pixelOffset);
		return;
	}

	const byte *ptr = data + pixelOffset;
	const byte *end = data + dataSize;

	// TXEN/RLE header - same format as SPA/SPB portrait patches.
	// Coordinates are in screen space (640x480), rows go top-down.
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

	int screenY = (int)txenY;
	int line = 0;

	while (ptr + 2 <= end) {
		const uint16 token = READ_LE_UINT16(ptr); ptr += 2;
		if (token == 0)
			break;
		if (token == 0xFFFF) {
			++line;
			screenY = (int)txenY + line;
			continue;
		}
		if (ptr + 2 > end) break;
		const uint16 xAbs = READ_LE_UINT16(ptr); ptr += 2;
		const int xBase = (int)txenX + (int)xAbs;

		for (uint16 i = 0; i < token && ptr + 2 <= end; i++) {
			const uint16 rgb565 = READ_LE_UINT16(ptr); ptr += 2;
			const int px = xBase + (int)i;
			if (px >= 0 && px < 640 && screenY >= 0 && screenY < 480) {
				EgyptPendingOverlayPixel p;
				p.x = (uint16)px;
				p.y = (uint16)screenY;
				p.rgb565 = rgb565;
				_pendingOverlayPixels.push_back(p);
			}
		}
	}

	_hasPendingOverlay = !_pendingOverlayPixels.empty();
	debugC(kDebugFile, "Egypt: decoded overlay frame %u -> %u pixels", frameIndex, (uint)_pendingOverlayPixels.size());
}

// Expands one RGB565 pixel to the destination format and writes it
static inline void writeRgb565Pixel(Graphics::Surface &surface, uint x, uint y, uint16 rgb565) {
	const Graphics::PixelFormat &fmt = surface.format;
	const uint8 r5 = (rgb565 >> 11) & 0x1f;
	const uint8 g6 = (rgb565 >>  5) & 0x3f;
	const uint8 b5 = (rgb565      ) & 0x1f;
	const uint8 r = (r5 << 3) | (r5 >> 2);
	const uint8 g = (g6 << 2) | (g6 >> 4);
	const uint8 b = (b5 << 3) | (b5 >> 2);

	const uint32 color = fmt.RGBToColor(r, g, b);
	void *dst = surface.getBasePtr(x, y);
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

void Egypt_SpriteLoader::applyOverlayToSurface(Graphics::Surface &surface) const {
	if (!_hasPendingOverlay || _pendingOverlayPixels.empty()) return;

	const uint surfW = surface.w;
	const uint surfH = surface.h;

	for (uint i = 0; i < _pendingOverlayPixels.size(); i++) {
		const EgyptPendingOverlayPixel &p = _pendingOverlayPixels[i];
		if (p.x >= surfW || p.y >= surfH) continue;
		writeRgb565Pixel(surface, p.x, p.y, p.rgb565);
	}
}

void Egypt_SpriteLoader::decodeSceneSprFrame(uint frameIndex) {
	_sceneSprPixels.clear();
	_sceneSprDirty = false;

	if (_sceneOverlayData.empty()) return;

	const byte *data = _sceneOverlayData.data();
	const uint dataSize = _sceneOverlayData.size();

	if (dataSize < 8) return;

	const uint32 firstPixelOffset = READ_LE_UINT32(data);
	if (firstPixelOffset == 0 || (firstPixelOffset % 8) != 0 || firstPixelOffset > dataSize) {
		warning("Egypt: scene SPR table has invalid first offset 0x%08x", firstPixelOffset);
		return;
	}

	const uint frameCount = firstPixelOffset / 8;
	if (frameIndex >= frameCount) {
		warning("Egypt: decodeSceneSprFrame %u out of range (%u frames)", frameIndex, frameCount);
		return;
	}

	const uint32 pixelOffset = READ_LE_UINT32(data + frameIndex * 8);
	if (pixelOffset >= dataSize) {
		warning("Egypt: scene SPR frame %u pixel offset 0x%08x out of range", frameIndex, pixelOffset);
		return;
	}

	const byte *ptr = data + pixelOffset;
	const byte *end = data + dataSize;

	// TXEN header: magic(4) + y(2) + x(2) + height(2) + width(2) = 12 bytes
	int16 txenY = 0;
	int16 txenX = 0;
	if (ptr + 12 <= end) {
		const uint32 magic = READ_LE_UINT32(ptr);
		if (magic == 0x4e455854u /* TXEN LE */ || magic == 0x5458454eu /* NEXT LE */) {
			txenY = (int16)READ_LE_UINT16(ptr + 4);
			txenX = (int16)READ_LE_UINT16(ptr + 6);
			ptr += 12;
		}
	}

	// Store raw TXEN coords: p.y = txenY + rleLine (top-down, screen-like).
	// applySceneSprToPanorama inverts Y to panorama space (767 - p.y).
	// applySceneSprToScreen uses p.y directly for fixed TGA views.
	int line = 0;

	while (ptr + 2 <= end) {
		const uint16 token = READ_LE_UINT16(ptr); ptr += 2;
		if (token == 0)
			break;
		if (token == 0xFFFF) {
			++line;
			continue;
		}
		if (ptr + 2 > end) break;
		const uint16 xAbs = READ_LE_UINT16(ptr); ptr += 2;
		const int xBase = (int)txenX + (int)xAbs;
		const int rawY  = (int)txenY + line;

		for (uint16 i = 0; i < token && ptr + 2 <= end; i++) {
			const uint16 rgb565 = READ_LE_UINT16(ptr); ptr += 2;
			const int px = xBase + (int)i;
			if (px >= 0 && px < 2048 && rawY >= 0 && rawY < 768) {
				EgyptPendingOverlayPixel p;
				p.x = (uint16)px;
				p.y = (uint16)rawY;
				p.rgb565 = rgb565;
				_sceneSprPixels.push_back(p);
			}
		}
	}

	_sceneSprDirty = !_sceneSprPixels.empty();
	debugC(kDebugFile, "Egypt: decoded scene SPR frame %u -> %u panorama pixels (txen x=%d y=%d)",
	        frameIndex, (uint)_sceneSprPixels.size(), (int)txenX, (int)txenY);
}

void Egypt_SpriteLoader::applySceneSprToPanorama(Graphics::Surface &surface) const {
	if (_sceneSprPixels.empty()) return;

	const uint surfW = (uint)surface.w;
	const uint surfH = (uint)surface.h;

	for (uint i = 0; i < _sceneSprPixels.size(); i++) {
		const EgyptPendingOverlayPixel &p = _sceneSprPixels[i];
		// p.y is stored as txenY + rleLine; panorama Y is inverted: 767 - p.y
		const uint panoramaY = (p.y <= 767) ? (767 - p.y) : 0;
		if (p.x >= surfW || panoramaY >= surfH) continue;
		writeRgb565Pixel(surface, p.x, panoramaY, p.rgb565);
	}
}

void Egypt_SpriteLoader::applySceneSprToScreen(Graphics::Surface &surface) const {
	if (_sceneSprPixels.empty()) return;

	const uint surfW = (uint)surface.w;
	const uint surfH = (uint)surface.h;

	for (uint i = 0; i < _sceneSprPixels.size(); i++) {
		const EgyptPendingOverlayPixel &p = _sceneSprPixels[i];
		if (p.x >= surfW || p.y >= surfH) continue;
		writeRgb565Pixel(surface, p.x, p.y, p.rgb565);
	}
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
