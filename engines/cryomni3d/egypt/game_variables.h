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

#ifndef CRYOMNI3D_EGYPT_GAME_VARIABLES_H
#define CRYOMNI3D_EGYPT_GAME_VARIABLES_H

namespace CryOmni3D {
namespace Egypt {

// All named script variables used in DEF files and engine code.
// Single source of truth: this X-macro table generates both the
// GameVariables::Var enum and the DEF-name lookup table used by the
// script VM (script.cpp). Indices are stable across saves - append-only.
// Identifiers deliberately keep the French names used by the game's DEF
// scripts so the code stays 1:1 traceable to the game data.
// V(enumSymbol, defName)
#define EGYPT_GAME_VARIABLES(V) \
	V(kZoneclic, "zoneclic")                                  /* 0 */ \
	V(kTmp, "tmp")                                            \
	V(kTimer, "timer")                                        \
	V(kMessage, "message")                                    \
	V(kLevel, "Level")                                        \
	V(kFlagVisite, "FlagVisite")                              \
	V(kEndGame, "EndGame")                                    \
	V(kFlagFinDuJeu, "FlagFinDuJeu")                          \
	V(kInitLevel1, "InitLevel1")                              \
	V(kInitLevel2, "InitLevel2")                              \
	V(kInitLevel3, "InitLevel3")                              /* 10 */ \
	V(kInitLevel4, "InitLevel4")                              \
	V(kInitLevel5, "InitLevel5")                              \
	V(kInitLevel6, "InitLevel6")                              \
	V(kMain, "main")                                          \
	V(kInventaire0, "inventaire0")                            \
	V(kInventaire1, "inventaire1")                            \
	V(kInventaire2, "inventaire2")                            \
	V(kInventaire3, "inventaire3")                            \
	V(kInventaire4, "inventaire4")                            \
	V(kInventaire5, "inventaire5")                            /* 20 */ \
	V(kInventaire6, "inventaire6")                            \
	V(kInventaire7, "inventaire7")                            \
	V(kInventaire8, "inventaire8")                            \
	V(kInventaire9, "inventaire9")                            \
	V(kAmulette, "Amulette")                                  \
	V(kAnoouver, "Anoouver")                                  \
	V(kBague, "Bague")                                        \
	V(kBaton, "Baton")                                        \
	V(kBol, "Bol")                                            \
	V(kBoom, "Boom")                                          /* 30 */ \
	V(kButin5, "Butin5")                                      \
	V(kButin6, "Butin6")                                      \
	V(kCheville, "Cheville")                                  \
	V(kCoffre, "Coffre")                                      \
	V(kCollierL, "CollierL")                                  \
	V(kColonne, "Colonne")                                    \
	V(kCoupelle, "Coupelle")                                  \
	V(kCouteau, "Couteau")                                    \
	V(kDeben, "Deben")                                        \
	V(kEchelle, "Echelle")                                    /* 40 */ \
	V(kEtoupe, "Etoupe")                                      \
	V(kHypoPlan, "HypoPlan")                                  \
	V(kLampe, "Lampe")                                        \
	V(kListApel, "ListApel")                                  \
	V(kMaquil1, "Maquil1")                                    \
	V(kMekhet, "Mekhet")                                      \
	V(kNoeudTit, "NoeudTit")                                  \
	V(kOeilOudj, "OeilOudj")                                  \
	V(kOstracon, "Ostracon")                                  \
	V(kOuadj, "Ouadj")                                        /* 50 */ \
	V(kPapymag1, "Papymag1")                                  \
	V(kPerruque, "Perruque")                                  \
	V(kPlanche, "Planche")                                    \
	V(kPotArgen, "PotArgen")                                  \
	V(kPotGros, "PotGros")                                    \
	V(kPotPetit, "PotPetit")                                  \
	V(kRevers, "Revers")                                      \
	V(kScarabe, "Scarabe")                                    \
	V(kSenet, "Senet")                                        \
	V(kSerpent, "Serpent")                                    /* 60 */ \
	V(kStatuete, "Statuete")                                  \
	V(kTorche, "Torche")                                      \
	V(kVaseOr, "VaseOr")                                      \
	V(kVautour, "Vautour")                                    \
	V(kAccouchement_deja_vu, "Accouchement_deja_vu")          \
	V(kArriveeCatafalque, "ArriveeCatafalque")                \
	V(kBague_presentee, "Bague_presentee")                    \
	V(kBagueRecue, "BagueRecue")                              \
	V(kBoire, "Boire")                                        \
	V(kBoissonN, "BoissonN")                                  /* 70 */ \
	V(kBoissonP, "BoissonP")                                  \
	V(kCabaretiere, "Cabaretiere")                            \
	V(kCabaretiere_Achetee, "Cabaretiere_Achetee")            \
	V(kCompteurCheville, "CompteurCheville")                  \
	V(kDepartCatafalque, "DepartCatafalque")                  \
	V(kDepart_Cabaretiere_Ramose, "Depart_Cabaretiere_Ramose")\
	V(kEchelle_Posee, "Echelle_Posee")                        \
	V(kEntree_Maison_Hori, "Entree_Maison_Hori")              \
	V(kEtoupe_Sur_Lampe, "Etoupe_Sur_Lampe")                  \
	V(kNbCaseM45, "NbCaseM45")                                /* 80 */ \
	V(kNbChanceEnigmeA17, "NbChanceEnigmeA17")                \
	V(kNbChanceStel, "NbChanceStel")                          \
	V(kOuverture_Porte_D05, "Ouverture_Porte_D05")            \
	V(kOuvrier_Excede, "Ouvrier_Excede")                      \
	V(kPassageK19Ok, "PassageK19Ok")                          \
	V(kRam_Tue_Cobra, "Ram_Tue_Cobra")                        \
	V(kRam_voir_cobra, "Ram_voir_cobra")                      \
	V(kRam_Voir_Puits, "Ram_Voir_Puits")                      \
	V(kRamose_Baton, "Ramose_Baton")                          \
	V(kReponseViseeOk, "ReponseViseeOk")                      /* 90 */ \
	V(kTimeBolPose, "TimeBolPose")                            \
	V(kFlagAccesS40Truie, "FlagAccesS40Truie")                \
	V(kFlagAllerN06, "FlagAllerN06")                          \
	V(kFlagAmuletteA14Prise, "FlagAmuletteA14Prise")          \
	V(kFlagAmulettePrise, "FlagAmulettePrise")                \
	V(kFlagAmuletteReconstitue, "FlagAmuletteReconstitue")    \
	V(kFlagAnneauOuvertPris, "FlagAnneauOuvertPris")          \
	V(kFlagArriveeA09A10, "FlagArriveeA09A10")                \
	V(kFlagBagueSpriteS43, "FlagBagueSpriteS43")              \
	V(kFlagBaton, "FlagBaton")                                /* 100 */ \
	V(kFlagBolPose, "FlagBolPose")                            \
	V(kFlagBolPris, "FlagBolPris")                            \
	V(kFlagBoomPris, "FlagBoomPris")                          \
	V(kFlagButin1Pris, "FlagButin1Pris")                      \
	V(kFlagButin5Actionner, "FlagButin5Actionner")            \
	V(kFlagButin5Pris, "FlagButin5Pris")                      \
	V(kFlagButin5Voir, "FlagButin5Voir")                      \
	V(kFlagCabBague, "FlagCabBague")                          \
	V(kFlagCase1EnigmeA17, "FlagCase1EnigmeA17")              \
	V(kFlagCase2EnigmeA17, "FlagCase2EnigmeA17")              /* 110 */ \
	V(kFlagCase3EnigmeA17, "FlagCase3EnigmeA17")              \
	V(kFlagCase4EnigmeA17, "FlagCase4EnigmeA17")              \
	V(kFlagCaseM45Heri, "FlagCaseM45Heri")                    \
	V(kFlagCaseM45Nefer, "FlagCaseM45Nefer")                  \
	V(kFlagCaseM45Pedjet, "FlagCaseM45Pedjet")                \
	V(kFlagCaseM45Ptah, "FlagCaseM45Ptah")                    \
	V(kFlagChevillePrise, "FlagChevillePrise")                \
	V(kFlagCoffreActionne, "FlagCoffreActionne")              \
	V(kFlagCoffrePris, "FlagCoffrePris")                      \
	V(kFlagCollierLDonne, "FlagCollierLDonne")                /* 120 */ \
	V(kFlagCollierLPris, "FlagCollierLPris")                  \
	V(kFlagCompteurEnigme2, "FlagCompteurEnigme2")            \
	V(kFlagCouffinD85COUFA, "FlagCouffinD85COUFA")            \
	V(kFlagCoupellePrise, "FlagCoupellePrise")                \
	V(kFlagCouteauPris, "FlagCouteauPris")                    \
	V(kFlagDejaEntreN06, "FlagDejaEntreN06")                  \
	V(kFlagDessinateurMort, "FlagDessinateurMort")            \
	V(kFlagDial1K50, "FlagDial1K50")                          \
	V(kFlagDial2K50, "FlagDial2K50")                          \
	V(kFlagDial3K50, "FlagDial3K50")                          /* 130 */ \
	V(kFlagDialAutoM27, "FlagDialAutoM27")                    \
	V(kFlagDialAutoM40, "FlagDialAutoM40")                    \
	V(kFlagDialAutoM42, "FlagDialAutoM42")                    \
	V(kFlagDialAutoS31, "FlagDialAutoS31")                    \
	V(kFlagDialD04, "FlagDialD04")                            \
	V(kFlagDialD72, "FlagDialD72")                            \
	V(kFlagDialD72_1, "FlagDialD72_1")                        \
	V(kFlagDialD72_2, "FlagDialD72_2")                        \
	V(kFlagDialIntendanteMonte, "FlagDialIntendanteMonte")    \
	V(kFlagDialK38, "FlagDialK38")                            /* 140 */ \
	V(kFlagDialScreen, "FlagDialScreen")                      \
	V(kFlagDialSMT0001, "FlagDialSMT0001")                    \
	V(kFlagDialogueK41, "FlagDialogueK41")                    \
	V(kFlagDialogueK50, "FlagDialogueK50")                    \
	V(kFlagEchellePrise, "FlagEchellePrise")                  \
	V(kFlagEmbaumeurA09Parti, "FlagEmbaumeurA09Parti")        \
	V(kFlagEmbaumeurA17Assome, "FlagEmbaumeurA17Assome")      \
	V(kFlagEmbaumeurA17Parti, "FlagEmbaumeurA17Parti")        \
	V(kFlagEnchainementWARP_HNM, "FlagEnchainementWARP_HNM")  \
	V(kFlagEnigme2_9, "FlagEnigme2_9")                        /* 150 */ \
	V(kFlagEnigme2_10, "FlagEnigme2_10")                      \
	V(kFlagEnigme2_11, "FlagEnigme2_11")                      \
	V(kFlagEnigme2_12, "FlagEnigme2_12")                      \
	V(kFlagEnigme2_13, "FlagEnigme2_13")                      \
	V(kFlagEnigme2_14, "FlagEnigme2_14")                      \
	V(kFlagEnigme2_15, "FlagEnigme2_15")                      \
	V(kFlagEnigme2_16, "FlagEnigme2_16")                      \
	V(kFlagEnigme2_17, "FlagEnigme2_17")                      \
	V(kFlagEnigme2_18, "FlagEnigme2_18")                      \
	V(kFlagEnigme2_19, "FlagEnigme2_19")                      /* 160 */ \
	V(kFlagEnigme2_20, "FlagEnigme2_20")                      \
	V(kFlagEnigme2_21, "FlagEnigme2_21")                      \
	V(kFlagEnigme2_22, "FlagEnigme2_22")                      \
	V(kFlagEnigme2_23, "FlagEnigme2_23")                      \
	V(kFlagEnigme2_24, "FlagEnigme2_24")                      \
	V(kFlagEnigmeA17, "FlagEnigmeA17")                        \
	V(kFlagEnigmeCollierL, "FlagEnigmeCollierL")              \
	V(kFlagEnigmeColonne, "FlagEnigmeColonne")                \
	V(kFlagEnigmeNoeudTit, "FlagEnigmeNoeudTit")              \
	V(kFlagEnigmeOeilOudj, "FlagEnigmeOeilOudj")              /* 170 */ \
	V(kFlagEnigmeOuadj, "FlagEnigmeOuadj")                    \
	V(kFlagEnigmeScarabe, "FlagEnigmeScarabe")                \
	V(kFlagEnigmeSerpent, "FlagEnigmeSerpent")                \
	V(kFlagEnigmeVautour, "FlagEnigmeVautour")                \
	V(kFlagEntreeM32, "FlagEntreeM32")                        \
	V(kFlagEntreeS01, "FlagEntreeS01")                        \
	V(kFlagEtatClepsydre, "FlagEtatClepsydre")                \
	V(kFlagEtoupePris, "FlagEtoupePris")                      \
	V(kFlagForceDialN06Stel, "FlagForceDialN06Stel")          \
	V(kFlagGrosPotPris, "FlagGrosPotPris")                    /* 180 */ \
	V(kFlagHorologueK38Parti, "FlagHorologueK38Parti")        \
	V(kFlagK19Butin5, "FlagK19Butin5")                        \
	V(kFlagK19Butin6, "FlagK19Butin6")                        \
	V(kFlagLampeHuilePrise, "FlagLampeHuilePrise")            \
	V(kFlagLettrePrise, "FlagLettrePrise")                    \
	V(kFlagMaquil1Porte, "FlagMaquil1Porte")                  \
	V(kFlagMaquil1Pris, "FlagMaquil1Pris")                    \
	V(kFlagMessageCode1, "FlagMessageCode1")                  \
	V(kFlagMessageCode2, "FlagMessageCode2")                  \
	V(kFlagMessageCode3, "FlagMessageCode3")                  /* 190 */ \
	V(kFlagMessageCode4, "FlagMessageCode4")                  \
	V(kFlagMouseVisee, "FlagMouseVisee")                      \
	V(kFlagNbButinMontre, "FlagNbButinMontre")                \
	V(kFlagNbFlecheTire, "FlagNbFlecheTire")                  \
	V(kFlagNicheM21Ouverte, "FlagNicheM21Ouverte")            \
	V(kFlagNiveau6Temps2, "FlagNiveau6Temps2")                \
	V(kFlagOstraconPose, "FlagOstraconPose")                  \
	V(kFlagPapyrusIntegre, "FlagPapyrusIntegre")              \
	V(kFlagPapyrusPris, "FlagPapyrusPris")                    \
	V(kFlagPerruquePorte, "FlagPerruquePorte")                /* 200 */ \
	V(kFlagPerruquePris, "FlagPerruquePris")                  \
	V(kFlagPetitPotPris, "FlagPetitPotPris")                  \
	V(kFlagPlanche, "FlagPlanche")                            \
	V(kFlagPlancheRelevee, "FlagPlancheRelevee")              \
	V(kFlagPlancheUse, "FlagPlancheUse")                      \
	V(kFlagPlayChat, "FlagPlayChat")                          \
	V(kFlagPoignardPris, "FlagPoignardPris")                  \
	V(kFlagPorteA05, "FlagPorteA05")                          \
	V(kFlagPorteA05EmbaumeParti, "FlagPorteA05EmbaumeParti")  \
	V(kFlagPorteA07, "FlagPorteA07")                          /* 210 */ \
	V(kFlagPorteTchaiOuverte, "FlagPorteTchaiOuverte")        \
	V(kFlagRamoseHabille, "FlagRamoseHabille")                \
	V(kFlagReponseVisee, "FlagReponseVisee")                  \
	V(kFlagS40Ostracon, "FlagS40Ostracon")                    \
	V(kFlagS40Pierre, "FlagS40Pierre")                        \
	V(kFlagSablier, "FlagSablier")                            \
	V(kFlagSenetPris, "FlagSenetPris")                        \
	V(kFlagSocleOuvert, "FlagSocleOuvert")                    \
	V(kFlagSoundChat, "FlagSoundChat")                        \
	V(kFlagStatuetteDeplacee, "FlagStatuetteDeplacee")        /* 220 */ \
	V(kFlagStatuettePrise, "FlagStatuettePrise")              \
	V(kFlagTeleporteK12, "FlagTeleporteK12")                  \
	V(kFlagTimerA05, "FlagTimerA05")                          \
	V(kFlagTimerCab, "FlagTimerCab")                          \
	V(kFlagTiroirDroitOuvert, "FlagTiroirDroitOuvert")        \
	V(kFlagTuniquePris, "FlagTuniquePris")                    \
	V(kFlagUseGrosPot, "FlagUseGrosPot")                      \
	V(kFlagUseK50Butin5, "FlagUseK50Butin5")                  \
	V(kFlagUseK50Butin6, "FlagUseK50Butin6")                  \
	V(kFlagUseSenet, "FlagUseSenet")                          /* 230 */ \
	V(kFlagVaseOrPris, "FlagVaseOrPris")                      \
	V(kIndiceVisuel01S03CART, "IndiceVisuel01S03CART")        \
	V(kIndiceVisuel02S06DJED, "IndiceVisuel02S06DJED")        \
	V(kIndiceVisuel03S06TIT, "IndiceVisuel03S06TIT")          \
	V(kIndiceVisuel04S08DJAT, "IndiceVisuel04S08DJAT")        \
	V(kIndiceVisuel05S40TRUIE, "IndiceVisuel05S40TRUIE")      \
	V(kIndiceVisuel06S44PTAH, "IndiceVisuel06S44PTAH")        \
	V(kIndiceVisuel07D63CHEV, "IndiceVisuel07D63CHEV")        \
	V(kIndiceVisuel08A21HERI, "IndiceVisuel08A21HERI")        \
	V(kIndiceVisuel09N03CHAT, "IndiceVisuel09N03CHAT")        /* 240 */ \
	V(kIndiceVisuel10N07SENET, "IndiceVisuel10N07SENET")      \

struct GameVariables {
	enum Var {
#define EGYPT_GV_EXPAND_ENUM(sym, name) sym,
		EGYPT_GAME_VARIABLES(EGYPT_GV_EXPAND_ENUM)
#undef EGYPT_GV_EXPAND_ENUM
		kMax
	};
};

} // End of namespace Egypt
} // End of namespace CryOmni3D

#endif
