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
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Kleiner, eigenstaendiger JSON-Leser und -Schreiber.
//
// Bewusst kein Fremdpaket: der Randomizer soll eine einzelne EXE ohne
// Installation sein, und die zu lesenden Dateien sind ausschliesslich unsere
// eigenen Exporte (Zonen, Level-Infos) plus unser eigenes Patch-Format. Der
// Funktionsumfang ist entsprechend zugeschnitten - vollstaendig genug fuer
// diese Dateien, kein allgemeiner Parser.
//
// Herkunft: der Kern stammt aus dem handgeschriebenen Leser in
// TombItemEdit2026s TRFile_RandomizerPatch.cpp, hier von _T()/stdafx geloest
// und um Schreiben, Gleitkomma und Fehlermeldungen mit Zeilennummer erweitert.
//
// UTF-8 ohne BOM ist das erwartete Format (so write es unsere Werkzeuge),
// ein vorhandenes BOM wird aber uebersprungen statt als Fehler behandelt -
// PowerShells Out-File setzt eines, und das hat in diesem Projekt schon einmal
// einen halben Tag gekostet.
//----------------------------------------------------------------------------
class JsonValue
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue() : m_type(Type::Null), m_bool(false), m_number(0.0) {}

    Type type() const { return m_type; }
    bool isNull()   const { return m_type == Type::Null; }
    bool isArray()  const { return m_type == Type::Array; }
    bool isObject() const { return m_type == Type::Object; }

    // Zugriff mit Standardwert - der uebliche Fall beim Lesen unserer Exporte,
    // wo optionale Felder schlicht fehlen duerfen.
    bool        asBool(bool def = false) const;
    double      asDouble(double def = 0.0) const;
    int64_t     asInt(int64_t def = 0) const;
    std::string asString(const std::string& def = std::string()) const;

    // Objektzugriff. Fehlt der Schluessel, kommt ein Null-Wert zurueck, kein
    // Fehler - so lassen sich optionale Felder ohne Vorabpruefung read.
    const JsonValue& operator[](const std::string& key) const;

    // Arrayzugriff. Ausserhalb des Bereichs kommt ebenfalls Null zurueck.
    const JsonValue& operator[](size_t index) const;

    size_t size() const;
    bool   has(const std::string& key) const;

    const std::vector<JsonValue>& items() const { return m_array; }

    // --- Aufbau fuer das Schreiben ---
    static JsonValue makeObject();
    static JsonValue makeArray();
    static JsonValue makeNumber(double v);
    static JsonValue makeInt(int64_t v);
    static JsonValue makeString(const std::string& v);
    static JsonValue makeBool(bool v);

    void set(const std::string& key, const JsonValue& v);
    void push(const JsonValue& v);

    // Kompakt, ohne Leerraum. Zahlen ohne Nachkommaanteil werden als
    // Ganzzahlen geschrieben, damit das Patch-Format so aussieht wie das der
    // PowerShell-Kette und beide Werkzeuge dieselben Dateien erzeugen koennen.
    std::string dump() const;

private:
    Type                               m_type;
    bool                               m_bool;
    double                             m_number;
    std::string                        m_string;
    std::vector<JsonValue>             m_array;
    std::map<std::string, JsonValue>   m_object;

    static const JsonValue s_null;

    friend class JsonParser;
};

namespace Json
{
    // Liest eine Datei. Bei Erfolg true; sonst false und error enthaelt eine
    // Meldung mit Zeilennummer.
    bool parseFile(const std::wstring& path, JsonValue& out, std::string& error);
    bool parseString(const std::string& text, JsonValue& out, std::string& error);

    // Schreibt UTF-8 OHNE BOM - der Leser in TombItemEdit2026 stolpert sonst.
    bool writeFile(const std::wstring& path, const JsonValue& value, std::string& error);
}
