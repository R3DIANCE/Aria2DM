#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void AppendUtf8(std::string& out, uint32_t cp) {
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

void AppendEscaped(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back((char)c);
            }
        }
    }
    out.push_back('"');
}

class Parser {
public:
    Parser(const std::string& text, std::string& error) : text_(text), error_(error) {}

    bool Parse(Json& out) {
        SkipWhitespace();
        if (!ParseValue(out)) return false;
        SkipWhitespace();
        if (pos_ != text_.size()) return Fail("trailing characters after JSON value");
        return true;
    }

private:
    const std::string& text_;
    std::string& error_;
    size_t pos_ = 0;
    int depth_ = 0;

    bool Fail(const char* message) {
        if (error_.empty()) error_ = message;
        return false;
    }

    void SkipWhitespace() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos_;
            else
                break;
        }
    }

    bool ParseValue(Json& out) {
        if (++depth_ > 256) return Fail("JSON nesting too deep");
        SkipWhitespace();
        if (pos_ >= text_.size()) return Fail("unexpected end of JSON");
        bool ok = false;
        char c = text_[pos_];
        if (c == '{') {
            ok = ParseObject(out);
        } else if (c == '[') {
            ok = ParseArray(out);
        } else if (c == '"') {
            std::string s;
            ok = ParseString(s);
            if (ok) out = Json(std::move(s));
        } else if (c == 't') {
            ok = Match("true");
            if (ok) out = Json(true);
        } else if (c == 'f') {
            ok = Match("false");
            if (ok) out = Json(false);
        } else if (c == 'n') {
            ok = Match("null");
            if (ok) out = Json();
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            ok = ParseNumber(out);
        } else {
            ok = Fail("unexpected character in JSON");
        }
        --depth_;
        return ok;
    }

    bool Match(const char* literal) {
        size_t len = std::strlen(literal);
        if (text_.compare(pos_, len, literal) != 0) return Fail("invalid JSON literal");
        pos_ += len;
        return true;
    }

    bool ParseObject(Json& out) {
        ++pos_; // {
        out = Json::Object();
        SkipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != '"') return Fail("expected object key");
            std::string key;
            if (!ParseString(key)) return false;
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != ':') return Fail("expected ':' in object");
            ++pos_;
            Json value;
            if (!ParseValue(value)) return false;
            out[key] = std::move(value);
            SkipWhitespace();
            if (pos_ >= text_.size()) return Fail("unterminated object");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return Fail("expected ',' or '}' in object");
        }
    }

    bool ParseArray(Json& out) {
        ++pos_; // [
        out = Json::Array();
        SkipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            Json value;
            if (!ParseValue(value)) return false;
            out.push(std::move(value));
            SkipWhitespace();
            if (pos_ >= text_.size()) return Fail("unterminated array");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return Fail("expected ',' or ']' in array");
        }
    }

    bool ParseString(std::string& out) {
        if (pos_ >= text_.size() || text_[pos_] != '"') return Fail("expected string");
        ++pos_;
        out.clear();
        while (pos_ < text_.size()) {
            unsigned char c = (unsigned char)text_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= text_.size()) return Fail("unterminated escape");
                char e = text_[pos_++];
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
                    uint32_t cp = 0;
                    if (!ParseHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        uint32_t low = 0;
                        if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            if (!ParseHex4(low)) return false;
                            if (low >= 0xDC00 && low <= 0xDFFF)
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    return Fail("invalid escape in string");
                }
            } else {
                out.push_back((char)c);
            }
        }
        return Fail("unterminated string");
    }

    bool ParseHex4(uint32_t& value) {
        if (pos_ + 4 > text_.size()) return Fail("truncated unicode escape");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= (uint32_t)(c - 'A' + 10);
            else
                return Fail("invalid unicode escape");
        }
        return true;
    }

    bool ParseNumber(Json& out) {
        size_t start = pos_;
        if (text_[pos_] == '-') ++pos_;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        std::string token = text_.substr(start, pos_ - start);
        if (token.empty() || token == "-") return Fail("invalid number");
        char* end = nullptr;
        double value = std::strtod(token.c_str(), &end);
        if (!end || *end != '\0') return Fail("invalid number");
        out = Json(value);
        return true;
    }
};

} // namespace

std::string Json::dump() const {
    std::string out;
    DumpInto(out);
    return out;
}

void Json::DumpInto(std::string& out) const {
    switch (type_) {
    case Type::Null:
        out += "null";
        break;
    case Type::Bool:
        out += bool_ ? "true" : "false";
        break;
    case Type::Number:
        if (std::isfinite(number_) && std::floor(number_) == number_ && std::fabs(number_) < 9.0e15) {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%lld", (long long)number_);
            out += buf;
        } else {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%.17g", number_);
            out += buf;
        }
        break;
    case Type::String:
        AppendEscaped(out, string_);
        break;
    case Type::Array: {
        out.push_back('[');
        for (size_t i = 0; i < array_.size(); ++i) {
            if (i) out.push_back(',');
            array_[i].DumpInto(out);
        }
        out.push_back(']');
        break;
    }
    case Type::Object: {
        out.push_back('{');
        bool first = true;
        for (const auto& kv : object_) {
            if (!first) out.push_back(',');
            first = false;
            AppendEscaped(out, kv.first);
            out.push_back(':');
            kv.second.DumpInto(out);
        }
        out.push_back('}');
        break;
    }
    }
}

Json Json::Parse(const std::string& text, std::string* error) {
    std::string localError;
    Parser parser(text, localError);
    Json result;
    if (!parser.Parse(result)) {
        if (error) *error = localError;
        return Json();
    }
    if (error) error->clear();
    return result;
}
