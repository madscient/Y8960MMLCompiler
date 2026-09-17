#pragma once

// Enough JSON to read what adpcm_packer writes. The core depends on the
// standard library alone, so there is no library to pull in for this.

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace y8 {

class JsonValue {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;

    bool isObject() const { return kind == Kind::Object; }
    bool isArray() const { return kind == Kind::Array; }

    // Returns null when the member is not there or this is not an object.
    const JsonValue* member(const std::string& name) const;

    bool asString(std::string& out) const;
    bool asLong(long& out) const;
};

// Parses `text`. On failure `error` says what went wrong and where.
bool parseJson(const std::string& text, JsonValue& out, std::string& error);

} // namespace y8
