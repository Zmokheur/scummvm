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

#include "common/textconsole.h"
#include "common/util.h"

#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

void CryOmni3DEngine_Egypt::collectInitialActiveZones() {
	_currentScene.activeZones.clear();
	double sourceAlpha = 0.0;
	double sourceBeta = 0.0;
	bool sourceAvailable = false;
	getRuntimeSourceViewAngles(sourceAlpha, sourceBeta, sourceAvailable);
	bool scriptProducedState = runPrototypeWarpScript(0, sourceAlpha, sourceBeta);

	if (!scriptProducedState) {
		for (Common::Array<EgyptZone>::const_iterator it = _currentScene.zones.begin();
		     it != _currentScene.zones.end(); ++it) {
			if (it->left != 0 || it->top != 0 || it->right != 0 || it->bottom != 0)
				_currentScene.activeZones.push_back(it->id);
		}

		warning("Egypt: no explicit zone activation in %s, using default active zones from scene data",
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

bool CryOmni3DEngine_Egypt::runPrototypeWarpScript(int zoneClick, double sourceAlpha, double sourceBeta) {
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

	_scriptVariables["zoneclic"] = zoneClick;
	warning("Egypt: prototype zoneclic=%d for scene %s",
	        zoneClick, _currentScene.name.c_str());

	return executeScriptBlock(blockLines, zoneClick, sourceAlpha, sourceBeta);
}

bool CryOmni3DEngine_Egypt::executeScriptBlock(const Common::Array<Common::String> &lines, uint zoneClick,
                                               double sourceAlpha, double sourceBeta) {
	Common::HashMap<Common::String, uint> labels;
	bool producedState = false;

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
		    line.equalsIgnoreCase("endwarp") || line.hasPrefixIgnoreCase("centrage") || line.hasSuffix(":") ||
		    line.hasPrefix("//")) {
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

				if (evaluateScriptCondition(condition) && labels.contains(label))
					pc = labels[label];
			} else {
				int commandPos = expression.find(' ');
				if (commandPos > 0) {
					Common::String condition = expression.substr(0, commandPos);
					Common::String command = expression.substr(commandPos + 1);
					condition.trim();
					command.trim();

					if (evaluateScriptCondition(condition)) {
						executeScriptCommand(command, labels, pc, zoneClick, sourceAlpha, sourceBeta, producedState);
						if (!_pendingWarpTarget.empty())
							return producedState;
					}
				}
			}
			continue;
		}

		executeScriptCommand(line, labels, pc, zoneClick, sourceAlpha, sourceBeta, producedState);
		if (!_pendingWarpTarget.empty())
			return producedState;
	}

	return producedState;
}

bool CryOmni3DEngine_Egypt::executeScriptCommand(const Common::String &rawLine,
                                                 const Common::HashMap<Common::String, uint> &labels,
                                                 uint &pc, uint zoneClick, double sourceAlpha,
                                                 double sourceBeta, bool &producedState) {
	Common::String line = rawLine;
	line.trim();
	if (line.empty())
		return false;

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
		return true;
	}

	if (line.hasPrefixIgnoreCase("zoneinactive ")) {
		Common::String value = line.substr(13);
		value.trim();
		uint zoneId = (uint)atoi(value.c_str());
		for (Common::Array<uint>::iterator it = _currentScene.activeZones.begin();
		     it != _currentScene.activeZones.end(); ++it) {
			if (*it == zoneId) {
				_currentScene.activeZones.remove_at(it - _currentScene.activeZones.begin());
				producedState = true;
				break;
			}
		}
		return true;
	}

	if (line.hasPrefixIgnoreCase("let ")) {
		setScriptVariable(line.substr(4));
		producedState = true;
		return true;
	}

	if (line.hasPrefixIgnoreCase("aller_warp ")) {
		Common::String value = line.substr(11);
		value.trim();
		if (value.hasSuffix("!"))
			value.deleteLastChar();
		if (queuePrototypeSceneChange((uint)atoi(value.c_str()), "aller_warp", zoneClick, false,
		                              sourceAlpha, sourceBeta)) {
			producedState = true;
		}
		return true;
	}

	if (line.hasPrefixIgnoreCase("aller_hnm_warp ")) {
		Common::String value = line.substr(15);
		value.trim();
		if (value.hasSuffix("!"))
			value.deleteLastChar();
		if (queuePrototypeSceneChange((uint)atoi(value.c_str()), "aller_hnm_warp", zoneClick, true,
		                              sourceAlpha, sourceBeta)) {
			producedState = true;
		}
		return true;
	}

	if (line.hasPrefixIgnoreCase("goto ")) {
		Common::String label = line.substr(5);
		label.trim();
		if (label.hasSuffix("!"))
			label.deleteLastChar();
		if (labels.contains(label))
			pc = labels[label];
		return true;
	}

	static const char *const kSafeNoopPrefixes[] = {
		"music", "stopmusic", "sound", "sounds", "bmouse", "dialoguer",
		"animspr", "editspr", "show", "hide"
	};
	for (uint i = 0; i < ARRAYSIZE(kSafeNoopPrefixes); ++i) {
		if (line.hasPrefixIgnoreCase(kSafeNoopPrefixes[i])) {
			logUnsupportedScriptCommand(line);
			return true;
		}
	}

	logUnsupportedScriptCommand(line);
	return false;
}

bool CryOmni3DEngine_Egypt::queuePrototypeSceneChange(uint zoneId, const char *reason, uint zoneClick,
                                                      bool viaHnm, double sourceAlpha, double sourceBeta) {
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

	bool sourceOrientationAvailable = _currentViewAnglesAvailable;
	if (!sourceOrientationAvailable && sourceAlpha == 0.0 && sourceBeta == 0.0)
		warning("Egypt: source orientation unavailable, using 0/0 for warp from %s via %s",
		        _currentScene.name.c_str(), reason);
	rememberPendingArrival(*zone, zoneClick, viaHnm, sourceOrientationAvailable, sourceAlpha, sourceBeta, reason);
	_pendingWarpTarget = resolvePrototypeWarpTarget(zone->targetWarp);
	warning("Egypt: prototype queued %s via zone %03u from %s to %s (target %s param=%s extra=%s)",
	        reason, zone->id, _currentScene.name.c_str(), _pendingWarpTarget.c_str(),
	        zone->targetWarp.c_str(), zone->param.c_str(), zone->extraParam.c_str());
	return true;
}

bool CryOmni3DEngine_Egypt::evaluateScriptCondition(const Common::String &expression) const {
	Common::String condition = expression;
	condition.trim();

	struct Operator {
		const char *symbol;
		int length;
	};
	static const Operator kOperators[] = {
		{"!=", 2}, {"<=", 2}, {">=", 2}, {"<", 1}, {">", 1}, {"=", 1}
	};

	for (uint i = 0; i < ARRAYSIZE(kOperators); ++i) {
		int operatorPos = condition.find(kOperators[i].symbol);
		if (operatorPos < 0)
			continue;

		Common::String left = condition.substr(0, operatorPos);
		Common::String right = condition.substr(operatorPos + kOperators[i].length);
		left.trim();
		right.trim();

		const int leftValue = getScriptVariableValue(left);
		const int rightValue = resolveScriptValue(right);

		if (strcmp(kOperators[i].symbol, "!=") == 0)
			return leftValue != rightValue;
		if (strcmp(kOperators[i].symbol, "<=") == 0)
			return leftValue <= rightValue;
		if (strcmp(kOperators[i].symbol, ">=") == 0)
			return leftValue >= rightValue;
		if (strcmp(kOperators[i].symbol, "<") == 0)
			return leftValue < rightValue;
		if (strcmp(kOperators[i].symbol, ">") == 0)
			return leftValue > rightValue;
		return leftValue == rightValue;
	}

	return false;
}

int CryOmni3DEngine_Egypt::resolveScriptValue(const Common::String &token) const {
	Common::String value = token;
	value.trim();
	if (value.empty())
		return 0;

	const char firstChar = value[0];
	if ((firstChar >= '0' && firstChar <= '9') || firstChar == '-' || firstChar == '+')
		return atoi(value.c_str());

	Common::HashMap<Common::String, int, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo>::const_iterator it =
		_scriptVariables.find(value);
	if (it != _scriptVariables.end())
		return it->_value;

	it = _scriptConstants.find(value);
	if (it != _scriptConstants.end())
		return it->_value;

	return 0;
}

int CryOmni3DEngine_Egypt::getScriptVariableValue(const Common::String &name) const {
	Common::HashMap<Common::String, int, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo>::const_iterator it =
		_scriptVariables.find(name);
	if (it != _scriptVariables.end())
		return it->_value;

	it = _scriptConstants.find(name);
	if (it != _scriptConstants.end())
		return it->_value;

	return 0;
}

void CryOmni3DEngine_Egypt::setScriptVariable(const Common::String &assignment) {
	Common::String op;
	int separatorPos = assignment.find("+=");
	if (separatorPos >= 0) {
		op = "+=";
	} else {
		separatorPos = assignment.find("-=");
		if (separatorPos >= 0) {
			op = "-=";
		} else {
			separatorPos = assignment.find('=');
			if (separatorPos >= 0)
				op = "=";
		}
	}

	if (separatorPos < 0)
		return;

	Common::String name = assignment.substr(0, separatorPos);
	Common::String value = assignment.substr(separatorPos + op.size());
	name.trim();
	value.trim();

	int result = resolveScriptValue(value);
	if (op == "+=")
		result = getScriptVariableValue(name) + result;
	else if (op == "-=")
		result = getScriptVariableValue(name) - result;

	_scriptVariables[name] = result;
	warning("Egypt: script variable %s=%d", name.c_str(), _scriptVariables[name]);
}

bool CryOmni3DEngine_Egypt::executePrototypeSceneLogic() {
	return !_pendingWarpTarget.empty();
}

const EgyptZone *CryOmni3DEngine_Egypt::findZoneById(uint zoneId) const {
	for (Common::Array<EgyptZone>::const_iterator it = _currentScene.zones.begin();
	     it != _currentScene.zones.end(); ++it) {
		if (it->id == zoneId)
			return &(*it);
	}

	return nullptr;
}

void CryOmni3DEngine_Egypt::logUnsupportedScriptCommand(const Common::String &line) const {
	Common::String token = line;
	int spacePos = token.find(' ');
	if (spacePos >= 0)
		token = token.substr(0, spacePos);

	Common::HashMap<Common::String, bool, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> &logged =
		const_cast<Common::HashMap<Common::String, bool, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> &>(_loggedScriptCommands);
	if (logged.contains(token))
		return;

	logged[token] = true;
	warning("Egypt: unsupported script command in %s: %s",
	        _currentScene.name.c_str(), line.c_str());
}

void CryOmni3DEngine_Egypt::logScriptLine(const Common::String &line) const {
	if (line.equalsIgnoreCase("warpinit") || line.equalsIgnoreCase("endwarp") ||
	    line.equalsIgnoreCase("endinit") || line.hasSuffix(":") ||
	    line.hasPrefixIgnoreCase("centrage") || line.hasPrefixIgnoreCase("music") ||
	    line.hasPrefixIgnoreCase("stopmusic") || line.hasPrefixIgnoreCase("if ") ||
	    line.hasPrefixIgnoreCase("let ") || line.hasPrefixIgnoreCase("goto ") ||
	    line.hasPrefixIgnoreCase("aller_warp") || line.hasPrefixIgnoreCase("aller_hnm_warp") ||
	    line.hasPrefixIgnoreCase("dialoguer") || line.hasPrefixIgnoreCase("zoneactive") ||
	    line.hasPrefixIgnoreCase("zoneinactive")) {
		warning("Egypt: script %s", line.c_str());
	}
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
