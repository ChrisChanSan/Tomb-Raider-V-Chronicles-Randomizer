#include "ApplyPatch.h"

#include <cstdio>

namespace
{
    std::string msg(const char* f, int a, int b = 0)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), f, a, b);
        return std::string(buf);
    }
}

bool Patch::apply(Tr5Level& level, const JsonValue& patch, PatchReport& report, std::string& error)
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
            // Das Bit 0x80 bleibt erhalten: es markiert ein regenerierendes
            // Pickup (game/init.cpp:480) und hat mit dem Ort nichts zu tun.
            // Wuerden wir es beim Verschieben mit loeschen, hoerten mehrere
            // Munitionsstapel im Spiel still auf, sich nachzufuellen -
            // Andrea1 Item 66 und 78, Andrea2 Item 69, joby2 Item 8. Erst
            // seit unser eigener Leser das Feld ungemaskt liefert, ist das
            // ueberhaupt sichtbar.
            //
            // 0x100 (Mesh unsichtbar) wird dagegen bewusst geloescht: ein
            // verschobenes Pickup soll an seinem neuen Platz zu sehen sein.
            // 0x40 traegt kein Codepfad aus - reines Editor-Artefakt.
            const uint16_t keep = static_cast<uint16_t>(it.triggerFlags) & 0x0080u;
            const uint16_t low = static_cast<uint16_t>(e["ocb"].asInt()) & 0x003Fu;
            it.triggerFlags = static_cast<int16_t>(keep | low);
        }
        if (e.has("flags"))  it.flags = static_cast<int16_t>(e["flags"].asInt());

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

    return true;
}
