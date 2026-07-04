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

// Documentation space ("base documentaire"), layout reproduced from
// EGYPTE.EXE (see devtools-egypt/doc_viewer_reverse_notes.md):
//
// - State machine 0x801000/0x801200 on global 0x4c2078; bit15 = entered
//   from the game (fiche shows only the exit spiral), bit15 clear =
//   standalone browser from the accueil menu (full navigation).
// - Sommaire (SOMMAIRE.TGA): 5 theme labels font 2 at x=100,
//   y={52,108,167,225,283} (table VA 0x435030), white/orange hover.
// - Theme fiche lists: level 1 font 7 at x=230, step 16, start=themeY+5
//   bottom-clamped to 470-16*count; level 2 at x=250+level1MaxWidth,
//   ellipsis-truncated at the right edge (renderer 0x803a60).
// - Fiche page 0x802b80: FicheXxx.TGA background (table VA 0x435048),
//   title font 3 white with black shadow at (120,27) width 518, photo at
//   native size (x=318, vertically centered in 52..383+caption), caption
//   font 8 justified under the photo, "@" annotations from ESPDOC.TXT
//   drawn over the photo (fonts {5,4,7} per type, table VA 0x435060),
//   body justified (0x8037e0) font 1 x=15 width 290 next to a photo or
//   font 7 width 620 otherwise; hyperlink words orange, hover shows the
//   target title font 1 orange at (94,398) width 544.
// - Nav arrows INTERFAC 0xcf/0xce (prev, x=95) and 0xd1/0xd0 (next,
//   x=122) at y=455; exit spiral INTERFAC 0x13 at (0,460); alphabetical
//   index overlay 0x802520 (button INTERFAC 0xd2 at (606,450) via its
//   TEXN header, entries from REF/FR/EspIndex.txt, font 10, right-aligned
//   rows of 16px ending at y=430, hover-dwell "..." rows).
// - Chronology page (state 0xf, handler 0x8019bd) for records 211/212:
//   left column of period titles + dates, hovered row shows the record
//   body in a detail pane on the right.

#include "common/debug.h"
#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"

#include "graphics/managed_surface.h"

#include "cryomni3d/egypt/documentation.h"
#include "cryomni3d/egypt/engine.h"
#include "cryomni3d/egypt/support/image_loader.h"

namespace CryOmni3D {
namespace Egypt {

namespace {

const int kEgyptDocumentationThemeCount = 6;
const int kEgyptDocumentationThemeIds[kEgyptDocumentationThemeCount] = { 1, 2, 3, 4, 5, 6 };
// DEF message labels 0x4cabf4[i]; only the first 5 appear in the sommaire
const char *const kEgyptDocumentationThemeLabels[kEgyptDocumentationThemeCount] = {
	"La terre", "Le temps", "Les hommes", "Le pharaon", "Les dieux", "Personnages"
};
// Backgrounds table VA 0x435048 (FicheTer..FichePer), indexed by theme
const char *const kEgyptDocumentationThemeBackgrounds[kEgyptDocumentationThemeCount] = {
	"FICHETER.TGA",  // La terre
	"FICHETEM.TGA",  // Le temps
	"FICHEHOM.TGA",  // Les hommes
	"FICHEPHA.TGA",  // Le pharaon
	"FICHEDIE.TGA",  // Les dieux
	"FICHEPER.TGA",  // Personnages
};

// EXE font slots (slot N = FONT{N+1:02}.CRF)
const uint kSlotFicheTitle   = 3;  // 0x8030f6/0x803262: push 0x3
const uint kSlotFicheBody    = 1;  // 0x8031f4: mov edi,0x1 (photo layout)
const uint kSlotFicheWide    = 7;  // 0x803180: mov edi,0x7 (no photo/caption)
const uint kSlotFicheCaption = 8;  // 0x803146: push 0x8
const uint kSlotThemeLabel   = 2;  // 0x8017c0/0x8017db: push 0x2
const uint kSlotFicheList    = 7;  // 0x8013ca/0x801418: push 0x7
const uint kSlotAlphaIndex   = 10; // 0x8027cb/0x8029a2: push 0xa
// Annotation fonts by type, EXE table VA 0x435060
const uint kSlotAnnotation[3] = { 5, 4, 7 };

// Fiche page layout (draw function 0x802b80)
const int kFicheTitleX      = 120; // 0x80312a: push 0x78 (white pass)
const int kFicheTitleY      = 27;  // 0x80312c: push 0x1b
const int kFicheTitleWidth  = 518; // 0x8030e7: mov edi,0x206
const int kFichePhotoX      = 318; // 0x802c33: mov ebp,0x13e
const int kFichePhotoAreaY  = 52;  // 0x802ca7/0x802cbc: add ..,0x34
const int kFichePhotoAreaH  = 344; // 0x802cb0: mov eax,0x158 (with caption)
const int kFichePhotoAreaHNoCap = 379; // 0x802c95: mov eax,0x17b (centered photo)
const int kFichePhotoBottom = 383; // 0x802ccc: cmp eax,0x17f
const int kFichePhotoPushUp = 398; // 0x802cd3: mov eax,0x18e (y = 398-totalH)
const int kFicheCaptionWidth = 310; // 0x802c3d: push 0x136 / 0x274-0x13e
const int kFicheCaptionGap  = 5;   // 0x803150: lea ecx,[esi+eax*1+0x5]
const int kFicheBodyX       = 15;  // 0x80318a block: push 0xf
const int kFicheBodyWidth   = 290; // 0x8031f9: mov ebp,0x122 (photo layout)
const int kFicheBodyWideWidth = 620; // 0x80317b: mov ebp,0x26c
const int kFicheBodyY       = 100; // 0x803185: mov esi,0x64
const int kFicheBodyTitleGap = 57; // 0x80321a: lea eax,[ebx+0x39]
const int kFicheBodyBottom  = 398; // 0x80320f: cmp eax,0x18e
const int kFichePreviewX    = 94;  // 0x803348: push 0x5e
const int kFichePreviewY    = 398; // 0x80332f: lea eax,[edi+0x18e]
const int kFichePreviewWidth = 544; // 0x803343: push 0x220

// Interface sprites and their fixed positions (TEXN deltas)
const int kSpriteExitSpiral = 0x13; // 20x20 spiral at (0,460), 0x801762
const int kSpritePrevHover  = 0xce; // arrows 12x12 at y=455
const int kSpritePrevNormal = 0xcf; // x=95 (TEXN header of 0xce/0xcf)
const int kSpriteNextHover  = 0xd0;
const int kSpriteNextNormal = 0xd1; // x=122
const int kSpriteAlphaIndex = 0xd2; // 20x20 index toggle, 0x802539
const int kExitSpiralX = 0,   kExitSpiralY = 460; // 0x801764: push 0x1cc
const int kArrowPrevX  = 95,  kArrowNextX = 122, kArrowY = 455;
// The EXE pushes (0,0) for sprite 0xd2 but its TEXN pixel header carries
// the deltas x=606, y=450, placing the button bottom-right
const int kAlphaButtonX = 606, kAlphaButtonY = 450;

// Sommaire and theme lists (states 1/5/6)
const int kThemeLabelX = 100;      // 0x8017de: push 0x64
const int kThemeLabelY[5] = { 52, 108, 167, 225, 283 }; // table VA 0x435030
const int kThemeHitH   = 15;       // 0x80180b: push 0xf
const int kListLevel1X = 230;      // 0x8015f5: push 0xe6
const int kListStepY   = 16;       // 0x80161b: add ebp,0x10
const int kListBottomY = 470;      // 0x8015c8: mov ebx,0x1d6
const int kListLevel2XBase = 250;  // 0x801680: add eax,0xfa (+ level1 max width)
const int kListLevel2Right = 638;  // 0x8016b5: 0x184 + 0xfa = right edge
const int kListStartGap = 5;       // level-1 start = themeY + 5

// Alphabetical index overlay (0x802520)
const int kAlphaRowH        = 16;  // 0x80275b: shl edx,0xb (16*640)
const int kAlphaMaxVisible  = 24;  // 0x802748: cmp esi,0x18
const int kAlphaBottomY     = 430; // 0x802948: mov esi,0x1ae
const int kAlphaHitH        = 15;  // 0x8027f8: push 0xf
const int kAlphaDwellTicks  = 10;  // 0x802818: cmp eax,0xa
const int kAlphaPageStep    = 22;  // 0x8026f2: mov esi,0x16
const uint kAlphaMaxEntries = 200; // EXE cap (error string VA 0x435108)

// Chronology page (state 0xf, handler 0x8019bd; record 0xd3=211 uses the
// "general" layout, 0xd4=212 the "reign of Ramses III" layout)
const int kChronoRecordGeneral = 211; // 0x8019de: mov esi,0xd3
const int kChronoRecordRamses  = 212;
const uint kSlotChronoTitleGen = 0; // 0x801a1f: font 0, minRow = 2*height
const uint kSlotChronoTitleRam = 2; // 0x801a2f: font 2, minRow = height
const uint kSlotChronoDate     = 7; // 0x801b16: push 0x7
const uint kSlotChronoDetailRam = 0; // 0x801e4e: push 0x0 (left-aligned)
const int kChronoListX      = 30;  // 0x801b88: push 0x1e (rows and hit rects)
const int kChronoAvailH     = 355; // 0x801a3d: mov eax,0x163
const int kChronoTopY       = 66;  // 0x801a7d: add esi,0x42
const int kChronoDateXGen   = 50;  // 0x801bf6: push 0x32 (211 date column)
const int kChronoDateGap    = 20;  // 0x801b2f: add eax,0x14
const int kChronoDetailGap  = 50;  // 0x801c79: lea edi,[edx+0x32]
const int kChronoDetailBaseY = 90; // 0x801d45: mov eax,0x5a
const int kChronoDetailRightGen = 455; // 0x801d56: mov eax,0x1c7
const int kChronoDetailRightRam = 635; // 0x801de4: mov esi,0x27b
const int kChronoDetailBottom   = 421; // 0x801db1: cmp ecx,0x1a5
const int kChronoParaGap    = 3;   // 0x801da0/0x801dd2: push 0x3 (0x803b40)

// One word of a text block plus the link run it belongs to
struct BlockWord {
	Common::String text;
	int linkIndex;  // -1 = plain text
	bool paraBreak; // starts a new line (explicit line break before it)
};

struct BlockLinkRect {
	Common::Rect rect;
	int linkIndex;
};

void splitRunsIntoWords(const Common::Array<EgyptDocTextRun> &runs,
                        Common::Array<BlockWord> &out) {
	bool pendingBreak = false;
	for (uint ri = 0; ri < runs.size(); ++ri) {
		const Common::String &text = runs[ri].text;
		const int lnk = runs[ri].linkIndex;
		uint i = 0;
		while (i < text.size()) {
			if (text[i] == '\n') { pendingBreak = true; ++i; continue; }
			if (text[i] == ' ')  { ++i; continue; }
			uint j = i;
			while (j < text.size() && text[j] != ' ' && text[j] != '\n')
				++j;
			BlockWord w;
			w.text = text.substr(i, j - i);
			w.linkIndex = lnk;
			w.paraBreak = pendingBreak;
			out.push_back(w);
			pendingBreak = false;
			i = j;
		}
	}
}

void splitPlainTextIntoWords(const Common::String &text, Common::Array<BlockWord> &out) {
	Common::Array<EgyptDocTextRun> runs;
	EgyptDocTextRun run;
	run.text = text;
	run.linkIndex = -1;
	runs.push_back(run);
	splitRunsIntoWords(runs, out);
}

// Wrapped text block, EXE semantics:
// - justify=false: left-aligned lines (EXE 0x803670)
// - justify=true: extra pixels spread between the words of every full
//   line; paragraph-final and block-final lines stay left-aligned
//   (EXE 0x8037e0/0x803b40)
// Lines step by the font height exactly (glyph advance already carries
// the +1 spacing). Passing surface=nullptr only measures.
// linkColor is used for words with linkIndex >= 0; their rects go to hits.
int drawTextBlock(Egypt_FontManager &fm, Graphics::ManagedSurface *surface,
                  uint slot, const Common::Array<BlockWord> &words,
                  int x, int y, int width, bool justify,
                  uint32 color, uint32 linkColor = 0,
                  Common::Array<BlockLinkRect> *hits = nullptr) {
	fm.setCurrentFont(slot);
	const int lineH = fm.getFontHeight();
	const uint spaceW = fm.getStrWidth(" ");

	uint wi = 0;
	int lineCount = 0;
	while (wi < words.size()) {
		// Collect the words of one line
		uint lineStart = wi;
		uint lineEnd = wi;
		uint lineWidth = 0;
		bool paraFinal = false;
		while (lineEnd < words.size()) {
			if (lineEnd > lineStart && words[lineEnd].paraBreak) {
				paraFinal = true;
				break;
			}
			const uint ww = fm.getStrWidth(words[lineEnd].text);
			const uint candidate = (lineEnd > lineStart) ? lineWidth + spaceW + ww : ww;
			if (lineEnd > lineStart && candidate > (uint)width)
				break;
			lineWidth = candidate;
			++lineEnd;
		}
		if (lineEnd == words.size())
			paraFinal = true;

		if (surface) {
			const uint wordCount = lineEnd - lineStart;
			uint sumWidths = 0;
			for (uint i = lineStart; i < lineEnd; ++i)
				sumWidths += fm.getStrWidth(words[i].text);

			// EXE 0x8037e0: gap = (width - sum(wordWidths)) / (n-1),
			// remainder given out one pixel at a time from the left
			int gap = (int)spaceW, rem = 0;
			if (justify && !paraFinal && wordCount >= 2) {
				gap = ((int)width - (int)sumWidths) / (int)(wordCount - 1);
				rem = ((int)width - (int)sumWidths) % (int)(wordCount - 1);
			}

			int drawX = x;
			const int drawY = y + lineCount * lineH;
			for (uint i = lineStart; i < lineEnd; ++i) {
				const uint ww = fm.getStrWidth(words[i].text);
				const bool isLink = words[i].linkIndex >= 0;
				fm.setForeColor(isLink ? linkColor : color);
				fm.displayStr(*surface, drawX, drawY, words[i].text);
				if (isLink && hits) {
					BlockLinkRect hit;
					hit.rect = Common::Rect(drawX, drawY, drawX + (int)ww, drawY + lineH);
					hit.linkIndex = words[i].linkIndex;
					hits->push_back(hit);
				}
				drawX += (int)ww + gap + (rem > 0 ? 1 : 0);
				if (rem > 0)
					--rem;
			}
		}

		++lineCount;
		wi = lineEnd;
	}

	return lineCount * lineH;
}

int measureTextBlock(Egypt_FontManager &fm, uint slot,
                     const Common::Array<BlockWord> &words, int width) {
	return drawTextBlock(fm, nullptr, slot, words, 0, 0, width, false, 0);
}

// Single line truncated with "..." while wider than maxWidth (EXE 0x803a60)
void drawEllipsisLine(Egypt_FontManager &fm, Graphics::ManagedSurface &surface,
                      uint slot, const Common::String &text,
                      int x, int y, int maxWidth, uint32 color) {
	fm.setCurrentFont(slot);
	Common::String line = text;
	while (line.size() > 3 && fm.getStrWidth(line) > (uint)maxWidth) {
		line.deleteLastChar();
		line.deleteLastChar();
		line.deleteLastChar();
		line.deleteLastChar();
		line += "...";
	}
	fm.setForeColor(color);
	fm.displayStr(surface, x, y, line);
}

// Blit one interface sprite (RGB565, masked) onto a 32bpp surface,
// top-left anchored (EXE unscaled draw path 0x8193a9); same routine as
// toolbar.cpp/menu.cpp
void blitDocSprite(const EgyptInterfaceSprite &sprite, Graphics::ManagedSurface &dst,
                   int x, int y) {
	static const Graphics::PixelFormat kSpriteFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);
	const int w = sprite.surface.w;
	const int h = sprite.surface.h;
	const Graphics::PixelFormat &dstFmt = dst.format;

	for (int row = 0; row < h; row++) {
		const int dstY = y + row;
		if (dstY < 0 || dstY >= dst.h)
			continue;

		const byte *srcRow  = (const byte *)sprite.surface.getBasePtr(0, row);
		const byte *maskRow = sprite.mask.data() + row * w;

		for (int col = 0; col < w; col++) {
			if (maskRow[col] == kCursorMaskTransparent)
				continue;

			const int dstX = x + col;
			if (dstX < 0 || dstX >= dst.w)
				continue;

			const uint16 srcPixel = READ_LE_UINT16(srcRow + col * 2);
			uint8 r, g, b;
			kSpriteFormat.colorToRGB(srcPixel, r, g, b);
			WRITE_LE_UINT32((byte *)dst.getBasePtr(dstX, dstY), dstFmt.RGBToColor(r, g, b));
		}
	}
}

Common::Rect spriteHitRect(const EgyptInterfaceSprite &sprite, int x, int y) {
	return Common::Rect(x, y, x + sprite.surface.w, y + sprite.surface.h);
}

bool parseIntegerToken(const Common::String &token, int &value) {
	if (token.empty())
		return false;

	int parsed = 0;
	int consumed = 0;
	if (sscanf(token.c_str(), "%d%n", &parsed, &consumed) != 1 || (uint)consumed != token.size())
		return false;

	value = parsed;
	return true;
}

bool parseDocumentationHeader(const Common::String &line, int &id, Common::String &title) {
	const int dotPos = line.find('.');
	if (dotPos <= 0)
		return false;

	Common::String idToken = line.substr(0, dotPos);
	idToken.trim();
	if (!parseIntegerToken(idToken, id))
		return false;

	title = line.substr(dotPos + 1);
	title.trim();
	return true;
}

Common::String sanitizeDocumentationText(const Common::String &text) {
	Common::String sanitized;

	for (uint i = 0; i < text.size(); ++i) {
		const char ch = text[i];
		if (ch == '#')
			continue;
		if (ch == '&')
			sanitized += " - ";
		else
			sanitized += ch;
	}

	return sanitized;
}

// "@ type x y text": annotation drawn onto the record photo (EXE parser
// 0x805f60 fills the 76-byte elements at record+0xb00)
bool parseDocumentationAnnotation(const Common::String &line, EgyptDocAnnotation &out) {
	Common::String rest = line.substr(1);
	rest.trim();
	if (rest.empty())
		return false; // bare "@" separator line

	int type = 0, x = 0, y = 0, consumed = 0;
	if (sscanf(rest.c_str(), "%d %d %d%n", &type, &x, &y, &consumed) != 3)
		return false;

	Common::String text = rest.substr(consumed);
	text.trim();
	if (text.empty())
		return false;

	out.type = CLIP(type, 0, 2);
	out.x = x;
	out.y = y;
	out.text = text;
	return true;
}

Common::Path documentationAssetPathFromName(const Common::String &assetName) {
	if (assetName.empty())
		return Common::Path();

	if (assetName.hasPrefixIgnoreCase("F"))
		return Common::Path(Common::String::format("SPRITE/BASEDOC/%s.TGA", assetName.c_str()));

	if (assetName.hasPrefixIgnoreCase("PCD"))
		return Common::Path(Common::String::format("SPRITE/BASEDOC/%s.TGA", assetName.substr(3).c_str()));

	if (assetName.hasPrefixIgnoreCase("CD"))
		return Common::Path(Common::String::format("SPRITE/BASEDOC/%s.TGA", assetName.substr(2).c_str()));

	return Common::Path();
}

// Parse a raw body line (with #word## link markers and & replacements) into text runs.
// linkCounter is incremented for each #word## found and maps to record->links[N].
void parseBodyRunLine(const Common::String &rawLine, int &linkCounter,
                      Common::Array<EgyptDocTextRun> &runs) {
	uint i = 0;
	Common::String normal;
	while (i < rawLine.size()) {
		if (rawLine[i] == '&') {
			normal += " - ";
			++i;
		} else if (rawLine[i] == '#') {
			// find closing ##
			uint j = i + 1;
			while (j < rawLine.size()) {
				if (rawLine[j] == '#' && j + 1 < rawLine.size() && rawLine[j + 1] == '#')
					break;
				++j;
			}
			if (j < rawLine.size()) {
				// valid #TEXT## link
				if (!normal.empty()) {
					EgyptDocTextRun r; r.text = normal; r.linkIndex = -1;
					runs.push_back(r); normal.clear();
				}
				EgyptDocTextRun r;
				r.text = rawLine.substr(i + 1, j - i - 1);
				r.linkIndex = linkCounter++;
				runs.push_back(r);
				i = j + 2;
			} else {
				++i; // stray #, skip
			}
		} else {
			normal += rawLine[i];
			++i;
		}
	}
	if (!normal.empty()) {
		EgyptDocTextRun r; r.text = normal; r.linkIndex = -1;
		runs.push_back(r);
	}
}

} // End of anonymous namespace

const EgyptDocumentationRecord *Egypt_Documentation::findRecord(int id) const {
	for (Common::Array<EgyptDocumentationRecord>::const_iterator it = _records.begin(); it != _records.end(); ++it) {
		if (it->id == id)
			return &(*it);
	}

	return nullptr;
}

void Egypt_Documentation::collectLeafRecords(int nodeId, Common::Array<int> &out) const {
	const EgyptDocumentationRecord *record = findRecord(nodeId);
	if (record) {
		bool alreadyPresent = false;
		for (Common::Array<int>::const_iterator it = out.begin(); it != out.end(); ++it) {
			if (*it == nodeId) {
				alreadyPresent = true;
				break;
			}
		}
		if (!alreadyPresent)
			out.push_back(nodeId);
	}

	Common::HashMap<int, Common::Array<int> >::const_iterator it = _tree.find(nodeId);
	if (it == _tree.end())
		return;

	for (Common::Array<int>::const_iterator child = it->_value.begin(); child != it->_value.end(); ++child)
		collectLeafRecords(*child, out);
}

int Egypt_Documentation::findThemeIndexForRecord(int recordId) const {
	for (int themeIndex = 0; themeIndex < kEgyptDocumentationThemeCount; ++themeIndex) {
		Common::Array<int> themeRecords;
		collectLeafRecords(kEgyptDocumentationThemeIds[themeIndex], themeRecords);
		for (Common::Array<int>::const_iterator it = themeRecords.begin(); it != themeRecords.end(); ++it) {
			if (*it == recordId)
				return themeIndex;
		}
	}

	return -1;
}

static int findDocumentationRecordIndex(const Common::Array<int> &recordIds, int recordId) {
	for (uint i = 0; i < recordIds.size(); ++i) {
		if (recordIds[i] == recordId)
			return (int)i;
	}

	return -1;
}

bool Egypt_Documentation::loadData() {
	if (_dataLoaded)
		return true;

	_records.clear();
	_tree.clear();

	Common::File docFile;
	if (!docFile.open(_engine->getFilePath(kFileTypeDocRecords))) {
		warning("Egypt: failed to open REF/FR/ESPDOC.TXT");
		return false;
	}

	Common::Array<Common::String> lines;
	while (!docFile.eos())
		lines.push_back(docFile.readLine());

	for (uint i = 0; i < lines.size(); ++i) {
		Common::String line = lines[i];
		line.trim();

		int recordId = -1;
		Common::String title;
		if (!parseDocumentationHeader(line, recordId, title))
			continue;

		EgyptDocumentationRecord record;
		record.id = recordId;
		record.title = sanitizeDocumentationText(title);

		int runLinkCounter = 0;
		for (++i; i < lines.size(); ++i) {
			Common::String current = lines[i];
			current.trim();

			int nextId = -1;
			Common::String nextTitle;
			if (parseDocumentationHeader(current, nextId, nextTitle)) {
				--i;
				break;
			}

			if (current.empty()) {
				if (!record.body.empty() && !record.body.hasSuffix("\n\n"))
					record.body += "\n";
				if (!record.bodyRuns.empty()) {
					EgyptDocTextRun sep; sep.text = "\n"; sep.linkIndex = -1;
					record.bodyRuns.push_back(sep);
				}
				continue;
			}

			if (current.hasPrefix("<")) {
				const int commaPos = current.find(',');
				const int endPos = current.find('>');
				if (commaPos > 1) {
					record.assetName = current.substr(1, commaPos - 1);
					if (endPos > commaPos + 1) {
						Common::String cap = current.substr(commaPos + 1, endPos - commaPos - 1);
						// Strip '$' (used as prefix for '(c)' copyright marker)
						Common::String cleanCap;
						for (uint ci = 0; ci < cap.size(); ++ci) {
							if (cap[ci] != '$') cleanCap += cap[ci];
						}
						record.assetCaption = cleanCap;
						record.assetCaption.trim();
					}
				} else if (endPos > 1) {
					record.assetName = current.substr(1, endPos - 1);
				}
				record.assetName.trim();
				continue;
			}

			if (current.hasPrefix("/") && current.hasSuffix("//")) {
				Common::String linkId = current.substr(1, current.size() - 3);
				linkId.trim();
				int relatedId = -1;
				if (parseIntegerToken(linkId, relatedId))
					record.links.push_back(relatedId);
				continue;
			}

			if (current.hasPrefix("@")) {
				// Photo annotation "@ type x y text" (EXE elements 0x45eca0)
				EgyptDocAnnotation annotation;
				if (parseDocumentationAnnotation(current, annotation))
					record.annotations.push_back(annotation);
				continue;
			}

			// Build bodyRuns from raw line (before sanitize strips '#')
			if (!record.bodyRuns.empty()) {
				EgyptDocTextRun sep; sep.text = "\n"; sep.linkIndex = -1;
				record.bodyRuns.push_back(sep);
			}
			parseBodyRunLine(current, runLinkCounter, record.bodyRuns);

			current = sanitizeDocumentationText(current);
			if (!record.body.empty() && !record.body.hasSuffix("\n"))
				record.body += "\n";
			record.body += current;
		}

		record.body.trim();
		_records.push_back(record);
	}

	Common::File treeFile;
	if (!treeFile.open(_engine->getFilePath(kFileTypeDocTree))) {
		warning("Egypt: failed to open REF/FR/ESPARBO.TXT");
		return false;
	}

	while (!treeFile.eos()) {
		Common::String line = treeFile.readLine();
		line.trim();
		if (line.empty())
			continue;

		const int colonPos = line.find(':');
		if (colonPos <= 0)
			continue;

		Common::String parentToken = line.substr(0, colonPos);
		parentToken.trim();

		int parentId = -1;
		if (!parseIntegerToken(parentToken, parentId))
			continue;

		Common::String childrenSpec = line.substr(colonPos + 1);
		childrenSpec.trim();
		Common::StringTokenizer tokenizer(childrenSpec);
		Common::Array<int> children;
		while (!tokenizer.empty()) {
			int childId = -1;
			if (parseIntegerToken(tokenizer.nextToken(), childId))
				children.push_back(childId);
		}

		_tree[parentId] = children;
	}

	_dataLoaded = true;
	debugC(kDebugFile, "Egypt: loaded %u documentation record(s) and %u documentation tree node(s)",
	        _records.size(), _tree.size());
	return true;
}

bool Egypt_Documentation::isDocumentationZone(const EgyptZone &zone) const {
	if (zone.actionId != 6)
		return false;

	if (zone.label.hasPrefixIgnoreCase("MSG"))
		return true;

	return zone.label.equalsIgnoreCase("DOC") ||
	       zone.label.equalsIgnoreCase("BDOC") ||
	       zone.label.equalsIgnoreCase("BASEDOC");
}

int Egypt_Documentation::resolveIdForZone(const EgyptZone &zone, Common::String *source) const {
	int documentationId = -1;
	if (parseIntegerToken(zone.extraParam, documentationId)) {
		if (source)
			*source = "zone_extra";
		return documentationId;
	}

	Common::HashMap<Common::String, EgyptMessageEntry, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo>::const_iterator it =
		_engine->_messageLabels.find(zone.label);
	if (it != _engine->_messageLabels.end() && it->_value.documentationId >= 0) {
		if (source)
			*source = "message_suffix";
		return it->_value.documentationId;
	}

	if (source)
		*source = "unresolved";
	return -1;
}

void Egypt_Documentation::displayZone(const EgyptZone &zone) {
	Common::String source;
	const int documentationId = resolveIdForZone(zone, &source);

	debugC(kDebugVariable, "EGYPT_BASEDOC: scene=%s context=%s zone=%03u label=%s docId=%d source=%s",
	        _engine->_currentScene.name.c_str(), _engine->_currentScene.contextName.c_str(), zone.id,
	        zone.label.c_str(), documentationId, source.c_str());

	if (documentationId >= 0) {
		displayRecord(documentationId, false);
		return;
	}

	// Fallback: no documentation ID could be resolved; simple text screen
	// (no EXE equivalent - the original never reaches this state)
	const Common::String title = _engine->resolveMessageLabel(zone.label);
	Graphics::ManagedSurface surface(kScreenWidth, kScreenHeight, g_system->getScreenFormat());
	surface.clear(surface.format.RGBToColor(0, 0, 0));

	Egypt_FontManager &fm = _engine->_fontManager;
	const uint32 white  = surface.format.RGBToColor(255, 255, 255);
	const uint32 orange = surface.format.RGBToColor(224, 112, 0);

	fm.setCurrentFont(kSlotThemeLabel);
	fm.setForeColor(orange);
	const Common::String header = title.empty() ? "Base documentaire" : title;
	fm.displayStr(surface, (kScreenWidth - (int)fm.getStrWidth(header)) / 2, 186, header);
	fm.setCurrentFont(kSlotAlphaIndex);
	fm.setForeColor(white);
	const Common::String info = "Aucune fiche resolue pour cette zone";
	fm.displayStr(surface, (kScreenWidth - (int)fm.getStrWidth(info)) / 2, 244, info);

	g_system->copyRectToScreen(surface.getPixels(), surface.pitch, 0, 0, surface.w, surface.h);
	g_system->updateScreen();

	_engine->showMouse(true);
	_engine->setInterfaceCursor(kEgyptCursorDefault);
	_engine->clearKeys();
	_engine->waitMouseRelease();

	while (!_engine->shouldAbort()) {
		_engine->pollEvents();
		const Common::KeyCode keycode = _engine->getNextKey().keycode;
		if (_engine->getCurrentMouseButton() == 1 || keycode != Common::KEYCODE_INVALID)
			break;
		g_system->delayMillis(10);
	}

	_engine->clearKeys();
	_engine->waitMouseRelease();
}

// Fiche page draw, EXE 0x802b80. Fully redraws the screen and refreshes
// state.linkHits/state.hoveredLink.
void Egypt_Documentation::drawRecordPage(ViewerState &state, const Common::Point &mousePos) {
	const EgyptDocumentationRecord *record = state.record;
	Egypt_FontManager &fm = _engine->_fontManager;

	Graphics::ManagedSurface rs(kScreenWidth, kScreenHeight, g_system->getScreenFormat());
	if (state.hasBackground)
		rs.blitFrom(state.background);
	else
		rs.clear(rs.format.RGBToColor(0, 0, 0));

	const uint32 white  = rs.format.RGBToColor(255, 255, 255); // 0x4d9974
	const uint32 orange = rs.format.RGBToColor(224, 112, 0);   // 0x4d6070
	const uint32 black  = rs.format.RGBToColor(0, 0, 0);       // accent 0x4d6280
	const uint32 shadowGray = rs.format.RGBToColor(198, 195, 198); // RGB565 0xC618

	// --- Photo (native size, EXE never scales) and its position ---
	Graphics::ManagedSurface photo;
	bool hasPhoto = false;
	const Common::Path assetPath = documentationAssetPathFromName(record->assetName);
	if (!assetPath.empty())
		hasPhoto = loadTgaImage(assetPath, photo, false) && photo.w > 0 && photo.h > 0;

	const bool hasCaption = !record->assetCaption.empty();

	Common::Array<BlockWord> captionWords;
	int captionH = 0;
	if (hasCaption) {
		splitPlainTextIntoWords(record->assetCaption, captionWords);
		captionH = measureTextBlock(fm, kSlotFicheCaption, captionWords, kFicheCaptionWidth);
	}

	// EXE 0x802c88..0x802cf1: photo centered in the 52..383(+caption) band;
	// without any caption it is also centered horizontally
	int photoX = kFichePhotoX, photoY = 2;
	if (hasPhoto) {
		const int totalH = photo.h + captionH;
		if (hasCaption) {
			photoX = kFichePhotoX;
			photoY = kFichePhotoAreaY + (kFichePhotoAreaH - totalH) / 2;
		} else {
			photoX = (kScreenWidth - photo.w) / 2;
			photoY = kFichePhotoAreaY + (kFichePhotoAreaHNoCap - totalH) / 2;
		}
		if (photoY + totalH > kFichePhotoBottom)
			photoY = kFichePhotoPushUp - totalH;
		if (photoY < 2)
			photoY = 0;

		Common::Rect src(0, 0, photo.w, photo.h);
		Common::Rect dst(photoX, photoY, photoX + photo.w, photoY + photo.h);
		dst.clip(Common::Rect(0, 0, rs.w, rs.h));
		src.setWidth(dst.width());
		src.setHeight(dst.height());
		rs.blitFrom(photo, src, dst);
	}

	// --- Photo annotations ("@" lines, EXE loop 0x802d72) ---
	for (uint ai = 0; ai < record->annotations.size(); ++ai) {
		const EgyptDocAnnotation &a = record->annotations[ai];
		const uint slot = kSlotAnnotation[a.type];
		fm.setCurrentFont(slot);
		const int ax = photoX + a.x;
		const int ay = photoY + a.y;
		if (a.type == 2) {
			// Boxed label: black box, white text (EXE 0x802de9/0x802f49)
			const int tw = (int)fm.getStrWidth(a.text);
			const int th = fm.getFontHeight();
			Common::Rect box(ax - 2, ay - 2, ax + tw + 2, ay + th + 2);
			box.clip(Common::Rect(0, 0, rs.w, rs.h));
			rs.fillRect(box, black);
			fm.setForeColor(white);
			fm.displayStr(rs, ax, ay, a.text);
		} else {
			// Gray shadow at (+1,+1), black text (EXE 0x803003)
			fm.setForeColor(shadowGray);
			fm.displayStr(rs, ax + 1, ay + 1, a.text);
			fm.setForeColor(black);
			fm.displayStr(rs, ax, ay, a.text);
		}
	}

	// --- Title: font 3, white with black shadow at +1,+1 (EXE 0x8030f6) ---
	// Width shrinks to photoX-125 when the photo top is above y=47
	const int titleWidth = (hasPhoto && photoY < 47) ? (photoX - 125) : kFicheTitleWidth;
	Common::Array<BlockWord> titleWords;
	splitPlainTextIntoWords(record->title, titleWords);
	drawTextBlock(fm, &rs, kSlotFicheTitle, titleWords,
	              kFicheTitleX + 1, kFicheTitleY + 1, titleWidth, false, black);
	const int titleH = drawTextBlock(fm, &rs, kSlotFicheTitle, titleWords,
	                                 kFicheTitleX, kFicheTitleY, titleWidth, false, white);

	// --- Caption: font 8, justified, white, under the photo (EXE 0x803156) ---
	if (hasCaption) {
		drawTextBlock(fm, &rs, kSlotFicheCaption, captionWords,
		              photoX, photoY + (hasPhoto ? photo.h : 0) + kFicheCaptionGap,
		              kFicheCaptionWidth, true, white);
	}

	// --- Body: justified; narrow font 1 column next to a photo/caption,
	// full width font 7 otherwise (EXE 0x80316a..0x8031e2) ---
	state.linkHits.clear();
	Common::Array<BlockWord> bodyWords;
	splitRunsIntoWords(record->bodyRuns, bodyWords);
	if (!bodyWords.empty()) {
		const bool narrow = hasPhoto || hasCaption;
		const uint bodySlot = narrow ? kSlotFicheBody : kSlotFicheWide;
		const int bodyWidth = narrow ? kFicheBodyWidth : kFicheBodyWideWidth;
		int bodyY = kFicheBodyY;
		if (narrow) {
			bodyY = MAX(photoY, titleH + kFicheBodyTitleGap);
			const int bodyH = measureTextBlock(fm, bodySlot, bodyWords, bodyWidth);
			if (bodyY + bodyH > kFicheBodyBottom)
				bodyY = kFicheBodyY;
		}

		// In-game (EXE bit15 set) the links stay white and inactive
		Common::Array<BlockLinkRect> hits;
		drawTextBlock(fm, &rs, bodySlot, bodyWords, kFicheBodyX, bodyY, bodyWidth, true,
		              white, state.standalone ? orange : white,
		              state.standalone ? &hits : nullptr);
		for (uint hi = 0; hi < hits.size(); ++hi) {
			DocLinkHit hit;
			hit.rect = hits[hi].rect;
			hit.linkIndex = hits[hi].linkIndex;
			state.linkHits.push_back(hit);
		}
	}

	// --- Hover: link cursor + target title preview at (94,398) font 1
	// orange (EXE 0x8032d0) ---
	state.hoveredLink = -1;
	for (uint hi = 0; hi < state.linkHits.size(); ++hi) {
		if (state.linkHits[hi].rect.contains(mousePos)) {
			state.hoveredLink = (int)hi;
			break;
		}
	}
	_engine->setInterfaceCursor(state.hoveredLink >= 0 ? kEgyptCursorWarpLabel
	                                                   : kEgyptCursorDefault);
	if (state.hoveredLink >= 0) {
		const int linkIdx = state.linkHits[state.hoveredLink].linkIndex;
		if (linkIdx < (int)record->links.size()) {
			const EgyptDocumentationRecord *target = findRecord(record->links[linkIdx]);
			if (target && !target->title.empty()) {
				Common::Array<BlockWord> previewWords;
				splitPlainTextIntoWords(target->title, previewWords);
				drawTextBlock(fm, &rs, kSlotFicheBody, previewWords,
				              kFichePreviewX, kFichePreviewY, kFichePreviewWidth,
				              false, orange);
			}
		}
	}

	// --- Navigation sprites (standalone only, EXE 0x8021eb skips them
	// when bit15 is set) ---
	if (state.standalone) {
		const bool canPrev = state.currentRecordIndex > 0;
		const bool canNext = state.currentRecordIndex + 1 < (int)state.themeRecords.size();
		if (canPrev) {
			const uint id = spriteHitRect(_engine->_spriteLoader.interfaceSprite(kSpritePrevNormal),
			                              kArrowPrevX, kArrowY).contains(mousePos)
			                    ? kSpritePrevHover : kSpritePrevNormal;
			blitDocSprite(_engine->_spriteLoader.interfaceSprite(id), rs, kArrowPrevX, kArrowY);
		}
		if (canNext) {
			const uint id = spriteHitRect(_engine->_spriteLoader.interfaceSprite(kSpriteNextNormal),
			                              kArrowNextX, kArrowY).contains(mousePos)
			                    ? kSpriteNextHover : kSpriteNextNormal;
			blitDocSprite(_engine->_spriteLoader.interfaceSprite(id), rs, kArrowNextX, kArrowY);
		}
	}

	// --- Exit spiral (always, EXE 0x801f14/0x802467) ---
	blitDocSprite(_engine->_spriteLoader.interfaceSprite(kSpriteExitSpiral), rs,
	              kExitSpiralX, kExitSpiralY);

	g_system->copyRectToScreen(rs.getPixels(), rs.pitch, 0, 0, rs.w, rs.h);
	g_system->updateScreen();
}

bool Egypt_Documentation::openRecordById(ViewerState &state, int recordId) {
	const int theme = findThemeIndexForRecord(recordId);
	if (theme >= 0) {
		Common::Array<int> themeRecords;
		collectLeafRecords(kEgyptDocumentationThemeIds[theme], themeRecords);
		const int idx = findDocumentationRecordIndex(themeRecords, recordId);
		if (idx >= 0) {
			state.themeRecords = themeRecords;
			state.currentRecordIndex = idx;
			return true;
		}
	}
	if (findRecord(recordId)) {
		state.themeRecords.clear();
		state.themeRecords.push_back(recordId);
		state.currentRecordIndex = 0;
		return true;
	}
	return false;
}

// Handles keys and clicks for the record viewer (EXE state 0xb handler
// 0x8021d4). Returns true when the current record must be reloaded.
bool Egypt_Documentation::handleRecordEvents(ViewerState &state, bool &exitViewer, bool &redraw) {
	const Common::KeyCode keycode = _engine->getNextKey().keycode;
	if (keycode == Common::KEYCODE_ESCAPE) {
		exitViewer = true;
		return false;
	}

	const bool canPrev = state.currentRecordIndex > 0;
	const bool canNext = state.currentRecordIndex + 1 < (int)state.themeRecords.size();

	if (state.standalone) {
		// EXE keys 0x25/0x27 (VK_LEFT/VK_RIGHT)
		if (keycode == Common::KEYCODE_LEFT && canPrev) {
			--state.currentRecordIndex;
			return true;
		}
		if (keycode == Common::KEYCODE_RIGHT && canNext) {
			++state.currentRecordIndex;
			return true;
		}
	}

	if (_engine->getCurrentMouseButton() != 1)
		return false;

	const Common::Point mouse = _engine->getMousePos();
	_engine->waitMouseRelease();

	// Exit spiral at (0,460)
	if (spriteHitRect(_engine->_spriteLoader.interfaceSprite(kSpriteExitSpiral),
	                  kExitSpiralX, kExitSpiralY).contains(mouse)) {
		exitViewer = true;
		return false;
	}

	if (!state.standalone)
		return false; // EXE bit15: nothing else reacts in-game

	// Hyperlink click -> jump to the target fiche
	if (state.hoveredLink >= 0) {
		const int linkIdx = state.linkHits[state.hoveredLink].linkIndex;
		if (linkIdx < (int)state.record->links.size() &&
		        openRecordById(state, state.record->links[linkIdx]))
			return true;
	}

	// Prev/next arrows at y=455
	if (canPrev && spriteHitRect(_engine->_spriteLoader.interfaceSprite(kSpritePrevNormal),
	                             kArrowPrevX, kArrowY).contains(mouse)) {
		--state.currentRecordIndex;
		return true;
	}
	if (canNext && spriteHitRect(_engine->_spriteLoader.interfaceSprite(kSpriteNextNormal),
	                             kArrowNextX, kArrowY).contains(mouse)) {
		++state.currentRecordIndex;
		return true;
	}

	redraw = true;
	return false;
}

void Egypt_Documentation::displayRecord(int docId, bool standalone) {
	if (!loadData()) {
		warning("Egypt: displayRecord(%d): documentation data could not be loaded", docId);
		return;
	}

	if (!_engine->_fontManager.fontsLoaded()) {
		warning("Egypt: displayRecord(%d): CRYO fonts not loaded", docId);
		return;
	}

	// Records 0xd3/0xd4 get the special chronology page instead of a
	// fiche (EXE state 0xa handler switches to state 0xf for these ids,
	// dispatcher check 0x8019c2 on 0x45e1a0)
	if (docId == kChronoRecordGeneral || docId == kChronoRecordRamses) {
		runChronologyPage(docId, standalone);
		return;
	}

	ViewerState state;
	state.standalone = standalone;
	if (!openRecordById(state, docId)) {
		warning("Egypt: displayRecord(%d): record not found in %u records", docId, _records.size());
		return;
	}

	_engine->showMouse(true);
	_engine->setInterfaceCursor(kEgyptCursorDefault);
	_engine->clearKeys();
	_engine->waitMouseRelease();

	bool exitViewer = false;

	while (!_engine->shouldAbort() && !exitViewer) {
		state.record = findRecord(state.themeRecords[state.currentRecordIndex]);
		if (!state.record)
			break;

		// Background from the record's theme (EXE table 0x435048)
		int theme = findThemeIndexForRecord(state.record->id);
		if (theme < 0)
			theme = kEgyptDocumentationThemeCount - 1;
		if (theme != state.loadedBgTheme) {
			state.background = Graphics::ManagedSurface();
			state.hasBackground = loadTgaImage(
			    _engine->getFilePath(kFileTypeSpriteImage, kEgyptDocumentationThemeBackgrounds[theme]),
			    state.background, true);
			state.loadedBgTheme = theme;
		}

		Common::Point lastMousePos(-1, -1);
		bool redraw = true;
		bool reloadRecord = false;

		while (!_engine->shouldAbort() && !exitViewer && !reloadRecord) {
			const Common::Point mousePos = _engine->getMousePos();
			if (redraw || mousePos != lastMousePos) {
				drawRecordPage(state, mousePos);
				lastMousePos = mousePos;
				redraw = false;
			}

			_engine->pollEvents();
			reloadRecord = handleRecordEvents(state, exitViewer, redraw);
			if (!exitViewer && !reloadRecord)
				g_system->delayMillis(10);
		}
	}

	_engine->clearKeys();
	_engine->waitMouseRelease();
}

// Loads REF/FR/EspIndex.txt once (EXE parser 0x806190, entries 0x469f50
// stride 68): ";X" lines are letter headers (id 0 in the EXE, never
// clickable, 0x806160 skips them), "Name//recordId" lines are clickable.
bool Egypt_Documentation::loadAlphaIndex() {
	if (_indexLoaded)
		return !_indexEntries.empty();
	_indexLoaded = true;

	Common::File file;
	if (!file.open(_engine->getFilePath(kFileTypeDocIndex))) {
		warning("Egypt: failed to open REF/FR/EspIndex.txt");
		return false;
	}

	while (!file.eos() && _indexEntries.size() < kAlphaMaxEntries) {
		Common::String line = file.readLine();
		line.trim();
		if (line.empty())
			continue;

		AlphaIndexEntry entry;
		if (line[0] == ';') {
			entry.name = line.substr(1);
			entry.name.trim();
			entry.id = -1;
		} else {
			// "Name//recordId"
			const int slashPos = line.find('/');
			if (slashPos <= 0 || (uint)slashPos + 1 >= line.size() || line[slashPos + 1] != '/')
				continue;
			entry.name = line.substr(0, slashPos);
			entry.name.trim();
			Common::String idToken = line.substr(slashPos + 2);
			idToken.trim();
			if (!parseIntegerToken(idToken, entry.id) || entry.id <= 0)
				continue;
		}
		if (!entry.name.empty())
			_indexEntries.push_back(entry);
	}

	return !_indexEntries.empty();
}

// Alphabetical index overlay (EXE 0x802520 + list builder 0x803580):
// right-aligned column, font 10, rows of 16px ending at y=430, up to 24
// visible rows, "..." hover-dwell rows and arrow keys to scroll.
// Returns the clicked record id, or -1 when dismissed.
int Egypt_Documentation::runAlphabeticalIndex(const Graphics::ManagedSurface &background) {
	Egypt_FontManager &fm = _engine->_fontManager;

	if (!loadAlphaIndex())
		return -1;

	fm.setCurrentFont(kSlotAlphaIndex);
	uint maxWidth = 0;
	for (uint i = 0; i < _indexEntries.size(); ++i)
		maxWidth = MAX(maxWidth, fm.getStrWidth(_indexEntries[i].name));

	const int count = (int)_indexEntries.size();
	const int visible = MIN(count, kAlphaMaxVisible);
	const int topY = kAlphaBottomY - visible * kAlphaRowH;
	const int textX = kScreenWidth - (int)maxWidth;
	const Common::String dots = "..."; // EXE string VA 0x4350f8

	int scrollOffset = 0; // EXE global 0x45f624
	int dwellTicks = 0;   // EXE tick counter 0x81a7b0
	const int fontH = fm.getFontHeight();
	const int rowTextYOff = (kAlphaRowH - fontH) / 2;

	Graphics::ManagedSurface rs(kScreenWidth, kScreenHeight, g_system->getScreenFormat());
	const uint32 white  = rs.format.RGBToColor(255, 255, 255);
	const uint32 orange = rs.format.RGBToColor(224, 112, 0);
	// Backing panel 0x817680 with bevel params at VA 0x4352fc (blue tones);
	// approximated as a dark blue box until those params are decoded
	const uint32 panelFill  = rs.format.RGBToColor(0, 0, 64);
	const uint32 panelFrame = rs.format.RGBToColor(0, 0, 160);

	_engine->clearKeys();
	_engine->waitMouseRelease();

	while (!_engine->shouldAbort()) {
		const Common::Point mouse = _engine->getMousePos();

		const bool dotsTop = scrollOffset > 0;
		const int firstRow = dotsTop ? scrollOffset + 1 : scrollOffset;
		int textRows = visible - (dotsTop ? 1 : 0);
		const bool dotsBottom = count > kAlphaMaxVisible && firstRow + textRows < count;
		if (dotsBottom)
			--textRows;

		rs.blitFrom(background);
		// Panel (EXE: x=634-maxW, y=427-16*visible, w=maxW+6, h=16*visible+4)
		Common::Rect panel(kScreenWidth - 6 - (int)maxWidth, topY - 3,
		                   kScreenWidth, topY + visible * kAlphaRowH + 1);
		panel.clip(Common::Rect(0, 0, rs.w, rs.h));
		rs.fillRect(panel, panelFill);
		rs.frameRect(panel, panelFrame);

		bool hoverDotsTop = false, hoverDotsBottom = false;
		int hoveredRow = -1;

		int rowY = topY;
		if (dotsTop) {
			const Common::Rect r(textX, rowY, kScreenWidth, rowY + kAlphaHitH);
			hoverDotsTop = r.contains(mouse);
			fm.setForeColor(white);
			fm.displayStr(rs, textX, rowY + rowTextYOff, dots);
			rowY += kAlphaRowH;
		}
		for (int i = 0; i < textRows && firstRow + i < count; ++i, rowY += kAlphaRowH) {
			const AlphaIndexEntry &entry = _indexEntries[firstRow + i];
			// Letter header rows stay white and are never clickable
			// (EXE 0x806160 skips entries with record id 0)
			const Common::Rect r(textX, rowY, kScreenWidth, rowY + kAlphaHitH);
			const bool hovered = entry.id > 0 && r.contains(mouse);
			if (hovered)
				hoveredRow = firstRow + i;
			fm.setForeColor(hovered ? orange : white);
			fm.displayStr(rs, textX, rowY + rowTextYOff, entry.name);
		}
		if (dotsBottom) {
			const Common::Rect r(textX, rowY, kScreenWidth, rowY + kAlphaHitH);
			hoverDotsBottom = r.contains(mouse);
			fm.setForeColor(white);
			fm.displayStr(rs, textX, rowY + rowTextYOff, dots);
		}

		g_system->copyRectToScreen(rs.getPixels(), rs.pitch, 0, 0, rs.w, rs.h);
		g_system->updateScreen();
		g_system->delayMillis(10);
		_engine->pollEvents();

		const int maxOffset = MAX(0, count - kAlphaMaxVisible);

		// Hover-dwell scrolling like the EXE (ticks 0x81a7b0 >= 10)
		if (hoverDotsTop || hoverDotsBottom) {
			if (++dwellTicks >= kAlphaDwellTicks) {
				dwellTicks = 0;
				scrollOffset = CLIP(scrollOffset + (hoverDotsBottom ? 1 : -1), 0, maxOffset);
			}
		} else {
			dwellTicks = 0;
		}

		// EXE keys: UP/DOWN one line, PGUP/PGDN 22 lines, HOME/END
		const Common::KeyCode keycode = _engine->getNextKey().keycode;
		if (keycode == Common::KEYCODE_ESCAPE)
			return -1;
		else if (keycode == Common::KEYCODE_DOWN)
			scrollOffset = MIN(scrollOffset + 1, maxOffset);
		else if (keycode == Common::KEYCODE_UP)
			scrollOffset = MAX(scrollOffset - 1, 0);
		else if (keycode == Common::KEYCODE_PAGEDOWN)
			scrollOffset = MIN(scrollOffset + kAlphaPageStep, maxOffset);
		else if (keycode == Common::KEYCODE_PAGEUP)
			scrollOffset = MAX(scrollOffset - kAlphaPageStep, 0);
		else if (keycode == Common::KEYCODE_HOME)
			scrollOffset = 0;
		else if (keycode == Common::KEYCODE_END)
			scrollOffset = maxOffset;

		if (_engine->getCurrentMouseButton() == 1) {
			_engine->waitMouseRelease();
			if (hoveredRow >= 0)
				return _indexEntries[hoveredRow].id;
			if (!hoverDotsTop && !hoverDotsBottom)
				return -1; // click elsewhere closes the overlay
		}
	}

	return -1;
}

// Chronology page, EXE state 0xf (handler 0x8019bd..0x8021d3), reached
// when the fiche to open is record 211 (0xd3, general chronology) or 212
// (0xd4, reign of Ramses III). The left column lists the child records:
// period title plus date (= first body line). Hovering a row that has
// more body text selects it (EXE 0x45d158, selection persists) and the
// remaining body lines are drawn in a detail pane on the right. Links in
// the pane are active in standalone mode only (bit15 test 0x801e5c).
void Egypt_Documentation::runChronologyPage(int recordId, bool standalone) {
	Egypt_FontManager &fm = _engine->_fontManager;
	const bool general = (recordId == kChronoRecordGeneral);

	const EgyptDocumentationRecord *pageRecord = findRecord(recordId);
	Common::Array<int> childIds;
	Common::HashMap<int, Common::Array<int> >::const_iterator treeIt = _tree.find(recordId);
	if (treeIt != _tree.end())
		childIds = treeIt->_value;
	if (!pageRecord || childIds.empty()) {
		warning("Egypt: chronology record %d has no children", recordId);
		return;
	}

	// Row data: date = first body line, detail = the remaining lines
	// (extractor 0x803e80 splits the record body the same way)
	struct ChronoRow {
		const EgyptDocumentationRecord *record = nullptr;
		Common::String date;
		Common::Array<BlockWord> detailWords;
		int width = 0; // hit width from x=30 (0x801b42)
	};
	Common::Array<ChronoRow> rows;
	for (uint i = 0; i < childIds.size(); ++i) {
		const EgyptDocumentationRecord *rec = findRecord(childIds[i]);
		if (!rec)
			continue;
		ChronoRow row;
		row.record = rec;
		const int nlPos = rec->body.find('\n');
		row.date = (nlPos > 0) ? rec->body.substr(0, nlPos) : rec->body;
		row.date.trim();
		Common::Array<BlockWord> allWords;
		splitRunsIntoWords(rec->bodyRuns, allWords);
		uint firstDetail = allWords.size();
		for (uint w = 1; w < allWords.size(); ++w) {
			if (allWords[w].paraBreak) {
				firstDetail = w;
				break;
			}
		}
		for (uint w = firstDetail; w < allWords.size(); ++w)
			row.detailWords.push_back(allWords[w]);
		if (!row.detailWords.empty())
			row.detailWords[0].paraBreak = false;
		rows.push_back(row);
	}
	if (rows.empty())
		return;

	const uint titleSlot  = general ? kSlotChronoTitleGen : kSlotChronoTitleRam;
	const uint detailSlot = general ? kSlotChronoDate : kSlotChronoDetailRam;
	fm.setCurrentFont(titleSlot);
	const int titleFontH = fm.getFontHeight();
	fm.setCurrentFont(kSlotChronoDate);
	const int dateFontH = fm.getFontHeight();
	fm.setCurrentFont(detailSlot);
	const int detailFontH = fm.getFontHeight();

	// Row height (0x801a3d..0x801a60): rowH = 355/n; below the minimum
	// (2*titleFontH for 211 where title and date stack, titleFontH for
	// 212) use the minimum, else average with it
	const int minRow = general ? 2 * titleFontH : titleFontH;
	const int n = (int)rows.size();
	int rowH = kChronoAvailH / n;
	if (rowH < minRow)
		rowH = minRow;
	else
		rowH -= (rowH - minRow) / 2;
	const int startY = (kChronoAvailH - rowH * n) / 2 + kChronoTopY; // 0x801a70

	// Max title width with the title font (loop 0x801a90)
	fm.setCurrentFont(titleSlot);
	int maxTitleW = 0;
	for (int i = 0; i < n; ++i)
		maxTitleW = MAX(maxTitleW, (int)fm.getStrWidth(rows[i].record->title));

	// Per-row hit width (0x801b27: 211 = max(titleW, dateW+20), 212 =
	// maxTitleW+20+dateW) and the widest row of the page
	int maxRowW = 0;
	for (int i = 0; i < n; ++i) {
		fm.setCurrentFont(titleSlot);
		const int titleW = (int)fm.getStrWidth(rows[i].record->title);
		fm.setCurrentFont(kSlotChronoDate);
		const int dateW = (int)fm.getStrWidth(rows[i].date);
		rows[i].width = general ? MAX(titleW, dateW + kChronoDateGap)
		                        : maxTitleW + kChronoDateGap + dateW;
		maxRowW = MAX(maxRowW, rows[i].width);
	}
	// Detail pane left edge is an absolute x (0x801c79; the background
	// restore loop at 0x801c97 uses it as a screen column)
	const int detailX = maxRowW + kChronoDetailGap;
	const int detailWidth =
	    (general ? kChronoDetailRightGen : kChronoDetailRightRam) - detailX;

	// Background from the record's theme, like a fiche page
	int theme = findThemeIndexForRecord(recordId);
	if (theme < 0)
		theme = 1; // Le temps
	Graphics::ManagedSurface background;
	const bool hasBackground = loadTgaImage(
	    _engine->getFilePath(kFileTypeSpriteImage, kEgyptDocumentationThemeBackgrounds[theme]),
	    background, true);

	_engine->showMouse(true);
	_engine->setInterfaceCursor(kEgyptCursorDefault);
	_engine->clearKeys();
	_engine->waitMouseRelease();

	int selectedRow = -1; // EXE 0x45d158
	Common::Array<BlockLinkRect> linkHits;

	while (!_engine->shouldAbort()) {
		const Common::Point mouse = _engine->getMousePos();

		// Hovering a row with detail text selects it (0x801b76/0x801ba5)
		for (int i = 0; i < n; ++i) {
			const int y = startY + i * rowH;
			const Common::Rect hit(kChronoListX, y,
			                       kChronoListX + rows[i].width, y + minRow - 1);
			if (hit.contains(mouse) && !rows[i].detailWords.empty()) {
				selectedRow = i;
				break;
			}
		}

		Graphics::ManagedSurface rs(kScreenWidth, kScreenHeight, g_system->getScreenFormat());
		if (hasBackground)
			rs.blitFrom(background);
		else
			rs.clear(rs.format.RGBToColor(0, 0, 0));

		const uint32 white  = rs.format.RGBToColor(255, 255, 255); // 0x4d9974
		const uint32 orange = rs.format.RGBToColor(224, 112, 0);   // 0x4d6070
		const uint32 black  = rs.format.RGBToColor(0, 0, 0);

		// Page title like the fiche title (font 3 with black shadow)
		Common::Array<BlockWord> titleWords;
		splitPlainTextIntoWords(pageRecord->title, titleWords);
		drawTextBlock(fm, &rs, kSlotFicheTitle, titleWords,
		              kFicheTitleX + 1, kFicheTitleY + 1, kFicheTitleWidth, false, black);
		drawTextBlock(fm, &rs, kSlotFicheTitle, titleWords,
		              kFicheTitleX, kFicheTitleY, kFicheTitleWidth, false, white);

		// Left column rows (title 0x801bd4, date 0x801c44)
		for (int i = 0; i < n; ++i) {
			const int y = startY + i * rowH;
			const uint32 color = (i == selectedRow) ? orange : white;
			fm.setCurrentFont(titleSlot);
			fm.setForeColor(color);
			fm.displayStr(rs, kChronoListX, y, rows[i].record->title);
			fm.setCurrentFont(kSlotChronoDate);
			fm.setForeColor(color);
			if (general) {
				// Date under the title (0x801bea: x=50, y+titleFontH)
				fm.displayStr(rs, kChronoDateXGen, y + titleFontH, rows[i].date);
			} else {
				// Date right of the title column, baseline-aligned
				// (0x801c1e: x=maxTitleW+50, y+titleFontH-dateFontH-1)
				fm.displayStr(rs, kChronoListX + maxTitleW + kChronoDateGap,
				              y + titleFontH - dateFontH - 1, rows[i].date);
			}
		}

		// Detail pane for the selected row (0x801cf4..0x801e59): 211
		// justified font 7 paragraphs with 3px gaps (0x803b40), 212
		// left-aligned font 0 (0x803670)
		linkHits.clear();
		if (selectedRow >= 0) {
			const ChronoRow &sel = rows[selectedRow];

			// Split into paragraphs
			Common::Array< Common::Array<BlockWord> > paragraphs;
			paragraphs.push_back(Common::Array<BlockWord>());
			for (uint w = 0; w < sel.detailWords.size(); ++w) {
				if (sel.detailWords[w].paraBreak && !paragraphs.back().empty())
					paragraphs.push_back(Common::Array<BlockWord>());
				BlockWord word = sel.detailWords[w];
				word.paraBreak = false;
				paragraphs.back().push_back(word);
			}

			const int paraGap = general ? kChronoParaGap : 0;
			int totalH = 0;
			for (uint p = 0; p < paragraphs.size(); ++p) {
				if (p > 0)
					totalH += paraGap;
				totalH += measureTextBlock(fm, detailSlot, paragraphs[p], detailWidth);
			}

			// y = 90 + rowH*selected + titleFontH - detailFontH - 1,
			// pushed up so the block ends above y=421 (0x801db1)
			int y = kChronoDetailBaseY + rowH * selectedRow + titleFontH - detailFontH - 1;
			if (y + totalH > kChronoDetailBottom)
				y = kChronoDetailBottom - totalH;

			for (uint p = 0; p < paragraphs.size(); ++p) {
				const int h = drawTextBlock(fm, &rs, detailSlot, paragraphs[p],
				                            detailX, y, detailWidth, general,
				                            white, standalone ? orange : white,
				                            standalone ? &linkHits : nullptr);
				y += h + paraGap;
			}
		}

		// Link hover -> cursor 11 (0x801ebc: mov 0x45d148,0xb)
		int hoveredLink = -1;
		for (uint hi = 0; hi < linkHits.size(); ++hi) {
			if (linkHits[hi].rect.contains(mouse)) {
				hoveredLink = (int)hi;
				break;
			}
		}
		_engine->setInterfaceCursor(hoveredLink >= 0 ? kEgyptCursorWarpLabel
		                                             : kEgyptCursorDefault);

		// Exit spiral always (0x801f14); index button standalone only
		blitDocSprite(_engine->_spriteLoader.interfaceSprite(kSpriteExitSpiral), rs,
		              kExitSpiralX, kExitSpiralY);
		if (standalone)
			blitDocSprite(_engine->_spriteLoader.interfaceSprite(kSpriteAlphaIndex), rs,
			              kAlphaButtonX, kAlphaButtonY);

		g_system->copyRectToScreen(rs.getPixels(), rs.pitch, 0, 0, rs.w, rs.h);
		g_system->updateScreen();
		g_system->delayMillis(10);
		_engine->pollEvents();

		const Common::KeyCode keycode = _engine->getNextKey().keycode;
		if (keycode == Common::KEYCODE_ESCAPE)
			break;

		if (_engine->getCurrentMouseButton() != 1)
			continue;
		_engine->waitMouseRelease();

		// Exit spiral at (0,460)
		if (spriteHitRect(_engine->_spriteLoader.interfaceSprite(kSpriteExitSpiral),
		                  kExitSpiralX, kExitSpiralY).contains(mouse))
			break;

		if (!standalone)
			continue; // EXE bit15: nothing else reacts in-game

		// Alphabetical index (0x8019cb calls 0x802520 for this state too)
		if (spriteHitRect(_engine->_spriteLoader.interfaceSprite(kSpriteAlphaIndex),
		                  kAlphaButtonX, kAlphaButtonY).contains(mouse)) {
			const int openId = runAlphabeticalIndex(rs);
			if (openId >= 0)
				displayRecord(openId, true);
			continue;
		}

		// Link click in the detail pane -> open the fiche (0x801eca)
		if (hoveredLink >= 0 && selectedRow >= 0) {
			const int linkIdx = linkHits[hoveredLink].linkIndex;
			const EgyptDocumentationRecord *sel = rows[selectedRow].record;
			if (linkIdx >= 0 && linkIdx < (int)sel->links.size())
				displayRecord(sel->links[linkIdx], true);
		}
	}

	_engine->setInterfaceCursor(kEgyptCursorDefault);
	_engine->clearKeys();
	_engine->waitMouseRelease();
}

void Egypt_Documentation::runStandaloneMode() {
	debugC(kDebugVariable, "EGYPT_MENU: selection=Documentation mode=standalone");

	Egypt_FontManager &fm = _engine->_fontManager;

	if (!loadData() || !fm.fontsLoaded()) {
		warning("Egypt: standalone documentation unavailable (data or fonts missing)");
		return;
	}

	Graphics::ManagedSurface summaryBackground;
	const bool hasSummaryBackground =
	    loadTgaImage(_engine->getFilePath(kFileTypeSpriteImage, "SOMMAIRE.TGA"), summaryBackground, true);

	_engine->showMouse(true);
	_engine->setInterfaceCursor(kEgyptCursorDefault);
	_engine->clearKeys();
	_engine->waitMouseRelease();

	// Browser state (EXE states 1/5/6 of machine 0x801200)
	int selectedTheme = -1;
	Common::Array<int> level1Ids;      // titles 0x45d270
	int level1StartY = 0;
	int level1MaxWidth = 0;            // 0x45c1c0
	int selectedLevel1 = -1;           // 0x45de7c
	Common::Array<int> level2Ids;      // titles 0x45c1c8
	int level2StartY = 0;

	bool exitDocumentation = false;
	int openRecordId = -1;

	while (!_engine->shouldAbort() && !exitDocumentation) {
		Graphics::ManagedSurface rs(kScreenWidth, kScreenHeight, g_system->getScreenFormat());
		if (hasSummaryBackground)
			rs.blitFrom(summaryBackground);
		else
			rs.clear(rs.format.RGBToColor(0, 0, 0));

		const uint32 white  = rs.format.RGBToColor(255, 255, 255);
		const uint32 orange = rs.format.RGBToColor(224, 112, 0);

		const Common::Point mouse = _engine->getMousePos();

		// --- Theme labels: font 2, x=100, y table 0x435030 ---
		int hoveredTheme = -1;
		fm.setCurrentFont(kSlotThemeLabel);
		for (int i = 0; i < 5; ++i) {
			const uint w = fm.getStrWidth(kEgyptDocumentationThemeLabels[i]);
			const Common::Rect hit(kThemeLabelX, kThemeLabelY[i],
			                       kThemeLabelX + (int)w, kThemeLabelY[i] + kThemeHitH);
			const bool hovered = hit.contains(mouse);
			if (hovered)
				hoveredTheme = i;
			fm.setForeColor((hovered || i == selectedTheme) ? orange : white);
			fm.displayStr(rs, kThemeLabelX, kThemeLabelY[i], kEgyptDocumentationThemeLabels[i]);
		}

		// --- Level-1 fiche titles: font 7, x=230, step 16 ---
		int hoveredLevel1 = -1;
		fm.setCurrentFont(kSlotFicheList);
		for (uint i = 0; i < level1Ids.size(); ++i) {
			const EgyptDocumentationRecord *rec = findRecord(level1Ids[i]);
			if (!rec)
				continue;
			const int y = level1StartY + (int)i * kListStepY;
			const uint w = fm.getStrWidth(rec->title);
			const Common::Rect hit(kListLevel1X, y, kListLevel1X + (int)w, y + kThemeHitH);
			const bool hovered = hit.contains(mouse);
			if (hovered)
				hoveredLevel1 = (int)i;
			fm.setForeColor((hovered || (int)i == selectedLevel1) ? orange : white);
			fm.displayStr(rs, kListLevel1X, y, rec->title);
		}

		// --- Level-2 fiche titles: x=250+level1MaxWidth, "..."-truncated ---
		int hoveredLevel2 = -1;
		const int level2X = kListLevel2XBase + level1MaxWidth;
		for (uint i = 0; i < level2Ids.size(); ++i) {
			const EgyptDocumentationRecord *rec = findRecord(level2Ids[i]);
			if (!rec)
				continue;
			const int y = level2StartY + (int)i * kListStepY;
			const Common::Rect hit(level2X, y, kListLevel2Right, y + kThemeHitH);
			const bool hovered = hit.contains(mouse);
			if (hovered)
				hoveredLevel2 = (int)i;
			drawEllipsisLine(fm, rs, kSlotFicheList, rec->title, level2X, y,
			                 kListLevel2Right - level2X, hovered ? orange : white);
		}

		// --- Alphabetical index button at (606,450) and exit spiral at (0,460) ---
		blitDocSprite(_engine->_spriteLoader.interfaceSprite(kSpriteAlphaIndex), rs,
		              kAlphaButtonX, kAlphaButtonY);
		blitDocSprite(_engine->_spriteLoader.interfaceSprite(kSpriteExitSpiral), rs,
		              kExitSpiralX, kExitSpiralY);

		g_system->copyRectToScreen(rs.getPixels(), rs.pitch, 0, 0, rs.w, rs.h);
		g_system->updateScreen();
		g_system->delayMillis(10);
		_engine->pollEvents();

		const Common::KeyCode keycode = _engine->getNextKey().keycode;
		if (keycode == Common::KEYCODE_ESCAPE)
			break;

		if (_engine->getCurrentMouseButton() != 1)
			continue;
		_engine->waitMouseRelease();

		// Exit spiral
		if (spriteHitRect(_engine->_spriteLoader.interfaceSprite(kSpriteExitSpiral),
		                  kExitSpiralX, kExitSpiralY).contains(mouse))
			break;

		// Alphabetical index toggle (sprite 0xd2 at (606,450) via TEXN)
		if (spriteHitRect(_engine->_spriteLoader.interfaceSprite(kSpriteAlphaIndex),
		                  kAlphaButtonX, kAlphaButtonY).contains(mouse)) {
			openRecordId = runAlphabeticalIndex(rs);
			if (openRecordId >= 0) {
				displayRecord(openRecordId, true);
				openRecordId = -1;
			}
			continue;
		}

		// Theme label -> load its level-1 list (EXE 0x80187a/0x803480)
		if (hoveredTheme >= 0) {
			selectedTheme = hoveredTheme;
			selectedLevel1 = -1;
			level2Ids.clear();
			level1Ids.clear();
			Common::HashMap<int, Common::Array<int> >::const_iterator it =
			    _tree.find(kEgyptDocumentationThemeIds[selectedTheme]);
			if (it != _tree.end())
				level1Ids = it->_value;
			// startY = themeY+5, bottom-clamped to 470-16*count (EXE 0x8015c8)
			level1StartY = kThemeLabelY[selectedTheme] + kListStartGap;
			if (level1StartY + (int)level1Ids.size() * kListStepY > kListBottomY)
				level1StartY = kListBottomY - (int)level1Ids.size() * kListStepY;
			fm.setCurrentFont(kSlotFicheList);
			level1MaxWidth = 0;
			for (uint i = 0; i < level1Ids.size(); ++i) {
				const EgyptDocumentationRecord *rec = findRecord(level1Ids[i]);
				if (rec)
					level1MaxWidth = MAX(level1MaxWidth, (int)fm.getStrWidth(rec->title));
			}
			continue;
		}

		// Level-1 title: open the sub-list when it has children, else the
		// fiche itself (EXE 0x8018ae)
		if (hoveredLevel1 >= 0) {
			const int nodeId = level1Ids[hoveredLevel1];
			Common::HashMap<int, Common::Array<int> >::const_iterator it = _tree.find(nodeId);
			if (it != _tree.end() && !it->_value.empty()) {
				selectedLevel1 = hoveredLevel1;
				level2Ids = it->_value;
				// startY = selected item's y, bottom-clamped (EXE 0x801628)
				level2StartY = level1StartY + selectedLevel1 * kListStepY;
				if (level2StartY + (int)level2Ids.size() * kListStepY > kListBottomY)
					level2StartY = kListBottomY - (int)level2Ids.size() * kListStepY;
			} else {
				displayRecord(nodeId, true);
			}
			continue;
		}

		// Level-2 title: always opens the fiche (EXE 0x80196c)
		if (hoveredLevel2 >= 0)
			displayRecord(level2Ids[hoveredLevel2], true);
	}

	_engine->clearKeys();
	_engine->waitMouseRelease();
	_engine->showMouse(false);
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
