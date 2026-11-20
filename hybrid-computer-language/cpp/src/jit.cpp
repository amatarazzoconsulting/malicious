#include "hybridlang/jit.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>

namespace hybridlang {
namespace {

constexpr std::size_t kMaxSourceSize = 1'000'000;
constexpr std::size_t kMaxBinaryInstructions = 1'000'000;
constexpr std::size_t kMaxStringSize = 100'000;
constexpr std::size_t kMaxOutputSize = 1'000'000;
constexpr std::size_t kMaxStackSize = 65'536;
constexpr std::size_t kMaxCallArguments = 1'024;
constexpr std::size_t kMaxParseDepth = 128;
constexpr std::uint32_t kBinaryVersion = 1;
constexpr char kBinaryMagic[] = {'H', 'Y', 'B', 'C'};

bool isNumeric(const Value& value) {
    return std::holds_alternative<std::int64_t>(value) ||
           std::holds_alternative<double>(value);
}

bool isInteger(const Value& value) {
    return std::holds_alternative<std::int64_t>(value);
}

void validateValue(const Value& value) {
    if (const auto* number = std::get_if<double>(&value)) {
        if (!std::isfinite(*number)) {
            throw std::runtime_error("non-finite floating-point value");
        }
    } else if (const auto* string = std::get_if<std::string>(&value)) {
        if (string->size() > kMaxStringSize) {
            throw std::runtime_error("string exceeds the 100000-character limit");
        }
    } else if (const auto* object = std::get_if<ObjectPtr>(&value)) {
        if (!*object) {
            throw std::runtime_error("null host object");
        }
    }
}

bool truthy(const Value& value) {
    if (const auto* number = std::get_if<std::int64_t>(&value)) {
        return *number != 0;
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return *number != 0.0;
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean;
    }
    if (const auto* string = std::get_if<std::string>(&value)) {
        return !string->empty();
    }
    if (const auto* object = std::get_if<ObjectPtr>(&value)) {
        return static_cast<bool>(*object);
    }
    return false;
}

std::string bindingKey(const std::string& className, const std::string& memberName) {
    return className + '\n' + memberName;
}

std::int64_t checkedAdd(std::int64_t left, std::int64_t right) {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
        throw std::runtime_error("integer addition overflow");
    }
    return left + right;
}

std::int64_t checkedSubtract(std::int64_t left, std::int64_t right) {
    if ((right < 0 && left > std::numeric_limits<std::int64_t>::max() + right) ||
        (right > 0 && left < std::numeric_limits<std::int64_t>::min() + right)) {
        throw std::runtime_error("integer subtraction overflow");
    }
    return left - right;
}

std::int64_t checkedMultiply(std::int64_t left, std::int64_t right) {
    if (left == 0 || right == 0) {
        return 0;
    }
    if ((left == -1 && right == std::numeric_limits<std::int64_t>::min()) ||
        (right == -1 && left == std::numeric_limits<std::int64_t>::min())) {
        throw std::runtime_error("integer multiplication overflow");
    }
    if (left > 0) {
        if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() / right) ||
            (right < 0 && right < std::numeric_limits<std::int64_t>::min() / left)) {
            throw std::runtime_error("integer multiplication overflow");
        }
    } else if ((right > 0 && left < std::numeric_limits<std::int64_t>::min() / right) ||
               (right < 0 && left < std::numeric_limits<std::int64_t>::max() / right)) {
        throw std::runtime_error("integer multiplication overflow");
    }
    return left * right;
}

std::string decodeString(const std::string& token) {
    std::string result;
    result.reserve(token.size() - 2);
    for (std::size_t i = 1; i + 1 < token.size(); ++i) {
        if (token[i] != '\\') {
            result.push_back(token[i]);
            continue;
        }
        ++i;
        if (i + 1 > token.size()) {
            throw std::runtime_error("unterminated string escape");
        }
        switch (token[i]) {
        case 'n':
            result.push_back('\n');
            break;
        case 'r':
            result.push_back('\r');
            break;
        case 't':
            result.push_back('\t');
            break;
        case '\\':
            result.push_back('\\');
            break;
        case '\'':
            result.push_back('\'');
            break;
        case '"':
            result.push_back('"');
            break;
        default:
            throw std::runtime_error("unsupported string escape");
        }
    }
    if (result.size() > kMaxStringSize) {
        throw std::runtime_error("string literal exceeds the 100000-character limit");
    }
    return result;
}

void writeExact(std::ostream& output, const char* data, std::size_t size) {
    output.write(data, static_cast<std::streamsize>(size));
    if (!output) {
        throw std::runtime_error("failed to write bytecode binary");
    }
}

void readExact(std::istream& input, char* data, std::size_t size) {
    input.read(data, static_cast<std::streamsize>(size));
    if (input.gcount() != static_cast<std::streamsize>(size)) {
        throw std::runtime_error("truncated bytecode binary");
    }
}

void writeU8(std::ostream& output, std::uint8_t value) {
    const char byte = static_cast<char>(value);
    writeExact(output, &byte, 1);
}

std::uint8_t readU8(std::istream& input) {
    char byte = 0;
    readExact(input, &byte, 1);
    return static_cast<std::uint8_t>(static_cast<unsigned char>(byte));
}

void writeU32(std::ostream& output, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        writeU8(output, static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

std::uint32_t readU32(std::istream& input) {
    std::uint32_t value = 0;
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(readU8(input)) << shift;
    }
    return value;
}

void writeU64(std::ostream& output, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        writeU8(output, static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

std::uint64_t readU64(std::istream& input) {
    std::uint64_t value = 0;
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(readU8(input)) << shift;
    }
    return value;
}

void writeI64(std::ostream& output, std::int64_t value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU64(output, bits);
}

std::int64_t readI64(std::istream& input) {
    const auto bits = readU64(input);
    std::int64_t value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void writeString(std::ostream& output, const std::string& value) {
    if (value.size() > kMaxSourceSize) {
        throw std::runtime_error("binary string exceeds the 1 MB limit");
    }
    writeU32(output, static_cast<std::uint32_t>(value.size()));
    writeExact(output, value.data(), value.size());
}

std::string readString(std::istream& input) {
    const auto size = readU32(input);
    if (size > kMaxSourceSize) {
        throw std::runtime_error("binary string exceeds the 1 MB limit");
    }
    std::string value(size, '\0');
    readExact(input, value.data(), value.size());
    return value;
}

void writeValue(std::ostream& output, const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        writeU8(output, 0);
    } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        writeU8(output, 1);
        writeI64(output, *integer);
    } else if (const auto* number = std::get_if<double>(&value)) {
        writeU8(output, 2);
        std::uint64_t bits = 0;
        std::memcpy(&bits, number, sizeof(bits));
        writeU64(output, bits);
    } else if (const auto* boolean = std::get_if<bool>(&value)) {
        writeU8(output, 3);
        writeU8(output, *boolean ? 1 : 0);
    } else if (const auto* string = std::get_if<std::string>(&value)) {
        if (string->size() > kMaxStringSize) {
            throw std::runtime_error("string constant exceeds the 100000-character limit");
        }
        writeU8(output, 4);
        writeString(output, *string);
    } else {
        throw std::runtime_error("object values cannot be serialized as constants");
    }
}

Value readValue(std::istream& input) {
    switch (readU8(input)) {
    case 0:
        return std::monostate{};
    case 1:
        return readI64(input);
    case 2: {
        const auto bits = readU64(input);
        double number = 0.0;
        std::memcpy(&number, &bits, sizeof(number));
        if (!std::isfinite(number)) {
            throw std::runtime_error("non-finite floating-point constant in binary");
        }
        return number;
    }
    case 3: {
        const auto value = readU8(input);
        if (value > 1) {
            throw std::runtime_error("invalid boolean constant in binary");
        }
        return value != 0;
    }
    case 4:
        return readString(input);
    default:
        throw std::runtime_error("invalid constant tag in bytecode binary");
    }
}

struct Token {
    std::string text;
    std::size_t line;
    std::size_t column;
};

class Lexer {
public:
    explicit Lexer(const std::string& source) : source_(source) {}

    std::vector<Token> scan() {
        while (!atEnd()) {
            skipWhitespaceAndComments();
            if (atEnd()) {
                break;
            }
            const auto startLine = line_;
            const auto startColumn = column_;
            const char current = peek();
            if (std::isalpha(static_cast<unsigned char>(current)) || current == '_') {
                scanIdentifier(startLine, startColumn);
            } else if (std::isdigit(static_cast<unsigned char>(current))) {
                scanNumber(startLine, startColumn);
            } else if (current == '"' || current == '\'') {
                scanString(startLine, startColumn);
            } else {
                scanOperator(startLine, startColumn);
            }
        }
        tokens_.push_back({"<eof>", line_, column_});
        return tokens_;
    }

private:
    bool atEnd() const {
        return offset_ >= source_.size();
    }

    char peek(std::size_t distance = 0) const {
        const auto position = offset_ + distance;
        return position < source_.size() ? source_[position] : '\0';
    }

    char advance() {
        const char value = source_[offset_++];
        if (value == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return value;
    }

    bool match(char value) {
        if (atEnd() || peek() != value) {
            return false;
        }
        advance();
        return true;
    }

    void skipWhitespaceAndComments() {
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
                advance();
                advance();
                bool closed = false;
                while (!atEnd()) {
                    if (peek() == '*' && peek(1) == '/') {
                        advance();
                        advance();
                        closed = true;
                        break;
                    }
                    advance();
                }
                if (!closed) {
                    fail("unterminated block comment", line_, column_);
                }
                continue;
            }
            return;
        }
    }

    void scanIdentifier(std::size_t startLine, std::size_t startColumn) {
        const auto start = offset_;
        while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
            advance();
        }
        tokens_.push_back({source_.substr(start, offset_ - start), startLine, startColumn});
    }

    void scanNumber(std::size_t startLine, std::size_t startColumn) {
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
                fail("malformed numeric exponent", startLine, startColumn);
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }
        tokens_.push_back({source_.substr(start, offset_ - start), startLine, startColumn});
    }

    void scanString(std::size_t startLine, std::size_t startColumn) {
        const auto start = offset_;
        const char delimiter = advance();
        bool closed = false;
        while (!atEnd()) {
            if (peek() == '\n') {
                fail("newline in string literal", startLine, startColumn);
            }
            if (peek() == '\\') {
                advance();
                if (atEnd()) {
                    break;
                }
                advance();
            } else if (advance() == delimiter) {
                closed = true;
                break;
            }
        }
        if (!closed) {
            fail("unterminated string literal", startLine, startColumn);
        }
        tokens_.push_back({source_.substr(start, offset_ - start), startLine, startColumn});
    }

    void scanOperator(std::size_t startLine, std::size_t startColumn) {
        const auto start = offset_;
        const char first = advance();
        std::string text(1, first);
        const char second = peek();
        if ((first == '=' && second == '=') || (first == '!' && second == '=') ||
            (first == '<' && (second == '=' || second == '<')) ||
            (first == '>' && (second == '=' || second == '>')) ||
            (first == '&' && second == '&') || (first == '|' && second == '|') ||
            (first == ':' && second == ':')) {
            text.push_back(advance());
        } else if (first == '.' && second == '.') {
            fail("range syntax is not supported", startLine, startColumn);
        }
        if (std::string("(){};,.-+*/%!=<>!~&|^").find(first) == std::string::npos &&
            !(first == ':' && text == "::")) {
            fail("unexpected character", startLine, startColumn);
        }
        tokens_.push_back({source_.substr(start, offset_ - start), startLine, startColumn});
    }

    [[noreturn]] static void fail(
        const std::string& message,
        std::size_t line,
        std::size_t column
    ) {
        throw std::runtime_error(
            "line " + std::to_string(line) + ", column " + std::to_string(column) +
            ": " + message
        );
    }

    const std::string& source_;
    std::vector<Token> tokens_;
    std::size_t offset_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
};

} // namespace

class Parser {
public:
    Parser(const std::string& source, std::vector<Jit::Instruction>& code)
        : tokens_(Lexer(source).scan()), code_(code) {}

    void parse() {
        while (!at("<eof>")) {
            if (match(";")) {
                continue;
            }
            statement();
        }
        emit(Jit::Op::Halt);
    }

private:
    class DepthGuard {
    public:
        DepthGuard(Parser& parser, std::size_t& depth, const char* label)
            : depth_(depth) {
            if (depth_ >= kMaxParseDepth) {
                parser.fail(std::string("maximum ") + label + " nesting exceeded");
            }
            ++depth_;
        }

        ~DepthGuard() {
            --depth_;
        }

    private:
        std::size_t& depth_;
    };

    const Token& current() const {
        return tokens_[position_];
    }

    const Token& previous() const {
        return tokens_[position_ - 1];
    }

    bool at(const std::string& text) const {
        return current().text == text;
    }

    bool match(const std::string& text) {
        if (!at(text)) {
            return false;
        }
        ++position_;
        return true;
    }

    const Token& consume(const std::string& text) {
        if (!at(text)) {
            fail("expected '" + text + "'");
        }
        return tokens_[position_++];
    }

    const Token& identifier() {
        const auto& token = current();
        if (token.text.empty() ||
            !(std::isalpha(static_cast<unsigned char>(token.text[0])) ||
              token.text[0] == '_')) {
            fail("expected an identifier");
        }
        ++position_;
        return token;
    }

    [[noreturn]] void failAt(const Token& token, const std::string& message) const {
        throw std::runtime_error(
            "line " + std::to_string(token.line) + ", column " +
            std::to_string(token.column) + ": " + message
        );
    }

    [[noreturn]] void fail(const std::string& message) const {
        failAt(current(), message);
    }

    std::size_t emit(
        Jit::Op op,
        std::int64_t operand = 0,
        std::string text = {},
        Value constant = {}
    ) {
        code_.push_back({op, operand, std::move(text), std::move(constant)});
        return code_.size() - 1;
    }

    void patchJump(std::size_t instruction, std::size_t target) {
        code_.at(instruction).operand = static_cast<std::int64_t>(target);
    }

    void block() {
        DepthGuard guard(*this, blockDepth_, "block");
        consume("{");
        while (!at("}") && !at("<eof>")) {
            if (!match(";")) {
                statement();
            }
        }
        consume("}");
    }

    void statement() {
        DepthGuard guard(*this, statementDepth_, "statement");
        if (at("{")) {
            block();
            return;
        }
        if (match("if")) {
            consume("(");
            expression();
            consume(")");
            const auto falseJump = emit(Jit::Op::JumpIfFalse);
            statement();
            if (match("else")) {
                const auto endJump = emit(Jit::Op::Jump);
                patchJump(falseJump, code_.size());
                statement();
                patchJump(endJump, code_.size());
            } else {
                patchJump(falseJump, code_.size());
            }
            return;
        }
        if (match("while")) {
            const auto loopStart = code_.size();
            consume("(");
            expression();
            consume(")");
            const auto exitJump = emit(Jit::Op::JumpIfFalse);
            statement();
            emit(Jit::Op::Jump, static_cast<std::int64_t>(loopStart));
            patchJump(exitJump, code_.size());
            return;
        }
        if (match("let") || match("var") || match("const") || match("int") ||
            match("long") || match("double") || match("float") || match("bool") ||
            match("string") || match("auto")) {
            const auto name = identifier().text;
            if (match("=")) {
                expression();
            } else {
                emit(Jit::Op::Constant, 0, {}, std::int64_t{0});
            }
            consume(";");
            emit(Jit::Op::Store, 0, name);
            return;
        }
        if (assignmentAhead()) {
            const auto name = identifier().text;
            if (match(".")) {
                const auto field = identifier().text;
                emit(Jit::Op::Load, 0, name);
                consume("=");
                expression();
                consume(";");
                emit(Jit::Op::StoreField, 0, field);
            } else {
                consume("=");
                expression();
                consume(";");
                emit(Jit::Op::Store, 0, name);
            }
            return;
        }
        expression();
        consume(";");
        emit(Jit::Op::Pop);
    }

    bool assignmentAhead() const {
        if (current().text.empty() ||
            !(std::isalpha(static_cast<unsigned char>(current().text[0])) ||
              current().text[0] == '_')) {
            return false;
        }
        if (position_ + 1 < tokens_.size() && tokens_[position_ + 1].text == "=") {
            return true;
        }
        return position_ + 3 < tokens_.size() && tokens_[position_ + 1].text == "." &&
               tokens_[position_ + 2].text.size() > 0 &&
               (std::isalpha(static_cast<unsigned char>(tokens_[position_ + 2].text[0])) ||
                tokens_[position_ + 2].text[0] == '_') &&
               tokens_[position_ + 3].text == "=";
    }

    static int precedence(const std::string& op) {
        if (op == "||") return 1;
        if (op == "&&") return 2;
        if (op == "|") return 3;
        if (op == "^") return 4;
        if (op == "&") return 5;
        if (op == "==" || op == "!=") return 6;
        if (op == "<" || op == "<=" || op == ">" || op == ">=") return 7;
        if (op == "<<" || op == ">>") return 8;
        if (op == "+" || op == "-") return 9;
        if (op == "*" || op == "/" || op == "%") return 10;
        return 0;
    }

    void expression(int minimumPrecedence = 1) {
        DepthGuard guard(*this, expressionDepth_, "expression");
        unary();
        while (precedence(current().text) >= minimumPrecedence) {
            const auto op = current().text;
            const auto level = precedence(op);
            ++position_;
            if (op == "&&") {
                const auto falseJump = emit(Jit::Op::JumpIfFalse);
                expression(level + 1);
                emit(Jit::Op::LogicalNot);
                emit(Jit::Op::LogicalNot);
                const auto endJump = emit(Jit::Op::Jump);
                patchJump(falseJump, code_.size());
                emit(Jit::Op::Constant, 0, {}, false);
                patchJump(endJump, code_.size());
                continue;
            }
            if (op == "||") {
                const auto falseJump = emit(Jit::Op::JumpIfFalse);
                emit(Jit::Op::Constant, 0, {}, true);
                const auto endJump = emit(Jit::Op::Jump);
                patchJump(falseJump, code_.size());
                expression(level + 1);
                emit(Jit::Op::LogicalNot);
                emit(Jit::Op::LogicalNot);
                patchJump(endJump, code_.size());
                continue;
            }
            expression(level + 1);
            emit(binaryOpcode(op));
        }
    }

    void unary() {
        if (match("!")) {
            DepthGuard guard(*this, unaryDepth_, "unary operator");
            unary();
            emit(Jit::Op::LogicalNot);
            return;
        }
        if (match("~")) {
            DepthGuard guard(*this, unaryDepth_, "unary operator");
            unary();
            emit(Jit::Op::BitwiseNot);
            return;
        }
        if (match("-")) {
            DepthGuard guard(*this, unaryDepth_, "unary operator");
            unary();
            emit(Jit::Op::Negate);
            return;
        }
        if (match("+")) {
            DepthGuard guard(*this, unaryDepth_, "unary operator");
            unary();
            return;
        }
        primary();
    }

    void primary() {
        if (match("(")) {
            expression();
            consume(")");
            return;
        }
        const auto token = current();
        if (!token.text.empty() &&
            std::isdigit(static_cast<unsigned char>(token.text[0]))) {
            ++position_;
            if (token.text.find_first_of(".eE") != std::string::npos) {
                std::size_t consumed = 0;
                double value = 0.0;
                try {
                    value = std::stod(token.text, &consumed);
                } catch (const std::exception&) {
                    failAt(token, "invalid floating-point literal");
                }
                if (consumed != token.text.size() || !std::isfinite(value)) {
                    failAt(token, "floating-point literal must be finite");
                }
                emit(Jit::Op::Constant, 0, {}, value);
            } else {
                std::size_t consumed = 0;
                std::int64_t value = 0;
                try {
                    value = std::stoll(token.text, &consumed);
                } catch (const std::exception&) {
                    failAt(token, "integer literal exceeds the 64-bit limit");
                }
                if (consumed != token.text.size()) {
                    failAt(token, "invalid integer literal");
                }
                emit(Jit::Op::Constant, 0, {}, value);
            }
            return;
        }
        if (!token.text.empty() && (token.text[0] == '"' || token.text[0] == '\'')) {
            ++position_;
            try {
                emit(Jit::Op::Constant, 0, {}, decodeString(token.text));
            } catch (const std::runtime_error& error) {
                failAt(token, error.what());
            }
            return;
        }
        if (match("true")) {
            emit(Jit::Op::Constant, 0, {}, true);
            return;
        }
        if (match("false")) {
            emit(Jit::Op::Constant, 0, {}, false);
            return;
        }
        auto name = identifier().text;
        while (match("::")) {
            name += "::" + identifier().text;
        }
        if (match("(")) {
            const auto argumentCount = arguments();
            emit(Jit::Op::CallFunction, static_cast<std::int64_t>(argumentCount), name);
        } else {
            emit(Jit::Op::Load, 0, name);
        }
        while (match(".")) {
            const auto member = identifier().text;
            if (match("(")) {
                const auto argumentCount = arguments();
                emit(Jit::Op::CallMethod, static_cast<std::int64_t>(argumentCount), member);
            } else {
                emit(Jit::Op::LoadField, 0, member);
            }
        }
    }

    std::size_t arguments() {
        std::size_t count = 0;
        if (match(")")) {
            return count;
        }
        do {
            expression();
            ++count;
            if (count > kMaxCallArguments) {
                fail("call exceeds the 1024-argument limit");
            }
        } while (match(","));
        consume(")");
        return count;
    }

    static Jit::Op binaryOpcode(const std::string& op) {
        if (op == "+") return Jit::Op::Add;
        if (op == "-") return Jit::Op::Subtract;
        if (op == "*") return Jit::Op::Multiply;
        if (op == "/") return Jit::Op::Divide;
        if (op == "%") return Jit::Op::Modulo;
        if (op == "==") return Jit::Op::Equal;
        if (op == "!=") return Jit::Op::NotEqual;
        if (op == "<") return Jit::Op::Less;
        if (op == "<=") return Jit::Op::LessEqual;
        if (op == ">") return Jit::Op::Greater;
        if (op == ">=") return Jit::Op::GreaterEqual;
        if (op == "&&") return Jit::Op::LogicalAnd;
        if (op == "||") return Jit::Op::LogicalOr;
        if (op == "&") return Jit::Op::BitwiseAnd;
        if (op == "|") return Jit::Op::BitwiseOr;
        if (op == "^") return Jit::Op::BitwiseXor;
        if (op == "<<") return Jit::Op::ShiftLeft;
        if (op == ">>") return Jit::Op::ShiftRight;
        throw std::runtime_error("internal compiler error: unknown operator " + op);
    }

    std::vector<Token> tokens_;
    std::vector<Jit::Instruction>& code_;
    std::size_t position_ = 0;
    std::size_t blockDepth_ = 0;
    std::size_t statementDepth_ = 0;
    std::size_t expressionDepth_ = 0;
    std::size_t unaryDepth_ = 0;
};

Jit::Jit() : input_(&std::cin), output_(&std::cout) {
    registerFunction("print", [this](const Arguments& arguments) {
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (i != 0) {
                *output_ << ' ';
                ++outputCharacters_;
            }
            const auto text = toString(arguments[i]);
            outputCharacters_ += text.size();
            if (outputCharacters_ > kMaxOutputSize) {
                throw std::runtime_error("output exceeds the 1000000-character limit");
            }
            *output_ << text;
        }
        if (!*output_) {
            throw std::runtime_error("failed to write program output");
        }
        return Value{};
    });
    registerFunction("println", [this](const Arguments& arguments) {
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (i != 0) {
                *output_ << ' ';
                ++outputCharacters_;
            }
            const auto text = toString(arguments[i]);
            outputCharacters_ += text.size();
            if (outputCharacters_ > kMaxOutputSize) {
                throw std::runtime_error("output exceeds the 1000000-character limit");
            }
            *output_ << text;
        }
        ++outputCharacters_;
        if (outputCharacters_ > kMaxOutputSize) {
            throw std::runtime_error("output exceeds the 1000000-character limit");
        }
        *output_ << '\n';
        if (!*output_) {
            throw std::runtime_error("failed to write program output");
        }
        return Value{};
    });
    registerFunction("input", [this](const Arguments& arguments) {
        if (!arguments.empty()) {
            throw std::runtime_error("input expects no arguments");
        }
        std::string line;
        char character = '\0';
        while (input_->get(character) && character != '\n') {
            if (line.size() == kMaxStringSize) {
                throw std::runtime_error("input exceeds the 100000-character limit");
            }
            line.push_back(character);
        }
        if (line.empty() && input_->eof()) {
            return Value{std::string{}};
        }
        return Value{std::move(line)};
    });
}

void Jit::compile(const std::string& source) {
    if (source.size() > kMaxSourceSize) {
        throw std::runtime_error("source exceeds the 1 MB input limit");
    }
    std::vector<Instruction> compiled;
    Parser parser(source, compiled);
    parser.parse();
    verifyCode(compiled);
    code_ = std::move(compiled);
}

Value Jit::run(std::size_t maxSteps) {
    if (maxSteps == 0) {
        throw std::invalid_argument("maxSteps must be greater than zero");
    }
    if (code_.empty()) {
        throw std::runtime_error("no compiled program is loaded");
    }
    outputCharacters_ = 0;
    std::vector<Value> stack;
    stack.reserve(64);
    std::size_t pc = 0;
    std::size_t steps = 0;
    Value result{};

    const auto pop = [&stack]() -> Value {
        if (stack.empty()) {
            throw std::runtime_error("bytecode stack underflow");
        }
        auto value = std::move(stack.back());
        stack.pop_back();
        return value;
    };
    const auto popArguments = [&pop](std::size_t count) {
        Arguments arguments(count);
        for (std::size_t i = count; i > 0; --i) {
            arguments[i - 1] = pop();
        }
        return arguments;
    };
    const auto push = [&stack](Value value) {
        if (stack.size() >= kMaxStackSize) {
            throw std::runtime_error("execution stack exceeds the 65536-value limit");
        }
        validateValue(value);
        stack.push_back(std::move(value));
    };
    const auto numericResult = [](double value) -> Value {
        if (!std::isfinite(value)) {
            throw std::runtime_error("non-finite numeric result");
        }
        return value;
    };
    const auto invokeFunction = [this](const std::string& name, const Arguments& args) {
        const auto function = functions_.find(name);
        if (function != functions_.end()) {
            try {
                return function->second(args);
            } catch (const std::exception& error) {
                throw std::runtime_error(
                    "native function '" + name + "' failed: " + error.what()
                );
            }
        }
        const auto classFactory = classes_.find(name);
        if (classFactory != classes_.end()) {
            try {
                auto object = classFactory->second(args);
                if (!object || object->className != name) {
                    throw std::runtime_error("class factory returned an invalid object");
                }
                return Value{std::move(object)};
            } catch (const std::exception& error) {
                throw std::runtime_error(
                    "class constructor '" + name + "' failed: " + error.what()
                );
            }
        }
        throw std::runtime_error("unregistered function or class '" + name + "'");
    };

    while (pc < code_.size()) {
        if (steps++ >= maxSteps) {
            throw std::runtime_error("execution exceeded the configured step limit");
        }
        const auto& instruction = code_[pc++];
        switch (instruction.op) {
        case Op::Constant:
            push(instruction.constant);
            break;
        case Op::Load: {
            const auto global = globals_.find(instruction.text);
            if (global == globals_.end()) {
                throw std::runtime_error("undefined variable '" + instruction.text + "'");
            }
            push(global->second);
            break;
        }
        case Op::Store:
            globals_[instruction.text] = pop();
            result = globals_[instruction.text];
            break;
        case Op::Pop:
            result = pop();
            break;
        case Op::Add:
        case Op::Subtract:
        case Op::Multiply:
        case Op::Divide:
        case Op::Modulo:
        case Op::Equal:
        case Op::NotEqual:
        case Op::Less:
        case Op::LessEqual:
        case Op::Greater:
        case Op::GreaterEqual:
        case Op::LogicalAnd:
        case Op::LogicalOr:
        case Op::BitwiseAnd:
        case Op::BitwiseOr:
        case Op::BitwiseXor:
        case Op::ShiftLeft:
        case Op::ShiftRight: {
            const auto right = pop();
            const auto left = pop();
            if (instruction.op == Op::Add) {
                if (const auto* leftString = std::get_if<std::string>(&left)) {
                    const auto* rightString = std::get_if<std::string>(&right);
                    if (rightString == nullptr) {
                        throw std::runtime_error("string addition requires two strings");
                    }
                    if (leftString->size() + rightString->size() > kMaxStringSize) {
                        throw std::runtime_error("string result exceeds the 100000-character limit");
                    }
                    push(*leftString + *rightString);
                } else if (isInteger(left) && isInteger(right)) {
                    push(checkedAdd(asInteger(left), asInteger(right)));
                } else {
                    push(numericResult(asNumber(left) + asNumber(right)));
                }
            } else if (instruction.op == Op::Subtract) {
                if (isInteger(left) && isInteger(right)) {
                    push(checkedSubtract(asInteger(left), asInteger(right)));
                } else {
                    push(numericResult(asNumber(left) - asNumber(right)));
                }
            } else if (instruction.op == Op::Multiply) {
                if (isInteger(left) && isInteger(right)) {
                    push(checkedMultiply(asInteger(left), asInteger(right)));
                } else {
                    push(numericResult(asNumber(left) * asNumber(right)));
                }
            } else if (instruction.op == Op::Divide) {
                const auto divisor = asNumber(right);
                if (divisor == 0.0) {
                    throw std::runtime_error("division by zero");
                }
                push(numericResult(asNumber(left) / divisor));
            } else if (instruction.op == Op::Modulo) {
                const auto dividend = asInteger(left);
                const auto divisor = asInteger(right);
                if (divisor == 0) {
                    throw std::runtime_error("modulo by zero");
                }
                if (dividend == std::numeric_limits<std::int64_t>::min() && divisor == -1) {
                    throw std::runtime_error("integer modulo overflow");
                }
                push(dividend % divisor);
            } else if (instruction.op == Op::Equal || instruction.op == Op::NotEqual) {
                bool equal = false;
                if (isNumeric(left) && isNumeric(right)) {
                    equal = asNumber(left) == asNumber(right);
                } else if (left.index() == right.index()) {
                    equal = left == right;
                }
                push(instruction.op == Op::Equal ? equal : !equal);
            } else if (instruction.op == Op::Less || instruction.op == Op::LessEqual ||
                       instruction.op == Op::Greater || instruction.op == Op::GreaterEqual) {
                bool less = false;
                bool equal = false;
                if (isNumeric(left) && isNumeric(right)) {
                    const auto leftNumber = asNumber(left);
                    const auto rightNumber = asNumber(right);
                    less = leftNumber < rightNumber;
                    equal = leftNumber == rightNumber;
                } else if (const auto* leftString = std::get_if<std::string>(&left)) {
                    const auto* rightString = std::get_if<std::string>(&right);
                    if (rightString == nullptr) {
                        throw std::runtime_error("comparison requires matching operand types");
                    }
                    less = *leftString < *rightString;
                    equal = *leftString == *rightString;
                } else {
                    throw std::runtime_error("comparison requires matching numeric or string types");
                }
                if (instruction.op == Op::Less) push(less);
                else if (instruction.op == Op::LessEqual) push(less || equal);
                else if (instruction.op == Op::Greater) push(!less && !equal);
                else push(!less);
            } else if (instruction.op == Op::LogicalAnd) {
                push(truthy(left) && truthy(right));
            } else if (instruction.op == Op::LogicalOr) {
                push(truthy(left) || truthy(right));
            } else {
                const auto leftInteger = asInteger(left);
                const auto rightInteger = asInteger(right);
                if (instruction.op == Op::BitwiseAnd) {
                    push(leftInteger & rightInteger);
                } else if (instruction.op == Op::BitwiseOr) {
                    push(leftInteger | rightInteger);
                } else if (instruction.op == Op::BitwiseXor) {
                    push(leftInteger ^ rightInteger);
                } else {
                    if (rightInteger < 0 || rightInteger > 62) {
                        throw std::runtime_error("shift count must be between 0 and 62");
                    }
                    if (instruction.op == Op::ShiftLeft) {
                        const auto shift = static_cast<unsigned int>(rightInteger);
                        const auto value = asInteger(left);
                        if (value < 0 ||
                            value > (std::numeric_limits<std::int64_t>::max() >> shift)) {
                            throw std::runtime_error("left shift exceeds the signed 64-bit limit");
                        }
                        push(value << shift);
                    } else {
                        const auto value = asInteger(left);
                        if (value < 0) {
                            throw std::runtime_error("right shift requires a non-negative value");
                        }
                        push(value >> static_cast<unsigned int>(rightInteger));
                    }
                }
            }
            break;
        }
        case Op::Negate: {
            const auto value = pop();
            if (isInteger(value)) {
                const auto integer = asInteger(value);
                if (integer == std::numeric_limits<std::int64_t>::min()) {
                    throw std::runtime_error("integer negation overflow");
                }
                push(-integer);
            } else {
                push(numericResult(-asNumber(value)));
            }
            break;
        }
        case Op::LogicalNot:
            push(!truthy(pop()));
            break;
        case Op::BitwiseNot:
            push(~asInteger(pop()));
            break;
        case Op::JumpIfFalse:
            if (!truthy(pop())) {
                pc = static_cast<std::size_t>(instruction.operand);
            }
            break;
        case Op::Jump:
            pc = static_cast<std::size_t>(instruction.operand);
            break;
        case Op::CallFunction: {
            const auto args = popArguments(static_cast<std::size_t>(instruction.operand));
            push(invokeFunction(instruction.text, args));
            break;
        }
        case Op::CallMethod: {
            const auto args = popArguments(static_cast<std::size_t>(instruction.operand));
            const auto receiverValue = pop();
            const auto* receiver = std::get_if<ObjectPtr>(&receiverValue);
            if (receiver == nullptr || !*receiver) {
                throw std::runtime_error("method receiver is not an object");
            }
            const auto method = methods_.find(bindingKey((*receiver)->className, instruction.text));
            if (method == methods_.end()) {
                throw std::runtime_error(
                    "unregistered method '" + (*receiver)->className + "." + instruction.text + "'"
                );
            }
            try {
                push(method->second(*receiver, args));
            } catch (const std::exception& error) {
                throw std::runtime_error(
                    "native method '" + (*receiver)->className + "." + instruction.text +
                    "' failed: " + error.what()
                );
            }
            break;
        }
        case Op::LoadField: {
            const auto receiverValue = pop();
            const auto* receiver = std::get_if<ObjectPtr>(&receiverValue);
            if (receiver == nullptr || !*receiver) {
                throw std::runtime_error("field receiver is not an object");
            }
            const auto fieldKey = bindingKey((*receiver)->className, instruction.text);
            const auto binding = fields_.find(fieldKey);
            if (binding != fields_.end()) {
                push(binding->second.getter(*receiver));
            } else {
                const auto field = (*receiver)->fields.find(instruction.text);
                if (field == (*receiver)->fields.end()) {
                    throw std::runtime_error(
                        "unknown field '" + (*receiver)->className + "." + instruction.text + "'"
                    );
                }
                push(field->second);
            }
            break;
        }
        case Op::StoreField: {
            const auto value = pop();
            const auto receiverValue = pop();
            const auto* receiver = std::get_if<ObjectPtr>(&receiverValue);
            if (receiver == nullptr || !*receiver) {
                throw std::runtime_error("field receiver is not an object");
            }
            const auto fieldKey = bindingKey((*receiver)->className, instruction.text);
            const auto binding = fields_.find(fieldKey);
            if (binding != fields_.end()) {
                binding->second.setter(*receiver, value);
            } else {
                (*receiver)->fields[instruction.text] = value;
            }
            result = value;
            break;
        }
        case Op::Halt:
            return result;
        }
    }
    return result;
}

void Jit::writeBinary(std::ostream& output) const {
    verifyCode(code_);
    writeExact(output, kBinaryMagic, sizeof(kBinaryMagic));
    writeU32(output, kBinaryVersion);
    writeU32(output, static_cast<std::uint32_t>(code_.size()));
    for (const auto& instruction : code_) {
        writeU8(output, static_cast<std::uint8_t>(instruction.op));
        writeI64(output, instruction.operand);
        writeString(output, instruction.text);
        writeValue(output, instruction.constant);
    }
}

void Jit::writeBinary(const std::string& path) const {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot open bytecode output file: " + path);
    }
    writeBinary(output);
}

void Jit::loadBinary(std::istream& input) {
    char magic[sizeof(kBinaryMagic)]{};
    readExact(input, magic, sizeof(magic));
    if (!std::equal(std::begin(kBinaryMagic), std::end(kBinaryMagic), magic)) {
        throw std::runtime_error("invalid bytecode binary signature");
    }
    if (readU32(input) != kBinaryVersion) {
        throw std::runtime_error("unsupported bytecode binary version");
    }
    const auto instructionCount = readU32(input);
    if (instructionCount == 0 || instructionCount > kMaxBinaryInstructions) {
        throw std::runtime_error("invalid bytecode instruction count");
    }
    std::vector<Instruction> loaded;
    loaded.reserve(instructionCount);
    for (std::uint32_t i = 0; i < instructionCount; ++i) {
        const auto rawOp = readU8(input);
        const auto operand = readI64(input);
        auto text = readString(input);
        auto constant = readValue(input);
        loaded.push_back(
            {static_cast<Op>(rawOp), operand, std::move(text), std::move(constant)}
        );
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("unexpected trailing data in bytecode binary");
    }
    verifyCode(loaded);
    code_ = std::move(loaded);
}

void Jit::loadBinary(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open bytecode input file: " + path);
    }
    loadBinary(input);
}

void Jit::registerFunction(std::string sourceName, NativeFunction function) {
    if (sourceName.empty() || !function) {
        throw std::invalid_argument("function name and callback must be non-empty");
    }
    functions_[std::move(sourceName)] = std::move(function);
}

void Jit::registerKeyword(std::string sourceName, NativeFunction function) {
    registerFunction(std::move(sourceName), std::move(function));
}

void Jit::addClass(std::string name, ClassFactory factory) {
    registerClassFactory(std::move(name), std::move(factory));
}

void Jit::addMethod(std::string className, std::string methodName, NativeMethod method) {
    registerMethodBinding(
        std::move(className),
        std::move(methodName),
        std::move(method)
    );
}

void Jit::addField(
    std::string className,
    std::string fieldName,
    FieldGetter getter,
    FieldSetter setter
) {
    registerFieldBinding(
        std::move(className),
        std::move(fieldName),
        std::move(getter),
        std::move(setter)
    );
}

void Jit::registerClassFactory(std::string name, ClassFactory factory) {
    if (name.empty() || !factory) {
        throw std::invalid_argument("class name and factory must be non-empty");
    }
    classes_[std::move(name)] = std::move(factory);
}

void Jit::registerMethodBinding(
    std::string className,
    std::string methodName,
    NativeMethod method
) {
    if (className.empty() || methodName.empty() || !method) {
        throw std::invalid_argument("class, method name, and callback must be non-empty");
    }
    methods_[bindingKey(className, methodName)] = std::move(method);
}

void Jit::registerFieldBinding(
    std::string className,
    std::string fieldName,
    FieldGetter getter,
    FieldSetter setter
) {
    if (className.empty() || fieldName.empty() || !getter || !setter) {
        throw std::invalid_argument("field name and getter/setter callbacks must be non-empty");
    }
    fields_[bindingKey(className, fieldName)] =
        FieldBinding{std::move(getter), std::move(setter)};
}

void Jit::verifyCode(const std::vector<Instruction>& code) const {
    if (code.empty() || code.size() > kMaxBinaryInstructions) {
        throw std::runtime_error("invalid bytecode instruction count");
    }
    if (code.back().op != Op::Halt) {
        throw std::runtime_error("bytecode must end with HALT");
    }
    const auto first = static_cast<std::uint8_t>(Op::Constant);
    const auto last = static_cast<std::uint8_t>(Op::Halt);
    for (std::size_t index = 0; index < code.size(); ++index) {
        const auto& instruction = code[index];
        const auto op = static_cast<std::uint8_t>(instruction.op);
        if (op < first || op > last) {
            throw std::runtime_error("invalid opcode in bytecode");
        }
        if (instruction.op == Op::Halt && index + 1 != code.size()) {
            throw std::runtime_error("HALT may only appear at the end of bytecode");
        }
        if ((instruction.op == Op::Jump || instruction.op == Op::JumpIfFalse) &&
            (instruction.operand < 0 ||
             static_cast<std::uint64_t>(instruction.operand) >= code.size())) {
            throw std::runtime_error("invalid jump target in bytecode");
        }
        if ((instruction.op == Op::CallFunction || instruction.op == Op::CallMethod) &&
            (instruction.operand < 0 ||
             static_cast<std::uint64_t>(instruction.operand) > kMaxCallArguments)) {
            throw std::runtime_error("invalid call argument count in bytecode");
        }
        if (instruction.op == Op::Constant &&
            std::holds_alternative<ObjectPtr>(instruction.constant)) {
            throw std::runtime_error("object values cannot be bytecode constants");
        }
        if (instruction.op == Op::Constant) {
            validateValue(instruction.constant);
        }
        if (instruction.text.size() > kMaxSourceSize) {
            throw std::runtime_error("bytecode name exceeds the 1 MB limit");
        }
    }

    std::vector<std::int64_t> heights(code.size(), -1);
    std::queue<std::pair<std::size_t, std::int64_t>> pending;
    pending.emplace(0, 0);
    while (!pending.empty()) {
        const auto [index, height] = pending.front();
        pending.pop();
        if (index >= code.size()) {
            throw std::runtime_error("bytecode control flow leaves the program");
        }
        if (heights[index] != -1) {
            if (heights[index] != height) {
                throw std::runtime_error("inconsistent bytecode stack height");
            }
            continue;
        }
        heights[index] = height;
        const auto& instruction = code[index];
        std::int64_t required = 0;
        std::int64_t delta = 0;
        switch (instruction.op) {
        case Op::Constant:
        case Op::Load:
            delta = 1;
            break;
        case Op::Store:
        case Op::Pop:
            required = 1;
            delta = -1;
            break;
        case Op::Add:
        case Op::Subtract:
        case Op::Multiply:
        case Op::Divide:
        case Op::Modulo:
        case Op::Equal:
        case Op::NotEqual:
        case Op::Less:
        case Op::LessEqual:
        case Op::Greater:
        case Op::GreaterEqual:
        case Op::LogicalAnd:
        case Op::LogicalOr:
        case Op::BitwiseAnd:
        case Op::BitwiseOr:
        case Op::BitwiseXor:
        case Op::ShiftLeft:
        case Op::ShiftRight:
            required = 2;
            delta = -1;
            break;
        case Op::Negate:
        case Op::LogicalNot:
        case Op::BitwiseNot:
        case Op::LoadField:
            required = 1;
            break;
        case Op::JumpIfFalse:
            required = 1;
            delta = -1;
            break;
        case Op::CallFunction:
            required = instruction.operand;
            delta = 1 - instruction.operand;
            break;
        case Op::CallMethod:
            required = instruction.operand + 1;
            delta = -instruction.operand;
            break;
        case Op::StoreField:
            required = 2;
            delta = -2;
            break;
        case Op::Jump:
        case Op::Halt:
            break;
        }
        if (height < required || height + delta < 0 ||
            height + delta > static_cast<std::int64_t>(kMaxStackSize)) {
            throw std::runtime_error("invalid bytecode stack operation");
        }
        const auto nextHeight = height + delta;
        if (instruction.op == Op::Halt) {
            if (height != 0) {
                throw std::runtime_error("bytecode HALT requires an empty stack");
            }
            continue;
        }
        if (instruction.op == Op::Jump) {
            pending.emplace(static_cast<std::size_t>(instruction.operand), nextHeight);
        } else if (instruction.op == Op::JumpIfFalse) {
            pending.emplace(static_cast<std::size_t>(instruction.operand), nextHeight);
            pending.emplace(index + 1, nextHeight);
        } else {
            pending.emplace(index + 1, nextHeight);
        }
    }
}

void Jit::setInput(std::istream& input) noexcept {
    input_ = &input;
}

void Jit::setOutput(std::ostream& output) noexcept {
    output_ = &output;
}

void Jit::setGlobal(std::string name, Value value) {
    if (name.empty()) {
        throw std::invalid_argument("global name must be non-empty");
    }
    validateValue(value);
    globals_[std::move(name)] = std::move(value);
}

Value Jit::getGlobal(const std::string& name) const {
    const auto value = globals_.find(name);
    if (value == globals_.end()) {
        throw std::out_of_range("unknown global variable '" + name + "'");
    }
    return value->second;
}

std::int64_t Jit::asInteger(const Value& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return *integer;
    }
    throw std::runtime_error("expected an integer value");
}

double Jit::asNumber(const Value& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*integer);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        return *number;
    }
    throw std::runtime_error("expected a numeric value");
}

std::string Jit::asString(const Value& value) {
    if (const auto* string = std::get_if<std::string>(&value)) {
        return *string;
    }
    throw std::runtime_error("expected a string value");
}

std::string Jit::toString(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) return "null";
    if (const auto* integer = std::get_if<std::int64_t>(&value)) return std::to_string(*integer);
    if (const auto* number = std::get_if<double>(&value)) {
        std::ostringstream stream;
        stream << *number;
        return stream.str();
    }
    if (const auto* boolean = std::get_if<bool>(&value)) return *boolean ? "true" : "false";
    if (const auto* string = std::get_if<std::string>(&value)) return *string;
    const auto& object = std::get<ObjectPtr>(value);
    return object ? "[" + object->className + "]" : "null";
}

} // namespace hybridlang
