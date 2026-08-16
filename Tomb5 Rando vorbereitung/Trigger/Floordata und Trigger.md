# Floordata und Trigger in TR5

Alles hier ist am Enginecode abgelesen (`TOMB5 Code\TOMB5`), nicht aus
Community-Dokumentation übernommen. Die maßgeblichen Stellen:
`GetHeight` (`game/control.cpp:960`) läuft die Kette ab,
`_TestTriggers` (`game/control.cpp:2126`) wertet einen Trigger aus,
`struct FLOOR_INFO` (`global/types.h:603`) ist der Einstieg.

---

## 1. Wo die Daten liegen

Im Hauptblock, direkt hinter den Räumen:

```
long numFloorData;  ushort floor_data[numFloorData];
```

Jeder Sektor eines Raums trägt einen `FLOOR_INFO`:

```c
struct FLOOR_INFO {
    ushort index;       // Offset in floor_data - 0 heisst KEINE Floordata
    ushort fx : 4;
    ushort box : 11;
    ushort stopper : 1;
    uchar pit_room;  char floor;
    uchar sky_room;  char ceiling;
};
```

**`index == 0` bedeutet „nichts".** Damit ist Offset 0 im Floordata-Block
unbenutzbar — dasselbe Muster wie beim Mesh-Index 0, nur diesmal von Core
selbst so gemeint (`if (!floor->index) return;`, `control.cpp:968`).

## 2. Die Kette

`floor_data[index]` ist der Anfang einer Liste variabler Länge. Je Eintrag:

| Bits | Bedeutung |
|---|---|
| 0–4 (`& 0x1F`) | Funktion, siehe Tabelle |
| 5–14 | funktionsabhängige Nutzdaten (z. B. Höhenversatz bei den Split-Typen) |
| 15 (`& 0x8000`) | **letzter Eintrag der Kette** |

Funktionen aus `enum floor_types` (`types.h:322`) mit der Zahl der
**zusätzlich** verbrauchten Wörter, abgelesen an `GetHeight`:

| # | Name | Extra-Wörter |
|---|---|---|
| 0 | FLOOR_TYPE | – |
| 1 | DOOR_TYPE (Portal) | 1 |
| 2 | TILT_TYPE (Bodenneigung) | 1 |
| 3 | ROOF_TYPE (Deckenneigung) | 1 |
| **4** | **TRIGGER_TYPE** | **1 + variable Befehlsliste** |
| 5 | LAVA_TYPE | – |
| 6 | CLIMB_TYPE | – |
| 7–10 | SPLIT1…SPLIT4 | 1 |
| 11–18 | NOCOLF*/NOCOLC* | 1 |
| 19 | MONKEY_TYPE | – |
| 20 | TRIGTRIGGER_TYPE | – |
| 21 | MINER_TYPE | – |

## 3. Der Trigger-Eintrag

```
Wort 0:  Funktion 4 in Bits 0-4, TRIGGERART in Bits 8-13
Wort 1:  Timer (Bits 0-7) | OneShot (Bit 8) | Codebits/Maske (Bits 9-13)
[Wort 2: nur bei SWITCH, KEY und PICKUP - das zu bedienende bzw. aufzunehmende Item]
Wort n:  Befehle, bis eines Bit 15 gesetzt hat
```

**PICKUP gehört in diese Zeile.** `_TestTriggers` liest das Wort für alle drei
Arten gleich: `control.cpp:2251` (SWITCH), `:2266` (KEY), `:2274` (PICKUP), jedes
Mal `value = *(data++) & 0x3FF;`.

Exporter und FloorTool haben lange nur SWITCH und KEY geprüft. Das hat die
Kette nicht verschoben — das Wort wurde einfach als **erster Befehl**
ausgegeben — und weil ein Pickup-Verweis immer als Befehlsart 0 mit dem Index
des eigenen Items daherkommt, sah er sogar aus wie ein plausibles
`Object`-Kommando. Das ist er nicht:

- Es ist kein Befehl, sondern der Bezug des Triggers auf sein Item.
- Beim **Umbauen** einer Kette hätte er als löschbarer Befehl gegolten. Wer aus
  einem Pickup-Trigger den Befehl `Object 107` entfernt, entfernt in Wahrheit
  das Referenzwort und zerstört den Trigger lautlos.

Der frühere Andrea1-Versuch (Befehl `Object 26` aus Trigger 118 entfernen) hat
das nur überlebt, weil 26 ≠ 107 war. Das war Glück, keine Korrektheit.

Aus `_TestTriggers`:
`type = (*data++ >> 8) & 0x3F;` dann `flags = *data++; timer = flags & 0xff;`

**Triggerarten** (`enum trigger_types`, `types.h:414`): TRIGGER, PAD, SWITCH,
KEY, PICKUP, HEAVY, ANTIPAD, COMBAT, DUMMY, ANTITRIGGER, HEAVYSWITCH,
HEAVYANTITRIGGER, MONKEY, SKELETON_T, TIGHTROPE_T, CRAWLDUCK_T, CLIMB_T.

**Befehlswort:** Parameter in Bits 0–9, Befehlsart in Bits 10–13, Ende in Bit 15.

**Befehlsarten** (`enum trigobj_types`, `types.h:435`): TO_OBJECT, TO_CAMERA,
TO_SINK, TO_FLIPMAP, TO_FLIPON, TO_FLIPOFF, TO_TARGET, TO_FINISH, TO_CD,
TO_FLIPEFFECT, TO_SECRET, TO_BODYBAG, TO_FLYBY, TO_CUTSCENE.

**TO_CAMERA und TO_FLYBY verbrauchen ein zusätzliches Wort.** Wer das beim
Ablaufen vergisst, liest den Rest der Kette verschoben — `GetHeight` behandelt
es an `control.cpp:1027` ausdrücklich.

### Geklärt: die Befehlsart hat 4 Bit

Die Engine belegt das an zwei unabhängigen Stellen:

- `_TestTriggers`: `switch ((trigger & 0x3FFF) >> 10)` — `control.cpp:2350`
- `GetHeight`: `(trigger & 0x3C00) != (TO_OBJECT << 10)` — `control.cpp:1025`

Bit 15 ist das Endebit, **Bit 14 wertet die Engine überhaupt nicht aus**. Der
Exporter las 5 Bit (`(wCmd & 0x7C00) >> 10`) und hätte ein gesetztes Bit 14
anders gedeutet als das Spiel. Auf `0x3C00` angeglichen, an beiden Fundstellen
in `TRFile_RandomizerExport.cpp` und im FloorTool.

---

## 4. Wie geändert wird: nur anhängen und umhängen

Vorgabe des Nutzers, dieselbe wie bei den Items: **add und replace, kein
remove.** Das ist hier keine Einschränkung, sondern die Lösung.

Floordata ist ein gemeinsamer Block, in den alle Sektoren zeigen. Bytes
mittendrin zu entfernen würde jeden nachfolgenden Index verschieben — also
**nie in der bestehenden Kette editieren**. Stattdessen:

1. Die neue Kette (alte Einträge plus/minus Trigger) **hinten an
   `floor_data` anhängen**.
2. `FLOOR_INFO.index` des betroffenen Sektors auf den neuen Offset setzen.
3. Die alte Kette bleibt als toter Ballast liegen. Sie stört niemanden — auf
   sie zeigt danach kein Sektor mehr.

Damit verschiebt sich **kein einziger vorhandener Index**. Auch das Entfernen
eines Triggers von einem Sektor läuft so: neue Kette ohne ihn anhängen,
umhängen, fertig — im Block wird nichts gelöscht.

**Kopf mitziehen:** `numFloorData` am Anfang des Blocks, die Größe des
Hauptblocks (steht doppelt davor, size und compressedSize) — genau wie beim
Anhängen von Items.

### Wohin in der Kette ein Trigger darf

Ein Trigger darf **nicht an beliebiger Stelle** stehen. Das Zusammenspiel von
`GetHeight` und `_TestTriggers` gibt eine feste Reihenfolge vor:

`GetHeight` (`control.cpp:1014–1053`) merkt sich beim Ablaufen der Kette einen
Zeiger `trigger_index`. Gesetzt wird er auf den **ersten** Eintrag aus
{TRIGGER, CLIMB, MONKEY, TRIGTRIGGER}; LAVA setzt ihn sogar bedingungslos neu.
Portale, Neigungen und die SPLIT/NOCOL-Einträge setzen ihn **nicht** — die
frisst `GetHeight` beim Laufen einfach mit.

`_TestTriggers` beginnt an diesem Zeiger und überspringt ab dort **genau vier**
Eintragsarten — LAVA, CLIMB, MONKEY, TRIGTRIGGER (`control.cpp:2151–2195`) —
und liest dann in Zeile 2197 **bedingungslos** einen Triggerkopf:

```c
type = (*data++ >> 8) & 0x3F;
```

Daraus folgt die Regel zum Anhängen:

> **Hinten anhängen ist erlaubt, solange die Kette keinen LAVA-, CLIMB-,
> MONKEY-, TRIGTRIGGER- oder TRIGGER-Eintrag enthält.**

Steht eines dieser fünf schon drin, zeigt `trigger_index` dorthin, und alles,
was zwischen ihm und dem angehängten Trigger liegt — ein Portal, eine Neigung —
wird von `_TestTriggers` als Triggerkopf fehlgedeutet. Ein Portalwort als
Trigger gelesen ergibt eine zufällige Triggerart mit zufälligen Befehlen.

Portale und Neigungen **allein** sind dagegen harmlos: sie stehen vor dem
Zeiger, nicht dahinter. Ein Sektor mit reiner Geometrie-Floordata ist also ein
gültiges Ziel. Geprüft wird das in `FloorTool` von `darfAnhaengen()`.

### Platz ist genug

`FLOOR_INFO.index` ist ein `ushort`, also maximal 65535. Gemessen an allen
dreizehn Leveln:

| Level | Floordata | | Level | Floordata |
|---|---|---|---|---|
| Andrea1 | 5 345 | | joby3 | 1 717 |
| Andrea2 | 6 035 | | joby4 | 10 688 |
| Andrea3 | 4 771 | | joby5 | 1 809 |
| andy1 | 10 692 | | rich1 | 2 740 |
| **andy3** | **14 890** | | rich2 | 5 240 |
| Andy2 | 6 169 | | rich3 | 5 189 |
| joby2 | 8 476 | | | |

Der größte liegt bei 23 % der Grenze. Selbst wenn wir die Floordata
vervierfachen, bleibt Luft. Die Grenze gehört trotzdem geprüft, bevor
angehängt wird — ein Level, das sie reißt, muss abgelehnt und nicht
stillschweigend kaputtgeschrieben werden.

---

## 4a. Was gezählt wurde

Über alle dreizehn Level (aus den `level_*.json`-Exporten):

| | |
|---|---|
| Trigger insgesamt | 2 057 |
| davon wecken einen Gegner | 269 |
| davon Pickup/Key-Trigger | **7** |
| Items, die heute festhängen | **7** |
| Items, die durch reines Gegner-Loslösen frei würden | **0** |

Verteilung: Trigger 1366, HeavyTrigger 240, AntiTrigger 181, Switch 82, Pad 53,
Key 39, Skeleton 36, Dummy 26, **Pickup 11**, HeavyAntiTrigger 10, Monkey 5,
Tightrope 4, HeavySwitch 2, Antipad 1, Climb 1.

**Folgerung:** Für die Itemfreiheit ist die Triggerarbeit fast wertlos — es geht
um sieben Items, und in jedem der sieben Fälle hängt neben dem Gegner noch etwas
anderes am Trigger (Tür, Flipmap, Cutscene). Der Wert liegt beim **Gegner-Pool**
und beim **Hinzufügen** von Gegnern: 269 Trigger sind die Masse, mit der sich
arbeiten lässt.

## 4b. Regeln für das Verschieben (Vorgabe des Nutzers)

### Entschieden am 13.08.2026: die Items bleiben, wie sie kuriert sind

Nachdem die Zählung ergeben hatte, dass von 2044 Items nur **zwei** durch
Trigger-Verschieben frei würden, hat der Nutzer den Item-Zweig geschlossen:

> „Ok dann lass die Items so lassen wie wir die manuell kurratiert haben. Dann
> nutzen wir die Trigger nur für neue Gegner die wir dem Level hinzufügen."

Damit gilt:

- Der **allgemeine gekoppelte Zug** von Item und Trigger wird **nicht**
  verfolgt. Die Kuration bleibt Maßstab.
- **Eine Ausnahme**, ebenfalls Nutzervorgabe: **Andrea1 Key 107** darf in den
  Pool, sein Pickup-Trigger wandert mit („da müssen wir nur den Trigger nur mit
  verschieben"). Siehe 4e.
- Der eigentliche Zweck der Triggerarbeit ist das **Hinzufügen neuer Gegner**.
  Dafür wird keine vorhandene Kette angefasst — siehe 4f.

**Trigger und Item bleiben zusammen**, wo überhaupt verschoben wird: der
Trigger wandert mit auf den neuen Sektor, ein Vorgang, nicht zwei. Der Sektor
ist neu, die Kopplung bleibt.

**Diese Trigger werden NIE verschoben**, weil sie raumspezifische Gegebenheiten
erwarten:

- **Flipmap** (alle `TO_FLIPMAP`/`TO_FLIPON`/`TO_FLIPOFF`)
- **Antitrigger** (`ANTIPAD`, `ANTITRIGGER`, `HEAVYANTITRIGGER`)
- **Heavy** (`HEAVY`, `HEAVYSWITCH`, `HEAVYANTITRIGGER`)
- **Cutscene** (`TO_CUTSCENE`, und `TO_FLYBY` gehört sinngemäß dazu)

Zusammen sind das über 400 der 2057 Trigger. Sie bleiben, wo sie sind, und das
Item an ihnen bleibt festgenagelt.

**Diese Items bleiben vorerst vanilla** (Nutzerentscheidung):

| Level | Items |
|---|---|
| Andrea2 | 71, 121, 128 |
| Andrea3 | 68 |
| andy3 | 6, 26 |
| joby3 | 41, 187 |
| rich1 | 14, 27 |

Beim Nachschlagen zeigte sich ein Muster, das die Liste erklärt: **fast alle
haben eine Sonder-OCB** — 121/128/68/26 haben OCB 2, 6/41 haben OCB 9, rich1s
Iris (14) hat OCB 8, item 27 hat OCB 6. Aus der Reihe fallen nur Andrea2 Item 71
(ein Animating, gar kein Pickup) und joby3 Item 187 (Key6 auf einem
Cutscene-Trigger, fällt also schon unter die globale Regel).

## 4b-bis. Bit 0x40: die Bedingung, ohne die kein Pickup-Trigger feuert

Ein Pickup-Trigger feuert **nicht**, weil er auf dem richtigen Sektor liegt. Er
feuert nur, wenn das aufgenommene Item danach überhaupt noch existiert — und das
entscheidet ein Bit **oberhalb** der sichtbaren OCB.

`PickUpCollision`, `game/pickup.cpp:989`:

```c
if (!(item->trigger_flags & 0xC0))
    KillItem(item_number);
else
{
    item->item_flags[3] = 1;
    item->flags |= IFL_TRIGGERED;
    item->status = ITEM_INVISIBLE;
}
```

Und `PickupTrigger`, `pickup.cpp:158`, verlangt genau diese beiden Zustände:

```c
if (item->flags & IFL_CLEARBODY || item->status != ITEM_INVISIBLE ||
    item->item_flags[3] != 1 || item->trigger_flags & 128)
    return 0;
```

Ohne 0x40 (oder 0x80) wird das Item beim Aufnehmen schlicht getötet, erreicht nie
`ITEM_INVISIBLE`, und `case PICKUP:` in `_TestTriggers` steigt mit `return` aus,
**bevor** ein einziger Befehl gelesen wird.

> **Merksatz:** Bit 0x40 ist die Eintrittskarte des Pickup-Triggers. Wandert ein
> Item, muss das Bit mitwandern.

**Warum das schwer zu sehen ist:** Jedes Werkzeug — trview, unser JSON-Export,
`DumpItems` — zeigt die OCB auf `0x3F` maskiert an. Andrea1 Item 107 steht
überall als „OCB 0" da; roh ist es `0x0040`. Wer nur auf die angezeigte OCB
schaut, hält das Bit für nicht vorhanden.

**Gegenprobe über alle 13 Level:** Alle sieben Items, die über einen
Pickup-Trigger ortsgebunden sind, tragen das Bit — und nur sie:

| Level | Item | roh |
|---|---|---|
| Andrea1 | 107, 94 | `0x0040` |
| Andrea3 | 68 | `0x0042` (OCB 2 + Bit) |
| andy1 | 8 | `0x0045` (OCB 5 + Bit) |
| Andy2 | 29 | `0x0040` |
| rich1 | 14 | `0x0048` (OCB 8 + Bit) |
| rich3 | 75 | `0x0044` (OCB 4 + Bit) |

Das Bit ist damit zugleich ein **Erkennungsmerkmal**: ein Pickup mit 0x40 hat
einen Pickup-Trigger, der etwas bewirkt.

Bit 0x80 daneben markiert ein regenerierendes Pickup (`game/init.cpp:492`) und
zählt für `& 0xC0` genauso — deshalb müssen beim Verschieben **beide** Bits
erhalten bleiben, nicht nur eines.

## 4b-ter. OneShot ist kein Schalter, sondern eine Verhaltensänderung

Bit 8 des Setup-Worts heißt „OneShot", tut aber mehr, als den Trigger einmalig
zu machen. `_TestTriggers` gibt es an das geweckte Item weiter
(`control.cpp:2415`):

```c
if (flags & IFL_INVISIBLE)
    item->flags |= IFL_INVISIBLE;
```

Und `IFL_INVISIBLE` (0x100) ist laut `types.h:243` ausdrücklich *„also used as
oneshot for everything else"* — dasselbe Bit trägt beide Bedeutungen. Manche
Gegner fragen es ab und verhalten sich dann anders.

**Larson (und Pierre) sind der Fall, an dem das auffiel.** `pierre.cpp:68`:

```c
if (item->hit_points <= 40 && !(item->flags & IFL_INVISIBLE))
{
    item->hit_points = 40;
    pierre->flags++;      // -> :133 setzt sie zurueck auf 60 und schickt ihn weg
}
```

Ohne das Bit ist Larson **gar nicht tötbar** — die Trefferpunkte werden bei 40
geklemmt und sofort wieder auf 60 gesetzt. Danach flieht er, und außerhalb von
Trajan's Markets löscht ihn `pierre.cpp:341` beim Erreichen seines Ziels.

> **Merksatz:** Wer einen Gegner neu ins Level setzt, gibt seinem Trigger
> OneShot. Bei den meisten ändert es nichts, bei Larson entscheidet es darüber,
> ob er überhaupt ein Gegner ist.

Im Spiel geprüft (Andrea1, Larson in Raum 2, OneShot-Trigger drei Sektoren
davor): er bleibt, verfolgt Lara über Raumgrenzen, geht auf 0 Trefferpunkte.

### Was Larson sonst noch verlangt

- **OCB 0.** Bei OCB != 0 setzt `pierre.cpp:84` `ai_bits = GUARD`, und dann
  sucht er AI-Objekte, die an einer neuen Stelle nicht stehen.
- **Sein Tod feuert einen Heavy-Trigger an seiner Startposition.**
  `pierre.cpp:118`, die Position kommt aus `draw_room`/`item_flags[2]`, die
  `InitialiseCreature` (`box.cpp:30`) aus der Platzierung packt. Landet er auf
  einem Sektor mit Heavy-Trigger, löst er den beim Sterben aus.
- **Andrea2s Larson (Item 76) darf nie verschoben werden.** Die Todesprobe folgt
  seiner Platzierung, der Cutscene-Trigger liegt aber fest auf `72,2,2` — sonst
  fällt die Todes-Cutscene aus.
- **Seine Leiche blieb stehen.** Seine Sterbeanimation (Slot 45, relativ 15,
  Frames 281..488) trägt sieben `PlaySound`-Befehle und keinen Anim-Befehl 4
  (`KillItem`) — der Hund hat ihn, Larson nicht, weil seine Leiche in Vanilla
  nur die Andrea2-Cutscene wegräumt. Behoben in unserer Engine: `pierre.cpp`
  setzt am letzten Frame `after_death = 1`, den Rest macht `draw.cpp:742`.
  Steht in `MODIFICATIONS.md`.

## 4c. Was die OCB-Werte wirklich verlangen

Vollständig aus `PickUpCollision` (`game/pickup.cpp:728`) abgelesen:

| OCB | Verhalten | verschiebbar? |
|---|---|---|
| 0 | gewöhnliches Pickup | ja |
| 1 | verstecktes Pickup, `ANIM_HIDDENPICKUP` | nein, braucht Sonderumgebung |
| **2** | **Brechstange**, `CrowbarPickUpBounds` | erst mit Wandfilter |
| 3, 4, 7, 8 | Sockel, `FindPlinth()` — ohne Sockel **stumm unaufnehmbar** | nein |
| 5, 10 | nie aufnehmbar (Rücksprung ganz oben) | – |
| **6** | fällt in den `default`-Zweig, also **gewöhnliches Pickup**, nur animiert | **ja** |
| **9** | „JobyCrow", `JobyCrowPickUpBounds`, eigene Animation | wie 2 |
| 11 | Münzglitzern | ja |

**OCB 6 ist harmlos:** es steht in keinem `case` der Auswahl und landet im
Standardzweig; es taucht nur in der Animationsliste (`pickup.cpp:580`) auf.

**Bei rich1s Item 27 ist die OCB aber gar nicht der Grund.** Die CD trägt
vanilla `Invisible=true` und liegt außerhalb der Karte; der Switch in Raum 15
aktiviert sie zusammen mit Door 4 und Door 2, und im Spiel fällt sie aus dem
Anzug, sobald dessen Animation läuft. Ein Ortswechsel zerstört die Enthüllung.
Sie steht deshalb **dauerhaft** in `FIXED_ITEMS['rich1']` *und* in
`VANILLA_TYPES['rich1']` (ZoneAnalyse.ps1) — **nicht** „vorerst vanilla", sondern
nie im Pool. Der Wandfilter ändert daran nichts.

Merksatz daraus: eine Sonder-OCB ist ein *Hinweis* darauf, dass ein Item
besonders ist, aber nicht der Beweis — und ihr Fehlen ist kein Freibrief.

## 4d. Wandfilter für OCB 2 und 9

Beide Zweige setzen `item->pos.y_rot = roty` zurück — den **eigenen** Winkel des
Items — bevor `TestLaraPosition` prüft (`pickup.cpp:783` bzw. `:872`). Der
Winkel bestimmt also, aus welcher Richtung Lara herankommen muss. Frei
schwebend ist es aufnehmbar, sieht aber falsch aus.

Zum Freigeben braucht es **drei Dinge, von denen zwei noch fehlen**:

1. Einen **Sektor mit Wandnachbar**. Datenlage: vorhanden — der Sektor-Export
   führt `isWall` je Sektor, die vier Nachbarn im selben Raum sind prüfbar.
2. Den **Winkel passend zur Wand** setzen. **Fehlt:** der Level-Export liefert
   heute gar keinen Item-Winkel.
3. Den Winkel **schreiben** zu können. **Fehlt:** das Patch-Format kennt kein
   `angle`; ein verschobenes Item behält bisher bewusst seine Ausrichtung.

## 4e. Der eine gekoppelte Zug: Andrea1 Key 107

Ausgangslage, mit korrekt gelesenem Referenzwort:

```
192,1,2  [3:0003 0200][4:8404 art=4 timer=0 once=0 maske=31 ref=107 c0:26 c0:108]
```

Der Sektor trägt zwei Einträge: einen ROOF-Eintrag und den Pickup-Trigger. Der
Trigger verweist auf Item 107 (Key, Typ 196, OCB 0) und weckt zusätzlich
Objekt 26 (Hund, Typ 41) und Objekt 108 (Flame Emitter2, Typ 122).

**Warum das Verschieben das Verhalten erhält:** `PickupTrigger`
(`pickup.cpp:152`) prüft ausschließlich den **Zustand des Items** —
`ITEM_INVISIBLE`, `item_flags[3]`, `trigger_flags & 128` — und ruft dann
`KillItem`. Nichts daran ist ortsgebunden. Der Trigger feuert, sobald Lara auf
seinem Sektor steht und das Item gerade eingesammelt wurde. Da Lara zum
Einsammeln ohnehin auf dem Item-Sektor steht, ist „Item und Trigger gemeinsam
versetzen" verhaltensgleich mit vanilla. Das Referenzwort bleibt gültig, weil
der Trigger sein Item über den **Index** anspricht, nicht über den Ort.

**Nachgewiesen** (`FloorTool move`, Ziel 59,1,1 als Probe):

| Prüfung | Ergebnis |
|---|---|
| Sektoren geändert | 2 von 6787 |
| Trigger am Ziel | wortgleich, `ref=107 c0:26 c0:108` |
| Quellsektor | behält den ROOF-Eintrag, `0003` → `8003` |
| Items | 151 vs. 151, kein Unterschied |
| Dateigröße | +14 Bytes (7 Shorts) |

Die Zeile `0003` → `8003` ist der Punkt, an dem so ein Umbau kippt: der
verbliebene Eintrag ist jetzt der **letzte** der Kette und braucht Bit 15. Wer
die Kette wortweise kopiert statt sie in Einträge zu zerlegen, vergisst das.

**Zielsektor-Auflage:** Es gilt die Regel aus „Wohin in der Kette ein Trigger
darf". Am einfachsten und ohne Ausnahme sicher: nur Sektoren **ohne jede
Floordata** zulassen. In Andrea1 sind das 678 der 971 begehbaren Sektoren —
für einen einzelnen Schlüssel mehr als genug.

**Nebenwirkung, die bleibt:** Der Flame Emitter2 zündet weiterhin an seinem
festen Ort, nur eben ausgelöst an der neuen Stelle. Das ist bewusst in Kauf
genommen, weil der Trigger als Ganzes mitwandert.

## 4f. Neue Gegner: die strengste Variante reicht

Für den eigentlichen Zweck — dem Level Gegner **hinzufügen** — wird gar keine
vorhandene Kette angefasst. Neue Gegnertrigger kommen ausschließlich auf
Sektoren mit `index == 0`: eine frische Kette hinten anhängen, Sektor umhängen,
fertig.

Das macht die Prüfregel aus Abschnitt 5 trivial: **jeder Sektor mit
`index != 0` muss byteidentisch bleiben.** Kein Zerlegen, kein Neuvergeben von
Endebits, kein Referenzwort im Spiel.

Platz ist reichlich vorhanden — begehbare Sektoren ganz ohne Floordata:

| Level | begehbar | davon ohne Floordata |
|---|---|---|
| Andrea1 | 971 | 678 |
| Andrea2 | 1031 | 730 |
| Andrea3 | 671 | 298 |
| andy1 | 600 | 178 |
| Andy2 | 652 | 401 |
| andy3 | 822 | 198 |
| joby2 | 1718 | 1335 |
| joby3 | 307 | 226 |
| joby4 | 1210 | 461 |
| joby5 | 257 | 190 |
| rich1 | 405 | 215 |
| rich2 | 592 | 325 |
| rich3 | 808 | 408 |
| **Summe** | **10044** | **5643** |

Selbst joby5 als Schlusslicht hat 190 freie Plätze. Die Einschränkung kostet
also nichts.

**Platzierung** (Nutzervorgabe): vom Gegner aus den Flutpfad rückwärts etwa
3 Sektoren. Das garantiert von der Konstruktion her Erreichbarkeit, die richtige
Zone und dass Lara den Trigger überquert — und hält den nötigen Abstand ein:
liegt der Trigger auf dem Gegnersektor, spawnt der Gegner in Lara hinein.

## 4g. Wie ein neuer Gegner ins Level kommt

Der Weg steht seit 13.08.2026 durchgehend. Vier Stationen:

**1. Kuration** — `$ENEMY_TYPES` in `ZoneAnalyse.ps1`, je Level eine Liste von
typeIds mit Begründung. Bewusst leer, bis ein Gegner im Spiel nachgewiesen hat,
dass er ohne Levelskript auskommt. Landet als `enemyTypes` im Zonen-Export.

**2. Anmarschsektoren** — ebenfalls `ZoneAnalyse.ps1`. Zu jedem itemfähigen
Sektor werden bis zu drei Sektoren mitgegeben, die Lara auf ihrem
**Erstzugangsweg** kurz davor betritt, etwa drei Schritte, beste Wahl zuerst.

Quelle ist `$firstParent` — der Baum, der festhält, über welche Kante ein Sektor
zum **ersten Mal** erreicht wurde. Das ist der Weg, den Lara wirklich nimmt, und
nicht irgendein Weg durch das fertige Level mit allen offenen Türen. Damit ist
der Anmarschsektor nicht geschätzt, sondern hergeleitet.

Gemessen: 889 von 961 Sektoren in Andrea1 haben einen, über alle Level
vergleichbar (joby4 ist mit 260 von 394 das Schlusslicht).

**3. Platzierung** — `Rando::run`. Ein Gegner darf auf einen freien Sektor,
dessen Anmarschsektor ebenfalls frei ist **und** einen Trigger verträgt
(`Patch::markTriggerSafeSectors` liest das aus der echten Floordata, weil der
Level-Info-Export Lava und Climb kennt, Monkey und TrigTrigger aber nicht).
Der Gegner kommt als `itemAdds`, sein Trigger als `triggerAdds`.

**4. Eintragen** — `Tr5Level::addTrigger`, rein additiv.

**Die Regeln, die dabei eingehalten werden:**

| Regel | Warum |
|---|---|
| Trigger nie auf dem Gegnersektor | sonst spawnt der Gegner in Lara hinein |
| Trigger auf dem Anmarschweg | sonst wacht er nie auf |
| OneShot gesetzt | siehe 4b-ter — bei Larson entscheidet es über Leben und Tod |
| OCB 0 | sonst `ai_bits = GUARD` und er sucht AI-Objekte, die dort nicht stehen |
| höchstens 256 Items | `MAX_ITEMS`; rich2 liegt mit 248 dicht darunter |

**Nachgemessen** (Andrea1, Seed 4711, 6 Gegner): 6 von 6 haben ihren Trigger,
alle auf einem anderen Sektor als der Gegner. Floordata: 11 Zeilen betroffen —
3 Sektoren hatten schon eine Kette (eine Neigung, ein ROOF, der Key-Quellsektor)
und wurden umgebaut, 4 kamen neu dazu. Items 151 → 157, Datei +202 Bytes.

## 5. Prüfregel: die Indizes der anderen müssen stehen bleiben

Vorgabe des Nutzers, und die eigentliche Gefahr an der ganzen Sache:
**verschiebt sich die Liste, ist alles kaputt** — und zwar lautlos, weil ein
falscher Index einfach eine andere Kette liest.

Daraus folgt eine Prüfung, die nach **jeder** Floordata-Änderung laufen muss:

1. Vorher für **jeden** Sektor jedes Raums Index und dekodierte Kette merken.
2. Nachher dasselbe.
3. Vergleichen: **jeder Sektor, den wir nicht bewusst umgehängt haben, muss
   denselben Index und dieselbe Kette haben.** Ein einziger abweichender
   Eintrag ist ein Abbruchgrund, keine Warnung.

Das ist dieselbe Disziplin, die bei den Item-Indizes gilt, und dieselbe, mit
der heute die Waffenimporte abgesichert wurden: nicht auf das Ergebnis
schauen, sondern auf das, was sich **nicht** verändert haben darf.

Ein Werkzeug dafür fehlt noch — es ist das erste, was gebaut werden sollte,
**vor** dem ersten Schreibversuch.
