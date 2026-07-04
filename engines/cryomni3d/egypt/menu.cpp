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

#include "common/debug.h"
#include "common/file.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "graphics/font.h"
#include "graphics/managed_surface.h"


#include "cryomni3d/egypt/engine.h"
#include "cryomni3d/egypt/support/image_loader.h"

namespace CryOmni3D {
namespace Egypt {

// ----------------------------------------------------------------------------
// Main menu - reverse-engineered from EGYPTE.EXE.
//
// The accueil menu lives in the state machine dispatched at 0x80ed40 on the
// state global 0x4c2060 (states 1..6 = startup variant on a fresh session,
// states 0x64..0x67 = in-game variant while a game is running). Background
// is SPRITE/ACC_FR.TGA (pushed at 0x80ed6a, drawn to the back buffer and
// copied to screen). Entries are drawn with CRYO font slot 2 (FONT03.CRF)
// at x=248, first y=276, step 30; a bullet sprite (INTERFAC 204, 205 when
// hovered) sits at x=224, label y+4. Hover/click rects are (224, y, 160x16)
// tested via 0x819f20/0x819f60. Colors: white 0x4d9974, hover orange
// 0x4d6070 = RGB(224,112,0).
//
// The load/save list screen is the function at 0x80fe40: header label in
// orange at (248,236), up to 10 save rows at (248, 276+i*15), "..." scroll
// zones at (248,256) and (248,426) (hover-dwell scrolls, 0x81a7b0 >= 10
// ticks), "Annuler" at (248,456). Save-name records are 20 bytes in the EXE
// (0x4c2098, stride 0x14) - same length as kSaveDescriptionLen.
//
// Detailed notes: devtools-egypt/menu_reverse_notes.md
// ----------------------------------------------------------------------------

namespace {

const bool kEgyptStartupDebugStoryEntryEnabled = false;
const char *const kEgyptStartupDebugStoryEntryScene = "S01";

// Labels, in EGYPTE.DEF order (runtime array 0x4cabc8.. in the EXE)
enum EgyptMenuLabel {
	kLabelStartGame = 0,   // Commencer le jeu
	kLabelLoadGame,        // Charger une partie
	kLabelVisit,           // Visiter le site
	kLabelDocumentation,   // Consulter l'espace documentaire
	kLabelOptions,         // Options
	kLabelQuit,            // Quitter le jeu
	kLabelCancel,          // Annuler
	kLabelSaveGame,        // Sauvegarder la partie
	kLabelResume,          // Reprendre la partie
	kLabelAbandon,         // Abandonner la partie
	kLabelCount
};

const char *const kMenuLabelDefaults[kLabelCount] = {
	"Commencer le jeu",
	"Charger une partie",
	"Visiter le site",
	"Consulter l'espace documentaire",
	"Options",
	"Quitter le jeu",
	"Annuler",
	"Sauvegarder la partie",
	"Reprendre la partie",
	"Abandonner la partie"
};

// Menu geometry (EXE 0x80ed40 accueil state machine)
const int kMenuTextX          = 248;  // 0x80ee9b: push 0xf8
const int kMenuBulletX        = 224;  // 0x80eddf: push 0xe0
const int kMenuFirstY         = 276;  // 0x80eea0: push 0x114
const int kMenuStepY          = 30;   // 0x80edf3: add esi,0x1e
const int kMenuHitW           = 160;  // 0x80ee0d: push 0xa0
const int kMenuHitH           = 16;   // 0x80ee12: push 0x10
const int kMenuBulletYOffset  = 4;    // bullets at 0x118 vs labels 0x114
const int kSpriteBullet        = 204; // 0xcc (INTERFAC.SPR)
const int kSpriteBulletHovered = 205; // 0xcd

// Load/save list screen geometry (EXE 0x80fe40)
const int kListHeaderY     = 236;  // 0x80fea5: push 0xec (orange header)
const int kListDotsTopY    = 256;  // 0x80ff4e: push 0x100
const int kListFirstY      = 276;  // 0x810105: add edx,0x114
const int kListStepY       = 15;   // 0x8101d3: add esi,0xf
const int kListDotsBottomY = 426;  // 0x80fffc: push 0x1aa
const int kListCancelY     = 456;  // 0x810094: push 0x1c8
const int kListHitW        = 128;  // 0x80fee1: push 0x80
const int kListRowHitH     = 10;   // 0x8100ee: push 0xa
const int kListDotsHitH    = 14;   // 0x80fef9: push 0xe
const int kListVisibleRows = 10;   // 0x8100af: lea ecx,[eax+0xa]
const int kMaxSaveSlots    = 90;   // 0x8101e9: cmp eax,0x5a
const int kListScrollDwellTicks = 10; // 0x80ff2f: cmp eax,0xa (0x81a7b0 ticks)

// Startup menu entries (EXE states 2/3: labels 0x4cabc8..0x4cabdc)
const EgyptMenuLabel kStartupEntries[] = {
	kLabelStartGame, kLabelLoadGame, kLabelVisit,
	kLabelDocumentation, kLabelOptions, kLabelQuit
};
// In-game menu entries (EXE states 0x65..: labels 0x4cac08/0x4cabcc/0x4cac0c/
// 0x4cac10/0x4cabd8 - no Quitter, Abandonner returns to the startup menu)
const EgyptMenuLabel kIngameEntries[] = {
	kLabelSaveGame, kLabelLoadGame, kLabelResume,
	kLabelAbandon, kLabelOptions
};

// Blit one interface sprite (RGB565, masked) onto a 32bpp surface.
// Same routine as toolbar.cpp's blitEgyptSprite (top-left anchored, which is
// the EXE unscaled draw path 0x8193a9 used for menu bullets and toolbar).
void blitMenuSprite(const EgyptInterfaceSprite &sprite, Graphics::ManagedSurface &dst,
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

} // End of anonymous namespace

bool CryOmni3DEngine_Egypt::loadMenuLabels() {
	if (_menuLabelsLoaded)
		return true;

	_menuLabels.clear();
	for (uint i = 0; i < kLabelCount; ++i)
		_menuLabels.push_back(kMenuLabelDefaults[i]);

	// EGYPTE.DEF carries the localized text (the EXE loads all "message"
	// lines into the 0x4cab.. pointer array); on the FR CD they equal the
	// defaults, other language versions would override them here.
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

		for (uint i = 0; i < kLabelCount; ++i) {
			if (text.equalsIgnoreCase(kMenuLabelDefaults[i])) {
				_menuLabels[i] = text;
				break;
			}
		}
	}

	_menuLabelsLoaded = true;
	return true;
}

bool CryOmni3DEngine_Egypt::loadMessageLabels() {
	if (_messageLabelsLoaded)
		return true;

	_messageLabels.clear();
	_orderedMessages.clear();

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

		// Keep every message in file order (EXE pointer array 0x4cab88);
		// the toolbar clue list uses entries 0..15 (EXE 0x808c3c).
		_orderedMessages.push_back(payload);

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

// Read back the description of one save slot (first kSaveDescriptionLen
// bytes of the file, zero-padded - the layout the EXE also uses, 20 bytes)
Common::String CryOmni3DEngine_Egypt::getSaveDescription(int slot) const {
	Common::InSaveFile *in = _saveFileMan->openForLoading(getSaveStateName(slot));
	if (!in)
		return Common::String();

	char desc[kSaveDescriptionLen + 1];
	desc[kSaveDescriptionLen] = '\0';
	if (in->read(desc, kSaveDescriptionLen) != kSaveDescriptionLen)
		desc[0] = '\0';
	delete in;
	return Common::String(desc);
}

// Load/save list screen - EXE 0x80fe40.
// Returns the selected slot (0-based) or -1 on cancel/abort.
int CryOmni3DEngine_Egypt::runSaveListScreen(bool saveMode, const Graphics::ManagedSurface *background) {
	Common::Array<Common::String> descriptions;
	descriptions.resize(kMaxSaveSlots);
	for (int i = 0; i < kMaxSaveSlots; ++i)
		descriptions[i] = getSaveDescription(i);

	Graphics::ManagedSurface surface(kScreenWidth, kScreenHeight, g_system->getScreenFormat());
	Egypt_FontManager &fm = _fontManager;
	const uint32 white  = surface.format.RGBToColor(255, 255, 255);
	const uint32 orange = surface.format.RGBToColor(224, 112, 0); // 0x4d6070

	int scrollOffset = 0;  // EXE global 0x4c288c
	int dwellTicks = 0;    // EXE tick counter 0x81a7b0: >= 10 hovering "..." scrolls

	const Common::String header = _menuLabels[saveMode ? kLabelSaveGame : kLabelLoadGame];
	const Common::String dots = "...";  // EXE string VA 0x4350f8

	while (!shouldAbort()) {
		if (background)
			surface.blitFrom(*background);
		else
			surface.clear(surface.format.RGBToColor(0, 0, 0));

		const Common::Point mouse = getMousePos();
		const bool canScrollUp   = scrollOffset > 0;
		const bool canScrollDown = scrollOffset + 1 < kMaxSaveSlots; // EXE caps offset at 0x5a
		const bool hoverDotsTop = canScrollUp &&
		    Common::Rect(kMenuTextX, kListDotsTopY, kMenuTextX + kListHitW,
		                 kListDotsTopY + kListDotsHitH).contains(mouse);
		const bool hoverDotsBottom = canScrollDown &&
		    Common::Rect(kMenuTextX, kListDotsBottomY, kMenuTextX + kListHitW,
		                 kListDotsBottomY + kListDotsHitH).contains(mouse);
		const bool hoverCancel =
		    Common::Rect(kMenuTextX, kListCancelY, kMenuTextX + kListHitW,
		                 kListCancelY + kMenuHitH).contains(mouse);

		fm.setCurrentFont(Egypt_FontManager::kSlotMenu); // font 2 throughout (0x80fe40)

		// Header always orange (EXE 0x80fe7a sets 0x4d6070 before the header)
		fm.setForeColor(orange);
		fm.displayStr(surface, kMenuTextX, kListHeaderY, header);

		if (canScrollUp) {
			fm.setForeColor(hoverDotsTop ? orange : white);
			fm.displayStr(surface, kMenuTextX, kListDotsTopY, dots);
		}

		int hoveredRow = -1;
		for (int i = 0; i < kListVisibleRows; ++i) {
			const int slot = scrollOffset + i;
			if (slot >= kMaxSaveSlots)
				break;
			const int y = kListFirstY + i * kListStepY;
			const bool rowHovered =
			    Common::Rect(kMenuTextX, y, kMenuTextX + kListHitW,
			                 y + kListRowHitH).contains(mouse);
			if (rowHovered)
				hoveredRow = slot;
			if (descriptions[slot].empty())
				continue; // empty rows are blank (EXE skips names with a NUL first byte)
			fm.setForeColor(rowHovered ? orange : white);
			fm.displayStr(surface, kMenuTextX, y, descriptions[slot]);
		}

		if (canScrollDown) {
			fm.setForeColor(hoverDotsBottom ? orange : white);
			fm.displayStr(surface, kMenuTextX, kListDotsBottomY, dots);
		}

		fm.setForeColor(hoverCancel ? orange : white);
		fm.displayStr(surface, kMenuTextX, kListCancelY, _menuLabels[kLabelCancel]);

		g_system->copyRectToScreen(surface.getPixels(), surface.pitch, 0, 0, surface.w, surface.h);
		g_system->updateScreen();
		g_system->delayMillis(10);
		pollEvents();

		// Hover-dwell scrolling (EXE: 0x81a7b0 tick counter reaches 10)
		if (hoverDotsTop || hoverDotsBottom) {
			if (++dwellTicks >= kListScrollDwellTicks) {
				dwellTicks = 0;
				scrollOffset += hoverDotsBottom ? 1 : -1;
			}
		} else {
			dwellTicks = 0;
		}

		if (getCurrentMouseButton() == 1) {
			waitMouseRelease();
			if (hoverCancel)
				return -1;
			if (hoveredRow >= 0) {
				// Load mode requires an existing save; save mode accepts
				// empty rows too (new save at that slot)
				if (saveMode || !descriptions[hoveredRow].empty())
					return hoveredRow;
			}
		}

		if (getNextKey().keycode == Common::KEYCODE_ESCAPE)
			return -1;
	}

	return -1;
}

void CryOmni3DEngine_Egypt::playStartupLogoIfPresent() {
	// EXE 0x4075fe: hardcoded startup sequence "logo" then "r1".
	// R1.HNS is HNM6 640x480 with embedded CRYO_APC audio in AA chunk (22050 Hz stereo).
	// Path resolution: HNM/<name>.HNS -> HNM/FR/<name>.HNS.
	static const char *const kStartupEntries[] = { "logo", "r1" };
	for (uint i = 0; i < ARRAYSIZE(kStartupEntries); ++i) {
		if (shouldAbort())
			break;
		const Common::Path path = getFilePath(kFileTypeHnm, kStartupEntries[i]);
		if (path.empty()) {
			warning("Egypt: startup HNS '%s' not found in HNM/ or HNM/FR/", kStartupEntries[i]);
			continue;
		}
		debugC(kDebugFile, "Egypt: startup: playing %s", path.toString(Common::Path::kNativeSeparator).c_str());
		playHNM(path, Audio::Mixer::kMusicSoundType);
		clearKeys();
		waitMouseRelease();
	}
}

CryOmni3DEngine_Egypt::EgyptStartupMode CryOmni3DEngine_Egypt::showMainMenu() {
	loadMenuLabels();

	// EXE 0x80ed60: ACC_FR.TGA drawn to the back buffer, copied to screen
	Graphics::ManagedSurface background;
	const bool hasBackground = loadTgaImage(getFilePath(kFileTypeSpriteImage, "ACC_FR.TGA"), background, true);
	if (!hasBackground)
		warning("Egypt: menu background SPRITE/ACC_FR.TGA not found");

	Graphics::ManagedSurface surface(kScreenWidth, kScreenHeight, g_system->getScreenFormat());
	Egypt_FontManager &fm = _fontManager;
	const uint32 white  = surface.format.RGBToColor(255, 255, 255); // 0x4d9974
	const uint32 orange = surface.format.RGBToColor(224, 112, 0);   // 0x4d6070

	showMouse(true);
	setInterfaceCursor(kEgyptCursorDefault); // EXE 0x810250: menu cursor = INTERFAC sprite 13
	clearKeys();
	waitMouseRelease();

	// Startup variant (EXE state 2) vs in-game variant (EXE state 0x65)
	bool inGameVariant = _isPlaying;

	while (!shouldAbort()) {
		const EgyptMenuLabel *entries = inGameVariant ? kIngameEntries : kStartupEntries;
		const int entryCount = inGameVariant ? ARRAYSIZE(kIngameEntries) : ARRAYSIZE(kStartupEntries);

		if (hasBackground)
			surface.blitFrom(background);
		else
			surface.clear(surface.format.RGBToColor(0, 0, 0));

		const Common::Point mouse = getMousePos();
		int hoveredEntry = -1;
		for (int i = 0; i < entryCount; ++i) {
			const int y = kMenuFirstY + i * kMenuStepY;
			// EXE 0x80ee0d..: hover rect (x=224, y, 160x16)
			if (Common::Rect(kMenuBulletX, y, kMenuBulletX + kMenuHitW, y + kMenuHitH).contains(mouse))
				hoveredEntry = i;
		}

		fm.setCurrentFont(Egypt_FontManager::kSlotMenu); // font 2 = FONT03.CRF

		for (int i = 0; i < entryCount; ++i) {
			const int y = kMenuFirstY + i * kMenuStepY;

			// Bullet sprite 204, 205 when hovered, at (224, y+4) (EXE 0x80edd5/0x80ee3c)
			const int sprId = (i == hoveredEntry) ? kSpriteBulletHovered : kSpriteBullet;
			if ((uint)sprId < _spriteLoader.interfaceSpriteCount())
				blitMenuSprite(_spriteLoader.interfaceSprite(sprId), surface,
				               kMenuBulletX, y + kMenuBulletYOffset);

			fm.setForeColor((i == hoveredEntry) ? orange : white);
			fm.displayStr(surface, kMenuTextX, y, _menuLabels[entries[i]]);
		}

		g_system->copyRectToScreen(surface.getPixels(), surface.pitch, 0, 0, surface.w, surface.h);
		g_system->updateScreen();
		g_system->delayMillis(10);
		pollEvents();

		if (getCurrentMouseButton() == 1 && hoveredEntry >= 0) {
			waitMouseRelease();
			const EgyptMenuLabel action = entries[hoveredEntry];
			switch (action) {
			case kLabelStartGame:
				showMouse(false);
				return EgyptStartupMode::kStory;
			case kLabelLoadGame: {
				const int slot = runSaveListScreen(false, hasBackground ? &background : nullptr);
				if (slot >= 0) {
					_pendingLoadSlot = slot;
					showMouse(false);
					return EgyptStartupMode::kMainMenu; // run() applies the pending load
				}
				break; // cancelled - back to the menu (EXE result 0x8002 -> state 1)
			}
			case kLabelVisit:
				showMouse(false);
				return EgyptStartupMode::kVisit;
			case kLabelDocumentation:
				showMouse(false);
				return EgyptStartupMode::kDocumentation;
			case kLabelOptions:
				// EXE state 6 -> options screen 0x810400 (display modes,
				// music...). Not ported: ScummVM options cover this.
				debugC(kDebugVariable, "EGYPT_MENU: options entry not ported (EXE 0x810400)");
				break;
			case kLabelQuit:
				showMouse(false);
				return EgyptStartupMode::kQuit;
			case kLabelSaveGame: {
				const int slot = runSaveListScreen(true, hasBackground ? &background : nullptr);
				if (slot >= 0) {
					// The EXE opens an inline name editor (0x8107f0); we
					// store a generated description instead for now.
					Common::String desc = _savedSceneName.empty()
					    ? Common::String("Egypte") : _savedSceneName;
					saveGameToSlot((uint)slot + 1, desc);
				}
				break;
			}
			case kLabelResume:
				showMouse(false);
				return EgyptStartupMode::kResume;
			case kLabelAbandon:
				// EXE: back to the startup variant (state 0, action cleared)
				_isPlaying = false;
				_savedSceneName.clear();
				inGameVariant = false;
				break;
			default:
				break;
			}
			clearKeys();
			waitMouseRelease();
		}

		const Common::KeyCode keycode = getNextKey().keycode;
		if (keycode == Common::KEYCODE_ESCAPE) {
			showMouse(false);
			return inGameVariant ? EgyptStartupMode::kResume : EgyptStartupMode::kQuit;
		}
		// Debug shortcuts (port-only, not in the EXE menu)
		if (keycode >= Common::KEYCODE_F1 && keycode <= Common::KEYCODE_F6) {
			showMouse(false);
			return (EgyptStartupMode)((int)EgyptStartupMode::kDebugLevel1 +
			                          (keycode - Common::KEYCODE_F1));
		}
	}

	showMouse(false);
	return EgyptStartupMode::kQuit;
}

Common::String CryOmni3DEngine_Egypt::startStoryModePrototype() {
	resetGameVariables();
	clearPendingWarpRequest();
	_pendingWarpTarget.clear();
	_isPlaying = true;
	_savedSceneName.clear();
	_gameVariables[GameVariables::kFlagVisite] = 0;
	_gameVariables[GameVariables::kMain] = 0;
	_gameVariables[GameVariables::kLevel] = 1;

	// EXE 0x4076fa/0x407704: hardcoded angles before the menu loop.
	// NUIT inherits these angles without applying centering.
	setRuntimeViewAngles(1.570000052, 0.425999999, true);

	Common::String entryScene = "NUIT";
	if (kEgyptStartupDebugStoryEntryEnabled)
		entryScene = kEgyptStartupDebugStoryEntryScene;

	debugC(kDebugVariable, "EGYPT_MENU: selection=Story entryScene=%s FlagVisite=0 Level=1", entryScene.c_str());
	return entryScene;
}

Common::String CryOmni3DEngine_Egypt::startDebugLevel(int level, const Common::String &scene) {
	resetGameVariables();
	clearPendingWarpRequest();
	_pendingWarpTarget.clear();
	_isPlaying = true;
	_savedSceneName.clear();
	_gameVariables[GameVariables::kFlagVisite] = 0;
	_gameVariables[GameVariables::kMain] = 0;
	_gameVariables[GameVariables::kLevel] = level;
	debugC(kDebugVariable, "EGYPT_MENU: selection=DebugLevel%d entryScene=%s", level, scene.c_str());
	return scene;
}

Common::String CryOmni3DEngine_Egypt::startVisitMode() {
	clearPendingWarpRequest();
	_pendingWarpTarget.clear();
	_gameVariables[GameVariables::kFlagVisite] = 1;
	_gameVariables[GameVariables::kMain] = 0;
	_gameVariables[GameVariables::kLevel] = 0;

	// Same hardcoded angles as story mode (EXE 0x4076fa/0x407704).
	// JOUR inherits these angles without applying centering.
	setRuntimeViewAngles(1.570000052, 0.425999999, true);

	debugC(kDebugVariable, "EGYPT_MENU: selection=Visit entryScene=JOUR FlagVisite=1 Level=0");
	return "JOUR";
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
