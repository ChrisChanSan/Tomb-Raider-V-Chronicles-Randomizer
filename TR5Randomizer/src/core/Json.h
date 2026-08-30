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
// Small, self-contained JSON reader and writer.
//
// Deliberately no third-party package: the randomizer is meant to be a
// single EXE without installation, and the files it reads are exclusively
// our own exports (zones, level infos) plus our own patch format. The
// feature set is trimmed accordingly - complete enough for these files, not
// a general-purpose parser.
//
// Origin: the core comes from the hand-written reader in TombItemEdit2026's
// TRFile_RandomizerPatch.cpp, detached here from _T()/stdafx and extended
// with writing, floating point and error messages with line numbers.
//
// That is OUR OWN code, and worth saying plainly, because Tr5Level.h rejects
// a different file of that same tool on licence grounds. The file rejected
// there is TRFile.cpp, TRViewer's TR5 parser, which carries no licence
// statement. TRFile_RandomizerPatch.cpp is a file this project added to that
// tool; its reader announces itself as "minimal hand-rolled JSON reader" and
// could not have come from TRViewer, which predates JSON's use here by
// roughly two decades. Only the naming style (boIsArray, lpFind) was matched
// to the host codebase. Nothing third-party travels with this file.
//
// UTF-8 without BOM is the expected format (that is what our tools write),
// but an existing BOM is skipped instead of treated as an error -
// PowerShell's Out-File adds one, and that has already cost half a day in
// this project.
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

    // Access with a default - the usual case when reading our exports,
    // where optional fields are simply allowed to be absent.
    bool        asBool(bool def = false) const;
    double      asDouble(double def = 0.0) const;
    int64_t     asInt(int64_t def = 0) const;
    std::string asString(const std::string& def = std::string()) const;

    // Object access. If the key is missing, a null value comes back, not an
    // error - so optional fields can be read without checking first.
    const JsonValue& operator[](const std::string& key) const;

    // Array access. Out of range likewise returns null.
    const JsonValue& operator[](size_t index) const;

    size_t size() const;
    bool   has(const std::string& key) const;

    const std::vector<JsonValue>& items() const { return m_array; }

    // --- Construction for writing ---
    static JsonValue makeObject();
    static JsonValue makeArray();
    static JsonValue makeNumber(double v);
    static JsonValue makeInt(int64_t v);
    static JsonValue makeString(const std::string& v);
    static JsonValue makeBool(bool v);

    void set(const std::string& key, const JsonValue& v);
    void push(const JsonValue& v);

    // Compact, no whitespace. Numbers without a fractional part are written
    // as integers, so the patch format looks like that of the PowerShell
    // chain and both tools can produce the same files.
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
    // Reads a file. true on success; otherwise false, and error carries a
    // message with a line number.
    bool parseFile(const std::wstring& path, JsonValue& out, std::string& error);
    bool parseString(const std::string& text, JsonValue& out, std::string& error);

    // Writes UTF-8 WITHOUT a BOM - the reader in TombItemEdit2026 trips
    // over one otherwise.
    bool writeFile(const std::wstring& path, const JsonValue& value, std::string& error);
}
