#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace hybridlang {

enum class Type {
    Void,
    I64,
    F64,
    Bool,
    Pointer
};

struct Signature {
    Type result = Type::Void;
    std::vector<Type> parameters;
};

class Jit {
public:
    struct ExternalFunction {
        std::string linkerSymbol;
        Signature signature;
    };

    struct ExternalField {
        std::string getterSymbol;
        std::string setterSymbol;
        Type type;
    };

    struct ExternalClass {
        std::string constructorSymbol;
        Signature constructor;
        std::unordered_map<std::string, ExternalFunction> methods;
        std::unordered_map<std::string, ExternalField> fields;
    };

    Jit();

    void registerFunction(
        std::string sourceName,
        std::string linkerSymbol,
        Signature signature
    );
    void registerClass(
        std::string className,
        std::string constructorSymbol,
        Signature constructor
    );
    void registerMethod(
        std::string className,
        std::string methodName,
        std::string linkerSymbol,
        Signature signature
    );
    void registerField(
        std::string className,
        std::string fieldName,
        std::string getterSymbol,
        std::string setterSymbol,
        Type type
    );

    void compile(const std::string& source);
    const std::string& emitIR() const;
    void writeObject(const std::string& path) const;

private:
    std::unordered_map<std::string, ExternalFunction> functions_;
    std::unordered_map<std::string, ExternalClass> classes_;
    std::string ir_;
};

} // namespace hybridlang
