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

struct RoomOrigin { int room = 0; int originX = 0; int originZ = 0; };

struct ZoneSector
{
    int room = 0, sectorX = 0, sectorZ = 0;
    int zone = 0;
    int floorY = 0;
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

struct Injection
{
    int         typeId = 0;
    int         room = 0;
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

struct RandoInput
{
    std::string              level;
    int                      maxZone = 0;
    std::vector<ZoneSector>  sectors;        // bereits nur validForItems + erreicht
    std::vector<KeyConstraint> constraints;
    std::vector<int>         fixedItems;     // aus dem Zonen-Export (Handkuration)
    std::vector<Injection>   injections;
    std::vector<LevelItem>   items;          // alle Items des Levels
    std::vector<RoomOrigin>  roomOrigins;
    std::set<int>            triggerPinned;  // abgeleitet, siehe Regel 2

    // Raeume, in denen keine Waffe liegen darf, und was als Waffe zaehlt.
    // joby3: eine zufaellig in den Chef-Bereich gewanderte Schrotflinte loest
    // denselben Glitch aus wie versetzte Pistolen. Das ist keine Eigenschaft
    // eines Schluesseltyps, sondern des Levels, und trifft deshalb auch
    // gewoehnliche Pickups.
    std::set<int>            noWeaponRooms;
    std::set<int>            weaponTypes;
};

struct RandoOptions
{
    bool shuffleKeyItems = true;
    bool shuffleOtherPickups = true;
};

struct RandoReport
{
    int movedKeyItems = 0;
    int movedOtherItems = 0;
    int pinnedItems = 0;
    int injected = 0;
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
}
