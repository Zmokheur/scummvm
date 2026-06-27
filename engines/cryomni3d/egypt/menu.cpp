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
#include <cstring>

#include "common/file.h"
#include "common/memstream.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/managed_surface.h"

#include "image/tga.h"

#include "cryomni3d/egypt/engine.h"
#include "cryomni3d/image/cpx5.h"

namespace CryOmni3D {
namespace Egypt {

namespace {

const bool kEgyptStartupDebugStoryEntryEnabled = false;
const char *const kEgyptStartupDebugStoryEntryScene = "S01";

enum EgyptMenuEntry {
	kEgyptMenuStory = 0,
	kEgyptMenuVisit = 1,
	kEgyptMenuDocumentation = 2,
	kEgyptMenuQuit = 3,
	kEgyptMenuDebugLevel1 = 4,
	kEgyptMenuDebugLevel2 = 5,
	kEgyptMenuDebugLevel3 = 6,
	kEgyptMenuDebugLevel4 = 7,
	kEgyptMenuDebugLevel5 = 8,
	kEgyptMenuDebugLevel6 = 9,
	kEgyptMenuCount = 10
};

static const char *const kDebugLevelScenes[] = { "S00", "D01", "A02", "N01A", "M01", "K43" };

void drawCenteredString(Graphics::ManagedSurface &surface, const Graphics::Font *font,
                        const Common::String &text, int y, uint32 color) {
	if (!font)
		return;

	const int x = MAX(0, (surface.w - font->getStringWidth(text)) / 2);
	font->drawString(&surface, text, x, y, surface.w, color);
}

} // End of anonymous namespace

bool CryOmni3DEngine_Egypt::loadMenuLabels() {
	if (_menuLabelsLoaded)
		return true;

	_menuLabels.clear();
	_menuLabels.push_back("Commencer le jeu");
	_menuLabels.push_back("Visiter le site");
	_menuLabels.push_back("Consulter l'espace documentaire");
	_menuLabels.push_back("Quitter le jeu");

	Common::File file;
	if (!file.open(Common::Path("REF/FR/EGYPTE.DEF"))) {
		warning("Egypt: failed to open EGYPTE.DEF while loading menu labels");
		_menuLabelsLoaded = true;
		return false;
	}

	while (!file.eos()) {
		Common::String line = file.readLine();
		line.trim();
		if (!line.hasPrefixIgnoreCase("message"))
			continue;

		Common::String text = line.substr(7);
		text.trim();
		if (text.empty())
			continue;

		if (text.equalsIgnoreCase("Commencer le jeu"))
			_menuLabels[kEgyptMenuStory] = text;
		else if (text.equalsIgnoreCase("Visiter le site"))
			_menuLabels[kEgyptMenuVisit] = text;
		else if (text.equalsIgnoreCase("Consulter l'espace documentaire"))
			_menuLabels[kEgyptMenuDocumentation] = text;
		else if (text.equalsIgnoreCase("Quitter le jeu"))
			_menuLabels[kEgyptMenuQuit] = text;
	}

	_menuLabelsLoaded = true;
	return true;
}

bool CryOmni3DEngine_Egypt::loadMessageLabels() {
	if (_messageLabelsLoaded)
		return true;

	_messageLabels.clear();

	Common::File file;
	if (!file.open(Common::Path("REF/FR/EGYPTE.DEF"))) {
		warning("Egypt: failed to open EGYPTE.DEF while loading message labels");
		_messageLabelsLoaded = true;
		return false;
	}

	while (!file.eos()) {
		Common::String line = file.readLine();
		line.trim();
		if (!line.hasPrefixIgnoreCase("message"))
			continue;

		Common::String payload = line.substr(7);
		payload.trim();
		if (payload.empty())
			continue;

		int separatorPos = payload.findFirstOf(" \t");
		if (separatorPos < 0)
			continue;

		Common::String messageId = payload.substr(0, separatorPos);
		Common::String text = payload.substr(separatorPos + 1);
		messageId.trim();
		text.trim();
		if (messageId.empty() || text.empty())
			continue;

		EgyptMessageEntry entry;
		entry.text = text;

		int suffixPos = text.find("//");
		if (suffixPos >= 0) {
			Common::String suffix = text.substr(suffixPos + 2);
			suffix.trim();
			char *endPtr = nullptr;
			const long documentationId = strtol(suffix.c_str(), &endPtr, 10);
			if (endPtr != suffix.c_str() && endPtr && *endPtr == '\0')
				entry.documentationId = (int)documentationId;

			entry.text = text.substr(0, suffixPos);
			entry.text.trim();
		}

		if (!entry.text.empty())
			_messageLabels[messageId] = entry;
	}

	_messageLabelsLoaded = true;
	return true;
}

Common::String CryOmni3DEngine_Egypt::resolveMessageLabel(const Common::String &messageId) const {
	if (messageId.empty())
		return Common::String();

	Common::HashMap<Common::String, EgyptMessageEntry, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo>::const_iterator it =
		_messageLabels.find(messageId);
	if (it != _messageLabels.end())
		return it->_value.text;

	return messageId;
}

Common::String CryOmni3DEngine_Egypt::getHoverTextForZone(const EgyptZone *zone) const {
	if (!zone)
		return Common::String();

	if (!zone->label.empty())
		return resolveMessageLabel(zone->label);

	return Common::String();
}

bool CryOmni3DEngine_Egypt::loadWrappedTgaSurface(const Common::Path &filename, Graphics::ManagedSurface &surface) const {
	Common::File file;
	if (!file.open(filename))
		return false;

	Common::Array<byte> decompressed;
	byte magic[4] = {0, 0, 0, 0};
	file.read(magic, sizeof(magic));
	file.seek(0);

	Image::TGADecoder decoder;
	bool decoded = false;
	if (memcmp(magic, "CPx5", sizeof(magic)) == 0) {
		if (!Image::Cpx5Decoder::decompress(file, decompressed)) {
			warning("Egypt: failed to decompress wrapped TGA %s",
			        filename.toString(Common::Path::kNativeSeparator).c_str());
			return false;
		}

		Common::MemoryReadStream stream(decompressed.data(), decompressed.size(), DisposeAfterUse::NO);
		decoded = decoder.loadStream(stream);
	} else {
		decoded = decoder.loadStream(file);
	}

	if (!decoded) {
		warning("Egypt: failed to decode TGA asset %s",
		        filename.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	Graphics::Surface *converted = nullptr;
	if (decoder.hasPalette())
		converted = decoder.getSurface()->convertTo(g_system->getScreenFormat(), decoder.getPalette().data(), decoder.getPalette().size());
	else
		converted = decoder.getSurface()->convertTo(g_system->getScreenFormat());

	if (!converted)
		return false;

	surface.create(640, 480, g_system->getScreenFormat());
	surface.clear(surface.format.RGBToColor(0, 0, 0));
	surface.blitFrom(*converted, Common::Rect(0, 0, converted->w, converted->h), Common::Rect(0, 0, 640, 480));
	delete converted;
	return true;
}

bool CryOmni3DEngine_Egypt::loadWrappedTgaRaw(const Common::Path &filename, Graphics::ManagedSurface &surface) const {
	Common::File file;
	if (!file.open(filename))
		return false;

	Common::Array<byte> decompressed;
	byte magic[4] = {0, 0, 0, 0};
	file.read(magic, sizeof(magic));
	file.seek(0);

	Image::TGADecoder decoder;
	bool decoded = false;
	if (memcmp(magic, "CPx5", sizeof(magic)) == 0) {
		if (!Image::Cpx5Decoder::decompress(file, decompressed))
			return false;
		Common::MemoryReadStream stream(decompressed.data(), decompressed.size(), DisposeAfterUse::NO);
		decoded = decoder.loadStream(stream);
	} else {
		decoded = decoder.loadStream(file);
	}
	if (!decoded)
		return false;

	Graphics::Surface *converted = nullptr;
	if (decoder.hasPalette())
		converted = decoder.getSurface()->convertTo(g_system->getScreenFormat(), decoder.getPalette().data(), decoder.getPalette().size());
	else
		converted = decoder.getSurface()->convertTo(g_system->getScreenFormat());
	if (!converted)
		return false;

	surface.create(converted->w, converted->h, g_system->getScreenFormat());
	surface.blitFrom(*converted);
	delete converted;
	return true;
}

void CryOmni3DEngine_Egypt::drawSimpleScreen(const Common::String &title, const Common::Array<Common::String> &lines,
                                             int selectedLine, const Graphics::ManagedSurface *background) const {
	Graphics::ManagedSurface surface(640, 480, g_system->getScreenFormat());
	if (background)
		surface.blitFrom(*background);
	else
		surface.clear(surface.format.RGBToColor(0, 0, 0));

	const uint32 panelColor = surface.format.RGBToColor(12, 12, 12);
	const uint32 borderColor = surface.format.RGBToColor(180, 150, 70);
	const uint32 textColor = surface.format.RGBToColor(240, 232, 210);
	const uint32 highlightColor = surface.format.RGBToColor(255, 210, 90);
	const uint32 dimColor = surface.format.RGBToColor(160, 160, 160);

	const Common::Rect panel(48, 210, 592, 448);
	surface.fillRect(panel, panelColor);
	surface.frameRect(panel, borderColor);

	const Graphics::Font *titleFont = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	const Graphics::Font *bodyFont = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!titleFont)
		titleFont = bodyFont;

	drawCenteredString(surface, titleFont, title, 230, highlightColor);

	int y = 282;
	for (uint i = 0; i < lines.size(); ++i, y += 30) {
		const uint32 color = ((int)i == selectedLine) ? highlightColor : textColor;
		drawCenteredString(surface, bodyFont, lines[i], y, color);
	}

	drawCenteredString(surface, bodyFont, "Esc: quitter / clic: choisir", 414, dimColor);

	g_system->copyRectToScreen(surface.getPixels(), surface.pitch, 0, 0, surface.w, surface.h);
	g_system->updateScreen();
}

void CryOmni3DEngine_Egypt::drawMenuScreen(Graphics::ManagedSurface &surface, int hoveredEntry, bool hasBackground) const {
	if (!hasBackground)
		surface.clear(surface.format.RGBToColor(0, 0, 0));

	const uint32 panelColor = hasBackground ? surface.format.RGBToColor(18, 18, 18) : surface.format.RGBToColor(0, 0, 0);
	const uint32 borderColor = surface.format.RGBToColor(164, 124, 48);
	const uint32 titleColor = surface.format.RGBToColor(232, 197, 104);
	const uint32 textColor = surface.format.RGBToColor(240, 233, 214);
	const uint32 hoverColor = surface.format.RGBToColor(255, 214, 96);
	const uint32 hintColor = surface.format.RGBToColor(180, 180, 180);

	const Common::Rect panel(36, 248, 398, 442);
	surface.fillRect(panel, panelColor);
	surface.frameRect(panel, borderColor);

	const Graphics::Font *titleFont = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	const Graphics::Font *bodyFont = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!titleFont)
		titleFont = bodyFont;

	const Common::String title = hasBackground ? "Menu principal" : "Egypt 1156";
	if (titleFont)
		titleFont->drawString(&surface, title, 56, 266, 320, titleColor);

	const char *const hotkeys[kEgyptMenuDebugLevel1] = { "1", "2", "3", "Esc" };
	int y = 304;
	for (int i = 0; i < kEgyptMenuDebugLevel1; ++i, y += 30) {
		const uint32 color = (i == hoveredEntry) ? hoverColor : textColor;
		Common::String line = Common::String::format("[%s] %s", hotkeys[i], _menuLabels[i].c_str());
		if (bodyFont)
			bodyFont->drawString(&surface, line, 58, y, 320, color);
	}

	if (bodyFont)
		bodyFont->drawString(&surface, hasBackground ? "ACC_FR.TGA + libelles du jeu" : "Fallback menu", 58, 410, 320, hintColor);

	const uint32 debugBorderColor = surface.format.RGBToColor(60, 140, 60);
	const uint32 debugTitleColor = surface.format.RGBToColor(100, 210, 100);

	const Common::Rect debugPanel(410, 240, 632, 452);
	surface.fillRect(debugPanel, panelColor);
	surface.frameRect(debugPanel, debugBorderColor);

	if (titleFont)
		titleFont->drawString(&surface, "DEBUG", 426, 258, 200, debugTitleColor);

	int dy = 292;
	for (int i = kEgyptMenuDebugLevel1; i < kEgyptMenuCount; ++i, dy += 25) {
		const int level = i - kEgyptMenuDebugLevel1 + 1;
		const uint32 color = (i == hoveredEntry) ? hoverColor : textColor;
		Common::String line = Common::String::format("[F%d] L%d -> %s", level, level, kDebugLevelScenes[i - kEgyptMenuDebugLevel1]);
		if (bodyFont)
			bodyFont->drawString(&surface, line, 422, dy, 204, color);
	}
}

void CryOmni3DEngine_Egypt::playStartupLogoIfPresent() {
	const Common::Path logoPath("HNM/LOGO.HNS");
	if (!Common::File::exists(logoPath)) {
		warning("EGYPT_MENU: logo=missing path=%s",
		        logoPath.toString(Common::Path::kNativeSeparator).c_str());
		return;
	}

	warning("EGYPT_MENU: logo=HNM/LOGO.HNS status=likely");
	playHNM(logoPath, Audio::Mixer::kMusicSoundType);
	clearKeys();
	waitMouseRelease();
}

CryOmni3DEngine_Egypt::EgyptStartupMode CryOmni3DEngine_Egypt::showMainMenu() {
	loadMenuLabels();

	Graphics::ManagedSurface background;
	const bool hasBackground = loadWrappedTgaSurface(Common::Path("SPRITE/ACC_FR.TGA"), background);
	if (hasBackground) {
		warning("EGYPT_MENU: asset=SPRITE/ACC_FR.TGA type=CPx5_wrapped_tga status=likely");
	} else {
		warning("EGYPT_MENU: asset=temporary_text_menu reason=real menu asset not identified yet");
	}

	Graphics::ManagedSurface surface(640, 480, g_system->getScreenFormat());
	if (hasBackground)
		surface.blitFrom(background);
	else
		surface.clear(surface.format.RGBToColor(0, 0, 0));

	Common::Rect boxes[kEgyptMenuCount];
	int hoveredEntry = kEgyptMenuStory;
	for (int i = 0; i < kEgyptMenuDebugLevel1; ++i)
		boxes[i] = Common::Rect(52, 300 + i * 30, 366, 324 + i * 30);
	for (int i = kEgyptMenuDebugLevel1; i < kEgyptMenuCount; ++i) {
		const int di = i - kEgyptMenuDebugLevel1;
		boxes[i] = Common::Rect(414, 292 + di * 25, 628, 314 + di * 25);
	}

	showMouse(true);
	setInterfaceCursor(kEgyptCursorDefault);
	clearKeys();
	waitMouseRelease();

	bool redraw = true;
	while (!shouldAbort()) {
		if (redraw) {
			if (hasBackground)
				surface.blitFrom(background);
			else
				surface.clear(surface.format.RGBToColor(0, 0, 0));
			drawMenuScreen(surface, hoveredEntry, hasBackground);
			g_system->copyRectToScreen(surface.getPixels(), surface.pitch, 0, 0, surface.w, surface.h);
			redraw = false;
		}

		g_system->updateScreen();
		g_system->delayMillis(10);

		pollEvents();

		const Common::Point mouse = getMousePos();
		for (int i = 0; i < kEgyptMenuCount; ++i) {
			if (boxes[i].contains(mouse) && hoveredEntry != i) {
				hoveredEntry = i;
				redraw = true;
			}
		}

		if (getCurrentMouseButton() == 1) {
			for (int i = 0; i < kEgyptMenuCount; ++i) {
				if (boxes[i].contains(mouse)) {
					waitMouseRelease();
					showMouse(false);
					switch (i) {
					case kEgyptMenuStory:
						return EgyptStartupMode::kStory;
					case kEgyptMenuVisit:
						return EgyptStartupMode::kVisit;
					case kEgyptMenuDocumentation:
						return EgyptStartupMode::kDocumentation;
					case kEgyptMenuDebugLevel1:
						return EgyptStartupMode::kDebugLevel1;
					case kEgyptMenuDebugLevel2:
						return EgyptStartupMode::kDebugLevel2;
					case kEgyptMenuDebugLevel3:
						return EgyptStartupMode::kDebugLevel3;
					case kEgyptMenuDebugLevel4:
						return EgyptStartupMode::kDebugLevel4;
					case kEgyptMenuDebugLevel5:
						return EgyptStartupMode::kDebugLevel5;
					case kEgyptMenuDebugLevel6:
						return EgyptStartupMode::kDebugLevel6;
					default:
						return EgyptStartupMode::kQuit;
					}
				}
			}
		}

		Common::KeyCode keycode = getNextKey().keycode;
		if (keycode == Common::KEYCODE_1 || keycode == Common::KEYCODE_KP1) {
			showMouse(false);
			return EgyptStartupMode::kStory;
		} else if (keycode == Common::KEYCODE_2 || keycode == Common::KEYCODE_KP2) {
			showMouse(false);
			return EgyptStartupMode::kVisit;
		} else if (keycode == Common::KEYCODE_3 || keycode == Common::KEYCODE_KP3) {
			showMouse(false);
			return EgyptStartupMode::kDocumentation;
		} else if (keycode == Common::KEYCODE_ESCAPE || keycode == Common::KEYCODE_q) {
			showMouse(false);
			return EgyptStartupMode::kQuit;
		} else if (keycode == Common::KEYCODE_F1) {
			showMouse(false);
			return EgyptStartupMode::kDebugLevel1;
		} else if (keycode == Common::KEYCODE_F2) {
			showMouse(false);
			return EgyptStartupMode::kDebugLevel2;
		} else if (keycode == Common::KEYCODE_F3) {
			showMouse(false);
			return EgyptStartupMode::kDebugLevel3;
		} else if (keycode == Common::KEYCODE_F4) {
			showMouse(false);
			return EgyptStartupMode::kDebugLevel4;
		} else if (keycode == Common::KEYCODE_F5) {
			showMouse(false);
			return EgyptStartupMode::kDebugLevel5;
		} else if (keycode == Common::KEYCODE_F6) {
			showMouse(false);
			return EgyptStartupMode::kDebugLevel6;
		} else if (keycode == Common::KEYCODE_UP) {
			hoveredEntry = (hoveredEntry + kEgyptMenuCount - 1) % kEgyptMenuCount;
			redraw = true;
		} else if (keycode == Common::KEYCODE_DOWN) {
			hoveredEntry = (hoveredEntry + 1) % kEgyptMenuCount;
			redraw = true;
		} else if (keycode == Common::KEYCODE_RETURN || keycode == Common::KEYCODE_SPACE) {
			showMouse(false);
			switch (hoveredEntry) {
			case kEgyptMenuStory:
				return EgyptStartupMode::kStory;
			case kEgyptMenuVisit:
				return EgyptStartupMode::kVisit;
			case kEgyptMenuDocumentation:
				return EgyptStartupMode::kDocumentation;
			case kEgyptMenuDebugLevel1:
				return EgyptStartupMode::kDebugLevel1;
			case kEgyptMenuDebugLevel2:
				return EgyptStartupMode::kDebugLevel2;
			case kEgyptMenuDebugLevel3:
				return EgyptStartupMode::kDebugLevel3;
			case kEgyptMenuDebugLevel4:
				return EgyptStartupMode::kDebugLevel4;
			case kEgyptMenuDebugLevel5:
				return EgyptStartupMode::kDebugLevel5;
			case kEgyptMenuDebugLevel6:
				return EgyptStartupMode::kDebugLevel6;
			default:
				return EgyptStartupMode::kQuit;
			}
		}
	}

	showMouse(false);
	return EgyptStartupMode::kQuit;
}

Common::String CryOmni3DEngine_Egypt::startStoryModePrototype() {
	clearPendingWarpRequest();
	_pendingWarpTarget.clear();
	_scriptVariables["FlagVisite"] = 0;
	_scriptVariables["main"] = 0;
	_scriptVariables["Level"] = 1;

	Common::String entryScene = "S01";
	if (kEgyptStartupDebugStoryEntryEnabled)
		entryScene = kEgyptStartupDebugStoryEntryScene;

	warning("EGYPT_MENU: selection=Story entryScene=%s context=prototype reason=story_prototype_entry",
	        entryScene.c_str());
	warning("Egypt: starting story mode through prototype entry scene %s", entryScene.c_str());
	return entryScene;
}

Common::String CryOmni3DEngine_Egypt::startDebugLevel(int level, const Common::String &scene) {
	clearPendingWarpRequest();
	_pendingWarpTarget.clear();
	_scriptVariables["FlagVisite"] = 0;
	_scriptVariables["main"] = 0;
	_scriptVariables["Level"] = level;
	warning("EGYPT_MENU: selection=DebugLevel%d entryScene=%s", level, scene.c_str());
	return scene;
}

Common::String CryOmni3DEngine_Egypt::startVisitMode() {
	clearPendingWarpRequest();
	_pendingWarpTarget.clear();
	_scriptVariables["FlagVisite"] = 1;
	_scriptVariables["main"] = 0;
	_scriptVariables["Level"] = 0;
	_currentContextName = "JOUR";

	warning("EGYPT_MENU: selection=Visit entryScene=JOUR context=JOUR FlagVisite=1 Level=0 reason=visit_mode_no_story_chapter");
	return "JOUR";
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
