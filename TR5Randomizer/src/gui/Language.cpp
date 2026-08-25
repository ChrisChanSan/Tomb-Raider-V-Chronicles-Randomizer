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
#include "Language.h"

#include <string>
#include <unordered_map>

bool gGermanUi = false;   // default English (user decision 2026-08-24)

// English -> German. The KEY is exactly the string from the source (for
// multi-line help texts, the complete concatenated text). The table was
// derived from the T() call sites in Main.cpp - whoever changes a text there
// must update the key here, or German shows English again (visible, and
// deliberately no crash).
//
// Strings that are the same in both languages ("Vanilla", "Cross-Mix",
// "Debug", "Allsanity", "%s", "SWAT", "Hydra", ...) deliberately do NOT
// stand here - the table miss returns them unchanged.
static const std::unordered_map<std::string, const char*>& table()
{
    static const std::unordered_map<std::string, const char*> t = {
        // --- Status line -------------------------------------------------
        { "No game folder selected yet.", "Noch kein Spielordner gewaehlt." },
        { "Building debug levels...", "Debug-Level werden gebaut..." },
        { "Run started.", "Lauf gestartet." },
        { "Copy failed: ", "Kopieren fehlgeschlagen: " },
        { "There is no backup - nothing has ever been randomized, "
          "so the levels are already in their original state.",
          "Es gibt kein Backup - da wurde noch nie randomisiert, "
          "die Level sind also bereits im Originalzustand." },
        { "Restore failed: ", "Zuruecksetzen fehlgeschlagen: " },
        { "Installation ready. The originals are in data\\Backup.",
          "Installation bereit. Originale liegen in data\\Backup." },
        { "Installation checked - all levels unmodified. data\\Backup "
          "is created on the first randomize.",
          "Installation geprueft - alle Level unveraendert. Beim ersten "
          "Randomisieren wird data\\Backup angelegt." },
        { "The backup is damaged: ", "Das Backup ist beschaedigt: " },
        { "%d levels restored to their original state.",
          "%d Level auf den Originalzustand zurueckgesetzt." },
        { "%d debug levels written to \"Debug Level\"%s. For trview "
          "only - the game will not start with this many items.",
          "%d Debug-Level in \"Debug Level\" abgelegt%s. Nur fuer trview - "
          "mit so vielen Items startet das Spiel nicht." },
        { " - with errors, see the list", " - mit Fehlern, siehe Liste" },
        { "%d levels randomized%s. %s You can start the game now.",
          "%d Level randomisiert%s. %s Du kannst das Spiel jetzt starten." },
        { " - with errors, see spoiler.txt", " - mit Fehlern, siehe spoiler.txt" },
        { "Details are in spoiler.txt next to the program.",
          "Einzelheiten stehen in spoiler.txt neben dem Programm." },
        { "spoiler.txt could not be written.",
          "spoiler.txt liess sich nicht schreiben." },
        { "Balance.txt written - takes effect on the next game start.",
          "Balance.txt geschrieben - gilt ab dem naechsten Spielstart." },

        // --- Window: installation paths ----------------------------------
        { "Installation paths", "Installations-Pfade" },
        { "The dinosaurs come from other games. This tool ships no game data - "
          "point it at those folders and the run fetches models, textures and "
          "sounds from there. Without a path the affected enemy types stay "
          "grayed out; everything else works as usual.",
          "Die Dinosaurier stammen aus anderen Spielen. Dieses Werkzeug bringt "
          "keine Spieldaten mit - gib die Ordner an, dann holt der Lauf sich "
          "Modelle, Texturen und Klaenge von dort. Ohne Pfad bleiben die "
          "betroffenen Gegnerarten ausgegraut, sonst laeuft alles wie gewohnt." },
        { "for %s", "fuer %s" },
        { "the levels that get randomized", "die Level, die randomisiert werden" },
        { "T-Rex, Raptor and Compy", "T-Rex, Raptor und Compy" },
        { "the vulture", "der Geier" },
        { "(not selected)", "(nicht gewaehlt)" },
        { "Choose folder...", "Ordner waehlen..." },
        { "Clear", "Leeren" },
        { "OK", "in Ordnung" },
        { "%s not found", "%s nicht gefunden" },
        { "not checked yet", "noch nicht geprueft" },
        { "In each case, choose the data folder of the installation.",
          "Gesucht wird jeweils der data-Ordner der Installation." },

        // --- Window: enemy types -----------------------------------------
        { "Enemy types", "Gegnerarten" },
        { "Which types may the random draw pick? The checkbox affects swapping "
          "and additional enemies - enemies that are in the level anyway and "
          "are not swapped remain untouched.",
          "Welche Arten darf der Zufall ziehen? Der Haken wirkt auf den Tausch "
          "und auf zusaetzliche Gegner - Gegner, die im Level ohnehin stehen und "
          "nicht getauscht werden, bleiben davon unberuehrt." },
        { "Whatever a type needs technically, the randomizer loads on its own.",
          "Was eine Art technisch braucht, laedt der Randomizer von selbst." },
        { "All on", "Alle an" },
        { "All off", "Alle aus" },
        { "%d of %d on", "%d von %d an" },
        { "- needs %s", "- braucht %s" },
        { "All types deselected: every level keeps its own enemies, "
          "no foreign ones are added.",
          "Alle Arten abgewaehlt: jedes Level bleibt bei seinen "
          "eigenen Gegnern, fremde kommen keine dazu." },
        { "If you deselect a lot, some levels have hardly any foreign "
          "types left to draw from - they will stay closer to the original.",
          "Waehlst du sehr viele ab, stehen manchen Leveln kaum "
          "noch fremde Arten zur Verfuegung - sie bleiben dann "
          "naeher am Original." },

        // --- Enemy display names (Rando::enemyKinds) ---------------------
        // Only the names that differ; the rest ("SWAT", "Larson", "Hydra",
        // "Imp", ...) pass through unchanged.
        { "Guard",                    "Wachmann" },
        { "Guard (Joby)",             "Wachmann (Joby)" },
        { "SWAT (heavy)",             "SWAT (schwer)" },
        { "Mafia (pistols)",          "Mafia (Pistolen)" },
        { "Dog",                      "Hund" },
        { "Lion",                     "Loewe" },
        { "Chef",                     "Koch" },
        { "Soldier statue (sword)",   "Soldatenstatue (Schwert)" },
        { "Soldier statue (hammer)",  "Soldatenstatue (Hammer)" },
        { "Maze monster",             "Labyrinth-Monster" },
        { "Vulture (TR3 LA)",         "Geier (TR3 LA)" },

        // --- Window: debug markers ---------------------------------------
        { "Debug: sector markers", "Debug: Sektoren-Marker" },
        { "Builds a copy of every level checked above in which every allowed "
          "sector carries a marker. View them in trview - with several thousand "
          "items the game will NOT start these files. Your installation stays "
          "untouched; everything is written to \"Debug Level\" next to the program.",
          "Baut zu jedem oben angehakten Level eine Kopie, in der jeder erlaubte "
          "Sektor einen Marker traegt. Zum Ansehen in trview - mit mehreren tausend "
          "Items startet das Spiel diese Dateien NICHT. Deine Installation bleibt "
          "unberuehrt, geschrieben wird nur nach \"Debug Level\" neben dem Programm." },
        { "Fill layer: mark every valid sector",
          "Fuellebene: jeden gueltigen Sektor markieren" },
        { "Fill layer type", "Typ der Fuellebene" },
        { "223 = Pickup Item4. Rare in the game, stands out immediately in the item list.",
          "223 = Pickup Item4. Im Spiel selten, faellt in der Itemliste sofort auf." },
        { "Build debug levels", "Debug-Level bauen" },
        { "Open folder", "Ordner oeffnen" },
        { "(choose an installation above first)",
          "(erst oben eine Installation waehlen)" },
        { "Result per type:", "Ergebnis je Typ:" },

        // --- Header + debug menu -----------------------------------------
        { "Sector markers...", "Sektoren-Marker..." },
        { "Open \"Debug Level\" folder", "Ordner \"Debug Level\" oeffnen" },
        { "Warning: outdated curation data", "Achtung: veralteter Curationsstand" },
        { "To fix: re-export ZoneAnalysis.ps1 for ALL levels into data\\.",
          "Beheben: ZoneAnalysis.ps1 fuer ALLE Level neu nach data\\ exportieren." },

        // --- Block 1: installation ---------------------------------------
        { "1. Your installation", "1. Deine Installation" },
        { "Choose the folder of your Tomb Raider Chronicles installation. "
          "The levels are randomized right there; the program first backs up "
          "the originals to data\\Backup.",
          "Waehle den Ordner deiner Tomb Raider Chronicles-Installation. "
          "Die Level werden direkt dort randomisiert; die Originale sichert "
          "das Programm vorher nach data\\Backup." },
        { "Check again", "Erneut pruefen" },
        { "Installation paths...", "Installations-Pfade..." },
        { "Optional: folders of Tomb Raider 3 and The Lost Artifact.\n"
          "Only with these can the dinosaurs be used -\n"
          "we ship no game data.",
          "Optional: Ordner von Tomb Raider 3 und The Lost Artifact.\n"
          "Nur damit lassen sich die Dinosaurier verwenden -\n"
          "wir liefern keine Spieldaten mit." },
        { "TR3 + LA found", "TR3 + LA gefunden" },
        { "%d without a path", "%d ohne Pfad" },

        // --- Block 2: seed -----------------------------------------------
        { "The same seed always produces the same game. Any input is valid - "
          "even a word.",
          "Derselbe Seed erzeugt immer dasselbe Spiel. Jede Eingabe ist gueltig - "
          "auch ein Wort." },
        { "Roll", "Wuerfeln" },
        { "effective: 0x%016llX", "wirksam: 0x%016llX" },

        // --- Balance sliders ---------------------------------------------
        { "Random", "Zufall" },
        { "Weapon damage", "Waffenschaden" },
        { "Enemy health", "Gegnerleben" },
        { "Enemy damage", "Gegnerschaden" },
        { "Instead of setting it by hand: the seed rolls the value, in "
          "steps of five between 0.50x and 2.00x. The same seed always "
          "gives the same value, no matter whether the other two boxes "
          "are checked.\n\n"
          "The slider immediately shows what the current seed gives. The "
          "roll goes into Balance.txt when you check the box and on the "
          "next run.\n\n"
          "Weapon damage too is only rolled from 0.50x upward - a seed "
          "that puts Lara's weapons at a tenth would not be hard, just "
          "unplayable. 0.00x is reserved for manual tuning.",
          "Statt von Hand: der Seed wuerfelt den Wert aus, in "
          "Fuenferschritten zwischen 0.50x und 2.00x. Derselbe Seed "
          "gibt immer denselben Wert, und zwar unabhaengig davon, ob "
          "die anderen beiden Haken gesetzt sind.\n\n"
          "Der Regler zeigt sofort, was der aktuelle Seed gibt. In die "
          "Balance.txt wandert der Wurf beim Anhaken und beim naechsten "
          "Lauf.\n\n"
          "Auch der Waffenschaden wird nur ab 0.50x geworfen - ein Seed, "
          "der Laras Waffen auf ein Zehntel stellt, waere nicht schwer, "
          "sondern unspielbar. 0.00x bleibt der Handeinstellung." },
        { "Affects all of Lara's weapons at once - pistols, Uzi, shotgun, "
          "revolver, HK, grappling gun and torch. 0.00x makes them "
          "harmless, 2.00x doubles their damage.\n\n"
          "This writes the WeaponDamage line in Balance.txt next to "
          "PCtomb5.exe. If you tuned individual weapons there by hand, "
          "they are kept: the slider applies on top.",
          "Wirkt auf alle Waffen Laras zugleich - Pistolen, Uzi, "
          "Schrotflinte, Revolver, HK, Grappling Gun und Fackel. "
          "0.00x macht sie wirkungslos, 2.00x verdoppelt ihren Schaden.\n\n"
          "Geschrieben wird die Zeile WeaponDamage in Balance.txt neben "
          "der PCtomb5.exe. Wer dort einzelne Waffen von Hand eingestellt "
          "hat, behaelt sie: der Regler wirkt zusaetzlich obendrauf." },
        { "How tough all enemies are. 0.50x halves their hit points, "
          "2.00x doubles them.\n\n"
          "This writes the EnemyHealth line in Balance.txt next to "
          "PCtomb5.exe. If you tuned individual enemies there by hand, "
          "they are kept: the slider applies on top.",
          "Wie zaeh alle Gegner sind. 0.50x halbiert ihre Lebenspunkte, "
          "2.00x verdoppelt sie.\n\n"
          "Geschrieben wird die Zeile EnemyHealth in Balance.txt neben der "
          "PCtomb5.exe. Wer dort einzelne Gegner von Hand eingestellt hat, "
          "behaelt sie: der Regler wirkt zusaetzlich obendrauf." },
        { "How hard all enemies hit. Traps, falls, fire, poison and "
          "drowning always stay as hard as in the original, as do the "
          "built-in level hazards like the ceiling gun.\n\n"
          "This writes the EnemyDamage line in Balance.txt next to "
          "PCtomb5.exe.",
          "Wie hart alle Gegner zuschlagen. Fallen, Stuerze, Feuer, Gift "
          "und Ertrinken bleiben immer so hart wie im Original, ebenso die "
          "fest eingebauten Levelgefahren wie das Deckengeschuetz.\n\n"
          "Geschrieben wird die Zeile EnemyDamage in Balance.txt neben "
          "der PCtomb5.exe." },

        // --- Block 3: items ----------------------------------------------
        { "Equipment (weapons, ammo, medipacks)",
          "Ausruestung (Waffen, Munition, Medipacks)" },
        { "Vanilla amount", "Vanilla Anzahl" },
        { "More", "Mehr" },
        { "A lot more", "Viel mehr" },
        { "Keys & puzzles", "Schluessel & Puzzles" },
        { "All weapons everywhere is standard and always included.",
          "Alle Waffen ueberall sind Standard und immer dabei." },
        { "No shuffle option selected - the levels only get the standard weapon set.",
          "Keine Wuerfel-Option gewaehlt - die Level bekommen nur das Standard-Waffenset." },
        { "The level keeps exactly as much equipment as Core intended - "
          "just in different places.",
          "Das Level behaelt genau so viel Ausruestung, wie Core vorgesehen "
          "hat - nur an anderen Stellen." },
        { "Scatters up to twelve additional consumables per level. Only "
          "what the level already knows is placed - ammo, medipacks, "
          "flares -, weighted by frequency: Andy2 (young Lara, no weapons) "
          "gets medipacks, a weapon level gets ammo. Keys, puzzle pieces "
          "and secrets are left out.",
          "Streut bis zu zwoelf zusaetzliche Verbrauchsgueter pro Level. "
          "Gelegt wird nur, was das Level ohnehin kennt - Munition, "
          "Medipacks, Fackeln -, gewichtet nach Haeufigkeit: Andy2 (Kind-Lara, "
          "keine Waffen) bekommt also Medipacks, ein Waffenlevel Munition. "
          "Schluessel, Puzzleteile und Secrets bleiben aussen vor." },
        { "Like \"More items\", but up to thirty per level. The limit is "
          "reached when the free placement spots run out or the engine's "
          "item limit (512) draws near - both cap the number on their own.",
          "Wie \"Mehr Items\", aber bis zu dreissig pro Level. Die Grenze "
          "ist erreicht, wenn die freien Ablageflaechen ausgehen oder das "
          "Item-Limit der Engine (512) naeher rueckt - beides deckelt die "
          "Zahl von selbst." },
        { "Every level gets the full weapon set on every run: pistols, Uzi, "
          "shotgun, Desert Eagle (replaces the revolver, in the joby levels "
          "by name too) and HK, ammo included. A level keeps its own "
          "working weapon; joby4 stays vanilla, and rich2 gets no pistols "
          "(its pistol slot carries sequence material from Core). The seed "
          "additionally scatters some of the new weapons with ammo into the "
          "level as pickups. The models come from the player's own level "
          "files at runtime.",
          "Jedes Level bekommt bei jedem Lauf das volle Waffenset: Pistolen, "
          "Uzi, Schrotflinte, Desert Eagle (ersetzt den Revolver, in den "
          "joby-Leveln auch im Namen) und HK samt Munition. Ein Level behaelt "
          "seine eigene funktionierende Waffe; joby4 bleibt vanilla, und "
          "rich2 bekommt keine Pistolen (sein Pistolenslot traegt "
          "Sequenzmaterial von Core). Der Seed streut zusaetzlich einige der "
          "neuen Waffen samt Munition als Pickups ins Level. Die Modelle "
          "kommen zur Laufzeit aus den eigenen Leveldateien des Spielers." },

        // --- Block 4: outfits --------------------------------------------
        { "Random outfits", "Zufaellige Outfits" },
        { "TR1 outfit", "TR1 Outfit" },
        { "TR2 outfits", "TR2 Outfits" },
        { "TR3 outfits", "TR3 Outfits" },
        { "Each section randomly gets Lara_Andrea, Lara_Joby or Lara_Rich "
          "(catsuit). Only skin and head are carried over (slots 8/9/10) "
          "plus the outfit byte - never slot 0, which carries the "
          "animations. joby4 keeps the diving suit; the andy levels always "
          "get an adult Lara (young Lara cannot carry weapons).",
          "Jeder Abschnitt bekommt zufaellig Lara_Andrea, Lara_Joby oder "
          "Lara_Rich (Catsuit). Uebertragen werden nur Haut und Kopf (Slots "
          "8/9/10) samt Outfit-Byte - nie Slot 0, der traegt die Animationen. "
          "joby4 behaelt den Taucheranzug; die andy-Level bekommen immer eine "
          "erwachsene Lara (Kind-Lara kann keine Waffen tragen)." },
        { "Coming later: the classic TR1 outfit as an additional choice in "
          "the outfit pool. Needs a matching TR5 model - we never ship "
          "other games' data, so the model has to be built from scratch or "
          "contributed by the player.",
          "Kommt noch: das klassische TR1-Outfit als zusaetzliche Wahl im "
          "Outfit-Pool. Braucht ein passendes TR5-Modell - Spieldaten "
          "anderer Spiele liefern wir nie mit, das Modell muss also "
          "eigens gebaut oder vom Spieler beigesteuert werden." },
        { "Coming later: the TR2 outfits (bomber jacket, wetsuit and more) "
          "as additional pool choices - same requirement as TR1.",
          "Kommt noch: die TR2-Outfits (u.a. Bomberjacke, Wetsuit) als "
          "zusaetzliche Poolauswahl - gleiche Voraussetzung wie TR1." },
        { "Coming later: the TR3 outfits (jungle, Antarctica and more) "
          "as additional pool choices - same requirement as TR1.",
          "Kommt noch: die TR3-Outfits (u.a. Dschungel, Antarktis) als "
          "zusaetzliche Poolauswahl - gleiche Voraussetzung wie TR1." },

        // --- Block 5: enemies --------------------------------------------
        { "5. Enemies", "5. Gegner" },
        { "Add extra enemies to the level", "Zusaetzliche Gegner ins Level setzen" },
        { "per level", "pro Level" },
        { "Swap existing enemies", "Vorhandene Gegner tauschen" },
        { "Enemy pool:", "Gegnerpool:" },
        { "Level pool only", "Nur Levelpool" },
        { "Enemy types...", "Gegnerarten..." },
        { "%d deselected", "%d abgewaehlt" },
        { "Reset balance", "Balance zuruecksetzen" },
        { "Each one gets its own trigger, about three sectors ahead on "
          "Lara's path - never on its own tile, or it would spawn inside "
          "her. Which enemy types a level can take is hand-curated; if the "
          "list is empty, the checkbox has no effect.",
          "Jeder bekommt einen eigenen Trigger, etwa drei Sektoren vor sich "
          "auf Laras Weg - nie auf seinem eigenen Feld, sonst spawnt er in ihr. "
          "Welche Gegnertypen ein Level vertraegt, ist handkuriert; ist die "
          "Liste leer, bleibt der Haken folgenlos." },
        { "The enemies already in the level swap roles. Only the type "
          "changes, the item index stays - every enemy still wakes up "
          "where its trigger intends. Count and distribution stay the "
          "same, scripted enemies remain untouched. Where the new roles "
          "come from is decided by the enemy pool below.",
          "Die Gegner, die im Level ohnehin stehen, tauschen die Rollen. "
          "Nur die Art wechselt, der Item-Index bleibt - jeder Gegner wacht "
          "also weiter dort auf, wo sein Trigger es vorsieht. Anzahl und "
          "Verteilung bleiben gleich, gescriptete Gegner bleiben unberuehrt. "
          "Woraus die neuen Rollen kommen, entscheidet der Gegnerpool unten." },
        { "Only what the level itself knows is swapped and placed "
          "(the hand-curated list per level).",
          "Getauscht und gesetzt wird nur, was das Level selbst kennt "
          "(die handkuratierte Liste je Level)." },
        { "Up to THREE foreign types from other levels join the pool - "
          "SWAT, guards, mafia, Larson, Pierre, lion, gladiator, statue, "
          "maze monster, imp and more. When swapping, roughly every fourth "
          "role goes to one of them, so the level stays recognizable. "
          "Models and sounds travel along from your own level files at "
          "runtime.",
          "Bis zu DREI fremde Typen aus anderen Leveln kommen in den Pool - "
          "SWAT, Guards, Mafia, Larson, Pierre, Loewe, Gladiator, Statue, "
          "Maze Monster, Imp und mehr. Beim Tausch geht etwa jede vierte "
          "Rolle an einen davon, das Level bleibt erkennbar. Modelle und "
          "Sounds wandern zur Laufzeit aus den eigenen Leveldateien mit." },
        { "The COMPLETE enemy pool is available to every level, and when "
          "swapping, every role rolls freely from everything - maximum "
          "variety. Still, only the types the seed actually uses are "
          "imported; the texture page budget stays, measured, well below "
          "the 256 limit. Scripted enemies remain untouched in every "
          "mode.",
          "Der KOMPLETTE Gegnerpool steht jedem Level zur Verfuegung, und "
          "beim Tausch wuerfelt jede Rolle frei aus allem - maximale "
          "Auswahl. Uebernommen werden trotzdem nur die Typen, die der "
          "Seed wirklich einsetzt; der Texturseiten-Haushalt bleibt "
          "gemessen deutlich unter der 256er-Grenze. Gescriptete Gegner "
          "bleiben in jedem Modus unberuehrt." },
        { "Which enemy types the random draw may pick at all. Good for "
          "seeing favorite enemies more often - or for testing a single "
          "type: deselect everything but one.",
          "Welche Gegnerarten der Zufall ueberhaupt ziehen darf. Gut, um "
          "Lieblingsgegner haeufiger zu sehen - oder um eine einzelne Art "
          "zu pruefen: alles abwaehlen bis auf eine." },
        { "Unchecks the random boxes, sets the three sliders to 1.00x and "
          "writes that to Balance.txt. Individual values tuned by hand in "
          "the file are kept - if you want those reset too, delete "
          "Balance.txt; the game recreates it completely on the next "
          "start.",
          "Nimmt die Zufallshaken zurueck, setzt die drei Regler auf 1.00x "
          "und traegt das in Balance.txt ein. "
          "Von Hand eingestellte Einzelwerte in der Datei bleiben erhalten - "
          "wer die auch zurueck will, loescht die Balance.txt; das Spiel legt "
          "sie beim naechsten Start neu und vollstaendig an." },

        // --- Block 6: levels ---------------------------------------------
        { "6. Levels", "6. Level" },
        { "Shuffle level sections", "Levelabschnitte switchen" },
        { "Level order: off", "Level-Reihenfolge: aus" },
        { "within the sections", "innerhalb der Abschnitte" },
        { "free across everything", "frei ueber alles" },
        { "Play the four story sections (Rome, Russia, Ireland, VCI) in "
          "random order. Within a section the order stays - all key "
          "chains remain valid, the credits come after the last section. "
          "REQUIRES the engine from 2026-08-22 on (level identity from "
          "the file name); old savegames do not match the new order.",
          "Die vier Story-Abschnitte (Rom, Russland, Irland, VCI) in "
          "zufaelliger Reihenfolge spielen. Innerhalb eines Abschnitts "
          "bleibt die Reihenfolge - alle Schluesselketten bleiben gueltig, "
          "der Abspann kommt nach dem letzten Abschnitt. BRAUCHT die "
          "Engine ab 2026-08-22 (Levelidentitaet aus dem Dateinamen); "
          "alte Spielstaende passen nicht zur neuen Reihenfolge." },
        { "Within: Rome/Russia/Ireland/VCI stay blocks, only the levels "
          "inside them shuffle. Free: all 13 levels across all sections "
          "(e.g. rich1 -> Andrea2 -> joby5 -> rich2). The cutscene "
          "richcut2 always stays attached to rich2. On every level start, "
          "keys/puzzles/pickups and the grappling gun (rich3 only) are "
          "reset - weapons, ammo, medipacks and flares are kept. Whatever "
          "a level needs from another one (Key 8 joby3, Key 7 joby5, "
          "Catapult Andy2, crowbar Andrea3), the script always grants - "
          "model and name included. Curated in LevelChains.h.",
          "Innerhalb: Rom/Russland/Irland/VCI bleiben Bloecke, nur die Level "
          "darin tauschen. Frei: alle 13 Level ueber alle Abschnitte hinweg "
          "(z.B. rich1 -> Andrea2 -> joby5 -> rich2). Die Cutscene richcut2 "
          "bleibt immer an rich2. Bei jedem Levelstart werden Keys/Puzzles/"
          "Pickups und die Grappling Gun (nur rich3) zurueckgesetzt - Waffen, "
          "Munition, Medis und Leuchtfackeln bleiben. Was ein Level aus einem "
          "anderen braucht (Key 8 joby3, Key 7 joby5, Catapult Andy2, "
          "Brechstange Andrea3), gibt das Skript immer - samt Modell und Namen. "
          "Kuratiert in LevelChains.h." },

        // --- Block 7: music ----------------------------------------------
        { "7. Music", "7. Musik" },
        { "Shuffle background music", "Hintergrundmusik switchen" },
        { "Shuffle trigger sound/music IDs", "Trigger-Sound/Musik-IDs switchen" },
        { "The level music is shuffled among the playable levels - the "
          "audio byte in the CMD_LEVEL entry of SCRIPT.DAT. Cutscene "
          "levels keep their music. Every run rolls anew from the backup; "
          "uncheck it and the next run restores the original music.",
          "Die Levelmusik wird unter den spielbaren Leveln vertauscht - "
          "das Audio-Byte im CMD_LEVEL-Eintrag der SCRIPT.DAT. Cutscene-"
          "Level behalten ihre Musik. Gewuerfelt wird bei jedem Lauf neu "
          "aus der Sicherung heraus; Haken weg = naechster Lauf stellt "
          "die Originalmusik wieder her." },
        { "The music triggers in the levels (TO_CD) get shuffled tracks - "
          "GLOBALLY across all levels: only tracks that some trigger in "
          "the game actually fires (no cutscene dialogue, no FMV audio), "
          "and the same track sounds the same in every level. Atmospheres "
          "still swap only among themselves, so a mood change stays a "
          "mood change. The hard-coded cutscene and tutorial triggering "
          "is not affected.",
          "Die Musik-Trigger in den Leveln (TO_CD) bekommen vertauschte "
          "Tracks - GLOBAL ueber alle Level: nur Tracks, die irgendwo im Spiel "
          "ein Trigger ausloest (keine Cutscene-Dialoge, kein FMV-Ton), "
          "und derselbe Track klingt in jedem Level gleich. "
          "Atmosphaeren tauschen weiter nur unter sich, damit ein "
          "Stimmungswechsel ein Stimmungswechsel bleibt. Die fest "
          "einprogrammierte Cutscene- und Tutorial-Ausloesung ist "
          "nicht betroffen." },

        // --- Block 8: secrets --------------------------------------------
        { "In the normal item pool", "Im normalen Item-Pool" },
        { "Glitchless list", "Glitchless-Liste" },
        { "Glitched list", "Glitched-Liste" },
        { "(no lists found)", "(keine Listen gefunden)" },
        { "Author", "Autor" },
        { "No %s list found in data\\secrets - the levels stay vanilla.",
          "Keine %s-Liste in data\\secrets gefunden - die Level bleiben vanilla." },
        { "The secrets stay where Core hid them.",
          "Die Secrets bleiben, wo Core sie versteckt hat." },
        { "The secrets go into the shuffle pool as ordinary pickups and "
          "can lie anywhere equipment is allowed to lie.",
          "Die Secrets wandern als gewoehnliche Pickups in den Wuerfelpool "
          "und koennen ueberall dort liegen, wo auch Ausruestung liegen darf." },
        { "Hand-picked hiding spots by one author, reachable without "
          "glitches. A list can also place extra objects per level - "
          "keys, switches, doors, even new portals.",
          "Handverlesene Verstecke eines Autors, ohne Glitches erreichbar. "
          "Eine Liste kann je Level auch Zusatzobjekte setzen - Schluessel, "
          "Schalter, Tueren, sogar neue Portale." },
        { "Like the glitchless list, but the hiding spots may require "
          "glitches.",
          "Wie die Glitchless-Liste, aber die Verstecke duerfen Glitches "
          "voraussetzen." },

        // --- Block 9: in preparation -------------------------------------
        { "9. In preparation", "9. In Vorbereitung" },
        { "Textures", "Texturen" },
        { "RGB vanilla textures", "RGB Vanilla Texturen" },
        { "Mix across levels", "Leveluebergreifend mixen" },
        { "Patch remaster textures", "Remaster-Texturen patchen" },
        { "Rooms", "Raeume" },
        { "Include backdrop areas", "Kulissen einbinden" },
        { "Make neighboring rooms walkable", "Nachbar-Raeume begehbar machen" },
        { "Coming later: color dice over the vanilla texture pages - the "
          "seed shifts the hues, purely cosmetic.",
          "Kommt noch: Farbwuerfel ueber die Vanilla-Texturseiten - der "
          "Seed verschiebt die Farbtoene, rein kosmetisch." },
        { "Coming later: swap texture pages between levels - Rome in "
          "Russia, the submarine in Ireland.",
          "Kommt noch: Texturseiten zwischen den Leveln tauschen - Rom in "
          "Russland, U-Boot in Irland." },
        { "Coming later: patch remaster textures (if the player owns "
          "them) into the classic levels.",
          "Kommt noch: Remaster-Texturen (sofern der Spieler sie hat) in "
          "die klassischen Level patchen." },
        { "Coming later: bring pure backdrop areas into play so items "
          "and enemies can be placed there.",
          "Kommt noch: reine Kulissenbereiche ins Spiel einbinden, damit "
          "dort Items und Gegner liegen koennen." },
        { "Coming later: open adjacent rooms that are locked in vanilla - "
          "more area for placement.",
          "Kommt noch: angrenzende, vanilla gesperrte Raeume oeffnen - "
          "mehr Flaeche fuer die Platzierung." },
        { "Coming later: Deepsea Dive without water - joby4 would "
          "finally be fully randomizable instead of vanilla.",
          "Kommt noch: Deepsea Dive ohne Wasser - damit waere joby4 "
          "endlich voll randomisierbar statt vanilla." },

        // --- Block 10: level list ----------------------------------------
        { "10. Levels", "10. Level" },
        { "%d non-playable files (Title, del, richcut2) are checked, "
          "but never touched.",
          "%d nicht spielbare Dateien (Title, del, richcut2) werden "
          "geprueft, aber nicht angetastet." },
        { "State", "Zustand" },
        { "modified", "veraendert" },
        { "missing", "fehlt" },
        { "unknown", "unbekannt" },
        { "Error", "Fehler" },
        { "skipped", "uebersprungen" },
        { "done", "fertig" },

        // --- Block 11: let's go ------------------------------------------
        { "11. Let's go", "11. Los geht's" },
        { "Restoring originals...", "Originale werden zurueckgespielt..." },
        { "Randomize", "Randomisieren" },
        { "Restore original", "Auf Original zuruecksetzen" },
        { "(choose an unmodified installation first)",
          "(erst eine unveraenderte Installation waehlen)" },
    };
    return t;
}

const char* T(const char* en)
{
    if (!gGermanUi || !en || !*en) return en;
    const auto& t = table();
    auto it = t.find(en);
    return it == t.end() ? en : it->second;
}
