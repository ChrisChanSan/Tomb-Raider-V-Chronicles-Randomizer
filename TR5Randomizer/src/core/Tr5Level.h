/*
 * TR5 Randomizer
 * Copyright (C) 2026 ChrisChanSan
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Boden.h"

//----------------------------------------------------------------------------
// Eigener Leser und Schreiber fuer TR5-Leveldateien (.trc).
//
// Warum eigener Code und nicht der vorhandene aus TombItemEdit2026: dessen
// TR5-Parser (TRFile.cpp) stammt aus TRViewer von Popov Inc. und traegt keine
// Lizenzangabe. Unsere eigenen Ergaenzungen dort setzen darauf auf, lassen sich
// also nicht herausloesen. Fuer ein Werkzeug, das weitergegeben werden soll,
// ist das nicht tragbar - deshalb hier von Grund auf neu.
//
// Formatquelle: LoadLevel() und LoadTextures() in specific/file.cpp der Engine,
// die diesem Paket ohnehin im Quelltext beiliegt und unter derselben Lizenz
// steht (GPLv3). Die Engine ist die autoritative Beschreibung - sie liest diese
// Dateien seit 2000 korrekt.
//
// Dateiaufbau, aus LoadLevel/LoadTextures abgeleitet:
//
//   int32  version
//   int16  RTPages, OTPages, BTPages        Anzahl Texturseiten
//   -- Texturblock 1 (32 Bit) --
//   int32  size, compressedSize
//   byte[] Nutzdaten                        compressedSize bzw. size Bytes
//   -- Texturblock 2 (16 Bit) --
//   int32  size, compressedSize
//   byte[] Nutzdaten
//   int16  data[16]                         32 Bytes, u.a. LaraDrawType/Weather
//   -- Hauptblock --
//   int32  size, compressedSize
//   byte[] size Bytes                       UNKOMPRIMIERT, wortwoertlich
//
// Der Hauptblock ist der interessante Teil: darin liegen nacheinander Raeume,
// Objekte, Sprites, Kameras, Sounds, Boxen, animierte Texturen und
// Texturinfos, dann ein int32 mit der Item-Anzahl und danach die Items zu je
// 24 Bytes. Dass er unkomprimiert und wortwoertlich in der Datei steht, ist
// der Grund, warum sich Items ueberhaupt an Ort und Stelle aendern lassen,
// ohne die ganze Datei neu zu schreiben - compressedSize wird fuer diesen
// Block gelesen, aber nie benutzt.
//----------------------------------------------------------------------------

struct Tr5Item
{
    int16_t objectNumber = 0;
    int16_t roomNumber = 0;
    int32_t x = 0, y = 0, z = 0;
    int16_t yRot = 0;
    int16_t shade = 0;
    int16_t triggerFlags = 0;   // OCB in den unteren 6 Bit, siehe Randomizer.h
    int16_t flags = 0;
};

//----------------------------------------------------------------------------
// Ein AI-Objekt (AIOBJECT, global/types.h:2295, 24 Byte im Block direkt
// hinter den Items). Marker wie AI_AMBUSH (Slot 379): Larson/Pierre fliehen
// ab 40 LP zu einem AIAmbush mit passender OCB in ihrer Zone und despawnen
// dort (GetAITarget/FindAITargetObject, box.cpp). boxNumber darf 0 sein -
// die Engine rechnet sie zur Laufzeit selbst (box.cpp:1640).
//----------------------------------------------------------------------------
struct Tr5AiObject
{
    int16_t objectNumber = 0;
    int16_t roomNumber = 0;
    int32_t x = 0, y = 0, z = 0;
    int16_t ocb = 0;            // trigger_flags: muss der OCB des Gegners entsprechen
    int16_t flags = 0;
    int16_t yRot = 0;
    int16_t boxNumber = 0;
};

//----------------------------------------------------------------------------
// Ein Raum, soweit wir ihn brauchen. Der Raum-Blob wird nicht mehr am Stueck
// uebersprungen, sondern aufgeschluesselt - Portale, Nachbarn und Flags sind
// die Grundlage dafuer, einen wirklich abgeschiedenen Raum zu finden.
//----------------------------------------------------------------------------
struct Tr5Room
{
    int   index = 0;
    int   originX = 0, originZ = 0, yBottom = 0;
    int   numXSectors = 0, numZSectors = 0;
    int   altRoom = -1;                 // Flipmap-Partner, -1 = keiner
    unsigned flags = 0;                 // Bit 0x1 = unter Wasser
    std::vector<int> portalRooms;       // Zielraeume der Portalliste

    // Absolute Dateiposition des Sektorfelds (numXSectors * numZSectors
    // Eintraege zu je 8 Byte). 0, wenn der Raum nicht aufging. Das erste Wort
    // jedes Eintrags ist FLOOR_INFO.index, der Zeiger in die Floordata.
    size_t sectorsOffset = 0;

    // Absolute Dateiposition des Raumkopfes (208 Byte). Gebraucht, um die
    // Flags bei +78 zu schreiben - das Wasserbit sitzt dort.
    size_t headerOffset = 0;

    bool isAlternate() const { return altRoom >= 0; }
};

//----------------------------------------------------------------------------
// Ein Eintrag einer Floordata-Kette, mit allen seinen Woertern.
//
// Das Zerlegen in Eintraege ist nicht Bequemlichkeit, sondern Notwendigkeit:
// woerter[0] traegt in Bit 15 das "letzter Eintrag der Kette"-Kennzeichen. Wer
// eine Kette wortweise kopiert und einen Eintrag herausnimmt, laesst dieses Bit
// am falschen Wort stehen - die Engine liest dann ueber das Kettenende hinaus.
// Beim Zusammensetzen wird es darum neu vergeben.
//----------------------------------------------------------------------------
struct Tr5FloorEntry
{
    int funktion = 0;                   // global/types.h, enum floor_types
    int art = 0;                        // nur bei funktion == 4 (Trigger)
    std::vector<uint16_t> woerter;
};

//----------------------------------------------------------------------------
// Ein Triggerbefehl. type ist die 4-Bit-Befehlsart (enum trigobj_types,
// global/types.h:435): 0 = TO_OBJECT, 1 = TO_CAMERA, 3 = TO_FLIPMAP,
// 8 = TO_CD, 13 = TO_CUTSCENE und so weiter.
//----------------------------------------------------------------------------
struct Tr5TriggerCmd
{
    int type = 0;
    int param = 0;
};

class Tr5Level
{
public:
    // Liest die Datei vollstaendig in den Speicher und ermittelt die Lage des
    // Hauptblocks. Bei Fehlschlag steht der Grund in error.
    bool load(const std::wstring& path, std::string& error);

    // Schreibt die - moeglicherweise geaenderte - Datei. Bewusst als eigener
    // Zielpfad: die Quelldatei bleibt unangetastet, bis der Aufrufer sie
    // ersetzen will.
    bool save(const std::wstring& path, std::string& error) const;

    int32_t version() const { return m_version; }
    size_t  fileSize() const { return m_bytes.size(); }

    // Absolute Position und Groesse des Hauptblocks in der Datei.
    size_t  mainBlockOffset() const { return m_mainOffset; }
    size_t  mainBlockSize() const { return m_mainSize; }

    // --- Items ---------------------------------------------------------
    // Absolute Position des int32 mit der Item-Anzahl; direkt dahinter
    // liegen die Items zu je 24 Byte.
    size_t  itemCountOffset() const { return m_itemCountOffset; }
    int32_t itemCount() const { return m_itemCount; }
    int     roomCount() const { return m_roomCount; }

    const std::vector<Tr5Room>& rooms() const { return m_rooms; }

    // Zaehlt, in wie vielen Raeumen die Portalliste NICHT aufging. Muss 0
    // sein - siehe die Rechnung in parseRoom().
    int     roomParseMismatches() const { return m_roomMismatch; }

    // Liest ein Item. index muss < itemCount() sein.
    bool    getItem(int index, Tr5Item& out) const;

    // Schreibt ein Item an derselben Stelle zurueck. Aendert nur die 24 Byte
    // dieses Datensatzes - Dateigroesse und alle anderen Offsets bleiben
    // unberuehrt, weshalb Aendern gefahrlos ist und Entfernen nicht.
    bool    setItem(int index, const Tr5Item& in);

    // Haengt ein AI-Objekt hinten an den AI-Block (direkt hinter den Items).
    // Fuer den Fluchtmarker von Larson/Pierre: EIN AI_AMBUSH je Level an
    // Laras Startposition - die 40-LP-Flucht fuehrt Richtung Leveleingang
    // und despawnt ausser Sicht. NIE auf den Spawn-Sektor eines Gegners
    // legen: GetAIPickups (setup.cpp:2024) macht AI-Objekte im Umkreis von
    // 512 Units zur Start-Verhaltensanweisung des Gegners (Dauer-AMBUSH,
    // er zielt nie) und verbraucht sie danach (room_number = NO_ROOM).
    bool    addAiObject(const Tr5AiObject& in, std::string& error);

    // Haengt ein Item hinten an. Das ist der einzige Weg, Items hinzuzufuegen:
    // anhaengen verschiebt keine bestehenden Indizes, und die "Object N"-
    // Verweise der Trigger bleiben damit gueltig. Ein Entfernen gibt es hier
    // bewusst NICHT - es wuerde alle nachfolgenden Indizes verschieben.
    bool    addItem(const Tr5Item& in, std::string& error);

    // Die Engine haelt items[] als festes Feld dieser Groesse (MAX_ITEMS in
    // global/types.h). Mehr Items laedt sie nicht - rich2 liegt mit 248 schon
    // dicht darunter, deshalb wird die Grenze hier geprueft und nicht gehofft.
    // Seit der Engine-Verdopplung 512 (vorher 256; global/types.h der
    // Engine ist die massgebliche Stelle, beide muessen uebereinstimmen).
    static constexpr int MAX_ITEMS = 512;

    // Hebt genau diese Grenze auf - ausschliesslich fuer die Debug-Level, die
    // jeden gueltigen Sektor mit einem Marker belegen. Die entstehen mit
    // mehreren tausend Items und sind deshalb NICHT spielbar; die Engine bricht
    // beim Laden mit einer Meldung ab. Sie sind zum Ansehen in trview gedacht
    // und landen darum auch in einem eigenen Ordner, nie in data\.
    //
    // Bewusst ein ausdruecklicher Schalter statt eines gelockerten Grenzwerts:
    // wer ihn setzt, muss wissen, dass das Ergebnis kein Level mehr ist.
    void    allowMoreThanMaxItems(bool on) { m_allowOverLimit = on; }

    // --- Floordata -----------------------------------------------------
    //
    // Geaendert wird nach genau einem Muster: die neue Kette hinten an den
    // Block anhaengen und den FLOOR_INFO.index des Sektors umhaengen. Nie
    // mittendrin einfuegen oder loeschen - das wuerde JEDEN nachfolgenden
    // Index verschieben, und zwar lautlos, weil ein falscher Index einfach
    // eine andere Kette liest. Die alte Kette bleibt als toter Ballast
    // liegen; auf sie zeigt danach kein Sektor mehr.
    int32_t floorDataCount() const { return m_floorCount; }

    // Liest den FLOOR_INFO.index eines Sektors. -1, wenn es den Sektor nicht
    // gibt. 0 bedeutet: dieser Sektor hat ueberhaupt keine Floordata.
    int     sectorIndex(int room, int sx, int sz) const;

    // Zerlegt die Kette ab index in ihre Eintraege. Bei index == 0 ist das
    // Ergebnis leer und der Rueckgabewert trotzdem true. false heisst: die
    // Kette laeuft aus dem Block heraus, Finger weg.
    bool    readChain(int index, std::vector<Tr5FloorEntry>& out) const;

    // Musik-Trigger: besucht jedes TO_CD-Aktionswort (Typ 8) aller
    // Sektorketten genau einmal (geteilte Ketten werden ueber den
    // Kettenanfang dedupliziert - sonst ginge ein Track ZWEIMAL durch die
    // Abbildung und landete woanders als gewuerfelt). gefunden sammelt die
    // Tracknummern; abbildung patcht sie IN PLACE - das Wort behaelt Typ,
    // Ende-Bit und Platz, nur die Tracknummer (Bits 0..9) wechselt, darum
    // braucht es hier KEIN Ketten-Neubauen. Rueckgabe: Zahl der gepatchten
    // bzw. gefundenen Woerter.
    long    musikTrigger(const std::map<int, int>* abbildung, std::set<int>* gefunden);

    // Der Neigungseintrag eines Sektors (TILT, SPLIT1/2, NOCOLF), roh fuer
    // bodenLageAn (Boden.h) - dort steckt die portierte GetHeight-Mathematik,
    // die aus Eintrag + Punkt die echte Bodenhoehe und die Belaufbarkeit
    // macht. baseY ist der Grundwert des Sektors (floor-Byte << 8), auf einer
    // Schraege die HOECHSTE Ecke.
    // Rueckgabe: -1 Kette unlesbar (Finger weg), 0 eben (kein Eintrag,
    // baseY trotzdem gefuellt), 1 Eintrag steht in si.
    int     floorNeigung(int room, int sx, int sz, SchraegeInfo& si, int& baseY) const;

    // Baut ein aus einem TR3-Level geholtes Moveable ein (Paket aus TR3Hol,
    // Kennung "T35H"). Implementierung in Tr5Tr3Import.cpp - bewusst neben
    // dem erprobten TR5->TR5-Weg und nicht in ihm.
    //
    // texinfoIndex: JEDES Face zeigt danach auf diesen im Ziel bereits
    // vorhandenen Texinfo. Damit wird keine Texturseite eingefuegt und keine
    // Kachelnummer verschoben; der Gegner ist falsch texturiert, aber die
    // Seitenverwaltung bleibt unberuehrt. Echte Texturen sind ein eigener
    // Schritt ueber Tr5Transfer.cpp.
    bool    addMoveableFromTr3(const std::vector<uint8_t>& paket, int zielSlot,
                               int texinfoIndex, std::string& error);

    // Setzt die Klaenge eines TR3-Pakets ("T35S" aus TR3Ton) auf die
    // Soundmap-Plaetze basisId, basisId+1, ... und liefert die Zuordnung
    // urspruengliche TR3-ID -> neuer Platz zurueck. Damit werden anschliessend
    // die SFX-Nummern in den Anim-Kommandos umgebogen.
    //
    // Warum hohe Plaetze statt der TR3-Nummern: die urspruenglichen IDs sind
    // in TR5 teils belegt (89 in JEDEM Level, 84/87 in einigen). Die Plaetze
    // 440..449 sind dagegen ueberall frei, und die Soundmap ist ein festes
    // Feld von 450 Eintraegen - ein hoher Platz kostet also kein Byte.
    //
    // lautProzent (optional, je Klang in Paketreihenfolge): dehnt die
    // Lautstaerke prozentual. Der Grundwert kommt aus einem vorhandenen
    // SAMPLE_INFO des Ziels, damit die Groessenordnung stimmt - TR3 fuehrt
    // Volume als ganzes Wort, TR5 als ein Byte, eine direkte Uebernahme waere
    // geraten. 100 oder fehlend heisst unveraendert.
    bool    addSoundsFromTr3(const std::vector<uint8_t>& paket, int basisId,
                             std::vector<std::pair<int,int>>& zuordnung,
                             std::string& error,
                             const std::vector<int>* lautProzent = nullptr);

    // Setzt die Texturen eines TR3-Modellpakets (Version 2) ein: Seiten
    // anhaengen, Texinfos anlegen. Liefert TR3-Texturnummer -> neuer
    // Texinfo-Index; damit biegt der Aufrufer die Texturworte der Faces um.
    //
    // Implementierung in Tr5Transfer.cpp - dort liegen die erprobten Helfer
    // fuer die Seitenverwaltung. Die ist die gefaehrlichste Stelle des
    // Werkzeugs: eingefuegt wird an derselben Stelle wie beim TR5-Transfer
    // (rt+ot), danach werden Texinfos und Sprites nachgezogen. Niemals
    // eigenmaechtig hinten anhaengen - das verschiebt die Grenze zwischen
    // Objekt- und Bump-Seiten.
    //
    // Bei Erfolg steht in error eine Kurzmeldung (neue Seiten, neue Infos).
    bool    addTexturesFromTr3(const std::vector<uint8_t>& paket,
                               std::map<int,int>& zuordnung, std::string& error);

    // Nimmt den Triggereintrag des Quellsektors heraus und haengt ihn an die
    // Kette des Zielsektors. Beide Ketten werden neu hinten angebaut, beide
    // Sektoren umgehaengt. Der Trigger behaelt sein Referenzwort und seine
    // Befehle wortgleich - er spricht sein Item ueber den Index an, nicht
    // ueber den Ort, und bleibt darum gueltig.
    //
    // Bricht ab, wenn die Quelle nicht genau einen Trigger hat oder das Ziel
    // nach canAppendTrigger() ungeeignet ist.
    bool    moveTrigger(int fromRoom, int fromSx, int fromSz,
                        int toRoom, int toSx, int toSz, std::string& error);

    // Setzt einen NEUEN Trigger auf einen Sektor. Das ist der Weg, dem Level
    // Gegner hinzuzufuegen: kein vorhandener Eintrag wird gelesen oder
    // umgebaut, es kommt nur eine Kette hinten dazu.
    //
    // art ist die Triggerart (0 = TRIGGER, 1 = PAD, 5 = HEAVY, ...), mask die
    // fuenf Codebits (31 = alle, der uebliche Wert). oneShot setzt Bit 8 des
    // Setup-Worts - und das ist mehr als "nur einmal": _TestTriggers
    // (control.cpp:2415) setzt daraufhin IFL_INVISIBLE am geweckten Item, was
    // bei manchen Gegnern deren Verhalten aendert. Larson etwa flieht ohne
    // dieses Bit bei 40 Trefferpunkten und ist gar nicht toetbar
    // (pierre.cpp:68).
    bool    addTrigger(int room, int sx, int sz, int art, int timer, bool oneShot,
                       int mask, const std::vector<Tr5TriggerCmd>& cmds,
                       std::string& error, int refItem = -1);

    // Darf an diese Kette hinten ein Trigger? Gibt den Hinderungsgrund zurueck
    // oder nullptr. Siehe die Herleitung in der Implementierung.
    static const char* canAppendTrigger(const std::vector<Tr5FloorEntry>& chain);


    // Macht einen Wandsektor zum Portal nach targetRoom (EINE Seite - der
    // Aufrufer macht die Gegenseite mit vertauschten Rollen).
    //
    // Vorbedingung, gefunden mit SektorPruef an andy3: das 32-Byte-SICHTFENSTER
    // in der Portalliste des Raums muss schon existieren. Diese Funktion
    // schreibt nur die Sektorseite: Boden/Decke vom ueberlappten Sektor des
    // Zielraums (die Portalspalten zweier Raeume liegen uebereinander),
    // Boxwort wie bei allen Portalsektoren (fx=6, box=2047), und an die
    // Floordata-Kette kommt ein DOOR-Eintrag - vorhandene Neigungen (TILT)
    // bleiben stehen, Vanilla-Muster [TILT][SPLIT][DOOR].
    //
    // Wiederholbar: zeigt die Kette schon auf targetRoom, passiert nichts.
    bool    makePortal(int room, int sx, int sz, int targetRoom, std::string& error);

    // Macht einen Sektor kletterbar: CLIMB-Eintrag mit Richtungsbits
    // (0x100=0 Grad, 0x200=90, 0x400=180, 0x800=270 - Laras Blick auf die
    // Wand). ODER-t in vorhandene Eintraege; Kettenordnung siehe Rumpf.
    bool    makeClimb(int room, int sx, int sz, int dirBits, std::string& error);

    // Wasserbit eines Raums (ROOM_UNDERWATER, Bit 0 der Raumflags bei +78).
    // Echter Eingriff: 44 Motorstellen haengen daran, nicht nur die Optik.
    bool    setRoomWater(int room, bool anAus, std::string& error);
    // --- Sound ---------------------------------------------------------
    //
    // Ein Level fuehrt nur die Samples mit, die es selbst braucht. Kommt ein
    // Gegner darin vanilla nicht vor, fehlen seine Geraeusche - er steht dann
    // stumm im Level. Pierre ist der Fall, an dem das auffiel.
    int  soundInfoIndex(int map) const;    // -1 = dieses Level kennt den Sound nicht
    int  soundSampleCount() const { return m_nSamples; }

    // Uebernimmt das Sample hinter srcMap aus src und legt es hier unter
    // dstMap ab. Zufallsvarianten ("nimm eines von N") kommen mit.
    // Tut nichts und meldet Erfolg, wenn dstMap hier schon aufloest.
    bool copySoundFrom(const Tr5Level& src, int srcMap, int dstMap, std::string& error);

    // --- Modelltransfer ------------------------------------------------
    //
    // Uebernimmt einen kompletten Objektslot aus einem anderen Level: Meshes,
    // Meshzeiger, Bones, Animationen samt Zustandswechseln, Bereichen,
    // Befehlen und Frames, die referenzierten Texturinfos, die zugehoerigen
    // Texturseiten (32- und 16-Bit synchron) und den OBJECT_INFO-Eintrag.
    // Existiert der Slot im Ziel, wird sein Eintrag ersetzt; die alten Daten
    // bleiben als toter Ballast liegen - dieselbe Philosophie wie ueberall:
    // nichts wird entfernt, nichts verschiebt fremde Indizes.
    //
    // Slot 0 (LARA) wird verweigert: er traegt die Animationen des Levels,
    // ein fremder Slot 0 nimmt dem Level seine eigenen Bewegungen (im Spiel
    // erlebt: fehlende Schalter- und Kriechanimationen).
    //
    // Sounds kommen NICHT automatisch mit - dafuer gibt es copySoundFrom und
    // die Wissenstabelle in Tr5Transfer.cpp (hartverdrahtete Waffen-SFX).
    //
    // dstSlot (< 0 = wie slotId): der Slot, unter dem die Kopie im ZIEL
    // eingetragen wird. Gebraucht fuer die Ausweichslots des Cross-Gegnermix:
    // die MESHSWAP1 des Spenders (405) landet dort unter 399-402, damit der
    // eigene MESHSWAP1 des Ziels unangetastet bleibt.
    //
    // nurMeshes: NUR Meshes, Zeiger und Texturen kopieren - keine
    // Animationen, Frames oder Bones. Fuer Mesh-Lager wie MESHSWAP1: dessen
    // Objekteintrag traegt zwar einen anim_index, aber keine eigenen
    // Animationen (Core-Altlast; der generische Zuschnitt liefe damit aus
    // seinen Bloecken). Der Eintrag im Ziel bekommt anim 0xFFFF, bone 0,
    // frame 0 - gelesen wird an einem Lagerslot ohnehin nur mesh_index.
    //
    // animZahl (<= 0 = Heuristik "bis zum naechsten Objekt"): explizite
    // Laenge der Anim-Scheibe. Noetig fuer den Imp (89): seine Steuerung
    // greift mit anim_index+45 QUER durch die globale Anim-Tabelle - die
    // Todesanimation liegt physisch in den Bereichen fremder Kulissenslots,
    // die Heuristik schneidet nach 18 Anims ab und die Engine laese im Ziel
    // hinter dem Tabellenende (im Spiel gesehen: Todes-Flugschleife). Die
    // Frame-Scheibe waechst dann mit bis hinter den hoechsten Frame-Zeiger
    // der erweiterten Scheibe; mitkopierte Kulissen-Anims sind harmloser
    // Ballast nach dem Prinzip "nichts entfernen, nichts verschieben".
    bool copyMoveableFrom(const Tr5Level& src, int slotId, std::string& error,
                          int dstSlot = -1, bool nurMeshes = false,
                          int animZahl = -1);

    // Laesst eine Anim die Daten- und Bewegungsfelder einer anderen Anim
    // DESSELBEN Levels uebernehmen: Frame-Zeiger, Rate, frame_size,
    // Geschwindigkeiten und die Kommandoreferenz. Zustand, Frame-Bereich,
    // Folgeanim und Changes bleiben stehen. Gedacht fuer tote Erbe-Anims wie
    // SWATs Zustand-3-Feueranim (+2): sie teilt sich ihren Frame-Zeiger mit
    // der Zielanim (+3), liest deren 34-Wort-Frames aber mit Schrittweite 36
    // - ab Keyframe 1 entsteht Muell (Modell verschwindet, Schatten zerrt).
    // Core hat das nie bemerkt, weil SWAT selbst ueber Zustand 35 feuert;
    // nur Guard/CRANE_GUY feuern ueber Zustand 3, und kein Vanilla-Level
    // laesst beide Saetze zusammen spielen. Verlangt identische
    // Frame-Bereiche in beiden Anims (Familiensaetze sind schablonengleich).
    bool uebernehmeAnimDaten(int dstSlot, int dstOffset, int srcSlot,
                             int srcOffset, std::string& fehler);

    // Eine einzelne Lara-Anim samt Frame-Daten, Kommandos und Sounds aus
    // einem SPENDERLEVEL einpflanzen (ANIM_KEYCARD 437, #53). Der Anim-Platz
    // existiert ueberall, die Bilddaten sind levelspezifisch - Begruendung
    // und Grenzen am Rumpf in Tr5Transfer.cpp.
    bool kopiereLaraAnimVon(const Tr5Level& src, int animNr, std::string& fehler);

    // Traegt Laras Anim <animNr> hier echte Bilddaten? FrameSize null ist
    // Cores Stub-Kennzeichen; zusaetzlich wird der Platz nachgerechnet.
    bool    laraAnimBrauchbar(int animNr) const;

    // Auf welche Anim zeigt Laras <animNr> weiter (next_animation)? Fuer die
    // Kettenreparatur - Bewegungen bestehen aus mehreren Gliedern, und Core
    // stubbt sie am Stueck. -1 = gibt es nicht.
    int     laraAnimNachfolger(int animNr) const;

    // Der Objektslot, wie er in der Datei steht. anim < 0 heisst: keiner.
    struct MoveableInfo
    {
        int slot = -1;
        int nmeshes = 0, meshIndex = 0, boneIndex = 0, animIndex = -1;
        uint32_t frameOffset = 0;
    };
    bool moveableInfo(int slotId, MoveableInfo& out) const;
    std::vector<int> moveableSlots() const;

    // Nur fuer Diagnosewerkzeuge: rohe Werte aus den Bloecken. Ausserhalb des
    // Blocks kommt 0 zurueck - die Werkzeuge vergleichen Spender gegen Kopie,
    // da faellt ein Ausreisser als Unterschied auf.
    int32_t debugReadBoneLong(long long i) const;
    int16_t debugReadFrameShort(long long i) const;
    // Diagnose der physischen Samplekette: wo das num_samples-Feld hinter dem
    // Hauptblock liegt und wie viele Samples es zaehlt, plus die Rohbytes.
    bool debugSampleArray(size_t& numSamplesAt, int32_t& n) const
    { if (!m_soundOk) return false; numSamplesAt = m_numSamplesAt; n = m_nSamples; return true; }
    const std::vector<uint8_t>& debugBytes() const { return m_bytes; }
    int32_t debugTexinfoCount() const { return m_texinfos.count; }
    bool debugTexinfoBlock(size_t& dataAt, int32_t& n) const { dataAt = m_texinfos.dataAt; n = m_texinfos.count; return m_texinfos.count > 0; }
    // Anim- und Kommandoblock fuer Diagnosewerkzeuge (AnimDump): Lage und
    // Anzahl, Datensatzformat siehe SZ_ANIM in Tr5Transfer.cpp (40 Byte).
    bool debugAnimBlock(size_t& dataAt, int32_t& n) const { dataAt = m_anims.dataAt; n = m_anims.count; return m_anims.count > 0; }
    // Klangkette fuer Messwerkzeuge: Soundmap, SAMPLE_INFO-Feld, Samplezahl.
    bool debugKlangBloecke(size_t& lutAt, size_t& infosAt, int32_t& nInfos, int32_t& nSamples) const
    { if (!m_soundOk) return false;
      lutAt = m_lutAt; infosAt = m_infosAt; nInfos = m_nInfos; nSamples = m_nSamples; return true; }

    // Modellbloecke fuer Messwerkzeuge (Objekttabelle und Frame-Scheibe).
    bool debugModellBloecke(size_t& objAt, int32_t& objN, size_t& frAt, int32_t& frN) const
    { objAt = m_objects.dataAt; objN = m_objects.count;
      frAt = m_frames.dataAt;  frN  = m_frames.count;  return objN > 0; }
    bool debugCommandBlock(size_t& dataAt, int32_t& n) const { dataAt = m_commands.dataAt; n = m_commands.count; return m_commands.count > 0; }
    bool debugChangeBlock(size_t& dataAt, int32_t& n) const { dataAt = m_changes.dataAt; n = m_changes.count; return m_changes.count > 0; }
    bool debugRangeBlock(size_t& dataAt, int32_t& n) const { dataAt = m_ranges.dataAt; n = m_ranges.count; return m_ranges.count > 0; }
    // Byteposition des SAMPLE_INFO einer Soundmap-ID (8 Byte: number s16,
    // volume u8, radius s8, randomness s8, pitch s8, flags s16).
    bool debugSoundInfoAt(int map, size_t& at) const
    { const int i = soundInfoIndex(map); if (i < 0) return false; at = m_infosAt + static_cast<size_t>(i) * 8; return true; }
    // Lage und Umfang der beiden Meshbloecke - fuer Rohvergleiche zweier
    // Fassungen desselben Levels (LevelDiff).
    int32_t debugMeshPtrCount() const { return m_meshPtrs.count; }
    bool debugMeshDataBlock(size_t& dataAt, int32_t& n) const
    { dataAt = m_meshData.dataAt; n = m_meshData.count; return m_meshData.count > 0; }
    // Byteposition eines Meshes ueber seinen Zeigerindex - fuer
    // Rohvergleiche (Walk-Kennzahlen unterscheiden identische Geometrie
    // mit verschiedener Texturierung nicht).
    bool debugMeshAt(int ptrIndex, size_t& at) const
    {
        if (ptrIndex < 0 || ptrIndex >= m_meshPtrs.count) return false;
        uint32_t ofs;
        std::memcpy(&ofs, m_bytes.data() + m_meshPtrs.dataAt + static_cast<size_t>(ptrIndex) * 4, 4);
        at = m_meshData.dataAt + ofs;
        return true;
    }
    // Kopf eines Meshes (x, y, z, r, flags, nVerts-Wort) ueber seinen
    // Zeigerindex. false, wenn es den Zeiger nicht gibt.
    bool    debugMeshHeader(int ptrIndex, int16_t out[6]) const;
    // Laeuft ein Mesh mit der korrekten Normalen-Regel ab (0 = Normalen,
    // nur negativ = Prelight) und liefert seine Kennzahlen. In
    // Tr5Transfer.cpp definiert, wo der Ableser wohnt.
    bool    debugMeshWalk(int ptrIndex, int& nVerts, int& nNorms,
                          int& ngt4, int& ngt3, int& bytes) const;

    // Das Outfit-Byte (LaraType) im Levelkopf, hinter den Texturbloecken.
    int  laraType() const;
    bool setLaraType(int value);
    int  fremdeSprechkoepfe() const;
    bool setFremdeSprechkoepfe(int value);

    // --- Texturwerkstatt (#104) ----------------------------------------
    //
    // Die beiden Texturbloecke des Levels werden beim ersten Seiten-Import
    // EINMAL entpackt und offen gehalten; jeder weitere Import haengt nur
    // noch an. Gepackt und in die Datei zurueckgesetzt wird EINMAL beim
    // Schliessen - save() ruft es selbst, der Aufrufer muss nichts tun.
    //
    // Vorher liefen Entpacken, Packen, Datei-Neuzusammensetzen und reparse
    // je importiertem Typ: bei Allsanity zehnmal je Level ueber 30+ MB, und
    // genau da steckten die gemessenen 5-10 Sekunden des Gegnermixes (ein
    // Level LADEN kostet nur 15 ms). Wichtig fuer alle Zwischenzustaende:
    // solange die Werkstatt offen ist, tragen die Texturbloecke in m_bytes
    // den ALTEN Stand - wer sie liest, muss vorher schliessen. Der
    // Hauptblock dahinter verschiebt sich bis zum Schliessen nicht, alle
    // Blockversaetze bleiben gueltig.
    bool texWerkstattSchliessen(std::string& error);

private:
    bool                 m_texOffen = false;
    bool                 m_texSchmutzig = false;
    std::vector<uint8_t> m_tex32, m_tex16;

    bool texWerkstattOeffnen(std::string& error);
    bool                 m_allowOverLimit = false;
    std::vector<uint8_t> m_bytes;
    int32_t              m_version = 0;
    int16_t              m_rtPages = 0, m_otPages = 0, m_btPages = 0;
    size_t               m_mainOffset = 0;   // Beginn der Hauptblock-Nutzdaten
    size_t               m_mainSize = 0;
    size_t               m_itemCountOffset = 0;
    int32_t              m_itemCount = 0;
    size_t               m_floorCountOffset = 0;  // das int32 mit numFloorData
    size_t               m_floorAt = 0;           // erstes Short der Floordata
    int32_t              m_floorCount = 0;

    // --- Sound-Block ---------------------------------------------------
    // Alles ausser den Sampledaten selbst liegt IM Hauptblock, hinter den
    // Items. Wer davor etwas einfuegt - Items oder Floordata -, muss diese
    // Offsets mitziehen; genau dafuer gibt es verschiebeSound().
    size_t               m_lutAt = 0;             // short sample_lut[450]
    size_t               m_numInfosAt = 0;
    size_t               m_infosAt = 0;
    int32_t              m_nInfos = 0;
    size_t               m_numSamplesInMainAt = 0;  // num_samples, Fassung im Hauptblock
    size_t               m_numSamplesAt = 0;        // num_samples, Fassung dahinter
    int32_t              m_nSamples = 0;
    bool                 m_soundOk = false;

    bool findSound();                     // nach findItems, darf scheitern
    void verschiebeSound(size_t bytes, size_t abOffset);

    // --- Blockkarte ----------------------------------------------------
    // Lage der Bloecke, die der Modelltransfer anfassen muss. countAt ist die
    // Position des int32 mit der Anzahl, dataAt der erste Datenbyte. Wird in
    // findItems mitgeschrieben und ist nach JEDEM Einfuegen ungueltig, bis
    // reparse() gelaufen ist - deshalb arbeitet der Transfer mit einer
    // Splice-Liste und fuegt alles auf einen Schlag ein.
    struct Block
    {
        size_t  countAt = 0;
        size_t  dataAt = 0;
        int32_t count = 0;
    };
    Block m_meshData, m_meshPtrs, m_anims, m_changes, m_ranges, m_commands,
          m_bones, m_frames, m_objects, m_sprites, m_texinfos;

    // Liest Kopf und Hauptblock neu ein, nachdem Bytes eingefuegt wurden.
    // copyMoveableFrom ist ein Member und in Tr5Transfer.cpp definiert - die
    // Trennung haelt Tr5Level.cpp lesbar, mehr nicht.
    bool reparse(std::string& error);
    int                  m_roomCount = 0;
    std::vector<Tr5Room> m_rooms;
    int                  m_roomMismatch = 0;

    // Setzt eine Kette hinten an den Block und liefert ihren Index. Leere
    // Kette -> 0 ("keine Floordata"). Zieht numFloorData und die doppelt
    // gefuehrte Hauptblockgroesse mit.
    bool appendChain(const std::vector<Tr5FloorEntry>& chain, uint16_t& outIndex,
                     std::string& error);
    void setSectorIndex(const Tr5Room& r, int sx, int sz, uint16_t index);

    // Schluesselt einen Raum-Blob auf. offset/size zeigen auf den Blob im
    // Hauptblock, index ist die laufende Raumnummer.
    void parseRoom(size_t offset, size_t size, int index);

    bool readHeader(std::string& error);
    bool findItems(std::string& error);   // laeuft den Hauptblock ab
};
