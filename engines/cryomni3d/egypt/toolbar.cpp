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

#include "common/endian.h"
#include "common/rect.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/managed_surface.h"
#include "graphics/surface.h"

#include "cryomni3d/egypt/cursor.h"
#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

namespace {

// Toolbar layout — confirmed from EXE function 0x808080 / 0x808990
//
// Total size: 640×48 pixels, screen Y = 432 (= 480 - 48)
// Confirmed by: push 0x30 (=48) at 0x8089ae, offset 0x87000 (=640×432×2) at 0x8089b0
//
// Y offsets within the 48-pixel-tall toolbar strip (from EXE constants):
//   Sprite 15 (30×30 slot): 0x1eb - param → at param=48 → screen y=443 → offset 11
//   Sprite 16/18 (20×20):   0x1f0 - param → at param=48 → screen y=448 → offset 16
//   Sprite 19 (20×20):      0x1fc - param → at param=48 → screen y=460 → offset 28 (=48-20, bottom-aligned)
//     Confirmed by static draw at absolute y=0x1cc=460 (0x808786) and hit-test (0x8087c4)
//
// X positions (EXE):
//   Sprite 19 (options):       x=0        (EXE 0x808f03: push edi=0)
//   Sprite 16 (view-item):     x=128=0x80 (EXE 0x808a4f: push 0x80)
//   Sprite 15 (inv slot ×10):  x=160+i*46 (EXE 0x808cdc: mov ebx,0xa0; 0x808edc: add ebx,0x2e)
//   Sprite 18 (doc button):    x=617=0x269(EXE 0x808a6e: push 0x269)
//
// Animation: 12 frames, step=4px (param: 4→48), 10ms/frame
//   Counter at ds:0x47c5f8; initial=4 (0x8088bc); stop at 0x30=48 (0x808941)
//
// Navigation shortcuts (EXE 0x80b980 state machine):
//   Zone 0x70-0x75 = Windows VK_F1-VK_F6 → scenes S00,D01,A02,N01A,M01,K43
//   Zone 0x76     = Windows VK_F7         → dismiss

static const int kSpriteSlot    = 15;  // 30×30 — empty inventory slot
static const int kSpriteLeft    = 16;  // 20×20 — view-item button (sprite 17 = selected state)
static const int kSpriteRight   = 18;  // 20×20 — documentation button
static const int kSpriteOptions = 19;  // 20×20 — options / dismiss (bottom-aligned)

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

// F1-F6 → scene navigation (zones 0x70-0x75 = Windows VK_F1-VK_F6)
// In visit mode, slots 0-5 are labelled with these site names (EXE 0x808683 text label path)
static const char *const kScenes[6] = { "S00", "D01", "A02", "N01A", "M01", "K43" };

// INTERFAC.SPR sprites are RGB565
static const Graphics::PixelFormat kEgyptSpriteFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);

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

bool CryOmni3DEngine_Egypt::displayToolbar(const Graphics::Surface *original) {
	if (_interfaceSprites.size() <= (uint)kSpriteOptions)
		return false;

	// EXE 0x808683: FlagVisite controls text label rendering (!=0 → labels shown in visit mode).
	// EXE 0x808529: FlagVisite!=0 → kEgyptCursorVisit when hovering a named slot.
	// The sprite layout (0x8089d0) is the same in both modes; only content/cursor/labels differ.
	const bool inVisitMode = (getScriptVariableValue("FlagVisite") != 0);

	// Working surfaces: 640 wide, kToolbarH (48) tall
	Graphics::ManagedSurface bgSurface(640, kToolbarH, g_system->getScreenFormat());
	Graphics::ManagedSurface dest(640, kToolbarH, g_system->getScreenFormat());

	// Build translucent background from the bottom 48 rows of the current scene
	if (original && original->w >= 640 && original->h >= 480) {
		const Common::Rect srcRect(0, original->h - kToolbarH, 640, original->h);
		bgSurface.copyRectToSurface(*original, 0, 0, srcRect);
		makeTranslucent(*bgSurface.surfacePtr(), bgSurface.rawSurface());
	}

	// Toolbar top: y=432 on a 480-pixel screen (= 480 - 48)
	const int screenY = 480 - kToolbarH;

	// Hit rects in screen coordinates — confirmed from EXE hit-tests (0x819f60 calls)
	// Sprite 19: EXE 0x8087c4 push 0x14,0x14,edi(0),0x1cc(460) → 20×20 at (0,460)
	// Sprite 16: EXE 0x808aab push 0x14,0x14,0x80(128),ebp     → 20×20 at (128, screenY+kYButtons)
	// Sprite 18: EXE 0x809131 push 0x14,0x14,0x269(617),ebp    → 20×20 at (617, screenY+kYButtons)
	// Slots:     EXE 0x808e80 push 0x1e(30),0x1e(30),x,y       → 30×30 per slot
	const Common::Rect kRectOptions(kXOptions,                   screenY + kYOptions,
	                                kXOptions + 20,              screenY + kYOptions + 20);
	const Common::Rect kRectLeft(   kXLeft,                      screenY + kYButtons,
	                                kXLeft + 20,                 screenY + kYButtons + 20);
	const Common::Rect kRectRight(  kXRight,                     screenY + kYButtons,
	                                kXRight + 20,                screenY + kYButtons + 20);

	// Helper: blit one interface sprite at a given x, y-offset within the toolbar strip.
	// `position` = number of toolbar rows still hidden (0 = fully visible, kToolbarH = fully hidden).
	auto blitSpr = [&](int sprId, int x, int yOff, int position) {
		if (sprId < 0 || (uint)sprId >= _interfaceSprites.size())
			return;
		const EgyptInterfaceSprite &spr = *_interfaceSprites[sprId];
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

			// Sprite layout identical in both modes (EXE 0x8089d0).
			// Story mode: slots will eventually hold inventory item images.
			// Visit mode:  slots are empty; site labels are drawn on hover below.
			blitSpr(kSpriteOptions, kXOptions, kYOptions, position);
			blitSpr(kSpriteLeft,    kXLeft,    kYButtons, position);
			for (int i = 0; i < kSlotCount; i++)
				blitSpr(kSpriteSlot, kSlotX0 + i * kSlotStep, kYSlot, position);
			blitSpr(kSpriteRight, kXRight, kYButtons, position);

			// Visit mode: draw a tooltip label above the hovered slot (EXE 0x808683).
			// Only the first 6 slots have a mapped site (F1-F6 destinations).
			if (position == 0 && inVisitMode && hoveredSlot >= 0 && hoveredSlot < 6) {
				const Graphics::Font *font =
				    FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
				if (font) {
					const Common::String label(kScenes[hoveredSlot]);
					const int textW = font->getStringWidth(label);
					const int fontH = font->getFontHeight();
					// Centre above the slot; clamp to toolbar bounds
					const int slotCX = kSlotX0 + hoveredSlot * kSlotStep + 15;
					const int textX  = CLIP<int>(slotCX - textW / 2, 2, 638 - textW);
					const int textY  = CLIP<int>(kYSlot - fontH - 2, 1, kToolbarH - fontH - 1);
					const Common::Rect bubble(textX - 4, textY - 2,
					                          textX + textW + 4, textY + fontH + 2);
					dest.fillRect(bubble,  dest.format.RGBToColor( 20,  18,  14));
					dest.frameRect(bubble, dest.format.RGBToColor(188, 154,  84));
					font->drawString(&dest, label, textX, textY,
					                 dest.w - textX,
					                 dest.format.RGBToColor(244, 232, 204));
				}
			}
		}

		g_system->copyRectToScreen(dest.getPixels(), dest.pitch, 0, screenY, 640, kToolbarH);
		g_system->updateScreen();
	};

	showMouse(true);
	setInterfaceCursor(kEgyptCursorDefault);

	// --- Slide in (position kToolbarH → 0), step=4, 10ms/frame ---
	for (int pos = kToolbarH; pos >= 0; pos -= 4) {
		drawFrame(pos);
		g_system->delayMillis(10);
		pollEvents();
		if (shouldAbort())
			return false;
	}
	drawFrame(0);

	clearKeys();
	waitMouseRelease();

	// --- Event loop ---
	bool mouseWasInsideToolbar = (getMousePos().y >= screenY);
	int selectedScene   = -1;
	int lastHoveredSlot = -2; // sentinel to force cursor initialisation

	while (!shouldAbort() && selectedScene < 0) {
		pollEvents();

		if (getCurrentMouseButton() == 2) {
			waitMouseRelease();
			break;
		}

		// Key handling.
		// EXE 0x80b980: F1-F6 navigation active in both story and visit modes
		// (gated only by mode flags and ds:0x4d99d0==0x70 — no FlagVisite check).
		Common::KeyCode kc;
		while ((kc = getNextKey().keycode) != Common::KEYCODE_INVALID) {
			if (kc == Common::KEYCODE_ESCAPE || kc == Common::KEYCODE_F7) {
				clearKeys();
				goto dismissToolbar;
			}
			for (int i = 0; i < 6; i++) {
				if (kc == (Common::KeyCode)(Common::KEYCODE_F1 + i)) {
					selectedScene = i;
					clearKeys();
					break;
				}
			}
			if (selectedScene >= 0)
				break;
		}
		if (selectedScene >= 0)
			break;

		// Left-click hit testing (on release, matching EXE behaviour).
		if (getDragStatus() == kDragStatus_Finished) {
			const Common::Point mouse = getMousePos();

			if (kRectOptions.contains(mouse)) {
				goto dismissToolbar;
			}

			if (kRectLeft.contains(mouse)) {
				// Sprite 16/17 — examine current item (story) / view-item (visit)
				warning("EGYPT_TOOLBAR: view-item button clicked — not implemented yet");
				goto dismissToolbar;
			}
			if (kRectRight.contains(mouse)) {
				// Sprite 18 — open documentary space
				warning("EGYPT_TOOLBAR: documentary space button clicked — not implemented yet");
				goto dismissToolbar;
			}

			for (int i = 0; i < kSlotCount; i++) {
				const Common::Rect slotRect(kSlotX0 + i * kSlotStep,     screenY + kYSlot,
				                            kSlotX0 + i * kSlotStep + 30, screenY + kYSlot + 30);
				if (slotRect.contains(mouse)) {
					if (inVisitMode && i < 6) {
						// Visit mode: slot click = F-key equivalent → navigate to site
						selectedScene = i;
					} else if (!inVisitMode) {
						// Story mode: slot click = select inventory item
						warning("EGYPT_TOOLBAR: inventory slot %d clicked — not implemented yet", i);
					}
					break;
				}
			}
		}

		// --- Update hovered slot and cursor ---
		{
			const Common::Point mouse = getMousePos();
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
				// Visit mode: kEgyptCursorVisit on first 6 slots (EXE 0x808529)
				if (inVisitMode && newHoveredSlot >= 0 && newHoveredSlot < 6)
					setInterfaceCursor(kEgyptCursorVisit);
				else
					setInterfaceCursor(kEgyptCursorDefault);
			}
		}

		// Mouse leaving toolbar → dismiss
		const Common::Point mouse = getMousePos();
		if (!mouseWasInsideToolbar) {
			mouseWasInsideToolbar = (mouse.y >= screenY);
		} else if (mouse.y < screenY) {
			break;
		}

		drawFrame(0);
		g_system->delayMillis(10);
	}

dismissToolbar:

	if (shouldAbort())
		return false;

	// --- Slide out (position 0 → kToolbarH), step=4 ---
	for (int pos = 4; pos <= kToolbarH; pos += 4) {
		drawFrame(pos);
		g_system->delayMillis(10);
		pollEvents();
		if (shouldAbort())
			return false;
	}

	if (selectedScene >= 0 && selectedScene < 6) {
		_pendingWarpTarget = kScenes[selectedScene];
		return true;
	}
	return false;
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
