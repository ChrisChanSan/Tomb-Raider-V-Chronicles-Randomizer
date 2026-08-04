#pragma once
#include <cstdint>
#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Verwaltung der Spieldaten.
//
// Grundentscheidung (Nutzer, 2026-08-05): der Randomizer schreibt NICHT in den
// Steam-Ordner. Er fragt einmal nach dessen Pfad, prueft die Level gegen die
// mitgelieferte Pruefsummenliste und kopiert sie in einen eigenen Spielordner
// neben die Engine. Gruende:
//  - Steams "Dateien ueberprüfen" spielt geaenderte Dateien kommentarlos
//    zurueck; eine randomisierte Partie waere ohne Vorwarnung wieder Vanilla.
//  - Ein Spiel-Update kollidiert mit unseren Aenderungen.
//  - Ein Werkzeug, das in einen gekauften Ordner schreibt, macht Nutzer zu
//    Recht nervoes.
//  - "Restore to default" wird dadurch trivial: der Steam-Ordner IST das
//    Original, zurueck heisst neu kopieren.
//
// Die Pruefsummenliste (VanillaLevelChecksums.csv) existiert bereits aus der
// Vorbereitungsphase und bekommt hier ihren eigentlichen Zweck: sicherstellen,
// dass als Quelle wirklich unveraenderte Vanilla-Level dienen. Ein bereits
// randomisiertes oder anderweitig modifiziertes Level ein zweites Mal zu
// randomisieren erzeugt sonst Ergebnisse, die niemand mehr nachvollziehen kann
// - und genau das soll die Seed-Funktion ja verhindern.
//----------------------------------------------------------------------------

struct LevelFile
{
    std::string  name;        // "Andrea1.trc"
    uint64_t     size = 0;
    std::string  sha256;      // Grossbuchstaben, wie in der CSV
};

enum class LevelState
{
    Missing,        // Datei ist im Quellordner nicht vorhanden
    Vanilla,        // Groesse und Pruefsumme stimmen mit der Liste ueberein
    Modified,       // vorhanden, aber weicht ab - nicht als Quelle geeignet
    Unknown         // kein Eintrag in der Pruefsummenliste (z.B. fremdes Level)
};

struct LevelStatus
{
    std::string name;
    LevelState  state = LevelState::Missing;
    uint64_t    actualSize = 0;
    std::string actualSha256;
};

class DataFolder
{
public:
    // Laedt die mitgelieferte Pruefsummenliste. Ohne sie kann nichts geprueft
    // werden, deshalb harter Fehler statt stiller Weiterlauf.
    bool loadChecksums(const std::wstring& csvPath, std::string& error);

    size_t knownLevelCount() const { return m_known.size(); }

    // Prueft jede Datei der Liste im angegebenen Quellordner. Der Ordner ist
    // der "data"-Unterordner der Steam-Installation.
    std::vector<LevelStatus> inspect(const std::wstring& sourceDataDir) const;

    // Ist der Ordner als Quelle brauchbar? Verlangt, dass ALLE bekannten Level
    // vorhanden und unveraendert sind. Bewusst streng: ein einzelnes
    // abweichendes Level reicht, um Seeds unvergleichbar zu machen.
    static bool isUsableSource(const std::vector<LevelStatus>& status, std::string& reason);

    // Kopiert alle bekannten Level aus dem Quell- in den Zielordner und legt
    // den Zielordner bei Bedarf an. Das ist zugleich "Restore to default" -
    // beides ist dieselbe Operation, deshalb gibt es nur eine Funktion.
    bool copyLevels(const std::wstring& sourceDataDir,
                    const std::wstring& targetDataDir,
                    std::string& error,
                    int* copiedCount = nullptr) const;

    const std::vector<LevelFile>& knownLevels() const { return m_known; }

private:
    std::vector<LevelFile> m_known;
};

namespace FileHash
{
    // SHA-256 ueber die Windows-eigene CNG-Schnittstelle (bcrypt). Bewusst
    // keine handgeschriebene Krypto: die Systemvariante ist geprueft, kostet
    // keine zusaetzliche Abhaengigkeit und ist deutlich schneller.
    bool sha256File(const std::wstring& path, std::string& hexUpper, uint64_t& size, std::string& error);
}

namespace PathUtil
{
    std::wstring join(const std::wstring& a, const std::wstring& b);
    bool         fileExists(const std::wstring& path);
    bool         dirExists(const std::wstring& path);
    bool         ensureDir(const std::wstring& path);          // legt auch Zwischenebenen an
    std::wstring exeDir();                                     // Ordner der laufenden EXE
    std::wstring widen(const std::string& s);                  // UTF-8 -> UTF-16
    std::string  narrow(const std::wstring& s);                // UTF-16 -> UTF-8
}
