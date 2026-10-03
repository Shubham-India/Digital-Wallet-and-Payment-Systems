#include "infrastructure/Json.h"

#include <cctype>
#include <cstdio>

namespace wallet {

const Json* Json::find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    auto it = o_.find(key);
    return it == o_.end() ? nullptr : &it->second;
}
bool Json::has(const std::string& key) const { return find(key) != nullptr; }
std::string Json::str(const std::string& key, const std::string& def) const {
    const Json* j = find(key);
    return (j && j->type_ == Type::String) ? j->s_ : def;
}
std::int64_t Json::integer(const std::string& key, std::int64_t def) const {
    const Json* j = find(key);
    return (j && j->type_ == Type::Int) ? j->i_ : def;
}

static void escapeTo(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void Json::dumpTo(std::string& out, int indent, int level) const {
    auto newline = [&](int lvl) {
        if (indent > 0) { out += '\n'; out.append(static_cast<std::size_t>(indent * lvl), ' '); }
    };
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += b_ ? "true" : "false"; break;
        case Type::Int: out += std::to_string(i_); break;
        case Type::String: escapeTo(out, s_); break;
        case Type::Array:
            out += '[';
            for (std::size_t i = 0; i < a_.size(); ++i) {
                if (i) out += ',';
                newline(level + 1);
                a_[i].dumpTo(out, indent, level + 1);
            }
            if (!a_.empty()) newline(level);
            out += ']';
            break;
        case Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& [k, v] : o_) {
                if (!first) out += ',';
                first = false;
                newline(level + 1);
                escapeTo(out, k);
                out += indent > 0 ? ": " : ":";
                v.dumpTo(out, indent, level + 1);
            }
            if (!o_.empty()) newline(level);
            out += '}';
            break;
        }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

namespace {
class Parser {
public:
    explicit Parser(const std::string& t) : t_(t) {}
    Result<Json> run() {
        auto v = value(0);
        if (!v) return v;
        skip();
        if (pos_ != t_.size()) return err("Unexpected trailing characters");
        return v;
    }
private:
    Failure err(const std::string& m) const {
        return fail(ErrorCode::PersistenceFailure, "JSON parse error at offset " + std::to_string(pos_) + ": " + m);
    }
    void skip() { while (pos_ < t_.size() && std::isspace(static_cast<unsigned char>(t_[pos_]))) ++pos_; }
    bool eat(char c) { skip(); if (pos_ < t_.size() && t_[pos_] == c) { ++pos_; return true; } return false; }
    bool literal(const char* word) {
        std::size_t n = std::char_traits<char>::length(word);
        if (t_.compare(pos_, n, word) == 0) { pos_ += n; return true; }
        return false;
    }

    Result<Json> value(int depth) {
        if (depth > 64) return err("Nesting too deep");
        skip();
        if (pos_ >= t_.size()) return err("Unexpected end of input");
        char c = t_[pos_];
        if (c == '{') return object(depth);
        if (c == '[') return arrayValue(depth);
        if (c == '"') { auto s = string(); if (!s) return fail(s.error()); return Json(s.value()); }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return number();
        if (literal("true")) return Json(true);
        if (literal("false")) return Json(false);
        if (literal("null")) return Json();
        return err("Unexpected character");
    }

    Result<Json> number() {
        std::size_t start = pos_;
        if (t_[pos_] == '-') ++pos_;
        while (pos_ < t_.size() && std::isdigit(static_cast<unsigned char>(t_[pos_]))) ++pos_;
        if (pos_ < t_.size() && (t_[pos_] == '.' || t_[pos_] == 'e' || t_[pos_] == 'E')) return err("Only integers are supported");
        std::string tok = t_.substr(start, pos_ - start);
        if (tok == "-" || tok.empty() || tok.size() > 18) return err("Bad number");
        return Json(static_cast<std::int64_t>(std::stoll(tok)));
    }

    Result<std::string> string() {
        ++pos_;  // opening quote
        std::string out;
        while (pos_ < t_.size()) {
            char c = t_[pos_++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= t_.size()) break;
            char e = t_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (pos_ + 4 > t_.size()) return err("Bad unicode escape");
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = t_[pos_++];
                        if (!std::isxdigit(static_cast<unsigned char>(h))) return err("Bad unicode escape");
                        code = code * 16 + static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(h)) ? h - '0' : (std::tolower(h) - 'a' + 10));
                    }
                    if (code < 0x80) out += static_cast<char>(code);
                    else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
                    else { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
                    break;
                }
                default: return err("Bad escape");
            }
        }
        return err("Unterminated string");
    }

    Result<Json> arrayValue(int depth) {
        ++pos_;
        Json::Array arr;
        if (eat(']')) return Json(std::move(arr));
        while (true) {
            auto v = value(depth + 1);
            if (!v) return v;
            arr.push_back(std::move(v).value());
            if (eat(',')) continue;
            if (eat(']')) return Json(std::move(arr));
            return err("Expected , or ]");
        }
    }

    Result<Json> object(int depth) {
        ++pos_;
        Json::Object obj;
        if (eat('}')) return Json(std::move(obj));
        while (true) {
            skip();
            if (pos_ >= t_.size() || t_[pos_] != '"') return err("Expected string key");
            auto k = string();
            if (!k) return fail(k.error());
            if (!eat(':')) return err("Expected :");
            auto v = value(depth + 1);
            if (!v) return v;
            obj.emplace(std::move(k).value(), std::move(v).value());
            if (eat(',')) continue;
            if (eat('}')) return Json(std::move(obj));
            return err("Expected , or }");
        }
    }

    const std::string& t_;
    std::size_t pos_ = 0;
};
}  // namespace

Result<Json> Json::parse(const std::string& text) { return Parser(text).run(); }

} // namespace wallet
