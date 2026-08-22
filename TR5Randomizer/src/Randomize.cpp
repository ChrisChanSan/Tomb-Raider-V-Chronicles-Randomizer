/*
 * TR5 Randomizer
 * Copyright (C) 2026 ChrisChanSan
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */
//----------------------------------------------------------------------------
// Durchstich: aus einem Seed eine fertige randomisierte Leveldatei.
//
// Aufruf:
//   Randomize.exe <zonen.json> <levelinfo.json> <quelle.trc> <ziel.trc> [seed]
//
// Spaeter treibt die Oberflaeche dieselben Schritte an; dieses Programm ist
// der Nachweis, dass die Kette vollstaendig ist und laesst sich zum Pruefen
// einzelner Level behalten.
//----------------------------------------------------------------------------
#include "core/ApplyPatch.h"
#include "core/Json.h"
#include "core/Randomizer.h"
#include "core/Rng.h"
#include "core/ScriptDat.h"
#include "core/SecretListe.h"
#include "core/Tr5Level.h"

#include <cstdio>
#include <set>
#include <string>

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf("Aufruf: Randomize.exe <zonen.json> <levelinfo.json> <quelle.trc> <ziel.trc> [seed] [schalter...]\n");
        std::printf("  Zahl:     Anzahl zusaetzlicher Gegner, 0 = keine (Vorgabe)\n");
        std::printf("  nur:      Items unangetastet lassen - fuer Gegner-/Outfittests\n");
        std::printf("  outfit:   zufaelliges Outfit (Lara_Andrea/Joby/Rich)\n");
        std::printf("  waffen:   volles Waffenset importieren (Spender Andrea3/joby2/rich1)\n");
        std::printf("  mix:      vorhandene Gegner untereinander tauschen\n");
        std::printf("  cross:    Gegnerpool um bis zu drei fremde Typen erweitern (mit mix/Zahl)\n");
        std::printf("  allsanity: der ganze Gegnerpool, jede Rolle wuerfelt frei (mit mix/Zahl)\n");
        std::printf("  itemsN:   N zusaetzliche Verbrauchsgueter streuen (z.B. items20)\n");
        std::printf("  skript=P: SCRIPT.DAT unter P mitbehandeln - Kind-Lara aus den\n");
        std::printf("            Ireland-Leveln und KEY8 fuer joby3 aus joby2. Wiederholbar:\n");
        std::printf("            ein zweiter Lauf ueber dieselbe Datei aendert nichts mehr.\n");
        std::printf("  musik=hintergrund|trigger|beides:\n");
        std::printf("            hintergrund wuerfelt die Levelmusik in der SCRIPT.DAT\n");
        std::printf("            (braucht skript=; NICHT wiederholbar - jeder Lauf wuerfelt\n");
        std::printf("            die Datei erneut). trigger schickt die TO_CD-Tracks der\n");
        std::printf("            Floordata durch eine GLOBALE Permutation der getriggerten Tracks:\n");
        std::printf("            Atmosphaeren unter sich, alles andere unter sich.\n");
        std::printf("  reihenfolge=abschnitte: die vier Story-Abschnitte permutieren\n");
        std::printf("            (braucht skript= und die Engine mit gfLevelId; NICHT\n");
        std::printf("            wiederholbar).\n");
        return 2;
    }

    const std::string seedText = (argc > 5) ? argv[5] : "Weihnachten";
    int gegner = 0, extraItems = 0;
    bool nurGegner = false, outfits = false, waffen = false, mix = false, cross = false, allsanity = false;
    bool notizen = false;
    std::set<int> gegnerAus;         // Abwahl, siehe Argument gegneraus=
    // Fremdinstallationen fuer die Gaeste aus TR3 (#92). Ohne sie fallen die
    // vier Arten still aus dem Pool - genau wie beim Nutzer ohne die Spiele.
    std::wstring tr3Dir, tr3GoldDir;
    // SCRIPT.DAT mitbehandeln - dieselben Eingriffe wie die Oberflaeche.
    std::wstring skriptDat;
    // Secret-Liste (#53): zweiter Patch-Durchgang aus einer Autor-Datei.
    std::wstring secretListe;
    // Musik: "hintergrund" wuerfelt das Audio-Byte der CMD_LEVEL-Eintraege
    // (braucht skript=), "trigger" die TO_CD-Tracks in der Floordata,
    // "beides" beides. VORSICHT hintergrund: nicht wiederholbar - jeder Lauf
    // mit skript= wuerfelt die Datei erneut durch.
    bool musikHintergrund = false, musikTrigger = false;
    // Levelabschnitte permutieren (braucht skript= und die Engine mit gfLevelId).
    bool abschnitte = false;
    for (int i = 6; i < argc; ++i)
    {
        const std::string s = argv[i];
        if (s == "nur")         nurGegner = true;
        else if (s == "notizen") notizen = true;   // Report-Notizen ausgeben (Diagnose)
        else if (s == "outfit") outfits = true;
        else if (s == "waffen") waffen = true;
        else if (s == "mix")    mix = true;
        else if (s == "cross")  cross = true;
        else if (s == "allsanity") allsanity = true;
        else if (s.rfind("tr3=", 0) == 0)     tr3Dir     = toW(s.c_str() + 4);
        else if (s.rfind("tr3gold=", 0) == 0) tr3GoldDir = toW(s.c_str() + 8);
        else if (s.rfind("skript=", 0) == 0)  skriptDat  = toW(s.c_str() + 7);
        else if (s.rfind("secrets=", 0) == 0) secretListe = toW(s.c_str() + 8);
        else if (s == "musik=hintergrund") musikHintergrund = true;
        else if (s == "musik=trigger")     musikTrigger = true;
        else if (s == "musik=beides")      { musikHintergrund = true; musikTrigger = true; }
        else if (s == "reihenfolge=abschnitte") abschnitte = true;
        // "gegneraus=47,49" - dieselbe Abwahl wie das Fenster "Gegnerarten"
        // in der Oberflaeche (#95), damit sich der Filter auch aus Skripten
        // pruefen laesst.
        else if (s.rfind("gegneraus=", 0) == 0)
        {
            size_t a2 = 10;
            while (a2 <= s.size())
            {
                size_t e = s.find(',', a2);
                if (e == std::string::npos) e = s.size();
                std::string stueck = s.substr(a2, e - a2);
                if (!stueck.empty())
                {
                    int t = std::atoi(stueck.c_str());
                    if (t > 0) gegnerAus.insert(t);
                }
                a2 = e + 1;
            }
        }
        // "itemsN" bzw. "items=N" - muss VOR dem Zahl-Rueckfall stehen, sonst
        // schluckt atoi das Wort als 0 und ueberschreibt die Gegnerzahl.
        else if (s.rfind("items", 0) == 0)
        {
            const char* p = s.c_str() + 5;
            if (*p == '=') ++p;
            extraItems = std::atoi(p);
        }
        else                    gegner = std::atoi(argv[i]);
    }
    const uint64_t seed = SeedText::parse(seedText);

    // --- SCRIPT.DAT mitbehandeln (Schalter skript=) -----------------------
    //
    // Dieselben Eingriffe wie die Oberflaeche, aus derselben Quelle: Kind-Lara
    // aus den Ireland-Leveln und der KEY8-Eintrag fuer joby3. Die Oberflaeche
    // legt sich vorher eine Sicherung an und liest immer aus ihr; hier gibt es
    // keinen Sicherungsordner, deshalb wird die Datei an Ort und Stelle
    // bearbeitet. Das ist unbedenklich, weil beide Eingriffe wiederholbar sind
    // - ein schon geloeschtes Bit bleibt geloescht, ein schon vorhandener
    // Objektbefehl wird nicht ein zweites Mal eingefuegt.
    //
    // Passiert vor der Levelarbeit: geht das Skript schief, ist noch nichts
    // geschrieben.
    if (!skriptDat.empty())
    {
        ScriptDat sd;
        std::string sErr;
        if (!sd.load(skriptDat, sErr)) { std::printf("SCRIPT.DAT: %s\n", sErr.c_str()); return 1; }

        ScriptDat::Eingriffe e;
        sd.randomizerEingriffe(e);

        if (e.levelFehlend > 0)
            std::printf("SCRIPT.DAT  %d Ireland-Level nicht im Skript - nichts geaendert.\n", e.levelFehlend);
        else
        {
            if (musikHintergrund)
            {
                int mg = 0;
                if (sd.musikWuerfeln(seed, mg, sErr))
                    std::printf("SCRIPT.DAT  Hintergrundmusik: %d Level neu vertont\n", mg);
                else
                    std::printf("SCRIPT.DAT  Hintergrundmusik: %s\n", sErr.c_str());
            }

            if (abschnitte)
            {
                std::string folge;
                if (sd.abschnitteWuerfeln(seed, folge, sErr))
                    std::printf("SCRIPT.DAT  Abschnitte: %s\n", folge.c_str());
                else
                    std::printf("SCRIPT.DAT  Abschnitte: %s\n", sErr.c_str());
            }

            if (!sd.save(skriptDat, sErr)) { std::printf("SCRIPT.DAT: %s\n", sErr.c_str()); return 1; }
            std::printf("SCRIPT.DAT  Kind-Lara: %d Level geaendert; KEY8 fuer joby3: %s\n",
                        e.flagsGeaendert,
                        !e.keyFehler.empty() ? e.keyFehler.c_str()
                                             : (e.keyKopiert ? "eingefuegt" : "war schon da"));
            std::printf("SCRIPT.DAT  rich2-Pistolen: %s\n",
                        !e.pistolenFehler.empty() ? e.pistolenFehler.c_str()
                                                  : (e.pistolenWeg > 0 ? "abgenommen" : "war schon so"));
        }
    }

    std::string err;

    JsonValue zone, info;
    if (!Json::parseFile(toW(argv[1]), zone, err)) { std::printf("Zonen-Export: %s\n", err.c_str()); return 1; }
    if (!Json::parseFile(toW(argv[2]), info, err)) { std::printf("Level-Info: %s\n", err.c_str()); return 1; }

    RandoInput in;
    if (!Rando::buildInput(zone, info, in, err)) { std::printf("Eingabe: %s\n", err.c_str()); return 1; }

    // Level VOR der Platzierung oeffnen, nicht erst zum Patchen: wandert ein
    // Trigger mit, muss die Platzierung wissen, welche Sektoren ihn aufnehmen
    // koennen, und das steht nur in der Floordata.
    Tr5Level lvl;
    if (!lvl.load(toW(argv[3]), err)) { std::printf("Level laden: %s\n", err.c_str()); return 1; }
    const int itemsBefore = lvl.itemCount();
    Patch::markTriggerSafeSectors(lvl, in);

    RandoOptions opt;
    opt.addEnemies = gegner > 0;
    opt.enemiesPerLevel = gegner;
    opt.swapEnemies = gegner > 0;      // im CLI zusammen, fuer Tests reicht das
    opt.randomOutfits = outfits;
    opt.allWeapons = waffen;
    opt.swapEnemies = mix;
    opt.crossEnemies = cross;
    opt.allsanity = allsanity;
    opt.gesperrteGegner = gegnerAus;
    opt.tr3Dir     = tr3Dir;
    opt.tr3GoldDir = tr3GoldDir;
    opt.extraPickups = extraItems;
    if (nurGegner) { opt.shuffleKeyItems = false; opt.shuffleOtherPickups = false; }

    JsonValue patch;
    RandoReport rep;
    if (!Rando::run(in, opt, seed, patch, rep))
    {
        std::printf("Platzierung fehlgeschlagen: %s\n", rep.error.c_str());
        return 1;
    }

    // Quell-Level fuer soundAdds: sie liegen neben der Quelldatei, die hier
    // ohnehin uebergeben wird.
    static std::wstring ordner = toW(argv[3]);
    {
        const size_t schnitt = ordner.find_last_of(L"\\/");
        ordner = (schnitt == std::wstring::npos) ? L"." : ordner.substr(0, schnitt);
    }
    auto quelle = [](const std::string& name, void*) -> std::wstring {
        std::wstring w;
        for (char c : name) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        return ordner + L"\\" + w + L".trc";
    };

    PatchReport pr;
    // Zwischenlager neben den Quelldateien (#103): einmal entnehmen statt je
    // Level. Ohne TR3-Pfad gibt es nichts zu lagern.
    const std::wstring lager = tr3Dir.empty() ? std::wstring() : ordner + L"\\Import-Export";
    if (!Patch::apply(lvl, patch, pr, err, quelle, nullptr, tr3Dir, tr3GoldDir, lager)) { std::printf("Patch: %s\n", err.c_str()); return 1; }
    for (const std::string& w : pr.warnings) std::printf("  Hinweis: %s\n", w.c_str());

    // Secrets aus einer Nutzerliste: zweiter Durchgang gegen den Stand nach
    // dem ersten Patch - exakt wie in der Oberflaeche (doLevel).
    if (!secretListe.empty())
    {
        std::string levelName = zone["level"].asString("");
        JsonValue sPatch;
        std::vector<std::string> sWarn;
        int sZahl = 0;
        if (!secretPatchBauen(secretListe, levelName, lvl, sPatch, sZahl, sWarn, err))
        { std::printf("Secret-Liste: %s\n", err.c_str()); return 1; }
        for (const std::string& w : sWarn) std::printf("  Secrets: %s\n", w.c_str());

        PatchReport spr;
        if (!Patch::apply(lvl, sPatch, spr, err, quelle, nullptr, tr3Dir, tr3GoldDir, lager))
        { std::printf("Secret-Patch: %s\n", err.c_str()); return 1; }
        for (const std::string& w : spr.warnings) std::printf("  Secrets: %s\n", w.c_str());
        std::printf("  Secrets     %d verschoben, %d Beiwerk (%d Trigger, %zu Portale)\n",
                    sZahl, (spr.edited + spr.added) - sZahl,
                    spr.triggersAdded, sPatch["portalAdds"].size());
    }


    if (notizen)
        for (const std::string& n : rep.notes) std::printf("  Notiz: %s\n", n.c_str());

    if (musikTrigger)
    {
        const long mw = Patch::musikTriggerWuerfeln(lvl, seed);
        std::printf("  Musik-Trigger %ld Woerter neu verteilt\n", mw);
    }

    if (!lvl.save(toW(argv[4]), err)) { std::printf("Speichern: %s\n", err.c_str()); return 1; }

    std::printf("Level %s\n", zone["level"].asString("?").c_str());
    std::printf("  Seed        %s  (0x%016llX)\n", seedText.c_str(), (unsigned long long)seed);
    std::printf("  Platzierung Key %d, sonstige %d, ortsfest %d, injiziert %d\n",
                rep.movedKeyItems, rep.movedOtherItems, rep.pinnedItems, rep.injected);
    std::printf("  Patch       %d geaendert, %d hinzugefuegt\n", pr.edited, pr.added);
    if (rep.enemiesAdded > 0 || pr.triggersAdded > 0 || pr.triggersMoved > 0)
        std::printf("  Trigger     %d Gegner gesetzt, %d Trigger neu, %d verschoben\n",
                    rep.enemiesAdded, pr.triggersAdded, pr.triggersMoved);
    if (rep.enemiesSwapped > 0 || pr.soundsCopied > 0)
        std::printf("  Gegner      %d getauscht, %d Samples uebernommen\n",
                    rep.enemiesSwapped, pr.soundsCopied);
    if (!pr.outfitFrom.empty())
        std::printf("  Outfit      Lara aus %s\n", pr.outfitFrom.c_str());
    if (pr.weaponsCopied > 0 || rep.weaponsScattered > 0)
        std::printf("  Waffen      %d Slots uebernommen, %d Items gestreut\n",
                    pr.weaponsCopied, rep.weaponsScattered);
    if (rep.extraPickups > 0)
        std::printf("  Abundance   %d zusaetzliche Verbrauchsgueter gestreut\n", rep.extraPickups);
    if (rep.crossTypesUsed > 0)
        std::printf("  Cross       %d fremde Gegnertypen, %d Slots uebernommen\n",
                    rep.crossTypesUsed, pr.moveablesCopied);
    std::printf("  Items       %d -> %d\n", itemsBefore, lvl.itemCount());

    // Gegenprobe: das Ergebnis neu einlesen und pruefen, dass die Aenderungen
    // wirklich in der Datei stehen und die Struktur weiter aufgeht. Ohne
    // diesen Schritt wuesste man nur, dass das Schreiben nicht abgestuerzt ist.
    Tr5Level check;
    if (!check.load(toW(argv[4]), err))
    {
        std::printf("  [FEHL] Ergebnis ist nicht mehr lesbar: %s\n", err.c_str());
        return 1;
    }
    if (check.itemCount() != lvl.itemCount())
    {
        std::printf("  [FEHL] Item-Anzahl nach dem Neuladen: %d statt %d\n", check.itemCount(), lvl.itemCount());
        return 1;
    }

    const JsonValue& edits = patch["itemEdits"];
    int verified = 0, wrong = 0;
    for (size_t i = 0; i < edits.size(); ++i)
    {
        const int idx = static_cast<int>(edits[i]["index"].asInt());
        Tr5Item it{};
        if (!check.getItem(idx, it)) { ++wrong; continue; }

        // Ein Gegnertausch traegt nur Index und typeId - keine Position, keine
        // OCB. Der Positionsvergleich unten wuerde ihn gegen lauter Nullen
        // pruefen und immer melden. Er bekommt seine eigene Pruefung.
        if (!edits[i].has("x"))
        {
            if (edits[i].has("typeId") &&
                it.objectNumber != static_cast<int16_t>(edits[i]["typeId"].asInt()))
                ++wrong;
            else
                ++verified;
            continue;
        }

        if (it.roomNumber != static_cast<int16_t>(edits[i]["room"].asInt()) ||
            it.x != static_cast<int32_t>(edits[i]["x"].asInt()) ||
            it.y != static_cast<int32_t>(edits[i]["y"].asInt()) ||
            it.z != static_cast<int32_t>(edits[i]["z"].asInt()) ||
            (it.triggerFlags & 0x3F) != 0)
        {
            ++wrong;
            continue;
        }
        ++verified;
    }
    std::printf("  Gegenprobe  %d Positionen bestaetigt, %d abweichend\n", verified, wrong);

    return wrong ? 1 : 0;
}
