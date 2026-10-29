// The project's small JSON reader and writer (ntr/json.h).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <ntr/json.h>

namespace ntr {

const std::string& Json::as_string() const {
    static const std::string kEmpty;
    return type_ == Type::String ? string_ : kEmpty;
}

void Json::push(Json value) {
    if (type_ != Type::Array) *this = array();
    items_.push_back(std::move(value));
}

const Json& Json::get(const std::string& key) const {
    static const Json kNull;
    for (const auto& m : members_) {
        if (m.first == key) return m.second;
    }
    return kNull;
}

void Json::set(const std::string& key, Json value) {
    if (type_ != Type::Object) *this = object();
    for (auto& m : members_) {
        if (m.first == key) {
            m.second = std::move(value);
            return;
        }
    }
    members_.emplace_back(key, std::move(value));
}

namespace {

struct Parser {
    const std::string& text;
    size_t at = 0;
    std::string error;

    bool fail(const std::string& message) {
        if (error.empty()) {
            int line = 1;
            for (size_t i = 0; i < at && i < text.size(); i++) line += text[i] == '\n';
            error = "line " + std::to_string(line) + ": " + message;
        }
        return false;
    }
    void space() {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) at++;
    }
    bool literal(const char* word) {
        size_t n = std::char_traits<char>::length(word);
        if (text.compare(at, n, word) != 0) return false;
        at += n;
        return true;
    }
    static void put_utf8(std::string& out, uint32_t c) {
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += {char(0xC0 | c >> 6), char(0x80 | (c & 0x3F))};
        } else if (c < 0x10000) {
            out += {char(0xE0 | c >> 12), char(0x80 | (c >> 6 & 0x3F)), char(0x80 | (c & 0x3F))};
        } else {
            out += {char(0xF0 | c >> 18), char(0x80 | (c >> 12 & 0x3F)), char(0x80 | (c >> 6 & 0x3F)), char(0x80 | (c & 0x3F))};
        }
    }
    bool hex4(uint32_t& v) {
        if (at + 4 > text.size()) return fail("bad \\u escape");
        v = 0;
        for (int i = 0; i < 4; i++) {
            char c = text[at++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool string(std::string& out) {
        at++;  // the opening quote
        while (at < text.size()) {
            char c = text[at++];
            if (c == '"') return true;
            if (uint8_t(c) < 0x20) return fail("control character in a string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at >= text.size()) break;
            switch (char e = text[at++]) {
                case '"': case '\\': case '/': out += e; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t c1;
                    if (!hex4(c1)) return false;
                    if (c1 >= 0xD800 && c1 < 0xDC00 && literal("\\u")) {
                        uint32_t c2;
                        if (!hex4(c2)) return false;
                        c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
                    }
                    put_utf8(out, c1);
                    break;
                }
                default: return fail("bad escape in a string");
            }
        }
        return fail("unterminated string");
    }
    bool value(Json& out, int depth) {
        if (depth > 64) return fail("nested too deep");
        space();
        if (at >= text.size()) return fail("expected a value");
        char c = text[at];
        if (c == '{') {
            at++;
            out = Json::object();
            space();
            if (at < text.size() && text[at] == '}') {
                at++;
                return true;
            }
            for (;;) {
                space();
                if (at >= text.size() || text[at] != '"') return fail("expected a key");
                std::string key;
                if (!string(key)) return false;
                space();
                if (at >= text.size() || text[at] != ':') return fail("expected ':'");
                at++;
                Json item;
                if (!value(item, depth + 1)) return false;
                out.set(key, std::move(item));
                space();
                if (at < text.size() && text[at] == ',') {
                    at++;
                    continue;
                }
                if (at < text.size() && text[at] == '}') {
                    at++;
                    return true;
                }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            at++;
            out = Json::array();
            space();
            if (at < text.size() && text[at] == ']') {
                at++;
                return true;
            }
            for (;;) {
                Json item;
                if (!value(item, depth + 1)) return false;
                out.push(std::move(item));
                space();
                if (at < text.size() && text[at] == ',') {
                    at++;
                    continue;
                }
                if (at < text.size() && text[at] == ']') {
                    at++;
                    return true;
                }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            std::string s;
            if (!string(s)) return false;
            out = Json(std::move(s));
            return true;
        }
        if (literal("true")) return out = Json(true), true;
        if (literal("false")) return out = Json(false), true;
        if (literal("null")) return out = Json(), true;
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* start = text.c_str() + at;
            char* end = nullptr;
            double n = std::strtod(start, &end);
            if (end == start) return fail("bad number");
            at += size_t(end - start);
            out = Json(n);
            return true;
        }
        return fail("unexpected character");
    }
};

void write(const Json& v, std::string& out, int indent) {
    auto newline = [&](int level) {
        out += '\n';
        out.append(size_t(level) * 2, ' ');
    };
    switch (v.type()) {
        case Json::Type::Null: out += "null"; break;
        case Json::Type::Bool: out += v.as_bool() ? "true" : "false"; break;
        case Json::Type::Number: {
            double n = v.as_number();
            char buf[32];
            if (std::isfinite(n) && n == std::floor(n) && std::fabs(n) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", n);
            else if (std::isfinite(n)) std::snprintf(buf, sizeof buf, "%.17g", n);
            else std::snprintf(buf, sizeof buf, "null");
            out += buf;
            break;
        }
        case Json::Type::String:
            out += '"';
            for (char c : v.as_string()) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (uint8_t(c) < 0x20) {
                            char buf[8];
                            std::snprintf(buf, sizeof buf, "\\u%04x", unsigned(uint8_t(c)));
                            out += buf;
                        } else {
                            out += c;
                        }
                }
            }
            out += '"';
            break;
        case Json::Type::Array:
            if (v.items().empty()) {
                out += "[]";
                break;
            }
            out += '[';
            for (size_t i = 0; i < v.items().size(); i++) {
                if (i) out += ',';
                newline(indent + 1);
                write(v.items()[i], out, indent + 1);
            }
            newline(indent);
            out += ']';
            break;
        case Json::Type::Object:
            if (v.members().empty()) {
                out += "{}";
                break;
            }
            out += '{';
            for (size_t i = 0; i < v.members().size(); i++) {
                if (i) out += ',';
                newline(indent + 1);
                write(Json(v.members()[i].first), out, indent + 1);
                out += ": ";
                write(v.members()[i].second, out, indent + 1);
            }
            newline(indent);
            out += '}';
            break;
    }
}

}  // namespace

bool json_parse(const std::string& text, Json& out, std::string& error) {
    Parser p{text, 0, {}};
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) p.at = 3;  // a BOM from a Windows editor
    Json value;
    if (!p.value(value, 0)) {
        error = p.error;
        return false;
    }
    p.space();
    if (p.at != text.size()) {
        p.fail("text after the value");
        error = p.error;
        return false;
    }
    out = std::move(value);
    return true;
}

std::string json_write(const Json& value) {
    std::string out;
    write(value, out, 0);
    out += '\n';
    return out;
}

}  // namespace ntr
