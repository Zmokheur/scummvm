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

#ifndef CRYOMNI3D_EGYPT_DOCUMENTATION_H
#define CRYOMNI3D_EGYPT_DOCUMENTATION_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/rect.h"
#include "common/str.h"

#include "graphics/managed_surface.h"

namespace Graphics {
class Font;
}

namespace CryOmni3D {
namespace Egypt {

class CryOmni3DEngine_Egypt;
struct EgyptZone;

// One run of body text; either plain text or an inline hyperlink
struct EgyptDocTextRun {
	Common::String text;
	int linkIndex; // -1 = normal text, >= 0 = index into EgyptDocumentationRecord::links
};

// One "@ type x y text" line of ESPDOC.TXT: a label drawn onto the record
// photo (EXE element array 0x45eca0, stride 76: +0 type, +4 x, +8 y, +12 text)
struct EgyptDocAnnotation {
	int type = 0; // font from EXE table 0x435060 = {5, 4, 7}; type 2 = boxed
	int x = 0;    // relative to the photo top-left
	int y = 0;
	Common::String text;
};

struct EgyptDocumentationRecord {
	int id = -1;
	Common::String title;
	Common::String assetName;
	Common::String assetCaption;
	Common::String body;
	Common::Array<EgyptDocTextRun> bodyRuns;
	Common::Array<int> links;
	Common::Array<EgyptDocAnnotation> annotations;
};

// In-game documentation ("base documentaire"): record data loaded from
// REF/FR/ESPDOC.TXT + ESPARBO.TXT (tree), a record viewer with inline
// hyperlinks, and the standalone summary browser reached from the menu.
// Works on engine services through a friend pointer, Versailles-style.
// Layout reverse-engineered from EGYPTE.EXE, see
// devtools-egypt/doc_viewer_reverse_notes.md (state machine 0x801000/
// 0x801200 on global 0x4c2078, fiche page draw 0x802b80).
class Egypt_Documentation {
public:
	explicit Egypt_Documentation(CryOmni3DEngine_Egypt *engine) : _engine(engine) {}

	// Loads records and tree on first call; returns false when data files are missing
	bool loadData();

	// Record viewer (fiche page). In-game calls (standalone=false) only
	// offer the exit spiral, like the EXE with bit15 of 0x4c2078 set;
	// the standalone browser gets links, prev/next arrows and the index.
	void displayRecord(int docId, bool standalone = false);

	// Standalone documentation browser (menu entry): SOMMAIRE.TGA summary,
	// theme fiche lists and the record viewer
	void runStandaloneMode();

	// Zone integration used by the warp click handler
	bool isDocumentationZone(const EgyptZone &zone) const;
	int resolveIdForZone(const EgyptZone &zone, Common::String *source = nullptr) const;
	void displayZone(const EgyptZone &zone);

private:
	struct DocLinkHit { Common::Rect rect; int linkIndex; };

	// Viewer state shared by draw/handle
	struct ViewerState {
		bool standalone = false;
		Common::Array<int> themeRecords;
		int currentRecordIndex = 0;

		const EgyptDocumentationRecord *record = nullptr;
		Common::Array<DocLinkHit> linkHits; // rebuilt by drawRecordPage
		int hoveredLink = -1;

		int loadedBgTheme = -2;
		Graphics::ManagedSurface background;
		bool hasBackground = false;
	};

	const EgyptDocumentationRecord *findRecord(int id) const;
	void collectLeafRecords(int nodeId, Common::Array<int> &out) const;
	int findThemeIndexForRecord(int recordId) const;

	// Fiche page draw (EXE 0x802b80) and event handling
	void drawRecordPage(ViewerState &state, const Common::Point &mousePos);
	// Returns true when the viewer should reload the current record
	bool handleRecordEvents(ViewerState &state, bool &exitViewer, bool &redraw);
	bool openRecordById(ViewerState &state, int recordId);

	// One row of REF/FR/EspIndex.txt (EXE parser 0x806190, entry table
	// 0x469f50 stride 68: 64-byte name + record id at +0x40)
	struct AlphaIndexEntry {
		Common::String name;
		int id = -1; // -1 = ";X" letter header row, never clickable
	};

	// Alphabetical index overlay (EXE 0x802520/0x803580); returns the
	// clicked record id or -1
	int runAlphabeticalIndex(const Graphics::ManagedSurface &background);
	bool loadAlphaIndex();

	// Special chronology page for records 211/212 (EXE state 0xf,
	// handler 0x8019bd)
	void runChronologyPage(int recordId, bool standalone);

	CryOmni3DEngine_Egypt *_engine;
	Common::Array<EgyptDocumentationRecord> _records;
	Common::HashMap<int, Common::Array<int> > _tree;
	bool _dataLoaded = false;
	Common::Array<AlphaIndexEntry> _indexEntries;
	bool _indexLoaded = false;
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
