// json.cpp - 极简 JSON 解析实现
#include "json.h"

#include <cstdlib>
#include <cstdio>
#include <cmath>

namespace {

// 追加一个 Unicode 码点的 UTF-8 编码
void AppendUtf8(std::string& out, unsigned cp) {
    if (cp <= 0x7F) {
        out.push_back((char)cp);
    } else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
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

struct Parser {
    const char* p;
    const char* end;

    bool skipWs() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
        return p < end;
    }

    bool parseValue(JsonValue& v) {
        if (!skipWs()) return false;
        switch (*p) {
            case '{': return parseObject(v);
            case '[': return parseArray(v);
            case '"':
                v.type = JsonValue::Type::String;
                return parseString(v.stringValue);
            case 't':
                if (end - p >= 4 && std::string(p, p + 4) == "true") {
                    v.type = JsonValue::Type::Bool;
                    v.boolValue = true;
                    p += 4;
                    return true;
                }
                return false;
            case 'f':
                if (end - p >= 5 && std::string(p, p + 5) == "false") {
                    v.type = JsonValue::Type::Bool;
                    v.boolValue = false;
                    p += 5;
                    return true;
                }
                return false;
            case 'n':
                if (end - p >= 4 && std::string(p, p + 4) == "null") {
                    v.type = JsonValue::Type::Null;
                    p += 4;
                    return true;
                }
                return false;
            default:
                return parseNumber(v);
        }
    }

    bool parseHex4(unsigned& out) {
        if (end - p < 4) return false;
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = p[i];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        p += 4;
        out = v;
        return true;
    }

    bool parseString(std::string& out) {
        out.clear();
        if (p >= end || *p != '"') return false;
        ++p;
        while (p < end) {
            unsigned char c = (unsigned char)*p++;
            if (c == '"') return true;
            if (c == '\\') {
                if (p >= end) return false;
                char e = *p++;
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
                        unsigned cp = 0;
                        if (!parseHex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 2 && p[0] == '\\' && p[1] == 'u') {
                            const char* save = p;
                            p += 2;
                            unsigned lo = 0;
                            if (parseHex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            } else {
                                p = save;  // 非合法代理对，按单码元处理
                            }
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                    default: return false;
                }
            } else {
                out.push_back((char)c);
            }
        }
        return false;
    }

    bool parseNumber(JsonValue& v) {
        const char* start = p;
        if (p < end && (*p == '-' || *p == '+')) ++p;
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' ||
                           *p == '+' || *p == '-')) {
            ++p;
        }
        if (p == start) return false;
        std::string num(start, p);
        char* endp = nullptr;
        double d = strtod(num.c_str(), &endp);
        if (!endp || endp == num.c_str()) return false;
        v.type = JsonValue::Type::Number;
        v.numberValue = d;
        return true;
    }

    bool parseObject(JsonValue& v) {
        if (*p != '{') return false;
        ++p;
        v.type = JsonValue::Type::Object;
        if (!skipWs()) return false;
        if (*p == '}') { ++p; return true; }
        for (;;) {
            if (!skipWs()) return false;
            std::string key;
            if (!parseString(key)) return false;
            if (!skipWs() || *p != ':') return false;
            ++p;
            v.objectValue.emplace_back();
            v.objectValue.back().first = std::move(key);
            if (!parseValue(v.objectValue.back().second)) return false;
            if (!skipWs()) return false;
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; return true; }
            return false;
        }
    }

    bool parseArray(JsonValue& v) {
        if (*p != '[') return false;
        ++p;
        v.type = JsonValue::Type::Array;
        if (!skipWs()) return false;
        if (*p == ']') { ++p; return true; }
        for (;;) {
            v.arrayValue.emplace_back();
            if (!parseValue(v.arrayValue.back())) return false;
            if (!skipWs()) return false;
            if (*p == ',') { ++p; continue; }
            if (*p == ']') { ++p; return true; }
            return false;
        }
    }
};

}  // namespace

bool JsonValue::Parse(const std::string& text, JsonValue& out) {
    Parser parser{text.data(), text.data() + text.size()};
    out = JsonValue();
    if (!parser.parseValue(out)) return false;
    parser.skipWs();
    return parser.p == parser.end;
}

const JsonValue* JsonValue::Find(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& kv : objectValue) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

std::string JsonValue::AsString(const std::string& def) const {
    switch (type) {
        case Type::String:
            return stringValue;
        case Type::Number: {
            char buf[64];
            double d = numberValue;
            if (d == (double)(int64_t)d && std::fabs(d) < 9.0e15) {
                snprintf(buf, sizeof(buf), "%lld", (long long)(int64_t)d);
            } else {
                snprintf(buf, sizeof(buf), "%.17g", d);
            }
            return std::string(buf);
        }
        case Type::Bool:
            return boolValue ? "true" : "false";
        default:
            return def;
    }
}

double JsonValue::AsNumber(double def) const {
    if (type == Type::Number) return numberValue;
    if (type == Type::String) {
        char* endp = nullptr;
        double d = strtod(stringValue.c_str(), &endp);
        if (endp && endp != stringValue.c_str()) return d;
    }
    if (type == Type::Bool) return boolValue ? 1.0 : 0.0;
    return def;
}

int64_t JsonValue::AsInt(int64_t def) const {
    if (type == Type::Number) return (int64_t)numberValue;
    if (type == Type::Bool) return boolValue ? 1 : 0;
    if (type == Type::String) {
        char* endp = nullptr;
        long long v = strtoll(stringValue.c_str(), &endp, 10);
        if (endp && endp != stringValue.c_str()) return (int64_t)v;
    }
    return def;
}

bool JsonValue::AsBool(bool def) const {
    if (type == Type::Bool) return boolValue;
    if (type == Type::Number) return numberValue != 0.0;
    return def;
}
