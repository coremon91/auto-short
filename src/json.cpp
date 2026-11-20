#include "json.hpp"

#include <cctype>
#include <cstdint>
#include <limits>

namespace xdcam {
namespace {

class Parser {
   public:
    explicit Parser(std::string_view text) : text_(text) {
        if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB && static_cast<unsigned char>(text_[2]) == 0xBF) {
            index_ = 3;
        }
    }

    Value Parse() {
        Skip();
        Value value = ParseValue();
        Skip();
        if (index_ != text_.size()) {
            Fail("trailing data");
        }
        return value;
    }

   private:
    std::string_view text_;
    std::size_t index_ = 0;

    [[noreturn]] void Fail(const std::string& message) const {
        throw JsonError(message + " at offset " + std::to_string(index_));
    }

    void Skip() {
        while (index_ < text_.size()) {
            const unsigned char ch = static_cast<unsigned char>(text_[index_]);
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                break;
            }
            ++index_;
        }
    }

    char Peek() const {
        if (index_ >= text_.size()) {
            Fail("unexpected end");
        }
        return text_[index_];
    }

    char Get() {
        const char ch = Peek();
        ++index_;
        return ch;
    }

    bool Consume(char expected) {
        Skip();
        if (index_ < text_.size() && text_[index_] == expected) {
            ++index_;
            return true;
        }
        return false;
    }

    Value ParseValue() {
        Skip();
        const char ch = Peek();
        if (ch == '{') {
            return ParseObject();
        }
        if (ch == '[') {
            return ParseArray();
        }
        if (ch == '"') {
            Value value;
            value.type = Value::Type::String;
            value.s = ParseString();
            return value;
        }
        if (ch == 't' || ch == 'f') {
            return ParseBool();
        }
        if (ch == 'n') {
            return ParseNull();
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
            return ParseNumber();
        }
        Fail("invalid value");
    }

    Value ParseObject() {
        Get();
        Value value;
        value.type = Value::Type::Object;
        Skip();
        if (Consume('}')) {
            return value;
        }
        while (true) {
            Skip();
            if (Peek() != '"') {
                Fail("object key must be a string");
            }
            const std::string key = ParseString();
            for (const auto& entry : value.o) {
                if (entry.first == key) {
                    Fail("duplicate key");
                }
            }
            Skip();
            if (Get() != ':') {
                Fail("expected ':'");
            }
            value.o.emplace_back(key, ParseValue());
            Skip();
            if (Consume('}')) {
                break;
            }
            if (!Consume(',')) {
                Fail("expected ',' or '}'");
            }
            Skip();
            if (Peek() == '}') {
                Fail("trailing comma");
            }
        }
        return value;
    }

    Value ParseArray() {
        Get();
        Value value;
        value.type = Value::Type::Array;
        Skip();
        if (Consume(']')) {
            return value;
        }
        while (true) {
            value.a.push_back(ParseValue());
            Skip();
            if (Consume(']')) {
                break;
            }
            if (!Consume(',')) {
                Fail("expected ',' or ']'");
            }
            Skip();
            if (Peek() == ']') {
                Fail("trailing comma");
            }
        }
        return value;
    }

    std::string ParseString() {
        if (Get() != '"') {
            Fail("expected string");
        }
        std::string out;
        while (true) {
            if (index_ >= text_.size()) {
                Fail("unterminated string");
            }
            const char ch = Get();
            if (ch == '"') {
                break;
            }
            if (static_cast<unsigned char>(ch) < 0x20) {
                Fail("raw control character in string");
            }
            if (ch != '\\') {
                out.push_back(ch);
                continue;
            }
            if (index_ >= text_.size()) {
                Fail("unterminated escape");
            }
            const char esc = Get();
            switch (esc) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(esc);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u':
                    AppendUtf8(out, ParseHex4());
                    break;
                default:
                    Fail("invalid escape");
            }
        }
        return out;
    }

    int ParseHex4() {
        int code = 0;
        for (int n = 0; n < 4; ++n) {
            if (index_ >= text_.size()) {
                Fail("short unicode escape");
            }
            const char ch = Get();
            code <<= 4;
            if (ch >= '0' && ch <= '9') {
                code += ch - '0';
            } else if (ch >= 'a' && ch <= 'f') {
                code += ch - 'a' + 10;
            } else if (ch >= 'A' && ch <= 'F') {
                code += ch - 'A' + 10;
            } else {
                Fail("invalid unicode escape");
            }
        }
        if (code >= 0xD800 && code <= 0xDFFF) {
            Fail("unicode surrogate is not supported");
        }
        return code;
    }

    static void AppendUtf8(std::string& out, int code) {
        if (code < 0 || code > 0x10FFFF) {
            throw JsonError("invalid unicode code point");
        }
        if (code <= 0x7F) {
            out.push_back(static_cast<char>(code));
        } else if (code <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    Value ParseBool() {
        Value value;
        value.type = Value::Type::Bool;
        if (text_.substr(index_, 4) == "true") {
            index_ += 4;
            value.b = true;
            return value;
        }
        if (text_.substr(index_, 5) == "false") {
            index_ += 5;
            value.b = false;
            return value;
        }
        Fail("invalid boolean");
    }

    Value ParseNull() {
        if (text_.substr(index_, 4) != "null") {
            Fail("invalid null");
        }
        index_ += 4;
        Value value;
        value.type = Value::Type::Null;
        return value;
    }

    Value ParseNumber() {
        const bool neg = Peek() == '-';
        if (neg) {
            Get();
        }
        if (index_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[index_]))) {
            Fail("invalid number");
        }
        if (text_[index_] == '0') {
            Get();
            if (index_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[index_]))) {
                Fail("leading zero");
            }
            if (neg) {
                Fail("negative zero");
            }
            RejectFraction();
            Value value;
            value.type = Value::Type::Int;
            value.i = 0;
            return value;
        }
        int64_t acc = 0;
        while (index_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[index_]))) {
            const int digit = text_[index_] - '0';
            if (acc > (std::numeric_limits<int64_t>::max() - digit) / 10) {
                Fail("integer overflow");
            }
            acc = acc * 10 + digit;
            ++index_;
        }
        RejectFraction();
        Value value;
        value.type = Value::Type::Int;
        value.i = neg ? -acc : acc;
        return value;
    }

    void RejectFraction() {
        if (index_ < text_.size()) {
            const char ch = text_[index_];
            if (ch == '.' || ch == 'e' || ch == 'E') {
                Fail("only integers are accepted");
            }
        }
    }
};

}  // namespace

Value ParseJson(std::string_view text) {
    Parser parser(text);
    return parser.Parse();
}

}  // namespace xdcam
