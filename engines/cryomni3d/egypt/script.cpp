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
#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

// --- Variable store (engine side) -------------------------------------------

// Maps DEF script variable names (case-insensitive) to GameVariables::Var indices.
// Built once on first call from the X-macro table in game_variables.h.
static int gameVarIndex(const Common::String &name) {
	typedef Common::HashMap<Common::String, int,
	    Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> VarMap;
	static VarMap s_map;
	if (s_map.empty()) {
#define EGYPT_GV_EXPAND_MAP(sym, defName) s_map[defName] = GameVariables::sym;
		EGYPT_GAME_VARIABLES(EGYPT_GV_EXPAND_MAP)
#undef EGYPT_GV_EXPAND_MAP
	}
	VarMap::const_iterator it = s_map.find(name);
	return it != s_map.end() ? it->_value : -1;
}

void CryOmni3DEngine_Egypt::setGameVar(const Common::String &name, int value) {
	int idx = gameVarIndex(name);
	if (idx >= 0) {
		_gameVariables[idx] = (uint)value;
	} else {
		warning("Egypt: setGameVar: unknown variable '%s'", name.c_str());
	}
}

bool CryOmni3DEngine_Egypt::evaluateScriptCondition(const Common::String &expression) const {
	Common::String condition = expression;
	condition.trim();

	// Handle "and" / "or" conjunctions (case-insensitive) by splitting recursively.
	Common::String condLower = condition;
	condLower.toLowercase();
	int andPos = condLower.find(" and ");
	if (andPos >= 0)
		return evaluateScriptCondition(condition.substr(0, andPos)) &&
		       evaluateScriptCondition(condition.substr(andPos + 5));
	int orPos = condLower.find(" or ");
	if (orPos >= 0)
		return evaluateScriptCondition(condition.substr(0, orPos)) ||
		       evaluateScriptCondition(condition.substr(orPos + 4));

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

	int idx = gameVarIndex(value);
	if (idx >= 0)
		return (int)_gameVariables[idx];

	Common::HashMap<Common::String, int, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo>::const_iterator it =
		_scriptConstants.find(value);
	if (it != _scriptConstants.end())
		return it->_value;

	return 0;
}

int CryOmni3DEngine_Egypt::getScriptVariableValue(const Common::String &name) const {
	int idx = gameVarIndex(name);
	if (idx >= 0)
		return (int)_gameVariables[idx];

	Common::HashMap<Common::String, int, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo>::const_iterator it =
		_scriptConstants.find(name);
	if (it != _scriptConstants.end())
		return it->_value;

	return 0;
}

void CryOmni3DEngine_Egypt::setScriptVariable(const Common::String &assignment) {
	struct OpEntry { const char *sym; int len; };
	static const OpEntry kOps[] = {
		{ "+=", 2 }, { "-=", 2 }, { "*=", 2 }, { "/=", 2 }, { "=", 1 }
	};

	Common::String op;
	int separatorPos = -1;
	for (uint oi = 0; oi < ARRAYSIZE(kOps); ++oi) {
		int pos = assignment.find(kOps[oi].sym);
		if (pos >= 0) {
			op = kOps[oi].sym;
			separatorPos = pos;
			break;
		}
	}

	if (separatorPos < 0)
		return;

	Common::String name = assignment.substr(0, separatorPos);
	Common::String value = assignment.substr(separatorPos + op.size());
	name.trim();
	value.trim();

	int rhs = resolveScriptValue(value);
	int result;
	if (op == "+=")
		result = getScriptVariableValue(name) + rhs;
	else if (op == "-=")
		result = getScriptVariableValue(name) - rhs;
	else if (op == "*=")
		result = getScriptVariableValue(name) * rhs;
	else if (op == "/=")
		result = (rhs != 0) ? getScriptVariableValue(name) / rhs : 0;
	else
		result = rhs;

	int idx = gameVarIndex(name);
	if (idx < 0) {
		warning("Egypt: setScriptVariable: unknown variable '%s'", name.c_str());
		return;
	}

	_gameVariables[idx] = (uint)result;
	debugC(kDebugVariable, "Egypt: script variable %s=%d", name.c_str(), result);

	if (idx == GameVariables::kLevel)
		resetScriptTimer();

	// EXE: the PRENDRE handler does objectValues[objectId]++ to mark the object as taken.
	// When a script sets main=X directly (e.g. dialogue giving an item), the PRENDRE
	// handler is bypassed, so the object variable stays at 0 and the pickup zone can
	// re-activate after the item is stored.  Mirror the increment here for any
	// assignment that places a known object into main.
	if (idx == GameVariables::kMain && result > 0) {
		Common::HashMap<Common::String, int,
		    Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo>::const_iterator cit;
		for (cit = _scriptConstants.begin(); cit != _scriptConstants.end(); ++cit) {
			if (cit->_value == result && cit->_key.hasPrefixIgnoreCase("Objet")) {
				const Common::String varName = cit->_key.substr(5); // strip "Objet"
				const int newVal = getScriptVariableValue(varName) + 1;
				setGameVar(varName, newVal);
				debugC(kDebugVariable, "Egypt: auto-increment %s=%d (main set via script to %d)",
				        varName.c_str(), newVal, result);
				break;
			}
		}
	}
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
	debugC(kDebugVariable, "Egypt: prototype queued %s via zone %03u from %s to %s (target %s param=%s extra=%s)",
	        reason, zone->id, _currentScene.name.c_str(), _pendingWarpTarget.c_str(),
	        zone->targetWarp.c_str(), zone->param.c_str(), zone->extraParam.c_str());
	return true;
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

// --- Script interpreter (Egypt_Script) --------------------------------------

bool Egypt_Script::executeBlock(const Common::Array<Common::String> &lines, uint zoneClick,
                                double sourceAlpha, double sourceBeta) {
	Common::HashMap<Common::String, uint> labels;
	bool producedState = false;

	for (uint i = 0; i < lines.size(); ++i) {
		Common::String trimmed = lines[i];
		trimmed.trim();
		if (!trimmed.empty() && trimmed.hasSuffix(":")) {
			Common::String label = trimmed;
			label.deleteLastChar();
			if (!label.empty())
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

		Context ctx = { labels, pc, zoneClick, sourceAlpha, sourceBeta, producedState };

		if (line.hasPrefixIgnoreCase("if ")) {
			Common::String expression = line.substr(3);
			int gotoPos = expression.find(" goto ");
			if (gotoPos >= 0) {
				Common::String condition = expression.substr(0, gotoPos);
				Common::String label = expression.substr(gotoPos + 6);
				label.trim();
				if (label.hasSuffix("!"))
					label.deleteLastChar();

				if (_engine->evaluateScriptCondition(condition) && labels.contains(label))
					pc = labels[label];
			} else {
				int commandPos = expression.find(' ');
				if (commandPos > 0) {
					Common::String condition = expression.substr(0, commandPos);
					Common::String command = expression.substr(commandPos + 1);
					condition.trim();
					command.trim();

					if (_engine->evaluateScriptCondition(condition)) {
						executeCommand(command, ctx);
						if (!_engine->_pendingWarpTarget.empty())
							return producedState;
					}
				}
			}
			continue;
		}

		executeCommand(line, ctx);
		if (!_engine->_pendingWarpTarget.empty())
			return producedState;
	}

	return producedState;
}

const Egypt_Script::CommandEntry Egypt_Script::kCommands[] = {
	{ "zoneactive",     true,  &Egypt_Script::cmdZoneActive },
	{ "zoneinactive",   true,  &Egypt_Script::cmdZoneInactive },
	{ "let",            true,  &Egypt_Script::cmdLet },
	{ "aller_warp",     true,  &Egypt_Script::cmdAllerWarp },
	{ "aller_hnm_warp", true,  &Egypt_Script::cmdAllerHnmWarp },
	{ "goto",           true,  &Egypt_Script::cmdGoto },
	{ "editspr",        true,  &Egypt_Script::cmdEditspr },
	{ "decompress",     false, &Egypt_Script::cmdDecompress },
	{ "animspr",        true,  &Egypt_Script::cmdAnimspr },
	{ "fonction",       true,  &Egypt_Script::cmdFonction },
	{ "dialoguer",      true,  &Egypt_Script::cmdDialoguer },
	{ "stopmusic",      false, &Egypt_Script::cmdStopMusic },
	{ "music",          true,  &Egypt_Script::cmdMusic },
	{ "sounds",         true,  &Egypt_Script::cmdSound },
	{ "sound",          true,  &Egypt_Script::cmdSound },
};

bool Egypt_Script::executeCommand(const Common::String &rawLine, Context &ctx) {
	Common::String line = rawLine;
	line.trim();
	if (line.empty())
		return false;

	for (uint i = 0; i < ARRAYSIZE(kCommands); ++i) {
		const CommandEntry &entry = kCommands[i];
		if (entry.takesArgs) {
			const Common::String prefix = Common::String(entry.name) + " ";
			if (line.hasPrefixIgnoreCase(prefix)) {
				Common::String args = line.substr(prefix.size());
				args.trim();
				(this->*entry.handler)(args, ctx);
				return true;
			}
		} else if (line.equalsIgnoreCase(entry.name)) {
			(this->*entry.handler)(Common::String(), ctx);
			return true;
		}
	}

	// Commands not implemented yet but known to be safe to skip
	static const char *const kSafeNoopPrefixes[] = {
		"bmouse", "show", "hide", "son_3d", "inventaire", "and"
	};
	for (uint i = 0; i < ARRAYSIZE(kSafeNoopPrefixes); ++i) {
		if (line.hasPrefixIgnoreCase(kSafeNoopPrefixes[i])) {
			logUnsupportedCommand(line);
			return true;
		}
	}

	logUnsupportedCommand(line);
	return false;
}

void Egypt_Script::cmdZoneActive(const Common::String &args, Context &ctx) {
	uint zoneId = (uint)atoi(args.c_str());
	// EXE (0x412a16): zoneactive is a no-op when holding an object.
	// The dialogue may give an item via "let main=X" without going through
	// PRENDRE, leaving the pickup variable at 0 and the zone seemingly
	// eligible - but the EXE's held-object guard prevents re-activation.
	if (_engine->getScriptVariableValue("main") != 0)
		return;
	bool alreadyActive = false;
	for (Common::Array<uint>::const_iterator activeIt = _engine->_currentScene.activeZones.begin();
	     activeIt != _engine->_currentScene.activeZones.end(); ++activeIt) {
		if (*activeIt == zoneId) {
			alreadyActive = true;
			break;
		}
	}
	if (zoneId != 0 && !alreadyActive) {
		_engine->_currentScene.activeZones.push_back(zoneId);
		ctx.producedState = true;
	}
}

void Egypt_Script::cmdZoneInactive(const Common::String &args, Context &ctx) {
	uint zoneId = (uint)atoi(args.c_str());
	for (Common::Array<uint>::iterator it = _engine->_currentScene.activeZones.begin();
	     it != _engine->_currentScene.activeZones.end(); ++it) {
		if (*it == zoneId) {
			_engine->_currentScene.activeZones.remove_at(it - _engine->_currentScene.activeZones.begin());
			ctx.producedState = true;
			break;
		}
	}
}

void Egypt_Script::cmdLet(const Common::String &args, Context &ctx) {
	_engine->setScriptVariable(args);
	ctx.producedState = true;
}

void Egypt_Script::cmdAllerWarp(const Common::String &args, Context &ctx) {
	Common::String value = args;
	if (value.hasSuffix("!"))
		value.deleteLastChar();
	if (_engine->queuePrototypeSceneChange((uint)atoi(value.c_str()), "aller_warp", ctx.zoneClick, false,
	                                       ctx.sourceAlpha, ctx.sourceBeta)) {
		ctx.producedState = true;
	}
}

void Egypt_Script::cmdAllerHnmWarp(const Common::String &args, Context &ctx) {
	Common::String value = args;
	if (value.hasSuffix("!"))
		value.deleteLastChar();
	const uint hnmZoneId = (uint)atoi(value.c_str());
	const EgyptZone *hnmZone = _engine->findZoneById(hnmZoneId);
	if (hnmZone && hnmZone->targetWarp.equalsIgnoreCase("NULL")) {
		// Play HNM sequence in-place, stay on current scene
		Common::String joined;
		for (uint si = 0; si < hnmZone->hnmSequence.size(); ++si) {
			if (si > 0) joined += "/";
			joined += hnmZone->hnmSequence[si];
		}
		if (!joined.empty())
			_engine->executeHnmSequence(joined);
		ctx.producedState = true;
	} else {
		if (_engine->queuePrototypeSceneChange(hnmZoneId, "aller_hnm_warp", ctx.zoneClick, true,
		                                       ctx.sourceAlpha, ctx.sourceBeta)) {
			ctx.producedState = true;
		}
	}
}

void Egypt_Script::cmdGoto(const Common::String &args, Context &ctx) {
	Common::String label = args;
	if (label.hasSuffix("!"))
		label.deleteLastChar();
	if (ctx.labels.contains(label))
		ctx.pc = ctx.labels[label];
}

void Egypt_Script::cmdEditspr(const Common::String &args, Context &ctx) {
	int base = 0, count = 1, unused = 0;
	sscanf(args.c_str(), "%d %d %d", &base, &count, &unused);
	if (!_engine->_spriteLoader.hasOverlayData())
		_engine->_spriteLoader.loadSceneOverlay(_engine->_currentScene.name,
		                                        _engine->getScriptVariableValue("Level"));
	EgyptOverlayCatalogEntry entry;
	entry.base = (uint32)MAX(0, base);
	entry.count = (uint32)MAX(1, count);
	entry.counter = 0;
	_overlayCatalog.push_back(entry);
	debugC(kDebugVariable, "Egypt: editspr catalog[%u] base=%u count=%u",
	        (uint)(_overlayCatalog.size() - 1), entry.base, entry.count);
}

void Egypt_Script::cmdDecompress(const Common::String &args, Context &ctx) {
	// Reset the panorama to the clean decoded WARP/HNM state, without any
	// animspr overlays. The script then redraws only the sprites still needed.
	_engine->_spriteLoader.clearSceneSpr();
	debugC(kDebugVariable, "Egypt: decompress - panorama reset");
}

void Egypt_Script::cmdAnimspr(const Common::String &args, Context &ctx) {
	const int n = atoi(args.c_str());
	if (n >= 1 && (uint)(n - 1) < _overlayCatalog.size()) {
		EgyptOverlayCatalogEntry &entry = _overlayCatalog[(uint)(n - 1)];
		const uint32 now = g_system->getMillis();
		if (now - entry.lastTick < 70)
			return;
		entry.lastTick = now;
		const uint frameIdx = entry.base + entry.counter;
		_engine->_spriteLoader.decodeSceneSprFrame(frameIdx);
		entry.counter = (entry.counter + 1) % entry.count;
		debugC(kDebugVariable, "Egypt: animspr %d -> scene SPR frame %u (next counter=%u)", n, frameIdx, entry.counter);
	} else {
		warning("Egypt: animspr %d out of range (catalog size=%u)", n, (uint)_overlayCatalog.size());
	}
}

void Egypt_Script::cmdFonction(const Common::String &args, Context &ctx) {
	const int funcNum = atoi(args.c_str());
	// FADE_IN = 6, FADE_OUT = 7 (constants defined in EGYPTE.DEF)
	if (funcNum == 6) {
		_engine->performScreenFade(false);
	} else if (funcNum == 7) {
		_engine->performScreenFade(true);
	} else {
		logUnsupportedCommand("fonction " + args);
	}
}

void Egypt_Script::cmdDialoguer(const Common::String &args, Context &ctx) {
	const uint zoneId = (uint)atoi(args.c_str());
	const EgyptZone *dlgZone = _engine->findZoneById(zoneId);
	if (dlgZone && dlgZone->commandName.equalsIgnoreCase("DIALOGUER")) {
		// Zone arg format: NAME-DIAL-LABEL (e.g. MONTOUMES-DIAL-SMT0001)
		// Extract the portion after the second dash.
		const Common::String &arg = dlgZone->label;
		int firstDash = arg.find('-');
		if (firstDash >= 0) {
			int secondDash = arg.find('-', firstDash + 1);
			if (secondDash >= 0) {
				Common::String label = arg.substr(secondDash + 1);
				if (label.hasSuffix("!"))
					label.deleteLastChar();
				label.toLowercase();
				_engine->_dialogPendingLabel = label;
				debugC(kDebugVariable, "Egypt: dialoguer %u -> label '%s'",
				        zoneId, label.c_str());
			}
		}
	} else {
		warning("Egypt: dialoguer %u - zone not found or not DIALOGUER", zoneId);
	}
}

// "music <name>" - start (or keep) the named ambient loop (MUSIC/<name>.WAV).
void Egypt_Script::cmdMusic(const Common::String &args, Context &ctx) {
	Common::String name = args;
	name.trim();
	debugC(kDebugVariable, "Egypt: music %s", name.c_str());
	_engine->playAmbientMusic(name);
}

// "stopmusic" - stop the ambient loop.
void Egypt_Script::cmdStopMusic(const Common::String &args, Context &ctx) {
	debugC(kDebugVariable, "Egypt: stopmusic");
	_engine->stopAmbientMusic();
}

// "sound <name>" / "sounds <name>" - fire a one-shot APC effect.
void Egypt_Script::cmdSound(const Common::String &args, Context &ctx) {
	Common::String name = args;
	name.trim();
	debugC(kDebugVariable, "Egypt: sound %s", name.c_str());
	_engine->playSfx(name);
}

void Egypt_Script::logUnsupportedCommand(const Common::String &line) {
	Common::String token = line;
	int spacePos = token.find(' ');
	if (spacePos >= 0)
		token = token.substr(0, spacePos);

	if (_loggedCommands.contains(token))
		return;

	_loggedCommands[token] = true;
	warning("Egypt: unsupported script command in %s: %s",
	        _engine->_currentScene.name.c_str(), line.c_str());
}

void Egypt_Script::logScriptLine(const Common::String &line) const {
	if (line.equalsIgnoreCase("warpinit") || line.equalsIgnoreCase("endwarp") ||
	    line.equalsIgnoreCase("endinit") || line.equalsIgnoreCase("decompress") ||
	    line.hasSuffix(":") ||
	    line.hasPrefixIgnoreCase("centrage") || line.hasPrefixIgnoreCase("music") ||
	    line.hasPrefixIgnoreCase("stopmusic") || line.hasPrefixIgnoreCase("if ") ||
	    line.hasPrefixIgnoreCase("let ") || line.hasPrefixIgnoreCase("goto ") ||
	    line.hasPrefixIgnoreCase("aller_warp") || line.hasPrefixIgnoreCase("aller_hnm_warp") ||
	    line.hasPrefixIgnoreCase("dialoguer ") || line.hasPrefixIgnoreCase("zoneactive") ||
	    line.hasPrefixIgnoreCase("zoneinactive")) {
		debugC(2, kDebugVariable, "Egypt: script %s", line.c_str());
	}
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
