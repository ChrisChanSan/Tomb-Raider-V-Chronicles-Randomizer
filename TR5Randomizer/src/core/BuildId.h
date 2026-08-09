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
#define RANDO_BUILD_ID      2
#define RANDO_BUILD_STRING  "0.2"

// Name der Datei, die der Randomizer neben die Level legt und die Engine beim
// Start liest, um Seed und Build im Pausenmenue anzuzeigen. Damit ist ein
// Fehlerbericht ein einzelner Screenshot statt einer Rueckfragerunde.
#define RANDO_SIDECAR_NAME  L"randomizer.txt"
