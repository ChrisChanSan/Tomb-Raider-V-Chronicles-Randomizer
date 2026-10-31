// Applies a patch JSON to a level - for hand-built test levels.
// Usage: LevelBuild <source.trc> <patch.json> <target.trc> [donorFolder]
//        [-moreitems] [-hdm]
// -hdm also writes the run's .hdm sidecar beside the target, as the
// randomizer does - a copied slot's `donor` line and a shrunk copy's `scale`
// line, so the engine and ModelProbe -hdm read the test level as a run's.
#include "core/Tr5Level.h"
#include "core/ApplyPatch.h"
#include "core/Json.h"
#include <cstdio>
#include <string>

static std::wstring g_folder;

static std::wstring donor(const std::string& name, void*)
{
    std::wstring w;
    for (char c : name) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return g_folder + L"\\" + w + L".trc";
}

int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("usage: LevelBuild <source.trc> <patch.json> <target.trc> [donorFolder] [-moreitems] [-hdm]\n"); return 1; }
    auto W = [](const char* p){ std::string s(p); return std::wstring(s.begin(), s.end()); };

    std::string err;
    JsonValue patch;
    if (!Json::parseFile(W(argv[2]), patch, err)) { std::printf("patch read: %s\n", err.c_str()); return 1; }

    Tr5Level lvl;
    if (!lvl.load(W(argv[1]), err)) { std::printf("level load: %s\n", err.c_str()); return 1; }

    // "-moreitems": lift the MAX_ITEMS bound. ONLY for test levels that are
    // viewed in trview and never loaded (wall test, key item range). The
    // result is deliberately no playable level anymore - the engine aborts
    // on load. See allowMoreThanMaxItems in Tr5Level.h.
    bool moreItems = false, hdm = false;
    std::wstring folder = W(".");
    for (int i = 4; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "-moreitems")) moreItems = true;
        else if (!std::strcmp(argv[i], "-hdm")) hdm = true;
        else folder = W(argv[i]);
    }
    if (moreItems)
    {
        lvl.allowMoreThanMaxItems(true);
        std::printf("note: MAX_ITEMS bound off - the result is for trview only.\n");
    }
    g_folder = folder;
    PatchReport rep;
    if (!Patch::apply(lvl, patch, rep, err, donor)) { std::printf("patch: %s\n", err.c_str()); return 1; }
    for (const std::string& w : rep.warnings) std::printf("  note: %s\n", w.c_str());

    if (!lvl.save(W(argv[3]), err)) { std::printf("save: %s\n", err.c_str()); return 1; }
    if (hdm)
    {
        int rooms = 0, donors = 0, cuts = 0;
        if (!Patch::writeHdMarkers(lvl, W(argv[3]), err, &rooms, &donors, &cuts))
        { std::printf("sidecar: %s\n", err.c_str()); return 1; }
        std::printf("sidecar: %d rooms, %d donors, %d cuts, %d scale lines\n",
                    rooms, donors, cuts, static_cast<int>(lvl.hdScales().size()));
    }
    std::printf("done: %d edited, %d added, %d triggers, %d slots, %d sounds -> %d items\n",
                rep.edited, rep.added, rep.triggersAdded, rep.moveablesCopied, rep.soundsCopied, lvl.itemCount());
    return 0;
}
