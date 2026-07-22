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

#include "audio/decoders/apc.h"
#include "audio/mixer.h"

#include "common/debug.h"
#include "common/endian.h"
#include "common/file.h"
#include "common/keyboard.h"
#include "common/memstream.h"
#include "common/rect.h"
#include "common/system.h"

#include "graphics/managed_surface.h"

#include "cryomni3d/egypt/senet.h"

#include "cryomni3d/egypt/dialog.h"
#include "cryomni3d/egypt/engine.h"
#include "cryomni3d/egypt/sprite.h"
#include "cryomni3d/egypt/support/image_loader.h"

namespace CryOmni3D {
namespace Egypt {

namespace {

// Screen coordinates of the 30 board cells (EXE table 0x434020, int16 x,y
// pairs). Logical order is a linear path: first row left->right, second row
// right->left, third row left->right. Cell 26 (0x1a) is the House of Water.
struct CellPos { int16 x, y; };
const CellPos kCellPos[30] = {
	{  49,  78 }, { 105,  79 }, { 162,  81 }, { 218,  83 }, { 272,  82 },
	{ 328,  83 }, { 385,  84 }, { 439,  85 }, { 496,  87 }, { 551,  88 },
	{ 556, 126 }, { 498, 126 }, { 441, 125 }, { 385, 124 }, { 328, 123 },
	{ 271, 121 }, { 214, 121 }, { 157, 119 }, {  99, 118 }, {  42, 117 },
	{  35, 158 }, {  94, 158 }, { 152, 159 }, { 211, 160 }, { 269, 162 },
	{ 327, 163 }, { 385, 164 }, { 443, 165 }, { 502, 165 }, { 560, 167 }
};

// Pawn draw anchor (EXE drawBoard 0x416460): cell (x,y) + (25, 35), then the
// sprite is centered on that point. The EXE nudges the player pawn up by 8 and
// the opponent pawn up by 6 (it subtracts the sprite index from Y).
const int kPawnAnchorX = 25;
const int kPawnAnchorY = 35;

// Cell hit rectangle (EXE hitTestCell 0x816830, w=0x23 h=0x32).
const int kCellHitW = 35;
const int kCellHitH = 50;

// Fallback throw zone when no thrown stick is under the cursor
// (EXE hitTestThrowZone 0x816880: rect x=307 y=90 w=105 h=150).
const Common::Rect kThrowZone(307, 90, 307 + 105, 90 + 150);

// Speech (Phase E). A voice line stays on screen 4000 ms (EXE clear routine
// 0x8169e0 compares elapsed against 0xfa0), the idle reminder fires after
// 20000 ms of inactivity (0x4e20), and an ambient line plays every 5 minutes
// (0x493e0). The index tables below are the .rdata arrays at real VA
// 0x436560/68/78/80 (see senet_exe_rules.md), indexing labelForIndex().
const uint32 kSubtitleHoldMs   = 4000;
const uint32 kIdleReminderMs   = 20000;
const uint32 kAmbientPeriodMs  = 300000;
const int kVoiceGoodMove[2]    = { 2, 7 };          // 0x436560[rnd&1]
const int kVoicePlayerBearOff[4] = { 0, 2, 3, 7 };  // 0x436568[rnd&3]
const int kVoiceOppBearOff[2]  = { 4, 5 };          // 0x436578[rnd&1]
const int kVoiceAmbient[4]     = { 9, 10, 11, 12 }; // 0x436580[0..3]

} // End of anonymous namespace

// EXE master label array 0x436590[0..12]. Indices 0..8 are the mpa4504..mpa4512
// commentary lines; 9..12 are the mca/mcb/mcc/mcd ambient lines.
const char *Egypt_Senet::labelForIndex(int idx) {
	static const char *const kLabels[13] = {
		"mpa4504", "mpa4505", "mpa4506", "mpa4507", "mpa4508",
		"mpa4509", "mpa4510", "mpa4511", "mpa4512",
		"mca4591", "mcb4592", "mcc4593", "mcd4594"
	};
	if (idx < 0 || idx >= 13)
		return nullptr;
	return kLabels[idx];
}

bool Egypt_Senet::load() {
	if (_loaded)
		return true;

	// SENET.TGA is a full-screen 640x480 board background (CPx5-wrapped).
	if (!loadTgaImage(_engine->getFilePath(kFileTypeSpriteImage, "SENET.TGA"),
	                  _background, true)) {
		warning("Egypt: Senet - failed to load SENET.TGA");
		return false;
	}

	// SENET.SPR: 11 sprites (0..7 sticks, 8 player pawn, 9 opponent pawn,
	// 10 reticle) in the shared CPx5 SPR-sheet format.
	if (!_engine->_spriteLoader.loadSprSheet(
	        _engine->getFilePath(kFileTypeSpriteImage, "SENET.SPR"), _sprites)) {
		warning("Egypt: Senet - failed to load SENET.SPR");
		return false;
	}

	debugC(kDebugFile, "Egypt: Senet resources loaded (%u sprites)", _sprites.size());
	_loaded = true;
	return true;
}

// EXE 0x416030: cells 0..13 alternate opponent/player, 14..29 empty.
void Egypt_Senet::initBoard() {
	for (int i = 0; i < kCellCount; ++i)
		_board[i] = kCellEmpty;
	for (int i = 0; i < 14; ++i)
		_board[i] = (i & 1) ? kCellPlayer : kCellOpponent; // {2,1,2,1,...}
}

// EXE throwSticks 0x816720: four sticks, each side=rnd&1, variant=rnd&3,
// sprite=side+variant*2; score = number of "1" sides, 0 -> 5. Also records
// each stick's random screen position so drawBoard can show the throw.
int Egypt_Senet::throwSticks() {
	int score = 0;
	for (int i = 0; i < 4; ++i) {
		const int side = _rnd.getRandomBit();          // rnd & 1
		const int variant = _rnd.getRandomNumber(3);   // rnd & 3
		_sticks[i].sprite = side + variant * 2;        // 0..7

		int w = 0, h = 0;
		if (_sticks[i].sprite < _sprites.size()) {
			w = _sprites[_sticks[i].sprite].surface.w;
			h = _sprites[_sticks[i].sprite].surface.h;
		}
		// EXE: x = 15 + rnd % (300 - w); y = 255 + rnd % (210 - h).
		const int rangeX = (300 - w) > 0 ? (300 - w) : 1;
		const int rangeY = (210 - h) > 0 ? (210 - h) : 1;
		_sticks[i].x = 15 + _rnd.getRandomNumber(rangeX - 1);
		_sticks[i].y = 255 + _rnd.getRandomNumber(rangeY - 1);

		if (side)
			score++;
	}
	if (score == 0)
		score = 5;
	return score;
}

// EXE 0x8163f0: count cells in [start, start+len) equal to value, clamping the
// window into [0, 30).
int Egypt_Senet::countRange(int value, int start, int len) const {
	if (start < 0)
		start = 0;
	if (start >= kCellCount)
		start = kCellCount - 1;
	if (start + len > kCellCount)
		len = kCellCount - start;
	int count = 0;
	for (int i = 0; i < len; ++i) {
		if (_board[start + i] == value)
			count++;
	}
	return count;
}

// EXE evalMove 0x816070. Returns 0 for an illegal move, otherwise a heuristic
// score 1..7 (higher = better, used by the AI). dest = from + throw. There is
// no capture in this variant: a piece may only land on an empty cell.
int Egypt_Senet::evalMove(int from, int throwVal) const {
	if (throwVal < 1 || throwVal > 5)
		return 0;
	const int piece = _board[from];
	if (piece == kCellEmpty)
		return 0;
	const int enemy = 3 - piece;
	const int dest = from + throwVal;

	// Normal cells must be empty to land on.
	if (from <= 25 && dest < 30 && _board[dest] != kCellEmpty)
		return 0;

	if (dest < 25) {
		if (from != kCellRebirth)
			return 3;
		// Leaving the House of Rebirth (14): weigh the water cell and home row.
		if (_board[kCellWater] == enemy)
			return 5;
		if (_board[kCellWater] == piece)
			return 1;
		const int mine = countRange(piece, 25, 5);
		const int en = countRange(enemy, 25, 5);
		if (mine >= 1 && en == 0)
			return 1;
		if (mine == 0 && en >= 1)
			return 5;
		return 3;
	}

	// dest >= 25.
	if (dest == 25)
		return 6; // House of Beauty
	if (from == 25) {
		if (dest == 30)
			return 7; // bear off
		if (dest == kCellWater)
			return 1;
		// dest in 27..29
		const int mine = countRange(piece, 20, 5);
		const int en = countRange(enemy, 20, 5);
		if (mine >= en)
			return 4;
		if (isReplayThrow(throwVal))
			return 4;
		if (mine + en == 5)
			return (_rnd.getRandomNumber(3) != 0) ? 2 : 4;
		return 2;
	}
	if (from >= 27 && from <= 29) {
		if (dest == 30)
			return 7; // bear off
		return (_board[kCellWater] == kCellEmpty) ? 1 : 0;
	}
	// from in 0..24 landing on 26..29, or from == 26: illegal.
	return 0;
}

// EXE applyMove 0x816210. Moves the piece and resolves the House of Water and
// bear-off. Voice comments in the original are deferred (Phase E).
void Egypt_Senet::applyMove(int from, int throwVal) {
	const int piece = _board[from];
	if (throwVal < 1 || throwVal > 5 || piece == kCellEmpty)
		return;
	int dest = from + throwVal;

	// Occupancy guard (same as evalMove); a blocked normal move is a no-op.
	if (from <= 25 && dest < 30 && _board[dest] != kCellEmpty)
		return;
	// A piece in the final houses that overshoots falls back to the water cell,
	// unless the water cell is occupied (then it cannot move).
	if (from > 25 && dest != 30) {
		if (_board[kCellWater] != kCellEmpty)
			return;
		dest = kCellWater;
	}

	_board[from] = kCellEmpty;

	if (dest >= 30) {
		// Borne off: the piece leaves the board. A bear-off comment plays only
		// while pieces remain (EXE 0x8162e3 skips the line on the last piece).
		if (countPieces(piece) > 0) {
			if (piece == kCellPlayer) {
				playLine(kVoicePlayerBearOff[_rnd.getRandomNumber(3)]);
			} else if (countPieces(kCellOpponent) < countPieces(kCellPlayer)) {
				playLine(kVoiceOppBearOff[_rnd.getRandomNumber(1)]);
			} else {
				playLine(5);
			}
		}
	} else {
		_board[dest] = (byte)piece;
		if (dest == kCellWater) {
			// House of Water: show the landing, then rebirth to cell 14 if it
			// is free; otherwise the piece stays trapped on the water.
			render();
			pumpedDelay(1000);
			if (_board[kCellRebirth] == kCellEmpty) {
				// EXE 0x8162a1: player rebirth = line 4, opponent = line 7.
				playLine(piece == kCellPlayer ? 4 : 7);
				_board[kCellWater] = kCellEmpty;
				_board[kCellRebirth] = (byte)piece;
			}
			_specialMove = true; // suppresses this turn's "good move" comment
		}
	}

	// Leaving cell 14 pulls a water-trapped piece back onto the House of
	// Rebirth (EXE 0x81633a).
	if (from == kCellRebirth && _board[kCellWater] != kCellEmpty &&
	        _board[kCellRebirth] == kCellEmpty) {
		_board[kCellRebirth] = _board[kCellWater];
		if (_board[kCellRebirth] == kCellPlayer)
			playLine(4); // rescued player piece
		_board[kCellWater] = kCellEmpty;
		_specialMove = true;
	}
}

// EXE chooseBestMove 0x816370: best legal move for the given side, ties won by
// the highest cell index. Returns the source cell, or -1 if no move is legal.
int Egypt_Senet::chooseBestMove(int value, int throwVal) const {
	int best = -1;
	int bestIndex = -1;
	for (int i = 0; i < kCellCount; ++i) {
		if (_board[i] != value)
			continue;
		const int score = evalMove(i, throwVal);
		if (score > 0 && score >= best) {
			best = score;
			bestIndex = i;
		}
	}
	return bestIndex;
}

// EXE hitTestCell 0x816830: first cell whose 35x50 rect contains the cursor.
int Egypt_Senet::hitTestCell(const Common::Point &mouse) const {
	for (int i = 0; i < kCellCount; ++i) {
		const Common::Rect r(kCellPos[i].x, kCellPos[i].y,
		                     kCellPos[i].x + kCellHitW, kCellPos[i].y + kCellHitH);
		if (r.contains(mouse))
			return i;
	}
	return -1;
}

// EXE hitTestThrowZone 0x816880: any thrown stick's own rect, else the fixed
// throw rectangle.
bool Egypt_Senet::hitTestThrowZone(const Common::Point &mouse) const {
	for (int i = 0; i < 4; ++i) {
		int w = 0, h = 0;
		if (_sticks[i].sprite < _sprites.size()) {
			w = _sprites[_sticks[i].sprite].surface.w;
			h = _sprites[_sticks[i].sprite].surface.h;
		}
		const Common::Rect r(_sticks[i].x, _sticks[i].y,
		                     _sticks[i].x + w, _sticks[i].y + h);
		if (r.contains(mouse))
			return true;
	}
	return kThrowZone.contains(mouse);
}

// Blit a masked RGB565 sprite centered on (centerX, centerY), clipped to the
// active surface.
void Egypt_Senet::blitSprite(uint spriteId, int centerX, int centerY) const {
	if (!_surface || spriteId >= _sprites.size())
		return;

	Graphics::ManagedSurface &dst = *_surface;
	const EgyptInterfaceSprite &sprite = _sprites[spriteId];
	const int w = sprite.surface.w;
	const int h = sprite.surface.h;
	const int originX = centerX - w / 2;
	const int originY = centerY - h / 2;
	const Graphics::PixelFormat &dstFmt = dst.format;

	for (int row = 0; row < h; ++row) {
		const int dstY = originY + row;
		if (dstY < 0 || dstY >= dst.h)
			continue;

		const byte *srcRow  = (const byte *)sprite.surface.getBasePtr(0, row);
		const byte *maskRow = sprite.mask.data() + row * w;

		for (int col = 0; col < w; ++col) {
			if (maskRow[col] == kCursorMaskTransparent)
				continue;
			const int dstX = originX + col;
			if (dstX < 0 || dstX >= dst.w)
				continue;

			const uint16 srcPixel = READ_LE_UINT16(srcRow + col * 2);
			uint8 r, g, b;
			kEgyptSpriteFormat.colorToRGB(srcPixel, r, g, b);
			WRITE_LE_UINT32((byte *)dst.getBasePtr(dstX, dstY), dstFmt.RGBToColor(r, g, b));
		}
	}
}

// Sticks are drawn from their top-left corner (no centering in the EXE).
void Egypt_Senet::blitSpriteTopLeft(uint spriteId, int x, int y) const {
	if (spriteId >= _sprites.size())
		return;
	const EgyptInterfaceSprite &sprite = _sprites[spriteId];
	blitSprite(spriteId, x + sprite.surface.w / 2, y + sprite.surface.h / 2);
}

// EXE drawBoard (0x816530 opponent / 0x8165f5 player): one pawn sprite per
// borne-off piece, laid out left to right in steps of 40 px, wrapping to a
// second row after four. Each pawn's top-left is (baseX - w/2, baseY - h).
// startX/rowY1/rowY2 come straight from the EXE constants for each side.
void Egypt_Senet::drawBorneOffCounter(uint spriteId, int count,
                                      int startX, int rowY1, int rowY2) const {
	if (spriteId >= _sprites.size())
		return;
	const int w = _sprites[spriteId].surface.w;
	const int h = _sprites[spriteId].surface.h;
	const int wrapX = startX + 160; // EXE 0x212 (opp) / 0x1fe (player) = startX+160
	int x = startX;
	for (int i = 0; i < count; ++i) {
		int col = x;
		int rowY = rowY1;
		if (x >= wrapX) {
			col = x - 140; // EXE -0x8c: second row shifts left
			rowY = rowY2;
		}
		blitSpriteTopLeft(spriteId, col - w / 2, rowY - h);
		x += 40; // EXE 0x28
	}
}

// EXE drawBoard 0x416460: background, pawns, the four sticks and the borne-off
// counters. The current throw value is shown as text for readability (the EXE
// leaves the player to read it off the sticks).
void Egypt_Senet::drawBoard() {
	if (!_surface)
		return;
	_surface->blitFrom(_background);

	for (int i = 0; i < kCellCount; ++i) {
		if (_board[i] == kCellPlayer)
			blitSprite(kSpritePlayerPawn, kCellPos[i].x + kPawnAnchorX,
			           kCellPos[i].y + kPawnAnchorY - 8);
		else if (_board[i] == kCellOpponent)
			blitSprite(kSpriteOpponentPawn, kCellPos[i].x + kPawnAnchorX,
			           kCellPos[i].y + kPawnAnchorY - 6);
	}

	for (int i = 0; i < 4; ++i)
		blitSpriteTopLeft(_sticks[i].sprite, _sticks[i].x, _sticks[i].y);

	// Borne-off counters, one pawn sprite per piece already home (EXE layout).
	// The EXE counts value+blink for each side (2/4 opponent, 1/3 player); the
	// port's blink clears the cell instead, so plain piece counts match every
	// non-blink frame.
	const int playerOff = 7 - countPieces(kCellPlayer);
	const int oppOff = 7 - countPieces(kCellOpponent);
	drawBorneOffCounter(kSpriteOpponentPawn, oppOff, 370, 300, 328);
	drawBorneOffCounter(kSpritePlayerPawn, playerOff, 350, 384, 412);

	// Current throw value as text (a port-only readability aid; the EXE leaves
	// the player to read it off the sticks).
	if (_throwValue > 0) {
		Egypt_FontManager &fm = _engine->_fontManager;
		fm.setCurrentFont(Egypt_FontManager::kSlotMenu);
		fm.setForeColor(_surface->format.RGBToColor(255, 255, 255));
		fm.displayStr(*_surface, 10, 455,
		              Common::String::format("%d", _throwValue));
	}

	// Active voice line's subtitle, centered along the bottom (EXE 0x8169e0
	// holds it for 4000 ms). Some lines have no on-screen text, only voice.
	if (subtitleActive() && !_subtitleText.empty()) {
		Egypt_FontManager &fm = _engine->_fontManager;
		fm.setCurrentFont(Egypt_FontManager::kSlotMenu);
		fm.setForeColor(_surface->format.RGBToColor(255, 240, 180));
		const int tw = (int)fm.getStrWidth(_subtitleText);
		fm.displayStr(*_surface, (_surface->w - tw) / 2, 430, _subtitleText);
	}
}

void Egypt_Senet::render() {
	drawBoard();
	if (!_surface)
		return;
	g_system->copyRectToScreen(_surface->getPixels(), _surface->pitch, 0, 0,
	                           _surface->w, _surface->h);
	g_system->updateScreen();
}

// EXE blinkPiece 0x816910: the AI's chosen piece flashes off/on three times to
// telegraph the move.
void Egypt_Senet::blinkPiece(int cell) {
	if (cell < 0 || cell >= kCellCount)
		return;
	const byte piece = _board[cell];
	for (int i = 0; i < 3; ++i) {
		_board[cell] = kCellEmpty;
		render();
		pumpedDelay(100);
		_board[cell] = piece;
		render();
		pumpedDelay(100);
	}
}

// EXE 0x8167e0: a busy wait, reimplemented as an event-pumping delay so quit
// and screen updates keep working.
void Egypt_Senet::pumpedDelay(uint32 ms) {
	const uint32 end = g_system->getMillis() + ms;
	while (g_system->getMillis() < end && !_engine->shouldAbort()) {
		_engine->pollEvents();
		g_system->updateScreen();
		g_system->delayMillis(10);
	}
}

// EXE 0x8169e0: a line stays visible for 4000 ms after it started.
bool Egypt_Senet::subtitleActive() const {
	return _subtitleStartMs != 0 &&
	       (g_system->getMillis() - _subtitleStartMs) < kSubtitleHoldMs;
}

// EXE playVoice 0x816990: play sound/FR/<label>.apc (kSpeechSoundType) and show
// the label's subtitle text. The EXE refuses to start a new line while one is
// still on screen (guard on 0x4d1c50), so overlapping calls are ignored.
void Egypt_Senet::playLine(int idx) {
	if (subtitleActive())
		return;
	const char *label = labelForIndex(idx);
	if (!label)
		return;

	// Voice (same APC path as Egypt_Dialog::playVoice).
	if (_engine->_mixer->isSoundHandleActive(_voiceHandle))
		_engine->_mixer->stopHandle(_voiceHandle);
	Common::File file;
	if (file.open(_engine->getFilePath(kFileTypeVoice, label))) {
		if (Audio::PacketizedAudioStream *stream = Audio::makeAPCStream(file)) {
			int32 remaining = (int32)(file.size() - file.pos());
			if (remaining > 0) {
				byte *buf = new byte[(uint32)remaining];
				file.read(buf, (uint32)remaining);
				stream->queuePacket(new Common::MemoryReadStream(
				    buf, (uint32)remaining, DisposeAfterUse::YES));
			}
			stream->finish();
			_engine->_mixer->playStream(Audio::Mixer::kSpeechSoundType,
			                            &_voiceHandle, stream);
		}
	}

	// Subtitle (looked up from Level.txt; empty text still holds the timer so the
	// voice is not immediately overwritten by another line).
	_subtitleText.clear();
	_engine->_dialog.getLineText(label, _subtitleText);
	_subtitleStartMs = g_system->getMillis();
	if (_subtitleStartMs == 0)
		_subtitleStartMs = 1; // 0 means "no line"; keep it non-zero
}

// EXE frame fn 0x8159a0: the ambient line (every 5 min, cycling four labels)
// and the idle reminder (20 s of inactivity while waiting to throw, gated by a
// 1-in-3 die roll). Only fires when no line is already showing.
void Egypt_Senet::updateAmbientAndIdle() {
	if (subtitleActive())
		return;
	const uint32 now = g_system->getMillis();

	// Ambient atmosphere line, cycling mca4591..mcd4594.
	if (now - _ambientBaseMs >= kAmbientPeriodMs) {
		playLine(kVoiceAmbient[_ambientIdx & 3]);
		_ambientIdx = (_ambientIdx + 1) & 3;
		_ambientBaseMs = now;
		return;
	}

	// Idle reminder, only while the player is waiting to throw (EXE checks
	// movePending == 0). Index depends on whether a replay streak is running.
	if (_playerTurn && !_movePending &&
	        now - _lastActivityMs >= kIdleReminderMs) {
		if (_rnd.getRandomNumber(2) == 0)  // 1-in-3 gate (rnd % 3 == 0)
			playLine(_replayCounter == 0 ? 6 : 8);
		_lastActivityMs = now; // reset regardless, matching the EXE
	}
}

int Egypt_Senet::run() {
	if (!load()) {
		// Resources missing: resolve as a victory so the M45PLAN flow still
		// advances rather than deadlocking.
		warning("Egypt: Senet - resources unavailable, resolving as victory");
		return 1;
	}

	initBoard();
	_throwValue = throwSticks(); // initial visual throw (EXE initBoard 0x416030)
	_playerTurn = true;
	_movePending = false;
	_replayCounter = 0;
	_specialMove = false;
	_subtitleText.clear();
	_subtitleStartMs = 0;
	_lastActivityMs = g_system->getMillis();
	_ambientBaseMs = _lastActivityMs;
	_ambientIdx = 0;

	Graphics::ManagedSurface surface(kScreenWidth, kScreenHeight,
	                                 g_system->getScreenFormat());
	_surface = &surface;

	_engine->showMouse(true);
	_engine->clearKeys();
	_engine->waitMouseRelease();

	int result = 1; // default: ESC = skip = victory
	bool done = false;

	while (!done && !_engine->shouldAbort()) {
		_engine->pollEvents();

		if (_engine->getNextKey().keycode == Common::KEYCODE_ESCAPE) {
			result = 1; // ESC skips the game as a win (EXE 0x8159dd, script)
			break;
		}

		const Common::Point mouse = _engine->getMousePos();
		const bool clicked = (_engine->getCurrentMouseButton() == 1);
		if (clicked) {
			_engine->waitMouseRelease();
			_lastActivityMs = g_system->getMillis(); // reset idle reminder
		}

		if (_playerTurn) {
			if (!_movePending) {
				// Waiting for the player to throw the sticks.
				if (clicked && hitTestThrowZone(mouse)) {
					_throwValue = throwSticks();
					if (_replayCounter > 0 && _throwValue == 1)
						_replayCounter--;
					_movePending = true;
					if (chooseBestMove(kCellPlayer, _throwValue) == -1) {
						if (isReplayThrow(_throwValue)) {
							// No move but a replay throw: throw again.
							_movePending = false;
						}
						// else: keep _movePending so the next frame passes turn.
					}
				}
			} else {
				// A throw is on the table; pick a piece to move.
				if (chooseBestMove(kCellPlayer, _throwValue) == -1) {
					// No legal move: pass to the opponent (pre-throw for it).
					_playerTurn = false;
					_movePending = false;
					_replayCounter = 0;
					_throwValue = throwSticks();
				} else if (clicked) {
					const int cell = hitTestCell(mouse);
					if (cell >= 0 && _board[cell] == kCellPlayer &&
					        evalMove(cell, _throwValue) > 0) {
						_specialMove = false;
						applyMove(cell, _throwValue);
						if (countPieces(kCellPlayer) == 0) {
							result = 1; // player bore off all -> victory
							render();
							pumpedDelay(2000);
							done = true;
						} else if (isReplayThrow(_throwValue)) {
							_movePending = false; // throw again, same turn
							_replayCounter++;
							// A three-throw streak with no special move earns a
							// "good move" comment (EXE 0x815c33, table 0x436560).
							if (_replayCounter >= 3 && !_specialMove) {
								playLine(kVoiceGoodMove[_rnd.getRandomNumber(1)]);
								_replayCounter = 0;
							}
						} else {
							_playerTurn = false;
							_movePending = false;
							_replayCounter = 0;
							_throwValue = throwSticks();
						}
					}
				}
			}
		} else {
			// Opponent (AI) turn, fully automatic.
			pumpedDelay(1000);
			const int cell = chooseBestMove(kCellOpponent, _throwValue);
			if (cell == -1) {
				// No move: pass back to the player.
				_playerTurn = true;
				_movePending = false;
				_replayCounter = 0;
			} else {
				blinkPiece(cell);
				_specialMove = false;
				applyMove(cell, _throwValue);
				if (countPieces(kCellOpponent) == 0) {
					result = 0; // opponent bore off all -> defeat
					render();
					pumpedDelay(2000);
					done = true;
				} else if (isReplayThrow(_throwValue)) {
					// Opponent's own three-throw streak comment (EXE 0x815dbe).
					_replayCounter++;
					if (_replayCounter >= 3 && !_specialMove) {
						playLine(6);
						_replayCounter = 0;
					}
					_throwValue = throwSticks(); // go again, same turn
					pumpedDelay(2000);
				} else {
					_playerTurn = true;
					_movePending = false;
					_replayCounter = 0;
				}
			}
		}

		if (!done) {
			updateAmbientAndIdle(); // 5-min ambient + 20-s idle reminder
			render();
			g_system->delayMillis(10);
		}
	}

	if (_engine->_mixer->isSoundHandleActive(_voiceHandle))
		_engine->_mixer->stopHandle(_voiceHandle);
	_surface = nullptr;
	_engine->clearKeys();
	_engine->waitMouseRelease();
	debugC(kDebugVariable, "Egypt: Senet mini-game exited -> result=%d", result);
	return result;
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
