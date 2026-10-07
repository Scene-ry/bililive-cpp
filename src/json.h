// json.h - 极简 JSON 解析（仅覆盖 B 站接口返回所需的子集）
#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstdint>

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::vector<std::pair<std::string, JsonValue>> objectValue;

    // 解析完整 JSON 文档；语法错误返回 false
    static bool Parse(const std::string& text, JsonValue& out);

    // 对象成员查找（不存在或非对象返回 nullptr）
    const JsonValue* Find(const std::string& key) const;

    bool IsNull() const { return type == Type::Null; }

    std::string AsString(const std::string& def = "") const;
    double AsNumber(double def = 0.0) const;
    int64_t AsInt(int64_t def = 0) const;
    bool AsBool(bool def = false) const;
};
