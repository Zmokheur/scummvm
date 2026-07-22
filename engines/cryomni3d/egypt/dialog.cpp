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
#include "common/events.h"
#include "common/file.h"
#include "common/memstream.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"

#include "graphics/managed_surface.h"
#include "graphics/surface.h"

#include "image/tga.h"

#include "cryomni3d/egypt/engine.h"
#include "cryomni3d/egypt/support/image_loader.h"

namespace CryOmni3D {
namespace Egypt {

// --- Level.txt loading ---

bool Egypt_Dialog::loadLevelTxt() {
	Common::File file;
	// EXE builds: ref\FR\Level.txt (0x413080)
	if (!file.open(_engine->getFilePath(kFileTypeLevelTxt))) {
		warning("Egypt: failed to open ref/FR/Level.txt");
		return false;
	}

	_nodes.clear();

	EgyptDialogNode current;
	bool inBlock = false;
	bool textSeen = false;

	while (!file.eos()) {
		Common::String line = file.readLine();
		// Strip CR/LF
		while (!line.empty() && (line.lastChar() == '\r' || line.lastChar() == '\n'))
			line.deleteLastChar();

		Common::String trimmed = line;
		trimmed.trim();

		if (trimmed.empty())
			continue;

		// A label line: single word ending with ':' and no embedded spaces.
		// The EXE checks for ':' as delimiter (0x414650).
		if (trimmed.lastChar() == ':' &&
		    trimmed.find(' ') == Common::String::npos &&
		    trimmed.find('\t') == Common::String::npos) {
			if (inBlock && !current.label.empty())
				_nodes[current.label] = current;

			current = EgyptDialogNode();
			current.label = trimmed.substr(0, trimmed.size() - 1);
			current.label.toLowercase();
			inBlock = true;
			textSeen = false;
			continue;
		}

		if (!inBlock)
			continue;

		if (!textSeen) {
			// First content line must start with '<' (EXE: error if not).
			if (trimmed[0] == '<') {
				int closePos = trimmed.find('>');
				if (closePos > 0)
					current.text = trimmed.substr(1, closePos - 1);
				else
					current.text = trimmed.substr(1);
				current.text.trim();
				textSeen = true;
			} else {
				warning("Egypt: Level.txt label '%s': expected '<>' text line, got: %s",
				        current.label.c_str(), trimmed.c_str());
				textSeen = true; // recover - treat as command
				current.commands.push_back(trimmed);
			}
			continue;
		}

		current.commands.push_back(trimmed);
	}

	if (inBlock && !current.label.empty())
		_nodes[current.label] = current;

	debugC(kDebugFile, "Egypt: Level.txt loaded - %u nodes", _nodes.size());
	return !_nodes.empty();
}

// --- Node lookup ---

const EgyptDialogNode *Egypt_Dialog::findNode(const Common::String &label) const {
	Common::String key = label;
	key.toLowercase();
	Common::HashMap<Common::String, EgyptDialogNode,
	    Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo>::const_iterator it =
	    _nodes.find(key);
	if (it != _nodes.end())
		return &it->_value;
	return nullptr;
}

// --- Ramose detection ---

// Labels whose characters at positions [1..2] are "ra" (case-insensitive) are
// Ramose player-reply options (EXE 0x413140).  Examples: sra0001, dra0101, ara0503.
static bool isRamoseChoiceLabel(const Common::String &label) {
	return label.size() >= 3 &&
	       (label[1] == 'r' || label[1] == 'R') &&
	       (label[2] == 'a' || label[2] == 'A');
}

// --- Speaker FLC mapping ---

// EXE 0x413930 (adjusted 0x813930): switch on family (label[0]), then compare
// the 2-char code label[1..2] against a per-family list, returning the matched
// speaker's FLC/portrait base name. This is a faithful transcription of that
// dispatch (jump table 0x414564, per-family handlers, string pool 0x436078..).
//
// Key points confirmed from the disassembly:
//   - Only families A, D, K, M, N, S have a handler; every other first letter
//     (B, C, E ... R ...) falls through to the default at 0x414557, which
//     returns no speaker name -> no portrait. In particular the R family (the
//     level-intro narration labels raa*/rra*) shows NO portrait.
//   - "RA" -> RAMOSE only exists *inside* the six real families, so it must
//     NOT be applied globally (an earlier port shortcut wrongly gave rra*
//     nodes a RAMOSE portrait the EXE never shows).
//   - The same 2-char code maps to different speakers per family (PT ->
//     PENTAOUR/PTAHEMEB/PTANEFER; VI -> VIEUX/TOH), so the family gate is
//     required.
//   - Family M's IM branch resolves to "NULL" and its CA/CB/CC/CD branches hit
//     the EXE's "unknown speaker" error path; both mean no portrait, so they
//     are simply absent here.
static const char *resolveSpeakerFlc(const Common::String &label) {
	if (label.size() < 3)
		return nullptr;

	const char family = (char)toupper((unsigned char)label[0]);
	const char c1     = (char)toupper((unsigned char)label[1]);
	const char c2     = (char)toupper((unsigned char)label[2]);

	struct Entry { char family; char c1; char c2; const char *name; };
	static const Entry kTable[] = {
		// Family A (0x813dcf)
		{ 'A', 'E', 'A', "EMBAUMEU" },
		{ 'A', 'E', 'M', "EMBAUMEU" },
		{ 'A', 'R', 'A', "RAMOSE"   },
		// Family D (0x813aeb)
		{ 'D', 'C', 'A', "CABARETI" },
		{ 'D', 'I', 'M', "IMENAKHT" },
		{ 'D', 'M', 'T', "MONTOUME" },
		{ 'D', 'O', 'U', "OUVRIERE" },
		{ 'D', 'P', 'E', "PENMENEF" },
		{ 'D', 'P', 'T', "PENTAOUR" },
		{ 'D', 'V', 'I', "VIEUX"    },
		{ 'D', 'E', 'N', "ENFANT"   },
		{ 'D', 'O', 'C', "COLERE"   },
		{ 'D', 'R', 'A', "RAMOSE"   },
		// Family K (0x814321)
		{ 'K', 'C', 'O', "AMEROUTH" },
		{ 'K', 'D', 'O', "DOYEN"    },
		{ 'K', 'H', 'O', "HOROLOG"  },
		{ 'K', 'P', 'R', "PRETRE"   },
		{ 'K', 'P', 'T', "PTANEFER" },
		{ 'K', 'V', 'I', "TOH"      },
		{ 'K', 'R', 'A', "RAMOSE"   },
		// Family M (0x813f53)
		{ 'M', 'F', 'E', "FEMME"    },
		{ 'M', 'F', 'N', "FEMME"    },
		{ 'M', 'I', 'N', "ESCLAV"   },
		{ 'M', 'N', 'O', "NOBLE"    },
		{ 'M', 'P', 'A', "PANAHESY" },
		{ 'M', 'P', 'O', "PORTIER"  },
		{ 'M', 'P', 'T', "PTAHEMEB" },
		{ 'M', 'R', 'A', "RAMOSE"   },
		// Family N (0x813e91)
		{ 'N', 'D', 'E', "DESSIN"   },
		{ 'N', 'P', 'L', "PLEUREUS" },
		{ 'N', 'R', 'A', "RAMOSE"   },
		// Family S (0x8139db)
		{ 'S', 'I', 'M', "IMENAKHT" },
		{ 'S', 'I', 'N', "INHERKHA" },
		{ 'S', 'M', 'T', "MONTOUME" },
		{ 'S', 'R', 'A', "RAMOSE"   },
	};

	for (uint i = 0; i < ARRAYSIZE(kTable); ++i) {
		if (kTable[i].family == family && kTable[i].c1 == c1 && kTable[i].c2 == c2)
			return kTable[i].name;
	}
	return nullptr;
}

// --- SYC phoneme -> mouth frame (EXE 0x4356a8, used at step 3) ---

static const uint kSycToMouthFrame[27] = {
	1, 2, 2, 8, 2, 2, 11, 8, 3,
	9, 2, 8, 1, 8, 7,  2, 7, 4,
	6, 7, 7, 8, 6, 10, 5, 0, 2
};

// Idle frames for the mouth between phonemes (EXE 0x435680)
static const uint kIdleMouthFrames[10] = {
	0, 1, 2, 3, 3, 3, 2, 1, 0, 0
};

// --- Node execution ---

// Runs the command list of one Level.txt block.
// Sets outText to the block's <text>, then processes commands in order.
// Returns the terminal action; caller shows outText (if non-empty) then acts.
EgyptDialogResult Egypt_Dialog::executeNode(
        const Common::String &label,
        Common::String &outText,
        Common::Array<EgyptDialogChoice> &outChoices,
        Common::String &outNextLabel) {

	outText.clear();
	outChoices.clear();
	outNextLabel.clear();

	const EgyptDialogNode *node = findNode(label);
	if (!node) {
		warning("Egypt: dialogue label not found: %s", label.c_str());
		return kDlgEnd;
	}

	const char *speaker = resolveSpeakerFlc(node->label);
	debugC(kDebugVariable, "Egypt: dialogue node '%s' speaker=%s text='%s'",
	        node->label.c_str(), speaker ? speaker : "?", node->text.c_str());

	outText = node->text;

	for (uint i = 0; i < node->commands.size(); ++i) {
		const Common::String &cmd = node->commands[i];

		if (cmd.hasPrefixIgnoreCase("if ")) {
			// Syntax: if var<op>value goto label
			Common::String expr = cmd.substr(3);
			// Search for " goto " case-insensitively by lowercasing a copy.
			Common::String exprLower = expr;
			exprLower.toLowercase();
			int gotoPos = exprLower.find(" goto ");
			if (gotoPos < 0) {
				warning("Egypt: dialogue 'if' without goto: %s", cmd.c_str());
				continue;
			}
			Common::String condition = expr.substr(0, gotoPos);
			Common::String target = expr.substr(gotoPos + 6);
			target.trim();
			if (_engine->evaluateScriptCondition(condition)) {
				// Apply the same split/classify logic as the goto handler.
				Common::Array<Common::String> targets;
				Common::StringTokenizer ifTok(target, ",");
				while (!ifTok.empty()) {
					Common::String t = ifTok.nextToken();
					t.trim();
					if (!t.empty())
						targets.push_back(t);
				}
				if (targets.size() == 1 && !isRamoseChoiceLabel(targets[0])) {
					outNextLabel = targets[0];
					outNextLabel.toLowercase();
					return kDlgJump;
				}
				for (uint j = 0; j < targets.size(); ++j) {
					EgyptDialogChoice choice;
					choice.targetLabel = targets[j];
					choice.targetLabel.toLowercase();
					const EgyptDialogNode *choiceNode = findNode(targets[j]);
					choice.displayText = choiceNode ? choiceNode->text : targets[j];
					outChoices.push_back(choice);
				}
				return kDlgChoices;
			}
			continue;
		}

		if (cmd.hasPrefixIgnoreCase("let ")) {
			_engine->setScriptVariable(cmd.substr(4));
			continue;
		}

		if (cmd.hasPrefixIgnoreCase("show ")) {
			Common::String arg = cmd.substr(5);
			arg.trim();
			warning("Egypt: dialogue 'show %s' (not yet implemented)", arg.c_str());
			return kDlgShow;
		}

		if (cmd.hasPrefixIgnoreCase("goto ")) {
			Common::String targetList = cmd.substr(5);
			targetList.trim();

			// Split by comma to find whether this is a multi-target (Ramose choices) goto.
			Common::Array<Common::String> targets;
			Common::StringTokenizer tok(targetList, ",");
			while (!tok.empty()) {
				Common::String t = tok.nextToken();
				t.trim();
				if (!t.empty())
					targets.push_back(t);
			}

			if (targets.size() == 1 && !isRamoseChoiceLabel(targets[0])) {
				outNextLabel = targets[0];
				outNextLabel.toLowercase();
				return kDlgJump;
			}

			// Ramose choices: fetch display text for each target label.
			for (uint j = 0; j < targets.size(); ++j) {
				EgyptDialogChoice choice;
				choice.targetLabel = targets[j];
				choice.targetLabel.toLowercase();
				const EgyptDialogNode *choiceNode = findNode(targets[j]);
				choice.displayText = choiceNode ? choiceNode->text : targets[j];
				outChoices.push_back(choice);
			}
			return kDlgChoices;
		}

		if (cmd.equalsIgnoreCase("end")) {
			return kDlgEnd;
		}

		warning("Egypt: dialogue unknown command: %s", cmd.c_str());
	}

	// EXE: "DIALOGUE: No match at end of dialogue" - treat as end.
	warning("Egypt: dialogue node '%s' has no end/goto", node->label.c_str());
	return kDlgEnd;
}

// --- Portrait loading ---

// Loads an SPA or SPB file.
// Format: table of N entries of 8 bytes [uint32 offset][uint16 h][uint16 w],
// where each offset points to a TXEN/RLE block in the same buffer.
// The raw (decompressed) data is kept intact for the TXEN blit.
bool Egypt_Dialog::loadSprite(const Common::Path &path, EgyptDialogSprite &out) {
	out.clear();

	if (!loadFileMaybeCpx5(path, out.data))
		return false;

	if (out.data.size() < 8)
		return false;

	// The first offset (table[0].offset) = table size = frameCount * 8.
	const uint32 firstOffset = READ_LE_UINT32(out.data.data());
	if (firstOffset == 0 || (firstOffset % 8) != 0 || firstOffset > out.data.size())
		return false;

	out.frameCount = firstOffset / 8;
	return true;
}

// Loads SPA + SPB for speakerName at the given level (cache: only reloads when the name changes).
void Egypt_Dialog::loadSpeaker(const Common::String &speakerName, int level) {
	if (speakerName == _speakerName)
		return;

	_tga.free();
	_spa.clear();
	_spb.clear();
	_speakerName = speakerName;

	if (speakerName.empty() || level < 1 || level > 6)
		return;

	// Confirmed actual path: SPRITE/LEVELx/<scene>/<PERSONNAGE>.SPA/SPB/TGA
	const Common::String &sceneName = _engine->_currentScene.name;
	if (sceneName.empty())
		return;

	Common::Path tgaPath(Common::String::format("SPRITE/LEVEL%d/%s/%s.TGA",
	                                             level, sceneName.c_str(), speakerName.c_str()));
	Common::Path spaPath(Common::String::format("SPRITE/LEVEL%d/%s/%s.SPA",
	                                             level, sceneName.c_str(), speakerName.c_str()));
	Common::Path spbPath(Common::String::format("SPRITE/LEVEL%d/%s/%s.SPB",
	                                             level, sceneName.c_str(), speakerName.c_str()));

	// TGA: static base image of the character.
	// The .TGA files are CPx5-compressed on disk - decompress first,
	// then decode the standard TGA from a MemoryReadStream.
	{
		Common::Array<byte> tgaData;
		if (loadFileMaybeCpx5(tgaPath, tgaData)) {
			Common::MemoryReadStream tgaStream(tgaData.data(), tgaData.size());
			Image::TGADecoder tgaDecoder;
			if (tgaDecoder.loadStream(tgaStream))
				_tga.copyFrom(*tgaDecoder.getSurface());
			else
				warning("Egypt: failed to decode TGA for %s (level %d)", speakerName.c_str(), level);
			tgaDecoder.destroy();
		} else {
			warning("Egypt: no TGA portrait for %s (level %d)", speakerName.c_str(), level);
		}
	}

	if (!loadSprite(spaPath, _spa))
		warning("Egypt: no SPA portrait for %s (level %d)", speakerName.c_str(), level);
	if (!loadSprite(spbPath, _spb))
		warning("Egypt: no SPB portrait for %s (level %d)", speakerName.c_str(), level);
}

// Decodes and blits a TXEN/RLE patch from an SPA or SPB frame.
// Each frame points to a "TXEN" block containing its screen coordinates and an RLE stream.
// RLE: uint16 count (0=end, 0xffff=next line), uint16 xOffset, then count x uint16 RGB565.
void Egypt_Dialog::blitSpriteFrame(Graphics::ManagedSurface &dst,
                                                   const EgyptDialogSprite &sprite,
                                                   uint frame) {
	if (sprite.empty() || frame >= sprite.frameCount)
		return;

	const uint32 offset = READ_LE_UINT32(sprite.data.data() + frame * 8);

	// Sentinel entry: offset points to EOF (e.g. SPB frame 11 for MONTOUME).
	// The EXE skips drawing in this case.
	if (offset + 12 > sprite.data.size())
		return;

	const byte *p   = sprite.data.data() + offset;
	const byte *end = sprite.data.data() + sprite.data.size();

	// TXEN header: [char[4] marker][int16 y][int16 x][uint16 h][uint16 w]
	// The coordinates are absolute positions on the 640x480 screen.
	const int baseY = (int)READ_LE_INT16(p + 4);
	const int baseX = (int)READ_LE_INT16(p + 6);
	p += 12;

	const Graphics::PixelFormat &fmt = dst.format;
	int line = 0;

	while (p + 2 <= end) {
		const uint16 count = READ_LE_UINT16(p);
		p += 2;

		if (count == 0)
			break;

		if (count == 0xffff) {
			++line;
			continue;
		}

		if (p + 2 > end)
			break;
		const uint16 xOff = READ_LE_UINT16(p);
		p += 2;

		for (uint16 i = 0; i < count; ++i) {
			if (p + 2 > end)
				break;
			const uint16 srcColor = READ_LE_UINT16(p);
			p += 2;

			const int px = baseX + (int)xOff + (int)i;
			const int py = baseY + line;
			if (px < 0 || py < 0 || px >= dst.w || py >= dst.h)
				continue;

			// Interpret as RGB565
			const uint8 r = (uint8)(((srcColor >> 11) & 0x1f) * 255 / 31);
			const uint8 g = (uint8)(((srcColor >>  5) & 0x3f) * 255 / 63);
			const uint8 b = (uint8)( (srcColor        & 0x1f) * 255 / 31);
			const uint32 color = fmt.RGBToColor(r, g, b);

			void *dstPtr = dst.getBasePtr(px, py);
			switch (fmt.bytesPerPixel) {
			case 2: WRITE_LE_UINT16(dstPtr, (uint16)color); break;
			case 4: WRITE_LE_UINT32(dstPtr, color); break;
			default: break;
			}
		}
	}
}

// --- APC voice ---

// Loads and plays sound/FR/<label>.apc via the mixer (kSpeechSoundType channel).
void Egypt_Dialog::playVoice(const Common::String &label) {
	stopVoice();

	Common::File file;
	if (!file.open(_engine->getFilePath(kFileTypeVoice, label)))
		return;

	Audio::PacketizedAudioStream *stream = Audio::makeAPCStream(file);
	if (!stream)
		return;

	// After reading the header (32 bytes), the rest of the file is ADPCM audio.
	int32 remaining = (int32)(file.size() - file.pos());
	if (remaining > 0) {
		byte *buf = new byte[(uint32)remaining];
		file.read(buf, (uint32)remaining);
		stream->queuePacket(new Common::MemoryReadStream(buf, (uint32)remaining, DisposeAfterUse::YES));
	}
	stream->finish();

	_engine->_mixer->playStream(Audio::Mixer::kSpeechSoundType, &_voiceHandle, stream);
}

void Egypt_Dialog::stopVoice() {
	if (_engine->_mixer->isSoundHandleActive(_voiceHandle))
		_engine->_mixer->stopHandle(_voiceHandle);
}

// --- SYC mouth sync ---

// Loads the SYC file for a given label.
// Fallback: syc/FR/<label>.syc -> sound/FR/<label>.syc -> sound/FR/null.syc
// Format: 0x98 bytes of header ignored, then 12-byte records.
void Egypt_Dialog::loadSyc(const Common::String &label) {
	_sycEvents.clear();
	_sycEventIdx = 0;

	Common::File file;
	if (!file.open(Common::Path(Common::String::format("syc/FR/%s.syc", label.c_str()))))
		if (!file.open(Common::Path(Common::String::format("sound/FR/%s.syc", label.c_str()))))
			if (!file.open(Common::Path("sound/FR/null.syc")))
				return;

	if (file.size() <= (int32)0x98)
		return;

	file.seek(0x98);
	while (file.pos() + 12 <= file.size()) {
		EgyptSycEvent ev;
		ev.timeMs      = file.readUint32LE();
		ev.phonemeCode = file.readUint32LE();
		file.readUint32LE(); // unknown, ignored
		_sycEvents.push_back(ev);
	}
}

// --- Text rendering helpers ---

namespace {

const int kDialogTextX      = 4;
const int kDialogTextWidth  = 630;
const int kDialogBoxTop     = 320;   // text area starts at y=320 on 640x480 screen
const int kDialogPadding    = 6;
const uint32 kColorNpcText  = 0xFFFFFFFF; // white - overridden to screen format in use
const uint32 kColorChoice   = 0xFFFFFF00; // yellow
const uint32 kColorHovered  = 0xFFFFCC33; // bright amber
const uint32 kColorBoxBg    = 0xFF101010; // near-black

// Build the dialogue text area rectangle.
inline Common::Rect dialogBoxRect() {
	return Common::Rect(0, kDialogBoxTop, 640, 480);
}

} // anonymous namespace

// --- Text display with typewriter ---

// Displays text in the bottom text area over the background snapshot.
// Returns when the player clicks or presses Space/Return.
void Egypt_Dialog::showText(const Graphics::ManagedSurface &background,
                                           const Common::String &text) {
	if (text.empty())
		return;

	// Original CRYOFONT (provisional slot, see kSlotDialog)
	Egypt_FontManager &fm = _engine->_fontManager;
	fm.setCurrentFont(Egypt_FontManager::kSlotDialog);

	// Word-wrap the text to fit in the dialogue column.
	Common::Array<Common::String> lines;
	fm.wordWrap(text, kDialogTextWidth, lines);

	const int lineH    = fm.getFontHeight() + 2;
	const int totalH   = (int)lines.size() * lineH + kDialogPadding * 2;
	const int boxTop   = MAX(kDialogBoxTop, kScreenHeight - totalH - 4);
	const Graphics::PixelFormat &fmt = g_system->getScreenFormat();

	// Typewriter state
	// Each 'unit' = one character; we reveal one unit every 70 ms (EXE: 7 centiseconds).
	const uint32 kMsPerChar = 70;
	uint32 startMs = g_system->getMillis();
	bool fullTextShown = false;
	bool waitingForAdvance = false;
	// If a voice was started, we auto-advance when it stops.
	// If there is no voice, we wait for the player's click.
	const bool voiceStarted = _engine->_mixer->isSoundHandleActive(_voiceHandle);

	// Consume any click that triggered this dialog so the loop doesn't
	// immediately treat it as a "skip text" input.
	_engine->waitMouseRelease();

	// Working surface: full-screen, kept in sync with background + overlay each frame.
	Graphics::ManagedSurface surface(640, 480, fmt);

	while (!_engine->shouldAbort()) {
		_engine->pollEvents();

		Common::KeyState key = _engine->getNextKey();
		const bool spaceOrEnter =
		    key.keycode == Common::KEYCODE_SPACE ||
		    key.keycode == Common::KEYCODE_RETURN ||
		    key.keycode == Common::KEYCODE_KP_ENTER;

		if (spaceOrEnter || _engine->getCurrentMouseButton() == 1) {
			if (!fullTextShown) {
				// EXE 0x40d9a0(1): stops the voice + SYC when the text is fast-forwarded
				fullTextShown = true;
				stopVoice();
				_sycEvents.clear();
				_sycEventIdx = 0;
			} else if (waitingForAdvance) {
				stopVoice();
				_engine->waitMouseRelease();
				break;
			}
		}

		// Progress typewriter.
		uint32 now = g_system->getMillis();
		int charsToShow = fullTextShown ? (int)text.size()
		                                : (int)((now - startMs) / kMsPerChar);
		if (charsToShow >= (int)text.size()) {
			fullTextShown = true;
			waitingForAdvance = true;
		}

		// Rebuild the displayed lines up to charsToShow characters.
		int remaining = charsToShow;
		Common::Array<Common::String> visibleLines;
		for (uint li = 0; li < lines.size() && remaining > 0; ++li) {
			if (remaining >= (int)lines[li].size()) {
				visibleLines.push_back(lines[li]);
				remaining -= (int)lines[li].size();
			} else {
				visibleLines.push_back(lines[li].substr(0, remaining));
				remaining = 0;
			}
		}

		// Render: blit background, draw portrait, draw overlay box, draw text.
		surface.blitFrom(background);

		// Portrait: TGA (static base) -> SPB (mouth, SYC frame) -> SPA (idle face).
		// Order matches EXE 0x40a0b0: SPB then SPA on top of the TGA.

		// Mouth frame: driven by SYC, fallback 2.
		uint mouthFrame = 2;
		if (!_sycEvents.empty()) {
			const uint32 elapsed = g_system->getMillis() - _nodeStartMs;
			// Advance to the last event whose adjusted time <= elapsed.
			while (_sycEventIdx + 1 < _sycEvents.size()) {
				uint32 nextT = _sycEvents[_sycEventIdx + 1].timeMs;
				if (nextT >= 0x122u) nextT -= 0x122u;
				if (nextT <= elapsed)
					++_sycEventIdx;
				else
					break;
			}
			const uint32 code = _sycEvents[_sycEventIdx].phonemeCode;
			mouthFrame = (code < 27u) ? kSycToMouthFrame[code] : 2u;
			if (mouthFrame > 11u) mouthFrame = 2u;
		}

		// Idle SPA frame: cycles via kIdleMouthFrames every ~150 ms.
		if (_idleNextMs == 0 || now >= _idleNextMs) {
			_idleFrameIdx = (_idleFrameIdx + 1) % 10;
			_idleNextMs = now + 150;
		}
		const uint idleFrame = kIdleMouthFrames[_idleFrameIdx];

		if (_tga.w > 0)
			surface.blitFrom(_tga);
		if (!_spb.empty() && mouthFrame != 11u)
			blitSpriteFrame(surface, _spb, mouthFrame);
		if (!_spa.empty())
			blitSpriteFrame(surface, _spa, idleFrame);

		const uint32 bgColor = fmt.RGBToColor(16, 16, 16);
		surface.fillRect(Common::Rect(0, boxTop, 640, 480), bgColor);

		fm.setForeColor(fmt.RGBToColor(255, 255, 255));
		int y = boxTop + kDialogPadding;
		for (uint li = 0; li < visibleLines.size(); ++li, y += lineH)
			fm.displayStr(surface, kDialogTextX, y, visibleLines[li]);

		g_system->copyRectToScreen(surface.getPixels(), surface.pitch, 0, 0, kScreenWidth, kScreenHeight);
		g_system->updateScreen();

		// Auto-advance when the voice has finished (and the text is fully displayed).
		// Without a voice, we wait for the player's click.
		if (waitingForAdvance && voiceStarted && !_engine->_mixer->isSoundHandleActive(_voiceHandle))
			break;

		g_system->delayMillis(16);
	}
}

// --- Choices display ---

// Displays Ramose reply choices in the bottom area.
// Returns the index of the chosen option, or -1 if the player aborted.
int Egypt_Dialog::showChoices(const Graphics::ManagedSurface &background,
                                             const Common::Array<EgyptDialogChoice> &choices) {
	if (choices.empty())
		return -1;

	// Original CRYOFONT (provisional slot, see kSlotDialog)
	Egypt_FontManager &fm = _engine->_fontManager;
	fm.setCurrentFont(Egypt_FontManager::kSlotDialog);

	const int lineH      = fm.getFontHeight() + 3;
	const int totalH     = (int)choices.size() * lineH + kDialogPadding * 2;
	const int boxTop     = MAX(kDialogBoxTop, kScreenHeight - totalH - 4);
	const Graphics::PixelFormat &fmt = g_system->getScreenFormat();

	// Pre-compute choice rects for hit-testing.
	Common::Array<Common::Rect> rects;
	for (uint i = 0; i < choices.size(); ++i) {
		int y = boxTop + kDialogPadding + (int)i * lineH;
		rects.push_back(Common::Rect(kDialogTextX, y, kDialogTextX + kDialogTextWidth, y + lineH));
	}

	Graphics::ManagedSurface surface(640, 480, fmt);
	const uint32 bgColor    = fmt.RGBToColor(16, 16, 16);
	// EXE UI convention: white text, orange (224,112,0) when highlighted
	// (color globals 0x4d9974 / 0x4d6070)
	const uint32 fgNormal   = fmt.RGBToColor(255, 255, 255);
	const uint32 fgHovered  = fmt.RGBToColor(224, 112, 0);

	int hoveredIdx = -1;

	while (!_engine->shouldAbort()) {
		_engine->pollEvents();

		Common::Point mouse = _engine->getMousePos();

		// Update hovered choice.
		hoveredIdx = -1;
		for (uint i = 0; i < rects.size(); ++i) {
			if (rects[i].contains(mouse)) {
				hoveredIdx = (int)i;
				break;
			}
		}

		// Render: TGA -> SPB (neutral mouth f.2) -> SPA (face f.0) -> choices.
		surface.blitFrom(background);
		if (_tga.w > 0)
			surface.blitFrom(_tga);
		if (!_spb.empty())
			blitSpriteFrame(surface, _spb, 2);
		if (!_spa.empty())
			blitSpriteFrame(surface, _spa, 0);
		surface.fillRect(Common::Rect(0, boxTop, 640, 480), bgColor);

		for (uint i = 0; i < choices.size(); ++i) {
			fm.setForeColor(((int)i == hoveredIdx) ? fgHovered : fgNormal);
			fm.displayStr(surface, kDialogTextX, rects[i].top, choices[i].displayText);
		}

		g_system->copyRectToScreen(surface.getPixels(), surface.pitch, 0, 0, kScreenWidth, kScreenHeight);
		g_system->updateScreen();

		if (_engine->getCurrentMouseButton() == 1 && hoveredIdx >= 0) {
			_engine->waitMouseRelease();
			return hoveredIdx;
		}

		Common::KeyState key = _engine->getNextKey();
		if (key.keycode == Common::KEYCODE_ESCAPE)
			return -1;

		g_system->delayMillis(16);
	}

	return -1;
}

// --- Main dialogue runner ---

void Egypt_Dialog::run(const Common::String &startLabel) {
	if (!_levelLoaded) {
		if (!loadLevelTxt()) {
			warning("Egypt: runDialogue: Level.txt could not be loaded, aborting");
			return;
		}
		_levelLoaded = true;
	}

	debugC(kDebugVariable, "Egypt: starting dialogue at label '%s'", startLabel.c_str());

	_engine->setInterfaceCursor(_engine->getDefaultCursorFrame());
	_engine->showMouse(true);

	// Snapshot the current screen; every rendered dialogue frame starts from this.
	Graphics::ManagedSurface background(kScreenWidth, kScreenHeight, g_system->getScreenFormat());
	{
		Graphics::Surface *screen = g_system->lockScreen();
		if (screen) {
			background.blitFrom(*screen);
			g_system->unlockScreen();
		}
	}

	// Force the portrait to reload for the first node.
	_speakerName.clear();
	const int level = _engine->getScriptVariableValue("Level");

	Common::String currentLabel = startLabel;
	currentLabel.toLowercase();

	while (!_engine->shouldAbort()) {
		Common::String text;
		Common::Array<EgyptDialogChoice> choices;
		Common::String nextLabel;

		EgyptDialogResult result = executeNode(currentLabel, text, choices, nextLabel);

		// Auto-chain: no text + direct jump -> skip UI entirely.
		if (result == kDlgJump && text.empty()) {
			currentLabel = nextLabel;
			continue;
		}

		// Resolve and load the portrait if the speaker has changed.
		const char *flcName = resolveSpeakerFlc(currentLabel);
		loadSpeaker(flcName ? flcName : "", level);

		// Load SYC (mouth sync) + reset animation state for this node.
		loadSyc(currentLabel);
		_nodeStartMs  = g_system->getMillis();
		_sycEventIdx  = 0;
		_idleFrameIdx = 0;
		_idleNextMs   = 0;

		// Start the voice for this label (EXE 0x40d5d0 / 0x40d6c0).
		playVoice(currentLabel);

		// Show NPC text (with typewriter), then act.
		if (!text.empty())
			showText(background, text);

		// Ensure the voice is stopped after display (in case the user did not skip).
		stopVoice();

		if (_engine->shouldAbort())
			break;

		switch (result) {
		case kDlgEnd:
		case kDlgShow:
			goto done;

		case kDlgJump:
			currentLabel = nextLabel;
			break;

		case kDlgChoices: {
			// Only one choice: chain automatically without showing the menu.
			int picked = (choices.size() == 1)
			             ? 0
			             : showChoices(background, choices);
			if (picked < 0)
				goto done;
			currentLabel = choices[picked].targetLabel;
			// The chosen Ramose node contains its own text + APC.
			break;
		}
		}
	}

done:
	stopVoice();
	_tga.free();
	_spa.clear();
	_spb.clear();
	_speakerName.clear();
	_sycEvents.clear();
	_sycEventIdx  = 0;
	_idleFrameIdx = 0;
	_idleNextMs   = 0;
	debugC(kDebugVariable, "Egypt: dialogue ended");
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
