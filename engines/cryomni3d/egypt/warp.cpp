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

#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

namespace {

bool scriptLineReferencesZoneclic(const Common::String &compactLine, uint zoneClick) {
	const char *line = compactLine.c_str();
	const char *needle = "zoneclic";
	const size_t needleLen = strlen(needle);

	for (const char *pos = strstr(line, needle); pos; pos = strstr(pos + needleLen, needle)) {
		const char *cursor = pos + needleLen;
		if (*cursor == '!' && *(cursor + 1) == '=')
			cursor += 2;
		else if (*cursor == '>' && *(cursor + 1) == '=')
			cursor += 2;
		else if (*cursor == '<' && *(cursor + 1) == '=')
			cursor += 2;
		else if (*cursor == '=' || *cursor == '>' || *cursor == '<')
			cursor += 1;
		else
			continue;

		if (!(*cursor >= '0' && *cursor <= '9'))
			continue;

		char *endPtr = nullptr;
		const long referencedZoneclic = strtol(cursor, &endPtr, 10);
		if (endPtr == cursor)
			continue;

		if ((uint)referencedZoneclic == zoneClick)
			return true;
	}

	return false;
}

} // End of anonymous namespace

bool isEgyptContextName(const Common::String &name) {
	return name.hasPrefixIgnoreCase("JOUR") || name.hasPrefixIgnoreCase("NUIT");
}

bool CryOmni3DEngine_Egypt::handleWarpClick(const Common::Point &mousePos, const Common::Point &warpPoint,
                                            double currentAlpha, double currentBeta) {
	const EgyptZone *zone = findInteractiveZone(warpPoint);
	if (zone) {
		const uint zoneClick = resolveScriptZoneClick(*zone);
		setRuntimeViewAngles(currentAlpha, currentBeta, true);

		warning("Egypt: click %s mouse=%d,%d warp=%d,%d zone=%03u zoneclic=%u command=%s",
		        _currentScene.name.c_str(), mousePos.x, mousePos.y, warpPoint.x, warpPoint.y,
		        zone->id, zoneClick, zone->command.c_str());

		if (isDocumentationZone(*zone)) {
			displayZoneDocumentation(*zone);
			return false;
		}

		_pendingWarpTarget.clear();
		_dialoguePendingLabel.clear();
		runEndInit(zoneClick);

		// Dialogue requested by the script (via "dialoguer N" command in endinit).
		if (!_dialoguePendingLabel.empty()) {
			Common::String dlgLabel = _dialoguePendingLabel;
			_dialoguePendingLabel.clear();
			runDialogue(dlgLabel);
			return false;
		}

		// Direct DIALOGUER zone action (actionId=1, commandName="DIALOGUER").
		// Handled like documentation zones: bypass warp, trigger dialogue directly.
		if (zone->commandName.equalsIgnoreCase("DIALOGUER") && !zone->label.empty()) {
			Common::String arg = zone->label; // e.g. "MONTOUMES-DIAL-SMT0009!"
			uint firstDash  = arg.find('-');
			uint secondDash = (firstDash != Common::String::npos)
			                  ? arg.find('-', firstDash + 1)
			                  : Common::String::npos;
			if (secondDash != Common::String::npos) {
				Common::String label = arg.substr(secondDash + 1);
				if (label.hasSuffix("!"))
					label.deleteLastChar();
				label.toLowercase();
				warning("Egypt: DIALOGUER zone %03u → label '%s'", zone->id, label.c_str());
				runDialogue(label);
			}
			return false;
		}

		if (_pendingWarpTarget.empty() && shouldUseDirectWarpFallback(*zone, zoneClick) &&
		    zone->commandName.equalsIgnoreCase("ALLER_WARP") &&
		    !zone->targetWarp.empty()) {
			rememberPendingArrival(*zone, zoneClick, false, true, currentAlpha, currentBeta, "active_zone_command");
			_pendingWarpTarget = resolvePrototypeWarpTarget(zone->targetWarp);
			warning("Egypt: active zone command from zone %03u to %s (target %s)",
			        zone->id, _pendingWarpTarget.c_str(), zone->targetWarp.c_str());
		}
		return !_pendingWarpTarget.empty();
	}

	warning("Egypt: click %s mouse=%d,%d warp=%d,%d no active zone",
	        _currentScene.name.c_str(), mousePos.x, mousePos.y, warpPoint.x, warpPoint.y);
	return false;
}

bool CryOmni3DEngine_Egypt::zoneContainsWarpPoint(const EgyptZone &zone, const Common::Point &warpPoint) const {
	if (warpPoint.x >= (int)zone.left && warpPoint.x < (int)zone.right &&
	    warpPoint.y >= (int)zone.top  && warpPoint.y < (int)zone.bottom)
		return true;
	for (uint i = 0; i < zone.extraRects.size(); ++i) {
		const EgyptZoneRect &r = zone.extraRects[i];
		if (warpPoint.x >= (int)r.left && warpPoint.x < (int)r.right &&
		    warpPoint.y >= (int)r.top  && warpPoint.y < (int)r.bottom)
			return true;
	}
	return false;
}

const EgyptZone *CryOmni3DEngine_Egypt::findHoveredActiveZone(const Common::Point &warpPoint) const {
	for (Common::Array<uint>::const_iterator it = _currentScene.activeZones.begin();
	     it != _currentScene.activeZones.end(); ++it) {
		for (Common::Array<EgyptZone>::const_iterator zit = _currentScene.zones.begin();
		     zit != _currentScene.zones.end(); ++zit) {
			if (zit->id == *it && zoneContainsWarpPoint(*zit, warpPoint))
				return &(*zit);
		}
	}

	return findInteractiveZone(warpPoint);
}

const EgyptZone *CryOmni3DEngine_Egypt::findInteractiveZone(const Common::Point &warpPoint) const {
	if (getScriptVariableValue("FlagVisite") != 0 &&
	    (_currentScene.contextName.equalsIgnoreCase("JOUR") ||
	     _currentScene.contextName.equalsIgnoreCase("NUIT"))) {
		for (Common::Array<EgyptZone>::const_iterator it = _currentScene.zones.begin();
		     it != _currentScene.zones.end(); ++it) {
			if ((it->id < 23 || it->id > 28) || !it->commandName.equalsIgnoreCase("ALLER_WARP"))
				continue;
			if (zoneContainsWarpPoint(*it, warpPoint))
				return &(*it);
		}
	}

	for (Common::Array<uint>::const_iterator it = _currentScene.activeZones.begin();
	     it != _currentScene.activeZones.end(); ++it) {
		for (Common::Array<EgyptZone>::const_iterator zit = _currentScene.zones.begin();
		     zit != _currentScene.zones.end(); ++zit) {
			if (zit->id == *it && zoneContainsWarpPoint(*zit, warpPoint))
				return &(*zit);
		}
	}

	return nullptr;
}

void CryOmni3DEngine_Egypt::rememberPendingArrival(const EgyptZone &zone, uint zoneclic, bool viaHnm,
                                                   bool sourceOrientationAvailable, double alpha, double beta,
                                                   const char *calledCommand) {
	_pendingWarp.active = true;
	_pendingWarp.fromScene = _currentScene.name;
	_pendingWarp.fromContext = _currentContextName;
	_pendingWarp.toScene = zone.targetWarp;
	_pendingWarp.toContext = isEgyptContextName(zone.targetWarp) ? zone.targetWarp : Common::String();
	{
		Common::String hnmJoined;
		for (uint si = 0; si < zone.hnmSequence.size(); ++si) {
			if (si > 0) hnmJoined += "/";
			hnmJoined += zone.hnmSequence[si];
		}
		_pendingWarp.hnmName  = hnmJoined;
		_pendingWarp.zoneExtra = hnmJoined.empty() ? zone.extraParam : hnmJoined;
	}
	_pendingWarp.zoneCommand = calledCommand ? calledCommand : zone.commandName;
	_pendingWarp.zoneParam = zone.param;
	_pendingWarp.zoneId = zone.id;
	_pendingWarp.zoneclic = zoneclic;
	_pendingWarp.viaHnm = viaHnm;
	_pendingWarp.sourceOrientationAvailable = sourceOrientationAvailable;
	_pendingWarp.sourceAlpha = alpha;
	_pendingWarp.sourceBeta = beta;
	warning("Egypt: remember warp fromScene=%s fromContext=%s toScene=%s toContext=%s zoneId=%u zoneclic=%u viaHnm=%d alpha=%0.3f beta=%0.3f",
	        _pendingWarp.fromScene.c_str(), _pendingWarp.fromContext.c_str(), _pendingWarp.toScene.c_str(),
	        _pendingWarp.toContext.c_str(), _pendingWarp.zoneId, _pendingWarp.zoneclic,
	        _pendingWarp.viaHnm ? 1 : 0, _pendingWarp.sourceAlpha, _pendingWarp.sourceBeta);
}

void CryOmni3DEngine_Egypt::clearPendingWarpRequest() {
	_pendingWarp = EgyptWarpRequest();
	_pendingRuntimeArrivalPrepared = false;
	_pendingRuntimeMatchedCentrage.clear();
	_pendingRuntimeResolved = EgyptResolvedCentrage();
}

void CryOmni3DEngine_Egypt::getRuntimeSourceViewAngles(double &alpha, double &beta, bool &available) const {
	available = _currentViewAnglesAvailable;
	if (available) {
		alpha = _currentViewAlpha;
		beta = _currentViewBeta;
	} else {
		alpha = 0.0;
		beta = 0.0;
	}
}

void CryOmni3DEngine_Egypt::setRuntimeViewAngles(double alpha, double beta, bool available) {
	_currentViewAnglesAvailable = available;
	_currentViewAlpha = alpha;
	_currentViewBeta = beta;
}

const EgyptCentrage *CryOmni3DEngine_Egypt::findCentrage(const Common::String &name) const {
	for (Common::Array<EgyptCentrage>::const_iterator it = _currentCentrages.begin();
	     it != _currentCentrages.end(); ++it) {
		if (!it->name.equalsIgnoreCase(name))
			continue;
		return &(*it);
	}

	return nullptr;
}

const EgyptCentrage *CryOmni3DEngine_Egypt::findArrivalCentrage(Common::String *matchedName) const {
	if (!_pendingWarp.active)
		return nullptr;

	if (!_pendingWarp.fromScene.empty()) {
		const EgyptCentrage *centrage = findCentrage(_pendingWarp.fromScene);
		if (centrage) {
			if (matchedName)
				*matchedName = _pendingWarp.fromScene;
			return centrage;
		}
	}

	if (!_pendingWarp.fromContext.empty()) {
		const EgyptCentrage *centrage = findCentrage(_pendingWarp.fromContext);
		if (centrage) {
			if (matchedName)
				*matchedName = _pendingWarp.fromContext;
			return centrage;
		}
	}

	if (matchedName)
		matchedName->clear();
	return nullptr;
}

Common::String CryOmni3DEngine_Egypt::resolvePrototypeWarpTarget(const Common::String &targetName) const {
	return targetName;
}

EgyptResolvedCentrage CryOmni3DEngine_Egypt::applyCentrageRaw(const EgyptCentrage &centrage,
                                                              double sourceAlpha, double sourceBeta) const {
	// The original EXE initialises α to π/2 before running the DEF script, then centrage rules
	// modify that default with +/- offsets or replace it with an absolute '=' value.
	// sourceAlpha (the click angle) is intentionally ignored for + and - operations.
	const double defaultAlpha = M_PI / 2.0;

	EgyptResolvedCentrage resolved;
	resolved.matched = true;
	resolved.rawFinalAlpha = defaultAlpha;
	resolved.finalBeta = 0.0;

	switch (centrage.op) {
	case '+':
		resolved.rawFinalAlpha = defaultAlpha + centrage.alpha;
		break;
	case '-':
		resolved.rawFinalAlpha = defaultAlpha - centrage.alpha;
		break;
	case '=':
		resolved.rawFinalAlpha = centrage.alpha;
		if (centrage.hasBeta)
			resolved.finalBeta = centrage.beta;
		break;
	default:
		resolved.matched = false;
		break;
	}

	if (resolved.matched) {
		resolved.normalizedFinalAlpha = resolved.rawFinalAlpha;
		while (resolved.normalizedFinalAlpha >= 2.0 * M_PI)
			resolved.normalizedFinalAlpha -= 2.0 * M_PI;
		while (resolved.normalizedFinalAlpha < 0.0)
			resolved.normalizedFinalAlpha += 2.0 * M_PI;
	}

	return resolved;
}

void CryOmni3DEngine_Egypt::prepareRuntimeArrivalView() {
	if (!_pendingWarp.active)
		return;

	_pendingRuntimeArrivalPrepared = true;
	_pendingRuntimeMatchedCentrage = "none";
	_pendingRuntimeResolved = EgyptResolvedCentrage();

	// Default: the EXE always starts each panorama at α = π/2. Centrage rules then either
	// offset from that default (+/-) or replace it with an absolute value (=).
	const double defaultAlpha = M_PI / 2.0;
	_pendingRuntimeResolved.matched = true;
	_pendingRuntimeResolved.rawFinalAlpha = defaultAlpha;
	_pendingRuntimeResolved.normalizedFinalAlpha = defaultAlpha;
	_pendingRuntimeResolved.finalBeta = 0.0;

	Common::String matchedName;
	const EgyptCentrage *centrage = findArrivalCentrage(&matchedName);
	if (!centrage) {
		// No centrage in the destination DEF for this source: try auto-detection.
		// Scenes that have explicit back-navigation centrage rules (e.g. centrage M15+3.14)
		// arrive looking opposite to the back-link zone. Level 1 scenes were authored without
		// such rules, so replicate the same effect here when a back-link zone exists.
		if (!_pendingWarp.fromScene.empty()) {
			for (const EgyptZone &z : _currentScene.zones) {
				if (!z.targetWarp.equalsIgnoreCase(_pendingWarp.fromScene))
					continue;
				// Skip invisible trigger zones (0-0-0-0 layout)
				if (z.left == 0 && z.right == 0 && z.top == 0 && z.bottom == 0)
					continue;
				const int centerX = (z.left + z.right) / 2;
				const double backAlpha = (2048.0 - centerX) * 2.0 * M_PI / 2048.0;
				double arrivalAlpha = backAlpha + M_PI;
				while (arrivalAlpha >= 2.0 * M_PI) arrivalAlpha -= 2.0 * M_PI;
				while (arrivalAlpha < 0.0)            arrivalAlpha += 2.0 * M_PI;
				_pendingRuntimeMatchedCentrage = Common::String::format("auto_backlink_%s_panoramaX%d",
				                                                        _pendingWarp.fromScene.c_str(), centerX);
				_pendingRuntimeResolved.rawFinalAlpha = backAlpha + M_PI;
				_pendingRuntimeResolved.normalizedFinalAlpha = arrivalAlpha;
				setRuntimeViewAngles(arrivalAlpha, 0.0, true);
				return;
			}
		}
		_pendingRuntimeMatchedCentrage = "default_pi_half";
		setRuntimeViewAngles(defaultAlpha, 0.0, true);
		return;
	}

	_pendingRuntimeResolved = applyCentrageRaw(*centrage, 0.0, 0.0);
	if (_pendingRuntimeResolved.matched) {
		_pendingRuntimeMatchedCentrage = Common::String::format("%s%c%0.3f",
		                                                       centrage->name.c_str(),
		                                                       centrage->op,
		                                                       centrage->alpha);
		if (centrage->hasBeta)
			_pendingRuntimeMatchedCentrage += Common::String::format(" %0.3f", centrage->beta);
		setRuntimeViewAngles(_pendingRuntimeResolved.normalizedFinalAlpha, _pendingRuntimeResolved.finalBeta, true);
	}
}

int CryOmni3DEngine_Egypt::consumeArrivalPanoramaX(double &alpha, double &beta, bool &hasAngles) {
	int arrivalPanoramaX = -1;
	hasAngles = false;
	if (_pendingRuntimeArrivalPrepared && _pendingRuntimeResolved.matched) {
		hasAngles = true;
		alpha = _pendingRuntimeResolved.normalizedFinalAlpha;
		beta = _pendingRuntimeResolved.finalBeta;
	}
	return arrivalPanoramaX;
}

void CryOmni3DEngine_Egypt::logRuntimeWarp(const Common::String &matchedCentrage, const EgyptResolvedCentrage &resolved,
                                           bool appliedToRenderer) const {
	if (!_pendingWarp.active)
		return;

	warning("EGYPT_RUNTIME_WARP: fromScene=%s fromContext=%s toScene=%s sourceAlpha=%0.3f sourceBeta=%0.3f matchedCentrage=%s rawFinalAlpha=%0.3f normalizedFinalAlpha=%0.3f finalBeta=%0.3f appliedToRenderer=%s",
	        _pendingWarp.fromScene.c_str(), _pendingWarp.fromContext.c_str(), _pendingWarp.toScene.c_str(),
	        _pendingWarp.sourceAlpha, _pendingWarp.sourceBeta, matchedCentrage.c_str(),
	        resolved.rawFinalAlpha, resolved.normalizedFinalAlpha, resolved.finalBeta,
	        appliedToRenderer ? "yes" : "no");
}

uint CryOmni3DEngine_Egypt::resolveScriptZoneClick(const EgyptZone &zone) const {
	if (getScriptVariableValue("FlagVisite") != 0 &&
	    (_currentScene.contextName.equalsIgnoreCase("JOUR") ||
	     _currentScene.contextName.equalsIgnoreCase("NUIT")) &&
	    zone.id >= 23 && zone.id <= 28)
		return zone.id - 22;

	if (_currentScene.name.equalsIgnoreCase("S01")) {
		if (zone.id == 1 && getScriptVariableValue("FlagEntreeS01") == 0)
			return 1;
		if (zone.id == 2)
			return 2;
	}

	return zone.id;
}

bool CryOmni3DEngine_Egypt::shouldUseDirectWarpFallback(const EgyptZone &zone, uint zoneClick) const {
	if (_currentScene.name.equalsIgnoreCase("S01") && zone.id == 1 &&
	    getScriptVariableValue("FlagEntreeS01") == 0 && zoneClick == 1) {
		warning("Egypt: keeping scripted intro path for first S01 click on zone %03u", zone.id);
		return false;
	}

	bool scriptUsesZoneclic = false;
	bool scriptReferencesCurrentZoneclic = false;
	for (Common::Array<Common::String>::const_iterator it = _currentScene.scriptLines.begin();
	     it != _currentScene.scriptLines.end(); ++it) {
		Common::String lower = *it;
		lower.toLowercase();
		if (lower.find("zoneclic") == Common::String::npos)
			continue;

		scriptUsesZoneclic = true;

		Common::String compact = lower;
		compact.replace(' ', '\0');
		compact.replace('\t', '\0');
		compact.deleteChar('\0');
		if (scriptLineReferencesZoneclic(compact, zoneClick)) {
			scriptReferencesCurrentZoneclic = true;
			break;
		}
	}

	if (_currentScene.hasWarpInit && _currentScene.hasEndWarp && scriptReferencesCurrentZoneclic)
		return false;

	if (_currentScene.hasWarpInit && _currentScene.hasEndWarp && scriptUsesZoneclic &&
	    zone.commandName.equalsIgnoreCase("ALLER_WARP")) {
		warning("Egypt: allowing direct warp fallback for zone %03u in %s because script does not reference zoneclic=%u",
		        zone.id, _currentScene.name.c_str(), zoneClick);
	}

	return true;
}


} // End of namespace Egypt
} // End of namespace CryOmni3D
