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

#ifndef CRYOMNI3D_EGYPT_SUPPORT_FONT_MANAGER_H
#define CRYOMNI3D_EGYPT_SUPPORT_FONT_MANAGER_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

namespace Graphics {
class ManagedSurface;
}

namespace CryOmni3D {

class CryoFont;

namespace Egypt {

// Text renderer reproducing the original EXE font system.
//
// EXE evidence (see devtools-egypt/font_reverse_notes.md):
// - init 0x80C4B0 loads SPRITE/FONT01.CRF..FONT11.CRF into slots 0..10
// - drawString 0x81a510 / drawChar 0x81a590: glyphs advance by
//   (glyph.advance + 1) pixels, background is transparent, glyph rows are
//   placed at y + fontHeight + offY - 2 (CryoFont::drawChar does the same)
// - the current text color is a single global (0x4d8fe8); callers set it
//   before drawing and restore the default white afterwards
// - strings are raw 8-bit bytes indexing glyphs 0x20..0xFE directly (no
//   codepage translation), CryoFont::mapGlyph matches this
//
// This is Egypt-owned support code: CryOmni3D::FontManager (Versailles)
// exposes only a byte foreground color which cannot address Egypt's
// RGBA32 screen, hence this thin manager on top of the shared CryoFont.
class Egypt_FontManager {
public:
	// EXE font slots (0-based load order, slot N = FONT{N+1:02}.CRF)
	static const uint kSlotDocSmall = 1;   // FONT02.CRF, doc browser small text
	static const uint kSlotMenu = 2;       // FONT03.CRF, menu labels and option/save screens
	static const uint kSlotSaveList = 7;   // FONT08.CRF, save slot list in menu
	// Provisional: the EXE dialogue text draw site is not located yet;
	// FONT08.CRF matches Versailles' dialog slot and the menu list font.
	static const uint kSlotDialog = 7;     // FONT08.CRF (TODO: confirm from EXE)
	static const uint kSlotHoverLabel = 9; // FONT10.CRF, in-game hover/message labels
	static const uint kSlotToolbar = 10;   // FONT11.CRF, toolbar texts, tooltips, doc body

	Egypt_FontManager();
	~Egypt_FontManager();

	// Loads FONT01.CRF..FONT11.CRF (paths resolved by the caller).
	// Missing files are fatal like in the EXE (files are on every CD).
	void loadFonts(const Common::Array<Common::Path> &fontFiles);
	bool fontsLoaded() const { return !_fonts.empty(); }

	void setCurrentFont(uint slot);
	uint getCurrentFont() const { return _currentSlot; }
	void setForeColor(uint32 color) { _foreColor = color; }

	// Font height as stored in the CRF header (EXE uses it for line steps)
	int getFontHeight() const;

	// Single-line draw, EXE drawString semantics (x advances by advance+1)
	void displayStr(Graphics::ManagedSurface &surface, int x, int y,
	                const Common::String &text) const;
	// EXE stringWidth semantics: sum of (advance+1) over the characters
	uint getStrWidth(const Common::String &text) const;

	// Word-wraps text into lines no wider than maxWidth (helper for the
	// documentation panels; wrap points are spaces, like the EXE record
	// formatter)
	void wordWrap(const Common::String &text, uint maxWidth,
	              Common::Array<Common::String> &lines) const;

private:
	Common::Array<CryoFont *> _fonts;
	uint _currentSlot;
	uint32 _foreColor;
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
