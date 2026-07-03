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
#include "common/savefile.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

// Save file layout (<target>.NNNN, NNNN = slot + 1):
//   byte[kSaveDescriptionLen]  description, zero-padded (read by the
//                              metaengine's listSaves, like Versailles)
//   uint32BE                   magic 'EGYS'
//   uint32LE                   format version (kSaveVersion)
//   uint32LE                   game variable count, then that many uint32LE
//   string                     current scene name   (uint32LE length + bytes)
//   string                     current context name (JOUR/NUIT chain)
//   doubleLE x2 + byte         camera alpha, beta, angles-available flag
//   string                     pending eye-warp return scene
// Everything else (zones, centrages, overlays, dialogue caches, timers) is
// recomputed by the normal loadScene() startup path.
static const uint32 kSaveMagic = MKTAG('E', 'G', 'Y', 'S');
static const uint32 kSaveVersion = 1;

static void writeSaveString(Common::WriteStream &out, const Common::String &str) {
	out.writeUint32LE(str.size());
	out.write(str.c_str(), str.size());
}

static Common::String readSaveString(Common::ReadStream &in) {
	const uint32 len = in.readUint32LE();
	if (len == 0 || len > 1024)
		return Common::String();
	Common::Array<char> buf;
	buf.resize(len);
	if (in.read(buf.data(), len) != len)
		return Common::String();
	return Common::String(buf.data(), len);
}

bool CryOmni3DEngine_Egypt::hasFeature(EngineFeature f) const {
	return CryOmni3DEngine::hasFeature(f)
	       || (f == kSupportsSavingDuringRuntime)
	       || (f == kSupportsLoadingDuringRuntime);
}

bool CryOmni3DEngine_Egypt::canSaveGameStateCurrently(Common::U32String *msg) {
	return _canLoadSave;
}

bool CryOmni3DEngine_Egypt::canLoadGameStateCurrently(Common::U32String *msg) {
	return _canLoadSave;
}

Common::Error CryOmni3DEngine_Egypt::saveGameState(int slot, const Common::String &desc, bool isAutosave) {
	return saveGameToSlot((uint)slot + 1, desc) ? Common::kNoError : Common::kWritingFailed;
}

Common::Error CryOmni3DEngine_Egypt::loadGameState(int slot) {
	// Defer the actual load: the run() loop applies it at a safe point
	// (Versailles' abort-command pattern).
	_pendingLoadSlot = slot;
	return Common::kNoError;
}

Common::String CryOmni3DEngine_Egypt::getSaveStateName(int slot) const {
	return Common::String::format("%s.%04u", _targetName.c_str(), slot + 1);
}

bool CryOmni3DEngine_Egypt::saveGameToSlot(uint saveNum, const Common::String &desc) {
	const Common::String saveFileName = Common::String::format("%s.%04u", _targetName.c_str(), saveNum);
	Common::OutSaveFile *out = _saveFileMan->openForSaving(saveFileName);
	if (!out) {
		warning("Egypt: cannot open save file %s for writing", saveFileName.c_str());
		return false;
	}

	// Fixed-size description header, read back by the metaengine's listSaves
	char descC[kSaveDescriptionLen];
	memset(descC, 0, sizeof(descC));
	strncpy(descC, desc.c_str(), kSaveDescriptionLen);
	out->write(descC, kSaveDescriptionLen);

	out->writeUint32BE(kSaveMagic);
	out->writeUint32LE(kSaveVersion);

	out->writeUint32LE(_gameVariables.size());
	for (uint i = 0; i < _gameVariables.size(); ++i)
		out->writeUint32LE(_gameVariables[i]);

	writeSaveString(*out, _currentScene.name);
	writeSaveString(*out, _currentContextName);

	out->writeDoubleLE(_currentViewAlpha);
	out->writeDoubleLE(_currentViewBeta);
	out->writeByte(_currentViewAnglesAvailable ? 1 : 0);

	writeSaveString(*out, _pendingReturnScene);

	out->finalize();
	const bool ok = !out->err();
	delete out;

	debugC(kDebugSaveLoad, "Egypt: saved slot %u scene=%s (%s)",
	       saveNum, _currentScene.name.c_str(), ok ? "ok" : "write error");
	return ok;
}

bool CryOmni3DEngine_Egypt::loadGameFromSlot(uint saveNum, Common::String &sceneName) {
	const Common::String saveFileName = Common::String::format("%s.%04u", _targetName.c_str(), saveNum);
	Common::InSaveFile *in = _saveFileMan->openForLoading(saveFileName);
	if (!in) {
		warning("Egypt: cannot open save file %s", saveFileName.c_str());
		return false;
	}

	in->skip(kSaveDescriptionLen);

	if (in->readUint32BE() != kSaveMagic) {
		warning("Egypt: save file %s has an invalid magic", saveFileName.c_str());
		delete in;
		return false;
	}
	const uint32 version = in->readUint32LE();
	if (version == 0 || version > kSaveVersion) {
		warning("Egypt: save file %s has unsupported version %u", saveFileName.c_str(), version);
		delete in;
		return false;
	}

	// Variables: zero-fill first so saves from older (shorter) variable
	// tables load cleanly - the enum is append-only.
	Common::fill(_gameVariables.begin(), _gameVariables.end(), 0u);
	const uint32 varCount = in->readUint32LE();
	for (uint32 i = 0; i < varCount; ++i) {
		const uint32 value = in->readUint32LE();
		if (i < _gameVariables.size())
			_gameVariables[i] = value;
	}

	sceneName = readSaveString(*in);
	_currentContextName = readSaveString(*in);
	if (_currentContextName.empty())
		_currentContextName = "NUIT";

	const double alpha = in->readDoubleLE();
	const double beta = in->readDoubleLE();
	const bool anglesAvailable = in->readByte() != 0;

	_pendingReturnScene = readSaveString(*in);

	const bool ok = !in->err() && !sceneName.empty();
	delete in;

	if (!ok) {
		warning("Egypt: save file %s is truncated or corrupt", saveFileName.c_str());
		return false;
	}

	// Restore the camera: with no pending warp, the rotation view picks
	// these angles up directly (the "inherited angles" path).
	clearPendingWarpRequest();
	setRuntimeViewAngles(alpha, beta, anglesAvailable);
	_dialogPendingLabel.clear();
	_pendingWarpTarget.clear();

	// The loaded game is resumable from the main menu like a live session.
	_isPlaying = true;
	_savedSceneName = sceneName;

	debugC(kDebugSaveLoad, "Egypt: loaded slot %u scene=%s context=%s alpha=%0.3f beta=%0.3f",
	       saveNum, sceneName.c_str(), _currentContextName.c_str(), alpha, beta);
	return true;
}

// Consumes _pendingLoadSlot; returns the scene to enter, or empty on failure
Common::String CryOmni3DEngine_Egypt::applyPendingLoad() {
	const int slot = _pendingLoadSlot;
	_pendingLoadSlot = -1;

	Common::String sceneName;
	if (slot < 0 || !loadGameFromSlot((uint)slot + 1, sceneName))
		return Common::String();
	return sceneName;
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
