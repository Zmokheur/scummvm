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
#include "common/file.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"

#include "engines/util.h"

#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

CryOmni3DEngine_Egypt::CryOmni3DEngine_Egypt(OSystem *syst,
		const CryOmni3DGameDescription *gamedesc) : CryOmni3DEngine(syst, gamedesc) {
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

	initGraphics(640, 480);
	fillSurface(0);
	syncSoundSettings();

	loadScene("S01");
	executePrototypeSceneLogic();

	return Common::kNoError;
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
