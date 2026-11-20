#include "hybridlang/jit.hpp"

#include <iostream>

int main() {
    hybridlang::Jit compiler;
    compiler.compile(
        "let total = 0; "
        "while (total < 10) { total = total + 1; }"
    );
    compiler.writeObject("hybrid-program.o");
    std::cout << "Wrote hybrid-program.o\n";
}
