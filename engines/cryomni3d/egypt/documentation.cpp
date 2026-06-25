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

const int kEgyptDocumentationThemeCount = 5;
const int kEgyptDocumentationThemeIds[kEgyptDocumentationThemeCount] = { 1, 2, 3, 4, 5 };
const char *const kEgyptDocumentationThemeLabels[kEgyptDocumentationThemeCount] = {
	"La terre", "Le temps", "Les hommes", "Le pharaon", "Les dieux"
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
				continue;
			}

			if (current.hasPrefix("<")) {
				const int commaPos = current.find(',');
				const int endPos = current.find('>');
				if (commaPos > 1)
					record.assetName = current.substr(1, commaPos - 1);
				else if (endPos > 1)
					record.assetName = current.substr(1, endPos - 1);
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
	const Common::String title = resolveMessageLabel(zone.label);
	Common::String source;
	const int documentationId = resolveDocumentationIdForZone(zone, &source);

	Graphics::ManagedSurface surface(640, 480, g_system->getScreenFormat());
	Common::Path assetPath;
	bool loaded = false;
	if (documentationId >= 0) {
		assetPath = Common::Path(Common::String::format("SPRITE/BASEDOC/F%d.TGA", documentationId));
		loaded = loadWrappedTgaSurface(assetPath, surface);
	}

	warning("EGYPT_BASEDOC: scene=%s context=%s zone=%03u label=%s docId=%d source=%s asset=%s status=%s",
	        _currentScene.name.c_str(), _currentScene.contextName.c_str(), zone.id, zone.label.c_str(),
	        documentationId, source.c_str(),
	        assetPath.empty() ? "none" : assetPath.toString(Common::Path::kNativeSeparator).c_str(),
	        loaded ? "loaded" : "fallback");

	const Graphics::Font *titleFont = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	const Graphics::Font *bodyFont = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!titleFont)
		titleFont = bodyFont;

	if (loaded) {
		const uint32 panelColor = surface.format.RGBToColor(12, 12, 12);
		const uint32 borderColor = surface.format.RGBToColor(170, 130, 48);
		const uint32 titleColor = surface.format.RGBToColor(250, 225, 170);
		const uint32 hintColor = surface.format.RGBToColor(244, 232, 204);

		const Common::Rect titlePanel(12, 10, 628, 42);
		const Common::Rect hintPanel(118, 444, 522, 472);
		surface.fillRect(titlePanel, panelColor);
		surface.frameRect(titlePanel, borderColor);
		surface.fillRect(hintPanel, panelColor);
		surface.frameRect(hintPanel, borderColor);

		drawCenteredLine(surface, titleFont, title.empty() ? zone.label : title, 18, titleColor);
		drawCenteredLine(surface, bodyFont, "Cliquer ou appuyer sur une touche pour revenir", 452, hintColor);
	} else {
		surface.clear(surface.format.RGBToColor(0, 0, 0));

		const uint32 borderColor = surface.format.RGBToColor(170, 130, 48);
		const uint32 titleColor = surface.format.RGBToColor(250, 225, 170);
		const uint32 textColor = surface.format.RGBToColor(244, 232, 204);
		const Common::Rect panel(48, 160, 592, 356);
		surface.fillRect(panel, surface.format.RGBToColor(12, 12, 12));
		surface.frameRect(panel, borderColor);

		drawCenteredLine(surface, titleFont, title.empty() ? "Base documentaire" : title, 186, titleColor);
		if (documentationId >= 0)
			drawCenteredLine(surface, bodyFont, Common::String::format("Fiche F%d introuvable", documentationId), 244, textColor);
		else
			drawCenteredLine(surface, bodyFont, "Aucune fiche resolue pour cette zone", 244, textColor);
		drawCenteredLine(surface, bodyFont, "Cliquer ou appuyer sur une touche pour revenir", 290, textColor);
	}

	g_system->copyRectToScreen(surface.getPixels(), surface.pitch, 0, 0, surface.w, surface.h);
	g_system->updateScreen();

	showMouse(true);
	setInterfaceCursor(kEgyptCursorDefault);
	clearKeys();
	waitMouseRelease();

	while (!shouldAbort()) {
		pollEvents();
		const Common::KeyCode keycode = getNextKey().keycode;
		if (getCurrentMouseButton() == 1 || keycode == Common::KEYCODE_ESCAPE ||
		    keycode == Common::KEYCODE_RETURN || keycode == Common::KEYCODE_SPACE ||
		    keycode != Common::KEYCODE_INVALID)
			break;
		g_system->updateScreen();
		g_system->delayMillis(10);
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
			} else if ((keycode >= Common::KEYCODE_1 && keycode <= Common::KEYCODE_5) ||
			           (keycode >= Common::KEYCODE_KP1 && keycode <= Common::KEYCODE_KP5)) {
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

		int currentRecordIndex = 0;
		int scrollOffset = 0;
		bool backToSummary = false;
		while (!shouldAbort() && !exitDocumentation && !backToSummary) {
			const EgyptDocumentationRecord *record = findDocumentationRecord(_documentationRecords, themeRecords[currentRecordIndex]);
			if (!record) {
				backToSummary = true;
				break;
			}

			const Common::Rect titlePanel(16, 14, 624, 48);
			const Common::Rect textPanel(18, 262, 622, 438);
			const Common::Rect footerPanel(18, 442, 622, 472);
			const Common::Rect backButton(26, 446, 166, 468);
			const Common::Rect prevButton(198, 446, 332, 468);
			const Common::Rect nextButton(454, 446, 614, 468);
			Common::Rect linkButtons[4];
			int visibleLinkCount = 0;
			int visibleLinkIds[4] = { -1, -1, -1, -1 };
			Common::Point lastMousePos(-1, -1);
			bool redrawRecord = true;
			bool reloadRecord = false;
			int maxScroll = 0;
			while (!shouldAbort() && !exitDocumentation && !backToSummary && !reloadRecord) {
				if (redrawRecord) {
					Graphics::ManagedSurface recordSurface(640, 480, g_system->getScreenFormat());
					if (hasViewerBackground)
						recordSurface.blitFrom(viewerBackground);
					else
						recordSurface.clear(recordSurface.format.RGBToColor(0, 0, 0));

					Graphics::ManagedSurface recordAsset;
					const Common::Path recordAssetPath = documentationAssetPathFromName(record->assetName);
					const bool hasRecordAsset = !recordAssetPath.empty() && loadWrappedTgaSurface(recordAssetPath, recordAsset);
					if (hasRecordAsset)
						recordSurface.blitFrom(recordAsset);

					const uint32 recordPanelColor = recordSurface.format.RGBToColor(10, 12, 18);
					const uint32 recordBorderColor = recordSurface.format.RGBToColor(172, 130, 52);
					const uint32 recordTitleColor = recordSurface.format.RGBToColor(245, 219, 160);
					const uint32 recordTextColor = recordSurface.format.RGBToColor(242, 235, 218);
					const uint32 recordHintColor = recordSurface.format.RGBToColor(182, 182, 182);
					const uint32 recordActiveColor = recordSurface.format.RGBToColor(255, 220, 98);
					const Common::Point mousePos = getMousePos();
					const bool hoverBack = backButton.contains(mousePos);
					const bool hoverPrev = prevButton.contains(mousePos);
					const bool hoverNext = nextButton.contains(mousePos);
					const bool hoverText = textPanel.contains(mousePos);

					recordSurface.fillRect(titlePanel, recordPanelColor);
					recordSurface.frameRect(titlePanel, recordBorderColor);
					recordSurface.fillRect(textPanel, recordPanelColor);
					recordSurface.frameRect(textPanel, recordBorderColor);
					recordSurface.fillRect(footerPanel, recordPanelColor);
					recordSurface.frameRect(footerPanel, recordBorderColor);
					recordSurface.frameRect(backButton, hoverBack ? recordActiveColor : recordBorderColor);
					recordSurface.frameRect(prevButton, hoverPrev ? recordActiveColor : recordBorderColor);
					recordSurface.frameRect(nextButton, hoverNext ? recordActiveColor : recordBorderColor);

					const Graphics::Font *recordTitleFont = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
					const Graphics::Font *recordBodyFont = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
					if (!recordTitleFont)
						recordTitleFont = recordBodyFont;
					if (!recordBodyFont)
						recordBodyFont = recordTitleFont;
					if (!recordBodyFont)
						break;

					drawCenteredLine(recordSurface, recordTitleFont, record->title, 22, recordTitleColor);

					Common::String metaLine = Common::String::format("%s  %d/%d  fiche %d",
					                                                 kEgyptDocumentationThemeLabels[selectedTheme],
					                                                 currentRecordIndex + 1, themeRecords.size(),
					                                                 record->id);
					recordBodyFont->drawString(&recordSurface, metaLine, 28, 236, 560, recordHintColor);

					Common::Array<Common::String> wrappedLines;
					recordBodyFont->wordWrapText(record->body, 580, wrappedLines);
					const int lineHeight = recordBodyFont->getFontHeight() + 1;
					const int visibleLines = MAX(1, (textPanel.height() - 14) / lineHeight);
					maxScroll = MAX<int>(0, (int)wrappedLines.size() - visibleLines);
					scrollOffset = CLIP<int>(scrollOffset, 0, maxScroll);

					int drawY = textPanel.top + 8;
					for (int i = scrollOffset; i < (int)wrappedLines.size() && i < scrollOffset + visibleLines; ++i, drawY += lineHeight)
						recordBodyFont->drawString(&recordSurface, wrappedLines[i], textPanel.left + 10, drawY, 580, recordTextColor);

					Common::String linksLine = "Liens: aucun";
					if (!record->links.empty()) {
						linksLine = "Liens:";
						for (uint i = 0; i < record->links.size() && i < 4; ++i)
							linksLine += Common::String::format(" %d", record->links[i]);
						if (record->links.size() > 4)
							linksLine += " ...";
					}
					recordBodyFont->drawString(&recordSurface, linksLine, 26, 428, 280, recordHintColor);
					visibleLinkCount = MIN<int>(4, record->links.size());
					for (int i = 0; i < visibleLinkCount; ++i) {
						visibleLinkIds[i] = record->links[i];
						linkButtons[i] = Common::Rect(208 + i * 82, 424, 280 + i * 82, 440);
						const bool hoverLink = linkButtons[i].contains(mousePos);
						recordSurface.frameRect(linkButtons[i], hoverLink ? recordActiveColor : recordBorderColor);
						recordBodyFont->drawString(&recordSurface, Common::String::format("%d", visibleLinkIds[i]),
						                           linkButtons[i].left + 4, 427, linkButtons[i].width() - 8,
						                           hoverLink ? recordActiveColor : recordHintColor,
						                           Graphics::kTextAlignCenter);
					}
					recordBodyFont->drawString(&recordSurface, "Sommaire",
					                           backButton.left + 8, 450, backButton.width() - 16,
					                           hoverBack ? recordActiveColor : recordHintColor, Graphics::kTextAlignCenter);
					recordBodyFont->drawString(&recordSurface, "Precedente",
					                           prevButton.left + 8, 450, prevButton.width() - 16,
					                           hoverPrev ? recordActiveColor : recordHintColor, Graphics::kTextAlignCenter);
					recordBodyFont->drawString(&recordSurface, "Suivante",
					                           nextButton.left + 8, 450, nextButton.width() - 16,
					                           hoverNext ? recordActiveColor : recordHintColor, Graphics::kTextAlignCenter);
					if (hoverText) {
						recordBodyFont->drawString(&recordSurface,
						                           "Clic haut/bas dans le texte pour defiler",
						                           206, 450, 236, recordActiveColor, Graphics::kTextAlignCenter);
					}

					g_system->copyRectToScreen(recordSurface.getPixels(), recordSurface.pitch, 0, 0, recordSurface.w, recordSurface.h);
					g_system->updateScreen();
					lastMousePos = mousePos;
					redrawRecord = false;
				}

				pollEvents();
				const Common::Point currentMousePos = getMousePos();
				if (currentMousePos != lastMousePos) {
					redrawRecord = true;
					continue;
				}

				const Common::KeyCode keycode = getNextKey().keycode;
				if (keycode == Common::KEYCODE_ESCAPE || keycode == Common::KEYCODE_BACKSPACE) {
					backToSummary = true;
					break;
				} else if (keycode == Common::KEYCODE_LEFT) {
					if (currentRecordIndex > 0) {
						--currentRecordIndex;
						scrollOffset = 0;
						reloadRecord = true;
					}
				} else if (keycode == Common::KEYCODE_RIGHT || keycode == Common::KEYCODE_SPACE) {
					if (currentRecordIndex + 1 < (int)themeRecords.size()) {
						++currentRecordIndex;
						scrollOffset = 0;
						reloadRecord = true;
					}
				} else if (keycode == Common::KEYCODE_UP) {
					if (scrollOffset > 0)
						--scrollOffset;
					redrawRecord = true;
				} else if (keycode == Common::KEYCODE_DOWN) {
					if (scrollOffset < maxScroll)
						++scrollOffset;
					redrawRecord = true;
				}

				if (getCurrentMouseButton() == 1) {
					const Common::Point mouse = getMousePos();
					waitMouseRelease();
					bool handledClick = false;
					for (int i = 0; i < visibleLinkCount; ++i) {
						if (!linkButtons[i].contains(mouse))
							continue;

						const int linkRecordId = visibleLinkIds[i];
						const int linkedThemeIndex = findDocumentationThemeIndexForRecord(_documentationRecords, _documentationTree, linkRecordId);
						if (linkedThemeIndex >= 0) {
							Common::Array<int> linkedThemeRecords;
							collectDocumentationLeafRecords(_documentationRecords, _documentationTree,
							                                kEgyptDocumentationThemeIds[linkedThemeIndex], linkedThemeRecords);
							const int linkedRecordIndex = findDocumentationRecordIndex(linkedThemeRecords, linkRecordId);
							if (linkedRecordIndex >= 0) {
								selectedTheme = linkedThemeIndex;
								themeRecords = linkedThemeRecords;
								currentRecordIndex = linkedRecordIndex;
								scrollOffset = 0;
								reloadRecord = true;
								handledClick = true;
								break;
							}
						}
					}

					if (handledClick) {
						// Continue inside the current record loop with the new fiche context.
					} else if (backButton.contains(mouse)) {
						backToSummary = true;
						redrawRecord = true;
					} else if (prevButton.contains(mouse)) {
						if (currentRecordIndex > 0) {
							--currentRecordIndex;
							scrollOffset = 0;
							reloadRecord = true;
						}
					} else if (nextButton.contains(mouse)) {
						if (currentRecordIndex + 1 < (int)themeRecords.size()) {
							++currentRecordIndex;
							scrollOffset = 0;
							reloadRecord = true;
						}
					} else if (textPanel.contains(mouse)) {
						if (mouse.y >= textPanel.top + textPanel.height() / 2) {
							if (scrollOffset < maxScroll)
								++scrollOffset;
						} else if (scrollOffset > 0) {
							--scrollOffset;
						}
						redrawRecord = true;
					} else if (footerPanel.contains(mouse)) {
						if (mouse.x > footerPanel.left + footerPanel.width() / 2) {
							if (currentRecordIndex + 1 < (int)themeRecords.size()) {
								++currentRecordIndex;
								scrollOffset = 0;
								reloadRecord = true;
							}
						} else if (currentRecordIndex > 0) {
							--currentRecordIndex;
							scrollOffset = 0;
							reloadRecord = true;
						} else {
							backToSummary = true;
						}
					}
				}

				if (reloadRecord)
					break;
				if (!backToSummary) {
					if (redrawRecord)
						g_system->updateScreen();
					g_system->delayMillis(10);
				}
			}
		}
	}

	clearKeys();
	waitMouseRelease();
	showMouse(false);
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
