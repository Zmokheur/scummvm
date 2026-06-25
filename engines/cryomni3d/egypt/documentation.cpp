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

#include "common/system.h"
#include "common/textconsole.h"

#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/managed_surface.h"

#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

namespace {

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

void drawCenteredLine(Graphics::ManagedSurface &surface, const Graphics::Font *font,
                      const Common::String &text, int y, uint32 color) {
	if (!font)
		return;

	const int x = MAX(0, (surface.w - font->getStringWidth(text)) / 2);
	font->drawString(&surface, text, x, y, surface.w, color);
}

} // End of anonymous namespace

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

} // End of namespace Egypt
} // End of namespace CryOmni3D
