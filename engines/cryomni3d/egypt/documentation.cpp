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

#include <cstdlib>

#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"

#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/managed_surface.h"

#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

namespace {

const int kEgyptDocumentationThemeCount = 6;
const int kEgyptDocumentationThemeIds[kEgyptDocumentationThemeCount] = { 1, 2, 3, 4, 5, 6 };
const char *const kEgyptDocumentationThemeLabels[kEgyptDocumentationThemeCount] = {
	"La terre", "Le temps", "Les hommes", "Le pharaon", "Les dieux", "Personnages"
};
const char *const kEgyptDocumentationThemeBackgrounds[kEgyptDocumentationThemeCount] = {
	"SPRITE/FICHETER.TGA",  // La terre
	"SPRITE/FICHETEM.TGA",  // Le temps
	"SPRITE/FICHEHOM.TGA",  // Les hommes
	"SPRITE/FICHEPHA.TGA",  // Le pharaon
	"SPRITE/FICHEDIE.TGA",  // Les dieux
	"SPRITE/FICHEPER.TGA",  // Personnages
};

bool parseIntegerToken(const Common::String &token, int &value) {
	if (token.empty())
		return false;

	char *endPtr = nullptr;
	const long parsedValue = strtol(token.c_str(), &endPtr, 10);
	if (endPtr == token.c_str() || !endPtr || *endPtr != '\0')
		return false;

	value = (int)parsedValue;
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

	return Common::Path();
}

// Parse a raw body line (with #word## link markers and & replacements) into text runs.
// linkCounter is incremented for each #word## found and maps to record->links[N].
static void parseBodyRunLine(const Common::String &rawLine, int &linkCounter,
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

struct DocWord    { Common::String text; int linkIndex; bool lineBreak; };
struct DocLineWord{ int wordIdx; int x; };
struct DocLine    { Common::Array<DocLineWord> words; };
struct DocLinkHit { Common::Rect rect; int linkIndex; };

static void buildDocWordList(const Common::Array<EgyptDocTextRun> &runs,
                              Common::Array<DocWord> &out) {
	bool pendingBreak = false;
	for (uint ri = 0; ri < runs.size(); ++ri) {
		const Common::String &text = runs[ri].text;
		const int lnk = runs[ri].linkIndex;
		uint i = 0;
		while (i < text.size()) {
			if (text[i] == '\n') { pendingBreak = true; ++i; continue; }
			if (text[i] == ' ')  { ++i; continue; }
			uint j = i;
			while (j < text.size() && text[j] != ' ' && text[j] != '\n') ++j;
			DocWord w; w.text = text.substr(i, j - i); w.linkIndex = lnk; w.lineBreak = pendingBreak;
			out.push_back(w);
			pendingBreak = false;
			i = j;
		}
	}
}

static int buildDocLines(const Common::Array<DocWord> &words, const Graphics::Font *font,
                          int maxWidth, Common::Array<DocLine> &lines) {
	lines.clear();
	DocLine curLine; int curW = 0;
	const int spW = font->getStringWidth(" ");
	for (uint wi = 0; wi < words.size(); ++wi) {
		const DocWord &w = words[wi];
		if (w.lineBreak && !curLine.words.empty()) {
			lines.push_back(curLine); curLine.words.clear(); curW = 0;
		}
		const int ww = font->getStringWidth(w.text);
		if (!curLine.words.empty() && curW + spW + ww > maxWidth) {
			lines.push_back(curLine); curLine.words.clear(); curW = 0;
		}
		DocLineWord lw; lw.wordIdx = wi; lw.x = curLine.words.empty() ? 0 : curW + spW;
		curLine.words.push_back(lw);
		curW = lw.x + ww;
	}
	if (!curLine.words.empty()) lines.push_back(curLine);
	return (int)lines.size();
}

static void renderDocLines(Graphics::ManagedSurface &surface, const Graphics::Font *font,
                            const Common::Array<DocWord> &words, const Common::Array<DocLine> &lines,
                            const Common::Rect &textRect, int scrollLine, int visibleLines,
                            uint32 normalColor, uint32 linkColor,
                            Common::Array<DocLinkHit> &outLinks) {
	outLinks.clear();
	const int lineH = font->getFontHeight() + 1;
	int drawY = textRect.top + 8;
	for (int li = scrollLine; li < (int)lines.size() && li < scrollLine + visibleLines; ++li, drawY += lineH) {
		for (uint wi = 0; wi < lines[li].words.size(); ++wi) {
			const DocLineWord &lw = lines[li].words[wi];
			const DocWord &word = words[lw.wordIdx];
			const int drawX = textRect.left + 10 + lw.x;
			font->drawString(&surface, word.text, drawX, drawY,
			                 textRect.width() - 20, word.linkIndex >= 0 ? linkColor : normalColor);
			if (word.linkIndex >= 0) {
				DocLinkHit hit;
				hit.rect = Common::Rect(drawX, drawY, drawX + font->getStringWidth(word.text), drawY + lineH);
				hit.linkIndex = word.linkIndex;
				outLinks.push_back(hit);
			}
		}
	}
}

} // End of anonymous namespace

static const EgyptDocumentationRecord *findDocumentationRecord(const Common::Array<EgyptDocumentationRecord> &records, int id) {
	for (Common::Array<EgyptDocumentationRecord>::const_iterator it = records.begin(); it != records.end(); ++it) {
		if (it->id == id)
			return &(*it);
	}

	return nullptr;
}

static void collectDocumentationLeafRecords(const Common::Array<EgyptDocumentationRecord> &records,
                                            const Common::HashMap<int, Common::Array<int> > &tree,
                                            int nodeId, Common::Array<int> &out) {
	const EgyptDocumentationRecord *record = findDocumentationRecord(records, nodeId);
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

	Common::HashMap<int, Common::Array<int> >::const_iterator it = tree.find(nodeId);
	if (it == tree.end())
		return;

	for (Common::Array<int>::const_iterator child = it->_value.begin(); child != it->_value.end(); ++child)
		collectDocumentationLeafRecords(records, tree, *child, out);
}

static int findDocumentationThemeIndexForRecord(const Common::Array<EgyptDocumentationRecord> &records,
                                                const Common::HashMap<int, Common::Array<int> > &tree,
                                                int recordId) {
	for (int themeIndex = 0; themeIndex < kEgyptDocumentationThemeCount; ++themeIndex) {
		Common::Array<int> themeRecords;
		collectDocumentationLeafRecords(records, tree, kEgyptDocumentationThemeIds[themeIndex], themeRecords);
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

bool CryOmni3DEngine_Egypt::loadDocumentationData() {
	if (_documentationDataLoaded)
		return true;

	_documentationRecords.clear();
	_documentationTree.clear();

	Common::File docFile;
	if (!docFile.open(Common::Path("REF/FR/ESPDOC.TXT"))) {
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
		_documentationRecords.push_back(record);
	}

	Common::File treeFile;
	if (!treeFile.open(Common::Path("REF/FR/ESPARBO.TXT"))) {
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

		_documentationTree[parentId] = children;
	}

	_documentationDataLoaded = true;
	warning("Egypt: loaded %u documentation record(s) and %u documentation tree node(s)",
	        _documentationRecords.size(), _documentationTree.size());
	return true;
}

bool CryOmni3DEngine_Egypt::isDocumentationZone(const EgyptZone &zone) const {
	if (zone.actionId != 6)
		return false;

	if (zone.label.hasPrefixIgnoreCase("MSG"))
		return true;

	return zone.label.equalsIgnoreCase("DOC") ||
	       zone.label.equalsIgnoreCase("BDOC") ||
	       zone.label.equalsIgnoreCase("BASEDOC");
}

int CryOmni3DEngine_Egypt::resolveDocumentationIdForZone(const EgyptZone &zone, Common::String *source) const {
	int documentationId = -1;
	if (parseIntegerToken(zone.extraParam, documentationId)) {
		if (source)
			*source = "zone_extra";
		return documentationId;
	}

	Common::HashMap<Common::String, EgyptMessageEntry, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo>::const_iterator it =
		_messageLabels.find(zone.label);
	if (it != _messageLabels.end() && it->_value.documentationId >= 0) {
		if (source)
			*source = "message_suffix";
		return it->_value.documentationId;
	}

	if (source)
		*source = "unresolved";
	return -1;
}

void CryOmni3DEngine_Egypt::displayZoneDocumentation(const EgyptZone &zone) {
	Common::String source;
	const int documentationId = resolveDocumentationIdForZone(zone, &source);

	warning("EGYPT_BASEDOC: scene=%s context=%s zone=%03u label=%s docId=%d source=%s",
	        _currentScene.name.c_str(), _currentScene.contextName.c_str(), zone.id,
	        zone.label.c_str(), documentationId, source.c_str());

	if (documentationId >= 0) {
		displayDocumentationById(documentationId);
		return;
	}

	// Fallback: no documentation ID could be resolved
	const Common::String title = resolveMessageLabel(zone.label);
	Graphics::ManagedSurface surface(640, 480, g_system->getScreenFormat());
	surface.clear(surface.format.RGBToColor(0, 0, 0));

	const Graphics::Font *titleFont = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	const Graphics::Font *bodyFont  = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!titleFont) titleFont = bodyFont;

	const uint32 borderColor = surface.format.RGBToColor(170, 130, 48);
	const uint32 titleColor  = surface.format.RGBToColor(250, 225, 170);
	const uint32 textColor   = surface.format.RGBToColor(244, 232, 204);
	const Common::Rect panel(48, 160, 592, 356);
	surface.fillRect(panel, surface.format.RGBToColor(12, 12, 12));
	surface.frameRect(panel, borderColor);
	drawCenteredLine(surface, titleFont, title.empty() ? "Base documentaire" : title, 186, titleColor);
	drawCenteredLine(surface, bodyFont, "Aucune fiche resolue pour cette zone", 244, textColor);
	drawCenteredLine(surface, bodyFont, "Cliquer ou appuyer sur une touche pour revenir", 290, textColor);

	g_system->copyRectToScreen(surface.getPixels(), surface.pitch, 0, 0, surface.w, surface.h);
	g_system->updateScreen();

	showMouse(true);
	setInterfaceCursor(kEgyptCursorDefault);
	clearKeys();
	waitMouseRelease();

	while (!shouldAbort()) {
		pollEvents();
		const Common::KeyCode keycode = getNextKey().keycode;
		if (getCurrentMouseButton() == 1 || keycode != Common::KEYCODE_INVALID)
			break;
		g_system->delayMillis(10);
	}

	clearKeys();
	waitMouseRelease();
}

void CryOmni3DEngine_Egypt::displayDocumentationById(int docId) {
	if (!loadDocumentationData()) {
		warning("Egypt: displayDocumentationById(%d): loadDocumentationData FAILED", docId);
		return;
	}

	warning("Egypt: displayDocumentationById(%d): data loaded, %u record(s)", docId, _documentationRecords.size());

	const EgyptDocumentationRecord *diagRec = findDocumentationRecord(_documentationRecords, docId);
	if (!diagRec)
		warning("Egypt: displayDocumentationById(%d): record NOT FOUND in %u records", docId, _documentationRecords.size());
	else
		warning("Egypt: displayDocumentationById(%d): record found title='%s' bodyRuns=%u links=%u",
		        docId, diagRec->title.c_str(), diagRec->bodyRuns.size(), diagRec->links.size());

	int selectedTheme = findDocumentationThemeIndexForRecord(_documentationRecords, _documentationTree, docId);
	Common::Array<int> themeRecords;
	int currentRecordIndex = 0;

	if (selectedTheme >= 0) {
		collectDocumentationLeafRecords(_documentationRecords, _documentationTree,
		                                kEgyptDocumentationThemeIds[selectedTheme], themeRecords);
		const int idx = findDocumentationRecordIndex(themeRecords, docId);
		if (idx >= 0) currentRecordIndex = idx;
	} else {
		selectedTheme = 0;
		themeRecords.push_back(docId);
	}

	const Graphics::Font *titleFont = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	const Graphics::Font *bodyFont  = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!titleFont) titleFont = bodyFont;
	if (!bodyFont)  bodyFont  = titleFont;
	if (!bodyFont)  return;

	showMouse(true);
	setInterfaceCursor(kEgyptCursorDefault);
	clearKeys();
	waitMouseRelease();

	// Layout — approximate, to be refined from EXE data
	const Common::Rect textPanel  ( 12,  60, 294, 388); // body text, left column
	const Common::Rect imageRect  (314,  60, 628, 335); // record photo, right column (16px gap after text)
	const Common::Rect captionRect(314, 340, 628, 388); // photo caption
	const Common::Rect prevButton ( 90, 432, 124, 462); // ◄
	const Common::Rect nextButton (130, 432, 164, 462); // ►
	const Common::Rect backButton (590, 432, 628, 468); // retour

	int scrollOffset = 0;
	bool exitViewer = false;
	int loadedBgTheme = -2;
	Graphics::ManagedSurface viewerBackground;
	bool hasViewerBackground = false;

	while (!shouldAbort() && !exitViewer) {
		bool reloadRecord = false;

		// Reload theme background only when theme changes
		if (selectedTheme != loadedBgTheme) {
			const char *bgName = (selectedTheme >= 0 && selectedTheme < kEgyptDocumentationThemeCount)
			    ? kEgyptDocumentationThemeBackgrounds[selectedTheme]
			    : "SPRITE/FONDBLEU.TGA";
			viewerBackground = Graphics::ManagedSurface();
			hasViewerBackground = loadWrappedTgaSurface(Common::Path(bgName), viewerBackground);
			loadedBgTheme = selectedTheme;
		}

		const EgyptDocumentationRecord *record =
		    findDocumentationRecord(_documentationRecords, themeRecords[currentRecordIndex]);
		if (!record) break;

		Common::Array<DocWord> docWords;
		buildDocWordList(record->bodyRuns, docWords);
		Common::Array<DocLine> docLines;
		buildDocLines(docWords, bodyFont, textPanel.width(), docLines);

		const int lineHeight   = bodyFont->getFontHeight() + 1;
		const int visibleLines = MAX(1, textPanel.height() / lineHeight);
		const int maxScroll    = MAX<int>(0, (int)docLines.size() - visibleLines);
		scrollOffset = CLIP<int>(scrollOffset, 0, maxScroll);

		Common::Point lastMousePos(-1, -1);
		bool redrawRecord = true;
		Common::Array<DocLinkHit> linkHits;

		while (!shouldAbort() && !exitViewer && !reloadRecord) {
			if (redrawRecord) {
				Graphics::ManagedSurface rs(640, 480, g_system->getScreenFormat());
				if (hasViewerBackground)
					rs.blitFrom(viewerBackground);
				else
					rs.clear(rs.format.RGBToColor(68, 10, 10));

				// Record photo (right side) — load at native size, scale to fit imageRect
				const Common::Path assetPath = documentationAssetPathFromName(record->assetName);
				if (!assetPath.empty()) {
					Graphics::ManagedSurface recordAsset;
					if (loadWrappedTgaRaw(assetPath, recordAsset) && recordAsset.w > 0 && recordAsset.h > 0) {
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

				const Common::Point mousePos = getMousePos();
				const uint32 titleCol   = rs.format.RGBToColor(245, 235, 215);
				const uint32 textCol    = rs.format.RGBToColor(242, 232, 210);
				const uint32 linkCol    = rs.format.RGBToColor(255, 180, 60);
				const uint32 captionCol = rs.format.RGBToColor(186, 178, 165);
				const uint32 navCol     = rs.format.RGBToColor(200, 190, 175);
				const uint32 navActive  = rs.format.RGBToColor(255, 235, 150);
				const uint32 navDimmed  = rs.format.RGBToColor(60, 55, 50);

				// Title (centered in title bar baked into TGA)
				drawCenteredLine(rs, titleFont, record->title, 24, titleCol);

				// Body text with inline hyperlinks
				renderDocLines(rs, bodyFont, docWords, docLines, textPanel,
				               scrollOffset, visibleLines, textCol, linkCol, linkHits);

				// Photo caption (centered, below image)
				if (!record->assetCaption.empty()) {
					Common::Array<Common::String> capLines;
					bodyFont->wordWrapText(record->assetCaption, captionRect.width(), capLines);
					int capY = captionRect.top;
					for (uint ci = 0; ci < capLines.size() && capY + lineHeight <= captionRect.bottom; ++ci, capY += lineHeight)
						bodyFont->drawString(&rs, capLines[ci], captionRect.left, capY,
						                     captionRect.width(), captionCol, Graphics::kTextAlignCenter);
				}

				// Navigation ◄ ►
				const bool canPrev = currentRecordIndex > 0;
				const bool canNext = currentRecordIndex + 1 < (int)themeRecords.size();
				bodyFont->drawString(&rs, "<", prevButton.left, prevButton.top + 2, prevButton.width(),
				    (canPrev && prevButton.contains(mousePos)) ? navActive : (canPrev ? navCol : navDimmed),
				    Graphics::kTextAlignCenter);
				bodyFont->drawString(&rs, ">", nextButton.left, nextButton.top + 2, nextButton.width(),
				    (canNext && nextButton.contains(mousePos)) ? navActive : (canNext ? navCol : navDimmed),
				    Graphics::kTextAlignCenter);
				bodyFont->drawString(&rs, "*", backButton.left, backButton.top + 2, backButton.width(),
				    backButton.contains(mousePos) ? navActive : navCol,
				    Graphics::kTextAlignCenter);

				// Hovered inline link → cursor + tooltip
				const DocLinkHit *hoveredLink = nullptr;
				for (uint hi = 0; hi < linkHits.size(); ++hi) {
					if (linkHits[hi].rect.contains(mousePos)) { hoveredLink = &linkHits[hi]; break; }
				}
				setInterfaceCursor(hoveredLink ? kEgyptCursorWarpLabel : kEgyptCursorDefault);
				if (hoveredLink && hoveredLink->linkIndex < (int)record->links.size()) {
					const EgyptDocumentationRecord *lr = findDocumentationRecord(
					    _documentationRecords, record->links[hoveredLink->linkIndex]);
					if (lr && !lr->title.empty()) {
						const int tipW = bodyFont->getStringWidth(lr->title);
						const int tipX = CLIP<int>(mousePos.x + 18, 8, rs.w - tipW - 12);
						const int tipY = CLIP<int>(mousePos.y + 14, 8, rs.h - lineHeight - 10);
						const Common::Rect tipR(tipX - 6, tipY - 3, tipX + tipW + 6, tipY + lineHeight + 4);
						rs.fillRect(tipR,  rs.format.RGBToColor(20, 18, 14));
						rs.frameRect(tipR, rs.format.RGBToColor(188, 154, 84));
						bodyFont->drawString(&rs, lr->title, tipX, tipY,
						                     rs.w - tipX, rs.format.RGBToColor(244, 232, 204));
					}
				}

				g_system->copyRectToScreen(rs.getPixels(), rs.pitch, 0, 0, rs.w, rs.h);
				g_system->updateScreen();
				lastMousePos = mousePos;
				redrawRecord = false;
			}

			pollEvents();
			const Common::Point currentMousePos = getMousePos();
			if (currentMousePos != lastMousePos) { redrawRecord = true; continue; }

			const Common::KeyCode keycode = getNextKey().keycode;
			if (keycode == Common::KEYCODE_ESCAPE || keycode == Common::KEYCODE_BACKSPACE) {
				exitViewer = true; break;
			} else if (keycode == Common::KEYCODE_LEFT) {
				if (currentRecordIndex > 0) { --currentRecordIndex; scrollOffset = 0; reloadRecord = true; }
			} else if (keycode == Common::KEYCODE_RIGHT) {
				if (currentRecordIndex + 1 < (int)themeRecords.size()) { ++currentRecordIndex; scrollOffset = 0; reloadRecord = true; }
			} else if (keycode == Common::KEYCODE_UP) {
				if (scrollOffset > 0) { --scrollOffset; redrawRecord = true; }
			} else if (keycode == Common::KEYCODE_DOWN) {
				if (scrollOffset < maxScroll) { ++scrollOffset; redrawRecord = true; }
			}

			if (getCurrentMouseButton() == 1) {
				const Common::Point mouse = getMousePos();
				waitMouseRelease();
				bool handledClick = false;

				// Inline hyperlink click
				for (uint hi = 0; hi < linkHits.size() && !handledClick; ++hi) {
					if (!linkHits[hi].rect.contains(mouse)) continue;
					const int hitIdx = linkHits[hi].linkIndex;
					if (hitIdx < (int)record->links.size()) {
						const int linkId = record->links[hitIdx];
						const int lt = findDocumentationThemeIndexForRecord(_documentationRecords, _documentationTree, linkId);
						if (lt >= 0) {
							Common::Array<int> ltr;
							collectDocumentationLeafRecords(_documentationRecords, _documentationTree, kEgyptDocumentationThemeIds[lt], ltr);
							const int li = findDocumentationRecordIndex(ltr, linkId);
							if (li >= 0) { selectedTheme = lt; themeRecords = ltr; currentRecordIndex = li; scrollOffset = 0; reloadRecord = true; handledClick = true; }
						} else {
							themeRecords.clear(); themeRecords.push_back(linkId);
							currentRecordIndex = 0; scrollOffset = 0; reloadRecord = true; handledClick = true;
						}
					}
				}

				if (!handledClick) {
					if (backButton.contains(mouse)) {
						exitViewer = true;
					} else if (prevButton.contains(mouse) && currentRecordIndex > 0) {
						--currentRecordIndex; scrollOffset = 0; reloadRecord = true;
					} else if (nextButton.contains(mouse) && currentRecordIndex + 1 < (int)themeRecords.size()) {
						++currentRecordIndex; scrollOffset = 0; reloadRecord = true;
					} else if (textPanel.contains(mouse)) {
						if (mouse.y >= textPanel.top + textPanel.height() / 2) { if (scrollOffset < maxScroll) ++scrollOffset; }
						else if (scrollOffset > 0) { --scrollOffset; }
						redrawRecord = true;
					}
				}
			}

			if (reloadRecord) break;
			if (!exitViewer) g_system->delayMillis(10);
		}
	}

	clearKeys();
	waitMouseRelease();
}

void CryOmni3DEngine_Egypt::startDocumentationMode() {
	warning("EGYPT_MENU: selection=Documentation mode=autonomous status=prototype");

	if (!loadDocumentationData()) {
		Common::Array<Common::String> lines;
		lines.push_back("Documentation data not available");
		lines.push_back("Cliquez ou appuyez sur une touche pour revenir au menu");
		drawSimpleScreen("Egypt 1156", lines);
		clearKeys();
		waitMouseRelease();
		while (!shouldAbort()) {
			pollEvents();
			if (getCurrentMouseButton() == 1 || getNextKey().keycode != Common::KEYCODE_INVALID)
				break;
			g_system->updateScreen();
			g_system->delayMillis(10);
		}
		clearKeys();
		waitMouseRelease();
		return;
	}

	Graphics::ManagedSurface summaryBackground;
	Graphics::ManagedSurface viewerBackground;
	const bool hasSummaryBackground = loadWrappedTgaSurface(Common::Path("SPRITE/SOMMAIRE.TGA"), summaryBackground);
	const bool hasViewerBackground = loadWrappedTgaSurface(Common::Path("SPRITE/FONDBLEU.TGA"), viewerBackground);
	if (hasSummaryBackground)
		warning("EGYPT_MENU: documentation_asset=SOMMAIRE.TGA status=loaded");
	if (hasViewerBackground)
		warning("EGYPT_MENU: documentation_asset=FONDBLEU.TGA status=loaded");

	showMouse(true);
	setInterfaceCursor(kEgyptCursorDefault);
	clearKeys();
	waitMouseRelease();

	int selectedTheme = 0;
	bool exitDocumentation = false;
	while (!shouldAbort() && !exitDocumentation) {
		Common::Rect themeBoxes[kEgyptDocumentationThemeCount];
		int y = 254;
		for (int i = 0; i < kEgyptDocumentationThemeCount; ++i, y += 32)
			themeBoxes[i] = Common::Rect(100, y - 2, 540, y + 22);

		const Graphics::Font *titleFont = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
		const Graphics::Font *bodyFont = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
		if (!titleFont)
			titleFont = bodyFont;
		if (!bodyFont)
			bodyFont = titleFont;
		if (!bodyFont) {
			exitDocumentation = true;
			break;
		}

		bool openTheme = false;
		bool redrawSummary = true;
		while (!shouldAbort() && !exitDocumentation && !openTheme) {
			if (redrawSummary) {
				Graphics::ManagedSurface summarySurface(640, 480, g_system->getScreenFormat());
				if (hasSummaryBackground)
					summarySurface.blitFrom(summaryBackground);
				else if (hasViewerBackground)
					summarySurface.blitFrom(viewerBackground);
				else
					summarySurface.clear(summarySurface.format.RGBToColor(0, 0, 0));

				const uint32 panelColor = summarySurface.format.RGBToColor(14, 16, 22);
				const uint32 borderColor = summarySurface.format.RGBToColor(172, 130, 52);
				const uint32 titleColor = summarySurface.format.RGBToColor(245, 219, 160);
				const uint32 textColor = summarySurface.format.RGBToColor(242, 235, 218);
				const uint32 selectedColor = summarySurface.format.RGBToColor(255, 220, 98);
				const uint32 hintColor = summarySurface.format.RGBToColor(182, 182, 182);
				const Common::Rect panel(54, 186, 586, 440);
				summarySurface.fillRect(panel, panelColor);
				summarySurface.frameRect(panel, borderColor);

				drawCenteredLine(summarySurface, titleFont, "Espace documentaire", 206, titleColor);

				int drawY = 254;
				for (int i = 0; i < kEgyptDocumentationThemeCount; ++i, drawY += 32) {
					const uint32 color = i == selectedTheme ? selectedColor : textColor;
					Common::String line = Common::String::format("[%d] %s", i + 1, kEgyptDocumentationThemeLabels[i]);
					bodyFont->drawString(&summarySurface, line, 112, drawY, 400, color);
				}

				drawCenteredLine(summarySurface, bodyFont,
				                 "Fleches: choisir  Entree: ouvrir  Echap: retour menu",
				                 408, hintColor);

				g_system->copyRectToScreen(summarySurface.getPixels(), summarySurface.pitch, 0, 0, summarySurface.w, summarySurface.h);
				g_system->updateScreen();
				redrawSummary = false;
			}

			pollEvents();
			const Common::Point mouse = getMousePos();
			for (int i = 0; i < kEgyptDocumentationThemeCount; ++i) {
				if (themeBoxes[i].contains(mouse) && selectedTheme != i) {
					selectedTheme = i;
					redrawSummary = true;
					break;
				}
			}
			if (redrawSummary)
				break;

			if (getCurrentMouseButton() == 1) {
				for (int i = 0; i < kEgyptDocumentationThemeCount; ++i) {
					if (themeBoxes[i].contains(mouse)) {
						selectedTheme = i;
						openTheme = true;
						waitMouseRelease();
						break;
					}
				}
			}

			const Common::KeyCode keycode = getNextKey().keycode;
			if (keycode == Common::KEYCODE_ESCAPE) {
				exitDocumentation = true;
				break;
			} else if (keycode == Common::KEYCODE_UP) {
				selectedTheme = (selectedTheme + kEgyptDocumentationThemeCount - 1) % kEgyptDocumentationThemeCount;
				redrawSummary = true;
			} else if (keycode == Common::KEYCODE_DOWN) {
				selectedTheme = (selectedTheme + 1) % kEgyptDocumentationThemeCount;
				redrawSummary = true;
			} else if ((keycode >= Common::KEYCODE_1 && keycode <= Common::KEYCODE_6) ||
			           (keycode >= Common::KEYCODE_KP1 && keycode <= Common::KEYCODE_KP6)) {
				selectedTheme = keycode >= Common::KEYCODE_KP1 ? keycode - Common::KEYCODE_KP1 : keycode - Common::KEYCODE_1;
				openTheme = true;
			} else if (keycode == Common::KEYCODE_RETURN || keycode == Common::KEYCODE_SPACE) {
				openTheme = true;
			}

			g_system->updateScreen();
			g_system->delayMillis(10);
		}

		if (exitDocumentation)
			break;
		if (redrawSummary)
			continue;
		if (!openTheme)
			break;

		Common::Array<int> themeRecords;
		collectDocumentationLeafRecords(_documentationRecords, _documentationTree,
		                                kEgyptDocumentationThemeIds[selectedTheme], themeRecords);
		if (themeRecords.empty()) {
			warning("EGYPT_MENU: documentation_theme=%d label=%s has no record",
			        kEgyptDocumentationThemeIds[selectedTheme], kEgyptDocumentationThemeLabels[selectedTheme]);
			continue;
		}

		displayDocumentationById(themeRecords[0]);
	}

	clearKeys();
	waitMouseRelease();
	showMouse(false);
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
