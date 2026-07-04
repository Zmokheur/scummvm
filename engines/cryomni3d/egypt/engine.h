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

#include "audio/mixer.h"

#include "common/array.h"
#include "common/fs.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/str.h"

#include "graphics/surface.h"

#include "cryomni3d/cryomni3d.h"
#include "cryomni3d/egypt/cursor.h"
#include "cryomni3d/egypt/dialog.h"
#include "cryomni3d/egypt/documentation.h"
#include "cryomni3d/egypt/game_variables.h"
#include "cryomni3d/egypt/scene.h"
#include "cryomni3d/egypt/script.h"
#include "cryomni3d/egypt/sprite.h"
#include "cryomni3d/egypt/support/font_manager.h"
#include "cryomni3d/egypt/toolbar.h"
#include "cryomni3d/egypt/warp.h"

namespace Graphics {
class ManagedSurface;
}

namespace CryOmni3D {
namespace Egypt {

// Original game screen size
static const int kScreenWidth = 640;
static const int kScreenHeight = 480;

// Entry scene of each level (1-6); also the F1-F6 toolbar destinations
extern const char *const kLevelStartScenes[6];

// Asset categories resolved by CryOmni3DEngine_Egypt::getFilePath()
// (same idea as Versailles' FileType/getFilePath: call sites never build
// asset paths by hand)
enum EgyptFileType {
	kFileTypeGameDef,          // REF/FR/EGYPTE.DEF (script constants)
	kFileTypeSceneDef,         // per-scene DEF, REF/FR first then SPRITE/LEVELx
	kFileTypeWarp,             // WARP/<name>
	kFileTypeHnm,              // HNM/<name>.HNS then HNM/FR/<name>.HNS; empty if absent
	kFileTypeSpriteImage,      // SPRITE/<name>
	kFileTypeInterfaceSprites, // SPRITE/INTERFAC.SPR
	kFileTypeLevelTxt,         // ref/FR/Level.txt (dialogue trees)
	kFileTypeDocRecords,       // REF/FR/ESPDOC.TXT
	kFileTypeDocTree,          // REF/FR/ESPARBO.TXT
	kFileTypeDocIndex,         // REF/FR/EspIndex.txt (alphabetical index, EXE parser 0x806190)
	kFileTypeVoice,            // sound/FR/<name>.apc
	kFileTypeFont              // SPRITE/<name> (FONT01.CRF..FONT11.CRF, EXE loader 0x80C4B0)
};

struct EgyptMessageEntry {
	Common::String text;
	int documentationId = -1;
};

class CryOmni3DEngine_Egypt : public CryOmni3DEngine {
	friend class Egypt_Dialog;
	friend class Egypt_Script;
	friend class Egypt_Documentation;
	friend class Egypt_Toolbar;
public:
	CryOmni3DEngine_Egypt(OSystem *syst, const CryOmni3DGameDescription *gamedesc);
	~CryOmni3DEngine_Egypt() override;

	void initializePath(const Common::FSNode &gamePath) override;

	bool displayToolbar(const Graphics::Surface *original) override;
	bool hasFeature(EngineFeature f) const override;
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override;
	Common::Error saveGameState(int slot, const Common::String &desc, bool isAutosave = false) override;
	Common::Error loadGameState(int slot) override;
	Common::String getSaveStateName(int slot) const override;
	bool hasPlaceDocumentation() override { return false; }
	bool displayPlaceDocumentation() override { return false; }
	uint displayOptions() override { return 0; }
	void makeTranslucent(Graphics::Surface &dst, const Graphics::Surface &src) const override;
	void setupPalette(const byte *colors, uint start, uint num) override {}

protected:
	Common::Error run() override;

private:
	enum class EgyptStartupMode {
		kMainMenu,
		kResume,
		kStory,
		kVisit,
		kDocumentation,
		kQuit,
		kDebugLevel1,
		kDebugLevel2,
		kDebugLevel3,
		kDebugLevel4,
		kDebugLevel5,
		kDebugLevel6
	};

	void resetGameVariables();
	void setupSprites();
	void setupFonts();

	// saveload.cpp
	bool saveGameToSlot(uint saveNum, const Common::String &desc);
	bool loadGameFromSlot(uint saveNum, Common::String &sceneName);
	Common::String applyPendingLoad();

	void playHnmWithSpeed(const Common::Path &path);
	bool loadSymbolDefinitions(const Common::Path &filename);
	bool setInterfaceCursor(uint spriteId) const;
	EgyptStartupMode showMainMenu();
	Common::String startStoryModePrototype();
	Common::String startVisitMode();
	Common::String startDebugLevel(int level, const Common::String &scene);
	void playStartupLogoIfPresent();
	void drawSimpleScreen(const Common::String &title, const Common::Array<Common::String> &lines,
	                      int selectedLine = -1, const Graphics::ManagedSurface *background = nullptr) const;
	// Load/save list screen (EXE 0x80fe40); returns a 0-based slot or -1
	int runSaveListScreen(bool saveMode, const Graphics::ManagedSurface *background);
	Common::String getSaveDescription(int slot) const;
	bool loadMenuLabels();
	bool loadMessageLabels();
	Common::String resolveMessageLabel(const Common::String &messageId) const;
	Common::String getHoverTextForZone(const EgyptZone *zone) const;
	Common::Path getFilePath(EgyptFileType type, const Common::String &name = Common::String()) const;
	void loadScene(const Common::String &sceneName);
	void parseSceneDefinition(const Common::Path &filename, const Common::String &sceneName);
	bool inspectWarpHeader(const Common::Path &filename, EgyptWarpHeader &header);
	bool displayCurrentWarpPreview(const Common::Path &filename);
	bool displayCurrentWarpRotation(const Graphics::Surface *frame);
	bool displayCurrentWarpFixed(const Graphics::Surface *frame);

	bool handleWarpClick(const Common::Point &mousePos, const Common::Point &warpPoint,
	                     double currentAlpha, double currentBeta);
	bool zoneContainsWarpPoint(const EgyptZone &zone, const Common::Point &warpPoint) const;
	const EgyptZone *findHoveredActiveZone(const Common::Point &warpPoint) const;
	const EgyptZone *findInteractiveZone(const Common::Point &warpPoint) const;
	uint getCursorFrameForZone(const EgyptZone &zone) const;
	uint getDefaultCursorFrame() const;
	uint getCursorFrameForHeldObject(int heldObjectId, bool variant) const;

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

	bool evaluateScriptCondition(const Common::String &expression) const;
	int resolveScriptValue(const Common::String &token) const;
	int getScriptVariableValue(const Common::String &name) const;
	void setGameVar(const Common::String &name, int value);
	void setScriptVariable(const Common::String &assignment);
	bool queuePrototypeSceneChange(uint zoneId, const char *reason, uint zoneClick,
	                               bool viaHnm, double sourceAlpha, double sourceBeta);
	bool executePrototypeSceneLogic();
	const EgyptZone *findZoneById(uint zoneId) const;
	void logRuntimeWarp(const Common::String &matchedCentrage, const EgyptResolvedCentrage &resolved,
	                    bool appliedToRenderer) const;


	// logic.cpp - scene lifecycle
	Common::Array<EgyptSceneAsset> detectSceneAssets(const Common::String &sceneName, int level) const;
	Common::Array<Common::String>  extractScriptBlock(const Common::String &fromMarker,
	                                                   const Common::String &toMarker) const;
	void runWarpInit();
	void runEndInit(int zoneclic);
	void autoActivateZoneclicZones();
	void runSceneStartup();
	void performScreenFade(bool toBlack);
	void performCrossFade(const Graphics::Surface *newScreen);
	void executeHnmSequence(const Common::String &hnmJoined);
	void resetScriptTimer();
	void updateScriptTimer();

	bool _isPlaying = false;
	Common::String _savedSceneName;
	int _pendingLoadSlot = -1;   // slot requested by GMM/launcher, applied by run()
	bool _canLoadSave = false;   // true only inside the in-scene display loops

	EgyptScene _currentScene;
	Common::Array<uint> _gameVariables;
	Common::HashMap<Common::String, int, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _scriptConstants;
	Common::String _pendingWarpTarget;
	Common::String _pendingReturnScene; // set by eye warps: scene to return to after the eye scene exits
	Common::String _currentContextName;
	bool _currentViewAnglesAvailable = false;
	double _currentViewAlpha = 0.0;
	double _currentViewBeta = 0.0;
	EgyptWarpRequest _pendingWarp;
	bool _pendingRuntimeArrivalPrepared = false;
	Common::String _pendingRuntimeMatchedCentrage;
	EgyptResolvedCentrage _pendingRuntimeResolved;
	Common::Array<EgyptCentrage> _currentCentrages;
	Egypt_SpriteLoader _spriteLoader;
	Egypt_FontManager _fontManager;
	Egypt_Toolbar _toolbar;
	Egypt_Documentation _documentation;
	Egypt_Dialog _dialog;
	Egypt_Script _script;
	// Zones always active regardless of script variables (e.g. UTILISER_SUR).
	// Set once by autoActivateZoneclicZones(); each runEndInit resets activeZones
	// to this baseline before re-running the script (mirrors EXE per-frame model).
	Common::Array<uint> _autoActivationZones;
	Common::Array<Common::String> _menuLabels;
	bool _menuLabelsLoaded = false;
	Common::HashMap<Common::String, EgyptMessageEntry, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _messageLabels;
	bool _messageLabelsLoaded = false;
	uint _lastHoveredZoneId;
	Common::Array<EgyptSceneAsset> _currentSceneAssets;
	uint32 _scriptTimerStartMs = 0;
	bool   _sceneHasTimerScript = false;
	Graphics::Surface _crossFadeOldScreen;
	bool _hasCrossFadeOldScreen = false;

	Common::String _dialogPendingLabel;
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
