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

#include <cstdio>
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
                  LevelSourceFn levelSource, void* levelSourceUser)
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
            const std::string von = w["fromLevel"].asString();
            const bool ersetzen = w["replace"].asBool(false);
            // Abweichender Zielslot (Ausweichslots des Cross-Gegnermix:
            // Spender-MESHSWAP1 405 landet unter 399-402), und ob nur die
            // Meshes wandern (Mesh-Lager ohne eigene Animationen).
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

    // Sobald SWAT (33) und Guard (37) zusammen im Level liegen, laeuft der
    // Guard per Engine-Alias auf SWATs Anim-Satz und feuert dort ueber
    // Zustand 3 - dessen Anim (+2) ist in Cores rich3-Daten totes Erbe mit
    // falscher Schrittweite (liest die 34-Wort-Frames der Zielanim in 36ern:
    // Modell verschwindet ab Keyframe 1, Schatten zerrt, kein Schusssound).
    // Reparatur: +2 uebernimmt Frame-Daten, Rate und Schusskommando der
    // gesunden Guard-Feueranim (+2 seines eigenen Satzes) im selben Level.
    {
        Tr5Level::MoveableInfo swat, guard;
        if (level.moveableInfo(33, swat) && swat.animIndex >= 0 &&
            level.moveableInfo(37, guard) && guard.animIndex >= 0)
        {
            std::string rErr;
            if (!level.uebernehmeAnimDaten(33, 2, 37, 2, rErr))
                report.warnings.push_back("SWAT-Feueranim nicht reparierbar: " + rErr);
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
    const JsonValue& tadds = patch["triggerAdds"];
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
                              cmds, addErr))
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

    return true;
}
