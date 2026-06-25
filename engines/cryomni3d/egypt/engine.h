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
#include "common/hash-str.h"
#include "common/str.h"

#include "cryomni3d/cryomni3d.h"
#include "cryomni3d/egypt/cursor.h"
#include "cryomni3d/egypt/scene.h"
#include "cryomni3d/egypt/warp.h"

namespace Graphics {
class ManagedSurface;
struct Surface;
}

namespace CryOmni3D {
namespace Egypt {

struct EgyptMessageEntry {
	Common::String text;
	int documentationId = -1;
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
	enum class EgyptStartupMode {
		kMainMenu,
		kStory,
		kVisit,
		kDocumentation,
		kQuit
	};

	void setupSprites();
	bool loadInterfaceSprites(const Common::Path &filename);
	bool loadSymbolDefinitions(const Common::Path &filename);
	bool setInterfaceCursor(uint spriteId) const;
	EgyptStartupMode showMainMenu();
	Common::String startStoryModePrototype();
	Common::String startVisitMode();
	void startDocumentationModePlaceholder();
	void playStartupLogoIfPresent();
	bool loadWrappedTgaSurface(const Common::Path &filename, Graphics::ManagedSurface &surface) const;
	void drawSimpleScreen(const Common::String &title, const Common::Array<Common::String> &lines,
	                      int selectedLine = -1, const Graphics::ManagedSurface *background = nullptr) const;
	void drawMenuScreen(Graphics::ManagedSurface &surface, int hoveredEntry, bool hasBackground) const;
	bool loadMenuLabels();
	bool loadMessageLabels();
	Common::String resolveMessageLabel(const Common::String &messageId) const;
	bool isDocumentationZone(const EgyptZone &zone) const;
	int resolveDocumentationIdForZone(const EgyptZone &zone, Common::String *source = nullptr) const;
	void displayZoneDocumentation(const EgyptZone &zone);
	Common::String getHoverTextForZone(const EgyptZone *zone) const;
	Common::Path resolveSceneDefinitionPath(const Common::String &sceneName) const;
	void loadScene(const Common::String &sceneName);
	void parseSceneDefinition(const Common::Path &filename, const Common::String &sceneName);
	bool inspectWarpHeader(const Common::Path &filename, EgyptWarpHeader &header);
	bool displayCurrentWarpPreview(const Common::Path &filename);
	bool displayCurrentWarpRotation(const Graphics::Surface *frame);
	bool handleWarpClick(const Common::Point &mousePos, const Common::Point &warpPoint,
	                     double currentAlpha, double currentBeta);
	bool zoneContainsWarpPoint(const EgyptZone &zone, const Common::Point &warpPoint) const;
	const EgyptZone *findHoveredActiveZone(const Common::Point &warpPoint) const;
	const EgyptZone *findInteractiveZone(const Common::Point &warpPoint) const;
	uint getCursorFrameForZone(const EgyptZone &zone) const;
	uint getDefaultCursorFrame() const;
	uint getCursorFrameForHeldObject(int heldObjectId, bool variant) const;
	int alphaToPanoramaX(double alpha) const;
	void rememberPendingArrival(const EgyptZone &zone, uint zoneclic, bool viaHnm, bool sourceOrientationAvailable,
	                           double alpha, double beta, const char *calledCommand);
	void clearPendingWarpRequest();
	void getRuntimeSourceViewAngles(double &alpha, double &beta, bool &available) const;
	void setRuntimeViewAngles(double alpha, double beta, bool available);
	const EgyptCentrage *findCentrage(const Common::String &name) const;
	const EgyptCentrage *findArrivalCentrage(Common::String *matchedName) const;
	EgyptResolvedCentrage applyCentrageRaw(const EgyptCentrage &centrage,
	                                       double sourceAlpha, double sourceBeta) const;
	void prepareRuntimeArrivalView();
	Common::String resolvePrototypeWarpTarget(const Common::String &targetName) const;
	int consumeArrivalPanoramaX(double &alpha, double &beta, bool &hasAngles);
	uint resolveScriptZoneClick(const EgyptZone &zone) const;
	bool shouldUseDirectWarpFallback(const EgyptZone &zone, uint zoneClick) const;
	void parseZoneCommand(EgyptZone &zone);
	void collectInitialActiveZones();
	bool runPrototypeWarpScript(int zoneClick = 0, double sourceAlpha = 0.0, double sourceBeta = 0.0);
	bool executeScriptBlock(const Common::Array<Common::String> &lines, uint zoneClick,
	                        double sourceAlpha, double sourceBeta);
	bool executeScriptCommand(const Common::String &line, const Common::HashMap<Common::String, uint> &labels,
	                         uint &pc, uint zoneClick, double sourceAlpha, double sourceBeta,
	                         bool &producedState);
	bool evaluateScriptCondition(const Common::String &expression) const;
	int resolveScriptValue(const Common::String &token) const;
	int getScriptVariableValue(const Common::String &name) const;
	void setScriptVariable(const Common::String &assignment);
	bool queuePrototypeSceneChange(uint zoneId, const char *reason, uint zoneClick,
	                               bool viaHnm, double sourceAlpha, double sourceBeta);
	bool executePrototypeSceneLogic();
	const EgyptZone *findZoneById(uint zoneId) const;
	void logScriptLine(const Common::String &line) const;
	void logUnsupportedScriptCommand(const Common::String &line) const;
	void logRuntimeWarp(const Common::String &matchedCentrage, const EgyptResolvedCentrage &resolved,
	                    bool appliedToRenderer) const;
	void logWarpTrace(const Common::String &matchedName, const EgyptCentrage *matchedCentrage,
	                  const EgyptResolvedCentrage &resolved) const;

	EgyptScene _currentScene;
	Common::HashMap<Common::String, int, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _scriptVariables;
	Common::HashMap<Common::String, int, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _scriptConstants;
	Common::HashMap<Common::String, bool, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _loggedScriptCommands;
	Common::String _pendingWarpTarget;
	Common::String _currentContextName;
	bool _currentViewAnglesAvailable = false;
	double _currentViewAlpha = 0.0;
	double _currentViewBeta = 0.0;
	EgyptWarpRequest _pendingWarp;
	bool _pendingRuntimeArrivalPrepared = false;
	Common::String _pendingRuntimeMatchedCentrage;
	EgyptResolvedCentrage _pendingRuntimeResolved;
	Common::Array<EgyptCentrage> _currentCentrages;
	Common::Array<EgyptInterfaceSprite *> _interfaceSprites;
	Common::Array<Common::String> _menuLabels;
	bool _menuLabelsLoaded = false;
	Common::HashMap<Common::String, EgyptMessageEntry, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _messageLabels;
	bool _messageLabelsLoaded = false;
	uint _lastHoveredZoneId;
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
