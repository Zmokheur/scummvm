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
	for (Common::Array<uint>::const_iterator it = _currentScene.activeZones.begin();
	     it != _currentScene.activeZones.end(); ++it) {
		const EgyptZone *zone = findZoneById(*it);
		if (!zone || !zoneContainsWarpPoint(*zone, warpPoint))
			continue;

		const uint zoneClick = resolveScriptZoneClick(*zone);
		setRuntimeViewAngles(currentAlpha, currentBeta, true);

		warning("Egypt: click %s mouse=%d,%d warp=%d,%d zone=%03u zoneclic=%u command=%s",
		        _currentScene.name.c_str(), mousePos.x, mousePos.y, warpPoint.x, warpPoint.y,
		        zone->id, zoneClick, zone->command.c_str());

		_pendingWarpTarget.clear();
		runPrototypeWarpScript(zoneClick, currentAlpha, currentBeta);
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
	return warpPoint.x >= (int)zone.left && warpPoint.x < (int)zone.right &&
	       warpPoint.y >= (int)zone.top && warpPoint.y < (int)zone.bottom;
}

const EgyptZone *CryOmni3DEngine_Egypt::findHoveredActiveZone(const Common::Point &warpPoint) const {
	for (Common::Array<uint>::const_iterator it = _currentScene.activeZones.begin();
	     it != _currentScene.activeZones.end(); ++it) {
		const EgyptZone *zone = findZoneById(*it);
		if (zone && zoneContainsWarpPoint(*zone, warpPoint))
			return zone;
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
	_pendingWarp.hnmName = zone.extraParam;
	_pendingWarp.zoneCommand = calledCommand ? calledCommand : zone.commandName;
	_pendingWarp.zoneParam = zone.param;
	_pendingWarp.zoneExtra = zone.extraParam;
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
	EgyptResolvedCentrage resolved;
	resolved.matched = true;
	resolved.rawFinalAlpha = sourceAlpha;
	resolved.finalBeta = sourceBeta;

	switch (centrage.op) {
	case '+':
		resolved.rawFinalAlpha += centrage.alpha;
		break;
	case '-':
		resolved.rawFinalAlpha -= centrage.alpha;
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
	_pendingRuntimeResolved.rawFinalAlpha = _pendingWarp.sourceAlpha;
	_pendingRuntimeResolved.normalizedFinalAlpha = _pendingWarp.sourceAlpha;
	_pendingRuntimeResolved.finalBeta = _pendingWarp.sourceBeta;

	Common::String matchedName;
	const EgyptCentrage *centrage = findArrivalCentrage(&matchedName);
	if (!centrage)
		return;

	_pendingRuntimeResolved = applyCentrageRaw(*centrage, _pendingWarp.sourceAlpha, _pendingWarp.sourceBeta);
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

void CryOmni3DEngine_Egypt::logWarpTrace(const Common::String &matchedName, const EgyptCentrage *matchedCentrage,
                                         const EgyptResolvedCentrage &resolved) const {
	if (!_pendingWarp.active)
		return;

	Common::String matchedCentrageText("none");
	if (matchedCentrage) {
		matchedCentrageText = Common::String::format("%s%c%0.3f",
		                                             matchedCentrage->name.c_str(),
		                                             matchedCentrage->op,
		                                             matchedCentrage->alpha);
		if (matchedCentrage->hasBeta)
			matchedCentrageText += Common::String::format(" %0.3f", matchedCentrage->beta);
	}

	warning("EGYPT_WARP_TRACE: fromScene=%s fromContext=%s zoneclic=%u called=%s zoneId=%u toScene=%s toContext=%s sourceAlpha=%0.3f sourceBeta=%0.3f matchedBy=%s matchedCentrage=%s rawFinalAlpha=%0.3f normalizedFinalAlpha=%0.3f finalBeta=%0.3f",
	        _pendingWarp.fromScene.c_str(), _pendingWarp.fromContext.c_str(), _pendingWarp.zoneclic,
	        _pendingWarp.zoneCommand.c_str(), _pendingWarp.zoneId, _pendingWarp.toScene.c_str(),
	        _pendingWarp.toContext.c_str(), _pendingWarp.sourceAlpha, _pendingWarp.sourceBeta,
	        matchedName.empty() ? "none" : matchedName.c_str(), matchedCentrageText.c_str(),
	        resolved.rawFinalAlpha, resolved.normalizedFinalAlpha, resolved.finalBeta);
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
