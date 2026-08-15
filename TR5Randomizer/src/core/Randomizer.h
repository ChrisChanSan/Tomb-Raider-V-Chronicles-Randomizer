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
#include "Json.h"
#include "Rng.h"

#include <map>
#include <set>
#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Platzierungslogik.
//
// Grundregeln, alle aus frueheren Erkenntnissen dieses Projekts und nicht
// verhandelbar:
//
// 1. KEIN Entfernen von Items. TR-Item-Indizes sind reine Arrayposition; ein
//    entferntes Item schiebt alle folgenden hoch und die "Object N"-Verweise
//    der Trigger zeigen anschliessend auf das falsche Objekt. Randomisieren
//    heisst deshalb: vorhandene Items an ihrem Index UMSCHREIBEN (itemEdits).
//    itemAdds gibt es nur fuer Pflicht-Injektionen wie Andrea3s Brechstange,
//    und die haengen sich hinten an, verschieben also nichts.
//
// 2. Ein Pickup-Trigger klebt am Sektor, nicht am Item. Wird ein Item bewegt,
//    bleibt sein Trigger zurueck und feuert nie wieder. Betroffene Items
//    werden aus den Leveldaten ABGELEITET (siehe findTriggerPinnedItems), nicht
//    von Hand gepflegt. Ausgenommen sind Trigger, deren einzige weitere Wirkung
//    PlaySoundtrack ist - ein fehlender Musikeinsatz ist ein bewusst
//    akzeptiertes Opfer (Nutzerentscheidung), alles andere waere ein Eingriff
//    in Fortschritt, Geometrie oder Gegner.
//    Den Trigger spaeter mitzuverschieben ist geplant, verlangt aber
//    Floordata-Bearbeitung und ist ausdruecklich erst nach dem Grundgeruest dran.
//
// 3. OCB wird beim Verschieben auf 0 gesetzt. Ein Pickup mit Sockel-OCB
//    (3/4/7/8) oder Versteck-OCB (1) braucht eine passende Sonderumgebung am
//    Zielort; fehlt sie, laesst sich das Item ANSTANDSLOS NICHT AUFNEHMEN -
//    kein Absturz, keine Meldung, es liegt einfach fuer immer da. Im Projekt
//    empirisch bestaetigt (Andrea3, Item 101). Nur OCB 0 ueberlebt einen
//    Ortswechsel zuverlaessig.
//
// 4. Alles ist seedabhaengig und reihenfolgestabil: Item- und Sektorlisten
//    werden vor jeder Zufallsentscheidung sortiert, damit derselbe Seed
//    unabhaengig von Datei- oder Map-Reihenfolge dasselbe Ergebnis liefert.
//----------------------------------------------------------------------------

// Pickup-Typbereiche, direkt aus game/setup.cpp abgelesen: dort bekommen genau
// diese beiden zusammenhaengenden Bereiche PickUpCollision zugewiesen
// (PUZZLE_ITEM1..BURNING_TORCH_ITEM und PISTOLS_ITEM..FLARE_INV_ITEM).
// Damit ist "was ist ueberhaupt ein aufnehmbares Item" nicht geraten.
constexpr int PICKUP_LO_1 = 172, PICKUP_HI_1 = 241;
constexpr int PICKUP_LO_2 = 334, PICKUP_HI_2 = 355;

inline bool isPickupType(int typeId)
{
    return (typeId >= PICKUP_LO_1 && typeId <= PICKUP_HI_1)
        || (typeId >= PICKUP_LO_2 && typeId <= PICKUP_HI_2);
}

// Schwarmgetier: Fledermaus, Ratte, Spinne und der Spinnengenerator. Loest ein
// Pickup-Trigger nur so etwas aus, haelt er das Item NICHT fest.
//
// Begruendung des Nutzers: diese Viecher verfolgen Lara ein paar Sekunden und
// verschwinden dann von selbst. Sie zu verlieren aendert nichts am Spielablauf.
// Ein Hund oder ein Swat verschwindet nicht - deshalb bleibt Andrea1s Key Item
// ortsfest, und genau das war auch die Vorgabe.
//
// Ueber alle 16 Level trifft diese Ausnahme exakt einen Fall: Andrea2s Item
// 121 (Puzzle Item3), dessen Aufnahme eine Ratte spawnt. Alles andere, was
// Pickup-Trigger im Spiel ausloesen, sind Tueren, Fallen, Steuerobjekte oder
// echte Gegner.
inline bool isDespawningSwarm(int typeId)
{
    return typeId >= 93 && typeId <= 96;   // Bat, Rat, Spider, Spider Generator
}

// Traeger im Sinne von GetCarriedItems (game/setup.cpp:1925): ein Item, das
// beim Levelstart die Pickups in seiner unmittelbaren Naehe einsammelt. Sie
// verschwinden dabei aus dem Raum (room_number = NO_ROOM) und kommen erst
// wieder zum Vorschein, wenn der Gegner stirbt oder die Kiste durchsucht wird.
//
// Die Engine fragt objects[...].intelligent ab. Das ist eine Eigenschaft, die
// setup.cpp Slot fuer Slot setzt, kein zusammenhaengender Zahlenbereich - hier
// stehen deshalb genau die 24 Slots, die sie dort bekommen. Dazu kommt der
// Bereich SEARCH_OBJECT1..SEARCH_OBJECT3, den GetCarriedItems ausdruecklich
// zusaetzlich prueft: die durchsuchbaren Kisten und Spinde. In den sechzehn
// Leveln stellen sie die Mehrheit aller Traeger.
//
// Ausdruecklich NICHT dabei, weil man es vermuten wuerde: Scientist (69) und
// Guardian (65) bekommen kein intelligent und tragen nichts.
inline bool isCarrierType(int typeId)
{
    switch (typeId)
    {
    case 31: case 33: case 37: case 39: case 41: case 43: case 45: case 47:
    case 51: case 57: case 59: case 61: case 63: case 67: case 71: case 73:
    case 75: case 77: case 79: case 81: case 83: case 85: case 87: case 89:
        return true;                              // intelligent = 1
    default:
        return typeId >= 164 && typeId <= 168;    // SEARCH_OBJECT1..3 samt MIPs
    }
}

// Der Kasten aus GetCarriedItems, unveraendert uebernommen: gleicher Raum,
// weniger als 512 Einheiten in X und Z, weniger als 256 in Y. Bewusst ein
// Positionstest in Weltkoordinaten und kein Sektortest - die Engine rechnet so,
// und ein Sektortest waere an den Raendern falsch.
inline bool isWithinCarryBox(int bx, int by, int bz, int px, int py, int pz)
{
    return (bx - px < 512) && (px - bx < 512)
        && (by - py < 256) && (py - by < 256)
        && (bz - pz < 512) && (pz - bz < 512);
}

struct RoomOrigin { int room = 0; int originX = 0; int originZ = 0; };

struct ZoneSector
{
    int room = 0, sectorX = 0, sectorZ = 0;
    int zone = 0;
    int floorY = 0;

    // Sektoren, die Lara auf ihrem Erstzugangsweg kurz VOR diesem betritt,
    // etwa drei Schritte davor, beste Wahl zuerst. Aus dem Zonen-Export; dort
    // steht der Erstzugangsbaum, hier nur noch die fertige Antwort.
    //
    // Dafuer gibt es genau einen Zweck: der Trigger eines neu gesetzten Gegners.
    // Er darf nicht auf dem Gegnersektor liegen - sonst spawnt der Gegner in
    // Lara hinein -, muss aber sicher ueberquert werden, sonst wacht der Gegner
    // nie auf.
    std::vector<long long> approach;
};

// Sektorschluessel: Raum, Sektor-X und Sektor-Z in eine Zahl gepackt. Die
// Zonen-Exporte nennen Sektoren als "raum,x,z"; hier wird daraus ein
// vergleichbarer Wert, damit die Mengen unten mit set<long long> auskommen.
inline long long secKey(int room, int sx, int sz)
{
    return (static_cast<long long>(room) << 32) | (static_cast<long long>(sx) << 16) | static_cast<long long>(sz);
}

struct KeyConstraint
{
    int              typeId = 0;
    int              maxZone = 0;
    std::vector<int> allowRooms;     // leer = keine Einschraenkung
    std::vector<int> excludeRooms;

    // Hoehengrenze einer Inselgruppe. Y-KONVENTION: in TR liegt KLEINERES y
    // HOEHER. "Hoechstens 1024 ueber der Baseline" ist deshalb
    // floorY >= baseline - 1024, und ein Sektor UNTER der Schranke ist zu hoch -
    // typischerweise die Oberseite eines Kistenstapels, auf den Lara erst mit
    // genau dem Schluessel kommt, der dort liegen soll.
    bool             hasMinFloorY = false;
    int              minFloorY = 0;
    // Wo diese Schranke gilt. Leer heisst ueberall. Sie ist Eigenschaft der
    // GRUPPE, nicht der Zone: joby2s Zone 0 hat 7756 Sektoren, die Insel 1900 -
    // an die Zone gehaengt wirkte sie im halben Level.
    std::set<long long> minFloorYScope;

    std::set<long long> islandSectors;     // Positivliste; leer = keine
    std::set<long long> bannedSectors;     // Umkehrung: hier gerade nicht
    std::set<long long> forbiddenSectors;  // hinter der eigenen Tuer

    // Von diesem Typ muss MINDESTENS EINE Instanz in dieser Menge liegen.
    // Anders als alle Felder darueber ist das kein Filter fuer eine einzelne
    // Instanz, sondern eine Aussage ueber alle Instanzen zusammen - siehe
    // die Behandlung in run().
    std::set<long long> requireInIsland;
};

struct PlacementOffset
{
    int dx = 0;
    int dy = 0;
    int dz = 0;
};

struct MarkerCount
{
    int typeId = 0;
    int sectors = 0;
    int rooms = 0;
};

struct Injection
{
    int         typeId = 0;
    int         room = 0;
    // Optionaler fester Sektor. Ohne ihn sucht sich die Injektion einen
    // beliebigen freien gueltigen Sektor des Raums - richtig fuer Andrea3s
    // Brechstange, die irgendwo in Raum 120 liegen darf. rich2 braucht das
    // Gegenteil: dort sollen drei Items auf genau einem Feld liegen, weil der
    // Nutzer die Stelle ausgesucht hat. Ein fester Sektor belegt ihn deshalb
    // auch NICHT fuer die naechste Injektion - Stapeln ist hier gewollt.
    bool        hasSector = false;
    int         sectorX = 0;
    int         sectorZ = 0;
    // Hoehenversatz gegen den Sektorboden. y waechst in TR nach UNTEN, ein
    // negativer Wert hebt das Item also an. Gebraucht, sobald mehrere
    // Injektionen auf demselben Feld stehen und nicht ineinander stecken sollen.
    int         yOffset = 0;
    std::string reason;
};

struct LevelItem
{
    int index = 0, typeId = 0, room = 0;
    int x = 0, y = 0, z = 0;
    int sectorX = 0, sectorZ = 0;
    int ocb = 0;
    // Kein Winkel: der Level-Info-Export liefert keinen, und der Patch laesst
    // "angle" weg - ein verschobenes Item behaelt damit seine Ausrichtung.
};

//----------------------------------------------------------------------------
// Ein Item, dessen Pickup-Trigger mitwandert.
//
// Normalerweise haelt ein Pickup-Trigger sein Item fest (Regel 2): der Trigger
// klebt am Sektor, das Item wuerde ohne ihn wegwandern und seine Wirkung - eine
// Tuer, ein Gegner - bliebe am alten Fleck zurueck. Der gekoppelte Zug loest
// das, indem beides zusammen umzieht: ein Vorgang, nicht zwei.
//
// Erlaubt ist das nicht allgemein, sondern nur, wo es ausdruecklich kuriert
// wurde. Heute genau ein Fall: Andrea1 Item 107, der Schluessel, dessen Trigger
// zusaetzlich den Hund und einen Flame Emitter weckt.
//----------------------------------------------------------------------------
struct CoupledTrigger
{
    int itemIndex = 0;
    int room = 0, sectorX = 0, sectorZ = 0;   // wo der Trigger heute liegt
};

//----------------------------------------------------------------------------
// Ein Sound, den ein Gegner braucht, den SEIN Level aber nicht mitfuehrt.
//
// Ein Level traegt nur die Samples, die es selbst benoetigt. Kommt ein Gegner
// darin vanilla nicht vor, fehlen sie - er steht dann stumm da. Woher Ersatz
// kommt, ist Kuration (ENEMY_SOUND_FIX in ZoneAnalyse.ps1) und steht fertig im
// Zonen-Export; hier wird nur noch weitergereicht.
//----------------------------------------------------------------------------
struct EnemySound
{
    int typeId = 0;
    int map = 0;                 // die fehlende SFX-Nummer im Ziellevel
    std::string fromLevel;
    int fromMap = 0;
};

struct RandoInput
{
    std::string              level;
    int                      maxZone = 0;
    std::vector<ZoneSector>  sectors;        // bereits nur validForItems + erreicht
    std::vector<KeyConstraint> constraints;
    std::vector<int>         fixedItems;     // aus dem Zonen-Export (Handkuration)
    std::vector<Injection>   injections;
    // typeId -> Versatz gegen die Sektormitte auf Bodenhoehe. Noetig, wenn der
    // Mesh-Ursprung eines Objekts nicht dort sitzt, wo das Modell ihn annimmt.
    // Die Aufloesung global/levelbezogen passiert schon im Export, hier kommt
    // nur der fertige Wert an. Siehe TYPE_OFFSET in ZoneAnalyse.ps1.
    std::map<int, PlacementOffset> typeOffset;
    // Typen, deren OCB einen Ortswechsel ueberlebt - die Ausnahme von Regel 3.
    // Nur fuer Objekte, bei denen die OCB die Aufnahmeart beschreibt und nicht
    // die alte Stelle. Siehe KEEP_OCB in ZoneAnalyse.ps1.
    std::set<int>            keepOcbTypes;
    std::vector<LevelItem>   items;          // alle Items des Levels
    std::vector<RoomOrigin>  roomOrigins;
    std::set<int>            triggerPinned;  // abgeleitet, siehe Regel 2

    // Raeume mit Wasser-Flag (waterRoom aus dem Level-Export). Nutzervorgabe
    // 2026-08-14: dort duerfen keine Gegner gesetzt werden - Landgegner
    // koennen unter Wasser weder laufen noch angreifen und staenden nur
    // hilflos herum. Betrifft die Zusatzgegner-Plaetze; getauschte Gegner
    // stehen an Cores eigenen Positionen und bleiben unberuehrt.
    std::set<int>            waterRooms;

    // Items, auf die ein AntiTrigger oder HeavyAntiTrigger zeigt. Zeigt so
    // ein Trigger auf einen AKTIVEN intelligenten Gegner, loescht er ihn
    // ohne Sterben und ohne Ausblenden aus dem Level (hit_points = -16384,
    // DisableBaddieAI, KillItem - control.cpp:2422). In Vanilla ist das
    // Absicht, weil der Gegner dort erledigt sein soll; ein getauschter
    // kaempft womoeglich noch und verschwand mitten im Kampf (im Spiel
    // gesehen: Andrea1 Item 34 unter Allsanity, Antitrigger 46 in Raum 88).
    //
    // Solche Items tauschen jetzt mit und bekommen dabei Cores eigene
    // Antitrigger-Immunitaet (IFL_ANTITRIGGER_ONESHOT) ins Flag-Wort - der
    // Antitrigger prallt dann ab, der Gegner stirbt normal. Das wirkt pro
    // ITEM: Andy2s Antitrigger auf die Items 150-154 bleibt fuer die
    // ungetauschten Geschwister in Kraft. Betroffen sind ueberhaupt nur
    // fuenf Gegner im ganzen Spiel (Andrea1 34, Andy2 151, rich1 180,
    // rich2 26 und 111 - gemessen), davon faellt Andrea1 34 schon durch die
    // OCB-Regel heraus.
    std::set<int>            antitriggered;

    // Modellslots, die das Level fuehrt - von der Levelpruefung in ApplyPatch
    // (markTriggerSafeSectors) aus der Datei gelesen. Die Item-Abundance
    // streut nur Typen, deren Modell da ist; ein Pickup ohne Modell waere ein
    // unsichtbarer Stolperstein. Fuer das Waffenset kommt hinzu, was der
    // Import erst hineinbringt - das steht dann nicht in dieser Liste.
    std::set<int>            vorhandeneSlots;

    // Typen, die dieses Level NICHT tragen darf. Gefuellt von der
    // Levelpruefung in ApplyPatch (markTriggerSafeSectors): Andrea2 traegt
    // in Slot 47 nur Pierres CUTSCENE-Variante (Todesanim +12 hat Zustand 9
    // und springt in eine Lebend-Anim - fuer die Trajan-Finale-Szene, nie
    // fuer PierreControl gedacht; das Level hat auch keine Typ-47-Items).
    // Weil der Slot "da ist", wuerde weder Cross importieren noch der Pool
    // ihn meiden - ein dort platzierter Pierre friert ein und stirbt in
    // einer Endlosschleife (im Spiel gesehen). Ersetzen verbietet sich:
    // die Finale-Cutscene braucht genau diesen Satz.
    std::set<int>            verboteneTypen;

    // Raeume, in denen keine Waffe liegen darf, und was als Waffe zaehlt.
    // joby3: eine zufaellig in den Chef-Bereich gewanderte Schrotflinte loest
    // denselben Glitch aus wie versetzte Pistolen. Das ist keine Eigenschaft
    // eines Schluesseltyps, sondern des Levels, und trifft deshalb auch
    // gewoehnliche Pickups.
    std::set<int>            noWeaponRooms;
    std::set<int>            weaponTypes;

    // Items, deren Trigger mitwandert. Sie duerfen sich trotz triggerPinned
    // bewegen - eben weil ihr Trigger nicht zurueckbleibt.
    std::vector<CoupledTrigger> coupledTriggers;

    // Gegnertypen, die diesem Level hinzugefuegt werden duerfen. Handkuration
    // aus dem Zonen-Export - leer heisst: keine. Ein Gegner gehoert erst hinein,
    // wenn er im Spiel nachgewiesen hat, dass er ohne Levelskript auskommt.
    std::vector<int>         enemyTypes;

    // Samples, die einzelne dieser Typen im Level nicht vorfinden.
    std::vector<EnemySound>  enemySounds;

    // Sektoren, die einen Trigger aufnehmen koennen, als secKey. Wird
    // ausgewertet, sobald coupledTriggers oder enemyTypes nicht leer sind.
    //
    // Diese Menge kommt aus der Floordata des echten Levels, nicht aus dem
    // Level-Info-Export: ob ein Sektor einen Trigger vertraegt, haengt an
    // Eintragsarten (Lava, Climb, Monkey, TrigTrigger), von denen der Export
    // nur zwei kennt. Raten waere hier ein stiller Levelschaden - siehe
    // Tr5Level::canAppendTrigger.
    std::set<long long>      triggerSafeSectors;
};

struct RandoOptions
{
    bool shuffleKeyItems = true;
    bool shuffleOtherPickups = true;

    // Zusaetzliche Gegner ins Level setzen. Aus wie viele: 0 heisst keine.
    // Die Typen kommen aus RandoInput::enemyTypes, nicht von hier.
    bool addEnemies = false;
    int  enemiesPerLevel = 6;

    // Vorhandene Gegner untereinander tauschen. Nur die Art wechselt, der
    // Item-Index bleibt - damit bleiben alle "Object N"-Verweise der Trigger
    // gueltig und jeder Gegner wacht weiter dort auf, wo er es sollte.
    //
    // Getauscht wird ausschliesslich unter den kuratierten Typen des Levels
    // (RandoInput::enemyTypes); handkurierte Items bleiben ausgenommen.
    bool swapEnemies = false;

    // Cross-Gegnermix: der Wuerfelpool fuer Tausch und Zusatzgegner wird um
    // Typen aus ANDEREN Leveln erweitert (globale Pool-Tabelle in
    // Randomizer.cpp, aus der Nutzerliste "Gegner Pool.txt"). Je Level
    // kommen hoechstens drei fremde Typen dazu - jeder kostet Texturseiten
    // und Sample-Speicher im Ziel. Modelle und Sounds wandern beim Patchen
    // ueber den nativen Transfer mit (moveableAdds/soundAdds); was das Level
    // schon kennt, wird uebersprungen. Wirkt nur zusammen mit swapEnemies
    // oder addEnemies - allein aendert der Haken nichts.
    bool crossEnemies = false;

    // Allsanity: wie crossEnemies, aber OHNE die Drei-Typen-Grenze - der
    // ganze Pool steht dem Level zur Verfuegung, und beim Tausch wuerfelt
    // jede Rolle frei aus dem vollen Pool statt nur jede vierte. Maximale
    // Auswahl (Nutzerwunsch). Importiert wird weiterhin nur, was der Lauf
    // wirklich einsetzt - so bleibt der Texturseiten-Haushalt im Rahmen
    // (gemessen: 3 Fremdtypen +15..25 Seiten von 256; selbst der volle Pool
    // bleibt zusammen mit Waffen und Outfit deutlich unter der Grenze).
    bool allsanity = false;

    // Jedes Level bekommt zufaellig eines der drei Outfits: Lara_Andrea
    // (Spender Andrea1), Lara_Joby (joby2) oder Lara_Rich (rich2, Catsuit).
    // Uebertragen werden die Slots 8/9/10 (LARA_SKIN, LARA_SKIN_JOINTS,
    // LARA_SCREAM) plus das LaraType-Byte des Spenders - NIE Slot 0, der
    // traegt die Animationen des Levels. joby4 ist ausgenommen (Taucheranzug,
    // bleibt ohnehin vanilla); die andy-Level bekommen immer einen Ersatz,
    // ihr eigenes Outfit ist die Kind-Lara.
    bool randomOutfits = false;

    // Jedes Level bekommt das volle Waffenset (Pistolen, Uzi, Schrotflinte,
    // Desert Eagle, HK samt Munitions- und Inventarslots). Die Spenderregeln
    // stammen aus der Waffenarbeit und sind in allen 13 Leveln gebaut und
    // geprueft (Import Export\Waffen Tests\Plan.md): Andrea3 fuellt nur, was
    // fehlt; joby2 ersetzt den Revolver immer durch die Desert Eagle
    // (Nutzervorgabe); rich1 liefert die HK ausser nach rich2, das seine
    // eigene Spezialpose behaelt. joby4 bleibt vanilla (Nutzerentscheidung).
    bool allWeapons = false;

    // Item-Abundance: so viele zusaetzliche Verbrauchsgueter je Level
    // streuen (0 = aus). Gelegt wird nur, was das Level ohnehin kennt -
    // Munition, Medipacks, Fackeln -, gewichtet nach Haeufigkeit; Details am
    // Streublock in Randomizer.cpp. Karge Level sind der Anlass: Andy2 hat
    // auf 254 Raeume zehn Pickups bei 348 gueltigen Ablageflaechen.
    //
    // Eine Gegenrichtung ("weniger als Vanilla") gibt es nicht: Items
    // entfernen wuerde alle nachfolgenden Indizes verschieben und jeden
    // "Object N"-Verweis der Trigger brechen.
    int  extraPickups = 0;
};

struct RandoReport
{
    int movedKeyItems = 0;
    int movedOtherItems = 0;
    int pinnedItems = 0;
    int injected = 0;
    int enemiesAdded = 0;
    int enemiesSwapped = 0;
    int weaponsScattered = 0;           // gestreute Waffen-/Munitions-Items
    int extraPickups = 0;               // zusaetzliche Ausruestung (Abundance)
    int crossTypesUsed = 0;             // fremde Gegnertypen im Einsatz
    std::vector<std::string> notes;     // Auffaelligkeiten fuer die Oberflaeche
    bool ok = true;
    std::string error;
};

namespace Rando
{
    // Liest Zonen-Export und Level-Info zu einem Eingabesatz zusammen und
    // leitet dabei die triggergebundenen Items ab.
    bool buildInput(const JsonValue& zoneExport,
                    const JsonValue& levelInfo,
                    RandoInput& out,
                    std::string& error);

    // Regel 2: welche Items sitzen auf einem Pickup-Trigger, dessen Wirkung
    // ueber PlaySoundtrack hinausgeht? Oeffentlich, damit der Selbsttest und
    // spaeter die Oberflaeche dieselbe Liste zeigen koennen.
    std::set<int> findTriggerPinnedItems(const JsonValue& levelInfo,
                                         std::vector<std::string>* explain = nullptr);

    // Erzeugt den Patch. Der Seed bestimmt das Ergebnis vollstaendig.
    bool run(const RandoInput& in,
             const RandoOptions& opt,
             uint64_t seed,
             JsonValue& patchOut,
             RandoReport& report);

    // Darf dieser Typ auf diesem Sektor liegen? Die eine Stelle, an der alle
    // Zwangsbedingungen einer Instanz geprueft werden - benutzt von der
    // Platzierung UND vom Debugwerkzeug. Bewusst gemeinsam: eine zweite Kopie
    // dieses Filters wuerde frueher oder spaeter abweichen, und dann zeigte das
    // Debugbild etwas anderes, als der Randomizer tut.
    //
    // `mustBeOnIsland` betrifft nur requireInIsland - das ist eine Aussage ueber
    // alle Instanzen zusammen und gilt deshalb genau fuer die eine, die der Lauf
    // hineinzwingt.
    bool sectorAllowedFor(const RandoInput& in,
                          const KeyConstraint* k,
                          const ZoneSector& s,
                          int typeId,
                          bool mustBeOnIsland);

    // Debugwerkzeug: legt Marker auf Sektoren, statt Items zu verschieben.
    //
    // Zwei Ebenen uebereinander. `fillType` kommt auf JEDEN gueltigen Sektor -
    // das ist die Flaeche, auf der ueberhaupt etwas liegen darf. Darueber je
    // Constraint-Typ dessen eigener, engerer Bereich. Wo beide liegen, sind
    // beide Itemarten erlaubt; wo nur der Fuelltyp liegt, ausschliesslich
    // gewoehnliche Ausruestung.
    //
    // fillType 0 laesst die Fuellebene weg. Das Ergebnis ist ein Patch mit
    // itemAdds - nur fuer trview, denn mit ueber 256 Items laedt die Engine das
    // Level nicht mehr (MAX_ITEMS, harter Abbruch in specific/file.cpp).
    bool buildMarkerPatch(const RandoInput& in,
                          int fillType,
                          JsonValue& patchOut,
                          std::vector<MarkerCount>& perType,
                          int& fillSectors);
}
