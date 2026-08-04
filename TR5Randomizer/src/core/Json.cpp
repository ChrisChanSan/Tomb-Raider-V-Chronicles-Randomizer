#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

const JsonValue JsonValue::s_null;

bool JsonValue::asBool(bool def) const
{
    if (m_type == Type::Bool)   return m_bool;
    if (m_type == Type::Number) return m_number != 0.0;
    return def;
}

double JsonValue::asDouble(double def) const
{
    if (m_type == Type::Number) return m_number;
    if (m_type == Type::Bool)   return m_bool ? 1.0 : 0.0;
    return def;
}

int64_t JsonValue::asInt(int64_t def) const
{
    if (m_type == Type::Number) return static_cast<int64_t>(m_number < 0 ? m_number - 0.5 : m_number + 0.5);
    if (m_type == Type::Bool)   return m_bool ? 1 : 0;
    return def;
}

std::string JsonValue::asString(const std::string& def) const
{
    if (m_type == Type::String) return m_string;
    return def;
}

const JsonValue& JsonValue::operator[](const std::string& key) const
{
    if (m_type != Type::Object) return s_null;
    auto it = m_object.find(key);
    return (it == m_object.end()) ? s_null : it->second;
}

const JsonValue& JsonValue::operator[](size_t index) const
{
    if (m_type != Type::Array || index >= m_array.size()) return s_null;
    return m_array[index];
}

size_t JsonValue::size() const
{
    if (m_type == Type::Array)  return m_array.size();
    if (m_type == Type::Object) return m_object.size();
    return 0;
}

bool JsonValue::has(const std::string& key) const
{
    return m_type == Type::Object && m_object.find(key) != m_object.end();
}

JsonValue JsonValue::makeObject() { JsonValue v; v.m_type = Type::Object; return v; }
JsonValue JsonValue::makeArray()  { JsonValue v; v.m_type = Type::Array;  return v; }
JsonValue JsonValue::makeBool(bool b)   { JsonValue v; v.m_type = Type::Bool;   v.m_bool = b;   return v; }
JsonValue JsonValue::makeNumber(double d) { JsonValue v; v.m_type = Type::Number; v.m_number = d; return v; }
JsonValue JsonValue::makeInt(int64_t i) { return makeNumber(static_cast<double>(i)); }

JsonValue JsonValue::makeString(const std::string& s)
{
    JsonValue v; v.m_type = Type::String; v.m_string = s; return v;
}

void JsonValue::set(const std::string& key, const JsonValue& v)
{
    if (m_type != Type::Object) { m_type = Type::Object; }
    m_object[key] = v;
}

void JsonValue::push(const JsonValue& v)
{
    if (m_type != Type::Array) { m_type = Type::Array; }
    m_array.push_back(v);
}

namespace
{
    void escapeTo(const std::string& s, std::string& out)
    {
        out.push_back('"');
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04X", c);
                    out += buf;
                }
                else
                {
                    // UTF-8-Bytes werden unveraendert durchgereicht.
                    out.push_back(static_cast<char>(c));
                }
                break;
            }
        }
        out.push_back('"');
    }

    void numberTo(double d, std::string& out)
    {
        char buf[64];
        // Ganzzahlig? Dann ohne Nachkommastelle schreiben - unsere Patches
        // enthalten ausschliesslich ganzzahlige Koordinaten und Indizes, und
        // "90624" liest sich in einer Fehlersuche besser als "90624.0".
        if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9.0e15)
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
        else
            std::snprintf(buf, sizeof(buf), "%.17g", d);
        out += buf;
    }

}

std::string JsonValue::dump() const
{
    std::string out;
    switch (m_type)
    {
    case Type::Null:   out += "null"; break;
    case Type::Bool:   out += (m_bool ? "true" : "false"); break;
    case Type::Number: numberTo(m_number, out); break;
    case Type::String: escapeTo(m_string, out); break;
    case Type::Array:
    {
        out.push_back('[');
        for (size_t i = 0; i < m_array.size(); ++i)
        {
            if (i) out.push_back(',');
            out += m_array[i].dump();
        }
        out.push_back(']');
        break;
    }
    case Type::Object:
    {
        out.push_back('{');
        bool first = true;
        // std::map haelt die Schluessel sortiert - dieselbe Eingabe erzeugt
        // damit bytegleiche Ausgabe, was den Vergleich zweier Laeufe mit
        // demselben Seed zu einem simplen Dateivergleich macht.
        for (const auto& kv : m_object)
        {
            if (!first) out.push_back(',');
            first = false;
            escapeTo(kv.first, out);
            out.push_back(':');
            out += kv.second.dump();
        }
        out.push_back('}');
        break;
    }
    }
    return out;
}

//----------------------------------------------------------------------------

class JsonParser
{
public:
    JsonParser(const std::string& text) : m_t(text), m_p(0), m_line(1) {}

    bool parse(JsonValue& out, std::string& error)
    {
        skipBomAndSpace();
        if (!parseValue(out)) { error = m_error; return false; }
        skipSpace();
        if (m_p != m_t.size())
        {
            // Nachlaufender Inhalt ist ein Fehler und kein Grund zum Schweigen:
            // meist eine abgeschnittene oder doppelt geschriebene Datei.
            fail("Unerwarteter Inhalt nach dem JSON-Wert");
            error = m_error;
            return false;
        }
        return true;
    }

private:
    const std::string& m_t;
    size_t             m_p;
    int                m_line;
    std::string        m_error;

    void fail(const char* msg)
    {
        if (!m_error.empty()) return;   // erste Ursache behalten
        char buf[256];
        std::snprintf(buf, sizeof(buf), "Zeile %d: %s", m_line, msg);
        m_error = buf;
    }

    void skipBomAndSpace()
    {
        if (m_t.size() >= 3 &&
            static_cast<unsigned char>(m_t[0]) == 0xEF &&
            static_cast<unsigned char>(m_t[1]) == 0xBB &&
            static_cast<unsigned char>(m_t[2]) == 0xBF)
        {
            m_p = 3;
        }
        skipSpace();
    }

    void skipSpace()
    {
        while (m_p < m_t.size())
        {
            char c = m_t[m_p];
            if (c == '\n') { ++m_line; ++m_p; }
            else if (c == ' ' || c == '\t' || c == '\r') { ++m_p; }
            else break;
        }
    }

    bool literal(const char* lit)
    {
        size_t n = std::strlen(lit);
        if (m_t.compare(m_p, n, lit) != 0) return false;
        m_p += n;
        return true;
    }

    bool parseValue(JsonValue& out)
    {
        skipSpace();
        if (m_p >= m_t.size()) { fail("Unerwartetes Dateiende"); return false; }

        char c = m_t[m_p];
        switch (c)
        {
        case '{': return parseObject(out);
        case '[': return parseArray(out);
        case '"':
        {
            std::string s;
            if (!parseString(s)) return false;
            out = JsonValue::makeString(s);
            return true;
        }
        case 't': if (literal("true"))  { out = JsonValue::makeBool(true);  return true; } fail("Ungueltiges Literal"); return false;
        case 'f': if (literal("false")) { out = JsonValue::makeBool(false); return true; } fail("Ungueltiges Literal"); return false;
        case 'n': if (literal("null"))  { out = JsonValue(); return true; } fail("Ungueltiges Literal"); return false;
        default:  return parseNumber(out);
        }
    }

    bool parseObject(JsonValue& out)
    {
        out = JsonValue::makeObject();
        ++m_p;                    // '{'
        skipSpace();
        if (m_p < m_t.size() && m_t[m_p] == '}') { ++m_p; return true; }

        for (;;)
        {
            skipSpace();
            std::string key;
            if (!parseString(key)) { fail("Schluessel erwartet"); return false; }
            skipSpace();
            if (m_p >= m_t.size() || m_t[m_p] != ':') { fail("':' erwartet"); return false; }
            ++m_p;

            JsonValue v;
            if (!parseValue(v)) return false;
            out.set(key, v);

            skipSpace();
            if (m_p >= m_t.size()) { fail("'}' erwartet"); return false; }
            if (m_t[m_p] == ',') { ++m_p; continue; }
            if (m_t[m_p] == '}') { ++m_p; return true; }
            fail("',' oder '}' erwartet");
            return false;
        }
    }

    bool parseArray(JsonValue& out)
    {
        out = JsonValue::makeArray();
        ++m_p;                    // '['
        skipSpace();
        if (m_p < m_t.size() && m_t[m_p] == ']') { ++m_p; return true; }

        for (;;)
        {
            JsonValue v;
            if (!parseValue(v)) return false;
            out.push(v);

            skipSpace();
            if (m_p >= m_t.size()) { fail("']' erwartet"); return false; }
            if (m_t[m_p] == ',') { ++m_p; continue; }
            if (m_t[m_p] == ']') { ++m_p; return true; }
            fail("',' oder ']' erwartet");
            return false;
        }
    }

    bool parseString(std::string& out)
    {
        skipSpace();
        if (m_p >= m_t.size() || m_t[m_p] != '"') { fail("'\"' erwartet"); return false; }
        ++m_p;
        out.clear();

        while (m_p < m_t.size())
        {
            char c = m_t[m_p++];
            if (c == '"') return true;
            if (c == '\\')
            {
                if (m_p >= m_t.size()) break;
                char e = m_t[m_p++];
                switch (e)
                {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u':
                {
                    // \uXXXX nach UTF-8. Ersatzpaare werden zusammengefasst,
                    // damit Levelnamen mit Sonderzeichen nicht zerfallen.
                    if (m_p + 4 > m_t.size()) { fail("Unvollstaendige \\u-Sequenz"); return false; }
                    unsigned cp = static_cast<unsigned>(std::strtoul(m_t.substr(m_p, 4).c_str(), nullptr, 16));
                    m_p += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && m_p + 6 <= m_t.size() &&
                        m_t[m_p] == '\\' && m_t[m_p + 1] == 'u')
                    {
                        unsigned lo = static_cast<unsigned>(std::strtoul(m_t.substr(m_p + 2, 4).c_str(), nullptr, 16));
                        if (lo >= 0xDC00 && lo <= 0xDFFF)
                        {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            m_p += 6;
                        }
                    }
                    appendUtf8(cp, out);
                    break;
                }
                default:
                    fail("Unbekannte Escape-Sequenz");
                    return false;
                }
            }
            else
            {
                if (c == '\n') ++m_line;
                out.push_back(c);
            }
        }
        fail("Nicht abgeschlossene Zeichenkette");
        return false;
    }

    static void appendUtf8(unsigned cp, std::string& out)
    {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool parseNumber(JsonValue& out)
    {
        size_t start = m_p;
        if (m_p < m_t.size() && (m_t[m_p] == '-' || m_t[m_p] == '+')) ++m_p;
        bool any = false;
        while (m_p < m_t.size() && m_t[m_p] >= '0' && m_t[m_p] <= '9') { ++m_p; any = true; }
        if (m_p < m_t.size() && m_t[m_p] == '.')
        {
            ++m_p;
            while (m_p < m_t.size() && m_t[m_p] >= '0' && m_t[m_p] <= '9') { ++m_p; any = true; }
        }
        if (any && m_p < m_t.size() && (m_t[m_p] == 'e' || m_t[m_p] == 'E'))
        {
            ++m_p;
            if (m_p < m_t.size() && (m_t[m_p] == '-' || m_t[m_p] == '+')) ++m_p;
            while (m_p < m_t.size() && m_t[m_p] >= '0' && m_t[m_p] <= '9') ++m_p;
        }
        if (!any) { fail("Zahl erwartet"); return false; }

        out = JsonValue::makeNumber(std::strtod(m_t.substr(start, m_p - start).c_str(), nullptr));
        return true;
    }
};

//----------------------------------------------------------------------------

namespace
{
    bool readWholeFile(const std::wstring& path, std::string& out, std::string& error)
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
        {
            error = "Datei laesst sich nicht oeffnen";
            return false;
        }
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n < 0) { std::fclose(f); error = "Dateigroesse nicht lesbar"; return false; }

        out.resize(static_cast<size_t>(n));
        size_t got = (n > 0) ? std::fread(&out[0], 1, static_cast<size_t>(n), f) : 0;
        std::fclose(f);
        if (got != static_cast<size_t>(n)) { error = "Datei nicht vollstaendig lesbar"; return false; }
        return true;
    }
}

bool Json::parseString(const std::string& text, JsonValue& out, std::string& error)
{
    JsonParser p(text);
    return p.parse(out, error);
}

bool Json::parseFile(const std::wstring& path, JsonValue& out, std::string& error)
{
    std::string text;
    if (!readWholeFile(path, text, error)) return false;
    return parseString(text, out, error);
}

bool Json::writeFile(const std::wstring& path, const JsonValue& value, std::string& error)
{
    const std::string text = value.dump();

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f)
    {
        error = "Datei laesst sich nicht schreiben";
        return false;
    }
    // Kein BOM - der Patch-Leser in TombItemEdit2026 erwartet reines UTF-8.
    size_t put = text.empty() ? 0 : std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    if (put != text.size()) { error = "Datei nicht vollstaendig geschrieben"; return false; }
    return true;
}
