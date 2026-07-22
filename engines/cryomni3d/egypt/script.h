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

#ifndef CRYOMNI3D_EGYPT_SCRIPT_H
#define CRYOMNI3D_EGYPT_SCRIPT_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/str.h"

namespace CryOmni3D {
namespace Egypt {

class CryOmni3DEngine_Egypt;

// One editspr overlay animation slot (created by editspr, played by animspr)
struct EgyptOverlayCatalogEntry {
	uint32 base;
	uint32 count;
	uint32 counter;
	uint32 lastTick = 0;
};

// DEF script interpreter: executes warpinit/endinit blocks of a scene's
// .DEF file (one command per line, labels, if/goto). The engine owns the
// variable store (GameVariables); this class owns block execution and
// per-command handlers, dispatched through a command table.
class Egypt_Script {
public:
	explicit Egypt_Script(CryOmni3DEngine_Egypt *engine) : _engine(engine) {}

	// Executes one script block (lines between warpinit/endinit/endwarp markers).
	// Returns true when the block changed observable state (zones, variables, warp).
	bool executeBlock(const Common::Array<Common::String> &lines, uint zoneClick,
	                  double sourceAlpha, double sourceBeta);

	// Traces one raw script line on the script debug channel (used by the parser)
	void logScriptLine(const Common::String &line) const;

	// Clears per-scene state (overlay animation catalog)
	void resetSceneState() { _overlayCatalog.clear(); }

private:
	// Execution context shared by command handlers
	struct Context {
		const Common::HashMap<Common::String, uint> &labels;
		uint &pc;
		uint zoneClick;
		double sourceAlpha;
		double sourceBeta;
		bool &producedState;
	};

	typedef void (Egypt_Script::*CommandHandler)(const Common::String &args, Context &ctx);
	struct CommandEntry {
		const char *name;    // command keyword
		bool takesArgs;      // true: match "name " prefix, false: match whole line
		CommandHandler handler;
	};
	static const CommandEntry kCommands[];

	// Returns true when the line matched a known (or safely ignored) command
	bool executeCommand(const Common::String &line, Context &ctx);

	void cmdZoneActive(const Common::String &args, Context &ctx);
	void cmdZoneInactive(const Common::String &args, Context &ctx);
	void cmdLet(const Common::String &args, Context &ctx);
	void cmdAllerWarp(const Common::String &args, Context &ctx);
	void cmdAllerHnmWarp(const Common::String &args, Context &ctx);
	void cmdGoto(const Common::String &args, Context &ctx);
	void cmdEditspr(const Common::String &args, Context &ctx);
	void cmdDecompress(const Common::String &args, Context &ctx);
	void cmdAnimspr(const Common::String &args, Context &ctx);
	void cmdFonction(const Common::String &args, Context &ctx);
	void cmdDialoguer(const Common::String &args, Context &ctx);
	void cmdMusic(const Common::String &args, Context &ctx);
	void cmdStopMusic(const Common::String &args, Context &ctx);

	void logUnsupportedCommand(const Common::String &line);

	CryOmni3DEngine_Egypt *_engine;
	Common::Array<EgyptOverlayCatalogEntry> _overlayCatalog;
	// Unsupported commands already reported (one warning per command keyword)
	Common::HashMap<Common::String, bool, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _loggedCommands;
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
