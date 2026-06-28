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
#include "cryomni3d/egypt/dialogue.h"
#include "cryomni3d/egypt/scene.h"
#include "cryomni3d/egypt/warp.h"

namespace Graphics {
class ManagedSurface;
}

namespace CryOmni3D {
namespace Egypt {

struct EgyptMessageEntry {
	Common::String text;
	int documentationId = -1;
};

struct EgyptOverlayCatalogEntry {
	uint32 base;
	uint32 count;
	uint32 counter;
	uint32 lastTick = 0;
};

struct EgyptPendingOverlayPixel {
	uint16 x;
	uint16 y;
	uint16 rgb565;
};

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

class CryOmni3DEngine_Egypt : public CryOmni3DEngine {
public:
	CryOmni3DEngine_Egypt(OSystem *syst, const CryOmni3DGameDescription *gamedesc);
	~CryOmni3DEngine_Egypt() override;

	void initializePath(const Common::FSNode &gamePath) override;

	bool displayToolbar(const Graphics::Surface *original) override;
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

	void setupSprites();
	bool loadInterfaceSprites(const Common::Path &filename);

	void playHnmFile(const Common::Path &path);
	bool loadSymbolDefinitions(const Common::Path &filename);
	bool setInterfaceCursor(uint spriteId) const;
	EgyptStartupMode showMainMenu();
	Common::String startStoryModePrototype();
	Common::String startVisitMode();
	Common::String startDebugLevel(int level, const Common::String &scene);
	void startDocumentationMode();
	void playStartupLogoIfPresent();
	bool loadWrappedTgaSurface(const Common::Path &filename, Graphics::ManagedSurface &surface) const;
	bool loadWrappedTgaRaw(const Common::Path &filename, Graphics::ManagedSurface &surface) const;
	void drawSimpleScreen(const Common::String &title, const Common::Array<Common::String> &lines,
	                      int selectedLine = -1, const Graphics::ManagedSurface *background = nullptr) const;
	void drawMenuScreen(Graphics::ManagedSurface &surface, int hoveredEntry, bool hasBackground) const;
	bool loadMenuLabels();
	bool loadMessageLabels();
	bool loadDocumentationData();
	Common::String resolveMessageLabel(const Common::String &messageId) const;
	bool isDocumentationZone(const EgyptZone &zone) const;
	int resolveDocumentationIdForZone(const EgyptZone &zone, Common::String *source = nullptr) const;
	void displayZoneDocumentation(const EgyptZone &zone);
	void displayDocumentationById(int docId);
	Common::String getHoverTextForZone(const EgyptZone *zone) const;
	Common::Path resolveSceneDefinitionPath(const Common::String &sceneName) const;
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

	bool loadSceneOverlay(const Common::String &sceneName);
	void decodeOverlayFrame(uint frameIndex);
	void applyOverlayToSurface(Graphics::Surface &surface) const;
	void decodeSceneSprFrame(uint frameIndex);
	void applySceneSprToPanorama(Graphics::Surface &surface) const;
	void applySceneSprToScreen(Graphics::Surface &surface) const;

	// dialogue.cpp — Level.txt dialogue system
	bool loadLevelTxt();
	const EgyptDialogNode *findDialogNode(const Common::String &label) const;
	EgyptDialogResult executeDialogNode(const Common::String &label,
	                                    Common::String &outText,
	                                    Common::Array<EgyptDialogChoice> &outChoices,
	                                    Common::String &outNextLabel);
	bool loadDialogSprite(const Common::Path &path, EgyptDialogSprite &out);
	void loadDialogSpeaker(const Common::String &speakerName, int level);
	void loadDialogSyc(const Common::String &label);
	void blitDialogSpriteFrame(Graphics::ManagedSurface &dst,
	                           const EgyptDialogSprite &sprite, uint frame);
	void playDialogVoice(const Common::String &label);
	void stopDialogVoice();
	void showDialogText(const Graphics::ManagedSurface &background, const Common::String &text);
	int  showDialogChoices(const Graphics::ManagedSurface &background,
	                       const Common::Array<EgyptDialogChoice> &choices);
	void runDialogue(const Common::String &startLabel);

	// logic.cpp — scene lifecycle
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

	EgyptScene _currentScene;
	Common::HashMap<Common::String, int, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _scriptVariables;
	Common::HashMap<Common::String, int, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _scriptConstants;
	Common::HashMap<Common::String, bool, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _loggedScriptCommands;
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
	Common::Array<EgyptInterfaceSprite *> _interfaceSprites;
	Common::Array<byte> _sceneOverlayData;
	Common::Array<EgyptOverlayCatalogEntry> _sceneOverlayCatalog;
	Common::Array<EgyptPendingOverlayPixel> _pendingOverlayPixels;
	bool _hasPendingOverlay = false;
	bool _overlayDirty = false;
	Common::Array<EgyptPendingOverlayPixel> _sceneSprPixels;
	bool _sceneSprDirty = false;
	// Zones always active regardless of script variables (e.g. UTILISER_SUR).
	// Set once by autoActivateZoneclicZones(); each runEndInit resets activeZones
	// to this baseline before re-running the script (mirrors EXE per-frame model).
	Common::Array<uint> _autoActivationZones;
	Common::Array<Common::String> _menuLabels;
	bool _menuLabelsLoaded = false;
	Common::HashMap<Common::String, EgyptMessageEntry, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _messageLabels;
	bool _messageLabelsLoaded = false;
	Common::Array<EgyptDocumentationRecord> _documentationRecords;
	Common::HashMap<int, Common::Array<int> > _documentationTree;
	bool _documentationDataLoaded = false;
	uint _lastHoveredZoneId;
	Common::Array<EgyptSceneAsset> _currentSceneAssets;
	uint32 _scriptTimerStartMs = 0;
	bool   _sceneHasTimerScript = false;
	Graphics::Surface _crossFadeOldScreen;
	bool _hasCrossFadeOldScreen = false;

	// Dialogue system (dialogue.cpp)
	Common::HashMap<Common::String, EgyptDialogNode,
	                Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _dialogueNodes;
	bool _dialogueLevelLoaded = false;
	Common::String _dialoguePendingLabel;
	Graphics::Surface _dlgTga;               // image de base statique (TGA)
	EgyptDialogSprite _dlgSpa;               // patches visage/idle (TXEN/RLE)
	EgyptDialogSprite _dlgSpb;               // patches bouche (TXEN/RLE, piloté par SYC)
	Common::String    _dlgSpeakerName;
	Audio::SoundHandle _dlgVoiceHandle;
	Common::Array<EgyptSycEvent> _dlgSycEvents;
	uint _dlgSycEventIdx = 0;
	uint _dlgIdleFrameIdx = 0;               // index dans kIdleMouthFrames pour SPA
	uint32 _dlgIdleNextMs = 0;              // prochain tick animation idle
	uint32 _dlgNodeStartMs = 0;             // timestamp début du nœud courant
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
