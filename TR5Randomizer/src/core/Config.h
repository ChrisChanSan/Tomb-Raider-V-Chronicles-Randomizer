#pragma once
#include <string>

//----------------------------------------------------------------------------
// Einstellungen, die zwischen zwei Starts erhalten bleiben.
//
// Bewusst eine Textdatei neben der EXE und keine Registry: das Werkzeug soll
// tragbar sein - Ordner kopieren, laeuft. Ausserdem kann der Nutzer
// hineinsehen und notfalls von Hand korrigieren, was bei einem Pfadproblem
// deutlich freundlicher ist als ein Registrierungseditor.
//----------------------------------------------------------------------------
struct Config
{
    std::wstring sourceDir;   // data-Ordner der eigenen Chronicles-Installation
    std::string  seedText;    // zuletzt benutzter Seed, als Text

    // Liest bzw. schreibt <exe-Ordner>\TR5Randomizer.cfg. Fehlt die Datei,
    // bleibt alles leer - das ist kein Fehler, sondern der erste Start.
    void load();
    bool save() const;

    static std::wstring path();
};
