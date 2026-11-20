%skeleton "lalr1.cc"
%require "3.8"
%define api.namespace {hybridlang}
%define api.parser.class {BisonParser}
%define api.value.type variant
%define api.token.constructor
%define parse.error detailed
%locations

%parse-param { hybridlang::ParseContext& driver }
%lex-param { hybridlang::ParseContext& driver }

%code requires {
    #include "hybridlang/ast.hpp"
    #include <string>
    #include <vector>

    namespace hybridlang {
    class ParseContext;
    }
}

%code {
    #include "parse_context.hpp"

    static hybridlang::BisonParser::symbol_type yylex(
        hybridlang::ParseContext& driver
    ) {
        return driver.lex();
    }

    void hybridlang::BisonParser::error(
        const location_type& location,
        const std::string& message
    ) {
        driver.report(location, message);
    }
}

%token END 0 "end of file"
%token <std::string> IDENT STRING
%token <std::int64_t> INTEGER
%token <double> REAL
%token LET VAR CONST INT_TYPE LONG_TYPE FLOAT_TYPE DOUBLE_TYPE BOOL_TYPE STRING_TYPE AUTO_TYPE
%token IF ELSE WHILE TRUE FALSE
%token EQ NE LE GE AND OR SHL SHR SCOPE NOT
%token ASSIGN SEMICOLON LPAREN RPAREN LBRACE RBRACE COMMA DOT
%token PLUS MINUS STAR SLASH PERCENT BITAND BITOR BITXOR BITNOT LT GT

%type <hybridlang::StmtList> statements
%type <hybridlang::StmtPtr> statement
%type <hybridlang::ExprPtr> expression unary primary
%type <hybridlang::ExprList> arguments arguments_opt
%type <std::string> declared_type qualified_name

%left OR
%left AND
%left BITOR
%left BITXOR
%left BITAND
%left EQ NE
%left LT LE GT GE
%left SHL SHR
%left PLUS MINUS
%left STAR SLASH PERCENT
%precedence IF_WITHOUT_ELSE
%precedence ELSE

%start program

%%

program:
    statements {
        driver.setStatements(std::move($1));
    }
;

statements:
    %empty {
        $$ = {};
    }
  | statements statement {
        $$ = std::move($1);
        $$.push_back(std::move($2));
    }
;

statement:
    declared_type IDENT ASSIGN expression SEMICOLON {
        $$ = hybridlang::makeAssignment(std::move($2), std::move($4), std::move($1));
    }
  | LET IDENT ASSIGN expression SEMICOLON {
        $$ = hybridlang::makeAssignment(std::move($2), std::move($4));
    }
  | VAR IDENT ASSIGN expression SEMICOLON {
        $$ = hybridlang::makeAssignment(std::move($2), std::move($4));
    }
  | CONST IDENT ASSIGN expression SEMICOLON {
        $$ = hybridlang::makeAssignment(std::move($2), std::move($4));
    }
  | IDENT ASSIGN expression SEMICOLON {
        $$ = hybridlang::makeAssignment(std::move($1), std::move($3));
    }
  | primary DOT IDENT ASSIGN expression SEMICOLON {
        $$ = hybridlang::makeFieldAssignment(
            std::move($1), std::move($3), std::move($5)
        );
    }
  | expression SEMICOLON {
        $$ = hybridlang::makeExpressionStatement(std::move($1));
    }
  | LBRACE statements RBRACE {
        $$ = hybridlang::makeBlock(std::move($2));
    }
  | IF LPAREN expression RPAREN statement %prec IF_WITHOUT_ELSE {
        hybridlang::StmtList body;
        if ($5->kind == hybridlang::Stmt::Kind::Block) {
            body = std::move($5->body);
        } else {
            body.push_back(std::move($5));
        }
        $$ = hybridlang::makeIf(std::move($3), std::move(body));
    }
  | IF LPAREN expression RPAREN statement ELSE statement {
        hybridlang::StmtList body;
        hybridlang::StmtList otherwise;
        if ($5->kind == hybridlang::Stmt::Kind::Block) {
            body = std::move($5->body);
        } else {
            body.push_back(std::move($5));
        }
        if ($7->kind == hybridlang::Stmt::Kind::Block) {
            otherwise = std::move($7->body);
        } else {
            otherwise.push_back(std::move($7));
        }
        $$ = hybridlang::makeIf(std::move($3), std::move(body), std::move(otherwise));
    }
  | WHILE LPAREN expression RPAREN statement {
        hybridlang::StmtList body;
        if ($5->kind == hybridlang::Stmt::Kind::Block) {
            body = std::move($5->body);
        } else {
            body.push_back(std::move($5));
        }
        $$ = hybridlang::makeWhile(std::move($3), std::move(body));
    }
;

declared_type:
    INT_TYPE { $$ = "int"; }
  | LONG_TYPE { $$ = "long"; }
  | FLOAT_TYPE { $$ = "float"; }
  | DOUBLE_TYPE { $$ = "double"; }
  | BOOL_TYPE { $$ = "bool"; }
  | STRING_TYPE { $$ = "string"; }
  | AUTO_TYPE { $$ = "auto"; }
;

expression:
    expression OR expression { $$ = hybridlang::makeBinary("||", std::move($1), std::move($3)); }
  | expression AND expression { $$ = hybridlang::makeBinary("&&", std::move($1), std::move($3)); }
  | expression BITOR expression { $$ = hybridlang::makeBinary("|", std::move($1), std::move($3)); }
  | expression BITXOR expression { $$ = hybridlang::makeBinary("^", std::move($1), std::move($3)); }
  | expression BITAND expression { $$ = hybridlang::makeBinary("&", std::move($1), std::move($3)); }
  | expression EQ expression { $$ = hybridlang::makeBinary("==", std::move($1), std::move($3)); }
  | expression NE expression { $$ = hybridlang::makeBinary("!=", std::move($1), std::move($3)); }
  | expression LT expression { $$ = hybridlang::makeBinary("<", std::move($1), std::move($3)); }
  | expression LE expression { $$ = hybridlang::makeBinary("<=", std::move($1), std::move($3)); }
  | expression GT expression { $$ = hybridlang::makeBinary(">", std::move($1), std::move($3)); }
  | expression GE expression { $$ = hybridlang::makeBinary(">=", std::move($1), std::move($3)); }
  | expression SHL expression { $$ = hybridlang::makeBinary("<<", std::move($1), std::move($3)); }
  | expression SHR expression { $$ = hybridlang::makeBinary(">>", std::move($1), std::move($3)); }
  | expression PLUS expression { $$ = hybridlang::makeBinary("+", std::move($1), std::move($3)); }
  | expression MINUS expression { $$ = hybridlang::makeBinary("-", std::move($1), std::move($3)); }
  | expression STAR expression { $$ = hybridlang::makeBinary("*", std::move($1), std::move($3)); }
  | expression SLASH expression { $$ = hybridlang::makeBinary("/", std::move($1), std::move($3)); }
  | expression PERCENT expression { $$ = hybridlang::makeBinary("%", std::move($1), std::move($3)); }
  | unary { $$ = std::move($1); }
;

unary:
    NOT unary { $$ = hybridlang::makeUnary("!", std::move($2)); }
  | BITNOT unary { $$ = hybridlang::makeUnary("~", std::move($2)); }
  | MINUS unary { $$ = hybridlang::makeUnary("-", std::move($2)); }
  | PLUS unary { $$ = hybridlang::makeUnary("+", std::move($2)); }
  | primary { $$ = std::move($1); }
;

primary:
    INTEGER { $$ = hybridlang::makeLiteral($1); }
  | REAL { $$ = hybridlang::makeLiteral($1); }
  | STRING { $$ = hybridlang::makeLiteral(std::move($1)); }
  | TRUE { $$ = hybridlang::makeLiteral(true); }
  | FALSE { $$ = hybridlang::makeLiteral(false); }
  | qualified_name { $$ = hybridlang::makeVariable(std::move($1)); }
  | qualified_name LPAREN arguments_opt RPAREN {
        $$ = hybridlang::makeCall(std::move($1), std::move($3));
    }
  | primary DOT IDENT {
        $$ = hybridlang::makeField(std::move($1), std::move($3));
    }
  | primary DOT IDENT LPAREN arguments_opt RPAREN {
        $$ = hybridlang::makeMethodCall(std::move($1), std::move($3), std::move($5));
    }
  | LPAREN expression RPAREN { $$ = std::move($2); }
;

qualified_name:
    IDENT { $$ = std::move($1); }
  | qualified_name SCOPE IDENT { $$ = std::move($1) + "::" + std::move($3); }
;

arguments_opt:
    %empty { $$ = {}; }
  | arguments { $$ = std::move($1); }
;

arguments:
    expression { $$ = {std::move($1)}; }
  | arguments COMMA expression {
        $$ = std::move($1);
        $$.push_back(std::move($3));
    }
;

%%
