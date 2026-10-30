#include "relay.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace json {

static const char BS = (char)92;
static const char DQ = (char)34;

static void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out.push_back((char)cp);
    } else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

static void appendEscaped(std::string& out, const std::string& s) {
    out.push_back(DQ);
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case (unsigned char)DQ:
            out.push_back(BS);
            out.push_back(DQ);
            break;
        case (unsigned char)BS:
            out.push_back(BS);
            out.push_back(BS);
            break;
        case 8:
            out.push_back(BS);
            out.push_back('b');
            break;
        case 12:
            out.push_back(BS);
            out.push_back('f');
            break;
        case 10:
            out.push_back(BS);
            out.push_back('n');
            break;
        case 13:
            out.push_back(BS);
            out.push_back('r');
            break;
        case 9:
            out.push_back(BS);
            out.push_back('t');
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "u%04x", (unsigned)c);
                out.push_back(BS);
                out += buf;
            } else {
                out.push_back((char)c);
            }
        }
    }
    out.push_back(DQ);
}

static void appendIndent(std::string& out, int indent, int level) {
    if (indent <= 0) return;
    out.push_back(10);
    out.append((std::size_t)(indent * level), ' ');
}

Value::Value() : type_(Type::Null), b_(false), n_(0.0) {}

Value Value::null() { return Value(); }

Value Value::boolean(bool b) {
    Value v;
    v.type_ = Type::Bool;
    v.b_ = b;
    return v;
}

Value Value::number(double d) {
    Value v;
    v.type_ = Type::Number;
    v.n_ = d;
    return v;
}

Value Value::integer(long long x) { return number((double)x); }

Value Value::str(const std::string& s) {
    Value v;
    v.type_ = Type::String;
    v.s_ = s;
    return v;
}

Value Value::array() {
    Value v;
    v.type_ = Type::Array;
    return v;
}

Value Value::object() {
    Value v;
    v.type_ = Type::Object;
    return v;
}

Type Value::type() const { return type_; }
bool Value::isNull() const { return type_ == Type::Null; }
bool Value::isBool() const { return type_ == Type::Bool; }
bool Value::isNumber() const { return type_ == Type::Number; }
bool Value::isString() const { return type_ == Type::String; }
bool Value::isArray() const { return type_ == Type::Array; }
bool Value::isObject() const { return type_ == Type::Object; }

bool Value::asBool(bool def) const {
    if (type_ == Type::Bool) return b_;
    if (type_ == Type::Number) return n_ != 0.0;
    return def;
}

double Value::asNumber(double def) const {
    if (type_ == Type::Number) return n_;
    if (type_ == Type::Bool) return b_ ? 1.0 : 0.0;
    if (type_ == Type::String) {
        const char* p = s_.c_str();
        char* end = nullptr;
        double d = std::strtod(p, &end);
        if (end && end != p) return d;
    }
    return def;
}

long long Value::asInt(long long def) const { return (long long)asNumber((double)def); }

std::string Value::asString(const std::string& def) const {
    if (type_ == Type::String) return s_;
    if (type_ == Type::Number) {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%.10g", n_);
        return std::string(buf);
    }
    if (type_ == Type::Bool) return b_ ? "true" : "false";
    return def;
}

std::size_t Value::size() const {
    if (type_ == Type::Array) return arr_.size();
    if (type_ == Type::Object) return obj_.size();
    if (type_ == Type::String) return s_.size();
    return 0;
}

Value& Value::at(std::size_t i) {
    static Value dummy;
    if (type_ != Type::Array) return dummy;
    if (i >= arr_.size()) return dummy;
    return arr_[i];
}

const Value& Value::at(std::size_t i) const {
    static const Value dummy;
    if (type_ != Type::Array) return dummy;
    if (i >= arr_.size()) return dummy;
    return arr_[i];
}

void Value::push(Value v) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        arr_.clear();
    }
    arr_.push_back(std::move(v));
}

bool Value::has(const std::string& key) const {
    if (type_ != Type::Object) return false;
    for (std::size_t i = 0; i < obj_.size(); ++i) {
        if (obj_[i].first == key) return true;
    }
    return false;
}

Value& Value::operator[](const std::string& key) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        obj_.clear();
    }
    for (std::size_t i = 0; i < obj_.size(); ++i) {
        if (obj_[i].first == key) return obj_[i].second;
    }
    obj_.push_back(std::make_pair(key, Value()));
    return obj_.back().second;
}

const Value& Value::get(const std::string& key) const {
    static const Value dummy;
    if (type_ != Type::Object) return dummy;
    for (std::size_t i = 0; i < obj_.size(); ++i) {
        if (obj_[i].first == key) return obj_[i].second;
    }
    return dummy;
}

void Value::set(const std::string& key, Value v) { (*this)[key] = std::move(v); }

void Value::remove(const std::string& key) {
    if (type_ != Type::Object) return;
    for (std::size_t i = 0; i < obj_.size(); ++i) {
        if (obj_[i].first == key) {
            obj_.erase(obj_.begin() + (std::ptrdiff_t)i);
            return;
        }
    }
}

std::vector<std::string> Value::keys() const {
    std::vector<std::string> out;
    if (type_ != Type::Object) return out;
    out.reserve(obj_.size());
    for (std::size_t i = 0; i < obj_.size(); ++i) out.push_back(obj_[i].first);
    return out;
}

const std::vector<std::pair<std::string, Value>>& Value::items() const { return obj_; }

std::string Value::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

void Value::dumpTo(std::string& out, int indent, int level) const {
    switch (type_) {
    case Type::Null:
        out += "null";
        break;
    case Type::Bool:
        out += b_ ? "true" : "false";
        break;
    case Type::Number: {
        double d = n_;
        long long ll = (long long)d;
        if (d == (double)ll && std::fabs(d) < 1e15) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%lld", ll);
            out += buf;
        } else {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%.10g", d);
            out += buf;
        }
        break;
    }
    case Type::String:
        appendEscaped(out, s_);
        break;
    case Type::Array:
        if (arr_.empty()) {
            out += "[]";
            break;
        }
        out.push_back('[');
        for (std::size_t i = 0; i < arr_.size(); ++i) {
            if (i) out.push_back(',');
            appendIndent(out, indent, level + 1);
            arr_[i].dumpTo(out, indent, level + 1);
        }
        appendIndent(out, indent, level);
        out.push_back(']');
        break;
    case Type::Object:
        if (obj_.empty()) {
            out += "{}";
            break;
        }
        out.push_back('{');
        for (std::size_t i = 0; i < obj_.size(); ++i) {
            if (i) out.push_back(',');
            appendIndent(out, indent, level + 1);
            appendEscaped(out, obj_[i].first);
            out.push_back(':');
            if (indent > 0) out.push_back(' ');
            obj_[i].second.dumpTo(out, indent, level + 1);
        }
        appendIndent(out, indent, level);
        out.push_back('}');
        break;
    }
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s), i_(0) {}

    bool parse(Value& out) {
        skipWs();
        if (!parseValue(out)) return false;
        skipWs();
        if (i_ != s_.size()) {
            error_ = "trailing characters";
            return false;
        }
        return true;
    }

    const std::string& error() const { return error_; }

private:
    void skipWs() {
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (c == ' ' || c == 9 || c == 10 || c == 13) ++i_;
            else break;
        }
    }

    bool readHex4(unsigned& out) {
        if (i_ + 4 > s_.size()) {
            error_ = "short unicode escape";
            return false;
        }
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else {
                error_ = "bad unicode escape";
                return false;
            }
        }
        out = v;
        return true;
    }

    bool parseString(std::string& out) {
        if (i_ >= s_.size() || s_[i_] != DQ) {
            error_ = "expected string";
            return false;
        }
        ++i_;
        out.clear();
        while (i_ < s_.size()) {
            unsigned char c = (unsigned char)s_[i_++];
            if (c == (unsigned char)DQ) return true;
            if (c == (unsigned char)BS) {
                if (i_ >= s_.size()) {
                    error_ = "bad escape";
                    return false;
                }
                char e = s_[i_++];
                if (e == DQ) out.push_back(DQ);
                else if (e == BS) out.push_back(BS);
                else if (e == '/') out.push_back('/');
                else if (e == 'b') out.push_back(8);
                else if (e == 'f') out.push_back(12);
                else if (e == 'n') out.push_back(10);
                else if (e == 'r') out.push_back(13);
                else if (e == 't') out.push_back(9);
                else if (e == 'u') {
                    unsigned cp = 0;
                    if (!readHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 5 < s_.size() && s_[i_] == BS && s_[i_ + 1] == 'u') {
                        std::size_t save = i_;
                        i_ += 2;
                        unsigned lo = 0;
                        if (!readHex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                        } else {
                            i_ = save;
                        }
                    }
                    appendUtf8(out, cp);
                } else {
                    error_ = "bad escape";
                    return false;
                }
                continue;
            }
            out.push_back((char)c);
        }
        error_ = "unterminated string";
        return false;
    }

    bool parseNumber(Value& out) {
        std::size_t start = i_;
        if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
        while (i_ < s_.size()) {
            char c = s_[i_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') ++i_;
            else break;
        }
        if (i_ == start) {
            error_ = "bad number";
            return false;
        }
        std::string num = s_.substr(start, i_ - start);
        char* end = nullptr;
        double d = std::strtod(num.c_str(), &end);
        if (!end || end == num.c_str()) {
            error_ = "bad number";
            return false;
        }
        out = Value::number(d);
        return true;
    }

    bool parseValue(Value& out) {
        if (i_ >= s_.size()) {
            error_ = "unexpected end";
            return false;
        }
        char c = s_[i_];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == DQ) {
            std::string s;
            if (!parseString(s)) return false;
            out = Value::str(s);
            return true;
        }
        if (c == 't' && s_.compare(i_, 4, "true") == 0) {
            i_ += 4;
            out = Value::boolean(true);
            return true;
        }
        if (c == 'f' && s_.compare(i_, 5, "false") == 0) {
            i_ += 5;
            out = Value::boolean(false);
            return true;
        }
        if (c == 'n' && s_.compare(i_, 4, "null") == 0) {
            i_ += 4;
            out = Value::null();
            return true;
        }
        return parseNumber(out);
    }

    bool parseArray(Value& out) {
        ++i_;
        out = Value::array();
        skipWs();
        if (i_ < s_.size() && s_[i_] == ']') {
            ++i_;
            return true;
        }
        for (;;) {
            skipWs();
            Value item;
            if (!parseValue(item)) return false;
            out.push(item);
            skipWs();
            if (i_ >= s_.size()) {
                error_ = "unterminated array";
                return false;
            }
            if (s_[i_] == ',') {
                ++i_;
                continue;
            }
            if (s_[i_] == ']') {
                ++i_;
                return true;
            }
            error_ = "expected , or ]";
            return false;
        }
    }

    bool parseObject(Value& out) {
        ++i_;
        out = Value::object();
        skipWs();
        if (i_ < s_.size() && s_[i_] == '}') {
            ++i_;
            return true;
        }
        for (;;) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (i_ >= s_.size() || s_[i_] != ':') {
                error_ = "expected :";
                return false;
            }
            ++i_;
            skipWs();
            Value item;
            if (!parseValue(item)) return false;
            out.set(key, item);
            skipWs();
            if (i_ >= s_.size()) {
                error_ = "unterminated object";
                return false;
            }
            if (s_[i_] == ',') {
                ++i_;
                continue;
            }
            if (s_[i_] == '}') {
                ++i_;
                return true;
            }
            error_ = "expected , or }";
            return false;
        }
    }

    const std::string& s_;
    std::size_t i_;
    std::string error_;
};

} // namespace

bool Value::parse(const std::string& text, Value& out, std::string* err) {
    Parser p(text);
    if (!p.parse(out)) {
        if (err) *err = p.error();
        return false;
    }
    return true;
}

} // namespace json
