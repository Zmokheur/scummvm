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

#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"

#include "engines/util.h"

#include "cryomni3d/egypt/engine.h"

#include "graphics/pixelformat.h"
#include "image/codecs/hnm.h"

namespace CryOmni3D {
namespace Egypt {

CryOmni3DEngine_Egypt::CryOmni3DEngine_Egypt(OSystem *syst,
		const CryOmni3DGameDescription *gamedesc) : CryOmni3DEngine(syst, gamedesc),
		_currentContextName("NUIT"),
		_lastHoveredZoneId(uint(-1)) {
	_pendingWarp.active = false;
}

CryOmni3DEngine_Egypt::~CryOmni3DEngine_Egypt() {
	for (Common::Array<EgyptInterfaceSprite *>::iterator it = _interfaceSprites.begin();
	     it != _interfaceSprites.end(); ++it) {
		delete *it;
	}
	_crossFadeOldScreen.free();
}

void CryOmni3DEngine_Egypt::initializePath(const Common::FSNode &gamePath) {
	SearchMan.addDirectory(gamePath, 0, 5, false);
	SearchMan.addSubDirectoryMatching(gamePath, "egypte", 0, 5, false);
	SearchMan.addSubDirectoryMatching(gamePath, "sprite", 0, 5, false);
	for (int level = 1; level <= 6; ++level) {
		SearchMan.addSubDirectoryMatching(gamePath, Common::String::format("sprite/level%d", level), 0, 5, false);
		SearchMan.addSubDirectoryMatching(gamePath, Common::String::format("sprite/level%d/all", level), 0, 5, false);
	}
	SearchMan.addSubDirectoryMatching(gamePath, "ref", 0, 5, false);
	SearchMan.addSubDirectoryMatching(gamePath, "ref/fr", 0, 5, false);
	SearchMan.addSubDirectoryMatching(gamePath, "hnm", 0, 5, false);
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
	loadSymbolDefinitions(Common::Path("REF/FR/EGYPTE.DEF"));
	loadMessageLabels();
	setupSprites();

	playStartupLogoIfPresent();

	while (!shouldAbort()) {
		EgyptStartupMode nextMode = showMainMenu();
		if (nextMode == EgyptStartupMode::kQuit)
			break;

		Common::String sceneName;
		switch (nextMode) {
		case EgyptStartupMode::kStory:
			sceneName = startStoryModePrototype();
			break;
		case EgyptStartupMode::kVisit:
			sceneName = startVisitMode();
			break;
		case EgyptStartupMode::kDocumentation:
			startDocumentationMode();
			break;
		case EgyptStartupMode::kDebugLevel1:
			sceneName = startDebugLevel(1, "S00");
			break;
		case EgyptStartupMode::kDebugLevel2:
			sceneName = startDebugLevel(2, "D01");
			break;
		case EgyptStartupMode::kDebugLevel3:
			sceneName = startDebugLevel(3, "A02");
			break;
		case EgyptStartupMode::kDebugLevel4:
			sceneName = startDebugLevel(4, "N01A");
			break;
		case EgyptStartupMode::kDebugLevel5:
			sceneName = startDebugLevel(5, "M01");
			break;
		case EgyptStartupMode::kDebugLevel6:
			sceneName = startDebugLevel(6, "K43");
			break;
		case EgyptStartupMode::kMainMenu:
		case EgyptStartupMode::kQuit:
			break;
		}

		while (!shouldAbort() && !sceneName.empty()) {
			loadScene(sceneName);
			if (_pendingWarpTarget.empty())
				break;
			sceneName = _pendingWarpTarget;
		}
	}

	return Common::kNoError;
}

bool CryOmni3DEngine_Egypt::loadSymbolDefinitions(const Common::Path &filename) {
	Common::File file;
	if (!file.open(filename)) {
		warning("Egypt: failed to open symbol definition file %s",
		        filename.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	_scriptConstants.clear();

	while (!file.eos()) {
		Common::String line = file.readLine();
		line.trim();
		if (line.empty())
			continue;

		char kind[32];
		char name[128];
		int value = 0;
		if (sscanf(line.c_str(), "%31s %127s %d", kind, name, &value) != 3)
			continue;

		if (scumm_stricmp(kind, "variable") == 0) {
			_scriptConstants[Common::String(name)] = value;
		} else if (scumm_stricmp(kind, "objet") == 0) {
			_scriptConstants[Common::String("Objet") + name] = value;
		} else {
			continue;
		}
	}

	warning("Egypt: loaded %u script constant(s) from %s",
	        _scriptConstants.size(), filename.toString(Common::Path::kNativeSeparator).c_str());
	return true;
}

Common::Path CryOmni3DEngine_Egypt::resolveSceneDefinitionPath(const Common::String &sceneName) const {
	Common::String normalizedName = sceneName;
	normalizedName.replace('\\', '/');

	// EXE order: ref\FR\ first (0x411099), then sprite\Level%d\ (0x41112e)
	Common::Path refPath(Common::String::format("REF/FR/%s.DEF", normalizedName.c_str()));
	if (Common::File::exists(refPath))
		return refPath;

	const int currentLevel = getScriptVariableValue("Level");
	if (currentLevel >= 1 && currentLevel <= 6) {
		Common::Path levelDefPath(Common::String::format("SPRITE/LEVEL%d/%s.DEF",
		                                                 currentLevel, normalizedName.c_str()));
		if (Common::File::exists(levelDefPath))
			return levelDefPath;
	}

	for (int level = 1; level <= 6; ++level) {
		Common::Path levelDefPath(Common::String::format("SPRITE/LEVEL%d/%s.DEF",
		                                                 level, normalizedName.c_str()));
		if (Common::File::exists(levelDefPath))
			return levelDefPath;
	}

	return Common::Path(Common::String::format("SPRITE/LEVEL1/%s.DEF", normalizedName.c_str()));
}

void CryOmni3DEngine_Egypt::loadScene(const Common::String &sceneName) {
	_pendingWarpTarget.clear();
	_lastHoveredZoneId = uint(-1);
	_pendingRuntimeArrivalPrepared = false;
	_pendingRuntimeMatchedCentrage.clear();
	_pendingRuntimeResolved = EgyptResolvedCentrage();
	_sceneOverlayData.clear();
	_sceneOverlayCatalog.clear();
	_pendingOverlayPixels.clear();
	_hasPendingOverlay = false;
	_overlayDirty = false;
	_sceneSprPixels.clear();
	_sceneSprDirty = false;

	if (_pendingWarp.viaHnm && !_pendingWarp.hnmName.empty())
		executeHnmSequence(_pendingWarp.hnmName);

	Common::Path scenePath = resolveSceneDefinitionPath(sceneName);
	parseSceneDefinition(scenePath, sceneName);

	if (!_pendingWarp.toContext.empty())
		_currentContextName = _pendingWarp.toContext;
	else if (isEgyptContextName(sceneName))
		_currentContextName = sceneName;

	_currentScene.contextName = _currentContextName;
	warning("Egypt: current context for %s is %s",
	        _currentScene.name.c_str(), _currentScene.contextName.c_str());
	prepareRuntimeArrivalView();

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
	_currentSceneAssets = detectSceneAssets(sceneName, getScriptVariableValue("Level"));
	resetScriptTimer();
	runSceneStartup();

	_hasCrossFadeOldScreen = false;
	if (_pendingWarp.active) {
		Graphics::Surface *screen = g_system->lockScreen();
		if (screen) {
			_crossFadeOldScreen.free();
			_crossFadeOldScreen.copyFrom(*screen);
			_hasCrossFadeOldScreen = true;
			g_system->unlockScreen();
		}
	}

	displayCurrentWarpPreview(warpPath);
}


void CryOmni3DEngine_Egypt::playHnmFile(const Common::Path &path) {
	Common::File file;
	if (!file.open(path)) {
		warning("Egypt: failed to open HNS file %s",
		        path.toString(Common::Path::kNativeSeparator).c_str());
		return;
	}

	// HNM6 header is 64 bytes total
	if (file.readUint32BE() != MKTAG('H', 'N', 'M', '6')) {
		warning("Egypt: not an HNM6 file: %s",
		        path.toString(Common::Path::kNativeSeparator).c_str());
		return;
	}
	file.skip(4);                                   // unknown(2) + audioflag(1) + bpp(1)
	const uint16 width      = file.readUint16LE();  // offset 8
	const uint16 height     = file.readUint16LE();  // offset 10
	file.skip(4);                                   // filesize
	const uint32 numFrames  = file.readUint32LE();  // offset 16
	file.skip(6);                                   // unknown(4) + unknown(2)
	const uint16 speed      = file.readUint16LE();  // offset 26: VBL count per frame
	const uint32 bufferSize = file.readUint32LE();  // offset 28: max frame buffer size
	file.skip(32);                                  // header text / copyright → reaches offset 64

	if (width == 0 || height == 0 || numFrames == 0) {
		warning("Egypt: invalid HNS header in %s",
		        path.toString(Common::Path::kNativeSeparator).c_str());
		return;
	}

	// speed = number of 50Hz VBL ticks per frame; 2 → 40ms/frame (25fps)
	const uint32 msPerFrame  = (speed > 0) ? (speed * 1000u / 50u) : 40u;
	const uint32 actualBufSz = (bufferSize >= 24u) ? bufferSize : 65536u;

	warning("Egypt: playing %s: %ux%u %u frames speed=%u (%ums/frame)",
	        path.toString(Common::Path::kNativeSeparator).c_str(),
	        width, height, numFrames, speed, msPerFrame);

	Image::HNM6Decoder *codec = Image::createHNM6Decoder(
	    width, height, g_system->getScreenFormat(), actualBufSz, true);

	fillSurface(0);

	bool abortPlayback = false;
	for (uint32 i = 0; i < numFrames && !shouldAbort() && !abortPlayback; i++) {
		if (file.eos())
			break;

		const int64 frameStart = file.pos();
		const uint32 frameSize = file.readUint32LE();
		if (frameSize < 4)
			break;
		const int64 frameEnd = frameStart + (int64)frameSize;

		const uint32 frameTimeMs = g_system->getMillis();
		bool videoDecoded = false;

		// Process inner chunks within this frame (video "IX"/"IW" and audio "AA")
		while (!file.eos() && file.pos() < frameEnd - 7) {
			const int64 chunkStart = file.pos();
			const uint32 chunkSize = file.readUint32LE();
			const uint16 chunkTag  = file.readUint16BE();
			file.skip(2);  // padding

			if (chunkSize < 8)
				break;
			const int64 chunkEnd = chunkStart + (int64)chunkSize;

			// 'IX' = 0x4958, 'IW' = 0x4957
			if (!videoDecoded && (chunkTag == 0x4958u || chunkTag == 0x4957u)) {
				const Graphics::Surface *surface = codec->decodeFrame(file);
				if (surface && surface->getPixels()) {
					g_system->copyRectToScreen(surface->getBasePtr(0, 0), surface->pitch,
					                           0, 0, surface->w, surface->h);
					g_system->updateScreen();
				}
				videoDecoded = true;
			}
			// Seek past any remaining chunk bytes (audio "AA" or leftover video data)
			file.seek(chunkEnd);
		}

		// Advance to start of next frame
		file.seek(frameEnd);

		// Frame timing: wait until msPerFrame has elapsed since frame started
		const uint32 elapsed = g_system->getMillis() - frameTimeMs;
		if (elapsed < msPerFrame)
			g_system->delayMillis(msPerFrame - elapsed);

		// Allow user to skip with any key or mouse button
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN ||
			    event.type == Common::EVENT_LBUTTONDOWN ||
			    event.type == Common::EVENT_RBUTTONDOWN) {
				abortPlayback = true;
				break;
			}
		}
	}

	delete codec;
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
