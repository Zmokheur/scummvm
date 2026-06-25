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

#ifndef CRYOMNI3D_EGYPT_ENGINE_H
#define CRYOMNI3D_EGYPT_ENGINE_H

#include "common/array.h"
#include "common/fs.h"
#include "common/hashmap.h"
#include "common/str.h"

#include "cryomni3d/cryomni3d.h"
#include "graphics/managed_surface.h"

namespace Graphics {
struct Surface;
}

namespace CryOmni3D {
namespace Egypt {

struct EgyptZone {
	uint id;
	uint left;
	uint top;
	uint right;
	uint bottom;
	uint actionId;
	Common::String commandName;
	Common::String command;
	Common::String targetWarp;
};

struct EgyptScene {
	Common::String name;
	Common::String warpName;
	Common::Array<EgyptZone> zones;
	Common::Array<Common::String> scriptLines;
	Common::Array<uint> activeZones;
};

struct EgyptWarpHeader {
	Common::String tag;
	uint16 width;
	uint16 height;
	byte audioFlags;
	byte bpp;
	uint32 frameSize;
	Common::String firstChunkTag;
	uint32 firstChunkSize;
};

struct EgyptInterfaceSprite {
	Graphics::ManagedSurface surface;
	Common::Array<byte> mask;
	int hotspotX;
	int hotspotY;
};

class CryOmni3DEngine_Egypt : public CryOmni3DEngine {
public:
	CryOmni3DEngine_Egypt(OSystem *syst, const CryOmni3DGameDescription *gamedesc);
	~CryOmni3DEngine_Egypt() override;

	void initializePath(const Common::FSNode &gamePath) override;

	bool displayToolbar(const Graphics::Surface *original) override { return false; }
	bool hasPlaceDocumentation() override { return false; }
	bool displayPlaceDocumentation() override { return false; }
	uint displayOptions() override { return 0; }
	void makeTranslucent(Graphics::Surface &dst, const Graphics::Surface &src) const override {}
	void setupPalette(const byte *colors, uint start, uint num) override {}

protected:
	Common::Error run() override;

private:
	void setupSprites();
	bool loadInterfaceSprites(const Common::Path &filename);
	bool setInterfaceCursor(uint spriteId) const;
	void loadScene(const Common::String &sceneName);
	void parseSceneDefinition(const Common::Path &filename, const Common::String &sceneName);
	bool inspectWarpHeader(const Common::Path &filename, EgyptWarpHeader &header);
	bool displayCurrentWarpPreview(const Common::Path &filename);
	bool displayCurrentWarpRotation(const Graphics::Surface *frame);
	void parseZoneCommand(EgyptZone &zone);
	void collectInitialActiveZones();
	bool runPrototypeWarpScript();
	bool executeScriptBlock(const Common::Array<Common::String> &lines);
	bool evaluateScriptCondition(const Common::String &expression) const;
	int getScriptVariableValue(const Common::String &name) const;
	void setScriptVariable(const Common::String &assignment);
	int getPrototypeZoneClick() const;
	bool queuePrototypeSceneChange(uint zoneId, const char *reason);
	bool executePrototypeSceneLogic();
	const EgyptZone *findZoneById(uint zoneId) const;
	void logScriptLine(const Common::String &line) const;

	EgyptScene _currentScene;
	Common::HashMap<Common::String, int> _scriptVariables;
	Common::String _pendingWarpTarget;
	Common::Array<EgyptInterfaceSprite *> _interfaceSprites;
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
