#include "hybridlang/jit.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using hybridlang::Jit;
using hybridlang::Signature;
using hybridlang::Type;

void testLLVMIRAndNativeCalls() {
    Jit compiler;
    compiler.registerFunction(
        "std::square",
        "host_square",
        Signature{Type::I64, {Type::I64}}
    );
    compiler.compile(
        "let x = 5; "
        "if (x > 0) { x = std::square(x); } "
        "while (x < 30) { x = x + 1; }"
    );
    const auto& ir = compiler.emitIR();
    assert(ir.find("declare i64 @host_square(i64)") != std::string::npos);
    assert(ir.find("call i64 @host_square(i64") != std::string::npos);
    assert(ir.find("while.cond") != std::string::npos);
}

void testClassABI() {
    Jit compiler;
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
        "let counter = Counter(4); "
        "counter.increment(3); "
        "counter.value = counter.value + 2;"
    );
    const auto& ir = compiler.emitIR();
    assert(ir.find("declare ptr @counter_create(i64)") != std::string::npos);
    assert(ir.find("declare i64 @counter_increment(ptr, i64)") != std::string::npos);
    assert(ir.find("declare i64 @counter_get_value(ptr)") != std::string::npos);
    assert(ir.find("declare void @counter_set_value(ptr, i64)") != std::string::npos);
}

void testObjectEmission() {
    Jit compiler;
    compiler.compile("let value = 40 + 2;");
    const std::string path = "hybrid-test-object.o";
    compiler.writeObject(path);
    std::ifstream object(path, std::ios::binary);
    char magic[4]{};
    object.read(magic, sizeof(magic));
    assert(object.gcount() == 4);
    assert(static_cast<unsigned char>(magic[0]) == 0x7f);
    assert(magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F');
    object.close();
    std::remove(path.c_str());
}

void testInvalidSourceAndSignatures() {
    Jit compiler;
    bool parseFailed = false;
    try {
        compiler.compile("let value = ;");
    } catch (const std::runtime_error&) {
        parseFailed = true;
    }
    assert(parseFailed);

    compiler.registerFunction(
        "native",
        "native_function",
        Signature{Type::I64, {Type::I64}}
    );
    bool typeFailed = false;
    try {
        compiler.compile("let value = native(true);");
    } catch (const std::runtime_error&) {
        typeFailed = true;
    }
    assert(typeFailed);
}

} // namespace

int main() {
    testLLVMIRAndNativeCalls();
    testClassABI();
    testObjectEmission();
    testInvalidSourceAndSignatures();
    std::cout << "C++ LLVM AOT tests passed\n";
}
