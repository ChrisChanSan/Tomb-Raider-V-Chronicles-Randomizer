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
#include "ApplyPatch.h"
#include "Tr3Quelle.h"

#include <cstdio>
#include <set>
#include <map>
#include <utility>

namespace
{
    std::string msg(const char* f, int a, int b = 0)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), f, a, b);
        return std::string(buf);
    }
}

void Patch::markTriggerSafeSectors(const Tr5Level& level, RandoInput& in)
{
    // Pierre-Kampfsatz-Pruefung: traegt das Level Slot 47 nur als
    // CUTSCENE-Variante (Todesanim +12 hat dort NICHT Zustand 5 -
    // Andrea2, Trajan-Finale), darf der Pool ihn hier nie einsetzen.
    // Der Slot bleibt unangetastet, die Cutscene braucht ihn.
    {
        Tr5Level::MoveableInfo mi;
        if (level.moveableInfo(47, mi) && mi.animIndex >= 0)
        {
            size_t animAt; int32_t nAnims;
            if (level.debugAnimBlock(animAt, nAnims) && mi.animIndex + 12 < nAnims)
            {
                uint16_t zustand = 0;
                std::memcpy(&zustand,
                            level.debugBytes().data() + animAt + (static_cast<size_t>(mi.animIndex) + 12) * 40 + 6, 2);
                if (zustand != 5)
                    in.verboteneTypen.insert(47);
            }
        }
    }

    // Welche Modellslots traegt das Level ueberhaupt? Die Item-Abundance
    // darf nur streuen, was auch gezeichnet werden kann - ein Pickup ohne
    // Modell im Level waere nichts als ein unsichtbarer Stolperstein.
    {
        const std::vector<int> slots = level.moveableSlots();
        in.vorhandeneSlots.insert(slots.begin(), slots.end());
    }

    // Neigungseintraege aller Platzierungssektoren einsammeln - IMMER, auch
    // wenn unten kein Trigger-Durchgang noetig ist: jede Item-Platzierung
    // braucht sie fuer Hoehe und Belaufbarkeit (RandoInput::schraegen).
    {
        SchraegeInfo si;
        int baseY = 0;
        for (const ZoneSector& s : in.sectors)
        {
            const int art = level.floorNeigung(s.room, s.sectorX, s.sectorZ, si, baseY);
            if (art == 0) continue;                          // eben: kein Eintrag noetig
            if (art < 0) { si = SchraegeInfo(); si.typWort = kSchraegeUnlesbar; }
            in.schraegen[secKey(s.room, s.sectorX, s.sectorZ)] = si;
        }
    }

    if (in.coupledTriggers.empty() && in.enemyTypes.empty()) return;

    std::vector<Tr5FloorEntry> chain;
    for (const ZoneSector& s : in.sectors)
    {
        const int idx = level.sectorIndex(s.room, s.sectorX, s.sectorZ);
        if (idx < 0) continue;
        if (!level.readChain(idx, chain)) continue;          // unlesbar: Finger weg
        if (Tr5Level::canAppendTrigger(chain) != nullptr) continue;
        in.triggerSafeSectors.insert(secKey(s.room, s.sectorX, s.sectorZ));
    }
}

bool Patch::apply(Tr5Level& level, const JsonValue& patch, PatchReport& report, std::string& error,
                  LevelSourceFn levelSource, void* levelSourceUser,
                  const std::wstring& tr3Dir, const std::wstring& tr3GoldDir,
                  const std::wstring& lagerDir)
{
    report = PatchReport();

    if (patch.has("itemRemoves"))
    {
        // Klare Ansage statt stiller Ignoranz: wer das Feld setzt, erwartet
        // eine Wirkung. Ein Entfernen wuerde alle nachfolgenden Item-Indizes
        // verschieben und damit die "Object N"-Verweise der Trigger ins Leere
        // zeigen lassen - das Level waere ohne erkennbare Fehlermeldung kaputt.
        error = "Der Patch enthaelt itemRemoves. Items zu entfernen verschiebt "
                "alle folgenden Indizes und zerstoert die Triggerzuordnung - "
                "das wird bewusst nicht unterstuetzt.";
        return false;
    }

    if (patch.has("triggerRemoves"))
    {
        // Dieselbe Ansage wie bei itemRemoves, aus demselben Grund: ein
        // Trigger, den niemand mehr ausloest, ist eine Tuer, die nie aufgeht.
        // Verschieben ja, wegnehmen nein.
        error = "Der Patch enthaelt triggerRemoves. Trigger zu entfernen wird "
                "bewusst nicht unterstuetzt - nur Verschieben (triggerMoves).";
        return false;
    }

    // --- Trigger verschieben --------------------------------------------
    //
    // Bewusst vor den Item-Aenderungen: so steht die Floordata fest, bevor
    // irgendetwas an den Items geschrieben wird. Notwendig ist die Reihenfolge
    // nicht - appendChain zieht den Item-Offset selbst nach - aber sie macht
    // den Ablauf beim Nachlesen eindeutig.
    const JsonValue& moves = patch["triggerMoves"];
    for (size_t i = 0; i < moves.size(); ++i)
    {
        const JsonValue& m = moves[i];
        std::string moveErr;
        if (!level.moveTrigger(static_cast<int>(m["fromRoom"].asInt(-1)),
                               static_cast<int>(m["fromSectorX"].asInt(-1)),
                               static_cast<int>(m["fromSectorZ"].asInt(-1)),
                               static_cast<int>(m["toRoom"].asInt(-1)),
                               static_cast<int>(m["toSectorX"].asInt(-1)),
                               static_cast<int>(m["toSectorZ"].asInt(-1)),
                               moveErr))
        {
            // Hart abbrechen, nicht warnen: wandert das Item, der Trigger aber
            // nicht, liegt der Schluessel woanders und seine Wirkung bleibt am
            // alten Fleck zurueck. Ein halb ausgefuehrter Patch waere schlimmer
            // als gar keiner.
            error = "triggerMoves[" + std::to_string(i) + "]: " + moveErr;
            return false;
        }
        ++report.triggersMoved;
    }

    // Spender-Level werden von Outfit UND soundAdds gebraucht und sind mehrere
    // Megabyte gross - jeder wird hoechstens einmal geladen.
    std::map<std::string, Tr5Level> quellen;
    auto quelle = [&](const std::string& name) -> Tr5Level* {
        auto it = quellen.find(name);
        if (it != quellen.end()) return &it->second;
        if (!levelSource) return nullptr;
        const std::wstring pfad = levelSource(name, levelSourceUser);
        std::string ladeErr;
        Tr5Level q;
        if (pfad.empty() || !q.load(pfad, ladeErr)) return nullptr;
        return &quellen.emplace(name, std::move(q)).first->second;
    };

    // --- Outfit ----------------------------------------------------------
    //
    // Slots 8/9/10 (LARA_SKIN, LARA_SKIN_JOINTS, LARA_SCREAM) aus dem
    // Spender, dazu sein LaraType-Byte. NIE Slot 0 - der traegt die
    // Animationen des Levels (im Spiel erlebt: fehlende Schalter- und
    // Kriechanimationen). Die Spenderregel stammt aus der Waffenarbeit und
    // ist in allen 13 Leveln gespielt.
    //
    // Ein halbes Outfit gibt es nicht: schlaegt einer der drei Slots fehl,
    // bricht der Patch ab, BEVOR gespeichert wird - das Level bleibt dann
    // die unveraenderte Kopie aus dem Backup.
    const JsonValue& outfit = patch["outfit"];
    if (outfit.has("fromLevel"))
    {
        const std::string von = outfit["fromLevel"].asString();
        Tr5Level* spender = quelle(von);
        if (!spender)
        {
            report.warnings.push_back("Outfit: Spender-Level " + von + " nicht ladbar - Outfit bleibt.");
        }
        else
        {
            // Haut, Gelenke, Schrei-Kopf - und ZWINGEND das Gefolge:
            //
            // Haar (30): das Haarmodell gehoert zum Kopf. joby2s Lara traegt
            // eine Muetze, ihr Haarobjekt ist dafuer gebaut; ein fremder Zopf
            // an diesem Kopf spiesst durch den Koerper (im Spiel gesehen).
            //
            // Holster (13-16): DrawLara__1 zeichnet bei erwachsener Lara die
            // beiden Oberschenkel ZUSAETZLICH aus den Holster-Objekten
            // (drawlara.cpp:227ff). rich- und andy-Level haben diese Objekte
            // gar nicht - objects[].mesh_index steht dann auf 0, und auf dem
            // linken Oberschenkel landet ein fremdes Mesh, leicht versetzt
            // (im Spiel gesehen). Der Catsuit-Pfad (DrawLara__4) braucht
            // keine Holster, deshalb hat rich2 auch keine zu bieten - fuer
            // ihn ist die Liste nach den Pflichtslots einfach zu Ende.
            std::vector<int> slots = { 8, 9, 10, 30 };
            for (int s : { 13, 14, 15, 16 })
            {
                Tr5Level::MoveableInfo mi;
                if (spender->moveableInfo(s, mi)) slots.push_back(s);
            }

            // Sprechkoepfe (17-20): in Dialog-Cutscenes tauscht die Engine
            // Laras Kopf gegen LARA_SPEECH_HEAD1-4 (deltapak.cpp,
            // handle_lara_chatting) - die tragen das VANILLA-Gesicht des
            // Levels (im Spiel gesehen: Catsuit mit Strickmuetze in joby5).
            // Hat der Spender eigene, wandern sie wie die Holster mit. rich2
            // hat KEINE - die VCI-Level kennen keine Dialog-Cutscenes; dann
            // bekommt das Ziel das Fremdkoepfe-Flag (data[2] im Levelkopf)
            // und die Engine laesst den Tausch aus.
            bool sprechkoepfeDa = true;
            for (int s : { 17, 18, 19, 20 })
            {
                Tr5Level::MoveableInfo mi;
                if (!spender->moveableInfo(s, mi)) { sprechkoepfeDa = false; break; }
            }
            if (sprechkoepfeDa)
                for (int s : { 17, 18, 19, 20 }) slots.push_back(s);

            for (int slot : slots)
            {
                std::string oErr;
                if (!level.copyMoveableFrom(*spender, slot, oErr))
                {
                    error = "Outfit-Slot " + std::to_string(slot) + " aus " + von + ": " + oErr;
                    return false;
                }
            }

            // Das LaraType-Byte ist unser Outfit-Marker fuer die Engine:
            // hair.cpp und laraskin.cpp machen die Zopf-Sonderbehandlung
            // (Verankerung UND Schweisstabelle des Zopfansatzes) daran fest -
            // Core hatte beides am Levelnamen haengen, weil das Outfit
            // vanilla nie den Abschnitt wechselt. Ohne Marker klebte rich1s
            // Zopf an den Kopf-Vertexnummern der Muetze (im Spiel gesehen).
            // Die Entscheidung faellt im Randomizer ("laraType" im Patch,
            // siehe dort: 2 = Joby-Outfit ueberall, 5 = Andrea-Outfit in
            // einem Russland-Level); der Rueckfall fuer handgeschriebene
            // Patches ohne das Feld bleibt die alte Joby-Regel.
            if (outfit.has("laraType"))
                level.setLaraType(static_cast<int>(outfit["laraType"].asInt(spender->laraType())));
            else
                level.setLaraType(von == "joby2" ? 2 : spender->laraType());
            level.setFremdeSprechkoepfe(sprechkoepfeDa ? 0 : 1);
            report.outfitFrom = von;
        }
    }

    // Holster nachruesten, wo das Outfit keine mitbringt. Der Catsuit-Spender
    // rich2 hat die Slots 13-16 nicht (sein Zeichenweg brauchte sie nie);
    // seit die Engine sie auch auf der Catsuit zeichnet, sollen sie ueberall
    // liegen. Nur fehlende Slots werden gefuellt - vorhandene (etwa Andrea1s
    // eigene) bleiben.
    if (outfit.has("holstersFrom"))
    {
        const std::string von = outfit["holstersFrom"].asString();
        Tr5Level* spender = quelle(von);
        if (!spender)
            report.warnings.push_back("Holster: Spender-Level " + von + " nicht ladbar.");
        else
            for (int s : { 13, 14, 15, 16 })
            {
                Tr5Level::MoveableInfo mi;
                if (level.moveableInfo(s, mi)) continue;       // hat es schon
                if (!spender->moveableInfo(s, mi)) continue;   // hat nicht mal der Spender
                std::string oErr;
                if (!level.copyMoveableFrom(*spender, s, oErr))
                {
                    error = "Holster-Slot " + std::to_string(s) + " aus " + von + ": " + oErr;
                    return false;
                }
            }
    }

    // --- Waffen ueberall --------------------------------------------------
    //
    // Kopierliste aus dem Randomizer (fertig aufgeloest, Regeln dort). Laeuft
    // NACH dem Outfit: die Holsterslots 13-16 des Outfit-Spenders sollen
    // stehen bleiben, die Waffeneintraege dafuer sind "nur was fehlt" und
    // werden dann hier uebersprungen.
    //
    // Fehlerbild mit Absicht zweigeteilt: ein nicht ladbarer SPENDER ist wie
    // bei Outfit und Sounds nur eine Warnung (das Level bleibt spielbar, ihm
    // fehlen halt Waffen). Ein GESCHEITERTER Kopiervorgang dagegen bricht ab
    // wie ein halbes Outfit - dann ist unklar, in welchem Zustand die
    // angehaengten Bloecke sind, und gespeichert wird so etwas nicht.
    // weaponAdds und moveableAdds sind dieselbe Operation - beide sind
    // aufgeloeste Kopierlisten {slot, fromLevel, replace} aus dem Randomizer,
    // sie zaehlen nur getrennt (Waffen bzw. Cross-Gegner).
    // Holt einen Gast aus Tomb Raider 3 und setzt ihn ein (#92).
    //
    // Alles passiert hier und jetzt auf dem Rechner des Nutzers: entnehmen,
    // umrechnen, einsetzen. Nichts davon liegt vorbereitet herum, denn
    // vorbereitete Pakete waeren fremde Spieldaten.
    auto tr3Bauen = [&](Tr5Level& lvl, int slot, PatchReport& rep, std::string& err) -> bool
    {
        const Tr3Quelle::DinoQuelle* d = nullptr;
        static const std::vector<Tr3Quelle::DinoQuelle> alle = Tr3Quelle::dinos();
        for (const Tr3Quelle::DinoQuelle& k : alle) if (k.slot == slot) { d = &k; break; }
        if (!d) { err = "Kein Gast fuer Slot " + std::to_string(slot) + " bekannt"; return false; }

        // Ueber das Zwischenlager (#103): einmal entnehmen statt je Level.
        std::vector<uint8_t> modell, klaenge;
        std::string f;
        if (!Tr3Quelle::holeMitLager(*d, tr3Dir, tr3GoldDir, lagerDir, modell, klaenge, f))
        { err = f; return false; }

        // 1. Sounds
        std::vector<std::pair<int,int>> zuordnung;
        if (!lvl.addSoundsFromTr3(klaenge, d->klangBasis, zuordnung, f, &d->laut))
        { err = std::string(d->name) + " (Sounds einsetzen): " + f; return false; }

        // 2. SFX-Nummern der Anim-Kommandos umbiegen. Bleibt eine Nummer
        //    offen, wird ABGEBROCHEN - sie wuerde im Ziel einen fremden
        //    Vanilla-Klang treffen (beim Compy wurde aus 243 ein wuchtiger
        //    SFX_GENERIC_HEAVY_FEET, und das zu finden hat Stunden gekostet).
        std::vector<int> offen;
        if (!Tr3Quelle::biegeSfxUm(modell, zuordnung, d->stumm, 411, offen, f))
        { err = std::string(d->name) + " (SFX umbiegen): " + f; return false; }
        if (!offen.empty())
        {
            err = std::string(d->name) + ": SFX-Nummer ohne Zuordnung (" +
                  std::to_string(offen[0]) + ") - sie wuerde einen fremden Klang treffen";
            return false;
        }

        // 3. Texturen ANHAENGEN, nie verschieben - sonst faellt das Level in
        //    sich zusammen (Nutzervorgabe, siehe Texturseiten-Regel).
        std::map<int,int> texMap;
        if (!lvl.addTexturesFromTr3(modell, texMap, f))
        { err = std::string(d->name) + " (Texturen): " + f; return false; }

        // Und die Faces darauf zeigen lassen. Ohne diesen Schritt behalten sie
        // die TR3-Nummern und greifen im Ziel wahllos in dessen Texinfos.
        if (Tr3Quelle::biegeTexUm(modell, texMap) < 0)
        { err = std::string(d->name) + ": Faces nicht lesbar"; return false; }

        // 4. Modell. texBasis -1, weil die Faces schon auf die neuen Texinfos
        //    zeigen.
        if (!lvl.addMoveableFromTr3(modell, slot, -1, f))
        { err = std::string(d->name) + " (Modell): " + f; return false; }

        rep.warnings.push_back(std::string("Gast aus Tomb Raider 3 geholt: ") + d->name +
                               " auf Slot " + std::to_string(slot));
        return true;
    };

    auto kopiereSlots = [&](const JsonValue& liste, const char* was, int& zaehler) -> bool
    {
        if (liste.size() > 0 && !levelSource)
        {
            report.warnings.push_back(std::to_string(liste.size()) + " " + was +
                                      "-Eintraege uebersprungen - kein Zugriff auf die Quell-Level.");
            return true;
        }
        for (size_t i = 0; i < liste.size(); ++i)
        {
            const JsonValue& w = liste[i];
            const int slot = static_cast<int>(w["slot"].asInt(-1));

            // --- Gast aus Tomb Raider 3 (#92) -------------------------------
            //
            // Kein Spenderlevel: alles wird JETZT aus der Installation des
            // Nutzers geholt. Genau dafuer ist dieser Weg da - wir liefern
            // keine Spieldaten mit, also darf nichts vorbereitet sein.
            //
            // Reihenfolge ist Vorschrift, nicht Geschmack (siehe Rezept):
            // SOUNDS -> TEXTUREN -> MODELL. Erst danach steht fest, worauf die
            // Anim-Kommandos und die Faces zeigen muessen, und beides steckt im
            // Modellpaket, das zuletzt eingebaut wird.
            if (w.has("ausTr3"))
            {
                if (!tr3Bauen(level, slot, report, error)) return false;
                ++zaehler;
                continue;
            }

            const std::string von = w["fromLevel"].asString();
            const bool ersetzen = w["replace"].asBool(false);
            // Abweichender Zielslot (Ausweichslots des Cross-Gegnermix:
            // Spender-MESHSWAP1 405 landet im Slot DES GEGNERS, seit #111
            // 401-404 bzw. 462-467), und ob nur die Meshes wandern
            // (Mesh-Lager ohne eigene Animationen).
            const int nach = static_cast<int>(w["toSlot"].asInt(-1));
            const int zielSlot = (nach < 0) ? slot : nach;
            const bool nurMeshes = w["onlyMeshes"].asBool(false);
            // Explizite Anim-Scheibenlaenge (Imp: Todesanim liegt jenseits
            // der Objektgrenze in der globalen Tabelle, siehe Tr5Level.h).
            const int animZahl = static_cast<int>(w["animZahl"].asInt(-1));

            Tr5Level::MoveableInfo mi;
            if (!ersetzen && level.moveableInfo(zielSlot, mi)) continue;   // hat es schon

            Tr5Level* spender = quelle(von);
            if (!spender)
            {
                report.warnings.push_back(std::string(was) + ": Spender-Level " + von +
                                          " nicht ladbar - Slots von dort fehlen.");
                continue;
            }

            std::string wErr;
            if (!level.copyMoveableFrom(*spender, slot, wErr, nach, nurMeshes, animZahl))
            {
                error = std::string(was) + "-Slot " + std::to_string(slot) + " aus " + von + ": " + wErr;
                return false;
            }
            ++zaehler;
        }
        return true;
    };
    if (!kopiereSlots(patch["weaponAdds"], "Waffen", report.weaponsCopied)) return false;
    if (!kopiereSlots(patch["moveableAdds"], "Cross-Gegner", report.moveablesCopied)) return false;

    // Sobald SWAT (33) und ein Guard zusammen im Level liegen, laeuft der
    // Guard per Engine-Alias auf SWATs Anim-Satz und feuert dort ueber
    // Zustand 3 - dessen Anim (+2) ist in Cores rich3-Daten totes Erbe mit
    // falscher Schrittweite (liest die 34-Wort-Frames der Zielanim in 36ern:
    // Modell verschwindet ab Keyframe 1, Schatten zerrt, kein Schusssound).
    // Reparatur: +2 uebernimmt Frame-Daten, Rate und Schusskommando der
    // gesunden Guard-Feueranim (+2 seines eigenen Satzes) im selben Level.
    // Als Quelle taugt jeder Guard-Satz: der klassische auf 37 oder der
    // Joby-Wachmann auf 460 (BLUE_GUARD2, #105) - beide feuern ueber
    // Zustand 3, ihre Saetze tragen die Anim gesund.
    {
        Tr5Level::MoveableInfo swat, guard;
        if (level.moveableInfo(33, swat) && swat.animIndex >= 0)
        {
            int guardSlot = -1;
            if (level.moveableInfo(37, guard) && guard.animIndex >= 0)
                guardSlot = 37;
            else if (level.moveableInfo(460, guard) && guard.animIndex >= 0)
                guardSlot = 460;

            if (guardSlot > 0)
            {
                std::string rErr;
                if (!level.uebernehmeAnimDaten(33, 2, guardSlot, 2, rErr))
                    report.warnings.push_back("SWAT-Feueranim nicht reparierbar: " + rErr);
            }
        }
    }

    // --- Sound-Samples uebernehmen ---------------------------------------
    const JsonValue& sadds = patch["soundAdds"];
    if (sadds.size() > 0 && !levelSource)
    {
        report.warnings.push_back(msg("%d soundAdds uebersprungen - kein Zugriff auf die Quell-Level.",
                                      static_cast<int>(sadds.size())));
    }
    else
    {
        for (size_t i = 0; i < sadds.size(); ++i)
        {
            const JsonValue& s = sadds[i];
            const std::string von = s["fromLevel"].asString();
            const int map = static_cast<int>(s["map"].asInt(-1));
            const int vonMap = static_cast<int>(s["fromMap"].asInt(-1));

            if (level.soundInfoIndex(map) >= 0) continue;      // schon vorhanden

            Tr5Level* q = quelle(von);
            if (!q)
            {
                report.warnings.push_back("soundAdds: Quell-Level " + von + " nicht ladbar - Sound fehlt.");
                continue;
            }

            std::string sErr;
            if (!level.copySoundFrom(*q, vonMap, map, sErr))
            {
                // Kein Abbruch: ein fehlender Klang macht das Level nicht
                // kaputt, anders als ein fehlender Trigger.
                report.warnings.push_back("soundAdds: Map " + std::to_string(map) + " - " + sErr);
                continue;
            }
            ++report.soundsCopied;
        }
    }

    // --- Neue Trigger ---------------------------------------------------
    //
    // Nach den Zuegen, aber vor den Items: so steht die gesamte Floordata fest,
    // bevor irgendetwas an den Items geschrieben wird. Zwingend ist es nicht -
    // appendChain zieht den Item-Offset selbst nach -, aber es haelt den Ablauf
    // beim Nachlesen eindeutig.
    // --- Lara-Anims aus Spenderleveln (#53) -----------------------------
    //
    // {"anim": 437, "fromLevel": "joby2"} pflanzt eine einzelne Lara-Anim
    // samt Frames, Kommandos und Sounds ein. Gebraucht fuer ANIM_KEYCARD:
    // der Platz existiert ueberall, echte Bilddaten hat nur joby2. Ein
    // nicht ladbarer Spender ist nur eine Warnung (die Engine faellt an
    // Key Hole 8 dann auf die gewoehnliche Schluesselanim zurueck); ein
    // GESCHEITERTER Einbau bricht ab.
    const JsonValue& lanims = patch["laraAnimAdds"];
    if (lanims.size() > 0 && !levelSource)
        report.warnings.push_back(std::to_string(lanims.size()) +
                                  " Lara-Anim-Eintraege uebersprungen - kein Zugriff auf die Quell-Level.");
    else for (size_t i = 0; i < lanims.size(); ++i)
    {
        const JsonValue& la = lanims[i];
        const std::string von = la["fromLevel"].asString();
        Tr5Level* spender = quelle(von);
        if (!spender)
        {
            report.warnings.push_back("Lara-Anim: Spender-Level " + von + " nicht ladbar.");
            continue;
        }
        std::string aErr;
        if (!level.kopiereLaraAnimVon(*spender, static_cast<int>(la["anim"].asInt(-1)), aErr))
        {
            error = "laraAnimAdds[" + std::to_string(i) + "]: " + aErr;
            return false;
        }
    }

    // --- Wasserraeume (#53) ---------------------------------------------
    //
    // {"room": N, "an": true|false} setzt bzw. loescht ROOM_UNDERWATER.
    // Harter Fehler wie bei Portal und Kletterwand: ein halb umgestellter
    // Raum waere schlimmer als gar keiner.
    const JsonValue& wasser = patch["roomWater"];
    for (size_t i = 0; i < wasser.size(); ++i)
    {
        const JsonValue& w = wasser[i];
        std::string wErr;
        if (!level.setRoomWater(static_cast<int>(w["room"].asInt(-1)), w["an"].asBool(true), wErr))
        {
            error = "roomWater[" + std::to_string(i) + "]: " + wErr;
            return false;
        }
    }

    // --- Kletterwaende (#53) --------------------------------------------
    //
    // {"room","sectorX","sectorZ","dirBits"}: der Sektor wird kletterbar.
    // dirBits nach der Engine-Konvention (control.cpp:2189), berechnet vom
    // Listenleser aus @Angle. Harter Fehler wie beim Portal - eine halbe
    // Kletterwand faende niemand.
    const JsonValue& klettern = patch["climbAdds"];
    for (size_t i = 0; i < klettern.size(); ++i)
    {
        const JsonValue& c = klettern[i];
        std::string cErr;
        if (!level.makeClimb(static_cast<int>(c["room"].asInt(-1)),
                             static_cast<int>(c["sectorX"].asInt(-1)),
                             static_cast<int>(c["sectorZ"].asInt(-1)),
                             static_cast<int>(c["dirBits"].asInt(0)), cErr))
        {
            error = "climbAdds[" + std::to_string(i) + "]: " + cErr;
            return false;
        }
    }

    // --- Portale (#53) --------------------------------------------------
    //
    // Ein Eintrag oeffnet BEIDE Seiten einer versiegelten Wand: der genannte
    // Sektor von roomA wird Portal nach roomB und umgekehrt. Die Sichtfenster
    // (32-Byte-Portale der Raumliste) muessen schon existieren - makePortal
    // prueft das und bricht sonst hart ab, denn eine begehbare Oeffnung ohne
    // Fenster waere ein schwarzes Loch im Bild.
    const JsonValue& portale = patch["portalAdds"];
    for (size_t i = 0; i < portale.size(); ++i)
    {
        const JsonValue& p = portale[i];
        std::string pErr;
        if (!level.makePortal(static_cast<int>(p["roomA"].asInt(-1)),
                              static_cast<int>(p["sxA"].asInt(-1)),
                              static_cast<int>(p["szA"].asInt(-1)),
                              static_cast<int>(p["roomB"].asInt(-1)), pErr) ||
            !level.makePortal(static_cast<int>(p["roomB"].asInt(-1)),
                              static_cast<int>(p["sxB"].asInt(-1)),
                              static_cast<int>(p["szB"].asInt(-1)),
                              static_cast<int>(p["roomA"].asInt(-1)), pErr))
        {
            error = "portalAdds[" + std::to_string(i) + "]: " + pErr;
            return false;
        }
    }

    const JsonValue& tadds = patch["triggerAdds"];

    // Versatz zwischen Planung und Datei ausgleichen (#107): die Planung
    // nummeriert neue Items ab der Itemzahl des LEVEL-JSONS (itemBase), die
    // itemAdds haengen aber an die ECHTE Datei an. Traegt die Quelldatei mehr
    // Items als das JSON (die Waffen-Basis entstand nach dem JSON-Export),
    // zeigte jeder Wecker auf das Item ZWEI VOR seinem Gegner, und die
    // letzten Gegner blieben ohne Trigger fuer immer regungslos stehen
    // (Andrea1 Seed 1: JSON 151, Datei 153 - in trview gesehen).
    // Verschoben werden nur TO_OBJECT-Ziele >= itemBase: alles darunter sind
    // Vanilla-Indizes, und die verschieben sich nicht, weil auch die
    // Waffen-Basis ihre Extra-Items nur hinten anhaengt.
    const int itemBase = static_cast<int>(patch["itemBase"].asInt(-1));
    const int itemDelta = (itemBase >= 0) ? (level.itemCount() - itemBase) : 0;
    if (itemDelta != 0 && tadds.size() > 0)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "Trigger-Ziele um %+d verschoben: das Level-JSON kennt %d Items, "
                      "die Quelldatei traegt %d. JSON-Export und Basislevel laufen auseinander.",
                      itemDelta, itemBase, level.itemCount());
        report.warnings.push_back(buf);
    }

    for (size_t i = 0; i < tadds.size(); ++i)
    {
        const JsonValue& t = tadds[i];

        std::vector<Tr5TriggerCmd> cmds;
        const JsonValue& cl = t["commands"];
        for (size_t c = 0; c < cl.size(); ++c)
        {
            Tr5TriggerCmd cmd;
            cmd.type = static_cast<int>(cl[c]["type"].asInt(0));
            cmd.param = static_cast<int>(cl[c]["param"].asInt(0));
            if (cmd.type == 0 && itemBase >= 0 && cmd.param >= itemBase)
                cmd.param += itemDelta;                      // TO_OBJECT auf ein NEUES Item
            cmds.push_back(cmd);
        }

        std::string addErr;
        // Vorgaben: art 0 (TRIGGER), Maske voll, oneShot gesetzt. Das ist die
        // Form, die ein Gegnertrigger braucht - abweichen muss, wer etwas
        // anderes will.
        if (!level.addTrigger(static_cast<int>(t["room"].asInt(-1)),
                              static_cast<int>(t["sectorX"].asInt(-1)),
                              static_cast<int>(t["sectorZ"].asInt(-1)),
                              static_cast<int>(t["art"].asInt(0)),
                              static_cast<int>(t["timer"].asInt(0)),
                              t["oneShot"].asInt(1) != 0,
                              static_cast<int>(t["mask"].asInt(31)),
                              cmds, addErr,
                              static_cast<int>(t["refItem"].asInt(-1))))
        {
            // Hart abbrechen wie beim Zug: ein Gegner ohne seinen Trigger steht
            // fuer immer regungslos im Level, und das faellt spaeter niemandem
            // als Fehler auf - es sieht nur kaputt aus.
            error = "triggerAdds[" + std::to_string(i) + "]: " + addErr;
            return false;
        }
        ++report.triggersAdded;
    }

    // --- Aenderungen ---------------------------------------------------
    const JsonValue& edits = patch["itemEdits"];
    for (size_t i = 0; i < edits.size(); ++i)
    {
        const JsonValue& e = edits[i];
        const int index = static_cast<int>(e["index"].asInt(-1));

        Tr5Item it{};
        if (index < 0 || !level.getItem(index, it))
        {
            report.warnings.push_back(msg("itemEdits[%d]: Item %d gibt es im Level nicht - uebersprungen.",
                                          static_cast<int>(i), index));
            continue;
        }

        // Nur gesetzte Felder uebernehmen.
        //
        // "typeId" tauscht die Gegnerart aus. Der Index bleibt dabei, was er
        // ist - deshalb bleiben alle "Object N"-Verweise der Trigger gueltig
        // und der Gegner wacht weiter an derselben Stelle auf. Genau deshalb
        // ist der Tausch ueberhaupt ungefaehrlich, waehrend ein Entfernen es
        // nie waere.
        if (e.has("typeId")) it.objectNumber = static_cast<int16_t>(e["typeId"].asInt());
        if (e.has("room"))   it.roomNumber = static_cast<int16_t>(e["room"].asInt());
        if (e.has("x"))      it.x = static_cast<int32_t>(e["x"].asInt());
        if (e.has("y"))      it.y = static_cast<int32_t>(e["y"].asInt());
        if (e.has("z"))      it.z = static_cast<int32_t>(e["z"].asInt());

        // "yAuto" auch beim Verschieben (#53): dieselbe Rechnung wie bei den
        // Hinzufuegungen weiter unten. Gebraucht von Erzeugern, die nur Raum
        // und Sektor kennen - die Secret-Listen des Nutzers nennen genau das.
        if (e["yAuto"].asBool(false) &&
            it.roomNumber >= 0 && static_cast<size_t>(it.roomNumber) < level.rooms().size())
        {
            const Tr5Room& rm = level.rooms()[static_cast<size_t>(it.roomNumber)];
            SchraegeInfo si;
            int baseY = 0;
            const int art = level.floorNeigung(it.roomNumber,
                                               (it.x - rm.originX) / 1024,
                                               (it.z - rm.originZ) / 1024, si, baseY);
            if (art == 0) it.y = static_cast<int32_t>(baseY);
            else if (art > 0) it.y = static_cast<int32_t>(bodenLageAn(si, baseY, it.x & 1023, it.z & 1023).y);
        }
        // Versatz gegen die gerechnete Bodenlage. Gebraucht fuer gestapelte
        // Objekte: eine Doppeltuer aus zwei Tuermodellen, das obere einen
        // Block ueber dem unteren. y waechst in TR nach UNTEN, ein Objekt
        // hoeher zu setzen heisst also abzuziehen - die Liste schreibt
        // deshalb eine Hoehe ueber dem Boden, und der Leser dreht das Zeichen.
        if (e.has("xOff")) it.x += static_cast<int32_t>(e["xOff"].asInt(0));
        if (e.has("zOff")) it.z += static_cast<int32_t>(e["zOff"].asInt(0));
        if (e.has("yOff")) it.y += static_cast<int32_t>(e["yOff"].asInt(0));
        if (e.has("angle"))  it.yRot = static_cast<int16_t>(e["angle"].asInt());
        if (e.has("ocb"))
        {
            // "ocb" setzt die unteren sechs Bit - das ist der Wert, den
            // PickUpCollision und InitialisePickup als OCB auswerten.
            //
            // Die Bits 0x40 UND 0x80 bleiben erhalten. Beide wertet
            // PickUpCollision gemeinsam aus (game/pickup.cpp:989):
            //
            //     if (!(item->trigger_flags & 0xC0))
            //         KillItem(item_number);
            //     else { item->item_flags[3] = 1;
            //            item->flags |= IFL_TRIGGERED;
            //            item->status = ITEM_INVISIBLE; }
            //
            // Ohne eines dieser Bits wird das aufgenommene Item schlicht
            // getoetet. Mit ihnen bleibt es als ITEM_INVISIBLE stehen und
            // bekommt item_flags[3] = 1 - und genau das sind die beiden
            // Bedingungen, die PickupTrigger (pickup.cpp:158) verlangt, damit
            // ein Pickup-Trigger ueberhaupt feuert.
            //
            // 0x80 markiert zusaetzlich ein regenerierendes Pickup
            // (game/init.cpp:492). Wuerden wir es loeschen, hoerten mehrere
            // Munitionsstapel still auf, sich nachzufuellen - Andrea1 Item 66
            // und 78, Andrea2 Item 69, joby2 Item 8.
            //
            // 0x40 stand hier frueher als "reines Editor-Artefakt, traegt kein
            // Codepfad aus" und wurde geloescht. Das war falsch und hat den
            // gekoppelten Triggerzug von Andrea1 Item 107 stumm gemacht: das
            // Item kam am neuen Platz ohne 0x40 an, wurde beim Aufnehmen
            // getoetet, und der mitgewanderte Trigger fand nie die Bedingung,
            // um Hund und Flame Emitter zu wecken. Sichtbar war das nirgends,
            // weil jedes Werkzeug die OCB auf 0x3F maskiert anzeigt - Item 107
            // steht ueberall als "OCB 0" da, roh ist es 0x0040.
            //
            // Gegenprobe: alle sieben Items, die ueber einen Pickup-Trigger
            // ortsgebunden sind, tragen dieses Bit (Andrea1 107 und 94,
            // Andrea3 68, andy1 8, Andy2 29, rich1 14, rich3 75).
            //
            // 0x100 (Mesh unsichtbar) wird dagegen bewusst geloescht: ein
            // verschobenes Pickup soll an seinem neuen Platz zu sehen sein.
            const uint16_t keep = static_cast<uint16_t>(it.triggerFlags) & 0x00C0u;
            const uint16_t low = static_cast<uint16_t>(e["ocb"].asInt()) & 0x003Fu;
            it.triggerFlags = static_cast<int16_t>(keep | low);
        }
        if (e.has("flags"))  it.flags = static_cast<int16_t>(e["flags"].asInt());

        // "antitriggerImmun" setzt IFL_ANTITRIGGER_ONESHOT (0x80) im
        // Flag-Wort. Ein Antitrigger, der auf einen AKTIVEN intelligenten
        // Gegner zeigt, toetet ihn ohne Umweg: hit_points = -16384,
        // DisableBaddieAI, KillItem (control.cpp:2422) - kein Sterben, kein
        // Ausblenden, er ist einfach weg. In Vanilla ist das Absicht, weil
        // der Gegner an dieser Stelle laengst erledigt sein soll; nach einem
        // Typentausch stimmt diese Annahme nicht mehr und der neue Gegner
        // verschwindet mitten im Kampf (im Spiel gesehen: Andrea1 Item 34).
        //
        // Cores eigene Bremse dagegen ist dieses Bit: steht es, laesst
        // TestTriggers das Item in Ruhe (control.cpp:2378) - gedacht als
        // "einmal antigetriggert, nie wieder". Wir setzen es von vornherein,
        // dann prallt der Antitrigger ab und der Gegner kaempft und stirbt
        // normal. Das wirkt pro ITEM, nicht pro Trigger: teilen sich mehrere
        // Gegner einen Antitrigger (Andy2 Items 150-154), bleiben die
        // uebrigen unberuehrt. Das Bit ist in allen 15 Vanilla-Leveln bei
        // keinem einzigen der 2094 Items gesetzt (gemessen) und wird von
        // InitialiseItem nicht angefasst, ueberlebt also aus der Datei.
        if (e["antitriggerImmun"].asBool(false))
            it.flags = static_cast<int16_t>(static_cast<uint16_t>(it.flags) | 0x0080u);

        if (!level.setItem(index, it))
        {
            error = msg("Item %d liess sich nicht schreiben.", index);
            return false;
        }
        ++report.edited;
    }

    // --- Hinzufuegungen -------------------------------------------------
    const JsonValue& adds = patch["itemAdds"];
    for (size_t i = 0; i < adds.size(); ++i)
    {
        const JsonValue& a = adds[i];

        Tr5Item it{};
        it.objectNumber = static_cast<int16_t>(a["typeId"].asInt());
        it.roomNumber = static_cast<int16_t>(a["room"].asInt());
        it.x = static_cast<int32_t>(a["x"].asInt());
        it.y = static_cast<int32_t>(a["y"].asInt());
        it.z = static_cast<int32_t>(a["z"].asInt());

        // "yAuto": die echte Bodenhoehe an dieser Stelle ausrechnen statt sie
        // vom Aufrufer zu uebernehmen (#61). Der Sektorgrundwert ist auf einer
        // Schraege die HOECHSTE Ecke - ein Item in der Sektormitte haenge damit
        // bis zu einen halben Sektor in der Luft (#86). Dieselbe Rechnung, mit
        // der die Engine den Boden findet: floorNeigung + bodenLageAn.
        //
        // Gebraucht von Erzeugern, die keine Neigungsdaten haben (das
        // Wandtest-Skript in PowerShell). Ohne das Feld bleibt alles wie
        // bisher - der Randomizer rechnet sein y selbst und setzt es nicht.
        if (a["yAuto"].asBool(false) &&
            it.roomNumber >= 0 && static_cast<size_t>(it.roomNumber) < level.rooms().size())
        {
            const Tr5Room& rm = level.rooms()[static_cast<size_t>(it.roomNumber)];
            const int sx = (it.x - rm.originX) / 1024;
            const int sz = (it.z - rm.originZ) / 1024;
            SchraegeInfo si;
            int baseY = 0;
            const int art = level.floorNeigung(it.roomNumber, sx, sz, si, baseY);
            if (art == 0)
                it.y = static_cast<int32_t>(baseY);          // eben
            else if (art > 0)
            {
                const BodenLage lage = bodenLageAn(si, baseY, it.x & 1023, it.z & 1023);
                it.y = static_cast<int32_t>(lage.y);
            }
            // art < 0 (Kette unlesbar): das mitgegebene y bleibt stehen.
        }


        // Wie bei den Aenderungen: Versatz gegen die gerechnete Bodenlage,
        // damit sich Objekte stapeln lassen (Doppeltuer).
        if (a.has("xOff")) it.x += static_cast<int32_t>(a["xOff"].asInt(0));
        if (a.has("zOff")) it.z += static_cast<int32_t>(a["zOff"].asInt(0));
        if (a.has("yOff")) it.y += static_cast<int32_t>(a["yOff"].asInt(0));
        it.yRot = static_cast<int16_t>(a["angle"].asInt(0));
        it.triggerFlags = static_cast<int16_t>(a["ocb"].asInt(0));
        it.flags = static_cast<int16_t>(a["flags"].asInt(0));
        // shade: -1 bedeutet dynamische Beleuchtung. 0 waere pechschwarz, und
        // ein pechschwarzes Pickup findet niemand.
        it.shade = static_cast<int16_t>(a["shade"].asInt(-1));

        std::string addErr;
        if (!level.addItem(it, addErr))
        {
            error = "itemAdds: " + addErr;
            return false;
        }
        ++report.added;
    }

    // --- AI-Objekte (Fluchtmarker) ----------------------------------------
    //
    // NACH den itemAdds: addAiObject rechnet die Lage des AI-Blocks aus der
    // aktuellen Item-Anzahl. Larson/Pierre bekommen so zu jedem Exemplar
    // einen AI_AMBUSH auf dem Spawn-Sektor - die 40-LP-Flucht findet ihr
    // Ziel und despawnt dort, statt endlos hinter Lara herzulaufen.
    const JsonValue& aiAdds = patch["aiAdds"];
    for (size_t i = 0; i < aiAdds.size(); ++i)
    {
        const JsonValue& a = aiAdds[i];

        Tr5AiObject ai{};
        ai.objectNumber = static_cast<int16_t>(a["slot"].asInt());
        ai.roomNumber = static_cast<int16_t>(a["room"].asInt());
        ai.x = static_cast<int32_t>(a["x"].asInt());
        ai.y = static_cast<int32_t>(a["y"].asInt());
        ai.z = static_cast<int32_t>(a["z"].asInt());
        ai.ocb = static_cast<int16_t>(a["ocb"].asInt(0));

        std::string aiErr;
        if (!level.addAiObject(ai, aiErr))
        {
            error = "aiAdds: " + aiErr;
            return false;
        }
    }

    // --- Laras Interaktions-Anims nachreichen (#53) ----------------------
    //
    // Laras Anim-Tabelle ist in jedem Level gleich lang, die BILDDATEN sind
    // aber levelspezifisch. Wo ein Level eine Bewegung nie braucht, laesst
    // Core den Eintrag als Stub stehen (FrameSize 0) - abgespielt wird eine
    // eingefrorene Pose oder eine viel zu kurze Bewegung. Beim Nutzer
    // zweimal aufgeschlagen: die Keycard in joby3 (22 Sekunden erstarrt) und
    // ein Schalter in andy3 ("macht eine Schalteranimation, nur nicht die
    // richtige - viel zu schnell"). Das zweite ist schlimmer als haesslich:
    // Lara ist vor dem Ende der OBJEKT-Animation fertig, verlaesst den
    // Sektor und der Trigger greift nie.
    //
    // Deshalb hier, NACH allen Objektaenderungen: nachsehen, welche
    // Interaktionen im fertigen Level ueberhaupt vorkommen, und die
    // zugehoerige Anim aus einem Spenderlevel holen, wenn sie fehlt.
    //
    // Die Spenderreihenfolge stammt aus der Vermessung aller 13 Level mit
    // LaraAnim: joby4 traegt fast alles, andy1 fast den Rest. Genommen wird
    // der erste Spender, der die Anim WIRKLICH traegt - geprueft wird das am
    // geladenen Spender, nicht geglaubt.
    {
        struct Bedarf { int typVon, typBis; int ocbVon, ocbBis; int anim; const char* was; };
        static const Bedarf kBedarf[] = {
            // Schalter 266..271: die OCB waehlt Laras Bewegung (switch.cpp:246ff)
            { 266, 271,  0,  0,  64, "Schalter" },
            { 266, 271,  1,  2, 325, "Schalter (versteckt)" },
            { 266, 271,  3,  3, 413, "Schalter (klein)" },
            { 266, 271,  4,  4, 196, "Schalter (Generator)" },
            { 266, 271,  5,  7, 197, "Schalter (einhaendig)" },
            { 266, 271,  8, 63, 470, "Schalter (Airlock)" },
            { 274, 274,  8, 63, 470, "Airlock Switch" },
            // Schluessel- und Puzzleloecher
            { 258, 264,  0, 63, 131, "Schluesselloch" },
            // Key Hole 8 nimmt die Kartenzieh-Geste, WENN das Level ihre
            // Daten hat (pickup.cpp fragt die FrameSize ab). Deshalb hier
            // anfordern - dann hat es sie, und die Geste passt zum Objekt.
            // ANIM_USEKEY bleibt trotzdem noetig: sie ist der Rueckfall.
            { 265, 265,  0, 63, 437, "Schluesselloch 8 (Karte)" },
            { 265, 265,  0, 63, 131, "Schluesselloch 8 (Rueckfall)" },
            { 236, 243,  0, 63, 134, "Puzzleloch" },
            // Falltueren und Schiebeblock
            { 109, 110,  0, 63, 317, "Bodenfalltuer" },
            { 111, 112,  0, 63, 318, "Deckenfalltuer" },
            { 137, 141,  0, 63, 316, "Schiebeblock" },
        };

        // Spenderreihenfolge, vermessen mit LaraAnim ueber alle 13 Level.
        static const char* kSpender[] = { "joby4", "andy1", "Andy3", "joby3", "Andrea3", "joby5", "Andy2", "joby2", "rich2" };

        // Diese Bewegungen haengen an keinem Objekt, sondern an Laras
        // Steuerung - sie muessen in JEDEM Level liegen. Gefunden vom
        // Nutzer: die Kriechrolle (Ducken + Sprint, lara.cpp:3288 setzt
        // Anim 218) ist in rich1 ein Stub, und zwar schon vanilla - Lara
        // wird nach vorn geschoben, ohne dass sich etwas bewegt. Gemessen
        // mit LaraAnim: rich1 ist das EINZIGE Level mit FrameSize 0 dort.
        static const int kImmer[] = {
            218,   // AS_DUCKROLL - Kriechrolle
            421,   // ANIM_CRAWLJUMP - Kriechsprung
        };
        std::set<int> gebraucht(std::begin(kImmer), std::end(kImmer));
        for (int i = 0; i < level.itemCount(); ++i)
        {
            Tr5Item it;
            if (!level.getItem(i, it)) continue;
            const int typ = it.objectNumber;
            const int ocb = it.triggerFlags & 0x3F;
            for (const Bedarf& b : kBedarf)
                if (typ >= b.typVon && typ <= b.typBis && ocb >= b.ocbVon && ocb <= b.ocbBis)
                    gebraucht.insert(b.anim);
        }

        for (int animNr : gebraucht)
        {
            if (level.laraAnimBrauchbar(animNr)) continue;
            if (!levelSource)
            {
                report.warnings.push_back("Lara-Anim " + std::to_string(animNr) +
                                          " fehlt und kein Zugriff auf die Spender-Level.");
                continue;
            }
            bool geholt = false;
            for (const char* sp : kSpender)
            {
                Tr5Level* q = quelle(sp);
                if (!q || !q->laraAnimBrauchbar(animNr)) continue;
                std::string aErr;
                if (!level.kopiereLaraAnimVon(*q, animNr, aErr)) continue;
                report.warnings.push_back("Lara-Anim " + std::to_string(animNr) +
                                          " aus " + sp + " nachgereicht.");
                geholt = true;
                break;
            }
            if (!geholt)
                report.warnings.push_back("Lara-Anim " + std::to_string(animNr) +
                                          " fehlt und kein Spender hat sie - Interaktion bleibt fehlerhaft.");
        }
    }

    // Texturwerkstatt schliessen (#104): alle Seiten-Importe dieses Laufs
    // werden jetzt EINMAL gepackt und in die Datei gesetzt. save() weigert
    // sich, eine schmutzig offene Werkstatt zu schreiben - vergessen faellt
    // also laut auf, nicht als kaputtes Level.
    if (!level.texWerkstattSchliessen(error))
    {
        error = "Texturwerkstatt: " + error;
        return false;
    }

    return true;
}
