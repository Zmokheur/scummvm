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

#ifndef CRYOMNI3D_EGYPT_DIALOG_H
#define CRYOMNI3D_EGYPT_DIALOG_H

#include "audio/mixer.h"

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/str.h"

#include "graphics/surface.h"

namespace Common {
class Path;
}

namespace Graphics {
class ManagedSurface;
}

namespace CryOmni3D {
namespace Egypt {

class CryOmni3DEngine_Egypt;

struct EgyptDialogNode {
	Common::String label;
	Common::String text;
	Common::Array<Common::String> commands;
};

struct EgyptDialogChoice {
	Common::String targetLabel;
	Common::String displayText;
};

// SPA/SPB portrait patch sprite.
// Each frame is a TXEN/RLE block with its own screen coordinates baked in.
struct EgyptDialogSprite {
	uint frameCount = 0;
	Common::Array<byte> data;  // decompressed SPA/SPB (8-byte entry table + TXEN/RLE blocks)

	bool empty() const { return frameCount == 0; }
	void clear() { frameCount = 0; data.clear(); }
};

// One event from a .SYC mouth-sync file.
struct EgyptSycEvent {
	uint32 timeMs;
	uint32 phonemeCode;
};

// Outcome produced by executing a single dialogue node's commands.
// The caller displays node.text (if non-empty) then acts on this.
enum EgyptDialogResult {
	kDlgEnd,      // end command - close dialogue
	kDlgJump,     // goto single label  (outNextLabel is set)
	kDlgChoices,  // goto with Ramose labels (outChoices is populated)
	kDlgShow      // show command - for future use (treated as end for now)
};

// NPC dialogue system: Level.txt dialogue trees, TGA/SPA/SPB portrait
// rendering, APC voice playback and SYC mouth synchronisation.
// This is Egypt's own system (the shared GTO DialogsManager does not
// match the game's data formats). Friend-manager pattern: reads game
// variables and the event pump through the engine pointer.
class Egypt_Dialog {
public:
	explicit Egypt_Dialog(CryOmni3DEngine_Egypt *engine) : _engine(engine) {}
	~Egypt_Dialog() { _tga.free(); }

	// Runs a full dialogue starting at the given Level.txt label
	void run(const Common::String &startLabel);

	// Invalidate the cached Level.txt (called when a new game session starts)
	void resetLevelCache() { _levelLoaded = false; }

	// Look up the subtitle text for a Level.txt label without running the full
	// dialogue UI. Used by the Senet mini-game (Phase E) to show its comment
	// lines. Loads Level.txt on first use; returns false if the label is absent.
	bool getLineText(const Common::String &label, Common::String &out);

private:
	bool loadLevelTxt();
	const EgyptDialogNode *findNode(const Common::String &label) const;
	EgyptDialogResult executeNode(const Common::String &label,
	                              Common::String &outText,
	                              Common::Array<EgyptDialogChoice> &outChoices,
	                              Common::String &outNextLabel);
	bool loadSprite(const Common::Path &path, EgyptDialogSprite &out);
	void loadSpeaker(const Common::String &speakerName, int level);
	void loadSyc(const Common::String &label);
	void blitSpriteFrame(Graphics::ManagedSurface &dst,
	                     const EgyptDialogSprite &sprite, uint frame);
	void playVoice(const Common::String &label);
	void stopVoice();
	void showText(const Graphics::ManagedSurface &background, const Common::String &text);
	int  showChoices(const Graphics::ManagedSurface &background,
	                 const Common::Array<EgyptDialogChoice> &choices);

	CryOmni3DEngine_Egypt *_engine;

	Common::HashMap<Common::String, EgyptDialogNode,
	                Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _nodes;
	bool _levelLoaded = false;
	Graphics::Surface _tga;          // static base portrait image (TGA)
	EgyptDialogSprite _spa;          // face/idle patches (TXEN/RLE)
	EgyptDialogSprite _spb;          // mouth patches (TXEN/RLE, driven by SYC)
	Common::String    _speakerName;
	Audio::SoundHandle _voiceHandle;
	Common::Array<EgyptSycEvent> _sycEvents;
	uint _sycEventIdx = 0;
	uint _idleFrameIdx = 0;          // index into the idle mouth frame cycle (SPA)
	uint32 _idleNextMs = 0;          // next idle animation tick
	uint32 _nodeStartMs = 0;         // timestamp when the current node started
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
