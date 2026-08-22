# Levelreihenfolge würfeln — Befund und Plan

Stand 2026-08-22. Anlass: die beiden ausgegrauten Haken „Levelabschnitte
switchen" und „Level-Reihenfolge switchen". Vor dem Bauen: was wandert
zwischen den Leveln, und woran hängt die Engine?

## 1. Was die SCRIPT.DAT je Level gibt und nimmt

Aus der Vanilla-SCRIPT.DAT (`SkriptPruef -roh`), Inventarnummern über
`invobj_types` aufgelöst:

| Level | gibt beim Start | nimmt beim Start |
|---|---|---|
| Andrea1 | PICKUP_ITEM1, Uzi | Uzi (Dev-Relikt: geben und gleich nehmen) |
| joby3 | — | Uzi, Pistolen, Revolver, Lasersight (Lara gefangen) |
| rich1 | HK | — |
| rich2 | HK, PUZZLE_ITEM2 | PICKUP_ITEM2, KEY_ITEM7, KEY_ITEM8 (+ unsere Pistolen) |
| GIBBY (Dev) | Crossbow-Ammo, HK, Lasersight | — |

Alle anderen Level greifen nicht ins Inventar. **Alles, was ein Level
nicht nimmt, bleibt im Inventar** — Schlüssel inklusive.

## 2. Schlüsselmatrix (Items vs. Schlösser je Level)

`ItemPruef` über alle Vanilla-Level, Familie PUZZLE/KEY/PICKUP (IDs
172–265 laut Engine-Enum; Zählfalle beachtet, Gegenprobe LARA=0,
KEY_HOLE8=265). PICKUP_ITEM4 = die drei Secret-Rosen je Level.

| Level | Items | Schlösser | Befund |
|---|---|---|---|
| Andrea1 | PUZZLE1×2, KEY1, PICKUP2 | PUZZLE_HOLE1×2, KEY_HOLE1 | in sich geschlossen; PICKUP2 ohne Schloss |
| Andrea2 | PUZZLE1,3,4,5, **Brechstange** | PUZZLE_HOLE1,3,4,5 | geschlossen; Brechstange bleibt im Inventar |
| Andrea3 | PUZZLE4 (2 Hälften), KEY1, KEY2, PICKUP1 | PUZZLE_HOLE4×2, KEY_HOLE1, KEY_HOLE2 | PICKUP1 = Story (Cutscene, `deltapak.cpp:1767`) |
| joby2 | PUZZLE1, **KEY7×2, KEY8** | PUZZLE_HOLE1, KEY_HOLE7×2, KEY_HOLE8×4 | KEY8 wandert weiter |
| joby3 | PUZZLE1, PUZZLE3+4 (Hälften), KEY6, KEY7 | PUZZLE_HOLE3,4, KEY_HOLE6,7, **KEY_HOLE8×2** | **braucht KEY8 aus joby2**; PUZZLE1 ohne Schloss |
| joby4 | — | — | reines Tauchlevel |
| joby5 | PUZZLE1,2, KEY6, KEY8 | PUZZLE_HOLE1,2, KEY_HOLE6, **KEY_HOLE7**, KEY_HOLE8×2 | **braucht KEY7 aus joby2/joby3** |
| andy1 | PUZZLE1, PUZZLE3, PUZZLE2 (Hälften), Fackel | PUZZLE_HOLE1 | **PUZZLE2+3 wandern weiter** |
| Andy2 | — | PUZZLE_HOLE1, PUZZLE_HOLE2 | **braucht Puzzles aus andy1** |
| andy3 | PUZZLE1,2,3, Fackel | — | Items ohne Schloss (Story/Cutscene) |
| rich1 | PUZZLE3,4, KEY7, KEY8, PICKUP2, Flaschen, Tuch | PUZZLE_HOLE1,3,4, KEY_HOLE8×4 | **PUZZLE_HOLE1 ohne Item** (Herkunft prüfen); KEY7 wird von rich2 weggenommen |
| rich2 | PUZZLE3, PICKUP2, Flaschen, Tuch | PUZZLE_HOLE2,3, PUZZLE_HOLE8×2 | HOLE2 über Skript-Gabe PUZZLE2; HOLE8 prüfen |
| rich3 | PUZZLE1 (Hälften) | PUZZLE_HOLE1 | geschlossen |
| richcut2 | — | — | Cutscene-Level zwischen rich2 und rich3 |

Vorsicht: Schlosszahlen können durch Flipmap-Räume aufgebläht sein (Item
in Normal- und Flip-Raum zählt doppelt). Die *Richtung* der Abhängigkeit
ist trotzdem eindeutig.

**Cross-Level-Abhängigkeiten (vanilla):**
- joby2 → joby3: KEY_ITEM8 (genau das, was der Nutzer für sein Secret
  einbauen ließ — jetzt wissen wir, warum joby3 das Modell nie brauchte:
  der Schlüssel kam im Inventar).
- joby2/joby3 → joby5: KEY_ITEM7.
- andy1 → Andy2: PUZZLE_ITEM2 (kombiniert) und PUZZLE_ITEM3.
- Andrea2 → (Andrea3?): Brechstange — `door.cpp:314ff` kennt
  Brechstangen-Türen; welche Türen in Andrea3 das sind, ist noch zu prüfen
  (kein CROWBAR_SWITCH in irgendeinem Level).
- rich1 → rich2: nichts (rich2 nimmt KEY7/8 bewusst weg; PUZZLE2 kommt
  per Skript).

Story-Tore, die am Inventar hängen: `control.cpp:781` beendet ein Level
nur mit PICKUP_ITEM1 im Inventar (Flipeffect 65) — levelgebunden, muss
mit dem Level mitwandern, nicht mit dem Slot.

## 3. Woran die Engine hängt

- **Levelende ist rein sequenziell:** `TO_FINISH` setzt
  `gfLevelComplete = gfCurrentLevel + 1`. Reihenfolge = Reihenfolge der
  Blöcke in der SCRIPT.DAT. Harte Sprungziele gibt es nur für Cheat,
  Cutscene-Auswahl (`cutsel[].lvl`) und „neues Spiel = Slot 1".
- **121 Stellen prüfen die Levelnummer**, davon **120 gegen
  `LVL5_`-Konstanten** (Identität: „bin ich im 13. Stock?") und eine als
  Index (`wanky_secrets_table[gfCurrentLevel]`, Secrets je Level). Dazu
  **23 Stellen**, die den Slot als Index/Arithmetik brauchen
  (`gfLevelNames[]`, `+1`, Savegame, Level-Select).
- **Was mit einem Block mitwandert:** FMV-Befehle, residente Cutscenes
  (CUT1–4), Flags (Horizont, Wetter, Kind-Lara), Levelmusik, die
  Objekt-/Inventarbefehle. Alles, was die Story eines Levels braucht,
  steht in seinem Block — gut für uns.
- **Was NICHT mitwandert:** die Levelnummer selbst. Die Engine glaubt
  nach einer Block-Permutation, Slot 1 sei immer noch Rom.

## 4. Kernidee A — Identität statt Slot

Neue Engine-Variable `gfLevelId` (Wert aus `LVL5_*`), beim Levelstart aus
dem **Dateinamen** des geladenen Levels abgeleitet (`DoLevel(Name)` →
Dateiindex → „DATA\JOBY3" → `LVL5_SUBMARINE`). Dann:

- die 120 Identitätsvergleiche mechanisch auf `gfLevelId` umstellen
  (alle vergleichen gegen `LVL5_`-Namen — ein Durchlauf mit Gegenprobe);
- `wanky_secrets_table` über `gfLevelId` indizieren;
- die 23 Slot-Stellen **unverändert** lassen (Sequenz, Namen, Saves);
- Cheat-Sprung und Cutscene-Auswahl bekommen eine Umkehrtabelle
  Id → Slot (aus der SCRIPT.DAT beim Laden aufgebaut).

Ohne Permutation ist `gfLevelId == gfCurrentLevel` — Vanilla verhält
sich byteidentisch. Das ist der Sicherheitsanker: die Umstellung lässt
sich ohne Reihenfolge-Würfel durchspielen.

## 5. Kernidee B — Abhängigkeiten per Skript auflösen

Wandert ein Level vor sein Spenderlevel, fehlt ein Schlüssel. Statt die
Abhängigkeit zu verbieten: **das Zielevel bekommt das Item per `GIVEOBJ`
beim Start** — genau der Befehl, den `ScriptDat` heute schon für KEY8
und die HK einfügt. Abhängigkeitstabelle (aus Abschnitt 2):

| Level | bekommt per GIVEOBJ, wenn der Spender nicht davor liegt |
|---|---|
| joby3 | KEY_ITEM8 |
| joby5 | KEY_ITEM7 (und KEY8, falls joby2/joby3 nicht davor) |
| Andy2 | PUZZLE_ITEM2, PUZZLE_ITEM3 |
| Andrea3 | Brechstange (nach Prüfung der Türen) |

Die Skript-**Wegnahmen** (joby3 Waffen, rich2 Keys) bleiben, wo sie sind
— sie gehören zur Story des Levels und wandern mit dem Block.

## 6. Stufenplan

**Stufe 0 — Engine-Identität (Voraussetzung für alles).** Kernidee A,
ohne Würfel. Test: Vanilla-Durchlauf, alle levelspezifischen Dinge
(Cutscenes, Tower, Kind-Lara-Flag, Secrets-Bonus) unverändert.

**Stufe 1 — Levelabschnitte switchen.** Die vier Abschnitte als
unteilbare Blöcke permutieren: Rom (Andrea1–3), Russland (joby2–5),
Irland (andy1–3), VCI (rich1, rich2, richcut2, rich3). Innerhalb bleibt
die Reihenfolge, damit bleiben **alle** Abhängigkeiten aus Abschnitt 2
gültig — kein GIVEOBJ nötig. Auch die FMV-Reihenfolge ergibt noch einen
Sinn (jeder Abschnitt bringt seine Videos mit). Risiko: gering.

**Stufe 2 — Reihenfolge innerhalb der Abschnitte.** Permutation im
Abschnitt, `richcut2` bleibt an rich2 geklebt. Braucht Kernidee B für
joby3/joby5/Andy2. Risiko: mittel (Schlüsselketten im Spiel verifizieren).

**Stufe 3 — frei über alles.** Beide Haken zusammen. Risiko: mittel
bis hoch — Schwierigkeitskurve und Waffenstand werden beliebig (joby3
nimmt Waffen weg, egal wann es kommt; rich1 gibt die HK, egal wann).

## 7. Offen / zu prüfen, bevor gebaut wird

1. Brechstangen-Türen in Andrea3 (`door.cpp:314`) — welche Items/OCBs?
2. rich1 PUZZLE_HOLE1 und rich2 PUZZLE_HOLE8×2 ohne Item — Flipmap-
   Dubletten oder echte Importe?
3. Flipeffect-65-Tor (`control.cpp:781`): in welchem Level, und hängt
   PICKUP_ITEM1 dort an einer Cutscene desselben Levels?
4. `available_levels[]` (Level-Select) und Statistik-Tabellen sind
   slotbasiert — für Anzeige egal, für Level-Select aber nach Permutation
   neu zu füllen.
5. Savegames: `savegame.CurrentLevel` ist ein Slot. Ein Spielstand gehört
   damit zu *seiner* SCRIPT.DAT — wie heute schon bei jedem Seed. Im
   Randomizer klar sagen.
6. Kind-Lara-Bit (GF_YOUNGLARA): im Skript des Nutzers bereits 0 —
   klären, ob Vanilla es überhaupt setzt oder die Irland-Outfits das
   regeln; sonst wäre „Irland zuerst" ein waffenloses Startlevel.

## 8. Aufwand

Stufe 0 ist der Löwenanteil (Engine, ~120 mechanische Stellen +
Umkehrtabelle + Vanilla-Regression). Stufe 1 danach klein
(`ScriptDat`: Offsettabelle permutieren, Blöcke bleiben unangetastet).
Stufe 2 mittel (Abhängigkeitstabelle + GIVEOBJ + Spieltests der
Schlüsselketten). Vergleichbar mit der Sprachwahl plus ein TR3-Port-Tag.

## 9. Stand nach dem ersten Spieltest (2026-08-22)

- Stufe 0 + 1: im Spiel bestätigt (Abschnitte, Videos nach Identität,
  Finale am letzten Slot, Abspann abbrechbar).
- Stufe 2 + 3 gebaut (innerhalb / frei, Levelketten.h mit drei aktiven
  Ketten, GIVEOBJ + Namensbefehl + Modellkopie, Wegnahme-Fall über
  TAKEOBJ dazwischen). Testseed für „frei": **380**.
- **Spieltest „frei" (Seed 380): „es passiert, was ich befürchtet habe —
  das wird was Großes."** Der Nutzer trägt eine große Fehlerliste zusammen;
  die wird die Grundlage für die nächste Runde. Erwartbare Kandidaten (aus
  Abschnitt 7): verwaiste Schlösser (rich1 PUZZLE_HOLE1, rich2
  PUZZLE_HOLE8, Andy2 PUZZLE_HOLE1), Brechstange Andrea3, levelgebundene
  Story-Tore, Waffenstand nach joby3 (nimmt alles), Irland als Startlevel.

## 10. Inventar-Garbage-Collector (2026-08-22, Vorschlag des Nutzers)

**Die Einsicht:** die Inventar-Slots sind generisch. `PUZZLE_ITEM1` ist in
Andrea1 der Garden Key, in joby4 die Chaff flares, in andy1 das Heart, in
rich3 der Helipad access key — jedes Level benennt denselben Slot um. Ein
mitgeschleppter Garden Key *wird* in joby4 zu Chaff flares. Und die
„Grappling Gun" ist der Crossbow-Slot (95 + Munition), der nur in rich3
Modell und Anims hat (Hard-Crash beim Ausrüsten anderswo).

**Darum:** beim Start jedes Kampagnen-Levels nimmt das Skript per TAKEOBJ
alles aus der Schlüsselfamilie weg, was ein früheres Level anbieten konnte
und das weder dieses noch ein späteres Level per Kette importiert; die
Crossbow-Familie überall außer rich3. Ketten (`Levelketten.h`) sind jetzt
„geben" (GIVEOBJ + Name + Modell) oder „nur behalten" (Story-Items
unterwegs). Engine: `gfPickups`/`gfTakeaways` von 16 auf 64 mit Riegel.

Offen für den Spieltest: ob die „nur behalten"-Ketten reichen (Iris,
Bestiary, Spear, Iron clapper), und ob irgendein Level ein Item braucht,
das die Matrix nicht zeigt (Flipmap-Dubletten).
