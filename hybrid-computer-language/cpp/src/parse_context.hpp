#pragma once

#include "hybrid_parser.hpp"
#include "hybridlang/ast.hpp"

#include <string>

namespace hybridlang {

class ParseContext {
public:
    explicit ParseContext(std::string source);

    BisonParser::symbol_type lex();
    void setStatements(StmtList statements);
    const StmtList& statements() const noexcept;
    [[noreturn]] void report(
        const BisonParser::location_type& location,
        const std::string& message
    ) const;

private:
    bool atEnd() const noexcept;
    char peek(std::size_t distance = 0) const noexcept;
    char advance() noexcept;
    bool match(char expected);
    void skipWhitespaceAndComments();
    std::string scanIdentifier();
    std::string scanNumber();
    std::string scanString();
    BisonParser::location_type location(
        std::size_t line,
        std::size_t column
    ) const;

    std::string source_;
    StmtList statements_;
    std::size_t offset_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
};

} // namespace hybridlang
