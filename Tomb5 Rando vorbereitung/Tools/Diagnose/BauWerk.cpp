// Applies a patch JSON to a level - for hand-built test levels.
// Usage: BauWerk <source.trc> <patch.json> <target.trc> [donorFolder]
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
    if (argc < 4) { std::printf("usage: BauWerk <source.trc> <patch.json> <target.trc> [donorFolder] [-moreitems]\n"); return 1; }
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
    bool moreItems = false;
    std::wstring folder = W(".");
    for (int i = 4; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "-moreitems")) moreItems = true;
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
    std::printf("done: %d edited, %d added, %d triggers, %d slots, %d sounds -> %d items\n",
                rep.edited, rep.added, rep.triggersAdded, rep.moveablesCopied, rep.soundsCopied, lvl.itemCount());
    return 0;
}
