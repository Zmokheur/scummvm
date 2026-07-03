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

#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/managed_surface.h"

#include "cryomni3d/egypt/documentation.h"
#include "cryomni3d/egypt/engine.h"
#include "cryomni3d/egypt/support/image_loader.h"

namespace CryOmni3D {
namespace Egypt {

namespace {

const int kEgyptDocumentationThemeCount = 6;
const int kEgyptDocumentationThemeIds[kEgyptDocumentationThemeCount] = { 1, 2, 3, 4, 5, 6 };
const char *const kEgyptDocumentationThemeLabels[kEgyptDocumentationThemeCount] = {
	"La terre", "Le temps", "Les hommes", "Le pharaon", "Les dieux", "Personnages"
};
const char *const kEgyptDocumentationThemeBackgrounds[kEgyptDocumentationThemeCount] = {
	"SPRITE/FICHETER.TGA",  // The Earth
	"SPRITE/FICHETEM.TGA",  // Time
	"SPRITE/FICHEHOM.TGA",  // Men
	"SPRITE/FICHEPHA.TGA",  // The Pharaoh
	"SPRITE/FICHEDIE.TGA",  // The Gods
	"SPRITE/FICHEPER.TGA",  // Personnages
};

// Record viewer layout - approximate, to be refined from EXE data.
// Rects built through functions to avoid global constructors.
Common::Rect docTextPanel()   { return Common::Rect( 12,  60, 294, 388); } // body text, left column
Common::Rect docImageRect()   { return Common::Rect(314,  60, 628, 335); } // record photo, right column
Common::Rect docCaptionRect() { return Common::Rect(314, 340, 628, 388); } // photo caption
Common::Rect docPrevButton()  { return Common::Rect( 90, 432, 124, 462); } // previous record
Common::Rect docNextButton()  { return Common::Rect(130, 432, 164, 462); } // next record
Common::Rect docBackButton()  { return Common::Rect(590, 432, 628, 468); } // back / exit viewer

// Viewer palette
struct DocRgb { byte r, g, b; };
const DocRgb kDocTitleColor    = {245, 235, 215};
const DocRgb kDocTextColor     = {242, 232, 210};
const DocRgb kDocLinkColor     = {255, 180,  60};
const DocRgb kDocCaptionColor  = {186, 178, 165};
const DocRgb kDocNavColor      = {200, 190, 175};
const DocRgb kDocNavActive     = {255, 235, 150};
const DocRgb kDocNavDimmed     = { 60,  55,  50};
const DocRgb kDocTooltipBg     = { 20,  18,  14};
const DocRgb kDocTooltipFrame  = {188, 154,  84};
const DocRgb kDocTooltipText   = {244, 232, 204};
const DocRgb kDocFallbackBg    = { 68,  10,  10};

uint32 docColor(const Graphics::PixelFormat &fmt, const DocRgb &c) {
	return fmt.RGBToColor(c.r, c.g, c.b);
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

void drawCenteredLine(Graphics::ManagedSurface &surface, const Graphics::Font *font,
                      const Common::String &text, int y, uint32 color) {
	if (!font)
		return;

	const int x = MAX(0, (surface.w - font->getStringWidth(text)) / 2);
	font->drawString(&surface, text, x, y, surface.w, color);
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

			if (current.hasPrefix("@"))
				continue;

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
		displayRecord(documentationId);
		return;
	}

	// Fallback: no documentation ID could be resolved
	const Common::String title = _engine->resolveMessageLabel(zone.label);
	Graphics::ManagedSurface surface(kScreenWidth, kScreenHeight, g_system->getScreenFormat());
	surface.clear(surface.format.RGBToColor(0, 0, 0));

	const Graphics::Font *titleFont = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	const Graphics::Font *bodyFont  = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!titleFont) titleFont = bodyFont;

	const uint32 borderColor = docColor(surface.format, kDocTooltipFrame);
	const uint32 titleColor  = surface.format.RGBToColor(250, 225, 170);
	const uint32 textColor   = docColor(surface.format, kDocTooltipText);
	const Common::Rect panel(48, 160, 592, 356);
	surface.fillRect(panel, surface.format.RGBToColor(12, 12, 12));
	surface.frameRect(panel, borderColor);
	drawCenteredLine(surface, titleFont, title.empty() ? "Base documentaire" : title, 186, titleColor);
	drawCenteredLine(surface, bodyFont, "Aucune fiche resolue pour cette zone", 244, textColor);
	drawCenteredLine(surface, bodyFont, "Cliquer ou appuyer sur une touche pour revenir", 290, textColor);

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

// Builds the word-wrapped layout of the current record; false when the record is missing
bool Egypt_Documentation::prepareRecord(ViewerState &state, const Graphics::Font *bodyFont) {
	state.record = findRecord(state.themeRecords[state.currentRecordIndex]);
	if (!state.record)
		return false;

	// Split body runs into words
	state.words.clear();
	bool pendingBreak = false;
	for (uint ri = 0; ri < state.record->bodyRuns.size(); ++ri) {
		const Common::String &text = state.record->bodyRuns[ri].text;
		const int lnk = state.record->bodyRuns[ri].linkIndex;
		uint i = 0;
		while (i < text.size()) {
			if (text[i] == '\n') { pendingBreak = true; ++i; continue; }
			if (text[i] == ' ')  { ++i; continue; }
			uint j = i;
			while (j < text.size() && text[j] != ' ' && text[j] != '\n') ++j;
			DocWord w; w.text = text.substr(i, j - i); w.linkIndex = lnk; w.lineBreak = pendingBreak;
			state.words.push_back(w);
			pendingBreak = false;
			i = j;
		}
	}

	// Wrap words into lines fitting the text panel
	const int maxWidth = docTextPanel().width();
	state.lines.clear();
	DocLine curLine; int curW = 0;
	const int spW = bodyFont->getStringWidth(" ");
	for (uint wi = 0; wi < state.words.size(); ++wi) {
		const DocWord &w = state.words[wi];
		if (w.lineBreak && !curLine.words.empty()) {
			state.lines.push_back(curLine); curLine.words.clear(); curW = 0;
		}
		const int ww = bodyFont->getStringWidth(w.text);
		if (!curLine.words.empty() && curW + spW + ww > maxWidth) {
			state.lines.push_back(curLine); curLine.words.clear(); curW = 0;
		}
		DocLineWord lw; lw.wordIdx = wi; lw.x = curLine.words.empty() ? 0 : curW + spW;
		curLine.words.push_back(lw);
		curW = lw.x + ww;
	}
	if (!curLine.words.empty()) state.lines.push_back(curLine);

	const int lineHeight = bodyFont->getFontHeight() + 1;
	state.visibleLines = MAX(1, docTextPanel().height() / lineHeight);
	state.maxScroll    = MAX<int>(0, (int)state.lines.size() - state.visibleLines);
	state.scrollOffset = CLIP<int>(state.scrollOffset, 0, state.maxScroll);
	return true;
}

// Pure drawing of the record screen; refreshes state.linkHits
void Egypt_Documentation::drawRecord(ViewerState &state, const Graphics::Font *titleFont,
                                     const Graphics::Font *bodyFont, const Common::Point &mousePos) {
	const EgyptDocumentationRecord *record = state.record;
	const Common::Rect textPanel   = docTextPanel();
	const Common::Rect imageRect   = docImageRect();
	const Common::Rect captionRect = docCaptionRect();
	const Common::Rect prevButton  = docPrevButton();
	const Common::Rect nextButton  = docNextButton();
	const Common::Rect backButton  = docBackButton();
	const int lineHeight = bodyFont->getFontHeight() + 1;

	Graphics::ManagedSurface rs(kScreenWidth, kScreenHeight, g_system->getScreenFormat());
	if (state.hasBackground)
		rs.blitFrom(state.background);
	else
		rs.clear(docColor(rs.format, kDocFallbackBg));

	// Record photo (right side) - load at native size, scale to fit imageRect
	const Common::Path assetPath = documentationAssetPathFromName(record->assetName);
	if (!assetPath.empty()) {
		Graphics::ManagedSurface recordAsset;
		if (loadTgaImage(assetPath, recordAsset, false) && recordAsset.w > 0 && recordAsset.h > 0) {
			const int dstW = imageRect.width();
			const int dstH = imageRect.height();
			const float scaleX = (float)dstW / recordAsset.w;
			const float scaleY = (float)dstH / recordAsset.h;
			const float scale  = MIN(scaleX, scaleY);
			const int fitW = (int)(recordAsset.w * scale);
			const int fitH = (int)(recordAsset.h * scale);
			const int offX = imageRect.left + (dstW - fitW) / 2;
			const int offY = imageRect.top  + (dstH - fitH) / 2;
			rs.blitFrom(recordAsset,
			            Common::Rect(0, 0, recordAsset.w, recordAsset.h),
			            Common::Rect(offX, offY, offX + fitW, offY + fitH));
		}
	}

	const uint32 titleCol   = docColor(rs.format, kDocTitleColor);
	const uint32 textCol    = docColor(rs.format, kDocTextColor);
	const uint32 linkCol    = docColor(rs.format, kDocLinkColor);
	const uint32 captionCol = docColor(rs.format, kDocCaptionColor);
	const uint32 navCol     = docColor(rs.format, kDocNavColor);
	const uint32 navActive  = docColor(rs.format, kDocNavActive);
	const uint32 navDimmed  = docColor(rs.format, kDocNavDimmed);

	// Title (centered in title bar baked into TGA)
	drawCenteredLine(rs, titleFont, record->title, 24, titleCol);

	// Body text with inline hyperlinks
	state.linkHits.clear();
	int drawY = textPanel.top + 8;
	for (int li = state.scrollOffset;
	     li < (int)state.lines.size() && li < state.scrollOffset + state.visibleLines;
	     ++li, drawY += lineHeight) {
		for (uint wi = 0; wi < state.lines[li].words.size(); ++wi) {
			const DocLineWord &lw = state.lines[li].words[wi];
			const DocWord &word = state.words[lw.wordIdx];
			const int drawX = textPanel.left + 10 + lw.x;
			bodyFont->drawString(&rs, word.text, drawX, drawY,
			                     textPanel.width() - 20, word.linkIndex >= 0 ? linkCol : textCol);
			if (word.linkIndex >= 0) {
				DocLinkHit hit;
				hit.rect = Common::Rect(drawX, drawY, drawX + bodyFont->getStringWidth(word.text), drawY + lineHeight);
				hit.linkIndex = word.linkIndex;
				state.linkHits.push_back(hit);
			}
		}
	}

	// Photo caption (centered, below image)
	if (!record->assetCaption.empty()) {
		Common::Array<Common::String> capLines;
		bodyFont->wordWrapText(record->assetCaption, captionRect.width(), capLines);
		int capY = captionRect.top;
		for (uint ci = 0; ci < capLines.size() && capY + lineHeight <= captionRect.bottom; ++ci, capY += lineHeight)
			bodyFont->drawString(&rs, capLines[ci], captionRect.left, capY,
			                     captionRect.width(), captionCol, Graphics::kTextAlignCenter);
	}

	// Navigation < > and back button
	const bool canPrev = state.currentRecordIndex > 0;
	const bool canNext = state.currentRecordIndex + 1 < (int)state.themeRecords.size();
	bodyFont->drawString(&rs, "<", prevButton.left, prevButton.top + 2, prevButton.width(),
	    (canPrev && prevButton.contains(mousePos)) ? navActive : (canPrev ? navCol : navDimmed),
	    Graphics::kTextAlignCenter);
	bodyFont->drawString(&rs, ">", nextButton.left, nextButton.top + 2, nextButton.width(),
	    (canNext && nextButton.contains(mousePos)) ? navActive : (canNext ? navCol : navDimmed),
	    Graphics::kTextAlignCenter);
	bodyFont->drawString(&rs, "*", backButton.left, backButton.top + 2, backButton.width(),
	    backButton.contains(mousePos) ? navActive : navCol,
	    Graphics::kTextAlignCenter);

	// Hovered inline link -> cursor + tooltip
	const DocLinkHit *hoveredLink = nullptr;
	for (uint hi = 0; hi < state.linkHits.size(); ++hi) {
		if (state.linkHits[hi].rect.contains(mousePos)) { hoveredLink = &state.linkHits[hi]; break; }
	}
	_engine->setInterfaceCursor(hoveredLink ? kEgyptCursorWarpLabel : kEgyptCursorDefault);
	if (hoveredLink && hoveredLink->linkIndex < (int)record->links.size()) {
		const EgyptDocumentationRecord *lr = findRecord(record->links[hoveredLink->linkIndex]);
		if (lr && !lr->title.empty()) {
			const int tipW = bodyFont->getStringWidth(lr->title);
			const int tipX = CLIP<int>(mousePos.x + 18, 8, rs.w - tipW - 12);
			const int tipY = CLIP<int>(mousePos.y + 14, 8, rs.h - lineHeight - 10);
			const Common::Rect tipR(tipX - 6, tipY - 3, tipX + tipW + 6, tipY + lineHeight + 4);
			rs.fillRect(tipR,  docColor(rs.format, kDocTooltipBg));
			rs.frameRect(tipR, docColor(rs.format, kDocTooltipFrame));
			bodyFont->drawString(&rs, lr->title, tipX, tipY,
			                     rs.w - tipX, docColor(rs.format, kDocTooltipText));
		}
	}

	g_system->copyRectToScreen(rs.getPixels(), rs.pitch, 0, 0, rs.w, rs.h);
	g_system->updateScreen();
}

// Handles keys and clicks for the record viewer.
// Returns true when the current record must be reloaded (navigation/link).
bool Egypt_Documentation::handleRecordEvents(ViewerState &state, bool &exitViewer, bool &redraw) {
	const Common::Rect textPanel  = docTextPanel();
	const Common::Rect prevButton = docPrevButton();
	const Common::Rect nextButton = docNextButton();
	const Common::Rect backButton = docBackButton();

	const Common::KeyCode keycode = _engine->getNextKey().keycode;
	if (keycode == Common::KEYCODE_ESCAPE || keycode == Common::KEYCODE_BACKSPACE) {
		exitViewer = true;
		return false;
	} else if (keycode == Common::KEYCODE_LEFT) {
		if (state.currentRecordIndex > 0) { --state.currentRecordIndex; state.scrollOffset = 0; return true; }
	} else if (keycode == Common::KEYCODE_RIGHT) {
		if (state.currentRecordIndex + 1 < (int)state.themeRecords.size()) {
			++state.currentRecordIndex; state.scrollOffset = 0; return true;
		}
	} else if (keycode == Common::KEYCODE_UP) {
		if (state.scrollOffset > 0) { --state.scrollOffset; redraw = true; }
	} else if (keycode == Common::KEYCODE_DOWN) {
		if (state.scrollOffset < state.maxScroll) { ++state.scrollOffset; redraw = true; }
	}

	if (_engine->getCurrentMouseButton() == 1) {
		const Common::Point mouse = _engine->getMousePos();
		_engine->waitMouseRelease();

		// Inline hyperlink click
		for (uint hi = 0; hi < state.linkHits.size(); ++hi) {
			if (!state.linkHits[hi].rect.contains(mouse)) continue;
			const int hitIdx = state.linkHits[hi].linkIndex;
			if (hitIdx < (int)state.record->links.size()) {
				const int linkId = state.record->links[hitIdx];
				const int lt = findThemeIndexForRecord(linkId);
				if (lt >= 0) {
					Common::Array<int> ltr;
					collectLeafRecords(kEgyptDocumentationThemeIds[lt], ltr);
					const int li = findDocumentationRecordIndex(ltr, linkId);
					if (li >= 0) {
						state.selectedTheme = lt; state.themeRecords = ltr;
						state.currentRecordIndex = li; state.scrollOffset = 0;
						return true;
					}
				} else {
					state.themeRecords.clear(); state.themeRecords.push_back(linkId);
					state.currentRecordIndex = 0; state.scrollOffset = 0;
					return true;
				}
			}
		}

		if (backButton.contains(mouse)) {
			exitViewer = true;
		} else if (prevButton.contains(mouse) && state.currentRecordIndex > 0) {
			--state.currentRecordIndex; state.scrollOffset = 0; return true;
		} else if (nextButton.contains(mouse) && state.currentRecordIndex + 1 < (int)state.themeRecords.size()) {
			++state.currentRecordIndex; state.scrollOffset = 0; return true;
		} else if (textPanel.contains(mouse)) {
			if (mouse.y >= textPanel.top + textPanel.height() / 2) {
				if (state.scrollOffset < state.maxScroll) ++state.scrollOffset;
			} else if (state.scrollOffset > 0) {
				--state.scrollOffset;
			}
			redraw = true;
		}
	}

	return false;
}

void Egypt_Documentation::displayRecord(int docId) {
	if (!loadData()) {
		warning("Egypt: displayRecord(%d): documentation data could not be loaded", docId);
		return;
	}

	debugC(kDebugFile, "Egypt: displayRecord(%d): data loaded, %u record(s)", docId, _records.size());

	const EgyptDocumentationRecord *diagRec = findRecord(docId);
	if (!diagRec)
		warning("Egypt: displayRecord(%d): record not found in %u records", docId, _records.size());
	else
		debugC(kDebugVariable, "Egypt: displayRecord(%d): record found title='%s' bodyRuns=%u links=%u",
		        docId, diagRec->title.c_str(), diagRec->bodyRuns.size(), diagRec->links.size());

	ViewerState state;
	state.selectedTheme = findThemeIndexForRecord(docId);

	if (state.selectedTheme >= 0) {
		collectLeafRecords(kEgyptDocumentationThemeIds[state.selectedTheme], state.themeRecords);
		const int idx = findDocumentationRecordIndex(state.themeRecords, docId);
		if (idx >= 0) state.currentRecordIndex = idx;
	} else {
		state.selectedTheme = 0;
		state.themeRecords.push_back(docId);
	}

	const Graphics::Font *titleFont = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	const Graphics::Font *bodyFont  = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!titleFont) titleFont = bodyFont;
	if (!bodyFont)  bodyFont  = titleFont;
	if (!bodyFont)  return;

	_engine->showMouse(true);
	_engine->setInterfaceCursor(kEgyptCursorDefault);
	_engine->clearKeys();
	_engine->waitMouseRelease();

	bool exitViewer = false;

	while (!_engine->shouldAbort() && !exitViewer) {
		// Reload theme background only when theme changes
		if (state.selectedTheme != state.loadedBgTheme) {
			const char *bgName = (state.selectedTheme >= 0 && state.selectedTheme < kEgyptDocumentationThemeCount)
			    ? kEgyptDocumentationThemeBackgrounds[state.selectedTheme]
			    : "SPRITE/FONDBLEU.TGA";
			state.background = Graphics::ManagedSurface();
			state.hasBackground = loadTgaImage(Common::Path(bgName), state.background, true);
			state.loadedBgTheme = state.selectedTheme;
		}

		if (!prepareRecord(state, bodyFont))
			break;

		Common::Point lastMousePos(-1, -1);
		bool redraw = true;
		bool reloadRecord = false;

		while (!_engine->shouldAbort() && !exitViewer && !reloadRecord) {
			if (redraw) {
				const Common::Point mousePos = _engine->getMousePos();
				drawRecord(state, titleFont, bodyFont, mousePos);
				lastMousePos = mousePos;
				redraw = false;
			}

			_engine->pollEvents();
			const Common::Point currentMousePos = _engine->getMousePos();
			if (currentMousePos != lastMousePos) { redraw = true; continue; }

			reloadRecord = handleRecordEvents(state, exitViewer, redraw);

			if (reloadRecord) break;
			if (!exitViewer) g_system->delayMillis(10);
		}
	}

	_engine->clearKeys();
	_engine->waitMouseRelease();
}

void Egypt_Documentation::runStandaloneMode() {
	debugC(kDebugVariable, "EGYPT_MENU: selection=Documentation mode=autonomous status=prototype");

	if (!loadData()) {
		Common::Array<Common::String> lines;
		lines.push_back("Documentation data not available");
		lines.push_back("Cliquez ou appuyez sur une touche pour revenir au menu");
		_engine->drawSimpleScreen("Egypt 1156", lines);
		_engine->clearKeys();
		_engine->waitMouseRelease();
		while (!_engine->shouldAbort()) {
			_engine->pollEvents();
			if (_engine->getCurrentMouseButton() == 1 || _engine->getNextKey().keycode != Common::KEYCODE_INVALID)
				break;
			g_system->updateScreen();
			g_system->delayMillis(10);
		}
		_engine->clearKeys();
		_engine->waitMouseRelease();
		return;
	}

	Graphics::ManagedSurface summaryBackground;
	Graphics::ManagedSurface viewerBackground;
	const bool hasSummaryBackground = loadTgaImage(_engine->getFilePath(kFileTypeSpriteImage, "SOMMAIRE.TGA"), summaryBackground, true);
	const bool hasViewerBackground = loadTgaImage(_engine->getFilePath(kFileTypeSpriteImage, "FONDBLEU.TGA"), viewerBackground, true);
	if (hasSummaryBackground)
		debugC(kDebugFile, "EGYPT_MENU: documentation_asset=SOMMAIRE.TGA status=loaded");
	if (hasViewerBackground)
		debugC(kDebugFile, "EGYPT_MENU: documentation_asset=FONDBLEU.TGA status=loaded");

	_engine->showMouse(true);
	_engine->setInterfaceCursor(kEgyptCursorDefault);
	_engine->clearKeys();
	_engine->waitMouseRelease();

	// Layout positions (estimated from original screenshot, 640x480)
	static const int kThemeTextX = 80;
	static const int kThemeTextY[kEgyptDocumentationThemeCount] = { 38, 92, 145, 198, 252, 306 };
	static const int kMidX      = 234;
	static const int kMidYStart = 55;
	static const int kMidSpacing = 22;
	static const int kRightX      = 322;
	static const int kRightYStart = 55;
	static const int kRightSpacing = 22;

	int selectedTheme   = -1;
	int hoveredTheme    = -1;
	int selectedMidId   = -1;
	int hoveredMidIdx   = -1;
	int hoveredRightIdx = -1;
	int openTargetId    = -1;

	bool exitDocumentation = false;
	while (!_engine->shouldAbort() && !exitDocumentation) {
		// Static theme hit boxes (icon + label area, left column)
		Common::Rect themeBoxes[kEgyptDocumentationThemeCount];
		for (int i = 0; i < kEgyptDocumentationThemeCount; ++i)
			themeBoxes[i] = Common::Rect(20, kThemeTextY[i] - 10, 210, kThemeTextY[i] + 20);

		// Prefer the smaller console font; fall back to GUI font if unavailable
		const Graphics::Font *bodyFont = FontMan.getFontByUsage(Graphics::FontManager::kConsoleFont);
		if (!bodyFont)
			bodyFont = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
		if (!bodyFont) {
			exitDocumentation = true;
			break;
		}

		bool openTheme    = false;
		bool redrawSummary = true;

		while (!_engine->shouldAbort() && !exitDocumentation && !openTheme) {
			// Recompute dynamic children lists from current selection each iteration
			Common::Array<int> midChildren;
			if (selectedTheme >= 0) {
				Common::HashMap<int, Common::Array<int> >::const_iterator it =
				        _tree.find(kEgyptDocumentationThemeIds[selectedTheme]);
				if (it != _tree.end())
					midChildren = it->_value;
			}

			Common::Array<int> rightChildren;
			if (selectedMidId >= 0) {
				Common::HashMap<int, Common::Array<int> >::const_iterator it =
				        _tree.find(selectedMidId);
				if (it != _tree.end())
					rightChildren = it->_value;
			}

			// Rebuild dynamic hit boxes from current children
			// Mid column extends to full width when right column is absent
			const int midBoxRight = rightChildren.empty() ? 635 : (kRightX - 4);
			Common::Array<Common::Rect> midBoxes;
			for (uint j = 0; j < midChildren.size(); ++j) {
				const int y = kMidYStart + (int)j * kMidSpacing;
				midBoxes.push_back(Common::Rect(kMidX - 4, y - 2, midBoxRight, y + 18));
			}

			Common::Array<Common::Rect> rightBoxes;
			for (uint k = 0; k < rightChildren.size(); ++k) {
				const int y = kRightYStart + (int)k * kRightSpacing;
				rightBoxes.push_back(Common::Rect(kRightX - 4, y - 2, 635, y + 18));
			}

			if (redrawSummary) {
				Graphics::ManagedSurface summarySurface(kScreenWidth, kScreenHeight, g_system->getScreenFormat());
				if (hasSummaryBackground)
					summarySurface.blitFrom(summaryBackground);
				else
					summarySurface.clear(summarySurface.format.RGBToColor(0, 0, 0));

				const uint32 normalColor = docColor(summarySurface.format, kDocTextColor);
				const uint32 activeColor = docColor(summarySurface.format, kDocLinkColor);

				const int fontH = bodyFont->getFontHeight();

				// Left column: theme labels - vertically centered within each icon slot
				for (int i = 0; i < kEgyptDocumentationThemeCount; ++i) {
					const bool active = (i == hoveredTheme || i == selectedTheme);
					const int textY = kThemeTextY[i] - fontH / 2;
					bodyFont->drawString(&summarySurface, kEgyptDocumentationThemeLabels[i],
					                     kThemeTextX, textY, 150,
					                     active ? activeColor : normalColor);
				}

				// Middle column width: full when right column is absent, narrow otherwise
				const int midWidth = rightChildren.empty() ? (634 - kMidX) : (kRightX - 4 - kMidX);

				// Middle column: direct children of selected theme
				for (uint j = 0; j < midChildren.size(); ++j) {
					const EgyptDocumentationRecord *rec = findRecord(midChildren[j]);
					if (!rec)
						continue;
					const bool active = ((int)j == hoveredMidIdx || midChildren[j] == selectedMidId);
					const int y = kMidYStart + (int)j * kMidSpacing;
					bodyFont->drawString(&summarySurface, rec->title,
					                     kMidX, y, midWidth, active ? activeColor : normalColor);
				}

				// Right column: children of selected mid node
				for (uint k = 0; k < rightChildren.size(); ++k) {
					const EgyptDocumentationRecord *rec = findRecord(rightChildren[k]);
					if (!rec)
						continue;
					const bool active = ((int)k == hoveredRightIdx);
					const int y = kRightYStart + (int)k * kRightSpacing;
					bodyFont->drawString(&summarySurface, rec->title,
					                     kRightX, y, 634 - kRightX, active ? activeColor : normalColor);
				}

				g_system->copyRectToScreen(summarySurface.getPixels(), summarySurface.pitch,
				                           0, 0, summarySurface.w, summarySurface.h);
				g_system->updateScreen();
				redrawSummary = false;
			}

			_engine->pollEvents();
			const Common::Point mouse = _engine->getMousePos();

			// Hover detection across all three columns
			int newHoveredTheme = -1;
			for (int i = 0; i < kEgyptDocumentationThemeCount; ++i) {
				if (themeBoxes[i].contains(mouse)) {
					newHoveredTheme = i;
					break;
				}
			}

			int newHoveredMidIdx = -1;
			for (uint j = 0; j < midBoxes.size(); ++j) {
				if (midBoxes[j].contains(mouse)) {
					newHoveredMidIdx = (int)j;
					break;
				}
			}

			int newHoveredRightIdx = -1;
			for (uint k = 0; k < rightBoxes.size(); ++k) {
				if (rightBoxes[k].contains(mouse)) {
					newHoveredRightIdx = (int)k;
					break;
				}
			}

			if (newHoveredTheme != hoveredTheme || newHoveredMidIdx != hoveredMidIdx ||
			    newHoveredRightIdx != hoveredRightIdx) {
				hoveredTheme    = newHoveredTheme;
				hoveredMidIdx   = newHoveredMidIdx;
				hoveredRightIdx = newHoveredRightIdx;
				redrawSummary   = true;
			}

			if (redrawSummary)
				continue;

			if (_engine->getCurrentMouseButton() == 1) {
				bool handled = false;

				// Left column: select theme, reset mid selection
				for (int i = 0; i < kEgyptDocumentationThemeCount && !handled; ++i) {
					if (themeBoxes[i].contains(mouse)) {
						if (selectedTheme != i) {
							selectedTheme   = i;
							selectedMidId   = -1;
							hoveredMidIdx   = -1;
							hoveredRightIdx = -1;
						}
						_engine->waitMouseRelease();
						redrawSummary = true;
						handled = true;
					}
				}

				// Middle column: drill into sub-category or open leaf article
				for (uint j = 0; j < midBoxes.size() && !handled; ++j) {
					if (midBoxes[j].contains(mouse)) {
						const int childId = midChildren[j];
						const bool hasSub = _tree.find(childId) != _tree.end();
						if (hasSub) {
							selectedMidId   = childId;
							hoveredRightIdx = -1;
							redrawSummary   = true;
						} else {
							openTargetId = childId;
							openTheme    = true;
						}
						_engine->waitMouseRelease();
						handled = true;
					}
				}

				// Right column: drill deeper if node has children, else open article
				for (uint k = 0; k < rightBoxes.size() && !handled; ++k) {
					if (rightBoxes[k].contains(mouse)) {
						const int childId = rightChildren[k];
						const bool hasSub = _tree.find(childId) != _tree.end();
						if (hasSub) {
							selectedMidId   = childId;
							hoveredRightIdx = -1;
							redrawSummary   = true;
						} else {
							openTargetId = childId;
							openTheme    = true;
						}
						_engine->waitMouseRelease();
						handled = true;
					}
				}
			}

			const Common::KeyCode keycode = _engine->getNextKey().keycode;
			if (keycode == Common::KEYCODE_ESCAPE)
				exitDocumentation = true;

			g_system->updateScreen();
			g_system->delayMillis(10);
		}

		if (exitDocumentation)
			break;
		if (!openTheme)
			break;

		hoveredTheme    = -1;
		hoveredMidIdx   = -1;
		hoveredRightIdx = -1;
		displayRecord(openTargetId);
		openTargetId = -1;
	}

	_engine->clearKeys();
	_engine->waitMouseRelease();
	_engine->showMouse(false);
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
