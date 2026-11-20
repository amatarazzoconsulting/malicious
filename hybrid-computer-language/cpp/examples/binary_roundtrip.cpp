#include "hybridlang/jit.hpp"

#include <iostream>
int main() {
    using namespace hybridlang;

    Jit compiler;
    compiler.compile(R"(
        let answer = 6 * 7;
        println("answer =", answer);
    )");
    compiler.writeBinary("hybrid-demo.hbc");

    Jit runtime;
    runtime.loadBinary("hybrid-demo.hbc");
    runtime.run();
}
