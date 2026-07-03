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

#ifndef CRYOMNI3D_EGYPT_SPRITE_H
#define CRYOMNI3D_EGYPT_SPRITE_H

#include "common/array.h"
#include "common/str.h"

#include "graphics/pixelformat.h"

#include "cryomni3d/egypt/cursor.h"

namespace Common {
class Path;
}

namespace Graphics {
struct Surface;
}

namespace CryOmni3D {
namespace Egypt {

// Pixel format of INTERFAC.SPR sprite data (RGB565)
extern const Graphics::PixelFormat kEgyptSpriteFormat;

// One pixel of a decoded TXEN/RLE sprite frame, in absolute coordinates
struct EgyptPendingOverlayPixel {
	uint16 x;
	uint16 y;
	uint16 rgb565;
};

// Decodes and holds Egypt's SPR sprite data:
// - interface sprites (INTERFAC.SPR): mouse cursors and toolbar icons
// - per-scene overlay SPR files: TXEN/RLE frames patched onto the
//   screen (fixed views) or onto the panorama (rotating views)
class Egypt_SpriteLoader {
public:
	// Interface sprites
	bool loadInterfaceSprites(const Common::Path &filename);
	uint interfaceSpriteCount() const { return _interfaceSprites.size(); }
	const EgyptInterfaceSprite &interfaceSprite(uint id) const { return _interfaceSprites[id]; }
	bool setInterfaceCursor(uint spriteId) const;

	// Scene overlay SPR
	bool loadSceneOverlay(const Common::String &sceneName, int level);
	bool hasOverlayData() const { return !_sceneOverlayData.empty(); }
	void resetSceneState();

	// Screen-space overlay frames (editspr/show path)
	void decodeOverlayFrame(uint frameIndex);
	bool hasPendingOverlay() const { return _hasPendingOverlay; }
	void applyOverlayToSurface(Graphics::Surface &surface) const;

	// Scene SPR frames (animspr path)
	void decodeSceneSprFrame(uint frameIndex);
	bool sceneSprEmpty() const { return _sceneSprPixels.empty(); }
	void clearSceneSpr() { _sceneSprPixels.clear(); _sceneSprDirty = true; }
	bool isSceneSprDirty() const { return _sceneSprDirty; }
	void setSceneSprDirty(bool dirty) { _sceneSprDirty = dirty; }
	void applySceneSprToPanorama(Graphics::Surface &surface) const;
	void applySceneSprToScreen(Graphics::Surface &surface) const;

private:
	Common::Array<EgyptInterfaceSprite> _interfaceSprites;
	Common::Array<byte> _sceneOverlayData;
	Common::Array<EgyptPendingOverlayPixel> _pendingOverlayPixels;
	bool _hasPendingOverlay = false;
	Common::Array<EgyptPendingOverlayPixel> _sceneSprPixels;
	bool _sceneSprDirty = false;
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
