#include "hybridlang/jit.hpp"

#include <iostream>

int main() {
    hybridlang::Jit compiler;
    compiler.registerFunction(
        "std::square",
        "host_square",
        {hybridlang::Type::I64, {hybridlang::Type::I64}}
    );
    compiler.compile("let result = std::square(9);");
    std::cout << compiler.emitIR();
}
