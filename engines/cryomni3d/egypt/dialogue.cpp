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

#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"

#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/managed_surface.h"
#include "graphics/surface.h"

#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

// ── Level.txt loading ─────────────────────────────────────────────────────────

bool CryOmni3DEngine_Egypt::loadLevelTxt() {
	Common::File file;
	// EXE builds: ref\FR\Level.txt (0x413080)
	if (!file.open(Common::Path("Level.txt"))) {
		warning("Egypt: failed to open Level.txt");
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

// EXE 0x413930: map 2-char speaker code (label[1..2]) to FLC base name.
static const char *mapSpeakerCode(const Common::String &label) {
	if (label.size() < 3)
		return nullptr;

	struct CodeMap { const char *code; const char *flc; };
	static const CodeMap kCodes[] = {
		{ "MT", "MONTOUME" },
		{ "IM", "IMENAKHT" },
		{ "IN", "INHERKHA" },
		{ "RA", "RAMOSE"   },
		{ "CA", "CABARETI" },
		{ "OU", "OUVRIERE" },
		{ "PE", "PENMENEF" },
		{ "PT", "PENTAOUR" },
		{ "VI", "VIEUX"    },
		{ "EN", "ENFANT"   },
		{ "OC", "COLERE"   },
		{ "EM", "EMBAUMEU" },
		{ "EA", "EMBAUMEU" },
		{ "DE", "DESSIN"   },
		{ "PL", "PLEUREUS" },
		{ "PO", "PORTIER"  },
	};

	char code[3] = { (char)toupper((unsigned char)label[1]),
	                 (char)toupper((unsigned char)label[2]), '\0' };

	for (uint i = 0; i < ARRAYSIZE(kCodes); ++i) {
		if (strcmp(kCodes[i].code, code) == 0)
			return kCodes[i].flc;
	}
	return nullptr;
}

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

	const char *speaker = mapSpeakerCode(node->label);
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
				outNextLabel = target;
				outNextLabel.toLowercase();
				return kDlgJump;
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
				fullTextShown = true;
			} else if (waitingForAdvance) {
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

		// Render: blit background, draw overlay box, draw text.
		surface.blitFrom(background);

		const uint32 bgColor = fmt.RGBToColor(16, 16, 16);
		const uint32 fgColor = fmt.RGBToColor(255, 255, 255);
		surface.fillRect(Common::Rect(0, boxTop, 640, 480), bgColor);

		int y = boxTop + kDialogPadding;
		for (uint li = 0; li < visibleLines.size(); ++li, y += lineH)
			font->drawString(&surface, visibleLines[li], kDialogTextX, y, kDialogTextWidth, fgColor);

		g_system->copyRectToScreen(surface.getPixels(), surface.pitch, 0, 0, 640, 480);
		g_system->updateScreen();
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

		// Render.
		surface.blitFrom(background);
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

	// Snapshot the current screen; every rendered dialogue frame starts from this.
	Graphics::ManagedSurface background(640, 480, g_system->getScreenFormat());
	{
		Graphics::Surface *screen = g_system->lockScreen();
		if (screen) {
			background.blitFrom(*screen);
			g_system->unlockScreen();
		}
	}

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

		// Show NPC text (with typewriter), then act.
		if (!text.empty())
			showDialogText(background, text);

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
			int picked = showDialogChoices(background, choices);
			if (picked < 0)
				goto done;
			currentLabel = choices[picked].targetLabel;

			// The chosen Ramose label has its own text (shown as NPC line for Ramose).
			// After the player's line, we need the *next* node, so parse again.
			// The Ramose block contains the reply text + its own goto.
			break;
		}
		}
	}

done:
	warning("Egypt: dialogue ended");
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
