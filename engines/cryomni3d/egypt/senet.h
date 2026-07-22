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

#ifndef CRYOMNI3D_EGYPT_SENET_H
#define CRYOMNI3D_EGYPT_SENET_H

#include "common/array.h"
#include "common/random.h"
#include "common/scummsys.h"
#include "common/str.h"

#include "audio/mixer.h"

#include "graphics/managed_surface.h"

#include "cryomni3d/egypt/cursor.h" // EgyptInterfaceSprite

namespace CryOmni3D {
namespace Egypt {

class CryOmni3DEngine_Egypt;

// Senet mini-game played once in Level 5 (scene M45PLAN, via
// "fonction JEU_SENET"). Self-contained modal sub-game with its own
// background (SENET.TGA), sprites (SENET.SPR), 30-cell board, four throwing
// sticks and an opponent AI. The complete rule set is reversed from the EXE
// (routines 0x4158e0..0x4169e0) in egypte-doc/senet_exe_rules.md.
//
// Friend-manager pattern like Egypt_Dialog: reads input and the event pump
// through the engine pointer, runs modally until the game ends. The EXE is
// frame-driven (re-enters 0x4159a0 every frame); this port folds that state
// machine into a modal loop, which is behaviorally identical because the EXE
// short-circuits all scene interaction while the game runs.
class Egypt_Senet {
public:
	explicit Egypt_Senet(CryOmni3DEngine_Egypt *engine) :
		_engine(engine), _rnd("egyptSenet") {}
	~Egypt_Senet() { _background.free(); }

	// Runs the mini-game modally. Returns the value the M45PLAN script stores
	// into the "tmp" variable: 1 = player (Ramose) victory or ESC skip
	// (-> dialogue mpa4598), 0 = defeat (-> dialogue mpa4599).
	int run();

private:
	// Number of board cells (EXE array 0x4d1bf8).
	static const int kCellCount = 30;
	// Logical index of the House of Water (trap) and the House of Rebirth.
	static const int kCellWater = 26;    // EXE 0x1a
	static const int kCellRebirth = 14;  // EXE 0x0e

	// SENET.SPR sprite indices. 0..7 are the throwing sticks (side + variant*2).
	enum SenetSprite {
		kSpritePlayerPawn   = 8,  // board value 1 (Ramose)
		kSpriteOpponentPawn = 9,  // board value 2 (Panehesy)
		kSpriteReticle      = 10  // hover reticle
	};

	// Board cell values.
	enum CellValue {
		kCellEmpty    = 0,
		kCellPlayer   = 1,  // Ramose (the human)
		kCellOpponent = 2   // Panehesy (the AI)
	};

	// One thrown stick (EXE struct at 0x4d1c30, 8 bytes: sprite, x, y).
	struct Stick {
		uint sprite;
		int16 x, y;
	};

	bool load();                                   // one-time resource load
	void initBoard();                              // EXE 0x416030

	// Rules (see senet_exe_rules.md).
	int throwSticks();                             // EXE 0x816720 -> score 1..5
	static bool isReplayThrow(int score) {         // EXE 0x8163d0
		return score == 1 || score == 4 || score == 5;
	}
	int countRange(int value, int start, int len) const; // EXE 0x8163f0
	int countPieces(int value) const { return countRange(value, 0, kCellCount); }
	int evalMove(int from, int throwVal) const;    // EXE 0x816070 (0 = illegal)
	void applyMove(int from, int throwVal);        // EXE 0x816210
	int chooseBestMove(int value, int throwVal) const; // EXE 0x816370

	// Input hit-testing.
	int hitTestCell(const Common::Point &mouse) const;      // EXE 0x816830
	bool hitTestThrowZone(const Common::Point &mouse) const; // EXE 0x816880

	// Rendering / pacing.
	void drawBoard();                              // EXE 0x416460
	void blitSprite(uint spriteId, int centerX, int centerY) const;
	void blitSpriteTopLeft(uint spriteId, int x, int y) const;
	// Draw a side's borne-off pawns as a stacked counter (EXE drawBoard: two
	// rows, wrapping after four, one pawn sprite per piece already home).
	void drawBorneOffCounter(uint spriteId, int count,
	                         int startX, int rowY1, int rowY2) const;
	void render();                                 // drawBoard + flush to screen
	void blinkPiece(int cell);                     // EXE 0x816910 (opponent tell)
	void pumpedDelay(uint32 ms);                   // EXE 0x8167e0, event-pumping

	// Speech / subtitles (Phase E, EXE playVoice 0x816990 + clear 0x8169e0).
	// The master label array (real VA 0x436590) maps a voice index to a
	// Level.txt label: 0..8 = mpa4504..mpa4512, 9..12 = mca4591/mcb4592/
	// mcc4593/mcd4594 (ambient). See senet_exe_rules.md.
	static const char *labelForIndex(int idx);
	bool subtitleActive() const;                   // a line is still on screen
	void playLine(int idx);                        // start voice + subtitle for idx
	void updateAmbientAndIdle();                   // 5-min ambient + 20-s reminder

	CryOmni3DEngine_Egypt *_engine;
	// Mutable: evalMove() is const but the EXE consults the RNG in one
	// tie-break branch (0x8161a7), and chooseBestMove()/const callers use it.
	mutable Common::RandomSource _rnd;

	bool _loaded = false;
	Graphics::ManagedSurface _background;          // SENET.TGA (640x480)
	Common::Array<EgyptInterfaceSprite> _sprites;  // SENET.SPR sheet
	Graphics::ManagedSurface *_surface = nullptr;  // active frame buffer (in run)

	byte _board[kCellCount];
	Stick _sticks[4];
	int _throwValue = 0;    // current throw (1..5)
	bool _playerTurn = true;
	bool _movePending = false; // player threw and a legal move exists
	int _replayCounter = 0;    // consecutive replay-throw streak (EXE 0x4d1c60)
	bool _specialMove = false; // water/rebirth happened this turn (EXE 0x4d1c54):
	                           // suppresses the "good move" comment that turn

	// Speech state.
	Audio::SoundHandle _voiceHandle;
	Common::String _subtitleText;        // current on-screen line ("" = none)
	uint32 _subtitleStartMs = 0;         // when it was shown (EXE 0x4d1bec)
	uint32 _lastActivityMs = 0;          // for the 20-s reminder (EXE 0x4d1c28)
	uint32 _ambientBaseMs = 0;           // for the 5-min ambient (EXE 0x4d1be8)
	int _ambientIdx = 0;                 // ambient line cursor 0..3 (EXE 0x4d1c58)
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
