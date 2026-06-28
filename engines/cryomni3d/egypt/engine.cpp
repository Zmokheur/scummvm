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
#include "common/tokenizer.h"

#include "engines/util.h"

#include "audio/mixer.h"

#include "cryomni3d/egypt/engine.h"

#include "graphics/pixelformat.h"
#include "video/hnm_decoder.h"

namespace CryOmni3D {
namespace Egypt {

CryOmni3DEngine_Egypt::CryOmni3DEngine_Egypt(OSystem *syst,
		const CryOmni3DGameDescription *gamedesc) : CryOmni3DEngine(syst, gamedesc),
		_currentContextName("NUIT"),
		_lastHoveredZoneId(uint(-1)) {
	_pendingWarp.active = false;
	_gameVariables.resize(GameVariables::kMax, 0);
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

void CryOmni3DEngine_Egypt::resetGameVariables() {
	Common::fill(_gameVariables.begin(), _gameVariables.end(), 0u);
	_pendingReturnScene.clear();
	_currentContextName = "NUIT";
	_currentViewAnglesAvailable = false;
	_currentViewAlpha = 0.0;
	_currentViewBeta = 0.0;
	_dialogueLevelLoaded = false;
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

		// Always clear EndGame so a previous session ending doesn't pollute the new one.
		_gameVariables[GameVariables::kEndGame] = 0;

		Common::String sceneName;
		switch (nextMode) {
		case EgyptStartupMode::kResume:
			// Re-enter the game without touching variables.
			sceneName = _savedSceneName;
			break;
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
			// Remember whether this scene is the eye-warp destination
			// so we know whether to consume the return scene on exit.
			const bool isEyeScene = !_pendingReturnScene.empty();

			loadScene(sceneName);

			if (!_pendingWarpTarget.empty()) {
				// Navigation from inside this scene (zone click, script, toolbar F-key…).
				// If we were inside an eye scene and the eye scene itself navigated
				// somewhere, discard the return: we follow the new navigation.
				if (isEyeScene)
					_pendingReturnScene.clear();
				sceneName = _pendingWarpTarget;
			} else if (!_pendingReturnScene.empty()) {
				// Eye-warp scene ended without further navigation → return to origin.
				sceneName = _pendingReturnScene;
				_pendingReturnScene.clear();
			} else {
				break;
			}
		}

		// Update resume state based on how the session ended.
		if (_isPlaying) {
			if (getScriptVariableValue("EndGame") != 0) {
				// Game ended normally — no longer resumable.
				_isPlaying = false;
				_savedSceneName.clear();
			} else if (!sceneName.empty()) {
				// Session interrupted (dead-end or future back-to-menu action) — save position.
				_savedSceneName = sceneName;
			}
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
	// DEF zone targets (WARP:ALL\LETTRE etc.) use Windows backslashes.
	// Normalise to forward slashes before any path construction.
	Common::String scene = sceneName;
	scene.replace('\\', '/');

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
	_autoActivationZones.clear();

	if (_pendingWarp.viaHnm && !_pendingWarp.hnmName.empty())
		executeHnmSequence(_pendingWarp.hnmName);

	Common::Path scenePath = resolveSceneDefinitionPath(scene);
	parseSceneDefinition(scenePath, scene);

	if (!_pendingWarp.toContext.empty())
		_currentContextName = _pendingWarp.toContext;
	else if (isEgyptContextName(scene))
		_currentContextName = scene;

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
	_currentSceneAssets = detectSceneAssets(scene, getScriptVariableValue("Level"));
	resetScriptTimer();

	// Snapshot main before warpinit runs; restore it immediately after.
	// In the EXE the held object lives outside the script-variable table, so
	// eye-scene scripts that do "let main=0" cannot affect it.  We replicate
	// this only for eye scenes (identified by _pendingReturnScene being set).
	const int mainBeforeInit = (!_pendingReturnScene.empty()) ? getScriptVariableValue("main") : 0;

	runSceneStartup();

	if (mainBeforeInit != 0)
		_gameVariables[GameVariables::kMain] = mainBeforeInit;

	// EXE (0x407e95): after endinit, tests "endgame"; if non-zero sets state=0 and
	// exits the scene loop.  Check here so FIN (which sets EndGame=1 on its first
	// endinit) returns to the main menu without ever displaying the black TGA.
	if (getScriptVariableValue("EndGame") != 0) {
		warning("Egypt: EndGame set in %s, returning to main menu", _currentScene.name.c_str());
		return;
	}

	// Scenes like MORT use a two-pass deferred pattern: the first endinit sets a
	// counter (tmp 0→1) and takes no action; the second fires aller_hnm_warp/aller_warp.
	// These scenes have no interactive zones and no timer script, so the display loop
	// would never call runEndInit again.  Give them one extra tick here.
	if (_pendingWarpTarget.empty() &&
	    _currentScene.activeZones.empty() &&
	    !_sceneHasTimerScript) {
		runEndInit(0);
		if (getScriptVariableValue("EndGame") != 0) {
			warning("Egypt: EndGame set after extra tick in %s", _currentScene.name.c_str());
			return;
		}
	}

	// If startup (or the extra tick above) already queued a warp, skip display.
	// The run() loop will load the target scene on the next iteration.
	if (!_pendingWarpTarget.empty())
		return;

	_hasCrossFadeOldScreen = false;
	// WARP:OLD returns to the eye-warp origin scene: no cross-fade (EXE behaviour).
	if (_pendingWarp.active && !_pendingWarp.toScene.equalsIgnoreCase("OLD")) {
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


void CryOmni3DEngine_Egypt::playHnmWithSpeed(const Common::Path &path) {
	// Peek the VBL speed at HNM6 header offset 26 (uint16LE, 50 Hz ticks per frame).
	// HNMDecoder's default 66 ms is only correct for 15-fps files; honour whatever
	// the file declares so 12.5-fps (speed=4 → 80 ms) clips don't run too fast.
	uint32 msPerFrame = 66; // safe default matching HNMDecoder's HNM6 fallback
	{
		Common::File peek;
		if (peek.open(path)) {
			peek.skip(26); // tag(4)+unk(2)+audioflags(1)+bpp(1)+w(2)+h(2)+filesize(4)+frames(4)+tabofs(4)+unk(2)
			const uint16 vblSpeed = peek.readUint16LE();
			if (vblSpeed > 0)
				msPerFrame = vblSpeed * 1000u / 50u;
		}
	}

	warning("Egypt: HNS %s msPerFrame=%u",
	        path.toString(Common::Path::kNativeSeparator).c_str(), msPerFrame);

	Video::HNMDecoder *dec = new Video::HNMDecoder(g_system->getScreenFormat());
	dec->setSoundType(Audio::Mixer::kMusicSoundType);
	dec->setRegularFrameDelay(msPerFrame); // must be before loadFile()

	if (!dec->loadFile(path)) {
		warning("Egypt: failed to open HNS %s",
		        path.toString(Common::Path::kNativeSeparator).c_str());
		delete dec;
		return;
	}

	dec->start();
	const uint16 w = dec->getWidth();
	const uint16 h = dec->getHeight();

	while (!shouldAbort() && !dec->endOfVideo()) {
		if (dec->needsUpdate()) {
			const Graphics::Surface *frame = dec->decodeNextFrame();
			if (frame)
				g_system->copyRectToScreen(frame->getPixels(), frame->pitch, 0, 0, w, h);
		}
		g_system->updateScreen();
		g_system->delayMillis(10);
		if (pollEvents() && checkKeysPressed())
			break;
	}

	delete dec;
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
