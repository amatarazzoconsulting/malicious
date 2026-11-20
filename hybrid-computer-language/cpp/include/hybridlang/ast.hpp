#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace hybridlang {

struct Expr;
struct Stmt;
using ExprPtr = std::shared_ptr<Expr>;
using StmtPtr = std::shared_ptr<Stmt>;
using ExprList = std::vector<ExprPtr>;
using StmtList = std::vector<StmtPtr>;
using Literal = std::variant<std::int64_t, double, bool, std::string>;

struct Expr {
    enum class Kind { Literal, Variable, Unary, Binary, Call, Field, MethodCall };

    Kind kind = Kind::Literal;
    Literal literal = std::int64_t{0};
    std::string name;
    std::string operation;
    ExprPtr left;
    ExprPtr right;
    ExprList arguments;
};

struct Stmt {
    enum class Kind { Assign, FieldAssign, Expression, Block, If, While };

    Kind kind = Kind::Expression;
    std::string name;
    std::string fieldName;
    std::string declaredType;
    ExprPtr expression;
    ExprPtr object;
    StmtList body;
    StmtList otherwise;
};

inline ExprPtr makeLiteral(Literal value) {
    auto expression = std::make_shared<Expr>();
    expression->kind = Expr::Kind::Literal;
    expression->literal = std::move(value);
    return expression;
}

inline ExprPtr makeVariable(std::string name) {
    auto expression = std::make_shared<Expr>();
    expression->kind = Expr::Kind::Variable;
    expression->name = std::move(name);
    return expression;
}

inline ExprPtr makeUnary(std::string operation, ExprPtr operand) {
    auto expression = std::make_shared<Expr>();
    expression->kind = Expr::Kind::Unary;
    expression->operation = std::move(operation);
    expression->left = std::move(operand);
    return expression;
}

inline ExprPtr makeBinary(std::string operation, ExprPtr left, ExprPtr right) {
    auto expression = std::make_shared<Expr>();
    expression->kind = Expr::Kind::Binary;
    expression->operation = std::move(operation);
    expression->left = std::move(left);
    expression->right = std::move(right);
    return expression;
}

inline ExprPtr makeCall(std::string name, ExprList arguments) {
    auto expression = std::make_shared<Expr>();
    expression->kind = Expr::Kind::Call;
    expression->name = std::move(name);
    expression->arguments = std::move(arguments);
    return expression;
}

inline ExprPtr makeField(ExprPtr object, std::string name) {
    auto expression = std::make_shared<Expr>();
    expression->kind = Expr::Kind::Field;
    expression->name = std::move(name);
    expression->left = std::move(object);
    return expression;
}

inline ExprPtr makeMethodCall(ExprPtr object, std::string name, ExprList arguments) {
    auto expression = std::make_shared<Expr>();
    expression->kind = Expr::Kind::MethodCall;
    expression->name = std::move(name);
    expression->left = std::move(object);
    expression->arguments = std::move(arguments);
    return expression;
}

inline StmtPtr makeAssignment(
    std::string name,
    ExprPtr expression,
    std::string declaredType = {}
) {
    auto statement = std::make_shared<Stmt>();
    statement->kind = Stmt::Kind::Assign;
    statement->name = std::move(name);
    statement->expression = std::move(expression);
    statement->declaredType = std::move(declaredType);
    return statement;
}

inline StmtPtr makeFieldAssignment(
    ExprPtr object,
    std::string fieldName,
    ExprPtr expression
) {
    auto statement = std::make_shared<Stmt>();
    statement->kind = Stmt::Kind::FieldAssign;
    statement->object = std::move(object);
    statement->fieldName = std::move(fieldName);
    statement->expression = std::move(expression);
    return statement;
}

inline StmtPtr makeExpressionStatement(ExprPtr expression) {
    auto statement = std::make_shared<Stmt>();
    statement->kind = Stmt::Kind::Expression;
    statement->expression = std::move(expression);
    return statement;
}

inline StmtPtr makeBlock(StmtList statements) {
    auto statement = std::make_shared<Stmt>();
    statement->kind = Stmt::Kind::Block;
    statement->body = std::move(statements);
    return statement;
}

inline StmtPtr makeIf(ExprPtr condition, StmtList body, StmtList otherwise = {}) {
    auto statement = std::make_shared<Stmt>();
    statement->kind = Stmt::Kind::If;
    statement->expression = std::move(condition);
    statement->body = std::move(body);
    statement->otherwise = std::move(otherwise);
    return statement;
}

inline StmtPtr makeWhile(ExprPtr condition, StmtList body) {
    auto statement = std::make_shared<Stmt>();
    statement->kind = Stmt::Kind::While;
    statement->expression = std::move(condition);
    statement->body = std::move(body);
    return statement;
}

} // namespace hybridlang
