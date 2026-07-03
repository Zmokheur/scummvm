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

struct EgyptDocumentationRecord {
	int id = -1;
	Common::String title;
	Common::String assetName;
	Common::String assetCaption;
	Common::String body;
	Common::Array<EgyptDocTextRun> bodyRuns;
	Common::Array<int> links;
};

// In-game documentation ("base documentaire"): record data loaded from
// REF/FR/ESPDOC.TXT + ESPARBO.TXT (tree), a record viewer with inline
// hyperlinks, and the standalone summary browser reached from the menu.
// Works on engine services through a friend pointer, Versailles-style.
class Egypt_Documentation {
public:
	explicit Egypt_Documentation(CryOmni3DEngine_Egypt *engine) : _engine(engine) {}

	// Loads records and tree on first call; returns false when data files are missing
	bool loadData();

	// Record viewer (photo, hyperlinked body, prev/next/back)
	void displayRecord(int docId);

	// Standalone documentation browser (menu entry): theme summary + viewer
	void runStandaloneMode();

	// Zone integration used by the warp click handler
	bool isDocumentationZone(const EgyptZone &zone) const;
	int resolveIdForZone(const EgyptZone &zone, Common::String *source = nullptr) const;
	void displayZone(const EgyptZone &zone);

private:
	// Word-wrapped layout of one record body
	struct DocWord     { Common::String text; int linkIndex; bool lineBreak; };
	struct DocLineWord { int wordIdx; int x; };
	struct DocLine     { Common::Array<DocLineWord> words; };
	struct DocLinkHit  { Common::Rect rect; int linkIndex; };

	// Viewer state shared by prepare/draw/handle
	struct ViewerState {
		int selectedTheme = 0;
		Common::Array<int> themeRecords;
		int currentRecordIndex = 0;
		int scrollOffset = 0;

		const EgyptDocumentationRecord *record = nullptr;
		Common::Array<DocWord> words;
		Common::Array<DocLine> lines;
		int visibleLines = 1;
		int maxScroll = 0;

		Common::Array<DocLinkHit> linkHits;

		int loadedBgTheme = -2;
		Graphics::ManagedSurface background;
		bool hasBackground = false;
	};

	const EgyptDocumentationRecord *findRecord(int id) const;
	void collectLeafRecords(int nodeId, Common::Array<int> &out) const;
	int findThemeIndexForRecord(int recordId) const;

	// displayRecord() split: layout preparation, pure drawing, event handling
	bool prepareRecord(ViewerState &state, const Graphics::Font *bodyFont);
	void drawRecord(ViewerState &state, const Graphics::Font *titleFont,
	                const Graphics::Font *bodyFont, const Common::Point &mousePos);
	// Returns true when the viewer should reload the current record
	bool handleRecordEvents(ViewerState &state, bool &exitViewer, bool &redraw);

	CryOmni3DEngine_Egypt *_engine;
	Common::Array<EgyptDocumentationRecord> _records;
	Common::HashMap<int, Common::Array<int> > _tree;
	bool _dataLoaded = false;
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
