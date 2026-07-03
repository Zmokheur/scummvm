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
#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "cryomni3d/egypt/engine.h"
#include "cryomni3d/egypt/panorama.h"
#include "cryomni3d/egypt/support/image_loader.h"
#include "cryomni3d/image/hnm.h"

#include "graphics/font.h"
#include "graphics/fontman.h"
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
		{
			const int held = getScriptVariableValue("main");
			uint cursorId;
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

		const Common::String hoverText = getHoverTextForZone(hoveredZone);
		if (!hoverText.empty()) {
			const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
			if (font) {
				const int textWidth = font->getStringWidth(hoverText);
				const int textX = CLIP<int>(mouse.x + 18, 8, compositedFrame.w - textWidth - 12);
				const int textY = CLIP<int>(mouse.y + 14, 8, compositedFrame.h - font->getFontHeight() - 10);
				const Common::Rect bubble(textX - 6, textY - 3,
				                          textX + textWidth + 6, textY + font->getFontHeight() + 4);
				compositedFrame.fillRect(bubble, compositedFrame.format.RGBToColor(20, 18, 14));
				compositedFrame.frameRect(bubble, compositedFrame.format.RGBToColor(188, 154, 84));
				font->drawString(&compositedFrame, hoverText, textX, textY,
				                 compositedFrame.w - textX, compositedFrame.format.RGBToColor(244, 232, 204));
			}
		}

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
		const Common::String hoverText = getHoverTextForZone(hoveredZone);
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
			if (!hoverText.empty()) {
				const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
				if (font) {
					const int textWidth = font->getStringWidth(hoverText);
					const int textX = CLIP<int>(mouse.x + 18, 8, compositedFrame.w - textWidth - 12);
					const int textY = CLIP<int>(mouse.y + 14, 8, compositedFrame.h - font->getFontHeight() - 10);
					const Common::Rect bubble(textX - 6, textY - 3,
					                          textX + textWidth + 6, textY + font->getFontHeight() + 4);
					compositedFrame.fillRect(bubble, compositedFrame.format.RGBToColor(20, 18, 14));
					compositedFrame.frameRect(bubble, compositedFrame.format.RGBToColor(188, 154, 84));
					font->drawString(&compositedFrame, hoverText, textX, textY,
					                 compositedFrame.w - textX, compositedFrame.format.RGBToColor(244, 232, 204));
				}
			}

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
