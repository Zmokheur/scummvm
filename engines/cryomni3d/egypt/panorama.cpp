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

#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "cryomni3d/egypt/engine.h"
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

class EgyptWarpRenderer {
public:
	EgyptWarpRenderer() : _vfov(0), _alpha(0), _beta(0), _xSpeed(0), _ySpeed(0),
		_helperValue(0), _dirty(true), _dirtyCoords(true), _sourceSurface(nullptr) {}

	~EgyptWarpRenderer() {
		_surface.free();
	}

	void init(double hfov, const Graphics::Surface *sourceSurface) {
		_sourceSurface = sourceSurface;
		_alpha = 0.0;
		_beta = 0.0;
		_xSpeed = 0.0;
		_ySpeed = 0.0;

		double oppositeSide = tan(hfov / 2.) / (4. / 3.);
		double vf = atan2(oppositeSide, 1.);
		_vfov = (M_PI_2 - vf - (13. / 180. * M_PI)) * 10. / 9.;

		double warpVfov = 155. / 180. * M_PI;
		double hypV = 768. / 2. / sin(warpVfov / 2.);
		double oppHTot = tan(hfov / 2.) * 16. / 320.;
		_helperValue = 2048 * 65536 / (2. * M_PI);

		for (int i = 0; i < 31; i++) {
			double oppH = (i - 15) * oppHTot;
			double angle = atan2(oppH, 1.);

			_anglesH[i] = angle;
			_hypothenusesH[i] = sqrt(oppH * oppH + 1);

			double oppVTot = hypV * _hypothenusesH[i];
			for (int j = 0; j < 21; j++) {
				double oppV = (j - 20) * oppHTot;

				_oppositeV[j] = oppV;

				double coord = sqrt(oppV * oppV + _hypothenusesH[i] * _hypothenusesH[i]);
				coord = oppVTot / coord;
				coord = coord * 65536;

				_squaresCoords[i][j] = coord;
			}
		}

		_surface.create(640, 480, sourceSurface->format);
		_dirty = true;
		_dirtyCoords = true;
	}

	void updateCoords(int xDelta, int yDelta, bool useOldSpeed) {
		double xDelta1 = xDelta * 0.00025;
		double yDelta1 = yDelta * 0.0002;

		if (useOldSpeed) {
			_xSpeed += xDelta1;
			_ySpeed += yDelta1;
		} else {
			_xSpeed = xDelta1;
			_ySpeed = yDelta1;
		}
		_alpha += _xSpeed;
		_beta += _ySpeed;

		_xSpeed *= 0.4;
		_ySpeed *= 0.6;

		if (_alpha >= 2. * M_PI) {
			_alpha -= 2. * M_PI;
		} else if (_alpha < 0.) {
			_alpha += 2. * M_PI;
		}

		if (useOldSpeed) {
			if (fabs(_xSpeed) < 0.001)
				_xSpeed = 0.0;
			if (fabs(_ySpeed) < 0.001)
				_ySpeed = 0.0;
		}

		if (_beta > 0.9 * _vfov)
			_beta = 0.9 * _vfov;
		else if (_beta < -0.9 * _vfov)
			_beta = -0.9 * _vfov;

		_dirtyCoords = true;
		updateImageCoords();
	}

	bool hasSpeed() const {
		return _xSpeed != 0. || _ySpeed != 0.;
	}

	double getAlpha() const { return _alpha; }
	double getBeta() const { return _beta; }

	void setViewAngles(double alpha, double beta) {
		_alpha = alpha;
		_beta = beta;

		while (_alpha >= 2. * M_PI)
			_alpha -= 2. * M_PI;
		while (_alpha < 0.)
			_alpha += 2. * M_PI;

		if (_beta > 0.9 * _vfov)
			_beta = 0.9 * _vfov;
		else if (_beta < -0.9 * _vfov)
			_beta = -0.9 * _vfov;

		_xSpeed = 0.0;
		_ySpeed = 0.0;
		_dirtyCoords = true;
		updateImageCoords();
	}

	void setPanoramaCenterX(int panoramaX) {
		while (panoramaX < 0)
			panoramaX += 2048;
		panoramaX %= 2048;

		setViewAngles((2048.0 - (double)panoramaX) * (2.0 * M_PI) / 2048.0, _beta);
	}

	Common::Point mapMouseCoords(const Common::Point &mouse) {
		Common::Point pt;

		if (_dirtyCoords)
			updateImageCoords();

		int smallX = mouse.x & 0xf;
		int squareX = mouse.x >> 4;
		int smallY = mouse.y & 0xf;
		int squareY = mouse.y >> 4;

		uint off = 82 * squareY + 2 * squareX;

		pt.x = ((_imageCoords[off + 2] +
		         smallY * ((_imageCoords[off + 84] - _imageCoords[off + 2]) >> 4) +
		         (smallX * smallY) * ((_imageCoords[off + 86] - _imageCoords[off + 84]) >> 8) +
		         (smallX * (16 - smallY)) * ((_imageCoords[off + 4] - _imageCoords[off + 2]) >> 8))
		        & 0x07ff0000) >> 16;
		pt.y = (_imageCoords[off + 3] +
		        smallY * ((_imageCoords[off + 85] - _imageCoords[off + 3]) >> 4) +
		        (smallX * smallY) * ((_imageCoords[off + 87] - _imageCoords[off + 85]) >> 8) +
		        (smallX * (16 - smallY)) * ((_imageCoords[off + 5] - _imageCoords[off + 3]) >> 8)) >> 16;

		return pt;
	}

	const Graphics::Surface *getSurface() {
		if (!_sourceSurface)
			return nullptr;

		if (_dirtyCoords)
			updateImageCoords();

		if (_dirty)
			render();

		return &_surface;
	}

private:
	void updateImageCoords() {
		if (!_dirtyCoords)
			return;

		double tmp = (2048 * 65536) - 2048 * 65536 / (2. * M_PI) * _alpha;

		uint k = 0;
		for (uint i = 0; i < 31; i++) {
			double v11 = _anglesH[i] + _beta;
			double v26 = sin(v11);
			double v25 = cos(v11) * _hypothenusesH[i];

			uint offset = 80;
			uint j;
			for (j = 0; j < 20; j++) {
				double v16 = atan2(_oppositeV[j], v25);
				double v17 = v16 * _helperValue;
				double v18 = (384 * 65536) - _squaresCoords[i][j] * v26;

				k += 2;
				_imageCoords[k + 0] = (int)(tmp + v17);
				_imageCoords[k + offset + 0] = (int)(tmp - v17);
				_imageCoords[k + 1] = (int)v18;
				_imageCoords[k + offset + 1] = (int)v18;

				offset -= 4;
			}

			double v19 = atan2(_oppositeV[j], v25);

			k += 2;
			_imageCoords[k + 0] = (int)((2048. * 65536.) - (_alpha - v19) * _helperValue);
			_imageCoords[k + 1] = (int)((384. * 65536.) - _squaresCoords[i][j] * v26);

			k += 40;
		}

		_dirtyCoords = false;
		_dirty = true;
	}

	void render() {
		const int bpp = _sourceSurface->format.bytesPerPixel;
		if (bpp != 2 && bpp != 4)
			return;

		uint off = 2;
		byte *dst = (byte *)_surface.getBasePtr(0, 0);
		const byte *src = (const byte *)_sourceSurface->getBasePtr(0, 0);
		const uint dstPitch = _surface.pitch;

		for (uint i = 0; i < 30; i++) {
			for (uint j = 0; j < 40; j++) {
				int x1 = (_imageCoords[off + 2] - _imageCoords[off + 0]) >> 4;
				int y1 = (_imageCoords[off + 3] - _imageCoords[off + 1]) >> 4;
				int x1_ = (_imageCoords[off + 82 + 2] - _imageCoords[off + 82 + 0]) >> 4;
				int y1_ = (_imageCoords[off + 82 + 3] - _imageCoords[off + 82 + 1]) >> 4;

				int dx1 = (x1_ - x1) >> 10;
				int dy1 = (y1_ - y1) >> 15;

				y1 >>= 5;

				int dx2 = (_imageCoords[off + 82 + 0] - _imageCoords[off + 0]) >> 4;
				int dy2 = (_imageCoords[off + 82 + 1] - _imageCoords[off + 1]) >> 9;
				int x2 = (((_imageCoords[off + 0] >> 0) * 2) + dx2) >> 1;
				int y2 = (((_imageCoords[off + 1] >> 5) * 2) + dy2) >> 1;

				for (uint y = 0; y < 16; y++) {
					uint px = (x2 * 2 + x1) * 16;
					uint py = (y2 * 2 + y1) / 2;
					uint deltaX = x1 * 32;
					uint deltaY = y1;
					byte *dstLine = dst;

					for (uint x = 0; x < 16; x++) {
						uint srcOff = (py & 0x1ff800) | (px >> 21);
						memcpy(dstLine, src + srcOff * bpp, bpp);
						dstLine += bpp;
						px += deltaX;
						py += deltaY;
					}

					dst += dstPitch;
					x1 += dx1;
					y1 += dy1;
					x2 += dx2;
					y2 += dy2;
				}
				dst -= 16 * dstPitch - 16 * bpp;
				off += 2;
			}
			dst += 15 * dstPitch;
			off += 2;
		}

		_dirty = false;
	}

	double _vfov;
	double _alpha;
	double _beta;
	double _xSpeed;
	double _ySpeed;
	int _imageCoords[2544];
	double _squaresCoords[31][21];
	double _hypothenusesH[31];
	double _anglesH[31];
	double _oppositeV[21];
	double _helperValue;
	bool _dirty;
	bool _dirtyCoords;
	const Graphics::Surface *_sourceSurface;
	Graphics::Surface _surface;
};

} // End of anonymous namespace

int CryOmni3DEngine_Egypt::alphaToPanoramaX(double alpha) const {
	while (alpha >= 2.0 * M_PI)
		alpha -= 2.0 * M_PI;
	while (alpha < 0.0)
		alpha += 2.0 * M_PI;

	const double panoramaX = 2048.0 - alpha * 2048.0 / (2.0 * M_PI);
	int wrapped = (int)panoramaX % 2048;
	if (wrapped < 0)
		wrapped += 2048;
	return wrapped;
}

bool CryOmni3DEngine_Egypt::displayCurrentWarpPreview(const Common::Path &filename) {
	Common::File file;
	if (!file.open(filename)) {
		warning("Egypt: preview failed to open warp %s",
		        filename.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	EgyptPreviewHNMDecoder imageDecoder(g_system->getScreenFormat());
	if (!imageDecoder.loadStream(file)) {
		warning("Egypt: preview failed to decode warp %s",
		        filename.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	if (imageDecoder.hasPalette()) {
		setupPalette(imageDecoder.getPalette().data(), 0, imageDecoder.getPalette().size());
	}

	const Graphics::Surface *frame = imageDecoder.getSurface();
	if (!frame) {
		warning("Egypt: preview got no frame for warp %s",
		        filename.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	// Story entry scenes, visit hubs, and any runtime warp arrivals should land
	// directly in the interactive panorama instead of the temporary crop preview.
	if (_currentScene.name.equalsIgnoreCase("S01") || _currentScene.name.equalsIgnoreCase("S03") ||
	    isEgyptContextName(_currentScene.name) || _pendingWarp.active)
		return displayCurrentWarpRotation(frame);

	double arrivalAlpha = 0.0;
	double arrivalBeta = 0.0;
	bool hasArrivalAngles = false;
	int arrivalPanoramaX = consumeArrivalPanoramaX(arrivalAlpha, arrivalBeta, hasArrivalAngles);
	if (hasArrivalAngles) {
		arrivalPanoramaX = alphaToPanoramaX(arrivalAlpha);
		setRuntimeViewAngles(arrivalAlpha, arrivalBeta, true);
		warning("Egypt: preview aligned %s to script angles alpha=%0.3f beta=%0.3f panorama x=%d",
		        _currentScene.name.c_str(), arrivalAlpha, arrivalBeta, arrivalPanoramaX);
	} else if (arrivalPanoramaX >= 0) {
		warning("Egypt: preview aligned %s to panorama x=%d",
		        _currentScene.name.c_str(), arrivalPanoramaX);
	}

	const int frameWidth = static_cast<int>(frame->w);
	const int frameHeight = static_cast<int>(frame->h);
	const int drawWidth = MIN(frameWidth, 640);
	const int drawHeight = MIN(frameHeight, 480);
	int srcX = MAX(0, (frameWidth - drawWidth) / 2);
	const int srcY = MAX(0, (frameHeight - drawHeight) / 2);
	if (arrivalPanoramaX >= 0 && frameWidth > drawWidth)
		srcX = CLIP<int>(arrivalPanoramaX - drawWidth / 2, 0, frameWidth - drawWidth);

	warning("Egypt: preview displays %dx%d crop at %d,%d from %s",
	        drawWidth, drawHeight, srcX, srcY, _currentScene.warpName.c_str());

	Graphics::Surface previewFrame;
	const Graphics::Surface *displayFrame = frame;
	if (_hasPendingOverlay) {
		previewFrame.copyFrom(*frame);
		applyOverlayToSurface(previewFrame);
		displayFrame = &previewFrame;
	}

	fillSurface(0);
	g_system->copyRectToScreen(displayFrame->getBasePtr(srcX, srcY), displayFrame->pitch,
	                           0, 0, drawWidth, drawHeight);
	g_system->updateScreen();
	previewFrame.free();
	if (_pendingWarp.active)
		logRuntimeWarp(_pendingRuntimeMatchedCentrage, _pendingRuntimeResolved, hasArrivalAngles);
	clearPendingWarpRequest();
	g_system->delayMillis(750);
	return true;
}

bool CryOmni3DEngine_Egypt::displayCurrentWarpRotation(const Graphics::Surface *frame) {
	Graphics::Surface overlaidFrame;
	const Graphics::Surface *sourceFrame = frame;
	if (_hasPendingOverlay) {
		overlaidFrame.copyFrom(*frame);
		applyOverlayToSurface(overlaidFrame);
		sourceFrame = &overlaidFrame;
		warning("Egypt: applied %u overlay pixels to warp panorama for %s",
		        (uint)_pendingOverlayPixels.size(), _currentScene.name.c_str());
	}

	EgyptWarpRenderer renderer;
	renderer.init(75. / 180. * M_PI, sourceFrame);
	Graphics::ManagedSurface compositedFrame(640, 480, g_system->getScreenFormat());

	double arrivalAlpha = 0.0;
	double arrivalBeta = 0.0;
	bool hasArrivalAngles = false;
	const int arrivalPanoramaX = consumeArrivalPanoramaX(arrivalAlpha, arrivalBeta, hasArrivalAngles);
	if (hasArrivalAngles) {
		renderer.setViewAngles(arrivalAlpha, arrivalBeta);
		setRuntimeViewAngles(arrivalAlpha, arrivalBeta, true);
		warning("Egypt: aligned %s to script angles alpha=%0.3f beta=%0.3f",
		        _currentScene.name.c_str(), arrivalAlpha, arrivalBeta);
	} else if (arrivalPanoramaX >= 0) {
		renderer.setPanoramaCenterX(arrivalPanoramaX);
		warning("Egypt: aligned %s to panorama x=%d",
		        _currentScene.name.c_str(), arrivalPanoramaX);
	}
	if (_pendingWarp.active)
		logRuntimeWarp(_pendingRuntimeMatchedCentrage, _pendingRuntimeResolved, hasArrivalAngles);
	clearPendingWarpRequest();

	const uint availableCursors = _interfaceSprites.size();
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

	warning("Egypt: interactive rotation enabled for %s, click or press space to continue",
	        _currentScene.name.c_str());

	bool exitRotation = false;
	bool firstDraw = true;
	while (!shouldAbort() && !exitRotation) {
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
		if (hoveredZone)
			movingCursor = getCursorFrameForZone(*hoveredZone);

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
					warning("Egypt: hover %s mouse=%d,%d warp=%d,%d zone=%03u action=%u cursor=%u target=%s",
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
			const Graphics::Surface *result = renderer.getSurface();
			if (!result)
				return;

			compositedFrame.blitFrom(*result);
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

		// Toolbar: triggered when mouse enters the bottom 48px strip (EXE toolbar at y=432 = 480-48)
		if (!exitRotation && getCurrentMouseButton() == 0 && mouse.y >= 432) {
			if (displayToolbar(&compositedFrame.rawSurface())) {
				exitRotation = true;
			} else {
				// Toolbar dismissed without navigation — redraw scene to restore the bottom strip
				firstDraw = true;
			}
			waitMouseRelease();
			clearKeys();
			setInterfaceCursor(getDefaultCursorFrame());
			showMouse(true);
		}

		g_system->delayMillis(10);
	}

	waitMouseRelease();
	clearKeys();
	showMouse(false);
	overlaidFrame.free();
	return true;
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
