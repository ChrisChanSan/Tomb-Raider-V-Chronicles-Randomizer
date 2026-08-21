# Laras Interaktions-Animationen: welches Level trägt welche wirklich

Vermessen am 2026-08-21 mit `LaraAnim` über alle 13 spielbaren Level.
Anlass: die Kartenzieh-Animation stand in joby3 22 Sekunden eingefroren,
ein Schalter in andy3 spielte eine viel zu kurze Bewegung, und rich1s
Kriechrolle bewegte gar nichts.

## Das Grundproblem

Laras Anim-Tabelle ist in **jedem** Level gleich lang — der Platz für eine
Animation existiert also überall. Die **Bilddaten** sind aber levelspezifisch.
Wo ein Level eine Bewegung nie braucht, lässt Core den Eintrag als Stub
stehen.

Zwei Stub-Formen, beide messbar:

| Form | Kennzeichen | Wirkung im Spiel |
|---|---|---|
| 1 | `FrameSize 0` | Der Motor rechnet `frame_ptr[n * 0]` → immer dasselbe Bild. Eingefrorene Pose. |
| 2 | `frame_base == frame_end` | Leerer Bildbereich, nichts zu spielen. |

Dazu ein dritter Fall, der kein Stub ist, aber genauso schiefgeht: die
Framegröße stimmt, doch zwischen dem eigenen Frame-Zeiger und dem
nächstgrößeren liegt **weniger Platz, als die Keyframes brauchen** — dann
gehören die Daten dort einer anderen Animation.

`LaraAnim` prüft alle drei:

```
Keyframes = (frame_end - frame_base) / rate + 1
gebraucht = Keyframes * FrameSize * 2 Byte
vorhanden = nächstgrößerer Frame-Zeiger irgendeiner Anim - eigener
```

Mehrere Anims dürfen sich **einen** Zeiger teilen, wenn sie dieselbe
`frame_base` haben — dann sind es Varianten desselben Bewegungsblocks
(andy3: 468/469/470 starten alle auf Frame 8190). Das ist normal.

Ein geteilter Zeiger heißt aber **nicht automatisch „alles in Ordnung"**.
Er kann auch bedeuten, dass ein echter Besitzer daliegt und die anderen
mit `FrameSize 0` nur daraufzeigen — genau rich1s Fall (218–220 zeigten auf
221s Block). Auseinanderzuhalten sind die beiden Fälle nur über die
Framegröße, nicht über den Zeiger.

## Ergebnis: kein einziges Level trägt alle

Reichste Spender: **joby4** und **andy1**. Ärmste: **rich1, rich2, rich3** —
ausgerechnet die Outfit-Spender.

Auszug der Spendertabelle (welche Level tragen die Anim echt):

| Anim | Name | Level mit echten Daten |
|---|---|---|
| 63/64 | SWITCHOFF / SWITCHON | 8 bzw. 7 |
| 131 | USEKEY | 11 |
| 134 | USEPUZZLE | **13 (alle)** |
| 135 | PICKUP | **13 (alle)** |
| 218 | DUCKROLL (Kriechrolle) | **12 — rich1 fehlt als einziges** |
| 316 | BLOCKSWITCH | 3 (andy1, Andy2, Andy3) |
| 319/320 | TURNSWITCH C/A | **2** (joby4, Andy3) |
| 324 | RAILSWITCH | **1 (nur andy1)** |
| 412 | TWODOOR | 2 (andy1, Andrea1) |
| 420 | CROWSWITCH | **1 (nur andy1)** |
| 421 | CRAWLJUMP | **13 (alle)** |
| 437 | KEYCARD | 4 (joby4, rich1, joby5, joby2) |
| 470 | AIRLOCKSW | 7 |

`WATERSWITCH` (415) und `WATERDOOR` (416) trägt **kein** Level — die sind in
TR5 ungenutzt.

**Vorsicht bei joby4:** es steht in fast jeder Zeile, hat aber selbst Stubs
der Form 2 (`AIRLOCKSW`: FrameSize 10, aber Frames 0..0). Die Prüfung fängt
das ab — eine Spendertabelle, die nur „Slot vorhanden" fragt, tut es nicht.

## Was der Randomizer daraus macht

`ApplyPatch` hat einen Nachlauf (nach allen Objektänderungen):

1. Er leitet aus den Objekten im **fertigen** Level den Anim-Bedarf ab —
   Schalter samt ihrer OCB, Schlüssel- und Puzzlelöcher, Falltüren,
   Schiebeblöcke.
2. Dazu kommen die Bewegungen, die an keinem Objekt hängen, sondern an
   Laras Steuerung und deshalb **immer** liegen müssen: Kriechrolle (218)
   und Kriechsprung (421).
3. Jede davon wird mit `Tr5Level::laraAnimBrauchbar` geprüft und bei Bedarf
   über `kopiereLaraAnimVon` aus dem ersten Spender geholt, der sie
   **wirklich** trägt — geprüft am geladenen Spender, nicht geglaubt.
4. **Der Bedarf wird entlang der Kette aufgezogen**, bevor irgendetwas
   geholt wird — siehe unten. Selbstbezug (222 → 222) beendet den Lauf.

Ein Vanilla-Durchlauf über alle Level repariert damit von sich aus neun
Animationen.

## Animationen sind Ketten — und Core stubbt sie am Stück

Eine Bewegung ist selten eine Animation. `next_animation` (Offset +28 im
40-Byte-Eintrag) hängt die nächste an, und Core lässt beim Stubben die
**ganze Kette** stehen. Die Kriechrolle in rich1:

| Anim | Frame-Zeiger | Rate | weiter mit |
|---|---|---|---|
| 218 | 103896 | 5 | 219 |
| 219 | 103896 | 5 | 220 |
| 220 | 103896 | 5 | 222 |
| 221 | 103896 | 4 | 11 |

Alle vier auf **einem** Zeiger. 221 ist der echte Besitzer (Größe 37,
braucht 518 der 592 Byte), 218–220 sind Stubs mit Größe 0.

Nur 218 zu reparieren reicht deshalb nicht: Lara setzt zum Rollen an und
fällt nach sechs Bildern in die Kriechpose zurück, weil 219 sie wieder auf
das Standbild schickt. Genau so im Spiel gesehen.

Der Nachlauf zieht den Bedarf darum erst über `laraAnimNachfolger` auf,
bevor er etwas holt. Nach dem Lauf:

| Anim | Frame-Zeiger | Rate |
|---|---|---|
| 218 | 410112 | 2 |
| 219 | 410408 | 2 |
| 220 | 411032 | 2 |
| 221 | 103896 | 4 (unangetastet, war nie kaputt) |

Die Klangbefehle kommen mit und werden auf die Zielbasis umgerechnet:
joby4s PlaySound auf Frame 5500 bei Basis 5497 landet in rich1 auf 4278 bei
Basis 4275 — derselbe Abstand.

**Gegenprobe:** `AnimKmd <level.trc> 0` zeigt Frame-Zeiger, Rate,
Nachfolger, Zustandswechsel und alle Befehle je Animation. Wenn zwei
benachbarte Anims denselben `FrameOfs` zeigen, sofort mit
`LaraAnim -anim <nr>` die Größe nachsehen.

## Die OCB wählt Laras Bewegung

Bei Schaltern hängt die Animation an der OCB (`switch.cpp:246ff`):

| OCB | Laras Animation |
|---|---|
| 0 | SWITCHON (64) — klassischer Wandhebel |
| 1–2 | HIDDENPICKUP (325) |
| 3 | SMALLSWITCH (413) |
| 4 | GENERATORSW (196) |
| 5–7 | ONEHANDPUSHSW (197) |
| ab 8 | AIRLOCKSW (470) |

Ein Nachbau mit der falschen OCB spielt die falsche Bewegung. Ist sie zu
kurz, ist Lara fertig, bevor das **Objekt** es ist — sie verlässt den Sektor
und **der Trigger greift nie**. Genau das passierte in andy3, wo der
Vanilla-Schalter OCB 8 trägt.

## Aufruf

```
LaraAnim <level.trc> [level.trc ...]      Übersicht: was fehlt
LaraAnim -alle <level.trc>                jede Anim einzeln
LaraAnim -anim <nr> <level.trc> [...]     eine Anim im Levelvergleich
```

`AnimProbe <level.trc> <animNr>` fragt stattdessen die Bibliothek
(`Tr5Level::laraAnimBrauchbar`) — die Gegenprobe, wenn Werkzeug und
Randomizer sich uneinig sind.
