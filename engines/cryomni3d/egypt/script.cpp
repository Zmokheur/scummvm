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

#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "cryomni3d/egypt/engine.h"

namespace CryOmni3D {
namespace Egypt {

// Maps DEF script variable names (case-insensitive) to GameVariables::Var indices.
// Built once on first call; safe because variable names are fixed at compile time.
static int gameVarIndex(const Common::String &name) {
	typedef Common::HashMap<Common::String, int,
	    Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> VarMap;
	static VarMap s_map;
	if (s_map.empty()) {
		s_map["zoneclic"]                 = GameVariables::kZoneclic;
		s_map["tmp"]                      = GameVariables::kTmp;
		s_map["timer"]                    = GameVariables::kTimer;
		s_map["message"]                  = GameVariables::kMessage;
		s_map["Level"]                    = GameVariables::kLevel;
		s_map["FlagVisite"]               = GameVariables::kFlagVisite;
		s_map["EndGame"]                  = GameVariables::kEndGame;
		s_map["FlagFinDuJeu"]             = GameVariables::kFlagFinDuJeu;
		s_map["InitLevel1"]               = GameVariables::kInitLevel1;
		s_map["InitLevel2"]               = GameVariables::kInitLevel2;
		s_map["InitLevel3"]               = GameVariables::kInitLevel3;
		s_map["InitLevel4"]               = GameVariables::kInitLevel4;
		s_map["InitLevel5"]               = GameVariables::kInitLevel5;
		s_map["InitLevel6"]               = GameVariables::kInitLevel6;
		s_map["main"]                     = GameVariables::kMain;
		s_map["inventaire0"]              = GameVariables::kInventaire0;
		s_map["inventaire1"]              = GameVariables::kInventaire1;
		s_map["inventaire2"]              = GameVariables::kInventaire2;
		s_map["inventaire3"]              = GameVariables::kInventaire3;
		s_map["inventaire4"]              = GameVariables::kInventaire4;
		s_map["inventaire5"]              = GameVariables::kInventaire5;
		s_map["inventaire6"]              = GameVariables::kInventaire6;
		s_map["inventaire7"]              = GameVariables::kInventaire7;
		s_map["inventaire8"]              = GameVariables::kInventaire8;
		s_map["inventaire9"]              = GameVariables::kInventaire9;
		s_map["Amulette"]                 = GameVariables::kAmulette;
		s_map["Anoouver"]                 = GameVariables::kAnoouver;
		s_map["Bague"]                    = GameVariables::kBague;
		s_map["Baton"]                    = GameVariables::kBaton;
		s_map["Bol"]                      = GameVariables::kBol;
		s_map["Boom"]                     = GameVariables::kBoom;
		s_map["Butin5"]                   = GameVariables::kButin5;
		s_map["Butin6"]                   = GameVariables::kButin6;
		s_map["Cheville"]                 = GameVariables::kCheville;
		s_map["Coffre"]                   = GameVariables::kCoffre;
		s_map["CollierL"]                 = GameVariables::kCollierL;
		s_map["Colonne"]                  = GameVariables::kColonne;
		s_map["Coupelle"]                 = GameVariables::kCoupelle;
		s_map["Couteau"]                  = GameVariables::kCouteau;
		s_map["Deben"]                    = GameVariables::kDeben;
		s_map["Echelle"]                  = GameVariables::kEchelle;
		s_map["Etoupe"]                   = GameVariables::kEtoupe;
		s_map["HypoPlan"]                 = GameVariables::kHypoPlan;
		s_map["Lampe"]                    = GameVariables::kLampe;
		s_map["ListApel"]                 = GameVariables::kListApel;
		s_map["Maquil1"]                  = GameVariables::kMaquil1;
		s_map["Mekhet"]                   = GameVariables::kMekhet;
		s_map["NoeudTit"]                 = GameVariables::kNoeudTit;
		s_map["OeilOudj"]                 = GameVariables::kOeilOudj;
		s_map["Ostracon"]                 = GameVariables::kOstracon;
		s_map["Ouadj"]                    = GameVariables::kOuadj;
		s_map["Papymag1"]                 = GameVariables::kPapymag1;
		s_map["Perruque"]                 = GameVariables::kPerruque;
		s_map["Planche"]                  = GameVariables::kPlanche;
		s_map["PotArgen"]                 = GameVariables::kPotArgen;
		s_map["PotGros"]                  = GameVariables::kPotGros;
		s_map["PotPetit"]                 = GameVariables::kPotPetit;
		s_map["Revers"]                   = GameVariables::kRevers;
		s_map["Scarabe"]                  = GameVariables::kScarabe;
		s_map["Senet"]                    = GameVariables::kSenet;
		s_map["Serpent"]                  = GameVariables::kSerpent;
		s_map["Statuete"]                 = GameVariables::kStatuete;
		s_map["Torche"]                   = GameVariables::kTorche;
		s_map["VaseOr"]                   = GameVariables::kVaseOr;
		s_map["Vautour"]                  = GameVariables::kVautour;
		s_map["Accouchement_deja_vu"]     = GameVariables::kAccouchement_deja_vu;
		s_map["ArriveeCatafalque"]        = GameVariables::kArriveeCatafalque;
		s_map["Bague_presentee"]          = GameVariables::kBague_presentee;
		s_map["BagueRecue"]               = GameVariables::kBagueRecue;
		s_map["Boire"]                    = GameVariables::kBoire;
		s_map["BoissonN"]                 = GameVariables::kBoissonN;
		s_map["BoissonP"]                 = GameVariables::kBoissonP;
		s_map["Cabaretiere"]              = GameVariables::kCabaretiere;
		s_map["Cabaretiere_Achetee"]      = GameVariables::kCabaretiere_Achetee;
		s_map["CompteurCheville"]         = GameVariables::kCompteurCheville;
		s_map["DepartCatafalque"]         = GameVariables::kDepartCatafalque;
		s_map["Depart_Cabaretiere_Ramose"] = GameVariables::kDepart_Cabaretiere_Ramose;
		s_map["Echelle_Posee"]            = GameVariables::kEchelle_Posee;
		s_map["Entree_Maison_Hori"]       = GameVariables::kEntree_Maison_Hori;
		s_map["Etoupe_Sur_Lampe"]         = GameVariables::kEtoupe_Sur_Lampe;
		s_map["NbCaseM45"]                = GameVariables::kNbCaseM45;
		s_map["NbChanceEnigmeA17"]        = GameVariables::kNbChanceEnigmeA17;
		s_map["NbChanceStel"]             = GameVariables::kNbChanceStel;
		s_map["Ouverture_Porte_D05"]      = GameVariables::kOuverture_Porte_D05;
		s_map["Ouvrier_Excede"]           = GameVariables::kOuvrier_Excede;
		s_map["PassageK19Ok"]             = GameVariables::kPassageK19Ok;
		s_map["Ram_Tue_Cobra"]            = GameVariables::kRam_Tue_Cobra;
		s_map["Ram_voir_cobra"]           = GameVariables::kRam_voir_cobra;
		s_map["Ram_Voir_Puits"]           = GameVariables::kRam_Voir_Puits;
		s_map["Ramose_Baton"]             = GameVariables::kRamose_Baton;
		s_map["ReponseViseeOk"]           = GameVariables::kReponseViseeOk;
		s_map["TimeBolPose"]              = GameVariables::kTimeBolPose;
		s_map["FlagAccesS40Truie"]        = GameVariables::kFlagAccesS40Truie;
		s_map["FlagAllerN06"]             = GameVariables::kFlagAllerN06;
		s_map["FlagAmuletteA14Prise"]     = GameVariables::kFlagAmuletteA14Prise;
		s_map["FlagAmulettePrise"]        = GameVariables::kFlagAmulettePrise;
		s_map["FlagAmuletteReconstitue"]  = GameVariables::kFlagAmuletteReconstitue;
		s_map["FlagAnneauOuvertPris"]     = GameVariables::kFlagAnneauOuvertPris;
		s_map["FlagArriveeA09A10"]        = GameVariables::kFlagArriveeA09A10;
		s_map["FlagBagueSpriteS43"]       = GameVariables::kFlagBagueSpriteS43;
		s_map["FlagBaton"]                = GameVariables::kFlagBaton;
		s_map["FlagBolPose"]              = GameVariables::kFlagBolPose;
		s_map["FlagBolPris"]              = GameVariables::kFlagBolPris;
		s_map["FlagBoomPris"]             = GameVariables::kFlagBoomPris;
		s_map["FlagButin1Pris"]           = GameVariables::kFlagButin1Pris;
		s_map["FlagButin5Actionner"]      = GameVariables::kFlagButin5Actionner;
		s_map["FlagButin5Pris"]           = GameVariables::kFlagButin5Pris;
		s_map["FlagButin5Voir"]           = GameVariables::kFlagButin5Voir;
		s_map["FlagCabBague"]             = GameVariables::kFlagCabBague;
		s_map["FlagCase1EnigmeA17"]       = GameVariables::kFlagCase1EnigmeA17;
		s_map["FlagCase2EnigmeA17"]       = GameVariables::kFlagCase2EnigmeA17;
		s_map["FlagCase3EnigmeA17"]       = GameVariables::kFlagCase3EnigmeA17;
		s_map["FlagCase4EnigmeA17"]       = GameVariables::kFlagCase4EnigmeA17;
		s_map["FlagCaseM45Heri"]          = GameVariables::kFlagCaseM45Heri;
		s_map["FlagCaseM45Nefer"]         = GameVariables::kFlagCaseM45Nefer;
		s_map["FlagCaseM45Pedjet"]        = GameVariables::kFlagCaseM45Pedjet;
		s_map["FlagCaseM45Ptah"]          = GameVariables::kFlagCaseM45Ptah;
		s_map["FlagChevillePrise"]        = GameVariables::kFlagChevillePrise;
		s_map["FlagCoffreActionne"]       = GameVariables::kFlagCoffreActionne;
		s_map["FlagCoffrePris"]           = GameVariables::kFlagCoffrePris;
		s_map["FlagCollierLDonne"]        = GameVariables::kFlagCollierLDonne;
		s_map["FlagCollierLPris"]         = GameVariables::kFlagCollierLPris;
		s_map["FlagCompteurEnigme2"]      = GameVariables::kFlagCompteurEnigme2;
		s_map["FlagCouffinD85COUFA"]      = GameVariables::kFlagCouffinD85COUFA;
		s_map["FlagCoupellePrise"]        = GameVariables::kFlagCoupellePrise;
		s_map["FlagCouteauPris"]          = GameVariables::kFlagCouteauPris;
		s_map["FlagDejaEntreN06"]         = GameVariables::kFlagDejaEntreN06;
		s_map["FlagDessinateurMort"]      = GameVariables::kFlagDessinateurMort;
		s_map["FlagDial1K50"]             = GameVariables::kFlagDial1K50;
		s_map["FlagDial2K50"]             = GameVariables::kFlagDial2K50;
		s_map["FlagDial3K50"]             = GameVariables::kFlagDial3K50;
		s_map["FlagDialAutoM27"]          = GameVariables::kFlagDialAutoM27;
		s_map["FlagDialAutoM40"]          = GameVariables::kFlagDialAutoM40;
		s_map["FlagDialAutoM42"]          = GameVariables::kFlagDialAutoM42;
		s_map["FlagDialAutoS31"]          = GameVariables::kFlagDialAutoS31;
		s_map["FlagDialD04"]              = GameVariables::kFlagDialD04;
		s_map["FlagDialD72"]              = GameVariables::kFlagDialD72;
		s_map["FlagDialD72_1"]            = GameVariables::kFlagDialD72_1;
		s_map["FlagDialD72_2"]            = GameVariables::kFlagDialD72_2;
		s_map["FlagDialIntendanteMonte"]  = GameVariables::kFlagDialIntendanteMonte;
		s_map["FlagDialK38"]              = GameVariables::kFlagDialK38;
		s_map["FlagDialScreen"]           = GameVariables::kFlagDialScreen;
		s_map["FlagDialSMT0001"]          = GameVariables::kFlagDialSMT0001;
		s_map["FlagDialogueK41"]          = GameVariables::kFlagDialogueK41;
		s_map["FlagDialogueK50"]          = GameVariables::kFlagDialogueK50;
		s_map["FlagEchellePrise"]         = GameVariables::kFlagEchellePrise;
		s_map["FlagEmbaumeurA09Parti"]    = GameVariables::kFlagEmbaumeurA09Parti;
		s_map["FlagEmbaumeurA17Assome"]   = GameVariables::kFlagEmbaumeurA17Assome;
		s_map["FlagEmbaumeurA17Parti"]    = GameVariables::kFlagEmbaumeurA17Parti;
		s_map["FlagEnchainementWARP_HNM"] = GameVariables::kFlagEnchainementWARP_HNM;
		s_map["FlagEnigme2_9"]            = GameVariables::kFlagEnigme2_9;
		s_map["FlagEnigme2_10"]           = GameVariables::kFlagEnigme2_10;
		s_map["FlagEnigme2_11"]           = GameVariables::kFlagEnigme2_11;
		s_map["FlagEnigme2_12"]           = GameVariables::kFlagEnigme2_12;
		s_map["FlagEnigme2_13"]           = GameVariables::kFlagEnigme2_13;
		s_map["FlagEnigme2_14"]           = GameVariables::kFlagEnigme2_14;
		s_map["FlagEnigme2_15"]           = GameVariables::kFlagEnigme2_15;
		s_map["FlagEnigme2_16"]           = GameVariables::kFlagEnigme2_16;
		s_map["FlagEnigme2_17"]           = GameVariables::kFlagEnigme2_17;
		s_map["FlagEnigme2_18"]           = GameVariables::kFlagEnigme2_18;
		s_map["FlagEnigme2_19"]           = GameVariables::kFlagEnigme2_19;
		s_map["FlagEnigme2_20"]           = GameVariables::kFlagEnigme2_20;
		s_map["FlagEnigme2_21"]           = GameVariables::kFlagEnigme2_21;
		s_map["FlagEnigme2_22"]           = GameVariables::kFlagEnigme2_22;
		s_map["FlagEnigme2_23"]           = GameVariables::kFlagEnigme2_23;
		s_map["FlagEnigme2_24"]           = GameVariables::kFlagEnigme2_24;
		s_map["FlagEnigmeA17"]            = GameVariables::kFlagEnigmeA17;
		s_map["FlagEnigmeCollierL"]       = GameVariables::kFlagEnigmeCollierL;
		s_map["FlagEnigmeColonne"]        = GameVariables::kFlagEnigmeColonne;
		s_map["FlagEnigmeNoeudTit"]       = GameVariables::kFlagEnigmeNoeudTit;
		s_map["FlagEnigmeOeilOudj"]       = GameVariables::kFlagEnigmeOeilOudj;
		s_map["FlagEnigmeOuadj"]          = GameVariables::kFlagEnigmeOuadj;
		s_map["FlagEnigmeScarabe"]        = GameVariables::kFlagEnigmeScarabe;
		s_map["FlagEnigmeSerpent"]        = GameVariables::kFlagEnigmeSerpent;
		s_map["FlagEnigmeVautour"]        = GameVariables::kFlagEnigmeVautour;
		s_map["FlagEntreeM32"]            = GameVariables::kFlagEntreeM32;
		s_map["FlagEntreeS01"]            = GameVariables::kFlagEntreeS01;
		s_map["FlagEtatClepsydre"]        = GameVariables::kFlagEtatClepsydre;
		s_map["FlagEtoupePris"]           = GameVariables::kFlagEtoupePris;
		s_map["FlagForceDialN06Stel"]     = GameVariables::kFlagForceDialN06Stel;
		s_map["FlagGrosPotPris"]          = GameVariables::kFlagGrosPotPris;
		s_map["FlagHorologueK38Parti"]    = GameVariables::kFlagHorologueK38Parti;
		s_map["FlagK19Butin5"]            = GameVariables::kFlagK19Butin5;
		s_map["FlagK19Butin6"]            = GameVariables::kFlagK19Butin6;
		s_map["FlagLampeHuilePrise"]      = GameVariables::kFlagLampeHuilePrise;
		s_map["FlagLettrePrise"]          = GameVariables::kFlagLettrePrise;
		s_map["FlagMaquil1Porte"]         = GameVariables::kFlagMaquil1Porte;
		s_map["FlagMaquil1Pris"]          = GameVariables::kFlagMaquil1Pris;
		s_map["FlagMessageCode1"]         = GameVariables::kFlagMessageCode1;
		s_map["FlagMessageCode2"]         = GameVariables::kFlagMessageCode2;
		s_map["FlagMessageCode3"]         = GameVariables::kFlagMessageCode3;
		s_map["FlagMessageCode4"]         = GameVariables::kFlagMessageCode4;
		s_map["FlagMouseVisee"]           = GameVariables::kFlagMouseVisee;
		s_map["FlagNbButinMontre"]        = GameVariables::kFlagNbButinMontre;
		s_map["FlagNbFlecheTire"]         = GameVariables::kFlagNbFlecheTire;
		s_map["FlagNicheM21Ouverte"]      = GameVariables::kFlagNicheM21Ouverte;
		s_map["FlagNiveau6Temps2"]        = GameVariables::kFlagNiveau6Temps2;
		s_map["FlagOstraconPose"]         = GameVariables::kFlagOstraconPose;
		s_map["FlagPapyrusIntegre"]       = GameVariables::kFlagPapyrusIntegre;
		s_map["FlagPapyrusPris"]          = GameVariables::kFlagPapyrusPris;
		s_map["FlagPerruquePorte"]        = GameVariables::kFlagPerruquePorte;
		s_map["FlagPerruquePris"]         = GameVariables::kFlagPerruquePris;
		s_map["FlagPetitPotPris"]         = GameVariables::kFlagPetitPotPris;
		s_map["FlagPlanche"]              = GameVariables::kFlagPlanche;
		s_map["FlagPlancheRelevee"]       = GameVariables::kFlagPlancheRelevee;
		s_map["FlagPlancheUse"]           = GameVariables::kFlagPlancheUse;
		s_map["FlagPlayChat"]             = GameVariables::kFlagPlayChat;
		s_map["FlagPoignardPris"]         = GameVariables::kFlagPoignardPris;
		s_map["FlagPorteA05"]             = GameVariables::kFlagPorteA05;
		s_map["FlagPorteA05EmbaumeParti"] = GameVariables::kFlagPorteA05EmbaumeParti;
		s_map["FlagPorteA07"]             = GameVariables::kFlagPorteA07;
		s_map["FlagPorteTchaiOuverte"]    = GameVariables::kFlagPorteTchaiOuverte;
		s_map["FlagRamoseHabille"]        = GameVariables::kFlagRamoseHabille;
		s_map["FlagReponseVisee"]         = GameVariables::kFlagReponseVisee;
		s_map["FlagS40Ostracon"]          = GameVariables::kFlagS40Ostracon;
		s_map["FlagS40Pierre"]            = GameVariables::kFlagS40Pierre;
		s_map["FlagSablier"]              = GameVariables::kFlagSablier;
		s_map["FlagSenetPris"]            = GameVariables::kFlagSenetPris;
		s_map["FlagSocleOuvert"]          = GameVariables::kFlagSocleOuvert;
		s_map["FlagSoundChat"]            = GameVariables::kFlagSoundChat;
		s_map["FlagStatuetteDeplacee"]    = GameVariables::kFlagStatuetteDeplacee;
		s_map["FlagStatuettePrise"]       = GameVariables::kFlagStatuettePrise;
		s_map["FlagTeleporteK12"]         = GameVariables::kFlagTeleporteK12;
		s_map["FlagTimerA05"]             = GameVariables::kFlagTimerA05;
		s_map["FlagTimerCab"]             = GameVariables::kFlagTimerCab;
		s_map["FlagTiroirDroitOuvert"]    = GameVariables::kFlagTiroirDroitOuvert;
		s_map["FlagTuniquePris"]          = GameVariables::kFlagTuniquePris;
		s_map["FlagUseGrosPot"]           = GameVariables::kFlagUseGrosPot;
		s_map["FlagUseK50Butin5"]         = GameVariables::kFlagUseK50Butin5;
		s_map["FlagUseK50Butin6"]         = GameVariables::kFlagUseK50Butin6;
		s_map["FlagUseSenet"]             = GameVariables::kFlagUseSenet;
		s_map["FlagVaseOrPris"]           = GameVariables::kFlagVaseOrPris;
		s_map["IndiceVisuel01S03CART"]    = GameVariables::kIndiceVisuel01S03CART;
		s_map["IndiceVisuel02S06DJED"]    = GameVariables::kIndiceVisuel02S06DJED;
		s_map["IndiceVisuel03S06TIT"]     = GameVariables::kIndiceVisuel03S06TIT;
		s_map["IndiceVisuel04S08DJAT"]    = GameVariables::kIndiceVisuel04S08DJAT;
		s_map["IndiceVisuel05S40TRUIE"]   = GameVariables::kIndiceVisuel05S40TRUIE;
		s_map["IndiceVisuel06S44PTAH"]    = GameVariables::kIndiceVisuel06S44PTAH;
		s_map["IndiceVisuel07D63CHEV"]    = GameVariables::kIndiceVisuel07D63CHEV;
		s_map["IndiceVisuel08A21HERI"]    = GameVariables::kIndiceVisuel08A21HERI;
		s_map["IndiceVisuel09N03CHAT"]    = GameVariables::kIndiceVisuel09N03CHAT;
		s_map["IndiceVisuel10N07SENET"]   = GameVariables::kIndiceVisuel10N07SENET;
	}
	VarMap::const_iterator it = s_map.find(name);
	return it != s_map.end() ? it->_value : -1;
}

void CryOmni3DEngine_Egypt::setGameVar(const Common::String &name, int value) {
	int idx = gameVarIndex(name);
	if (idx >= 0) {
		_gameVariables[idx] = (uint)value;
	} else {
		warning("Egypt: setGameVar: unknown variable '%s'", name.c_str());
	}
}

bool CryOmni3DEngine_Egypt::runPrototypeWarpScript(int zoneClick, double sourceAlpha, double sourceBeta) {
	Common::Array<Common::String> blockLines;
	bool inWarpBlock = false;

	for (Common::Array<Common::String>::const_iterator it = _currentScene.scriptLines.begin();
	     it != _currentScene.scriptLines.end(); ++it) {
		if (it->equalsIgnoreCase("warpinit")) {
			inWarpBlock = true;
			blockLines.push_back(*it);
			continue;
		}

		if (!inWarpBlock)
			continue;

		blockLines.push_back(*it);
		if (it->equalsIgnoreCase("endwarp"))
			break;
	}

	if (blockLines.empty()) {
		warning("Egypt: no warp script block found for %s", _currentScene.name.c_str());
		return false;
	}

	_gameVariables[GameVariables::kZoneclic] = zoneClick;
	warning("Egypt: prototype zoneclic=%d for scene %s",
	        zoneClick, _currentScene.name.c_str());

	return executeScriptBlock(blockLines, zoneClick, sourceAlpha, sourceBeta);
}

bool CryOmni3DEngine_Egypt::executeScriptBlock(const Common::Array<Common::String> &lines, uint zoneClick,
                                               double sourceAlpha, double sourceBeta) {
	Common::HashMap<Common::String, uint> labels;
	bool producedState = false;

	for (uint i = 0; i < lines.size(); ++i) {
		Common::String trimmed = lines[i];
		trimmed.trim();
		if (!trimmed.empty() && trimmed.hasSuffix(":")) {
			Common::String label = trimmed;
			label.deleteLastChar();
			if (!label.empty())
				labels[label] = i;
		}
	}

	for (uint pc = 0; pc < lines.size(); ++pc) {
		Common::String line = lines[pc];
		line.trim();
		if (line.empty() || line.equalsIgnoreCase("warpinit") || line.equalsIgnoreCase("endinit") ||
		    line.equalsIgnoreCase("endwarp") || line.hasPrefixIgnoreCase("centrage") || line.hasSuffix(":") ||
		    line.hasPrefix("//")) {
			continue;
		}

		if (line.hasPrefixIgnoreCase("if ")) {
			Common::String expression = line.substr(3);
			int gotoPos = expression.find(" goto ");
			if (gotoPos >= 0) {
				Common::String condition = expression.substr(0, gotoPos);
				Common::String label = expression.substr(gotoPos + 6);
				label.trim();
				if (label.hasSuffix("!"))
					label.deleteLastChar();

				if (evaluateScriptCondition(condition) && labels.contains(label))
					pc = labels[label];
			} else {
				int commandPos = expression.find(' ');
				if (commandPos > 0) {
					Common::String condition = expression.substr(0, commandPos);
					Common::String command = expression.substr(commandPos + 1);
					condition.trim();
					command.trim();

					if (evaluateScriptCondition(condition)) {
						executeScriptCommand(command, labels, pc, zoneClick, sourceAlpha, sourceBeta, producedState);
						if (!_pendingWarpTarget.empty())
							return producedState;
					}
				}
			}
			continue;
		}

		executeScriptCommand(line, labels, pc, zoneClick, sourceAlpha, sourceBeta, producedState);
		if (!_pendingWarpTarget.empty())
			return producedState;
	}

	return producedState;
}

bool CryOmni3DEngine_Egypt::executeScriptCommand(const Common::String &rawLine,
                                                 const Common::HashMap<Common::String, uint> &labels,
                                                 uint &pc, uint zoneClick, double sourceAlpha,
                                                 double sourceBeta, bool &producedState) {
	Common::String line = rawLine;
	line.trim();
	if (line.empty())
		return false;

	if (line.hasPrefixIgnoreCase("zoneactive ")) {
		Common::String value = line.substr(11);
		value.trim();
		uint zoneId = (uint)atoi(value.c_str());
		// EXE (0x412a16): zoneactive is a no-op when holding an object.
		// The dialogue may give an item via "let main=X" without going through
		// PRENDRE, leaving the pickup variable at 0 and the zone seemingly
		// eligible — but the EXE's held-object guard prevents re-activation.
		if (getScriptVariableValue("main") != 0)
			return true;
		bool alreadyActive = false;
		for (Common::Array<uint>::const_iterator activeIt = _currentScene.activeZones.begin();
		     activeIt != _currentScene.activeZones.end(); ++activeIt) {
			if (*activeIt == zoneId) {
				alreadyActive = true;
				break;
			}
		}
		if (zoneId != 0 && !alreadyActive) {
			_currentScene.activeZones.push_back(zoneId);
			producedState = true;
		}
		return true;
	}

	if (line.hasPrefixIgnoreCase("zoneinactive ")) {
		Common::String value = line.substr(13);
		value.trim();
		uint zoneId = (uint)atoi(value.c_str());
		for (Common::Array<uint>::iterator it = _currentScene.activeZones.begin();
		     it != _currentScene.activeZones.end(); ++it) {
			if (*it == zoneId) {
				_currentScene.activeZones.remove_at(it - _currentScene.activeZones.begin());
				producedState = true;
				break;
			}
		}
		return true;
	}

	if (line.hasPrefixIgnoreCase("let ")) {
		setScriptVariable(line.substr(4));
		producedState = true;
		return true;
	}

	if (line.hasPrefixIgnoreCase("aller_warp ")) {
		Common::String value = line.substr(11);
		value.trim();
		if (value.hasSuffix("!"))
			value.deleteLastChar();
		if (queuePrototypeSceneChange((uint)atoi(value.c_str()), "aller_warp", zoneClick, false,
		                              sourceAlpha, sourceBeta)) {
			producedState = true;
		}
		return true;
	}

	if (line.hasPrefixIgnoreCase("aller_hnm_warp ")) {
		Common::String value = line.substr(15);
		value.trim();
		if (value.hasSuffix("!"))
			value.deleteLastChar();
		const uint hnmZoneId = (uint)atoi(value.c_str());
		const EgyptZone *hnmZone = findZoneById(hnmZoneId);
		if (hnmZone && hnmZone->targetWarp.equalsIgnoreCase("NULL")) {
			// Play HNM sequence in-place, stay on current scene
			Common::String joined;
			for (uint si = 0; si < hnmZone->hnmSequence.size(); ++si) {
				if (si > 0) joined += "/";
				joined += hnmZone->hnmSequence[si];
			}
			if (!joined.empty())
				executeHnmSequence(joined);
			producedState = true;
		} else {
			if (queuePrototypeSceneChange(hnmZoneId, "aller_hnm_warp", zoneClick, true,
			                              sourceAlpha, sourceBeta)) {
				producedState = true;
			}
		}
		return true;
	}

	if (line.hasPrefixIgnoreCase("goto ")) {
		Common::String label = line.substr(5);
		label.trim();
		if (label.hasSuffix("!"))
			label.deleteLastChar();
		if (labels.contains(label))
			pc = labels[label];
		return true;
	}

	if (line.hasPrefixIgnoreCase("editspr ")) {
		Common::String args = line.substr(8);
		args.trim();
		int base = 0, count = 1, unused = 0;
		sscanf(args.c_str(), "%d %d %d", &base, &count, &unused);
		if (_sceneOverlayData.empty())
			loadSceneOverlay(_currentScene.name);
		EgyptOverlayCatalogEntry entry;
		entry.base = (uint32)MAX(0, base);
		entry.count = (uint32)MAX(1, count);
		entry.counter = 0;
		_sceneOverlayCatalog.push_back(entry);
		warning("Egypt: editspr catalog[%u] base=%u count=%u",
		        (uint)(_sceneOverlayCatalog.size() - 1), entry.base, entry.count);
		return true;
	}

	if (line.equalsIgnoreCase("decompress")) {
		// Reset the panorama to the clean decoded WARP/HNM state, without any
		// animspr overlays. The script then redraws only the sprites still needed.
		_sceneSprPixels.clear();
		_sceneSprDirty = true;
		warning("Egypt: decompress — panorama reset");
		return true;
	}

	if (line.hasPrefixIgnoreCase("animspr ")) {
		Common::String args = line.substr(8);
		args.trim();
		const int n = atoi(args.c_str());
		if (n >= 1 && (uint)(n - 1) < _sceneOverlayCatalog.size()) {
			EgyptOverlayCatalogEntry &entry = _sceneOverlayCatalog[(uint)(n - 1)];
			const uint32 now = g_system->getMillis();
			if (now - entry.lastTick < 70)
				return true;
			entry.lastTick = now;
			const uint frameIdx = entry.base + entry.counter;
			decodeSceneSprFrame(frameIdx);
			entry.counter = (entry.counter + 1) % entry.count;
			warning("Egypt: animspr %d → scene SPR frame %u (next counter=%u)", n, frameIdx, entry.counter);
		} else {
			warning("Egypt: animspr %d out of range (catalog size=%u)", n, (uint)_sceneOverlayCatalog.size());
		}
		return true;
	}

	if (line.hasPrefixIgnoreCase("fonction ")) {
		Common::String numStr = line.substr(9);
		numStr.trim();
		const int funcNum = atoi(numStr.c_str());
		// FADE_IN = 6, FADE_OUT = 7 (constants defined in EGYPTE.DEF)
		if (funcNum == 6) {
			performScreenFade(false);
		} else if (funcNum == 7) {
			performScreenFade(true);
		} else {
			logUnsupportedScriptCommand(line);
		}
		return true;
	}

	if (line.hasPrefixIgnoreCase("dialoguer ")) {
		Common::String numStr = line.substr(10);
		numStr.trim();
		const uint zoneId = (uint)atoi(numStr.c_str());
		const EgyptZone *dlgZone = findZoneById(zoneId);
		if (dlgZone && dlgZone->commandName.equalsIgnoreCase("DIALOGUER")) {
			// Zone arg format: NAME-DIAL-LABEL (e.g. MONTOUMES-DIAL-SMT0001)
			// Extract the portion after the second dash.
			const Common::String &arg = dlgZone->label;
			int firstDash = arg.find('-');
			if (firstDash >= 0) {
				int secondDash = arg.find('-', firstDash + 1);
				if (secondDash >= 0) {
					_dialoguePendingLabel = arg.substr(secondDash + 1);
					if (_dialoguePendingLabel.hasSuffix("!"))
						_dialoguePendingLabel.deleteLastChar();
					_dialoguePendingLabel.toLowercase();
					warning("Egypt: dialoguer %u → label '%s'",
					        zoneId, _dialoguePendingLabel.c_str());
				}
			}
		} else {
			warning("Egypt: dialoguer %u — zone not found or not DIALOGUER", zoneId);
		}
		return true;
	}

	static const char *const kSafeNoopPrefixes[] = {
		"music", "stopmusic", "sound", "sounds", "bmouse",
		"show", "hide", "son_3d", "inventaire", "and"
	};
	for (uint i = 0; i < ARRAYSIZE(kSafeNoopPrefixes); ++i) {
		if (line.hasPrefixIgnoreCase(kSafeNoopPrefixes[i])) {
			logUnsupportedScriptCommand(line);
			return true;
		}
	}

	logUnsupportedScriptCommand(line);
	return false;
}

bool CryOmni3DEngine_Egypt::queuePrototypeSceneChange(uint zoneId, const char *reason, uint zoneClick,
                                                      bool viaHnm, double sourceAlpha, double sourceBeta) {
	const EgyptZone *zone = findZoneById(zoneId);
	if (!zone) {
		warning("Egypt: %s references unknown zone %u in scene %s",
		        reason, zoneId, _currentScene.name.c_str());
		return false;
	}

	if (zone->targetWarp.empty()) {
		warning("Egypt: %s references zone %u without target warp in scene %s",
		        reason, zoneId, _currentScene.name.c_str());
		return false;
	}

	bool sourceOrientationAvailable = _currentViewAnglesAvailable;
	if (!sourceOrientationAvailable && sourceAlpha == 0.0 && sourceBeta == 0.0)
		warning("Egypt: source orientation unavailable, using 0/0 for warp from %s via %s",
		        _currentScene.name.c_str(), reason);
	rememberPendingArrival(*zone, zoneClick, viaHnm, sourceOrientationAvailable, sourceAlpha, sourceBeta, reason);
	_pendingWarpTarget = resolvePrototypeWarpTarget(zone->targetWarp);
	warning("Egypt: prototype queued %s via zone %03u from %s to %s (target %s param=%s extra=%s)",
	        reason, zone->id, _currentScene.name.c_str(), _pendingWarpTarget.c_str(),
	        zone->targetWarp.c_str(), zone->param.c_str(), zone->extraParam.c_str());
	return true;
}

bool CryOmni3DEngine_Egypt::evaluateScriptCondition(const Common::String &expression) const {
	Common::String condition = expression;
	condition.trim();

	// Handle "and" / "or" conjunctions (case-insensitive) by splitting recursively.
	Common::String condLower = condition;
	condLower.toLowercase();
	int andPos = condLower.find(" and ");
	if (andPos >= 0)
		return evaluateScriptCondition(condition.substr(0, andPos)) &&
		       evaluateScriptCondition(condition.substr(andPos + 5));
	int orPos = condLower.find(" or ");
	if (orPos >= 0)
		return evaluateScriptCondition(condition.substr(0, orPos)) ||
		       evaluateScriptCondition(condition.substr(orPos + 4));

	struct Operator {
		const char *symbol;
		int length;
	};
	static const Operator kOperators[] = {
		{"!=", 2}, {"<=", 2}, {">=", 2}, {"<", 1}, {">", 1}, {"=", 1}
	};

	for (uint i = 0; i < ARRAYSIZE(kOperators); ++i) {
		int operatorPos = condition.find(kOperators[i].symbol);
		if (operatorPos < 0)
			continue;

		Common::String left = condition.substr(0, operatorPos);
		Common::String right = condition.substr(operatorPos + kOperators[i].length);
		left.trim();
		right.trim();

		const int leftValue = getScriptVariableValue(left);
		const int rightValue = resolveScriptValue(right);

		if (strcmp(kOperators[i].symbol, "!=") == 0)
			return leftValue != rightValue;
		if (strcmp(kOperators[i].symbol, "<=") == 0)
			return leftValue <= rightValue;
		if (strcmp(kOperators[i].symbol, ">=") == 0)
			return leftValue >= rightValue;
		if (strcmp(kOperators[i].symbol, "<") == 0)
			return leftValue < rightValue;
		if (strcmp(kOperators[i].symbol, ">") == 0)
			return leftValue > rightValue;
		return leftValue == rightValue;
	}

	return false;
}

int CryOmni3DEngine_Egypt::resolveScriptValue(const Common::String &token) const {
	Common::String value = token;
	value.trim();
	if (value.empty())
		return 0;

	const char firstChar = value[0];
	if ((firstChar >= '0' && firstChar <= '9') || firstChar == '-' || firstChar == '+')
		return atoi(value.c_str());

	int idx = gameVarIndex(value);
	if (idx >= 0)
		return (int)_gameVariables[idx];

	Common::HashMap<Common::String, int, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo>::const_iterator it =
		_scriptConstants.find(value);
	if (it != _scriptConstants.end())
		return it->_value;

	return 0;
}

int CryOmni3DEngine_Egypt::getScriptVariableValue(const Common::String &name) const {
	int idx = gameVarIndex(name);
	if (idx >= 0)
		return (int)_gameVariables[idx];

	Common::HashMap<Common::String, int, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo>::const_iterator it =
		_scriptConstants.find(name);
	if (it != _scriptConstants.end())
		return it->_value;

	return 0;
}

void CryOmni3DEngine_Egypt::setScriptVariable(const Common::String &assignment) {
	struct OpEntry { const char *sym; int len; };
	static const OpEntry kOps[] = {
		{ "+=", 2 }, { "-=", 2 }, { "*=", 2 }, { "/=", 2 }, { "=", 1 }
	};

	Common::String op;
	int separatorPos = -1;
	for (uint oi = 0; oi < ARRAYSIZE(kOps); ++oi) {
		int pos = assignment.find(kOps[oi].sym);
		if (pos >= 0) {
			op = kOps[oi].sym;
			separatorPos = pos;
			break;
		}
	}

	if (separatorPos < 0)
		return;

	Common::String name = assignment.substr(0, separatorPos);
	Common::String value = assignment.substr(separatorPos + op.size());
	name.trim();
	value.trim();

	int rhs = resolveScriptValue(value);
	int result;
	if (op == "+=")
		result = getScriptVariableValue(name) + rhs;
	else if (op == "-=")
		result = getScriptVariableValue(name) - rhs;
	else if (op == "*=")
		result = getScriptVariableValue(name) * rhs;
	else if (op == "/=")
		result = (rhs != 0) ? getScriptVariableValue(name) / rhs : 0;
	else
		result = rhs;

	int idx = gameVarIndex(name);
	if (idx < 0) {
		warning("Egypt: setScriptVariable: unknown variable '%s'", name.c_str());
		return;
	}

	_gameVariables[idx] = (uint)result;
	warning("Egypt: script variable %s=%d", name.c_str(), result);

	if (idx == GameVariables::kLevel)
		resetScriptTimer();

	// EXE: the PRENDRE handler does objectValues[objectId]++ to mark the object as taken.
	// When a script sets main=X directly (e.g. dialogue giving an item), the PRENDRE
	// handler is bypassed, so the object variable stays at 0 and the pickup zone can
	// re-activate after the item is stored.  Mirror the increment here for any
	// assignment that places a known object into main.
	if (idx == GameVariables::kMain && result > 0) {
		Common::HashMap<Common::String, int,
		    Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo>::const_iterator cit;
		for (cit = _scriptConstants.begin(); cit != _scriptConstants.end(); ++cit) {
			if (cit->_value == result && cit->_key.hasPrefixIgnoreCase("Objet")) {
				const Common::String varName = cit->_key.substr(5); // strip "Objet"
				const int newVal = getScriptVariableValue(varName) + 1;
				setGameVar(varName, newVal);
				warning("Egypt: auto-increment %s=%d (main set via script to %d)",
				        varName.c_str(), newVal, result);
				break;
			}
		}
	}
}

bool CryOmni3DEngine_Egypt::executePrototypeSceneLogic() {
	return !_pendingWarpTarget.empty();
}

const EgyptZone *CryOmni3DEngine_Egypt::findZoneById(uint zoneId) const {
	for (Common::Array<EgyptZone>::const_iterator it = _currentScene.zones.begin();
	     it != _currentScene.zones.end(); ++it) {
		if (it->id == zoneId)
			return &(*it);
	}

	return nullptr;
}

void CryOmni3DEngine_Egypt::logUnsupportedScriptCommand(const Common::String &line) const {
	Common::String token = line;
	int spacePos = token.find(' ');
	if (spacePos >= 0)
		token = token.substr(0, spacePos);

	Common::HashMap<Common::String, bool, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> &logged =
		const_cast<Common::HashMap<Common::String, bool, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> &>(_loggedScriptCommands);
	if (logged.contains(token))
		return;

	logged[token] = true;
	warning("Egypt: unsupported script command in %s: %s",
	        _currentScene.name.c_str(), line.c_str());
}

void CryOmni3DEngine_Egypt::logScriptLine(const Common::String &line) const {
	if (line.equalsIgnoreCase("warpinit") || line.equalsIgnoreCase("endwarp") ||
	    line.equalsIgnoreCase("endinit") || line.equalsIgnoreCase("decompress") ||
	    line.hasSuffix(":") ||
	    line.hasPrefixIgnoreCase("centrage") || line.hasPrefixIgnoreCase("music") ||
	    line.hasPrefixIgnoreCase("stopmusic") || line.hasPrefixIgnoreCase("if ") ||
	    line.hasPrefixIgnoreCase("let ") || line.hasPrefixIgnoreCase("goto ") ||
	    line.hasPrefixIgnoreCase("aller_warp") || line.hasPrefixIgnoreCase("aller_hnm_warp") ||
	    line.hasPrefixIgnoreCase("dialoguer ") || line.hasPrefixIgnoreCase("zoneactive") ||
	    line.hasPrefixIgnoreCase("zoneinactive")) {
		warning("Egypt: script %s", line.c_str());
	}
}

} // End of namespace Egypt
} // End of namespace CryOmni3D
