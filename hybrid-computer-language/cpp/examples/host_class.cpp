#include "hybridlang/jit.hpp"

#include <iostream>

int main() {
    using hybridlang::Signature;
    using hybridlang::Type;

    hybridlang::Jit compiler;
    compiler.registerClass(
        "Counter",
        "counter_create",
        Signature{Type::Pointer, {Type::I64}}
    );
    compiler.registerMethod(
        "Counter",
        "increment",
        "counter_increment",
        Signature{Type::I64, {Type::Pointer, Type::I64}}
    );
    compiler.registerField(
        "Counter",
        "value",
        "counter_get_value",
        "counter_set_value",
        Type::I64
    );
    compiler.compile(
        "let count = Counter(10); "
        "count.increment(2); "
        "count.value = count.value + 1;"
    );
    std::cout << compiler.emitIR();
}
