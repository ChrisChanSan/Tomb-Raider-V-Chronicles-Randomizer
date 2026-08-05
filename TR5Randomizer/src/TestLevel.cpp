// Prueft den eigenen TR5-Leser gegen die echten Vanilla-Level.
// Aufruf: TestLevel.exe <data-Ordner mit den .trc>
#include "core/Tr5Level.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("Aufruf: TestLevel.exe <data-Ordner>\n"); return 2; }

    std::wstring dir;
    for (const char* p = argv[1]; *p; ++p) dir.push_back((wchar_t)(unsigned char)*p);
    if (!dir.empty() && dir.back() != L'\\') dir.push_back(L'\\');

    const char* names[] = {
        "Andrea1","Andrea2","Andrea3","andy1","Andy2","andy3","del",
        "joby2","joby3","joby4","joby5","rich1","rich2","rich3","richcut2","Title"
    };

    int ok = 0, fail = 0;
    for (const char* n : names)
    {
        std::wstring path = dir;
        for (const char* p = n; *p; ++p) path.push_back((wchar_t)(unsigned char)*p);
        path += L".trc";

        Tr5Level lvl;
        std::string err;
        if (!lvl.load(path, err))
        {
            std::printf("  [FEHL] %-10s %s\n", n, err.c_str());
            ++fail;
            continue;
        }

        // Der Hauptblock sollte praktisch die gesamte Restdatei ausmachen -
        // dahinter kommt nichts Grosses mehr. Weicht das stark ab, haben wir
        // den Kopfteil falsch gedeutet.
        Tr5Item first{};
        lvl.getItem(0, first);
        std::printf("  [ok ] %-10s Raeume %3d  Items %4d @%8zu   erstes: Typ %3d Raum %3d (%d,%d,%d)\n",
                    n, lvl.roomCount(), lvl.itemCount(), lvl.itemCountOffset(),
                    first.objectNumber, first.roomNumber, first.x, first.y, first.z);
        ++ok;
    }

    std::printf("\n%d gelesen, %d fehlgeschlagen\n", ok, fail);
    return fail ? 1 : 0;
}
