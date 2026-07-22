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

#include "audio/audiostream.h"
#include "audio/mixer.h"
#include "audio/decoders/wave.h"

#include "common/file.h"

#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

// Ambient background music. The scene scripts name the loop directly
// ("music s0"), so - unlike Versailles' place-id table - all we need is to
// open MUSIC/<name>.WAV (standard PCM), loop it forever on the music channel
// and remember which track is playing so a repeated request for the same
// loop does not restart it.
void CryOmni3DEngine_Egypt::playAmbientMusic(const Common::String &name) {
	Common::String normalized = name;
	normalized.trim();
	if (normalized.empty())
		return;

	if (normalized.equalsIgnoreCase(_musicCurrentFile) &&
	    _mixer->isSoundHandleActive(_musicHandle)) {
		// Same loop already playing - nothing to do.
		return;
	}

	stopAmbientMusic();

	Common::Path musicPath = getFilePath(kFileTypeMusic, normalized);
	Common::File *musicFile = new Common::File();
	if (!musicFile->open(musicPath)) {
		warning("Egypt: failed to open music %s", musicPath.toString(Common::Path::kNativeSeparator).c_str());
		delete musicFile;
		return;
	}

	Audio::SeekableAudioStream *decoder = Audio::makeWAVStream(musicFile, DisposeAfterUse::YES);
	musicFile = nullptr; // ownership transferred to the decoder
	if (!decoder) {
		warning("Egypt: failed to decode music %s", musicPath.toString(Common::Path::kNativeSeparator).c_str());
		return;
	}

	Audio::AudioStream *loopStream = Audio::makeLoopingAudioStream(decoder, 0);
	decoder = nullptr; // ownership transferred to the looping stream

	_mixer->playStream(Audio::Mixer::kMusicSoundType, &_musicHandle, loopStream);
	_musicCurrentFile = normalized;
}

void CryOmni3DEngine_Egypt::stopAmbientMusic() {
	if (_mixer->isSoundHandleActive(_musicHandle))
		_mixer->stopHandle(_musicHandle);
	_musicCurrentFile.clear();
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
