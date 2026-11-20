#include "parse_context.hpp"

#include <cctype>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace hybridlang {
namespace {

constexpr std::size_t kMaxSourceSize = 1'000'000;
constexpr std::size_t kMaxStringSize = 100'000;

} // namespace

ParseContext::ParseContext(std::string source) : source_(std::move(source)) {
    if (source_.size() > kMaxSourceSize) {
        throw std::runtime_error("source exceeds the 1 MB input limit");
    }
}

BisonParser::symbol_type ParseContext::lex() {
    skipWhitespaceAndComments();
    const auto startLine = line_;
    const auto startColumn = column_;
    if (atEnd()) {
        return BisonParser::make_END(location(startLine, startColumn));
    }

    const auto current = peek();
    if (std::isalpha(static_cast<unsigned char>(current)) || current == '_') {
        const auto text = scanIdentifier();
        static const std::unordered_map<std::string, int> keywords{
            {"let", BisonParser::token::LET},
            {"var", BisonParser::token::VAR},
            {"const", BisonParser::token::CONST},
            {"int", BisonParser::token::INT_TYPE},
            {"long", BisonParser::token::LONG_TYPE},
            {"float", BisonParser::token::FLOAT_TYPE},
            {"double", BisonParser::token::DOUBLE_TYPE},
            {"bool", BisonParser::token::BOOL_TYPE},
            {"string", BisonParser::token::STRING_TYPE},
            {"auto", BisonParser::token::AUTO_TYPE},
            {"if", BisonParser::token::IF},
            {"else", BisonParser::token::ELSE},
            {"while", BisonParser::token::WHILE},
            {"true", BisonParser::token::TRUE},
            {"false", BisonParser::token::FALSE},
            {"and", BisonParser::token::AND},
            {"or", BisonParser::token::OR},
        };
        const auto keyword = keywords.find(text);
        if (keyword != keywords.end()) {
            const auto loc = location(startLine, startColumn);
            switch (keyword->second) {
            case BisonParser::token::LET: return BisonParser::make_LET(loc);
            case BisonParser::token::VAR: return BisonParser::make_VAR(loc);
            case BisonParser::token::CONST: return BisonParser::make_CONST(loc);
            case BisonParser::token::INT_TYPE: return BisonParser::make_INT_TYPE(loc);
            case BisonParser::token::LONG_TYPE: return BisonParser::make_LONG_TYPE(loc);
            case BisonParser::token::FLOAT_TYPE: return BisonParser::make_FLOAT_TYPE(loc);
            case BisonParser::token::DOUBLE_TYPE: return BisonParser::make_DOUBLE_TYPE(loc);
            case BisonParser::token::BOOL_TYPE: return BisonParser::make_BOOL_TYPE(loc);
            case BisonParser::token::STRING_TYPE: return BisonParser::make_STRING_TYPE(loc);
            case BisonParser::token::AUTO_TYPE: return BisonParser::make_AUTO_TYPE(loc);
            case BisonParser::token::IF: return BisonParser::make_IF(loc);
            case BisonParser::token::ELSE: return BisonParser::make_ELSE(loc);
            case BisonParser::token::WHILE: return BisonParser::make_WHILE(loc);
            case BisonParser::token::TRUE: return BisonParser::make_TRUE(loc);
            case BisonParser::token::FALSE: return BisonParser::make_FALSE(loc);
            case BisonParser::token::AND: return BisonParser::make_AND(loc);
            case BisonParser::token::OR: return BisonParser::make_OR(loc);
            default: break;
            }
        }
        return BisonParser::make_IDENT(text, location(startLine, startColumn));
    }

    if (std::isdigit(static_cast<unsigned char>(current))) {
        const auto text = scanNumber();
        try {
            if (text.find_first_of(".eE") != std::string::npos) {
                const auto value = std::stod(text);
                if (!std::isfinite(value)) {
                    report(location(startLine, startColumn), "float literal must be finite");
                }
                return BisonParser::make_REAL(value, location(startLine, startColumn));
            }
            return BisonParser::make_INTEGER(
                std::stoll(text),
                location(startLine, startColumn)
            );
        } catch (const std::exception&) {
            report(location(startLine, startColumn), "numeric literal is out of range");
        }
    }

    if (current == '"' || current == '\'') {
        const auto text = scanString();
        return BisonParser::make_STRING(text, location(startLine, startColumn));
    }

    const char first = advance();
    const auto loc = location(startLine, startColumn);
    switch (first) {
    case '=': return match('=') ? BisonParser::make_EQ(loc) : BisonParser::make_ASSIGN(loc);
    case '!': return match('=') ? BisonParser::make_NE(loc) : BisonParser::make_NOT(loc);
    case '<':
        if (match('=')) return BisonParser::make_LE(loc);
        if (match('<')) return BisonParser::make_SHL(loc);
        return BisonParser::make_LT(loc);
    case '>':
        if (match('=')) return BisonParser::make_GE(loc);
        if (match('>')) return BisonParser::make_SHR(loc);
        return BisonParser::make_GT(loc);
    case '&': return match('&') ? BisonParser::make_AND(loc) : BisonParser::make_BITAND(loc);
    case '|': return match('|') ? BisonParser::make_OR(loc) : BisonParser::make_BITOR(loc);
    case ':': return match(':') ? BisonParser::make_SCOPE(loc) : BisonParser::make_YYUNDEF(loc);
    case ';': return BisonParser::make_SEMICOLON(loc);
    case '(': return BisonParser::make_LPAREN(loc);
    case ')': return BisonParser::make_RPAREN(loc);
    case '{': return BisonParser::make_LBRACE(loc);
    case '}': return BisonParser::make_RBRACE(loc);
    case ',': return BisonParser::make_COMMA(loc);
    case '.': return BisonParser::make_DOT(loc);
    case '+': return BisonParser::make_PLUS(loc);
    case '-': return BisonParser::make_MINUS(loc);
    case '*': return BisonParser::make_STAR(loc);
    case '/': return BisonParser::make_SLASH(loc);
    case '%': return BisonParser::make_PERCENT(loc);
    case '^': return BisonParser::make_BITXOR(loc);
    case '~': return BisonParser::make_BITNOT(loc);
    default:
        report(loc, "unexpected character");
    }
}

void ParseContext::setStatements(StmtList statements) {
    statements_ = std::move(statements);
}

const StmtList& ParseContext::statements() const noexcept {
    return statements_;
}

[[noreturn]] void ParseContext::report(
    const BisonParser::location_type& location,
    const std::string& message
) const {
    throw std::runtime_error(
        "line " + std::to_string(location.begin.line) + ", column " +
        std::to_string(location.begin.column) + ": " + message
    );
}

bool ParseContext::atEnd() const noexcept {
    return offset_ >= source_.size();
}

char ParseContext::peek(std::size_t distance) const noexcept {
    const auto position = offset_ + distance;
    return position < source_.size() ? source_[position] : '\0';
}

char ParseContext::advance() noexcept {
    if (atEnd()) {
        return '\0';
    }
    const auto value = source_[offset_++];
    if (value == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return value;
}

bool ParseContext::match(char expected) {
    if (atEnd() || peek() != expected) {
        return false;
    }
    advance();
    return true;
}

void ParseContext::skipWhitespaceAndComments() {
    for (;;) {
        if (atEnd()) {
            return;
        }
        if (std::isspace(static_cast<unsigned char>(peek()))) {
            advance();
            continue;
        }
        if (peek() == '/' && peek(1) == '/') {
            while (!atEnd() && peek() != '\n') {
                advance();
            }
            continue;
        }
        if (peek() == '/' && peek(1) == '*') {
            const auto startLine = line_;
            const auto startColumn = column_;
            advance();
            advance();
            while (!atEnd() && !(peek() == '*' && peek(1) == '/')) {
                advance();
            }
            if (atEnd()) {
                report(location(startLine, startColumn), "unterminated block comment");
            }
            advance();
            advance();
            continue;
        }
        return;
    }
}

std::string ParseContext::scanIdentifier() {
    const auto start = offset_;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
        advance();
    }
    return source_.substr(start, offset_ - start);
}

std::string ParseContext::scanNumber() {
    const auto start = offset_;
    while (std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }
    if (peek() == 'e' || peek() == 'E') {
        advance();
        if (peek() == '+' || peek() == '-') {
            advance();
        }
        if (!std::isdigit(static_cast<unsigned char>(peek()))) {
            report(location(line_, column_), "malformed numeric exponent");
        }
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }
    return source_.substr(start, offset_ - start);
}

std::string ParseContext::scanString() {
    const auto startLine = line_;
    const auto startColumn = column_;
    const char delimiter = advance();
    std::string result;
    while (!atEnd() && peek() != delimiter) {
        if (peek() == '\n') {
            report(location(startLine, startColumn), "newline in string literal");
        }
        if (match('\\')) {
            if (atEnd()) {
                report(location(startLine, startColumn), "unterminated string escape");
            }
            const auto escaped = advance();
            switch (escaped) {
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case '\\': result.push_back('\\'); break;
            case '\'': result.push_back('\''); break;
            case '"': result.push_back('"'); break;
            default: report(location(line_, column_), "unsupported string escape");
            }
        } else {
            result.push_back(advance());
        }
        if (result.size() > kMaxStringSize) {
            report(location(startLine, startColumn), "string exceeds the 100000-character limit");
        }
    }
    if (!match(delimiter)) {
        report(location(startLine, startColumn), "unterminated string literal");
    }
    return result;
}

BisonParser::location_type ParseContext::location(
    std::size_t line,
    std::size_t column
) const {
    BisonParser::location_type result;
    result.begin.line = static_cast<int>(line);
    result.begin.column = static_cast<int>(column);
    result.end = result.begin;
    return result;
}

} // namespace hybridlang
