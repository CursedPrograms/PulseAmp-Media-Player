// ─── json.cpp ─────────────────────────────────────────────────────────────────
#include "json.h"
#include <cstdlib>
#include <cstring>

static const Json kNull;

const Json& Json::operator[](const std::string& key) const {
    if (type_ != Type::Object) return kNull;
    auto it = obj_.find(key);
    return it == obj_.end() ? kNull : it->second;
}

const Json& Json::operator[](size_t i) const {
    return (type_ == Type::Array && i < arr_.size()) ? arr_[i] : kNull;
}

class JsonParser {
public:
    JsonParser(const std::string& t) : t_(t) {}

    Json parseDocument(std::string& err) {
        Json v;
        if (!value(v) || (ws(), p_ != t_.size())) {
            if (err_.empty()) err_ = "unexpected content";
            err = err_ + " at offset " + std::to_string(p_);
            return Json();
        }
        return v;
    }

private:
    const std::string& t_;
    size_t p_ = 0;
    std::string err_;
    int depth_ = 0;

    void ws() {
        while (p_ < t_.size()) {
            char c = t_[p_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++p_; continue; }
            // Allow // and /* */ comments (handy in hand-written skin files)
            if (c == '/' && p_ + 1 < t_.size() && t_[p_ + 1] == '/') {
                while (p_ < t_.size() && t_[p_] != '\n') ++p_;
                continue;
            }
            if (c == '/' && p_ + 1 < t_.size() && t_[p_ + 1] == '*') {
                size_t e = t_.find("*/", p_ + 2);
                p_ = e == std::string::npos ? t_.size() : e + 2;
                continue;
            }
            break;
        }
    }
    bool fail(const char* m) { if (err_.empty()) err_ = m; return false; }

    bool value(Json& v) {
        if (++depth_ > 64) return fail("nesting too deep");
        ws();
        if (p_ >= t_.size()) return fail("unexpected end");
        bool ok;
        char c = t_[p_];
        if (c == '{')      ok = object(v);
        else if (c == '[') ok = array(v);
        else if (c == '"') { v.type_ = Json::Type::String; ok = string(v.s_); }
        else if (c == 't' && t_.compare(p_, 4, "true") == 0)  { v.type_ = Json::Type::Bool; v.b_ = true;  p_ += 4; ok = true; }
        else if (c == 'f' && t_.compare(p_, 5, "false") == 0) { v.type_ = Json::Type::Bool; v.b_ = false; p_ += 5; ok = true; }
        else if (c == 'n' && t_.compare(p_, 4, "null") == 0)  { p_ += 4; ok = true; }
        else ok = number(v);
        --depth_;
        return ok;
    }

    bool number(Json& v) {
        const char* start = t_.c_str() + p_;
        char* end = nullptr;
        double d = std::strtod(start, &end);
        if (end == start) return fail("invalid value");
        p_ += (size_t)(end - start);
        v.type_ = Json::Type::Number;
        v.n_ = d;
        return true;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }

    bool hex4(unsigned& cp) {
        if (p_ + 4 > t_.size()) return fail("bad \\u escape");
        cp = 0;
        for (int i = 0; i < 4; ++i) {
            char h = t_[p_++];
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= h - '0';
            else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
            else return fail("bad \\u escape");
        }
        return true;
    }

    bool string(std::string& out) {
        ++p_; // opening quote
        while (p_ < t_.size()) {
            char c = t_[p_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (p_ >= t_.size()) break;
            char e = t_[p_++];
            switch (e) {
                case '"': out += '"'; break;   case '\\': out += '\\'; break;
                case '/': out += '/'; break;   case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;  case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;  case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && t_.compare(p_, 2, "\\u") == 0) {
                        p_ += 2;
                        unsigned lo;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }

    bool array(Json& v) {
        v.type_ = Json::Type::Array;
        ++p_;
        ws();
        if (p_ < t_.size() && t_[p_] == ']') { ++p_; return true; }
        for (;;) {
            Json item;
            if (!value(item)) return false;
            v.arr_.push_back(std::move(item));
            ws();
            if (p_ < t_.size() && t_[p_] == ',') { ++p_; ws(); if (p_ < t_.size() && t_[p_] == ']') { ++p_; return true; } continue; }
            if (p_ < t_.size() && t_[p_] == ']') { ++p_; return true; }
            return fail("expected , or ]");
        }
    }

    bool object(Json& v) {
        v.type_ = Json::Type::Object;
        ++p_;
        ws();
        if (p_ < t_.size() && t_[p_] == '}') { ++p_; return true; }
        for (;;) {
            ws();
            if (p_ >= t_.size() || t_[p_] != '"') return fail("expected key");
            std::string key;
            if (!string(key)) return false;
            ws();
            if (p_ >= t_.size() || t_[p_] != ':') return fail("expected :");
            ++p_;
            Json item;
            if (!value(item)) return false;
            v.obj_[key] = std::move(item);
            ws();
            if (p_ < t_.size() && t_[p_] == ',') { ++p_; ws(); if (p_ < t_.size() && t_[p_] == '}') { ++p_; return true; } continue; }
            if (p_ < t_.size() && t_[p_] == '}') { ++p_; return true; }
            return fail("expected , or }");
        }
    }
};

Json Json::parse(const std::string& text, std::string& error) {
    error.clear();
    JsonParser p(text);
    return p.parseDocument(error);
}
