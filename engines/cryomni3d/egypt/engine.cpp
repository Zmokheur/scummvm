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

#include "engines/util.h"

#include "cryomni3d/egypt/engine.h"

#include "graphics/pixelformat.h"

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

		if (scumm_stricmp(kind, "variable") != 0)
			continue;

		_scriptConstants[Common::String(name)] = value;
	}

	warning("Egypt: loaded %u script constant(s) from %s",
	        _scriptConstants.size(), filename.toString(Common::Path::kNativeSeparator).c_str());
	return true;
}

Common::Path CryOmni3DEngine_Egypt::resolveSceneDefinitionPath(const Common::String &sceneName) const {
	Common::String normalizedName = sceneName;
	normalizedName.replace('\\', '/');

	const int currentLevel = getScriptVariableValue("Level");
	if (currentLevel >= 1 && currentLevel <= 6) {
		Common::String levelPath = Common::String::format("SPRITE/LEVEL%d/%s.DEF",
		                                                  currentLevel, normalizedName.c_str());
		Common::Path levelDefPath(levelPath);
		if (Common::File::exists(levelDefPath))
			return levelDefPath;
	}

	for (int level = 1; level <= 6; ++level) {
		Common::String levelPath = Common::String::format("SPRITE/LEVEL%d/%s.DEF",
		                                                  level, normalizedName.c_str());
		Common::Path levelDefPath(levelPath);
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
	collectInitialActiveZones();
	displayCurrentWarpPreview(warpPath);
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
