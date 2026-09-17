#include "json.h"

#include <cmath>
#include <cstdlib>

namespace y8 {
namespace {

class Parser {
public:
    Parser(const std::string& text) : s_(text) {}

    bool parse(JsonValue& out, std::string& error) {
        skipSpace();
        if (!value(out)) {
            error = error_.empty() ? "malformed JSON" : error_;
            error += " at offset " + std::to_string(pos_);
            return false;
        }
        skipSpace();
        if (pos_ != s_.size()) {
            error = "trailing text at offset " + std::to_string(pos_);
            return false;
        }
        return true;
    }

private:
    void skipSpace() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool literal(const char* word) {
        std::size_t n = 0;
        while (word[n]) ++n;
        if (s_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }

    bool fail(const char* why) {
        if (error_.empty()) error_ = why;
        return false;
    }

    bool string(std::string& out) {
        if (pos_ >= s_.size() || s_[pos_] != '"') return fail("a string was expected");
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= s_.size()) return fail("the string ends inside an escape");
            char e = s_[pos_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos_ + 4 > s_.size()) return fail("a \\u escape is cut short");
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = s_[pos_++];
                        unsigned d;
                        if (h >= '0' && h <= '9') d = static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') d = static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') d = static_cast<unsigned>(h - 'A' + 10);
                        else return fail("a \\u escape needs four hex digits");
                        code = code * 16 + d;
                    }
                    // Enough for what names in this file can hold. Surrogate
                    // pairs are left as the two code points they are written as.
                    if (code < 0x80) {
                        out.push_back(static_cast<char>(code));
                    } else if (code < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default: return fail("that escape is not a JSON escape");
            }
        }
        return fail("the string has no closing quote");
    }

    bool value(JsonValue& out) {
        if (pos_ >= s_.size()) return fail("the text ends where a value was expected");
        char c = s_[pos_];
        if (c == '{') return object(out);
        if (c == '[') return array(out);
        if (c == '"') {
            out.kind = JsonValue::Kind::String;
            return string(out.text);
        }
        if (literal("true")) {
            out.kind = JsonValue::Kind::Bool;
            out.boolean = true;
            return true;
        }
        if (literal("false")) {
            out.kind = JsonValue::Kind::Bool;
            out.boolean = false;
            return true;
        }
        if (literal("null")) {
            out.kind = JsonValue::Kind::Null;
            return true;
        }
        return number(out);
    }

    bool number(JsonValue& out) {
        const char* start = s_.c_str() + pos_;
        char* end = nullptr;
        double v = std::strtod(start, &end);
        if (end == start) return fail("a value was expected");
        pos_ += static_cast<std::size_t>(end - start);
        out.kind = JsonValue::Kind::Number;
        out.number = v;
        return true;
    }

    bool array(JsonValue& out) {
        ++pos_;  // '['
        out.kind = JsonValue::Kind::Array;
        skipSpace();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            skipSpace();
            JsonValue item;
            if (!value(item)) return false;
            out.array.push_back(std::move(item));
            skipSpace();
            if (pos_ >= s_.size()) return fail("the array has no closing bracket");
            if (s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (s_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return fail("',' or ']' was expected");
        }
    }

    bool object(JsonValue& out) {
        ++pos_;  // '{'
        out.kind = JsonValue::Kind::Object;
        skipSpace();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            skipSpace();
            std::string key;
            if (!string(key)) return false;
            skipSpace();
            if (pos_ >= s_.size() || s_[pos_] != ':') return fail("':' was expected");
            ++pos_;
            skipSpace();
            JsonValue item;
            if (!value(item)) return false;
            out.object[key] = std::move(item);
            skipSpace();
            if (pos_ >= s_.size()) return fail("the object has no closing brace");
            if (s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return fail("',' or '}' was expected");
        }
    }

    const std::string& s_;
    std::size_t pos_ = 0;
    std::string error_;
};

} // namespace

const JsonValue* JsonValue::member(const std::string& name) const {
    if (kind != Kind::Object) return nullptr;
    auto it = object.find(name);
    return it == object.end() ? nullptr : &it->second;
}

bool JsonValue::asString(std::string& out) const {
    if (kind != Kind::String) return false;
    out = text;
    return true;
}

bool JsonValue::asLong(long& out) const {
    if (kind != Kind::Number) return false;
    double rounded = std::floor(number + 0.5);
    if (std::fabs(number - rounded) > 1e-6) return false;
    out = static_cast<long>(rounded);
    return true;
}

bool parseJson(const std::string& text, JsonValue& out, std::string& error) {
    Parser p(text);
    return p.parse(out, error);
}

} // namespace y8
