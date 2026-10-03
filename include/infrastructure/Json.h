#pragma once
// Minimal JSON value/parser/serializer (no external dependency).
// Supports null, bool, integers, strings, arrays, objects. Numbers with fractions are rejected.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "domain/Result.h"

namespace wallet {

class Json {
public:
    enum class Type { Null, Bool, Int, String, Array, Object };
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;

    Json() = default;
    Json(bool b) : type_(Type::Bool), b_(b) {}
    Json(int v) : type_(Type::Int), i_(v) {}
    Json(std::int64_t v) : type_(Type::Int), i_(v) {}
    Json(const char* s) : type_(Type::String), s_(s) {}
    Json(std::string s) : type_(Type::String), s_(std::move(s)) {}
    Json(Array a) : type_(Type::Array), a_(std::move(a)) {}
    Json(Object o) : type_(Type::Object), o_(std::move(o)) {}

    Type type() const noexcept { return type_; }
    bool isObject() const noexcept { return type_ == Type::Object; }
    bool isArray() const noexcept { return type_ == Type::Array; }

    // Typed accessors with defaults: robust against missing/mistyped fields in hand-edited files.
    std::string str(const std::string& key, const std::string& def = "") const;
    std::int64_t integer(const std::string& key, std::int64_t def = 0) const;
    bool has(const std::string& key) const;
    const Json* find(const std::string& key) const;
    const Array& array() const { return a_; }
    const Object& object() const { return o_; }
    const std::string& asString() const { return s_; }
    std::int64_t asInt() const { return i_; }

    std::string dump(int indent = 0) const;
    static Result<Json> parse(const std::string& text);

private:
    void dumpTo(std::string& out, int indent, int level) const;
    Type type_ = Type::Null;
    bool b_ = false;
    std::int64_t i_ = 0;
    std::string s_;
    Array a_;
    Object o_;
};

} // namespace wallet
