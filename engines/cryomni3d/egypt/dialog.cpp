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

#include "common/endian.h"
#include "common/events.h"
#include "common/file.h"
#include "common/memstream.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"

#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/managed_surface.h"
#include "graphics/surface.h"

#include "image/tga.h"

#include "cryomni3d/egypt/engine.h"
#include "cryomni3d/image/cpx5.h"

namespace CryOmni3D {
namespace Egypt {

// ── Level.txt loading ─────────────────────────────────────────────────────────

bool CryOmni3DEngine_Egypt::loadLevelTxt() {
	Common::File file;
	// EXE builds: ref\FR\Level.txt (0x413080)
	if (!file.open(Common::Path("ref/FR/Level.txt"))) {
		warning("Egypt: failed to open ref/FR/Level.txt");
		return false;
	}

	_dialogueNodes.clear();

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
				_dialogueNodes[current.label] = current;

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
				textSeen = true; // recover — treat as command
				current.commands.push_back(trimmed);
			}
			continue;
		}

		current.commands.push_back(trimmed);
	}

	if (inBlock && !current.label.empty())
		_dialogueNodes[current.label] = current;

	warning("Egypt: Level.txt loaded — %u nodes", _dialogueNodes.size());
	return !_dialogueNodes.empty();
}

// ── Node lookup ───────────────────────────────────────────────────────────────

const EgyptDialogNode *CryOmni3DEngine_Egypt::findDialogNode(const Common::String &label) const {
	Common::String key = label;
	key.toLowercase();
	Common::HashMap<Common::String, EgyptDialogNode,
	    Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo>::const_iterator it =
	    _dialogueNodes.find(key);
	if (it != _dialogueNodes.end())
		return &it->_value;
	return nullptr;
}

// ── Ramose detection ──────────────────────────────────────────────────────────

// Labels whose characters at positions [1..2] are "ra" (case-insensitive) are
// Ramose player-reply options (EXE 0x413140).  Examples: sra0001, dra0101, ara0503.
static bool isRamoseChoiceLabel(const Common::String &label) {
	return label.size() >= 3 &&
	       (label[1] == 'r' || label[1] == 'R') &&
	       (label[2] == 'a' || label[2] == 'A');
}

// ── Speaker FLC mapping ───────────────────────────────────────────────────────

// EXE 0x413930: dispatch par famille (label[0]) puis code (label[1..2]).
// Certains codes sont ambigus entre familles (PT, VI, IN) — le contexte famille est requis.
static const char *resolveSpeakerFlc(const Common::String &label) {
	if (label.size() < 3)
		return nullptr;

	const char family = (char)toupper((unsigned char)label[0]);
	const char c1     = (char)toupper((unsigned char)label[1]);
	const char c2     = (char)toupper((unsigned char)label[2]);

	// RAMOSE est le speaker Ramose dans toutes les familles (labels *RA*)
	if (c1 == 'R' && c2 == 'A')
		return "RAMOSE";

	struct Entry { char family; char c1; char c2; const char *name; };
	static const Entry kTable[] = {
		// Famille S
		{ 'S', 'M', 'T', "MONTOUME" },
		{ 'S', 'I', 'M', "IMENAKHT" },
		{ 'S', 'I', 'N', "INHERKHA" },
		// Famille D (exclusif)
		{ 'D', 'C', 'A', "CABARETI" },
		{ 'D', 'O', 'U', "OUVRIERE" },
		{ 'D', 'P', 'E', "PENMENEF" },
		{ 'D', 'P', 'T', "PENTAOUR" },
		{ 'D', 'V', 'I', "VIEUX"    },
		{ 'D', 'E', 'N', "ENFANT"   },
		{ 'D', 'O', 'C', "COLERE"   },
		// D + A partagés
		{ 'D', 'E', 'A', "EMBAUMEU" },
		{ 'D', 'E', 'M', "EMBAUMEU" },
		{ 'A', 'E', 'A', "EMBAUMEU" },
		{ 'A', 'E', 'M', "EMBAUMEU" },
		// D + A + N partagés
		{ 'D', 'D', 'E', "DESSIN"   },
		{ 'A', 'D', 'E', "DESSIN"   },
		{ 'N', 'D', 'E', "DESSIN"   },
		{ 'M', 'D', 'E', "DESSIN"   },
		{ 'D', 'P', 'L', "PLEUREUS" },
		{ 'A', 'P', 'L', "PLEUREUS" },
		{ 'N', 'P', 'L', "PLEUREUS" },
		// D + A + M partagés
		{ 'D', 'P', 'O', "PORTIER"  },
		{ 'A', 'P', 'O', "PORTIER"  },
		{ 'M', 'P', 'O', "PORTIER"  },
		{ 'D', 'I', 'N', "ESCLAV"   },
		{ 'A', 'I', 'N', "ESCLAV"   },
		{ 'M', 'I', 'N', "ESCLAV"   },
		{ 'D', 'F', 'N', "FEMME"    },
		{ 'A', 'F', 'N', "FEMME"    },
		{ 'M', 'F', 'N', "FEMME"    },
		// Famille M (exclusif)
		{ 'M', 'P', 'T', "PTAHEMEB" },
		{ 'M', 'N', 'O', "NOBLE"    },
		{ 'M', 'P', 'A', "PANAHESY" },
		// Famille K
		{ 'K', 'C', 'O', "AMEROUTH" },
		{ 'K', 'P', 'R', "PRETRE"   },
		{ 'K', 'D', 'O', "DOYEN"    },
		{ 'K', 'H', 'O', "HOROLOG"  },
		{ 'K', 'V', 'I', "TOH"      },
		{ 'K', 'P', 'T', "PTANEFER" },
	};

	for (uint i = 0; i < ARRAYSIZE(kTable); ++i) {
		if (kTable[i].family == family && kTable[i].c1 == c1 && kTable[i].c2 == c2)
			return kTable[i].name;
	}
	return nullptr;
}

// ── SYC phonème → frame bouche (EXE 0x4356a8, utilisé à l'étape 3) ───────────

static const uint kSycToMouthFrame[27] = {
	1, 2, 2, 8, 2, 2, 11, 8, 3,
	9, 2, 8, 1, 8, 7,  2, 7, 4,
	6, 7, 7, 8, 6, 10, 5, 0, 2
};

// Frames idle pour la bouche entre les phonèmes (EXE 0x435680)
static const uint kIdleMouthFrames[10] = {
	0, 1, 2, 3, 3, 3, 2, 1, 0, 0
};

// ── Node execution ────────────────────────────────────────────────────────────

// Runs the command list of one Level.txt block.
// Sets outText to the block's <text>, then processes commands in order.
// Returns the terminal action; caller shows outText (if non-empty) then acts.
EgyptDialogResult CryOmni3DEngine_Egypt::executeDialogNode(
        const Common::String &label,
        Common::String &outText,
        Common::Array<EgyptDialogChoice> &outChoices,
        Common::String &outNextLabel) {

	outText.clear();
	outChoices.clear();
	outNextLabel.clear();

	const EgyptDialogNode *node = findDialogNode(label);
	if (!node) {
		warning("Egypt: dialogue label not found: %s", label.c_str());
		return kDlgEnd;
	}

	const char *speaker = resolveSpeakerFlc(node->label);
	warning("Egypt: dialogue node '%s' speaker=%s text='%s'",
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
			if (evaluateScriptCondition(condition)) {
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
					const EgyptDialogNode *choiceNode = findDialogNode(targets[j]);
					choice.displayText = choiceNode ? choiceNode->text : targets[j];
					outChoices.push_back(choice);
				}
				return kDlgChoices;
			}
			continue;
		}

		if (cmd.hasPrefixIgnoreCase("let ")) {
			setScriptVariable(cmd.substr(4));
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
				const EgyptDialogNode *choiceNode = findDialogNode(targets[j]);
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

	// EXE: "DIALOGUE : Pas de correspondance en fin de dialogue" — treat as end.
	warning("Egypt: dialogue node '%s' has no end/goto", node->label.c_str());
	return kDlgEnd;
}

// ── Portrait loading ──────────────────────────────────────────────────────────

// Ouvre un fichier CPx5 ou brut et place les données décompressées dans out.
static bool loadRawOrCpx5(const Common::Path &path, Common::Array<byte> &data) {
	Common::File file;
	if (!file.open(path))
		return false;

	if (file.size() >= 4) {
		char magic[4];
		file.read(magic, 4);
		file.seek(0);
		if (memcmp(magic, "CPx5", 4) == 0)
			return Image::Cpx5Decoder::decompress(file, data);
	}

	// Fichier non compressé
	int32 size = file.size();
	if (size <= 0)
		return false;
	data.resize((uint32)size);
	return file.read(data.data(), (uint32)size) == (uint32)size;
}

// Charge un fichier SPA ou SPB.
// Format : table de N entrées de 8 octets [uint32 offset][uint16 h][uint16 w],
// où chaque offset pointe vers un bloc TXEN/RLE dans le même buffer.
// Les données brutes (décompressées) sont conservées intactes pour le blit TXEN.
bool CryOmni3DEngine_Egypt::loadDialogSprite(const Common::Path &path, EgyptDialogSprite &out) {
	out.clear();

	if (!loadRawOrCpx5(path, out.data))
		return false;

	if (out.data.size() < 8)
		return false;

	// Le premier offset (table[0].offset) = taille de la table = frameCount * 8.
	const uint32 firstOffset = READ_LE_UINT32(out.data.data());
	if (firstOffset == 0 || (firstOffset % 8) != 0 || firstOffset > out.data.size())
		return false;

	out.frameCount = firstOffset / 8;
	return true;
}

// Charge SPA + SPB pour speakerName au niveau donné (cache : ne recharge que si le nom change).
void CryOmni3DEngine_Egypt::loadDialogSpeaker(const Common::String &speakerName, int level) {
	if (speakerName == _dlgSpeakerName)
		return;

	_dlgTga.free();
	_dlgSpa.clear();
	_dlgSpb.clear();
	_dlgSpeakerName = speakerName;

	if (speakerName.empty() || level < 1 || level > 6)
		return;

	// Chemin réel confirmé : SPRITE/LEVELx/<scene>/<PERSONNAGE>.SPA/SPB/TGA
	const Common::String &sceneName = _currentScene.name;
	if (sceneName.empty())
		return;

	Common::Path tgaPath(Common::String::format("SPRITE/LEVEL%d/%s/%s.TGA",
	                                             level, sceneName.c_str(), speakerName.c_str()));
	Common::Path spaPath(Common::String::format("SPRITE/LEVEL%d/%s/%s.SPA",
	                                             level, sceneName.c_str(), speakerName.c_str()));
	Common::Path spbPath(Common::String::format("SPRITE/LEVEL%d/%s/%s.SPB",
	                                             level, sceneName.c_str(), speakerName.c_str()));

	// TGA : image de base statique du personnage.
	// Les fichiers .TGA sont CPx5-compressés sur disque — décompresser d'abord,
	// puis décoder le TGA standard depuis un MemoryReadStream.
	{
		Common::Array<byte> tgaData;
		if (loadRawOrCpx5(tgaPath, tgaData)) {
			Common::MemoryReadStream tgaStream(tgaData.data(), tgaData.size());
			Image::TGADecoder tgaDecoder;
			if (tgaDecoder.loadStream(tgaStream))
				_dlgTga.copyFrom(*tgaDecoder.getSurface());
			else
				warning("Egypt: failed to decode TGA for %s (level %d)", speakerName.c_str(), level);
			tgaDecoder.destroy();
		} else {
			warning("Egypt: no TGA portrait for %s (level %d)", speakerName.c_str(), level);
		}
	}

	if (!loadDialogSprite(spaPath, _dlgSpa))
		warning("Egypt: no SPA portrait for %s (level %d)", speakerName.c_str(), level);
	if (!loadDialogSprite(spbPath, _dlgSpb))
		warning("Egypt: no SPB portrait for %s (level %d)", speakerName.c_str(), level);
}

// Décode et blitte un patch TXEN/RLE d'une frame SPA ou SPB.
// Chaque frame pointe vers un bloc "TXEN" contenant ses coordonnées écran et un flux RLE.
// RLE : uint16 count (0=fin, 0xffff=ligne suivante), uint16 xOffset, puis count×uint16 RGB565.
void CryOmni3DEngine_Egypt::blitDialogSpriteFrame(Graphics::ManagedSurface &dst,
                                                   const EgyptDialogSprite &sprite,
                                                   uint frame) {
	if (sprite.empty() || frame >= sprite.frameCount)
		return;

	const uint32 offset = READ_LE_UINT32(sprite.data.data() + frame * 8);

	// Entrée sentinelle : offset pointe à EOF (ex : SPB frame 11 pour MONTOUME).
	// L'EXE saute le dessin dans ce cas.
	if (offset + 12 > sprite.data.size())
		return;

	const byte *p   = sprite.data.data() + offset;
	const byte *end = sprite.data.data() + sprite.data.size();

	// En-tête TXEN : [char[4] marker][int16 y][int16 x][uint16 h][uint16 w]
	// Les coordonnées sont des positions absolues sur l'écran 640×480.
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

			// Interpréter comme RGB565
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

// ── Voix APC ─────────────────────────────────────────────────────────────────

// Charge et lance sound/FR/<label>.apc via le mixer (canal kSpeechSoundType).
void CryOmni3DEngine_Egypt::playDialogVoice(const Common::String &label) {
	stopDialogVoice();

	Common::File file;
	Common::Path path(Common::String::format("sound/FR/%s.apc", label.c_str()));
	if (!file.open(path))
		return;

	Audio::PacketizedAudioStream *stream = Audio::makeAPCStream(file);
	if (!stream)
		return;

	// Après lecture du header (32 octets), le reste du fichier est l'audio ADPCM.
	int32 remaining = (int32)(file.size() - file.pos());
	if (remaining > 0) {
		byte *buf = new byte[(uint32)remaining];
		file.read(buf, (uint32)remaining);
		stream->queuePacket(new Common::MemoryReadStream(buf, (uint32)remaining, DisposeAfterUse::YES));
	}
	stream->finish();

	_mixer->playStream(Audio::Mixer::kSpeechSoundType, &_dlgVoiceHandle, stream);
}

void CryOmni3DEngine_Egypt::stopDialogVoice() {
	if (_mixer->isSoundHandleActive(_dlgVoiceHandle))
		_mixer->stopHandle(_dlgVoiceHandle);
}

// ── SYC mouth sync ────────────────────────────────────────────────────────────

// Charge le fichier SYC pour un label donné.
// Fallback : syc/FR/<label>.syc → sound/FR/<label>.syc → sound/FR/null.syc
// Format : 0x98 octets d'en-tête ignorés, puis enregistrements de 12 octets.
void CryOmni3DEngine_Egypt::loadDialogSyc(const Common::String &label) {
	_dlgSycEvents.clear();
	_dlgSycEventIdx = 0;

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
		file.readUint32LE(); // unknown, ignoré
		_dlgSycEvents.push_back(ev);
	}
}

// ── Text rendering helpers ────────────────────────────────────────────────────

namespace {

const int kDialogTextX      = 4;
const int kDialogTextWidth  = 630;
const int kDialogBoxTop     = 320;   // text area starts at y=320 on 640x480 screen
const int kDialogPadding    = 6;
const uint32 kColorNpcText  = 0xFFFFFFFF; // white — overridden to screen format in use
const uint32 kColorChoice   = 0xFFFFFF00; // yellow
const uint32 kColorHovered  = 0xFFFFCC33; // bright amber
const uint32 kColorBoxBg    = 0xFF101010; // near-black

// Build the dialogue text area rectangle.
inline Common::Rect dialogBoxRect() {
	return Common::Rect(0, kDialogBoxTop, 640, 480);
}

} // anonymous namespace

// ── Text display with typewriter ──────────────────────────────────────────────

// Displays text in the bottom text area over the background snapshot.
// Returns when the player clicks or presses Space/Return.
void CryOmni3DEngine_Egypt::showDialogText(const Graphics::ManagedSurface &background,
                                           const Common::String &text) {
	if (text.empty())
		return;

	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kConsoleFont);
	if (!font)
		font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!font) {
		warning("Egypt: dialogue: no font available, skipping text display");
		g_system->delayMillis(1500);
		return;
	}

	// Word-wrap the text to fit in the dialogue column.
	Common::Array<Common::String> lines;
	font->wordWrapText(text, kDialogTextWidth, lines);

	const int lineH    = font->getFontHeight() + 2;
	const int totalH   = (int)lines.size() * lineH + kDialogPadding * 2;
	const int boxTop   = MAX(kDialogBoxTop, 480 - totalH - 4);
	const Graphics::PixelFormat &fmt = g_system->getScreenFormat();

	// Typewriter state
	// Each 'unit' = one character; we reveal one unit every 70 ms (EXE: 7 centiseconds).
	const uint32 kMsPerChar = 70;
	uint32 startMs = g_system->getMillis();
	bool fullTextShown = false;
	bool waitingForAdvance = false;
	// Si une voix a été lancée, on auto-avance quand elle s'arrête.
	// Si pas de voix, on attend le clic du joueur.
	const bool voiceStarted = _mixer->isSoundHandleActive(_dlgVoiceHandle);

	// Consume any click that triggered this dialog so the loop doesn't
	// immediately treat it as a "skip text" input.
	waitMouseRelease();

	// Working surface: full-screen, kept in sync with background + overlay each frame.
	Graphics::ManagedSurface surface(640, 480, fmt);

	while (!shouldAbort()) {
		pollEvents();

		Common::KeyState key = getNextKey();
		const bool spaceOrEnter =
		    key.keycode == Common::KEYCODE_SPACE ||
		    key.keycode == Common::KEYCODE_RETURN ||
		    key.keycode == Common::KEYCODE_KP_ENTER;

		if (spaceOrEnter || getCurrentMouseButton() == 1) {
			if (!fullTextShown) {
				// EXE 0x40d9a0(1) : stoppe la voix + SYC quand on accélère le texte
				fullTextShown = true;
				stopDialogVoice();
				_dlgSycEvents.clear();
				_dlgSycEventIdx = 0;
			} else if (waitingForAdvance) {
				stopDialogVoice();
				waitMouseRelease();
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

		// Portrait : TGA (base statique) → SPB (bouche, frame SYC) → SPA (visage idle).
		// Ordre conforme EXE 0x40a0b0 : SPB puis SPA sur le TGA.

		// Frame bouche : pilotée par SYC, fallback 2.
		uint mouthFrame = 2;
		if (!_dlgSycEvents.empty()) {
			const uint32 elapsed = g_system->getMillis() - _dlgNodeStartMs;
			// Avancer jusqu'au dernier événement dont le temps ajusté <= elapsed.
			while (_dlgSycEventIdx + 1 < _dlgSycEvents.size()) {
				uint32 nextT = _dlgSycEvents[_dlgSycEventIdx + 1].timeMs;
				if (nextT >= 0x122u) nextT -= 0x122u;
				if (nextT <= elapsed)
					++_dlgSycEventIdx;
				else
					break;
			}
			const uint32 code = _dlgSycEvents[_dlgSycEventIdx].phonemeCode;
			mouthFrame = (code < 27u) ? kSycToMouthFrame[code] : 2u;
			if (mouthFrame > 11u) mouthFrame = 2u;
		}

		// Frame idle SPA : cycle via kIdleMouthFrames toutes les ~150 ms.
		if (_dlgIdleNextMs == 0 || now >= _dlgIdleNextMs) {
			_dlgIdleFrameIdx = (_dlgIdleFrameIdx + 1) % 10;
			_dlgIdleNextMs = now + 150;
		}
		const uint idleFrame = kIdleMouthFrames[_dlgIdleFrameIdx];

		if (_dlgTga.w > 0)
			surface.blitFrom(_dlgTga);
		if (!_dlgSpb.empty() && mouthFrame != 11u)
			blitDialogSpriteFrame(surface, _dlgSpb, mouthFrame);
		if (!_dlgSpa.empty())
			blitDialogSpriteFrame(surface, _dlgSpa, idleFrame);

		const uint32 bgColor = fmt.RGBToColor(16, 16, 16);
		const uint32 fgColor = fmt.RGBToColor(255, 255, 255);
		surface.fillRect(Common::Rect(0, boxTop, 640, 480), bgColor);

		int y = boxTop + kDialogPadding;
		for (uint li = 0; li < visibleLines.size(); ++li, y += lineH)
			font->drawString(&surface, visibleLines[li], kDialogTextX, y, kDialogTextWidth, fgColor);

		g_system->copyRectToScreen(surface.getPixels(), surface.pitch, 0, 0, 640, 480);
		g_system->updateScreen();

		// Auto-avance quand la voix est terminée (et le texte entièrement affiché).
		// Sans voix, on attend le clic du joueur.
		if (waitingForAdvance && voiceStarted && !_mixer->isSoundHandleActive(_dlgVoiceHandle))
			break;

		g_system->delayMillis(16);
	}
}

// ── Choices display ───────────────────────────────────────────────────────────

// Displays Ramose reply choices in the bottom area.
// Returns the index of the chosen option, or -1 if the player aborted.
int CryOmni3DEngine_Egypt::showDialogChoices(const Graphics::ManagedSurface &background,
                                             const Common::Array<EgyptDialogChoice> &choices) {
	if (choices.empty())
		return -1;

	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kConsoleFont);
	if (!font)
		font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!font) {
		warning("Egypt: dialogue: no font for choices");
		return 0;
	}

	const int lineH      = font->getFontHeight() + 3;
	const int totalH     = (int)choices.size() * lineH + kDialogPadding * 2;
	const int boxTop     = MAX(kDialogBoxTop, 480 - totalH - 4);
	const Graphics::PixelFormat &fmt = g_system->getScreenFormat();

	// Pre-compute choice rects for hit-testing.
	Common::Array<Common::Rect> rects;
	for (uint i = 0; i < choices.size(); ++i) {
		int y = boxTop + kDialogPadding + (int)i * lineH;
		rects.push_back(Common::Rect(kDialogTextX, y, kDialogTextX + kDialogTextWidth, y + lineH));
	}

	Graphics::ManagedSurface surface(640, 480, fmt);
	const uint32 bgColor    = fmt.RGBToColor(16, 16, 16);
	const uint32 fgNormal   = fmt.RGBToColor(255, 255, 80);
	const uint32 fgHovered  = fmt.RGBToColor(255, 220, 40);

	int hoveredIdx = -1;

	while (!shouldAbort()) {
		pollEvents();

		Common::Point mouse = getMousePos();

		// Update hovered choice.
		hoveredIdx = -1;
		for (uint i = 0; i < rects.size(); ++i) {
			if (rects[i].contains(mouse)) {
				hoveredIdx = (int)i;
				break;
			}
		}

		// Render : TGA → SPB (bouche neutre f.2) → SPA (visage f.0) → choix.
		surface.blitFrom(background);
		if (_dlgTga.w > 0)
			surface.blitFrom(_dlgTga);
		if (!_dlgSpb.empty())
			blitDialogSpriteFrame(surface, _dlgSpb, 2);
		if (!_dlgSpa.empty())
			blitDialogSpriteFrame(surface, _dlgSpa, 0);
		surface.fillRect(Common::Rect(0, boxTop, 640, 480), bgColor);

		for (uint i = 0; i < choices.size(); ++i) {
			const uint32 color = ((int)i == hoveredIdx) ? fgHovered : fgNormal;
			font->drawString(&surface, choices[i].displayText,
			                 kDialogTextX, rects[i].top, kDialogTextWidth, color);
		}

		g_system->copyRectToScreen(surface.getPixels(), surface.pitch, 0, 0, 640, 480);
		g_system->updateScreen();

		if (getCurrentMouseButton() == 1 && hoveredIdx >= 0) {
			waitMouseRelease();
			return hoveredIdx;
		}

		Common::KeyState key = getNextKey();
		if (key.keycode == Common::KEYCODE_ESCAPE)
			return -1;

		g_system->delayMillis(16);
	}

	return -1;
}

// ── Main dialogue runner ──────────────────────────────────────────────────────

void CryOmni3DEngine_Egypt::runDialogue(const Common::String &startLabel) {
	if (!_dialogueLevelLoaded) {
		if (!loadLevelTxt()) {
			warning("Egypt: runDialogue: Level.txt could not be loaded, aborting");
			return;
		}
		_dialogueLevelLoaded = true;
	}

	warning("Egypt: starting dialogue at label '%s'", startLabel.c_str());

	setInterfaceCursor(getDefaultCursorFrame());
	showMouse(true);

	// Snapshot the current screen; every rendered dialogue frame starts from this.
	Graphics::ManagedSurface background(640, 480, g_system->getScreenFormat());
	{
		Graphics::Surface *screen = g_system->lockScreen();
		if (screen) {
			background.blitFrom(*screen);
			g_system->unlockScreen();
		}
	}

	// Forcer le rechargement du portrait au premier nœud.
	_dlgSpeakerName.clear();
	const int level = getScriptVariableValue("Level");

	Common::String currentLabel = startLabel;
	currentLabel.toLowercase();

	while (!shouldAbort()) {
		Common::String text;
		Common::Array<EgyptDialogChoice> choices;
		Common::String nextLabel;

		EgyptDialogResult result = executeDialogNode(currentLabel, text, choices, nextLabel);

		// Auto-chain: no text + direct jump → skip UI entirely.
		if (result == kDlgJump && text.empty()) {
			currentLabel = nextLabel;
			continue;
		}

		// Résolution et chargement du portrait si le speaker a changé.
		const char *flcName = resolveSpeakerFlc(currentLabel);
		loadDialogSpeaker(flcName ? flcName : "", level);

		// Chargement SYC (synchro bouche) + reset état animation pour ce nœud.
		loadDialogSyc(currentLabel);
		_dlgNodeStartMs  = g_system->getMillis();
		_dlgSycEventIdx  = 0;
		_dlgIdleFrameIdx = 0;
		_dlgIdleNextMs   = 0;

		// Lancement de la voix pour ce label (EXE 0x40d5d0 / 0x40d6c0).
		playDialogVoice(currentLabel);

		// Show NPC text (with typewriter), then act.
		if (!text.empty())
			showDialogText(background, text);

		// Assurer l'arrêt de la voix après affichage (cas où user n'a pas skipé).
		stopDialogVoice();

		if (shouldAbort())
			break;

		switch (result) {
		case kDlgEnd:
		case kDlgShow:
			goto done;

		case kDlgJump:
			currentLabel = nextLabel;
			break;

		case kDlgChoices: {
			// Un seul choix : enchaîner automatiquement sans afficher le menu.
			int picked = (choices.size() == 1)
			             ? 0
			             : showDialogChoices(background, choices);
			if (picked < 0)
				goto done;
			currentLabel = choices[picked].targetLabel;
			// Le nœud Ramose choisi contient son propre texte + APC.
			break;
		}
		}
	}

done:
	stopDialogVoice();
	_dlgTga.free();
	_dlgSpa.clear();
	_dlgSpb.clear();
	_dlgSpeakerName.clear();
	_dlgSycEvents.clear();
	_dlgSycEventIdx  = 0;
	_dlgIdleFrameIdx = 0;
	_dlgIdleNextMs   = 0;
	warning("Egypt: dialogue ended");
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
