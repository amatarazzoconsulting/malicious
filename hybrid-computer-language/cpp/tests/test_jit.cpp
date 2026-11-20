#include "hybridlang/jit.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Counter {
    explicit Counter(std::int64_t initial) : value(initial) {}
    std::int64_t value;
};

void testCompileAndExecute() {
    hybridlang::Jit jit;
    std::ostringstream output;
    jit.setOutput(output);
    jit.compile("let x = 2; x = x * 3 + 1; println(x);");
    jit.run();
    assert(output.str() == "7\n");
    assert(hybridlang::Jit::asInteger(jit.getGlobal("x")) == 7);
}

void testRegisteredFunctionAndInput() {
    using namespace hybridlang;
    Jit jit;
    std::istringstream input("21\n");
    std::ostringstream output;
    jit.setInput(input);
    jit.setOutput(output);
    jit.registerKeyword("std::double", [](const Arguments& args) {
        const auto parsed = std::stoll(Jit::asString(args.at(0)));
        return Value{parsed * 2};
    });
    jit.compile("let result = std::double(input()); println(result);");
    jit.run();
    assert(output.str() == "42\n");
}

void testBooleanOperatorsShortCircuit() {
    using namespace hybridlang;
    Jit jit;
    std::int64_t calls = 0;
    std::ostringstream output;
    jit.setOutput(output);
    jit.registerFunction("std::bump", [&calls](const Arguments&) {
        ++calls;
        return Value{true};
    });
    jit.compile(
        "let left = false && std::bump(); "
        "let right = true || std::bump(); println(calls);"
    );
    jit.setGlobal("calls", std::int64_t{0});
    jit.run();
    assert(calls == 0);
    assert(output.str() == "0\n");
}

void testHostClassMethodsAndFields() {
    using namespace hybridlang;
    Jit jit;
    std::ostringstream output;
    jit.setOutput(output);
    jit.addNativeClass<Counter>("Counter", [](const Arguments& args) {
        return std::make_shared<Counter>(Jit::asInteger(args.at(0)));
    });
    jit.addNativeMethod<Counter>(
        "Counter",
        "increment",
        [](Counter& counter, const Arguments& args) -> Value {
            counter.value += Jit::asInteger(args.at(0));
            return Value{counter.value};
        }
    );
    jit.addNativeField<Counter>(
        "Counter",
        "value",
        [](const Counter& counter) -> Value { return Value{counter.value}; },
        [](Counter& counter, const Value& value) {
            counter.value = Jit::asInteger(value);
        }
    );
    jit.compile(
        "let counter = Counter(4); counter.increment(3); "
        "counter.value = counter.value + 2; println(counter.value);"
    );
    jit.run();
    assert(output.str() == "9\n");
}

void testBinaryRoundTrip() {
    using namespace hybridlang;
    Jit compiler;
    compiler.compile("let result = std::twice(21); println(result);");
    std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
    compiler.writeBinary(binary);
    binary.seekg(0);

    Jit runtime;
    std::ostringstream output;
    runtime.setOutput(output);
    runtime.registerFunction("std::twice", [](const Arguments& args) {
        return Value{Jit::asInteger(args.at(0)) * 2};
    });
    runtime.loadBinary(binary);
    runtime.run();
    assert(output.str() == "42\n");
}

void testIntegerOverflowIsReported() {
    using namespace hybridlang;
    Jit jit;
    jit.compile("let value = 9223372036854775807; value = value + 1;");
    bool overflow = false;
    try {
        jit.run();
    } catch (const std::runtime_error&) {
        overflow = true;
    }
    assert(overflow);
}

void testParserNestingLimit() {
    hybridlang::Jit jit;
    std::string source(200, '(');
    source += '1';
    source += std::string(200, ')');
    source += ';';
    bool rejected = false;
    try {
        jit.compile(source);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);
}

void testExecutionAndBinaryLimits() {
    using namespace hybridlang;
    Jit loop;
    loop.compile("while (true) { }");
    bool limited = false;
    try {
        loop.run(8);
    } catch (const std::runtime_error&) {
        limited = true;
    }
    assert(limited);

    Jit loader;
    std::stringstream invalid("NOPE", std::ios::in | std::ios::binary);
    bool rejected = false;
    try {
        loader.loadBinary(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);
}

} // namespace

int main() {
    testCompileAndExecute();
    testRegisteredFunctionAndInput();
    testBooleanOperatorsShortCircuit();
    testHostClassMethodsAndFields();
    testBinaryRoundTrip();
    testIntegerOverflowIsReported();
    testParserNestingLimit();
    testExecutionAndBinaryLimits();
    std::cout << "C++ JIT tests passed\n";
}
