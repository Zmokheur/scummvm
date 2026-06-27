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

#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

// ── Asset detection ───────────────────────────────────────────────────────────

namespace {

struct AssetProbe {
	EgyptAssetKind kind;
	const char    *suffix;   // appended to sceneName before extension
	const char    *ext;
	bool           inWarp;   // true = WARP/ ; false = SPRITE/LEVELx/
};

const AssetProbe kAssetProbes[] = {
	{ kAssetWarpHNM,  "_24", ".HNM", true  },
	{ kAssetBackTGA,  "",    ".TGA", false },
	{ kAssetSceneSPR, "",    ".SPR", false },
	{ kAssetCharSPRA, "A",   ".SPR", false },
	{ kAssetCharSPRB, "B",   ".SPR", false },
};

} // anonymous namespace

Common::Array<EgyptSceneAsset> CryOmni3DEngine_Egypt::detectSceneAssets(
		const Common::String &sceneName, int level) const {
	Common::Array<EgyptSceneAsset> result;

	for (uint p = 0; p < ARRAYSIZE(kAssetProbes); ++p) {
		const AssetProbe &probe = kAssetProbes[p];
		EgyptSceneAsset asset;
		asset.kind    = probe.kind;
		asset.present = false;

		Common::String filename = sceneName + probe.suffix + probe.ext;

		if (probe.inWarp) {
			asset.path    = Common::Path("WARP/" + filename);
			asset.present = Common::File::exists(asset.path);
		} else {
			const int startLevel = (level >= 1 && level <= 6) ? level : 1;
			for (int li = 0; li <= 6 && !asset.present; ++li) {
				int tryLevel = (li == 0) ? startLevel : li;
				if (li > 0 && tryLevel == startLevel)
					continue;  // don't probe startLevel twice
				Common::Path tryPath(Common::String::format("SPRITE/LEVEL%d/%s",
				                                             tryLevel, filename.c_str()));
				if (Common::File::exists(tryPath)) {
					asset.path    = tryPath;
					asset.present = true;
				}
			}
			if (!asset.present)
				asset.path = Common::Path(Common::String::format("SPRITE/LEVEL%d/%s",
				                                                   startLevel, filename.c_str()));
		}

		warning("Egypt: asset %s %s → %s",
		        filename.c_str(), asset.present ? "found" : "absent",
		        asset.path.toString(Common::Path::kNativeSeparator).c_str());
		result.push_back(asset);
	}

	return result;
}

// ── Script block extraction ───────────────────────────────────────────────────

// Returns lines strictly between fromMarker and toMarker.
// A marker is recognised only when it sits at column 0 (no leading whitespace),
// which distinguishes it from indented labels that happen to share the same text.
Common::Array<Common::String> CryOmni3DEngine_Egypt::extractScriptBlock(
		const Common::String &fromMarker, const Common::String &toMarker) const {
	Common::Array<Common::String> result;
	bool inBlock = false;

	for (uint i = 0; i < _currentScene.scriptLines.size(); ++i) {
		const Common::String &rawLine = _currentScene.scriptLines[i];

		const bool hasIndent = !rawLine.empty() &&
		                       (rawLine[0] == ' ' || rawLine[0] == '\t');
		Common::String trimmed = rawLine;
		trimmed.trim();

		if (!hasIndent && trimmed.equalsIgnoreCase(fromMarker)) {
			inBlock = true;
			continue;
		}

		if (!inBlock)
			continue;

		if (!hasIndent && trimmed.equalsIgnoreCase(toMarker))
			break;

		result.push_back(rawLine);
	}

	return result;
}

// ── Two-phase script execution ────────────────────────────────────────────────

// warpinit phase: run once when entering the scene.
// Sets up sprite catalog (editspr), starts music, initialises variables.
void CryOmni3DEngine_Egypt::runWarpInit() {
	if (!_currentScene.hasWarpInit) {
		warning("Egypt: scene %s has no warpinit marker", _currentScene.name.c_str());
		return;
	}
	_scriptVariables["zoneclic"] = 0;
	Common::Array<Common::String> block = extractScriptBlock("warpinit", "endinit");
	executeScriptBlock(block, 0, 0.0, 0.0);
}

// endinit phase: run on each player interaction (and once with zoneclic=0 on scene load).
// Determines active zones and handles navigation.
void CryOmni3DEngine_Egypt::runEndInit(int zoneclic) {
	if (!_currentScene.hasEndInit) {
		warning("Egypt: scene %s has no endinit marker", _currentScene.name.c_str());
		return;
	}
	_scriptVariables["zoneclic"] = zoneclic;
	double alpha = 0.0, beta = 0.0;
	bool available = false;
	getRuntimeSourceViewAngles(alpha, beta, available);
	Common::Array<Common::String> block = extractScriptBlock("endinit", "endwarp");
	executeScriptBlock(block, (uint)zoneclic, alpha, beta);
}

// ── Zone auto-activation ──────────────────────────────────────────────────────

// Safety-net: activate any zone whose id is directly compared against
// 'zoneclic' in the script but was not explicitly activated by it.
void CryOmni3DEngine_Egypt::autoActivateZoneclicZones() {
	for (uint i = 0; i < _currentScene.scriptLines.size(); ++i) {
		Common::String lower = _currentScene.scriptLines[i];
		lower.toLowercase();
		const char *src       = lower.c_str();
		const char *needle    = "zoneclic";
		const size_t needleLen = 8;

		for (const char *pos = strstr(src, needle); pos != nullptr;
		     pos = strstr(pos + needleLen, needle)) {
			const char *cursor = pos + needleLen;
			if (*cursor == '!' && *(cursor + 1) == '=')
				cursor += 2;
			else if (*cursor == '=')
				cursor += 1;
			else
				continue;

			if (!(*cursor >= '0' && *cursor <= '9'))
				continue;

			char *endPtr = nullptr;
			const long zoneId = strtol(cursor, &endPtr, 10);
			if (endPtr == cursor || zoneId <= 0)
				continue;

			const EgyptZone *zone = findZoneById((uint)zoneId);
			if (!zone || (zone->left == 0 && zone->top == 0 &&
			              zone->right == 0 && zone->bottom == 0))
				continue;

			bool alreadyActive = false;
			for (uint ai = 0; ai < _currentScene.activeZones.size(); ++ai) {
				if (_currentScene.activeZones[ai] == (uint)zoneId) {
					alreadyActive = true;
					break;
				}
			}
			if (!alreadyActive) {
				_currentScene.activeZones.push_back((uint)zoneId);
				warning("Egypt: auto-activating zone %u in %s (referenced by zoneclic comparison)",
				        (uint)zoneId, _currentScene.name.c_str());
			}
		}
	}
}

// ── Scene startup orchestration ───────────────────────────────────────────────

// Called from loadScene() after parseSceneDefinition() and prepareRuntimeArrivalView().
// Runs the two DEF phases, applies fallback activation, and logs the result.
void CryOmni3DEngine_Egypt::runSceneStartup() {
	_currentScene.activeZones.clear();

	runWarpInit();
	runEndInit(0);

	// Fallback: if no zone was activated by the script, enable all non-zero-rect zones
	if (_currentScene.activeZones.empty()) {
		for (uint i = 0; i < _currentScene.zones.size(); ++i) {
			const EgyptZone &z = _currentScene.zones[i];
			if (z.left != 0 || z.top != 0 || z.right != 0 || z.bottom != 0)
				_currentScene.activeZones.push_back(z.id);
		}
		warning("Egypt: no zones activated in %s, enabling all non-zero-rect zones",
		        _currentScene.name.c_str());
	}

	autoActivateZoneclicZones();

	Common::String activeList;
	for (uint i = 0; i < _currentScene.activeZones.size(); ++i) {
		if (!activeList.empty()) activeList += ",";
		activeList += Common::String::format("%u", _currentScene.activeZones[i]);
	}
	warning("Egypt: initial active zones for %s = [%s]",
	        _currentScene.name.c_str(), activeList.c_str());
}

// ── HNM sequence player ───────────────────────────────────────────────────────

// Plays an ordered, slash-joined list of HNM tokens.
// FADE_OUT is handled as an engine effect (black frame); other tokens are
// resolved as HNM/<token>.HNS with HNM/FR/<token>.HNS as fallback.
void CryOmni3DEngine_Egypt::executeHnmSequence(const Common::String &hnmJoined) {
	if (hnmJoined.empty())
		return;

	Common::StringTokenizer tok(hnmJoined, "/");
	while (!tok.empty()) {
		if (shouldAbort())
			break;

		Common::String token = tok.nextToken();
		if (token.empty())
			continue;

		if (token.equalsIgnoreCase("FADE_OUT")) {
			fillSurface(0);
			g_system->updateScreen();
			g_system->delayMillis(200);
			continue;
		}

		Common::Path path(Common::String::format("HNM/%s.HNS", token.c_str()));
		if (!Common::File::exists(path)) {
			path = Common::Path(Common::String::format("HNM/FR/%s.HNS", token.c_str()));
			if (!Common::File::exists(path)) {
				warning("Egypt: HNM token '%s' not found in HNM/ or HNM/FR/", token.c_str());
				continue;
			}
		}

		playHnmFile(path);
	}
}


// ── Script timer ──────────────────────────────────────────────────────────────

void CryOmni3DEngine_Egypt::resetScriptTimer() {
	_scriptTimerStartMs = g_system->getMillis();
	_scriptVariables["timer"] = 0;
	warning("Egypt: timer reset for scene %s", _currentScene.name.c_str());
}

void CryOmni3DEngine_Egypt::updateScriptTimer() {
	const uint32 elapsed = g_system->getMillis() - _scriptTimerStartMs;
	_scriptVariables["timer"] = (int)(elapsed / 10); // centièmes de seconde
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
