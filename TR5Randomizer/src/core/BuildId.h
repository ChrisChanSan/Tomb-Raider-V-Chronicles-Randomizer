#pragma once

//----------------------------------------------------------------------------
// Gemeinsame Kennung von Randomizer, Engine und Kurationsdaten.
//
// Warum das ueberhaupt existiert: ein Seed allein reproduziert gar nichts. Ob
// aus Seed X dasselbe Level entsteht, haengt an drei Dingen - der
// Platzierungslogik im Randomizer, den mitgelieferten Zonen-/Kurationsdaten und
// der Engine, die das Ergebnis spielt. Meldet jemand "Seed K7M... ist
// unspielbar", ist das ohne diese drei Angaben nicht nachstellbar. Genau dafuer
// war die Seed-Funktion aber gedacht.
//
// Regel: RANDO_BUILD_ID wird bei JEDER Aenderung erhoeht, die das Ergebnis
// eines Seeds veraendern kann. Das ist mehr, als man zuerst denkt - dazu
// gehoeren auch die Kurationstabellen in ZoneAnalyse.ps1, denn eine neue
// Sperrliste verschiebt Items. Reine Oberflaechenaenderungen dagegen nicht.
//
// Die Engine traegt dieselbe Konstante. Stimmen sie nicht ueberein, verweigert
// der Randomizer die Arbeit, statt einen Seed zu erzeugen, dessen Ergebnis
// niemand nachvollziehen kann. Beim Erhoehen hier also immer auch die Engine
// anfassen - die beiden Projekte liegen in getrennten Baeumen, deshalb laesst
// sich das nicht durch einen gemeinsamen Header erzwingen.
//----------------------------------------------------------------------------

// 2: Die Kurationsdaten setzen jetzt durch, was sie schon lange beschreiben -
//    Inselgruppen, Hoehengrenzen, gesperrte Sektoren, waffenfreie Raeume und
//    "hinter der eigenen Tuer". Der Randomizer hat diese Felder vorher gelesen
//    und ignoriert, jeder Seed faellt damit anders aus als unter 1.
// 3: Secrets (typeId 223, spielweit 36 Stueck) bleiben vorerst ortsfest, und
//    Andy2 ist kuratiert. Beides sind Kurationsdaten, und die verschieben
//    Platzierungen genauso wie eine Codeaenderung.
// 4: andy3, rich1 und rich2 kuratiert - und MANUAL_LINK repariert. Die Tabelle
//    hat nie funktioniert (PowerShell zog die Paare flach), joby2 und rich1
//    liefen also die ganze Zeit ohne ihre Handkanten. Zonen und Ablagen der
//    zwoelf anderen Level bleiben davon zwar unberuehrt, rich2 aber nicht,
//    und die Zonennummern selbst sind Eingabe jeder Platzierung.
// 5: rich2 hat jetzt Constraints - vorher hatte es null. Puzzle Item3 bekommt
//    seine Zonengrenze, Pickup Item2 und je eine Flasche und ein Tuch muessen
//    in den Startbereich, die Aufzugssektion traegt keine Key-Items mehr.
// 6: rich2s erste Sperrliste aus dem trview-Durchgang, und Puzzle Item3 bekommt
//    eine Untergrenze - es darf erst ab Raum 128 und nicht mehr hinter dem
//    Teleporter liegen. 214 -> 145 erlaubte Ablagen fuer diesen Typ.
// 7: Pflicht-Injektionen koennen einen festen Sektor tragen und sich dort
//    stapeln. rich2 bekommt damit HK plus zwei Magazine auf 140(1,2).
// 8: Injektionen koennen einen Hoehenversatz tragen, und rich2 grenzt Flasche
//    und Tuch enger ein als den uebrigen Startbereich.
// 9: TYPE_Y_OFFSET - Typen, deren Mesh-Ursprung nicht auf der Unterkante sitzt,
//    werden bei JEDER Platzierung angehoben. Bisher nur die HK (345).
// 10: Aus TYPE_Y_OFFSET wird TYPE_OFFSET mit dx/dy/dz und einer Levelebene.
//     Neu: Andrea2s Puzzle Item3 rueckt um dz -512 in die Sektormitte.
// 11: Andrea2s Puzzle Item3 bleibt vanilla - mit genullter OCB ist es nicht
//     aufnehmbar, mit erhaltener braucht es eine Wand dahinter. KEEP_OCB ist
//     als Mechanismus gebaut, aber leer.
#define RANDO_BUILD_ID      11
#define RANDO_BUILD_STRING  "0.11"

// Name der Datei, die der Randomizer neben die Level legt und die Engine beim
// Start liest, um Seed und Build im Pausenmenue anzuzeigen. Damit ist ein
// Fehlerbericht ein einzelner Screenshot statt einer Rueckfragerunde.
#define RANDO_SIDECAR_NAME  L"randomizer.txt"
