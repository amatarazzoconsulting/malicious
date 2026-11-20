#include "hybridlang/jit.hpp"

#include "hybrid_parser.hpp"
#include "hybridlang/ast.hpp"
#include "parse_context.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/AsmParser/Parser.h>
#include <llvm/Support/SourceMgr.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hybridlang {
namespace {

using VariableMap = std::unordered_map<std::string, llvm::AllocaInst*>;
using ClassTypeMap = std::unordered_map<std::string, std::string>;

llvm::Type* llvmType(llvm::LLVMContext& context, Type type) {
    switch (type) {
    case Type::Void: return llvm::Type::getVoidTy(context);
    case Type::I64: return llvm::Type::getInt64Ty(context);
    case Type::F64: return llvm::Type::getDoubleTy(context);
    case Type::Bool: return llvm::Type::getInt1Ty(context);
    case Type::Pointer: return llvm::PointerType::getUnqual(context);
    }
    throw std::runtime_error("unsupported external type");
}

bool isNumeric(Type type) {
    return type == Type::I64 || type == Type::F64;
}

struct TypedValue {
    llvm::Value* value;
    Type type;
    std::string className;
};

class CodeGenerator {
public:
    CodeGenerator(
        llvm::LLVMContext& context,
        llvm::Module& module,
        const std::unordered_map<std::string, Jit::ExternalFunction>& functions,
        const std::unordered_map<std::string, Jit::ExternalClass>& classes
    )
        : context_(context),
          module_(module),
          builder_(context),
          functions_(functions),
          classes_(classes) {}

    void generate(const StmtList& statements) {
        auto* functionType = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(context_),
            false
        );
        auto* function = llvm::Function::Create(
            functionType,
            llvm::Function::ExternalLinkage,
            "main",
            module_
        );
        auto* entry = llvm::BasicBlock::Create(context_, "entry", function);
        builder_.SetInsertPoint(entry);
        emitStatements(statements);
        if (!builder_.GetInsertBlock()->getTerminator()) {
            builder_.CreateRet(llvm::ConstantInt::get(
                llvm::Type::getInt32Ty(context_),
                0
            ));
        }
    }

private:
    llvm::AllocaInst* createVariable(const std::string& name, Type type) {
        auto* function = builder_.GetInsertBlock()->getParent();
        llvm::IRBuilder<> entryBuilder(&function->getEntryBlock(),
                                       function->getEntryBlock().begin());
        return entryBuilder.CreateAlloca(llvmType(context_, type), nullptr, name);
    }

    llvm::Function* declareExternal(
        const std::string& symbol,
        const Signature& signature
    ) {
        std::vector<llvm::Type*> parameters;
        parameters.reserve(signature.parameters.size());
        for (const auto parameter : signature.parameters) {
            if (parameter == Type::Void) {
                throw std::runtime_error("external functions cannot take void parameters");
            }
            parameters.push_back(llvmType(context_, parameter));
        }
        auto* functionType = llvm::FunctionType::get(
            llvmType(context_, signature.result),
            parameters,
            false
        );
        if (auto* existing = module_.getFunction(symbol)) {
            if (existing->getFunctionType() != functionType) {
                throw std::runtime_error(
                    "conflicting external ABI declarations for symbol: " + symbol
                );
            }
            return existing;
        }
        return llvm::Function::Create(
            functionType,
            llvm::Function::ExternalLinkage,
            symbol,
            module_
        );
    }

    llvm::Value* convert(llvm::Value* value, Type from, Type to) {
        if (from == to) {
            return value;
        }
        if (from == Type::I64 && to == Type::F64) {
            return builder_.CreateSIToFP(value, llvmType(context_, to));
        }
        if (from == Type::F64 && to == Type::I64) {
            return builder_.CreateFPToSI(value, llvmType(context_, to));
        }
        throw std::runtime_error("incompatible value types");
    }

    std::string classOf(const ExprPtr& expression) const {
        if (expression->kind == Expr::Kind::Variable) {
            const auto found = variableClasses_.find(expression->name);
            return found == variableClasses_.end() ? std::string{} : found->second;
        }
        if (expression->kind == Expr::Kind::Call) {
            return classes_.count(expression->name) != 0 ? expression->name : std::string{};
        }
        throw std::runtime_error("class receivers must be variables or constructor calls");
    }

    TypedValue emitExpression(const ExprPtr& expression) {
        switch (expression->kind) {
        case Expr::Kind::Literal:
            return std::visit(
                [this](const auto& value) -> TypedValue {
                    using ValueType = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<ValueType, std::int64_t>) {
                        return {llvm::ConstantInt::get(
                                    llvm::Type::getInt64Ty(context_),
                                    static_cast<std::uint64_t>(value),
                                    true),
                                Type::I64,
                                {}};
                    } else if constexpr (std::is_same_v<ValueType, double>) {
                        if (!std::isfinite(value)) {
                            throw std::runtime_error("floating-point literals must be finite");
                        }
                        return {llvm::ConstantFP::get(
                                    llvm::Type::getDoubleTy(context_),
                                    value),
                                Type::F64,
                                {}};
                    } else if constexpr (std::is_same_v<ValueType, bool>) {
                        return {llvm::ConstantInt::get(
                                    llvm::Type::getInt1Ty(context_),
                                    value),
                                Type::Bool,
                                {}};
                    } else {
                        throw std::runtime_error("string values are not supported by this AOT subset");
                    }
                },
                expression->literal
            );
        case Expr::Kind::Variable: {
            const auto found = variables_.find(expression->name);
            if (found == variables_.end()) {
                throw std::runtime_error("unknown variable: " + expression->name);
            }
            auto* pointer = found->second;
            const auto type = typeOf(pointer->getAllocatedType());
            const auto classFound = variableClasses_.find(expression->name);
            return {builder_.CreateLoad(pointer->getAllocatedType(), pointer, expression->name),
                    type,
                    classFound == variableClasses_.end() ? std::string{} : classFound->second};
        }
        case Expr::Kind::Unary: {
            auto operand = emitExpression(expression->left);
            if (expression->operation == "!") {
                auto* condition = toBoolean(operand);
                return {builder_.CreateNot(condition), Type::Bool, {}};
            }
            if (expression->operation == "~" && operand.type == Type::I64) {
                return {builder_.CreateNot(operand.value), Type::I64, {}};
            }
            if (expression->operation == "-" && isNumeric(operand.type)) {
                return {operand.type == Type::F64
                            ? builder_.CreateFNeg(operand.value)
                            : builder_.CreateNeg(operand.value),
                        operand.type,
                        {}};
            }
            if (expression->operation == "+" && isNumeric(operand.type)) {
                return operand;
            }
            throw std::runtime_error("invalid unary operator/type combination");
        }
        case Expr::Kind::Binary:
            return emitBinary(expression);
        case Expr::Kind::Call:
            return emitCall(expression);
        case Expr::Kind::MethodCall:
            return emitMethodCall(expression);
        case Expr::Kind::Field:
            return emitField(expression);
        }
        throw std::runtime_error("unsupported expression node");
    }

    Type typeOf(llvm::Type* type) const {
        if (type->isIntegerTy(1)) return Type::Bool;
        if (type->isIntegerTy(64)) return Type::I64;
        if (type->isDoubleTy()) return Type::F64;
        if (type->isPointerTy()) return Type::Pointer;
        throw std::runtime_error("unsupported local variable type");
    }

    llvm::Value* toBoolean(const TypedValue& value) {
        if (value.type == Type::Bool) {
            return value.value;
        }
        if (value.type == Type::I64) {
            return builder_.CreateICmpNE(
                value.value,
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0)
            );
        }
        if (value.type == Type::F64) {
            return builder_.CreateFCmpONE(
                value.value,
                llvm::ConstantFP::get(llvm::Type::getDoubleTy(context_), 0.0)
            );
        }
        throw std::runtime_error("condition must be numeric or boolean");
    }

    TypedValue emitBinary(const ExprPtr& expression) {
        const auto& operation = expression->operation;
        if (operation == "&&" || operation == "||") {
            auto left = emitExpression(expression->left);
            auto* leftBoolean = toBoolean(left);
            auto* function = builder_.GetInsertBlock()->getParent();
            auto* leftBlock = builder_.GetInsertBlock();
            auto* rightBlock = llvm::BasicBlock::Create(context_, "logic.rhs", function);
            auto* endBlock = llvm::BasicBlock::Create(context_, "logic.end", function);
            if (operation == "&&") {
                builder_.CreateCondBr(leftBoolean, rightBlock, endBlock);
            } else {
                builder_.CreateCondBr(leftBoolean, endBlock, rightBlock);
            }
            builder_.SetInsertPoint(rightBlock);
            const auto right = emitExpression(expression->right);
            auto* rightBoolean = toBoolean(right);
            auto* rightEnd = builder_.GetInsertBlock();
            builder_.CreateBr(endBlock);
            builder_.SetInsertPoint(endBlock);
            auto* result = builder_.CreatePHI(llvm::Type::getInt1Ty(context_), 2);
            result->addIncoming(
                llvm::ConstantInt::get(llvm::Type::getInt1Ty(context_),
                                       operation == "||"),
                leftBlock
            );
            result->addIncoming(rightBoolean, rightEnd);
            return {result, Type::Bool, {}};
        }

        auto left = emitExpression(expression->left);
        auto right = emitExpression(expression->right);
        if (isNumeric(left.type) && isNumeric(right.type) &&
            (left.type == Type::F64 || right.type == Type::F64)) {
            left.value = convert(left.value, left.type, Type::F64);
            right.value = convert(right.value, right.type, Type::F64);
            left.type = right.type = Type::F64;
        }
        if (operation == "+" || operation == "-" || operation == "*" ||
            operation == "/" || operation == "%") {
            if (!isNumeric(left.type) || left.type != right.type) {
                throw std::runtime_error("arithmetic operands must be numeric");
            }
            llvm::Value* result = nullptr;
            if (left.type == Type::F64) {
                if (operation == "+") result = builder_.CreateFAdd(left.value, right.value);
                if (operation == "-") result = builder_.CreateFSub(left.value, right.value);
                if (operation == "*") result = builder_.CreateFMul(left.value, right.value);
                if (operation == "/") result = builder_.CreateFDiv(left.value, right.value);
                if (operation == "%") result = builder_.CreateFRem(left.value, right.value);
            } else {
                if (operation == "+") result = builder_.CreateAdd(left.value, right.value);
                if (operation == "-") result = builder_.CreateSub(left.value, right.value);
                if (operation == "*") result = builder_.CreateMul(left.value, right.value);
                if (operation == "/") result = builder_.CreateSDiv(left.value, right.value);
                if (operation == "%") result = builder_.CreateSRem(left.value, right.value);
            }
            return {result, left.type, {}};
        }
        if (operation == "&" || operation == "|" || operation == "^" ||
            operation == "<<" || operation == ">>") {
            if (left.type != Type::I64 || right.type != Type::I64) {
                throw std::runtime_error("bitwise operators require integer operands");
            }
            llvm::Value* result = nullptr;
            if (operation == "&") result = builder_.CreateAnd(left.value, right.value);
            if (operation == "|") result = builder_.CreateOr(left.value, right.value);
            if (operation == "^") result = builder_.CreateXor(left.value, right.value);
            if (operation == "<<") result = builder_.CreateShl(left.value, right.value);
            if (operation == ">>") result = builder_.CreateAShr(left.value, right.value);
            return {result, Type::I64, {}};
        }
        if (operation == "==" || operation == "!=" || operation == "<" ||
            operation == "<=" || operation == ">" || operation == ">=") {
            if (left.type != right.type || !isNumeric(left.type)) {
                throw std::runtime_error("comparison operands must have the same numeric type");
            }
            llvm::Value* result = nullptr;
            if (left.type == Type::F64) {
                if (operation == "==") result = builder_.CreateFCmpOEQ(left.value, right.value);
                if (operation == "!=") result = builder_.CreateFCmpONE(left.value, right.value);
                if (operation == "<") result = builder_.CreateFCmpOLT(left.value, right.value);
                if (operation == "<=") result = builder_.CreateFCmpOLE(left.value, right.value);
                if (operation == ">") result = builder_.CreateFCmpOGT(left.value, right.value);
                if (operation == ">=") result = builder_.CreateFCmpOGE(left.value, right.value);
            } else {
                if (operation == "==") result = builder_.CreateICmpEQ(left.value, right.value);
                if (operation == "!=") result = builder_.CreateICmpNE(left.value, right.value);
                if (operation == "<") result = builder_.CreateICmpSLT(left.value, right.value);
                if (operation == "<=") result = builder_.CreateICmpSLE(left.value, right.value);
                if (operation == ">") result = builder_.CreateICmpSGT(left.value, right.value);
                if (operation == ">=") result = builder_.CreateICmpSGE(left.value, right.value);
            }
            return {result, Type::Bool, {}};
        }
        throw std::runtime_error("unsupported binary operator: " + operation);
    }

    TypedValue emitCall(const ExprPtr& expression) {
        const auto classFound = classes_.find(expression->name);
        if (classFound != classes_.end()) {
            const auto& definition = classFound->second;
            auto* function = declareExternal(
                definition.constructorSymbol,
                definition.constructor
            );
            auto arguments = emitArguments(expression->arguments, definition.constructor);
            return {builder_.CreateCall(function, arguments, "object"),
                    Type::Pointer,
                    expression->name};
        }
        const auto found = functions_.find(expression->name);
        if (found == functions_.end()) {
            throw std::runtime_error("unregistered external function: " + expression->name);
        }
        auto* function = declareExternal(found->second.linkerSymbol, found->second.signature);
        auto arguments = emitArguments(expression->arguments, found->second.signature);
        auto* call = builder_.CreateCall(function, arguments);
        return {call, found->second.signature.result, {}};
    }

    std::vector<llvm::Value*> emitArguments(
        const ExprList& expressions,
        const Signature& signature
    ) {
        if (expressions.size() != signature.parameters.size()) {
            throw std::runtime_error("external call has the wrong number of arguments");
        }
        std::vector<llvm::Value*> values;
        values.reserve(expressions.size());
        for (std::size_t index = 0; index < expressions.size(); ++index) {
            auto value = emitExpression(expressions[index]);
            if (value.type != signature.parameters[index] &&
                !(isNumeric(value.type) && isNumeric(signature.parameters[index]))) {
                throw std::runtime_error("external call argument type mismatch");
            }
            values.push_back(convert(
                value.value,
                value.type,
                signature.parameters[index]
            ));
        }
        return values;
    }

    TypedValue emitMethodCall(const ExprPtr& expression) {
        const auto className = classOf(expression->left);
        const auto classFound = classes_.find(className);
        if (classFound == classes_.end()) {
            throw std::runtime_error("method receiver is not a registered class");
        }
        const auto methodFound = classFound->second.methods.find(expression->name);
        if (methodFound == classFound->second.methods.end()) {
            throw std::runtime_error("unregistered external method: " + className + "." + expression->name);
        }
        auto object = emitExpression(expression->left);
        auto* function = declareExternal(
            methodFound->second.linkerSymbol,
            methodFound->second.signature
        );
        const auto& signature = methodFound->second.signature;
        if (signature.parameters.empty() || signature.parameters.front() != Type::Pointer ||
            expression->arguments.size() + 1 != signature.parameters.size()) {
            throw std::runtime_error("external method ABI must begin with a pointer receiver");
        }
        std::vector<llvm::Value*> arguments{object.value};
        for (std::size_t index = 0; index < expression->arguments.size(); ++index) {
            auto value = emitExpression(expression->arguments[index]);
            const auto parameter = signature.parameters[index + 1];
            if (value.type != parameter &&
                !(isNumeric(value.type) && isNumeric(parameter))) {
                throw std::runtime_error("external method argument type mismatch");
            }
            arguments.push_back(convert(value.value, value.type, parameter));
        }
        auto* call = builder_.CreateCall(function, arguments);
        return {call, signature.result, {}};
    }

    TypedValue emitField(const ExprPtr& expression) {
        const auto className = classOf(expression->left);
        const auto classFound = classes_.find(className);
        if (classFound == classes_.end()) {
            throw std::runtime_error("field receiver is not a registered class");
        }
        const auto fieldFound = classFound->second.fields.find(expression->name);
        if (fieldFound == classFound->second.fields.end()) {
            throw std::runtime_error("unregistered external field: " + className + "." + expression->name);
        }
        const auto& field = fieldFound->second;
        const Signature signature{field.type, {Type::Pointer}};
        auto* getter = declareExternal(field.getterSymbol, signature);
        auto object = emitExpression(expression->left);
        return {builder_.CreateCall(getter, {object.value}),
                field.type,
                {}};
    }

    void emitStatements(const StmtList& statements) {
        for (const auto& statement : statements) {
            if (builder_.GetInsertBlock()->getTerminator()) {
                break;
            }
            emitStatement(statement);
        }
    }

    void emitStatement(const StmtPtr& statement) {
        switch (statement->kind) {
        case Stmt::Kind::Assign: {
            auto value = emitExpression(statement->expression);
            if (value.type == Type::Void) {
                throw std::runtime_error("cannot assign the result of a void function");
            }
            const auto found = variables_.find(statement->name);
            if (found == variables_.end()) {
                Type type = value.type;
                if (!statement->declaredType.empty()) {
                    if (statement->declaredType == "int" ||
                        statement->declaredType == "long") type = Type::I64;
                    else if (statement->declaredType == "float" ||
                             statement->declaredType == "double") type = Type::F64;
                    else if (statement->declaredType == "bool") type = Type::Bool;
                    else if (statement->declaredType == "auto") type = value.type;
                    else throw std::runtime_error("unsupported declared variable type");
                    if (type != value.type &&
                        !(isNumeric(type) && isNumeric(value.type))) {
                        throw std::runtime_error("initializer does not match declared type");
                    }
                }
                auto* slot = createVariable(statement->name, type);
                variables_.emplace(statement->name, slot);
                if (!value.className.empty()) {
                    variableClasses_[statement->name] = value.className;
                }
                builder_.CreateStore(convert(value.value, value.type, type), slot);
            } else {
                const auto type = typeOf(found->second->getAllocatedType());
                if (!value.className.empty()) {
                    throw std::runtime_error("class variables cannot be reassigned in this subset");
                }
                builder_.CreateStore(convert(value.value, value.type, type), found->second);
            }
            return;
        }
        case Stmt::Kind::FieldAssign: {
            const auto className = classOf(statement->object);
            const auto classFound = classes_.find(className);
            if (classFound == classes_.end()) {
                throw std::runtime_error("field receiver is not a registered class");
            }
            const auto fieldFound = classFound->second.fields.find(statement->fieldName);
            if (fieldFound == classFound->second.fields.end()) {
                throw std::runtime_error("unregistered external field: " + className + "." + statement->fieldName);
            }
            const auto& field = fieldFound->second;
            const Signature signature{Type::Void, {Type::Pointer, field.type}};
            auto* setter = declareExternal(field.setterSymbol, signature);
            auto object = emitExpression(statement->object);
            auto value = emitExpression(statement->expression);
            if (value.type != field.type &&
                !(isNumeric(value.type) && isNumeric(field.type))) {
                throw std::runtime_error("field assignment type mismatch");
            }
            builder_.CreateCall(setter, {
                object.value,
                convert(value.value, value.type, field.type)
            });
            return;
        }
        case Stmt::Kind::Expression:
            emitExpression(statement->expression);
            return;
        case Stmt::Kind::Block:
            emitStatements(statement->body);
            return;
        case Stmt::Kind::If:
            emitIf(statement);
            return;
        case Stmt::Kind::While:
            emitWhile(statement);
            return;
        }
    }

    void emitIf(const StmtPtr& statement) {
        auto condition = emitExpression(statement->expression);
        auto* function = builder_.GetInsertBlock()->getParent();
        auto* thenBlock = llvm::BasicBlock::Create(context_, "if.then", function);
        auto* mergeBlock = llvm::BasicBlock::Create(context_, "if.end", function);
        auto* elseBlock = statement->otherwise.empty()
            ? mergeBlock
            : llvm::BasicBlock::Create(context_, "if.else", function);
        builder_.CreateCondBr(toBoolean(condition), thenBlock, elseBlock);
        builder_.SetInsertPoint(thenBlock);
        emitStatements(statement->body);
        if (!builder_.GetInsertBlock()->getTerminator()) {
            builder_.CreateBr(mergeBlock);
        }
        if (!statement->otherwise.empty()) {
            builder_.SetInsertPoint(elseBlock);
            emitStatements(statement->otherwise);
            if (!builder_.GetInsertBlock()->getTerminator()) {
                builder_.CreateBr(mergeBlock);
            }
        }
        builder_.SetInsertPoint(mergeBlock);
    }

    void emitWhile(const StmtPtr& statement) {
        auto* function = builder_.GetInsertBlock()->getParent();
        auto* conditionBlock = llvm::BasicBlock::Create(context_, "while.cond", function);
        auto* bodyBlock = llvm::BasicBlock::Create(context_, "while.body", function);
        auto* endBlock = llvm::BasicBlock::Create(context_, "while.end", function);
        builder_.CreateBr(conditionBlock);
        builder_.SetInsertPoint(conditionBlock);
        auto condition = emitExpression(statement->expression);
        builder_.CreateCondBr(toBoolean(condition), bodyBlock, endBlock);
        builder_.SetInsertPoint(bodyBlock);
        emitStatements(statement->body);
        if (!builder_.GetInsertBlock()->getTerminator()) {
            builder_.CreateBr(conditionBlock);
        }
        builder_.SetInsertPoint(endBlock);
    }

    llvm::LLVMContext& context_;
    llvm::Module& module_;
    llvm::IRBuilder<> builder_;
    const std::unordered_map<std::string, Jit::ExternalFunction>& functions_;
    const std::unordered_map<std::string, Jit::ExternalClass>& classes_;
    VariableMap variables_;
    ClassTypeMap variableClasses_;
};

void validateSignature(const Signature& signature) {
    if (signature.parameters.size() > 32) {
        throw std::invalid_argument("external signatures are limited to 32 parameters");
    }
    for (const auto type : signature.parameters) {
        if (type == Type::Void) {
            throw std::invalid_argument("external parameters cannot have void type");
        }
    }
}

} // namespace

Jit::Jit() = default;

void Jit::registerFunction(
    std::string sourceName,
    std::string linkerSymbol,
    Signature signature
) {
    if (sourceName.empty() || linkerSymbol.empty()) {
        throw std::invalid_argument("function names and linker symbols cannot be empty");
    }
    validateSignature(signature);
    if (classes_.count(sourceName) != 0) {
        throw std::invalid_argument("source name is already registered as a class");
    }
    const auto inserted = functions_.emplace(
        std::move(sourceName),
        ExternalFunction{std::move(linkerSymbol), std::move(signature)}
    );
    if (!inserted.second) {
        throw std::invalid_argument("source function name is already registered");
    }
}

void Jit::registerClass(
    std::string className,
    std::string constructorSymbol,
    Signature constructor
) {
    if (className.empty() || constructorSymbol.empty()) {
        throw std::invalid_argument("class names and constructor symbols cannot be empty");
    }
    validateSignature(constructor);
    if (constructor.result != Type::Pointer) {
        throw std::invalid_argument("class constructors must return an opaque pointer");
    }
    if (functions_.count(className) != 0) {
        throw std::invalid_argument("source name is already registered as a function");
    }
    const auto inserted = classes_.emplace(
        std::move(className),
        ExternalClass{std::move(constructorSymbol), std::move(constructor), {}, {}}
    );
    if (!inserted.second) {
        throw std::invalid_argument("class name is already registered");
    }
}

void Jit::registerMethod(
    std::string className,
    std::string methodName,
    std::string linkerSymbol,
    Signature signature
) {
    const auto found = classes_.find(className);
    if (found == classes_.end()) {
        throw std::invalid_argument("register the class before its methods");
    }
    if (methodName.empty() || linkerSymbol.empty() || signature.parameters.empty() ||
        signature.parameters.front() != Type::Pointer) {
        throw std::invalid_argument("method ABI requires a pointer receiver as its first parameter");
    }
    validateSignature(signature);
    const auto inserted = found->second.methods.emplace(
        std::move(methodName),
        ExternalFunction{std::move(linkerSymbol), std::move(signature)}
    );
    if (!inserted.second) {
        throw std::invalid_argument("class method is already registered");
    }
}

void Jit::registerField(
    std::string className,
    std::string fieldName,
    std::string getterSymbol,
    std::string setterSymbol,
    Type type
) {
    const auto found = classes_.find(className);
    if (found == classes_.end()) {
        throw std::invalid_argument("register the class before its fields");
    }
    if (fieldName.empty() || getterSymbol.empty() || setterSymbol.empty() ||
        type == Type::Void || type == Type::Pointer) {
        throw std::invalid_argument("fields require names and a scalar type");
    }
    const auto inserted = found->second.fields.emplace(
        std::move(fieldName),
        ExternalField{std::move(getterSymbol), std::move(setterSymbol), type}
    );
    if (!inserted.second) {
        throw std::invalid_argument("class field is already registered");
    }
}

void Jit::compile(const std::string& source) {
    ParseContext context(source);
    BisonParser parser(context);
    if (parser.parse() != 0) {
        throw std::runtime_error("source parsing failed");
    }

    llvm::LLVMContext llvmContext;
    llvm::Module module("hybrid-aot", llvmContext);
    module.setTargetTriple(llvm::sys::getDefaultTargetTriple());
    CodeGenerator generator(llvmContext, module, functions_, classes_);
    generator.generate(context.statements());
    std::string diagnostic;
    llvm::raw_string_ostream output(diagnostic);
    if (llvm::verifyModule(module, &output)) {
        output.flush();
        throw std::runtime_error("LLVM IR verification failed: " + diagnostic);
    }
    llvm::raw_string_ostream irStream(ir_);
    module.print(irStream, nullptr);
    irStream.flush();
}

const std::string& Jit::emitIR() const {
    if (ir_.empty()) {
        throw std::runtime_error("compile source before requesting LLVM IR");
    }
    return ir_;
}

void Jit::writeObject(const std::string& path) const {
    if (ir_.empty()) {
        throw std::runtime_error("compile source before writing an object file");
    }
    if (path.empty()) {
        throw std::invalid_argument("object file path cannot be empty");
    }
    static std::once_flag initializeTargets;
    std::call_once(initializeTargets, [] {
        if (llvm::InitializeNativeTarget()) {
            throw std::runtime_error("LLVM has no native target configured");
        }
        if (llvm::InitializeNativeTargetAsmPrinter()) {
            throw std::runtime_error("LLVM has no native assembly printer configured");
        }
    });

    llvm::LLVMContext context;
    llvm::SMDiagnostic parseError;
    auto module = llvm::parseAssemblyString(ir_, parseError, context);
    if (!module) {
        std::string diagnostic;
        llvm::raw_string_ostream output(diagnostic);
        parseError.print("hybridlang", output);
        output.flush();
        throw std::runtime_error("could not parse generated LLVM IR: " + diagnostic);
    }

    const auto triple = llvm::sys::getDefaultTargetTriple();
    std::string targetError;
    const auto* target = llvm::TargetRegistry::lookupTarget(triple, targetError);
    if (target == nullptr) {
        throw std::runtime_error("could not select native LLVM target: " + targetError);
    }
    llvm::TargetOptions options;
    std::unique_ptr<llvm::TargetMachine> machine(
        target->createTargetMachine(
            triple,
            "generic",
            "",
            options,
            std::nullopt,
            std::nullopt,
            llvm::CodeGenOptLevel::Default,
            false
        )
    );
    if (!machine) {
        throw std::runtime_error("could not create native LLVM target machine");
    }
    module->setDataLayout(machine->createDataLayout());

    std::error_code error;
    llvm::raw_fd_ostream output(path, error, llvm::sys::fs::OF_None);
    if (error) {
        throw std::runtime_error("could not open object output: " + error.message());
    }
    llvm::legacy::PassManager passes;
    if (machine->addPassesToEmitFile(
            passes,
            output,
            nullptr,
            llvm::CodeGenFileType::ObjectFile
        )) {
        throw std::runtime_error("native LLVM target cannot emit object files");
    }
    passes.run(*module);
    output.flush();
    if (output.has_error()) {
        throw std::runtime_error("failed while writing object output");
    }
}

} // namespace hybridlang
