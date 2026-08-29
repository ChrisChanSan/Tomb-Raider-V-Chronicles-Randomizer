// BalanceWriteCheck - checks the randomizer's interface to Balance.txt
// (#108): does it really write ONLY the three percentage lines and leave
// everything else in place character for character?
//
// Build (64 bit), from the .bat next to this file:
//   set R=..\..\Tomb5 Native win 11 fix\TR5Randomizer\src
//   cl /nologo /EHsc /std:c++17 /I "%R%" BalanceWriteCheck.cpp
//       "%R%\core\BalanceFile.cpp" "%R%\core\DataFolder.cpp"
//       /Fe:BalanceWriteCheck.exe
#include "core/BalanceFile.h"
#include "core/DataFolder.h"

#include <cstdio>
#include <string>
#include <windows.h>

static int error = 0;

static void check(const char* what, bool ok, const char* extra = "")
{
    if (ok)
        std::printf("    ok    %s %s\n", what, extra);
    else
    {
        std::printf("    ERROR %s %s\n", what, extra);
        error++;
    }
}

static void checkNum(const char* what, int is, int expected)
{
    if (is == expected)
        std::printf("    ok    %-32s = %d\n", what, is);
    else
    {
        std::printf("    ERROR %-32s = %d, expected %d\n", what, is, expected);
        error++;
    }
}

static std::wstring folder;

static void writeFile(const std::string& content)
{
    const std::wstring p = PathUtil::join(folder, L"Balance.txt");
    FILE* f = _wfopen(p.c_str(), L"wb");
    std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
}

static std::string readFile()
{
    const std::wstring p = PathUtil::join(folder, L"Balance.txt");
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f) return std::string();
    std::string s;
    char b[4096];
    size_t n;
    while ((n = std::fread(b, 1, sizeof(b), f)) > 0) s.append(b, n);
    std::fclose(f);
    return s;
}

int main()
{
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    folder = PathUtil::join(tmp, L"BalanceWriteCheck");
    PathUtil::ensureDir(folder);
    DeleteFileW(PathUtil::join(folder, L"Balance.txt").c_str());

    std::printf("--- 1) File missing: a short file with the three lines arises\n");
    {
        BalanceFile::Values w;
        w.weaponDamage = 150; w.enemyHealth = 75; w.enemyDamage = 125;
        std::string err;
        check("write reports success", BalanceFile::write(folder, w, &err), err.c_str());

        const std::string t = readFile();
        check("WeaponDamage is inside", t.find("WeaponDamage") != std::string::npos);
        check("EnemyHealth is inside", t.find("EnemyHealth") != std::string::npos);
        check("EnemyDamage is inside", t.find("EnemyDamage") != std::string::npos);
        check("the header explains the origin", t.find("TR5 Randomizer") != std::string::npos);

        const BalanceFile::Values z = BalanceFile::read(folder);
        checkNum("read weaponDamage", z.weaponDamage, 150);
        checkNum("read enemyHealth", z.enemyHealth, 75);
        checkNum("read enemyDamage", z.enemyDamage, 125);
    }

    std::printf("--- 2) Complete file: only the three lines change\n");
    {
        const std::string before =
            "; header the player shall keep\r\n"
            "Pistols                = 42     ; (original 1)\r\n"
            "HP_Dog                 = 7      ; (original 18)\r\n"
            "DMG_Skeleton_Strike    = 5      ; (original 400)\r\n"
            "\r\n"
            "WeaponDamage           = 100    ; (original 100)  note\r\n"
            "EnemyHealth            = 100    ; (original 100)  note\r\n"
            "EnemyDamage            = 100    ; (original 100)  note\r\n"
            "SceneryRange           = 65     ; (original 65)\r\n"
            "; closing comment\r\n";
        writeFile(before);

        BalanceFile::Values w;
        w.weaponDamage = 200; w.enemyHealth = 50; w.enemyDamage = 175;
        BalanceFile::write(folder, w, nullptr);

        const std::string t = readFile();
        check("Pistols = 42 untouched", t.find("Pistols                = 42") != std::string::npos);
        check("HP_Dog = 7 untouched", t.find("HP_Dog                 = 7") != std::string::npos);
        check("DMG_Skeleton_Strike = 5 untouched", t.find("DMG_Skeleton_Strike    = 5") != std::string::npos);
        check("SceneryRange untouched", t.find("SceneryRange           = 65") != std::string::npos);
        check("the header comment is still there", t.find("; header the player") != std::string::npos);
        check("the closing comment is still there", t.find("; closing comment") != std::string::npos);
        check("no line was added", t.find("TR5 Randomizer") == std::string::npos);

        const BalanceFile::Values z = BalanceFile::read(folder);
        checkNum("new weaponDamage", z.weaponDamage, 200);
        checkNum("new enemyHealth", z.enemyHealth, 50);
        checkNum("new enemyDamage", z.enemyDamage, 175);

        // The line count must have stayed the same.
        auto count = [](const std::string& s)
        { int n = 0; for (char c : s) if (c == '\n') n++; return n; };
        checkNum("line count unchanged", count(t), count(before));
    }

    std::printf("--- 3) Old file without the three lines: they go to the end\n");
    {
        writeFile("Pistols = 1\r\nHP_Dog = 18\r\n");
        BalanceFile::Values w;
        w.weaponDamage = 120; w.enemyHealth = 90; w.enemyDamage = 110;
        BalanceFile::write(folder, w, nullptr);

        const std::string t = readFile();
        check("Pistols is still there", t.find("Pistols = 1") != std::string::npos);
        check("HP_Dog is still there", t.find("HP_Dog = 18") != std::string::npos);
        const BalanceFile::Values z = BalanceFile::read(folder);
        checkNum("appended weaponDamage", z.weaponDamage, 120);
        checkNum("appended enemyDamage", z.enemyDamage, 110);
    }

    std::printf("--- 4) Lower/upper case, tabs, last line without a newline\n");
    {
        writeFile("enemyhealth\t=\t42\r\nPistols = 1");
        BalanceFile::Values w;
        w.weaponDamage = 100; w.enemyHealth = 60; w.enemyDamage = 100;
        BalanceFile::write(folder, w, nullptr);

        const std::string t = readFile();
        const BalanceFile::Values z = BalanceFile::read(folder);
        checkNum("lower-case line replaced", z.enemyHealth, 60);
        check("only one EnemyHealth line", t.find("nemyHealth") == t.rfind("nemyHealth"));
        check("Pistols without a newline survives", t.find("Pistols = 1") != std::string::npos);
    }

    std::printf("--- 5) A commented-out line does not count as a hit\n");
    {
        writeFile("; EnemyHealth = 999\r\nPistols = 1\r\n");
        BalanceFile::Values w;
        w.enemyHealth = 80;
        BalanceFile::write(folder, w, nullptr);

        const std::string t = readFile();
        check("the comment is unchanged", t.find("; EnemyHealth = 999") != std::string::npos);
        checkNum("the real line was appended", BalanceFile::read(folder).enemyHealth, 80);
    }

    std::printf("--- 6) Without a game folder nothing happens\n");
    {
        std::string err;
        BalanceFile::Values w;
        check("write reports an error", !BalanceFile::write(L"", w, &err));
        check("with a reason", !err.empty(), err.c_str());
        checkNum("read delivers 100", BalanceFile::read(L"").enemyHealth, 100);
    }

    DeleteFileW(PathUtil::join(folder, L"Balance.txt").c_str());
    RemoveDirectoryW(folder.c_str());

    std::printf("\n%s (%d deviations)\n",
                error ? "FAILED" : "ALL FINE", error);
    return error ? 1 : 0;
}
