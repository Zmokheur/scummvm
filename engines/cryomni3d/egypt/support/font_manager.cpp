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
#include "common/path.h"
#include "common/textconsole.h"

#include "graphics/managed_surface.h"

// cryofont.h relies on common/path.h being included beforehand
#include "cryomni3d/fonts/cryofont.h"

#include "cryomni3d/egypt/support/font_manager.h"

namespace CryOmni3D {
namespace Egypt {

Egypt_FontManager::Egypt_FontManager() : _currentSlot(0), _foreColor(0) {
}

Egypt_FontManager::~Egypt_FontManager() {
	for (uint i = 0; i < _fonts.size(); i++)
		delete _fonts[i];
}

void Egypt_FontManager::loadFonts(const Common::Array<Common::Path> &fontFiles) {
	for (uint i = 0; i < _fonts.size(); i++)
		delete _fonts[i];
	_fonts.clear();

	for (uint i = 0; i < fontFiles.size(); i++) {
		CryoFont *font = new CryoFont();
		// CryoFont::load error()s out on missing/invalid files, matching
		// the EXE which has no recovery path either (loader 0x80C4B0)
		font->load(fontFiles[i]);
		_fonts.push_back(font);
	}
}

void Egypt_FontManager::setCurrentFont(uint slot) {
	if (slot >= _fonts.size()) {
		warning("EGYPT_FONT: invalid font slot %u (have %u), keeping %u",
		        slot, _fonts.size(), _currentSlot);
		return;
	}
	_currentSlot = slot;
}

int Egypt_FontManager::getFontHeight() const {
	if (_fonts.empty())
		return 0;
	return _fonts[_currentSlot]->getFontHeight();
}

void Egypt_FontManager::displayStr(Graphics::ManagedSurface &surface, int x, int y,
                                   const Common::String &text) const {
	if (_fonts.empty())
		return;
	// Call through the Graphics::Font base so the ManagedSurface overload
	// (which keeps dirty rects updated) is visible; CryoFont's override
	// hides it otherwise
	const Graphics::Font *font = _fonts[_currentSlot];
	// EXE drawString 0x81a510: chars below 0x20 are skipped (CR/LF would
	// reset x / advance y, single-line callers never pass them), each
	// glyph advances by charWidth 0x81a6e0 = advance + 1
	for (uint i = 0; i < text.size(); i++) {
		byte c = (byte)text[i];
		if (c < 0x20)
			continue;
		font->drawChar(&surface, c, x, y, _foreColor);
		x += font->getCharWidth(c) + 1;
	}
}

uint Egypt_FontManager::getStrWidth(const Common::String &text) const {
	if (_fonts.empty())
		return 0;
	const CryoFont *font = _fonts[_currentSlot];
	uint width = 0;
	for (uint i = 0; i < text.size(); i++) {
		byte c = (byte)text[i];
		if (c < 0x20)
			continue;
		width += font->getCharWidth(c) + 1;
	}
	return width;
}

void Egypt_FontManager::wordWrap(const Common::String &text, uint maxWidth,
                                 Common::Array<Common::String> &lines) const {
	lines.clear();
	if (_fonts.empty()) {
		lines.push_back(text);
		return;
	}

	Common::String current;
	Common::String word;
	for (uint i = 0; i <= text.size(); i++) {
		char c = (i < text.size()) ? text[i] : ' ';
		if (c == ' ' || c == '\n') {
			if (!word.empty()) {
				Common::String candidate = current;
				if (!candidate.empty())
					candidate += ' ';
				candidate += word;
				if (getStrWidth(candidate) > maxWidth && !current.empty()) {
					lines.push_back(current);
					current = word;
				} else {
					current = candidate;
				}
				word.clear();
			}
			if (c == '\n') {
				lines.push_back(current);
				current.clear();
			}
		} else {
			word += c;
		}
	}
	if (!current.empty())
		lines.push_back(current);
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
