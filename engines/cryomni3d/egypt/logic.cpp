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
#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"
#include "graphics/surface.h"

#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

// --- Asset detection ---

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

		debugC(kDebugFile, "Egypt: asset %s %s -> %s",
		        filename.c_str(), asset.present ? "found" : "absent",
		        asset.path.toString(Common::Path::kNativeSeparator).c_str());
		result.push_back(asset);
	}

	return result;
}

// --- Script block extraction ---

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

// --- Two-phase script execution ---

// warpinit phase: run once when entering the scene.
// Sets up sprite catalog (editspr), starts music, initialises variables.
void CryOmni3DEngine_Egypt::runWarpInit() {
	if (!_currentScene.hasWarpInit) {
		warning("Egypt: scene %s has no warpinit marker", _currentScene.name.c_str());
		return;
	}
	_gameVariables[GameVariables::kZoneclic] = 0;
	Common::Array<Common::String> block = extractScriptBlock("warpinit", "endinit");
	_script.executeBlock(block, 0, 0.0, 0.0);
}

// endinit phase: run on each player interaction (and once with zoneclic=0 on scene load).
// Determines active zones and handles navigation.
void CryOmni3DEngine_Egypt::runEndInit(int zoneclic) {
	// Reset to the baseline zones that are always active (e.g. UTILISER_SUR zones
	// added by autoActivateZoneclicZones).  On the first call from runSceneStartup
	// _autoActivationZones is still empty, so this is a no-op clear - same as
	// before.  On subsequent calls (timer ticks, dialogue refresh) this mirrors
	// the EXE's per-frame model: script-managed zones are re-evaluated from scratch.
	_currentScene.activeZones = _autoActivationZones;

	if (!_currentScene.hasEndInit) {
		warning("Egypt: scene %s has no endinit marker", _currentScene.name.c_str());
		return;
	}
	_gameVariables[GameVariables::kZoneclic] = zoneclic;
	double alpha = 0.0, beta = 0.0;
	bool available = false;
	getRuntimeSourceViewAngles(alpha, beta, available);
	Common::Array<Common::String> block = extractScriptBlock("endinit", "endwarp");
	_script.executeBlock(block, (uint)zoneclic, alpha, beta);
}

// --- Zone auto-activation ---

// Safety-net: activate any zone whose id is directly compared against
// 'zoneclic' in the script but was not activated by the initial runEndInit(0) pass.
// We no longer skip zones that also appear in zoneactive/zoneinactive calls:
// those calls may be in conditional branches that don't execute at startup
// (e.g. S09 Zone 1 is activated in Suite1 only when FlagPlancheUse!=0, but
// also referenced by "if zoneclic!=1" in Suite2 for the pit-fall path).
// The alreadyActive guard is sufficient - if runEndInit(0) already activated
// the zone, we don't add it again.
void CryOmni3DEngine_Egypt::autoActivateZoneclicZones() {
	const char *needle    = "zoneclic";
	const size_t needleLen = 8;
	for (uint i = 0; i < _currentScene.scriptLines.size(); ++i) {
		Common::String lower = _currentScene.scriptLines[i];
		lower.toLowercase();
		const char *src = lower.c_str();

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
				_autoActivationZones.push_back((uint)zoneId);
				debugC(kDebugVariable, "Egypt: auto-activating zone %u in %s (referenced by zoneclic comparison)",
				        (uint)zoneId, _currentScene.name.c_str());
			}
		}
	}
}

// --- Scene startup orchestration ---

// Called from loadScene() after parseSceneDefinition() and prepareRuntimeArrivalView().
// Runs the two DEF phases, applies fallback activation, and logs the result.
void CryOmni3DEngine_Egypt::runSceneStartup() {
	_currentScene.activeZones.clear();

	// Clear before both phases: warpinit and endinit can each set a label.
	_dialogPendingLabel.clear();
	runWarpInit();
	runEndInit(0);

	// Capture the label set by either phase (warpinit has priority
	// if both set one, since endinit overwrites it; we take the last one).
	Common::String startupDlgLabel = _dialogPendingLabel;
	_dialogPendingLabel.clear();

	// Fallback: if no zone was activated by the script, enable all non-zero-rect zones.
	// This fires when holding an object blocks every zoneactive call (EXE 0x412a16).
	// Track the fallback set in _autoActivationZones so subsequent runEndInit resets
	// (timer ticks, post-dialogue) preserve them while main!=0.
	if (_currentScene.activeZones.empty()) {
		for (uint i = 0; i < _currentScene.zones.size(); ++i) {
			const EgyptZone &z = _currentScene.zones[i];
			if (z.left != 0 || z.top != 0 || z.right != 0 || z.bottom != 0) {
				_currentScene.activeZones.push_back(z.id);
				_autoActivationZones.push_back(z.id);
			}
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
	debugC(kDebugVariable, "Egypt: initial active zones for %s = [%s]",
	        _currentScene.name.c_str(), activeList.c_str());

	// Dialogue requested by the startup script (e.g. SuiteInit -> dialoguer N).
	if (!startupDlgLabel.empty()) {
		_dialog.run(startupDlgLabel);
		// The dialogue may have changed script variables (e.g. giving an item sets
		// main, auto-incrementing the object variable).  Refresh active zones so
		// any pickup zone that is now gated-out is removed immediately, rather
		// than persisting until the first timer tick.
		runEndInit(0);
	}
}

// --- Screen fade ---

// Animates a fade between the current screen and black.
// toBlack=true : fade current -> black (FADE_OUT), ~350 ms.
// toBlack=false: capture current, fill black, fade back -> current (FADE_IN).
// 18 steps x 20 ms matches the original EXE blend loop (0x417910 / 0x417d70).
void CryOmni3DEngine_Egypt::performScreenFade(bool toBlack) {
	static const uint kSteps  = 18;
	static const uint kStepMs = 20;

	Graphics::Surface snapshot;
	{
		Graphics::Surface *screen = g_system->lockScreen();
		if (!screen) {
			if (toBlack) fillSurface(0);
			return;
		}
		snapshot.copyFrom(*screen);
		g_system->unlockScreen();
	}

	if (snapshot.format.bytesPerPixel != 4) {
		warning("Egypt: performScreenFade: unexpected format bpp=%u, skipping blend",
		        snapshot.format.bytesPerPixel);
		snapshot.free();
		if (toBlack) fillSurface(0);
		return;
	}

	if (!toBlack) {
		g_system->fillScreen(0);
		g_system->updateScreen();
	}

	Graphics::Surface blended;
	blended.create(snapshot.w, snapshot.h, snapshot.format);
	const Graphics::PixelFormat &fmt = snapshot.format;

	for (uint step = 0; step < kSteps; ++step) {
		const uint factor = toBlack ? (kSteps - step) * 255 / kSteps
		                            : (step + 1)      * 255 / kSteps;

		for (int y = 0; y < snapshot.h; ++y) {
			const uint32 *srcRow = (const uint32 *)snapshot.getBasePtr(0, y);
			uint32 *dstRow       = (uint32 *)blended.getBasePtr(0, y);
			for (int x = 0; x < snapshot.w; ++x) {
				uint8 r, g, b;
				fmt.colorToRGB(srcRow[x], r, g, b);
				r = (uint8)((uint)r * factor / 255);
				g = (uint8)((uint)g * factor / 255);
				b = (uint8)((uint)b * factor / 255);
				dstRow[x] = fmt.RGBToColor(r, g, b);
			}
		}

		g_system->copyRectToScreen(blended.getPixels(), blended.pitch,
		                           0, 0, snapshot.w, snapshot.h);
		g_system->updateScreen();
		g_system->delayMillis(kStepMs);
	}

	snapshot.free();
	blended.free();

	if (toBlack) fillSurface(0);
}

// --- Scene crossfade ---

// Blends oldScreen -> newScreen over ~480 ms, matching the EXE routine at 0x418220.
// counter += 0x10 per step; factor = min(counter, 0x100); step every 30 ms.
// Formula per channel: out = srcA - ((srcA - srcB) * factor >> 8)  (lerp old->new).
// 16 visible blend steps (0x10..0x100) then 3 hold steps (0x110..0x130) -> ~570 ms total.
void CryOmni3DEngine_Egypt::performCrossFade(const Graphics::Surface *newScreen) {
	static const int kStepMs  = 30;
	static const int kStep    = 0x10;
	static const int kFull    = 0x100;
	static const int kEnd     = 0x130;

	if (!_hasCrossFadeOldScreen || !newScreen) {
		_hasCrossFadeOldScreen = false;
		return;
	}

	const Graphics::Surface &oldS = _crossFadeOldScreen;
	if (oldS.format.bytesPerPixel != 4 || newScreen->format.bytesPerPixel != 4) {
		warning("Egypt: performCrossFade: unexpected bpp (old=%u new=%u), skipping",
		        oldS.format.bytesPerPixel, newScreen->format.bytesPerPixel);
		_hasCrossFadeOldScreen = false;
		_crossFadeOldScreen.free();
		return;
	}

	const int w = MIN(MIN((int)oldS.w, (int)newScreen->w), 640);
	const int h = MIN(MIN((int)oldS.h, (int)newScreen->h), 480);
	const Graphics::PixelFormat &fmt = oldS.format;

	Graphics::Surface blended;
	blended.create(w, h, fmt);

	for (int counter = kStep; counter <= kEnd && !shouldAbort(); counter += kStep) {
		const int factor = MIN(counter, kFull);

		for (int y = 0; y < h; ++y) {
			const uint32 *srcA = (const uint32 *)oldS.getBasePtr(0, y);
			const uint32 *srcB = (const uint32 *)newScreen->getBasePtr(0, y);
			uint32 *dst        = (uint32 *)blended.getBasePtr(0, y);
			for (int x = 0; x < w; ++x) {
				uint8 ar, ag, ab, br, bg, bb;
				fmt.colorToRGB(srcA[x], ar, ag, ab);
				fmt.colorToRGB(srcB[x], br, bg, bb);
				const uint8 r = (uint8)((int)ar - (((int)ar - (int)br) * factor >> 8));
				const uint8 g = (uint8)((int)ag - (((int)ag - (int)bg) * factor >> 8));
				const uint8 b = (uint8)((int)ab - (((int)ab - (int)bb) * factor >> 8));
				dst[x] = fmt.RGBToColor(r, g, b);
			}
		}

		g_system->copyRectToScreen(blended.getPixels(), blended.pitch, 0, 0, w, h);
		g_system->updateScreen();
		g_system->delayMillis(kStepMs);
	}

	blended.free();
	_crossFadeOldScreen.free();
	_hasCrossFadeOldScreen = false;
}

// --- HNM sequence player ---

// Plays an ordered, slash-joined list of HNM tokens.
// FADE_OUT / FADE_IN are internal blend effects; other tokens are resolved as
// HNM/<token>.HNS with HNM/FR/<token>.HNS as fallback.
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
			performScreenFade(true);
			continue;
		}

		if (token.equalsIgnoreCase("FADE_IN")) {
			performScreenFade(false);
			continue;
		}

		const Common::Path path = getFilePath(kFileTypeHnm, token);
		if (path.empty()) {
			warning("Egypt: HNM token '%s' not found in HNM/ or HNM/FR/", token.c_str());
			continue;
		}

		// Use HNMDecoder (audio) with the VBL speed from the file header (timing).
		playHnmWithSpeed(path);
	}
}


// --- Script timer ---

void CryOmni3DEngine_Egypt::resetScriptTimer() {
	_scriptTimerStartMs = g_system->getMillis();
	_gameVariables[GameVariables::kTimer] = 0;
	debugC(kDebugVariable, "Egypt: timer reset for scene %s", _currentScene.name.c_str());
}

void CryOmni3DEngine_Egypt::updateScriptTimer() {
	const uint32 elapsed = g_system->getMillis() - _scriptTimerStartMs;
	_gameVariables[GameVariables::kTimer] = (uint)(elapsed / 10); // hundredths of a second
}

// EXE "fonction SABLIER" (5), dispatch at 0x8126cc. Every Level 6 (Karnak)
// scene calls this once per visit, then does "if FlagSablier=3 aller_warp N"
// to a time's-up branch. FlagSablier states:
//   0 = not armed (the priest has not left K38 yet)
//   1 = armed this frame (set by K38.DEF once FlagHorologueK38Parti != 0)
//   2 = running (real-time countdown, ~327 s)
//   3 = expired -> the per-scene script warps to the game-over branch
//   4 = disabled/success (both loot pieces shown)
// The EXE times this with QueryPerformanceCounter scaled to centiseconds
// ((counter - base) * 100 / frequency, 0x81a930) against a 0x7fff-cs limit,
// resetting the base (0x4d1e58) on the 1 -> 2 transition (0x81a9a0).
void CryOmni3DEngine_Egypt::applySablier() {
	// Inactive during scene-revisit mode (EXE checks FlagVisite first).
	if (_gameVariables[GameVariables::kFlagVisite] != 0)
		return;

	const uint sablier = _gameVariables[GameVariables::kFlagSablier];
	// Nothing to do before the hourglass is armed or after it has expired.
	if (sablier == 0 || sablier == 3)
		return;

	// First call after arming: start counting and reset the real-time base.
	if (sablier == 1) {
		_gameVariables[GameVariables::kFlagSablier] = 2;
		_sablierStartMs = g_system->getMillis();
	}

	// Success shortcut: both required loot pieces shown -> stop the timer.
	if (_gameVariables[GameVariables::kFlagNbButinMontre] == 2) {
		_gameVariables[GameVariables::kFlagSablier] = 4;
		_gameVariables[GameVariables::kFlagTeleporteK12] = 0;
		return;
	}

	// Only state 2 (already running) runs the expiry check; the arming call
	// (original value 1) just started the clock this frame.
	if (sablier != 2)
		return;

	// Defensive: if the base is unset (e.g. a game saved mid-countdown and
	// reloaded), re-arm instead of instantly expiring the player.
	if (_sablierStartMs == 0)
		_sablierStartMs = g_system->getMillis();

	// Elapsed centiseconds, compared against the EXE 0x7fff-cs limit (~327.7 s).
	// The EXE also draws a 1..11 hourglass level (0x4d1bdc) in the toolbar; the
	// port does not render that HUD element yet, but the timer still expires.
	const uint32 elapsedCs = (g_system->getMillis() - _sablierStartMs) / 10;
	if (elapsedCs >= 0x7fff) {
		_gameVariables[GameVariables::kFlagMouseVisee] = 0;
		_gameVariables[GameVariables::kFlagSablier] = 3;
		_gameVariables[GameVariables::kFlagTeleporteK12] = 1;
		debugC(kDebugVariable, "Egypt: SABLIER expired (%u cs) -> FlagSablier=3", elapsedCs);
	}
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
