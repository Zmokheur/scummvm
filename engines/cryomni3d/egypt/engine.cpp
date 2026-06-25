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

#include "common/archive.h"
#include "common/endian.h"
#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"

#include "engines/util.h"

#include "cryomni3d/egypt/engine.h"
#include "cryomni3d/image/hnm.h"

#include "graphics/cursorman.h"
#include "graphics/palette.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

namespace CryOmni3D {
namespace Egypt {

namespace {

static const Graphics::PixelFormat kEgyptSpriteFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);

enum EgyptCursorFrame {
	kEgyptCursorNav0 = 0,
	kEgyptCursorNav1 = 1,
	kEgyptCursorNav2 = 2,
	kEgyptCursorNav3 = 3,
	kEgyptCursorNav4 = 4,
	kEgyptCursorNav5 = 5,
	kEgyptCursorNav6 = 6,
	kEgyptCursorNav7 = 7,
	kEgyptCursorBusy = 13
};

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

CryOmni3DEngine_Egypt::CryOmni3DEngine_Egypt(OSystem *syst,
		const CryOmni3DGameDescription *gamedesc) : CryOmni3DEngine(syst, gamedesc) {
}

CryOmni3DEngine_Egypt::~CryOmni3DEngine_Egypt() {
	for (Common::Array<EgyptInterfaceSprite *>::iterator it = _interfaceSprites.begin();
	     it != _interfaceSprites.end(); ++it) {
		delete *it;
	}
}

void CryOmni3DEngine_Egypt::initializePath(const Common::FSNode &gamePath) {
	SearchMan.addDirectory(gamePath, 0, 5, false);
	SearchMan.addSubDirectoryMatching(gamePath, "egypte", 0, 5, false);
	SearchMan.addSubDirectoryMatching(gamePath, "sprite", 0, 5, false);
	SearchMan.addSubDirectoryMatching(gamePath, "sprite/level1", 0, 5, false);
	SearchMan.addSubDirectoryMatching(gamePath, "ref", 0, 5, false);
	SearchMan.addSubDirectoryMatching(gamePath, "ref/fr", 0, 5, false);
	SearchMan.addSubDirectoryMatching(gamePath, "warp", 0, 5, false);
}

Common::Error CryOmni3DEngine_Egypt::run() {
	CryOmni3DEngine::run();

	const Graphics::PixelFormat egyptFormat = Graphics::PixelFormat::createFormatRGBA32();
	initGraphics(640, 480, &egyptFormat);
	warning("Egypt: current screen format uses %d byte(s) per pixel",
	        g_system->getScreenFormat().bytesPerPixel);
	fillSurface(0);
	syncSoundSettings();
	setupSprites();

	loadScene("S01");
	executePrototypeSceneLogic();

	return Common::kNoError;
}

void CryOmni3DEngine_Egypt::setupSprites() {
	for (Common::Array<EgyptInterfaceSprite *>::iterator it = _interfaceSprites.begin();
	     it != _interfaceSprites.end(); ++it) {
		delete *it;
	}
	_interfaceSprites.clear();

	if (!loadInterfaceSprites(Common::Path("SPRITE/INTERFAC.SPR"))) {
		warning("Egypt: failed to load interface sprites from INTERFAC.SPR");
		return;
	}

	warning("Egypt: loaded %u interface sprite(s) from INTERFAC.SPR", _interfaceSprites.size());
	if (!_interfaceSprites.empty())
		setInterfaceCursor(kEgyptCursorBusy);
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

		sprite->hotspotX = 0;
		sprite->hotspotY = 0;
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

void CryOmni3DEngine_Egypt::loadScene(const Common::String &sceneName) {
	_pendingWarpTarget.clear();

	Common::Path scenePath(Common::String::format("SPRITE/LEVEL1/%s.DEF", sceneName.c_str()));
	parseSceneDefinition(scenePath, sceneName);

	EgyptWarpHeader warpHeader;
	Common::Path warpPath(Common::String::format("WARP/%s", _currentScene.warpName.c_str()));
	if (inspectWarpHeader(warpPath, warpHeader)) {
		warning("Egypt: warp %s tag=%s size=%ux%u audioFlags=%u bpp=%u frameSize=%u firstChunk=%s/%u",
		        _currentScene.warpName.c_str(), warpHeader.tag.c_str(), warpHeader.width, warpHeader.height,
		        warpHeader.audioFlags, warpHeader.bpp, warpHeader.frameSize,
		        warpHeader.firstChunkTag.c_str(), warpHeader.firstChunkSize);
	}

	warning("Egypt: scene %s uses warp %s and has %u zone(s)",
	        _currentScene.name.c_str(), _currentScene.warpName.c_str(), _currentScene.zones.size());
	collectInitialActiveZones();
	displayCurrentWarpPreview(warpPath);
}

void CryOmni3DEngine_Egypt::parseSceneDefinition(const Common::Path &filename, const Common::String &sceneName) {
	Common::File file;
	if (!file.open(filename)) {
		warning("Egypt: failed to open scene definition %s",
		        filename.toString(Common::Path::kNativeSeparator).c_str());
		return;
	}

	_currentScene.name = sceneName;
	_currentScene.warpName = sceneName + "_24.HNM";
	_currentScene.zones.clear();
	_currentScene.scriptLines.clear();
	_currentScene.activeZones.clear();

	while (!file.eos()) {
		Common::String line = file.readLine();
		line.trim();
		if (line.empty())
			continue;

		uint zoneId = 0;
		uint left = 0;
		uint top = 0;
		uint right = 0;
		uint bottom = 0;
		uint actionId = 0;
		char commandBuffer[512];
		commandBuffer[0] = '\0';

		if (sscanf(line.c_str(), "Zone-%u %u-%u-%u-%u %u:%511[^\r\n]",
		           &zoneId, &left, &top, &right, &bottom, &actionId, commandBuffer) == 7) {
			EgyptZone zone;
			zone.id = zoneId;
			zone.left = left;
			zone.top = top;
			zone.right = right;
			zone.bottom = bottom;
			zone.actionId = actionId;
			zone.commandName.clear();
			zone.command = Common::String(commandBuffer);
			zone.command.trim();
			zone.targetWarp.clear();
			parseZoneCommand(zone);
			_currentScene.zones.push_back(zone);

			warning("Egypt: zone %03u bounds=%u-%u-%u-%u action=%u command=%s target=%s",
			        zone.id, zone.left, zone.top, zone.right, zone.bottom,
			        zone.actionId, zone.command.c_str(), zone.targetWarp.c_str());
			continue;
		}

		_currentScene.scriptLines.push_back(line);
		logScriptLine(line);
	}
}

bool CryOmni3DEngine_Egypt::inspectWarpHeader(const Common::Path &filename, EgyptWarpHeader &header) {
	Common::File file;
	if (!file.open(filename)) {
		warning("Egypt: failed to open warp %s",
		        filename.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	char tag[5];
	tag[0] = (char)file.readByte();
	tag[1] = (char)file.readByte();
	tag[2] = (char)file.readByte();
	tag[3] = (char)file.readByte();
	tag[4] = '\0';
	header.tag = tag;

	file.skip(2);
	header.audioFlags = file.readByte();
	header.bpp = file.readByte();
	header.width = file.readUint16LE();
	header.height = file.readUint16LE();

	// After width/height, HNM6 stores filesize, frame count, one unknown dword,
	// speed, maxbuffer, buffer_size, then two 16-byte strings.
	file.skip(52);

	header.frameSize = file.readUint32LE();
	header.firstChunkSize = file.readUint32LE();

	char chunkTag[3];
	chunkTag[0] = (char)file.readByte();
	chunkTag[1] = (char)file.readByte();
	chunkTag[2] = '\0';
	header.firstChunkTag = chunkTag;

	return true;
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

	if (_currentScene.name.equalsIgnoreCase("S01") || _currentScene.name.equalsIgnoreCase("S03"))
		return displayCurrentWarpRotation(frame);

	const int frameWidth = static_cast<int>(frame->w);
	const int frameHeight = static_cast<int>(frame->h);
	const int drawWidth = MIN(frameWidth, 640);
	const int drawHeight = MIN(frameHeight, 480);
	const int srcX = MAX(0, (frameWidth - drawWidth) / 2);
	const int srcY = MAX(0, (frameHeight - drawHeight) / 2);

	warning("Egypt: preview displays %dx%d crop at %d,%d from %s",
	        drawWidth, drawHeight, srcX, srcY, _currentScene.warpName.c_str());

	fillSurface(0);
	g_system->copyRectToScreen(frame->getBasePtr(srcX, srcY), frame->pitch, 0, 0, drawWidth, drawHeight);
	g_system->updateScreen();
	g_system->delayMillis(750);
	return true;
}

bool CryOmni3DEngine_Egypt::displayCurrentWarpRotation(const Graphics::Surface *frame) {
	EgyptWarpRenderer renderer;
	renderer.init(75. / 180. * M_PI, frame);

	const uint availableCursors = _interfaceSprites.size();
	auto setRotationCursor = [&](uint cursorId) {
		if (setInterfaceCursor(cursorId))
			return;

		const uint fallbackCursor = (availableCursors > kEgyptCursorBusy) ? kEgyptCursorBusy : 0;
		if (availableCursors > 0) {
			warning("Egypt: cursor %u unavailable, fallback to cursor %u (%u available)",
			        cursorId, fallbackCursor, availableCursors);
			setInterfaceCursor(fallbackCursor);
		}
	};

	clearKeys();
	waitMouseRelease();
	showMouse(true);
	setRotationCursor(kEgyptCursorBusy);

	warning("Egypt: interactive rotation enabled for %s, click or press space to continue",
	        _currentScene.name.c_str());

	bool exitRotation = false;
	bool firstDraw = true;
	while (!shouldAbort() && !exitRotation) {
		pollEvents();

		Common::Point mouse = getMousePos();
		int xDelta = 0;
		int yDelta = 0;
		uint movingCursor = kEgyptCursorBusy;

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

		Common::KeyState key = getNextKey();
		if (key.keycode == Common::KEYCODE_SPACE || key.keycode == Common::KEYCODE_RETURN ||
		    key.keycode == Common::KEYCODE_ESCAPE || getCurrentMouseButton() == 1) {
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

		if (firstDraw || xDelta != 0 || yDelta != 0 || renderer.hasSpeed()) {
			renderer.updateCoords(xDelta, -yDelta, true);
			const Graphics::Surface *result = renderer.getSurface();
			if (result) {
				g_system->copyRectToScreen(result->getPixels(), result->pitch, 0, 0, result->w, result->h);
				g_system->updateScreen();
			}
			firstDraw = false;
		} else {
			g_system->updateScreen();
		}

		g_system->delayMillis(10);
	}

	waitMouseRelease();
	clearKeys();
	showMouse(false);
	return true;
}

void CryOmni3DEngine_Egypt::parseZoneCommand(EgyptZone &zone) {
	Common::StringTokenizer tokenizer(zone.command);
	if (tokenizer.empty())
		return;

	zone.commandName = tokenizer.nextToken();
	while (!tokenizer.empty()) {
		Common::String token = tokenizer.nextToken();
		if (token.hasPrefixIgnoreCase("WARP:")) {
			zone.targetWarp = token.substr(5);
			return;
		}
	}
}

void CryOmni3DEngine_Egypt::collectInitialActiveZones() {
	_currentScene.activeZones.clear();
	bool scriptProducedState = runPrototypeWarpScript();

	if (!scriptProducedState) {
		for (Common::Array<EgyptZone>::const_iterator it = _currentScene.zones.begin();
		     it != _currentScene.zones.end(); ++it) {
			if (it->left != 0 || it->top != 0 || it->right != 0 || it->bottom != 0)
				_currentScene.activeZones.push_back(it->id);
		}

		warning("Egypt: no active zone from script for %s, fallback to geometry-based prototype",
		        _currentScene.name.c_str());
	}

	Common::String activeList;
	for (Common::Array<uint>::const_iterator it = _currentScene.activeZones.begin();
	     it != _currentScene.activeZones.end(); ++it) {
		if (!activeList.empty())
			activeList += ",";
		activeList += Common::String::format("%u", *it);
	}

	warning("Egypt: initial active zones for %s = [%s]",
	        _currentScene.name.c_str(), activeList.c_str());
}

bool CryOmni3DEngine_Egypt::runPrototypeWarpScript() {
	Common::Array<Common::String> blockLines;
	bool inWarpBlock = false;

	for (Common::Array<Common::String>::const_iterator it = _currentScene.scriptLines.begin();
	     it != _currentScene.scriptLines.end(); ++it) {
		if (it->equalsIgnoreCase("warpinit")) {
			inWarpBlock = true;
			blockLines.push_back(*it);
			continue;
		}

		if (!inWarpBlock)
			continue;

		blockLines.push_back(*it);
		if (it->equalsIgnoreCase("endwarp"))
			break;
	}

	if (blockLines.empty()) {
		warning("Egypt: no warp script block found for %s", _currentScene.name.c_str());
		return false;
	}

	return executeScriptBlock(blockLines);
}

bool CryOmni3DEngine_Egypt::executeScriptBlock(const Common::Array<Common::String> &lines) {
	Common::HashMap<Common::String, uint> labels;
	bool producedState = false;

	_scriptVariables["zoneclic"] = getPrototypeZoneClick();
	warning("Egypt: prototype zoneclic=%d for scene %s",
	        _scriptVariables["zoneclic"], _currentScene.name.c_str());

	for (uint i = 0; i < lines.size(); ++i) {
		if (lines[i].hasSuffix(":")) {
			Common::String label = lines[i];
			label.deleteLastChar();
			labels[label] = i;
		}
	}

	for (uint pc = 0; pc < lines.size(); ++pc) {
		Common::String line = lines[pc];
		line.trim();
		if (line.empty() || line.equalsIgnoreCase("warpinit") || line.equalsIgnoreCase("endinit") ||
		    line.equalsIgnoreCase("endwarp") || line.hasPrefixIgnoreCase("centrage") ||
		    line.hasPrefixIgnoreCase("music") || line.hasPrefixIgnoreCase("stopmusic") ||
		    line.hasPrefixIgnoreCase("dialoguer") || line.hasSuffix(":")) {
			continue;
		}

		if (line.hasPrefixIgnoreCase("zoneactive ")) {
			Common::String value = line.substr(11);
			value.trim();
			uint zoneId = (uint)atoi(value.c_str());
			bool alreadyActive = false;
			for (Common::Array<uint>::const_iterator activeIt = _currentScene.activeZones.begin();
			     activeIt != _currentScene.activeZones.end(); ++activeIt) {
				if (*activeIt == zoneId) {
					alreadyActive = true;
					break;
				}
			}
			if (zoneId != 0 && !alreadyActive) {
				_currentScene.activeZones.push_back(zoneId);
				producedState = true;
			}
			continue;
		}

		if (line.hasPrefixIgnoreCase("let ")) {
			setScriptVariable(line.substr(4));
			producedState = true;
			continue;
		}

		if (line.hasPrefixIgnoreCase("aller_warp ")) {
			Common::String value = line.substr(11);
			value.trim();
			if (value.hasSuffix("!"))
				value.deleteLastChar();
			if (queuePrototypeSceneChange((uint)atoi(value.c_str()), "aller_warp"))
				producedState = true;
			continue;
		}

		if (line.hasPrefixIgnoreCase("aller_hnm_warp ")) {
			Common::String value = line.substr(15);
			value.trim();
			if (value.hasSuffix("!"))
				value.deleteLastChar();
			if (queuePrototypeSceneChange((uint)atoi(value.c_str()), "aller_hnm_warp"))
				producedState = true;
			continue;
		}

		if (line.hasPrefixIgnoreCase("if ")) {
			Common::String expression = line.substr(3);
			int gotoPos = expression.find(" goto ");
			if (gotoPos >= 0) {
				Common::String condition = expression.substr(0, gotoPos);
				Common::String label = expression.substr(gotoPos + 6);
				label.trim();
				if (label.hasSuffix("!"))
					label.deleteLastChar();

				if (evaluateScriptCondition(condition) && labels.contains(label)) {
					pc = labels[label];
				}
			}
			continue;
		}

		if (line.hasPrefixIgnoreCase("goto ")) {
			Common::String label = line.substr(5);
			label.trim();
			if (label.hasSuffix("!"))
				label.deleteLastChar();
			if (labels.contains(label))
				pc = labels[label];
			continue;
		}
	}

	return producedState;
}

int CryOmni3DEngine_Egypt::getPrototypeZoneClick() const {
	if (_currentScene.name.equalsIgnoreCase("S01") && getScriptVariableValue("FlagEntreeS01") == 0)
		return 1;

	return 0;
}

bool CryOmni3DEngine_Egypt::queuePrototypeSceneChange(uint zoneId, const char *reason) {
	const EgyptZone *zone = findZoneById(zoneId);
	if (!zone) {
		warning("Egypt: %s references unknown zone %u in scene %s",
		        reason, zoneId, _currentScene.name.c_str());
		return false;
	}

	if (zone->targetWarp.empty()) {
		warning("Egypt: %s references zone %u without target warp in scene %s",
		        reason, zoneId, _currentScene.name.c_str());
		return false;
	}

	_pendingWarpTarget = zone->targetWarp;
	warning("Egypt: prototype queued %s via zone %03u from %s to %s",
	        reason, zone->id, _currentScene.name.c_str(), _pendingWarpTarget.c_str());
	return true;
}

bool CryOmni3DEngine_Egypt::evaluateScriptCondition(const Common::String &expression) const {
	Common::String condition = expression;
	condition.trim();

	int operatorPos = condition.find("!=");
	if (operatorPos >= 0) {
		Common::String left = condition.substr(0, operatorPos);
		Common::String right = condition.substr(operatorPos + 2);
		left.trim();
		right.trim();
		return getScriptVariableValue(left) != atoi(right.c_str());
	}

	operatorPos = condition.find('=');
	if (operatorPos >= 0) {
		Common::String left = condition.substr(0, operatorPos);
		Common::String right = condition.substr(operatorPos + 1);
		left.trim();
		right.trim();
		return getScriptVariableValue(left) == atoi(right.c_str());
	}

	return false;
}

int CryOmni3DEngine_Egypt::getScriptVariableValue(const Common::String &name) const {
	Common::HashMap<Common::String, int>::const_iterator it = _scriptVariables.find(name);
	if (it != _scriptVariables.end())
		return it->_value;

	return 0;
}

void CryOmni3DEngine_Egypt::setScriptVariable(const Common::String &assignment) {
	int separatorPos = assignment.find('=');
	if (separatorPos < 0)
		return;

	Common::String name = assignment.substr(0, separatorPos);
	Common::String value = assignment.substr(separatorPos + 1);
	name.trim();
	value.trim();
	_scriptVariables[name] = atoi(value.c_str());
	warning("Egypt: script variable %s=%d", name.c_str(), _scriptVariables[name]);
}

bool CryOmni3DEngine_Egypt::executePrototypeSceneLogic() {
	if (!_pendingWarpTarget.empty()) {
		Common::String targetScene = _pendingWarpTarget;
		warning("Egypt: prototype executes scripted transition from %s to %s",
		        _currentScene.name.c_str(), targetScene.c_str());
		loadScene(targetScene);
		return true;
	}

	for (Common::Array<uint>::const_iterator it = _currentScene.activeZones.begin();
	     it != _currentScene.activeZones.end(); ++it) {
		const EgyptZone *zone = findZoneById(*it);
		if (!zone)
			continue;

		if (zone->commandName.equalsIgnoreCase("ALLER_WARP") && !zone->targetWarp.empty()) {
			warning("Egypt: prototype executes zone %03u from %s to %s",
			        zone->id, _currentScene.name.c_str(), zone->targetWarp.c_str());
			loadScene(zone->targetWarp);
			return true;
		}
	}

	warning("Egypt: no executable ALLER_WARP found in active zones for %s",
	        _currentScene.name.c_str());
	return false;
}

const EgyptZone *CryOmni3DEngine_Egypt::findZoneById(uint zoneId) const {
	for (Common::Array<EgyptZone>::const_iterator it = _currentScene.zones.begin();
	     it != _currentScene.zones.end(); ++it) {
		if (it->id == zoneId)
			return it;
	}

	return nullptr;
}

void CryOmni3DEngine_Egypt::logScriptLine(const Common::String &line) const {
	if (line.equalsIgnoreCase("warpinit") || line.equalsIgnoreCase("endwarp") ||
	    line.equalsIgnoreCase("endinit") || line.hasSuffix(":") ||
	    line.hasPrefixIgnoreCase("centrage") || line.hasPrefixIgnoreCase("music") ||
	    line.hasPrefixIgnoreCase("stopmusic") || line.hasPrefixIgnoreCase("if ") ||
	    line.hasPrefixIgnoreCase("let ") || line.hasPrefixIgnoreCase("goto ") ||
	    line.hasPrefixIgnoreCase("aller_warp") || line.hasPrefixIgnoreCase("aller_hnm_warp") ||
	    line.hasPrefixIgnoreCase("dialoguer") || line.hasPrefixIgnoreCase("zoneactive")) {
		warning("Egypt: script %s", line.c_str());
	}
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
