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
#include "common/endian.h"
#include "common/rect.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "graphics/font.h"
#include "graphics/managed_surface.h"
#include "graphics/surface.h"

#include "cryomni3d/egypt/cursor.h"
#include "cryomni3d/egypt/engine.h"
#include "cryomni3d/egypt/toolbar.h"
#include "cryomni3d/egypt/sprite.h"

namespace CryOmni3D {
namespace Egypt {

namespace {

// Toolbar layout - confirmed from EXE function 0x808080 / 0x808990
//
// Total size: 640x48 pixels, screen Y = 432 (= 480 - 48)
// Confirmed by: push 0x30 (=48) at 0x8089ae, offset 0x87000 (=640x432x2) at 0x8089b0
//
// Y offsets within the 48-pixel-tall toolbar strip (from EXE constants):
//   Sprite 15 (30x30 slot): 0x1eb - param -> at param=48 -> screen y=443 -> offset 11
//   Sprite 16/18 (20x20):   0x1f0 - param -> at param=48 -> screen y=448 -> offset 16
//   Sprite 19 (20x20):      0x1fc - param -> at param=48 -> screen y=460 -> offset 28 (=48-20, bottom-aligned)
//     Confirmed by static draw at absolute y=0x1cc=460 (0x808786) and hit-test (0x8087c4)
//
// X positions (EXE):
//   Sprite 19 (options):       x=0        (EXE 0x808f03: push edi=0)
//   Sprite 16 (view-item):     x=128=0x80 (EXE 0x808a4f: push 0x80)
//   Sprite 15 (inv slot x10):  x=160+i*46 (EXE 0x808cdc: mov ebx,0xa0; 0x808edc: add ebx,0x2e)
//   Sprite 18 (doc button):    x=617=0x269(EXE 0x808a6e: push 0x269)
//
// Animation: 12 frames, step=4px (param: 4->48), 10ms/frame
//   Counter at ds:0x47c5f8; initial=4 (0x8088bc); stop at 0x30=48 (0x808941)
//
// Navigation shortcuts (EXE 0x80b980 state machine):
//   Zone 0x70-0x75 = Windows VK_F1-VK_F6 -> scenes S00,D01,A02,N01A,M01,K43
//   Zone 0x76     = Windows VK_F7         -> dismiss
//
// Hovered slot label (EXE 0x808df2..0x808e97): with an empty hand, hovering
// a filled slot draws the object display name (DEF name table 0x4d1278) at
// (1, 0x1e1-param) in font 10, current color (white), plus an orange
// connector line at y=0x1e5-param from x=nameWidth+4 to slotX+15 and a 4 px
// vertical tick at (slotX+14, 0x1e6-param). param=48 at rest, so the label
// row is screen y=433. No label in visit mode: the slots are empty there
// and the EXE label is purely slot-content driven.
//
// Sprite 18 = visual clue list button (EXE hit 0x809131, flag 0x4c207c set
// at 0x809165, list drawn at 0x808f6c..0x809121): with an empty hand it
// opens a right-aligned list, above the toolbar, of the collected visual
// clues (variables IndiceVisuel01..16 walked by index at 0x808c05, names =
// first 16 DEF messages via pointer array 0x4cab88); clicking an entry
// warps to the eye scene named by table 0x435738 = "ALL\" + the suffix
// embedded in the variable name. The list closes as soon as the mouse
// leaves it (0x8090cc..0x809100) or on a click outside a row (0x809105).

static const int kSpriteSlot       = 15;  // 30x30 - empty inventory slot
static const int kSpriteLeft       = 16;  // 20x20 - view-item / eye button (inactive)
static const int kSpriteLeftActive = 17;  // 20x20 - eye button (active: main has eye action)
static const int kSpriteRight      = 18;  // 20x20 - visual clue list button (EXE 0x809131)
static const int kSpriteOptions    = 19;  // 20x20 - options / dismiss (bottom-aligned)

// Object icon sprite base: objectId + kItemIconBase (EXE: objectId + 0x92)
static const int kItemIconBase = 0x92;   // = 146

// Eye action table (EXE 0x435774) - maps objectId to a packed action word.
// Bit 15 set -> warp: bits 14..0 = index into kEyeWarpTargets[].
// Bit 15 clear -> documentation ID opened in the documentation viewer.
struct EgyptEyeEntry { int objectId; uint32 action; };

static const char *const kEyeWarpTargets[] = {
    "ALL/HYPOS",      // 0
    "ALL/LETTRE",     // 1
    "ALL/LISTE_NO",   // 2
    "ALL/OSTRAC_V",   // 3
    "ALL/OSTRAC_R",   // 4
    "ALL/CODE",       // 5
    "ALL/D89PAPY"     // 6
};

static const EgyptEyeEntry kEyeTable[] = {
    { 11, 363  },           { 14, 333  },
    { 15, 3810 },           { 20, 366  },
    { 23, 0x8000u | 0u },  { 24, 366  },
    { 25, 0x8000u | 1u },  { 26, 0x8000u | 2u },
    { 27, 331  },           { 28, 332  },
    { 29, 204  },           { 30, 0x8000u | 5u },
    { 35, 333  },           { 36, 388  },
    { 37, 389  },           { 38, 0x8000u | 3u },
    { 39, 387  },           { 40, 0x8000u | 6u },
    { 41, 332  },           { 42, 387  },
    { 48, 0x8000u | 4u },  { 50, 3811 },
    { 51, 352  },           { 52, 3814 },
    { 55, 331  },           { 57, 3812 }
};

static uint32 lookupEyeAction(int objectId) {
    for (uint i = 0; i < ARRAYSIZE(kEyeTable); ++i)
        if (kEyeTable[i].objectId == objectId)
            return kEyeTable[i].action;
    return 0;
}

static const int kToolbarH   = 48;  // toolbar height in pixels (EXE: push 0x30 at 0x8089ae)
static const int kYSlot      = 11;  // sprite 15 y-offset within toolbar (EXE: 0x1eb - 48 - 432 = 11)
static const int kYButtons   = 16;  // sprite 16/18 y-offset (EXE: 0x1f0 - 48 - 432 = 16)
static const int kYOptions   = 28;  // sprite 19 y-offset (EXE: 0x1fc - 48 - 432 = 28 = 48-20)

static const int kSlotX0     = 160; // first slot x (EXE: mov ebx, 0xa0)
static const int kSlotStep   = 46;  // slot x step  (EXE: add ebx, 0x2e)
static const int kSlotCount  = 10;  // number of slots (loop 0x4c35f8..0x4c3620 = 40 bytes / 4)
static const int kXOptions   = 0;   // sprite 19 x
static const int kXLeft      = 128; // sprite 16 x (EXE: push 0x80)
static const int kXRight     = 617; // sprite 18 x (EXE: push 0x269)

// Blit one toolbar sprite (RGB565, masked) onto a 32bpp destination surface
static void blitEgyptSprite(const EgyptInterfaceSprite &sprite, Graphics::ManagedSurface &dst,
                             int x, int y) {
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
			kEgyptSpriteFormat.colorToRGB(srcPixel, r, g, b);
			WRITE_LE_UINT32((byte *)dst.getBasePtr(dstX, dstY), dstFmt.RGBToColor(r, g, b));
		}
	}
}

// 50/50 average of each pixel with the dark tint {30, 25, 18} - EXE
// 0x817680 (params at 0x4352fc/0x435300/0x435304), used for the toolbar
// background (call 0x8089c5) and the clue list box (call 0x808fac). The
// EXE works on RGB565: r5' = (r5 + (30>>3)) >> 1, g6' = (g6 + (25>>2)) >> 1,
// b5' = (b5 + (18>>3)) >> 1.
static void blendDarken(Graphics::ManagedSurface &surface, const Common::Rect &rect) {
	const Graphics::PixelFormat &fmt = surface.format;
	assert(fmt.bytesPerPixel == 4);

	for (int y = rect.top; y < rect.bottom; y++) {
		if (y < 0 || y >= surface.h)
			continue;
		byte *row = (byte *)surface.getBasePtr(0, y);
		for (int x = rect.left; x < rect.right; x++) {
			if (x < 0 || x >= surface.w)
				continue;
			const uint32 pixel = READ_LE_UINT32(row + x * 4);
			uint8 r, g, b;
			fmt.colorToRGB(pixel, r, g, b);
			r = (uint8)((((r >> 3) + (30 >> 3)) >> 1) << 3);
			g = (uint8)((((g >> 2) + (25 >> 2)) >> 1) << 2);
			b = (uint8)((((b >> 3) + (18 >> 3)) >> 1) << 3);
			WRITE_LE_UINT32(row + x * 4, fmt.RGBToColor(r, g, b));
		}
	}
}

// Visual clue variables, in EGYPTE.DEF order right after "IndiceVisuel"
// (the EXE walks them by index, 0x808c05/0x808c2c). The eye scene opened on
// click is "ALL\" + the suffix embedded in the name (EXE string table
// 0x435738: IndiceVisuel01S03CART -> ALL\S03CART; 11..16 -> ALL\RIEN).
static const char *const kClueVars[16] = {
	"IndiceVisuel01S03CART", "IndiceVisuel02S06DJED",  "IndiceVisuel03S06TIT",
	"IndiceVisuel04S08DJAT", "IndiceVisuel05S40TRUIE", "IndiceVisuel06S44PTAH",
	"IndiceVisuel07D63CHEV", "IndiceVisuel08A21HERI",  "IndiceVisuel09N03CHAT",
	"IndiceVisuel10N07SENET", "IndiceVisuel11", "IndiceVisuel12",
	"IndiceVisuel13", "IndiceVisuel14", "IndiceVisuel15", "IndiceVisuel16"
};

} // End of anonymous namespace

// ----------------------------------------------------------------------------

void CryOmni3DEngine_Egypt::makeTranslucent(Graphics::Surface &dst,
        const Graphics::Surface &src) const {
	assert(dst.w == src.w && dst.h == src.h);
	assert(src.format.bytesPerPixel == 4);

	for (int y = 0; y < src.h; y++) {
		const byte *srcRow = (const byte *)src.getBasePtr(0, y);
		byte       *dstRow = (byte *)dst.getBasePtr(0, y);
		for (int x = 0; x < src.w; x++) {
			const uint32 srcPixel = READ_LE_UINT32(srcRow + x * 4);
			uint8 r, g, b;
			src.format.colorToRGB(srcPixel, r, g, b);
			WRITE_LE_UINT32(dstRow + x * 4, dst.format.RGBToColor(r >> 1, g >> 1, b >> 1));
		}
	}
}

// ----------------------------------------------------------------------------

bool Egypt_Toolbar::display(const Graphics::Surface *original) {
	if (_engine->_spriteLoader.interfaceSpriteCount() <= (uint)kSpriteOptions)
		return false;

	// FlagVisite drives the visit cursor on site slots (EXE 0x8085b7 via the
	// zone hover state machine 0x808080) and empties the story-specific
	// content; the sprite layout (0x8089d0) is the same in both modes.
	const bool inVisitMode = (_engine->getScriptVariableValue("FlagVisite") != 0);

	// Working surfaces: 640 wide, kToolbarH (48) tall
	Graphics::ManagedSurface bgSurface(640, kToolbarH, g_system->getScreenFormat());
	Graphics::ManagedSurface dest(640, kToolbarH, g_system->getScreenFormat());

	// Build translucent background from the bottom 48 rows of the current scene
	if (original && original->w >= 640 && original->h >= 480) {
		const Common::Rect srcRect(0, original->h - kToolbarH, 640, original->h);
		bgSurface.copyRectToSurface(*original, 0, 0, srcRect);
		// EXE 0x8089c5: the strip is darkened by averaging each pixel with
		// {30, 25, 18} (0x817680), not by halving the channels
		blendDarken(bgSurface, Common::Rect(0, 0, 640, kToolbarH));
	}

	// Toolbar top: y=432 on a 480-pixel screen (= 480 - 48)
	const int screenY = kScreenHeight - kToolbarH;

	// Hit rects in screen coordinates - confirmed from EXE hit-tests (0x819f60 calls)
	// Sprite 19: EXE 0x8087c4 push 0x14,0x14,edi(0),0x1cc(460) -> 20x20 at (0,460)
	// Sprite 16: EXE 0x808aab push 0x14,0x14,0x80(128),ebp     -> 20x20 at (128, screenY+kYButtons)
	// Sprite 18: EXE 0x809131 push 0x14,0x14,0x269(617),ebp    -> 20x20 at (617, screenY+kYButtons)
	// Slots:     EXE 0x808e80 push 0x1e(30),0x1e(30),x,y       -> 30x30 per slot
	const Common::Rect kRectOptions(kXOptions,                   screenY + kYOptions,
	                                kXOptions + 20,              screenY + kYOptions + 20);
	const Common::Rect kRectLeft(   kXLeft,                      screenY + kYButtons,
	                                kXLeft + 20,                 screenY + kYButtons + 20);
	const Common::Rect kRectRight(  kXRight,                     screenY + kYButtons,
	                                kXRight + 20,                screenY + kYButtons + 20);

	// Helper: blit one interface sprite at a given x, y-offset within the toolbar strip.
	// `position` = number of toolbar rows still hidden (0 = fully visible, kToolbarH = fully hidden).
	auto blitSpr = [&](int sprId, int x, int yOff, int position) {
		if (sprId < 0 || (uint)sprId >= _engine->_spriteLoader.interfaceSpriteCount())
			return;
		const EgyptInterfaceSprite &spr = _engine->_spriteLoader.interfaceSprite(sprId);
		blitEgyptSprite(spr, dest, x, position + yOff);
	};

	// Index of the slot currently under the mouse (-1 = none).
	// Used by drawFrame to draw the visit-mode label and by the event loop to set the cursor.
	int hoveredSlot = -1;

	// Draw the toolbar at a given slide position (0 = fully visible, kToolbarH = hidden).
	auto drawFrame = [&](int position) {
		if (position > 0 && original && original->w >= 640 && original->h >= 480) {
			const Common::Rect srcRect(0, screenY, 640, screenY + position);
			dest.copyRectToSurface(*original, 0, 0, srcRect);
		}

		if (position < kToolbarH) {
			const Common::Rect bgRect(0, position, 640, kToolbarH);
			dest.copyRectToSurface(bgSurface.rawSurface(), 0, position, bgRect);

			// Sprite layout (EXE 0x8089d0).
			blitSpr(kSpriteOptions, kXOptions, kYOptions, position);

			// Eye button: active (17) in story mode when main object has an eye action.
			if (!inVisitMode) {
				const int mainId = _engine->getScriptVariableValue("main");
				const uint32 eyeAct = (mainId != 0) ? lookupEyeAction(mainId) : 0u;
				blitSpr(eyeAct != 0 ? kSpriteLeftActive : kSpriteLeft, kXLeft, kYButtons, position);
			} else {
				blitSpr(kSpriteLeft, kXLeft, kYButtons, position);
			}

			// Inventory slots: empty slot background + item icon on top.
			for (int i = 0; i < kSlotCount; i++) {
				blitSpr(kSpriteSlot, kSlotX0 + i * kSlotStep, kYSlot, position);
				if (!inVisitMode) {
					const int slotId = _engine->getScriptVariableValue(
					    Common::String::format("inventaire%d", i));
					if (slotId > 0) {
						const int iconSpr = slotId + kItemIconBase;
						if ((uint)iconSpr < _engine->_spriteLoader.interfaceSpriteCount())
							blitSpr(iconSpr, kSlotX0 + i * kSlotStep, kYSlot, position);
					}
				}
			}

			blitSpr(kSpriteRight, kXRight, kYButtons, position);

			// Hovered slot label (EXE 0x808df2..0x808e97): with an empty
			// hand, the hovered slot's object name is drawn once, in white,
			// font 10 at (1, 433), connected to the slot by an orange line
			// at y=437 (from x=nameWidth+4 to slotX+15, guard 0x808e30) and
			// a 4 px vertical tick at (slotX+14, 438..441). Strip offsets
			// below = screen y - 432.
			if (position == 0 && hoveredSlot >= 0 && !inVisitMode &&
			    _engine->getScriptVariableValue("main") == 0) {
				const int slotId = _engine->getScriptVariableValue(
				    Common::String::format("inventaire%d", hoveredSlot));
				if (slotId > 0 && (uint)slotId < _engine->_objectNames.size() &&
				    !_engine->_objectNames[slotId].empty()) {
					Egypt_FontManager &fm = _engine->_fontManager;
					fm.setCurrentFont(Egypt_FontManager::kSlotToolbar);
					const Common::String &label = _engine->_objectNames[slotId];
					const int textW = (int)fm.getStrWidth(label);
					const int slotX = kSlotX0 + hoveredSlot * kSlotStep;
					const uint32 orange = dest.format.RGBToColor(224, 112, 0);
					if (textW + 4 < slotX + 15) {
						dest.hLine(textW + 4, 5, slotX + 14, orange);
						dest.vLine(slotX + 14, 6, 9, orange);
					}
					fm.setForeColor(dest.format.RGBToColor(255, 255, 255));
					fm.displayStr(dest, 1, 1, label);
				}
			}
		}

		g_system->copyRectToScreen(dest.getPixels(), dest.pitch, 0, screenY, 640, kToolbarH);
		g_system->updateScreen();
	};

	_engine->showMouse(true);
	_engine->setInterfaceCursor(kEgyptCursorDefault);

	// --- Slide in (position kToolbarH -> 0), step=4, 10ms/frame ---
	for (int pos = kToolbarH; pos >= 0; pos -= 4) {
		drawFrame(pos);
		g_system->delayMillis(10);
		_engine->pollEvents();
		if (_engine->shouldAbort())
			return false;
	}
	drawFrame(0);

	_engine->clearKeys();
	_engine->waitMouseRelease();

	// --- Event loop ---
	int  selectedScene   = -1;
	int  lastHoveredSlot = -2; // sentinel to force cursor initialisation
	bool eyeWarpQueued   = false;

	// Visual clue list state (EXE flag 0x4c207c; geometry from
	// 0x808f6c..0x809021: box top-left (630-maxW, 429-16n), size
	// (maxW+10) x (16n+4), rows at (636-maxW, 432-16*(n-i)) font 10)
	struct ClueEntry { Common::String name; Common::String target; };
	Common::Array<ClueEntry> clueEntries;
	bool clueListOpen = false;
	int  clueMaxW = 0;

	// Repaints the scene rows hidden by the clue list (the box floats above
	// the toolbar strip; drawFrame() repaints the strip itself)
	auto restoreClueArea = [&]() {
		if (!original || clueEntries.empty())
			return;
		const int boxX = 630 - clueMaxW;
		const int boxY = 429 - 16 * (int)clueEntries.size();
		g_system->copyRectToScreen((const byte *)original->getBasePtr(boxX, boxY),
		                           original->pitch, boxX, boxY,
		                           clueMaxW + 10, screenY - boxY);
	};

	while (!_engine->shouldAbort() && selectedScene < 0 && !eyeWarpQueued) {
		_engine->pollEvents();

		if (_engine->getCurrentMouseButton() == 2) {
			_engine->waitMouseRelease();
			break;
		}

		// --- Visual clue list (EXE 0x808bf6: while the flag is set, the
		// list replaces every other toolbar interaction) ---
		if (clueListOpen) {
			const int count = (int)clueEntries.size();
			const int listX = 636 - clueMaxW;      // rows x = 640-(maxW+4)
			const Common::Point mouse = _engine->getMousePos();

			int hoveredRow = -1;
			for (int i = 0; i < count; i++) {
				// Row hit rect (EXE 0x808fe6): w = maxW+4, h = 15
				const int rowY = 432 - 16 * (count - i);
				if (Common::Rect(listX, rowY, listX + clueMaxW + 4,
				                 rowY + 15).contains(mouse)) {
					hoveredRow = i;
					break;
				}
			}

			// EXE 0x8090cc..0x809100: the list closes as soon as the mouse
			// leaves its area (h spans 16*(count+3) from the first row)
			const int areaY = 432 - 16 * count;
			bool closeList = !Common::Rect(listX, areaY, listX + clueMaxW + 4,
			                               areaY + 16 * (count + 3)).contains(mouse);

			if (_engine->getDragStatus() == kDragStatus_Finished) {
				if (hoveredRow >= 0 && !clueEntries[hoveredRow].target.empty()) {
					// EXE 0x80904c..0x809098: warp to the clue's eye scene,
					// with a return scene like the eye button warps
					_engine->_pendingReturnScene = _engine->_currentScene.name;
					_engine->_pendingWarpTarget  = clueEntries[hoveredRow].target;
					eyeWarpQueued = true;
					goto dismissToolbar; // dismiss path restores the area
				}
				closeList = true; // click outside a row (EXE 0x809105)
			}

			// Draw the darkened box and the rows (white, hovered orange)
			const int boxX = 630 - clueMaxW;
			const int boxY = 429 - 16 * count;
			const int boxW = clueMaxW + 10;  // right edge = 640
			const int boxH = 16 * count + 4; // bottom edge = 433
			Graphics::ManagedSurface listSurf(boxW, boxH, g_system->getScreenFormat());
			for (int row = 0; row < boxH; row++) {
				const int screenRow = boxY + row;
				if (screenRow < screenY) {
					if (original)
						listSurf.copyRectToSurface(*original, 0, row,
						    Common::Rect(boxX, screenRow, boxX + boxW, screenRow + 1));
				} else {
					// bottom rows overlap the toolbar strip
					listSurf.copyRectToSurface(dest.rawSurface(), 0, row,
					    Common::Rect(boxX, screenRow - screenY,
					                 boxX + boxW, screenRow - screenY + 1));
				}
			}
			blendDarken(listSurf, Common::Rect(0, 0, boxW, boxH));

			Egypt_FontManager &fm = _engine->_fontManager;
			fm.setCurrentFont(Egypt_FontManager::kSlotToolbar);
			for (int i = 0; i < count; i++) {
				const int rowY = 432 - 16 * (count - i);
				fm.setForeColor(i == hoveredRow
				    ? listSurf.format.RGBToColor(224, 112, 0)
				    : listSurf.format.RGBToColor(255, 255, 255));
				fm.displayStr(listSurf, listX - boxX, rowY - boxY, clueEntries[i].name);
			}

			drawFrame(0);
			g_system->copyRectToScreen(listSurf.getPixels(), listSurf.pitch,
			                           boxX, boxY, boxW, boxH);
			g_system->updateScreen();

			if (closeList) {
				clueListOpen = false;
				restoreClueArea();
				drawFrame(0);
			}

			g_system->delayMillis(10);
			continue;
		}

		// Key handling.
		// EXE 0x80b980: F1-F6 navigation active in both story and visit modes
		// (gated only by mode flags and ds:0x4d99d0==0x70 - no FlagVisite check).
		Common::KeyCode kc;
		while ((kc = _engine->getNextKey().keycode) != Common::KEYCODE_INVALID) {
			if (kc == Common::KEYCODE_SPACE) {
				_engine->clearKeys();
				goto dismissToolbar;
			}
			for (int i = 0; i < 6; i++) {
				if (kc == (Common::KeyCode)(Common::KEYCODE_F1 + i)) {
					selectedScene = i;
					_engine->clearKeys();
					break;
				}
			}
			if (selectedScene >= 0)
				break;
		}
		if (selectedScene >= 0)
			break;

		// Left-click hit testing (on release, matching EXE behaviour).
		if (_engine->getDragStatus() == kDragStatus_Finished) {
			const Common::Point mouse = _engine->getMousePos();

			if (kRectOptions.contains(mouse)) {
				goto dismissToolbar;
			}

			if (kRectLeft.contains(mouse)) {
				// Eye / view-item button.
				if (!inVisitMode) {
					const int mainId = _engine->getScriptVariableValue("main");
					if (mainId != 0) {
						const uint32 eyeAct = lookupEyeAction(mainId);
						if (eyeAct & 0x8000) {
							const uint warpIdx = eyeAct & 0x7fff;
							if (warpIdx < ARRAYSIZE(kEyeWarpTargets)) {
								_engine->_pendingReturnScene = _engine->_currentScene.name;
								_engine->_pendingWarpTarget  = kEyeWarpTargets[warpIdx];
								eyeWarpQueued = true;
								goto dismissToolbar;
							}
						} else if (eyeAct != 0) {
							_engine->_documentation.displayRecord((int)eyeAct);
						}
					}
				}
				goto dismissToolbar;
			}
			if (kRectRight.contains(mouse)) {
				// Visual clue list button (EXE 0x809131); requires an empty
				// hand (0x809152..0x80915f checks main == 0). With no clue
				// collected nothing shows (EXE 0x808cbc drops the flag).
				if (!inVisitMode && original &&
				    _engine->getScriptVariableValue("main") == 0) {
					clueEntries.clear();
					clueMaxW = 0;
					Egypt_FontManager &fm = _engine->_fontManager;
					fm.setCurrentFont(Egypt_FontManager::kSlotToolbar);
					for (uint i = 0; i < ARRAYSIZE(kClueVars); i++) {
						if (_engine->getScriptVariableValue(kClueVars[i]) == 0)
							continue;
						ClueEntry entry;
						// Name = i-th "message" line of EGYPTE.DEF (0x4cab88)
						entry.name = (i < _engine->_orderedMessages.size())
						    ? _engine->_orderedMessages[i]
						    : Common::String(kClueVars[i]);
						// Eye scene = "ALL/" + suffix after "IndiceVisuelNN"
						// (EXE table 0x435738); none for clues 11..16
						const char *suffix = kClueVars[i] + 14;
						if (*suffix)
							entry.target = Common::String("ALL/") + suffix;
						clueMaxW = MAX<int>(clueMaxW, (int)fm.getStrWidth(entry.name));
						clueEntries.push_back(entry);
					}
					clueListOpen = !clueEntries.empty();
				}
				// unlike the other buttons this one keeps the toolbar open
			}

			for (int i = 0; i < kSlotCount; i++) {
				const Common::Rect slotRect(kSlotX0 + i * kSlotStep,     screenY + kYSlot,
				                            kSlotX0 + i * kSlotStep + 30, screenY + kYSlot + 30);
				if (slotRect.contains(mouse)) {
					if (inVisitMode && i < 6) {
						// Visit mode: slot click = F-key equivalent -> navigate to site
						selectedScene = i;
					} else if (!inVisitMode) {
						// Story mode: swap main <-> slot (EXE 0x4089d0).
						const Common::String slotKey = Common::String::format("inventaire%d", i);
						int held = _engine->getScriptVariableValue("main");
						int slotVal = _engine->getScriptVariableValue(slotKey);

						if (held == 0) {
							if (slotVal != 0) {
								_engine->_gameVariables[GameVariables::kMain] = slotVal;
								_engine->setGameVar(slotKey, 0);
							}
						} else if (slotVal == 0) {
							_engine->setGameVar(slotKey, held);
							_engine->_gameVariables[GameVariables::kMain] = 0;
						} else {
							// Both non-zero: check Etoupe-on-Lampe special case.
							const int etoupe = _engine->getScriptVariableValue("ObjetEtoupe");
							const int lampe  = _engine->getScriptVariableValue("ObjetLampe");
							if (etoupe > 0 && lampe > 0 &&
							    held == etoupe && slotVal == lampe &&
							    _engine->getScriptVariableValue("Etoupe_Sur_Lampe") == 0) {
								_engine->_gameVariables[GameVariables::kEtoupe_Sur_Lampe] = 1;
								debugC(kDebugVariable, "Egypt: Etoupe_Sur_Lampe activated");
							} else {
								_engine->_gameVariables[GameVariables::kMain] = slotVal;
								_engine->setGameVar(slotKey, held);
							}
						}

						// Update cursor to reflect new held object.
						const int newMain = _engine->getScriptVariableValue("main");
						if (newMain != 0)
							_engine->setInterfaceCursor(_engine->getCursorFrameForHeldObject(newMain, false));
						else
							_engine->setInterfaceCursor(kEgyptCursorDefault);
					}
					break;
				}
			}
		}

		// --- Update hovered slot and cursor ---
		{
			const Common::Point mouse = _engine->getMousePos();
			int newHoveredSlot = -1;
			if (mouse.y >= screenY) {
				for (int i = 0; i < kSlotCount; i++) {
					const Common::Rect slotRect(kSlotX0 + i * kSlotStep,     screenY + kYSlot,
					                            kSlotX0 + i * kSlotStep + 30, screenY + kYSlot + 30);
					if (slotRect.contains(mouse)) {
						newHoveredSlot = i;
						break;
					}
				}
			}
			if (newHoveredSlot != lastHoveredSlot) {
				lastHoveredSlot = newHoveredSlot;
				hoveredSlot     = newHoveredSlot;
				if (inVisitMode && newHoveredSlot >= 0 && newHoveredSlot < 6) {
					_engine->setInterfaceCursor(kEgyptCursorVisit);
				} else {
					const int mainId = !inVisitMode ? _engine->getScriptVariableValue("main") : 0;
					if (mainId != 0)
						_engine->setInterfaceCursor(_engine->getCursorFrameForHeldObject(mainId, false));
					else
						_engine->setInterfaceCursor(kEgyptCursorDefault);
				}
			}
		}

		drawFrame(0);
		g_system->delayMillis(10);
	}

dismissToolbar:

	if (_engine->shouldAbort())
		return false;

	// If the clue list was open, repaint the scene rows it covered
	if (clueListOpen) {
		clueListOpen = false;
		restoreClueArea();
	}

	// --- Slide out (position 0 -> kToolbarH), step=4 ---
	for (int pos = 4; pos <= kToolbarH; pos += 4) {
		drawFrame(pos);
		g_system->delayMillis(10);
		_engine->pollEvents();
		if (_engine->shouldAbort())
			return false;
	}

	if (eyeWarpQueued)
		return true;
	if (selectedScene >= 0 && selectedScene < 6) {
		_engine->_pendingWarpTarget = kLevelStartScenes[selectedScene];
		return true;
	}
	return false;
}


bool CryOmni3DEngine_Egypt::displayToolbar(const Graphics::Surface *original) {
	return _toolbar.display(original);
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
