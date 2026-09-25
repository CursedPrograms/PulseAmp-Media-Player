#pragma once
// ─── json.h ───────────────────────────────────────────────────────────────────
// Minimal JSON reader (for skin.json): objects, arrays, strings, numbers,
// true/false/null. Read-only; lookups on missing keys return a null value.
// ─────────────────────────────────────────────────────────────────────────────
#include <map>
#include <memory>
#include <string>
#include <vector>

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    // Parse text; on error returns a Null value and sets error (with position)
    static Json parse(const std::string& text, std::string& error);

    Type type() const { return type_; }
    bool isNull()   const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray()  const { return type_ == Type::Array; }
    bool isString() const { return type_ == Type::String; }
    bool isNumber() const { return type_ == Type::Number; }

    // Values with defaults when the type doesn't match
    std::string str(const std::string& def = "") const { return type_ == Type::String ? s_ : def; }
    double      num(double def = 0.0) const { return type_ == Type::Number ? n_ : def; }
    bool        boolean(bool def = false) const { return type_ == Type::Bool ? b_ : def; }

    // Object / array access (null value if missing / out of range)
    const Json& operator[](const std::string& key) const;
    const Json& operator[](size_t i) const;
    size_t size() const { return type_ == Type::Array ? arr_.size() : type_ == Type::Object ? obj_.size() : 0; }
    bool   has(const std::string& key) const { return type_ == Type::Object && obj_.count(key); }
    const std::vector<Json>& items() const { return arr_; }
    const std::map<std::string, Json>& members() const { return obj_; }

private:
    friend class JsonParser;
    Type   type_ = Type::Null;
    bool   b_ = false;
    double n_ = 0.0;
    std::string s_;
    std::vector<Json> arr_;
    std::map<std::string, Json> obj_;
};
