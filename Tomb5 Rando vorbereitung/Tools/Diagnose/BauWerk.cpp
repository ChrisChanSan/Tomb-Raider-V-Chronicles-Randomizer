// Wendet eine Patch-JSON auf ein Level an - fuer handgebaute Testlevel.
// Aufruf: PatchAn <quelle.trc> <patch.json> <ziel.trc> [spenderordner]
#include "core/Tr5Level.h"
#include "core/ApplyPatch.h"
#include "core/Json.h"
#include <cstdio>
#include <string>

static std::wstring g_ordner;

static std::wstring spender(const std::string& name, void*)
{
    std::wstring w;
    for (char c : name) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return g_ordner + L"\\" + w + L".trc";
}

int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("Aufruf: BauWerk <quelle.trc> <patch.json> <ziel.trc> [spenderordner] [-mehritems]\n"); return 1; }
    auto W = [](const char* p){ std::string s(p); return std::wstring(s.begin(), s.end()); };

    std::string err;
    JsonValue patch;
    if (!Json::parseFile(W(argv[2]), patch, err)) { std::printf("Patch lesen: %s\n", err.c_str()); return 1; }

    Tr5Level lvl;
    if (!lvl.load(W(argv[1]), err)) { std::printf("Level laden: %s\n", err.c_str()); return 1; }

    // "-mehritems": die MAX_ITEMS-Schranke aufheben. NUR fuer Testlevel, die
    // in trview angesehen und nie geladen werden (Wandtest, Key-Item-Range).
    // Das Ergebnis ist bewusst kein spielbares Level mehr - die Engine bricht
    // beim Laden ab. Siehe allowMoreThanMaxItems in Tr5Level.h.
    bool mehrItems = false;
    std::wstring ordner = W(".");
    for (int i = 4; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "-mehritems")) mehrItems = true;
        else ordner = W(argv[i]);
    }
    if (mehrItems)
    {
        lvl.allowMoreThanMaxItems(true);
        std::printf("Hinweis: MAX_ITEMS-Schranke aus - Ergebnis ist nur fuer trview.\n");
    }
    g_ordner = ordner;
    PatchReport rep;
    if (!Patch::apply(lvl, patch, rep, err, spender)) { std::printf("Patch: %s\n", err.c_str()); return 1; }
    for (const std::string& w : rep.warnings) std::printf("  Hinweis: %s\n", w.c_str());

    if (!lvl.save(W(argv[3]), err)) { std::printf("Speichern: %s\n", err.c_str()); return 1; }
    std::printf("Fertig: %d geaendert, %d hinzugefuegt, %d Trigger, %d Slots, %d Sounds -> %d Items\n",
                rep.edited, rep.added, rep.triggersAdded, rep.moveablesCopied, rep.soundsCopied, lvl.itemCount());
    return 0;
}
