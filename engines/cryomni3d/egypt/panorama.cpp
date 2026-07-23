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
#include "common/rect.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "cryomni3d/egypt/engine.h"
#include "cryomni3d/egypt/panorama.h"
#include "cryomni3d/egypt/support/image_loader.h"
#include "cryomni3d/image/hnm.h"

#include "graphics/managed_surface.h"
#include "graphics/palette.h"
#include "graphics/surface.h"

namespace CryOmni3D {
namespace Egypt {

namespace {

class EgyptPreviewHNMDecoder : public Image::HNMFileDecoder {
public:
	EgyptPreviewHNMDecoder(const Graphics::PixelFormat &format) : Image::HNMFileDecoder(format) {}

	const Graphics::Palette &getPalette() const override { return _palette; }

private:
	Graphics::Palette _palette;
};

} // End of anonymous namespace

// 50/50 average of each pixel with the dark tint {30, 25, 18} - EXE 0x817680
// (params 0x4352fc/0x435300/0x435304), the same darken the toolbar uses. Mirrors
// blendDarken() in toolbar.cpp; kept local so the hover-name box matches the EXE
// box drawn behind the zone name.
static void blendDarkenRect(Graphics::ManagedSurface &surface, const Common::Rect &rect) {
	const Graphics::PixelFormat &fmt = surface.format;
	if (fmt.bytesPerPixel != 4)
		return;
	for (int y = rect.top; y < rect.bottom; y++) {
		if (y < 0 || y >= surface.h)
			continue;
		byte *row = (byte *)surface.getBasePtr(0, y);
		for (int x = rect.left; x < rect.right; x++) {
			if (x < 0 || x >= surface.w)
				continue;
			const uint32 pixel = READ_LE_UINT32(row + x * 4);
			uint8 r, g, b;
			fmt.colorToRGB(pixel, r, g, b);
			r = (uint8)((((r >> 3) + (30 >> 3)) >> 1) << 3);
			g = (uint8)((((g >> 2) + (25 >> 2)) >> 1) << 2);
			b = (uint8)((((b >> 3) + (18 >> 3)) >> 1) << 3);
			WRITE_LE_UINT32(row + x * 4, fmt.RGBToColor(r, g, b));
		}
	}
}

void CryOmni3DEngine_Egypt::drawZoneHoverText(Graphics::ManagedSurface &surface,
                                              const EgyptZone *zone, uint cursorId,
                                              const Common::Point &mouse) {
	if (!zone)
		return;
	const Common::String text = getHoverTextForZone(zone);
	if (text.empty())
		return;

	if (cursorId == kEgyptCursorWarpLabel) {
		// EXE cursor renderer (~0x8094xx): with the "finger" warp-label cursor
		// (edi==0xb, cmp at 0x8095f3) the zone name is drawn ABOVE the cursor - a
		// darkened box (0x817680) then FONT11 text (font id 0xa) with a black drop
		// shadow at (x+1,y+1) and white on top, centered on the cursor and 20px
		// above its top (edi = ebp-0x14).
		if (cursorId >= _spriteLoader.interfaceSpriteCount())
			return;
		const EgyptInterfaceSprite &cursorSpr = _spriteLoader.interfaceSprite(cursorId);
		const int cursorTop = mouse.y - cursorSpr.hotspotY;

		_fontManager.setCurrentFont(Egypt_FontManager::kSlotToolbar); // FONT11 = EXE font 0xa
		const int textW = (int)_fontManager.getStrWidth(text);

		int x = mouse.x - textW / 2;  // centered on the cursor (EXE esi - width/2)
		int y = cursorTop - 20;       // EXE ebp - 0x14
		if (x < 1)
			x = 1;
		if (x + textW > kScreenWidth - 1)
			x = kScreenWidth - 1 - textW;
		if (y < 1)
			y = 1;

		// EXE 0x80972c box: top-left (X-1, Y-1), width textW+2, height 0xf (15).
		blendDarkenRect(surface, Common::Rect(x - 1, y - 1, x + textW + 1, y - 1 + 15));
		_fontManager.setForeColor(surface.format.RGBToColor(0, 0, 0));
		_fontManager.displayStr(surface, x + 1, y + 1, text);
		_fontManager.setForeColor(surface.format.RGBToColor(255, 255, 255));
		_fontManager.displayStr(surface, x, y, text);
		return;
	}

	// Info zones (actionId 6): the description is shown bottom-left over a darkened
	// strip, not at the cursor.  EXE 0x808480: the text is drawn (0x81a510) at
	// y = 0x1d2 (466), x = 0x2 in story mode or 0x16 (22) in visit mode, with FONT11
	// (font id 0xa) and no shadow.  Behind it the EXE darkens a bottom strip
	// (0x817680, tint {30,25,18}) starting at buffer offset 0x91000 == row 464,
	// height 0x10 (16px, 464..480), width = text width + 0x4 (story) / +0x18 (visit).
	if (zone->actionId != 6)
		return;

	const bool visitMode = getScriptVariableValue("FlagVisite") != 0;
	_fontManager.setCurrentFont(Egypt_FontManager::kSlotToolbar); // FONT11 = EXE font 0xa
	const int textW = (int)_fontManager.getStrWidth(text);
	const int textX = visitMode ? 22 : 2;                 // EXE 0x16 / 0x2
	const int boxW  = textW + (visitMode ? 24 : 4);       // EXE esi + 0x18 / +0x4

	blendDarkenRect(surface, Common::Rect(0, 464, boxW, 480));
	_fontManager.setForeColor(surface.format.RGBToColor(255, 255, 255));
	_fontManager.displayStr(surface, textX, 466, text);
}


bool CryOmni3DEngine_Egypt::displayCurrentWarpFixed(const Graphics::Surface *frame) {
	if (!frame)
		return false;

	double arrivalAlpha = 0.0, arrivalBeta = 0.0;
	bool hasArrivalAngles = false;
	consumeArrivalPanoramaX(arrivalAlpha, arrivalBeta, hasArrivalAngles);
	if (_pendingWarp.active)
		logRuntimeWarp(_pendingRuntimeMatchedCentrage, _pendingRuntimeResolved, false);
	clearPendingWarpRequest();

	clearKeys();
	waitMouseRelease();
	showMouse(true);
	setInterfaceCursor(getDefaultCursorFrame());

	debugC(kDebugVariable, "Egypt: fixed view active for %s", _currentScene.name.c_str());

	Graphics::ManagedSurface compositedFrame(MIN((int)frame->w, kScreenWidth), MIN((int)frame->h, kScreenHeight),
	                                         g_system->getScreenFormat());

	compositedFrame.blitFrom(*frame);
	performCrossFade(&compositedFrame.rawSurface());

	bool exitView = false;
	_canLoadSave = true;
	while (!shouldAbort() && !exitView) {
		if (_pendingLoadSlot >= 0)
			break;
		if (_sceneHasTimerScript) {
			updateScriptTimer();
			runEndInit(0);
			if (!_pendingWarpTarget.empty() || getScriptVariableValue("EndGame") != 0) {
				exitView = true;
				break;
			}
		}
		pollEvents();

		const Common::Point mouse = getMousePos();
		// Zone coords for TGA scenes are in screen space - use mouse directly as warp point.
		const EgyptZone *hoveredZone = findHoveredActiveZone(mouse);
		uint cursorId = getDefaultCursorFrame();
		{
			const int held = getScriptVariableValue("main");
			if (held == 0) {
				cursorId = hoveredZone ? getCursorFrameForZone(*hoveredZone) : getDefaultCursorFrame();
			} else if (hoveredZone && hoveredZone->commandName.equalsIgnoreCase("UTILISER_SUR")) {
				const int required = getScriptVariableValue("Objet" + hoveredZone->label);
				cursorId = (required != 0 && held == required)
				           ? getCursorFrameForHeldObject(held, true)
				           : getDefaultCursorFrame();
			} else {
				cursorId = getDefaultCursorFrame();
			}
			setInterfaceCursor(cursorId);
		}

		if (getCurrentMouseButton() == 1) {
			if (handleWarpClick(mouse, mouse, 0.0, 0.0))
				exitView = true;
			waitMouseRelease();
		}

		if (!exitView && getCurrentMouseButton() == 2) {
			waitMouseRelease();
			if (displayToolbar(frame)) {
				exitView = true;
			} else {
				clearKeys();
				setInterfaceCursor(getDefaultCursorFrame());
				showMouse(true);
			}
		}

		const Common::KeyCode kc = getNextKey().keycode;
		if (kc == Common::KEYCODE_SPACE || kc == Common::KEYCODE_RETURN ||
		    kc == Common::KEYCODE_ESCAPE)
			exitView = true;

		compositedFrame.blitFrom(*frame);
		if (!_spriteLoader.sceneSprEmpty())
			_spriteLoader.applySceneSprToScreen(*compositedFrame.surfacePtr());
		if (_spriteLoader.isSceneSprDirty())
			_spriteLoader.setSceneSprDirty(false);

		// Finger cursor -> zone name above cursor; info zones -> description
		// bottom-left (EXE ~0x8094xx).
		drawZoneHoverText(compositedFrame, hoveredZone, cursorId, mouse);

		g_system->copyRectToScreen(compositedFrame.getPixels(), compositedFrame.pitch,
		                           0, 0, compositedFrame.w, compositedFrame.h);
		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	_canLoadSave = false;
	waitMouseRelease();
	clearKeys();
	showMouse(false);
	return !_pendingWarpTarget.empty();
}

bool CryOmni3DEngine_Egypt::displayCurrentWarpPreview(const Common::Path &warpPath) {
	// Determine display mode solely from the pre-built asset table.
	enum SceneDisplayMode { kRotation, kFixed, kNone };
	SceneDisplayMode mode;

	if (_currentSceneAssets.size() > (uint)kAssetWarpHNM &&
	    _currentSceneAssets[kAssetWarpHNM].present) {
		mode = kRotation;
	} else if (_currentSceneAssets.size() > (uint)kAssetBackTGA &&
	           _currentSceneAssets[kAssetBackTGA].present) {
		mode = kFixed;
	} else {
		mode = kNone;
	}

	switch (mode) {
	case kFixed: {
		const Common::Path tgaPath = _currentSceneAssets[kAssetBackTGA].path;

		if (tgaPath.empty()) {
			warning("Egypt: fixed view %s has no usable TGA", _currentScene.name.c_str());
			return false;
		}
		Graphics::ManagedSurface tgaSurface;
		if (!loadTgaImage(tgaPath, tgaSurface, true)) {
			warning("Egypt: fixed view %s failed to load TGA %s",
			        _currentScene.name.c_str(),
			        tgaPath.toString(Common::Path::kNativeSeparator).c_str());
			return false;
		}
		debugC(kDebugFile, "Egypt: fixed view %s using %s",
		        _currentScene.name.c_str(),
		        tgaPath.toString(Common::Path::kNativeSeparator).c_str());
		return displayCurrentWarpFixed(&tgaSurface.rawSurface());
	}

	case kRotation: {
		Common::File file;
		if (!file.open(warpPath)) {
			warning("Egypt: rotation view %s failed to open warp %s",
			        _currentScene.name.c_str(),
			        warpPath.toString(Common::Path::kNativeSeparator).c_str());
			return false;
		}
		EgyptPreviewHNMDecoder imageDecoder(g_system->getScreenFormat());
		if (!imageDecoder.loadStream(file)) {
			warning("Egypt: rotation view %s failed to decode warp %s",
			        _currentScene.name.c_str(),
			        warpPath.toString(Common::Path::kNativeSeparator).c_str());
			return false;
		}
		if (imageDecoder.hasPalette())
			setupPalette(imageDecoder.getPalette().data(), 0, imageDecoder.getPalette().size());
		const Graphics::Surface *frame = imageDecoder.getSurface();
		if (!frame) {
			warning("Egypt: rotation view %s got no frame from warp", _currentScene.name.c_str());
			return false;
		}
		return displayCurrentWarpRotation(frame);
	}

	default:
		warning("Egypt: scene %s has no displayable asset (no warp HNM, no TGA)",
		        _currentScene.name.c_str());
		return false;
	}
}

bool CryOmni3DEngine_Egypt::displayCurrentWarpRotation(const Graphics::Surface *frame) {
	// Mutable copy of the panorama so scene SPR overlays can be composited into it
	// before Omni3D projection (TXEN coords are in panorama space, Y inverted).
	Graphics::ManagedSurface panoramaCopy;
	panoramaCopy.copyFrom(*frame);

	Egypt_Panorama renderer;
	renderer.init(75. / 180. * M_PI, panoramaCopy.surfacePtr());
	Graphics::ManagedSurface compositedFrame(kScreenWidth, kScreenHeight, g_system->getScreenFormat());

	double arrivalAlpha = 0.0;
	double arrivalBeta = 0.0;
	bool hasArrivalAngles = false;
	const int arrivalPanoramaX = consumeArrivalPanoramaX(arrivalAlpha, arrivalBeta, hasArrivalAngles);
	if (hasArrivalAngles) {
		renderer.setViewAngles(arrivalAlpha, arrivalBeta);
		setRuntimeViewAngles(arrivalAlpha, arrivalBeta, true);
		debugC(kDebugVariable, "Egypt: aligned %s to script angles alpha=%0.3f beta=%0.3f",
		        _currentScene.name.c_str(), arrivalAlpha, arrivalBeta);
	} else if (arrivalPanoramaX >= 0) {
		renderer.setPanoramaCenterX(arrivalPanoramaX);
		debugC(kDebugVariable, "Egypt: aligned %s to panorama x=%d",
		        _currentScene.name.c_str(), arrivalPanoramaX);
	}
	if (_pendingWarp.active)
		logRuntimeWarp(_pendingRuntimeMatchedCentrage, _pendingRuntimeResolved, hasArrivalAngles);
	clearPendingWarpRequest();

	const uint availableCursors = _spriteLoader.interfaceSpriteCount();
	auto setRotationCursor = [&](uint cursorId) {
		if (setInterfaceCursor(cursorId))
			return;

		const uint fallbackCursor = (availableCursors > kEgyptCursorDefault) ? kEgyptCursorDefault : 0;
		if (availableCursors > 0) {
			warning("Egypt: cursor %u unavailable, fallback to cursor %u (%u available)",
			        cursorId, fallbackCursor, availableCursors);
			setInterfaceCursor(fallbackCursor);
		}
	};

	clearKeys();
	waitMouseRelease();
	showMouse(true);
	setRotationCursor(getDefaultCursorFrame());

	debugC(kDebugVariable, "Egypt: interactive rotation enabled for %s, click or press space to continue",
	        _currentScene.name.c_str());

	// Consume any scene SPR overlay set by warpinit (e.g. the plank in S03) before
	// the first render so it's visible in the cross-fade and not just from the first
	// display-loop tick onward.
	if (_spriteLoader.isSceneSprDirty()) {
		if (!_spriteLoader.sceneSprEmpty()) {
			_spriteLoader.applySceneSprToPanorama(*panoramaCopy.surfacePtr());
			renderer.markSourceChanged();
		}
		_spriteLoader.setSceneSprDirty(false);
	}

	{
		const Graphics::Surface *firstFrame = renderer.getSurface();
		if (firstFrame) {
			compositedFrame.blitFrom(*firstFrame);
			performCrossFade(&compositedFrame.rawSurface());
		}
	}

	bool exitRotation = false;
	bool firstDraw    = true;
	_canLoadSave = true;
	while (!shouldAbort() && !exitRotation) {
		if (_pendingLoadSlot >= 0)
			break;
		if (_sceneHasTimerScript) {
			updateScriptTimer();
			runEndInit(0);
			if (!_pendingWarpTarget.empty() || getScriptVariableValue("EndGame") != 0) {
				exitRotation = true;
				break;
			}
		}

		pollEvents();

		Common::Point mouse = getMousePos();
		int xDelta = 0;
		int yDelta = 0;
		uint movingCursor = getDefaultCursorFrame();

		bool topZone = false;
		bool bottomZone = false;
		bool leftZone = false;
		bool rightZone = false;

		if (mouse.y < 100) {
			topZone = true;
			yDelta = 100 - mouse.y;
		} else if (mouse.y > 380) {
			bottomZone = true;
			yDelta = 380 - mouse.y;
		}

		if (mouse.x < 100) {
			leftZone = true;
			xDelta = 100 - mouse.x;
		} else if (mouse.x > 540) {
			rightZone = true;
			xDelta = 540 - mouse.x;
		}

		if (topZone && !leftZone && !rightZone)
			movingCursor = kEgyptCursorNav0;
		else if (topZone && rightZone)
			movingCursor = kEgyptCursorNav1;
		else if (rightZone && !topZone && !bottomZone)
			movingCursor = kEgyptCursorNav2;
		else if (bottomZone && rightZone)
			movingCursor = kEgyptCursorNav3;
		else if (bottomZone && !leftZone && !rightZone)
			movingCursor = kEgyptCursorNav4;
		else if (bottomZone && leftZone)
			movingCursor = kEgyptCursorNav5;
		else if (leftZone && !topZone && !bottomZone)
			movingCursor = kEgyptCursorNav6;
		else if (topZone && leftZone)
			movingCursor = kEgyptCursorNav7;

		xDelta /= 5;
		yDelta /= 5;

		Common::Point warpPoint = renderer.mapMouseCoords(mouse);
		const EgyptZone *hoveredZone = findHoveredActiveZone(warpPoint);
		{
			const int held = getScriptVariableValue("main");
			if (hoveredZone) {
				if (held == 0) {
					movingCursor = getCursorFrameForZone(*hoveredZone);
				} else if (hoveredZone->commandName.equalsIgnoreCase("UTILISER_SUR")) {
					const int required = getScriptVariableValue("Objet" + hoveredZone->label);
					if (required != 0 && held == required)
						movingCursor = getCursorFrameForHeldObject(held, true);
					// else: keep movingCursor (nav arrow or default object cursor)
				}
				// else: object in hand blocks zone cursor highlight
			}
		}

		Common::KeyState key = getNextKey();
		if (getCurrentMouseButton() == 1) {
			if (handleWarpClick(mouse, warpPoint, renderer.getAlpha(), renderer.getBeta()))
				exitRotation = true;
			waitMouseRelease();
		}

		if (key.keycode == Common::KEYCODE_SPACE || key.keycode == Common::KEYCODE_RETURN ||
		    key.keycode == Common::KEYCODE_ESCAPE) {
			exitRotation = true;
		}

		if (key.keycode == Common::KEYCODE_LEFT)
			xDelta -= 6;
		else if (key.keycode == Common::KEYCODE_RIGHT)
			xDelta += 6;

		if (key.keycode == Common::KEYCODE_UP)
			yDelta -= 5;
		else if (key.keycode == Common::KEYCODE_DOWN)
			yDelta += 5;

		setRotationCursor(movingCursor);

		if (getCurrentMouseButton() == 0) {
			if (hoveredZone) {
				if (_lastHoveredZoneId != hoveredZone->id) {
					debugC(2, kDebugVariable, "Egypt: hover %s mouse=%d,%d warp=%d,%d zone=%03u action=%u cursor=%u target=%s",
					        _currentScene.name.c_str(), mouse.x, mouse.y, warpPoint.x, warpPoint.y,
					        hoveredZone->id, hoveredZone->actionId, movingCursor,
					        hoveredZone->targetWarp.c_str());
					_lastHoveredZoneId = hoveredZone->id;
				}
			} else if (_lastHoveredZoneId != uint(-1)) {
				_lastHoveredZoneId = uint(-1);
			}
		}

		auto drawFrame = [&]() {
			if (_spriteLoader.isSceneSprDirty()) {
				panoramaCopy.blitFrom(*frame);
				_spriteLoader.applySceneSprToPanorama(*panoramaCopy.surfacePtr());
				renderer.markSourceChanged();
				_spriteLoader.setSceneSprDirty(false);
			}
			const Graphics::Surface *result = renderer.getSurface();
			if (!result)
				return;

			compositedFrame.blitFrom(*result);
			if (_spriteLoader.hasPendingOverlay())
				_spriteLoader.applyOverlayToSurface(*compositedFrame.surfacePtr());
			// Finger cursor -> name above cursor; info zones -> description
			// bottom-left (see fixed-view variant).
			drawZoneHoverText(compositedFrame, hoveredZone, movingCursor, mouse);

			g_system->copyRectToScreen(compositedFrame.getPixels(), compositedFrame.pitch, 0, 0,
			                           compositedFrame.w, compositedFrame.h);
			g_system->updateScreen();
		};

		if (firstDraw || xDelta != 0 || yDelta != 0 || renderer.hasSpeed()) {
			renderer.updateCoords(xDelta, -yDelta, true);
			drawFrame();
			firstDraw = false;
		} else {
			drawFrame();
		}

		// Toolbar: triggered only by right-click; rotation is implicitly paused
		// while displayToolbar() runs its own blocking event loop.
		if (!exitRotation && getCurrentMouseButton() == 2) {
			waitMouseRelease();
			if (displayToolbar(&compositedFrame.rawSurface())) {
				exitRotation = true;
			} else {
				firstDraw = true;
			}
			clearKeys();
			setInterfaceCursor(getDefaultCursorFrame());
			showMouse(true);
		}

		g_system->delayMillis(10);
	}

	_canLoadSave = false;
	waitMouseRelease();
	clearKeys();
	showMouse(false);
	return true;
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
