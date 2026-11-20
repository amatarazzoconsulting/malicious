#include "hybridlang/jit.hpp"

#include <cmath>
#include <iostream>
#include <sstream>

int main() {
    hybridlang::Jit jit;
    std::ostringstream output;
    jit.setOutput(output);

    jit.setGlobal("input_value", std::int64_t{12});
    jit.registerKeyword("std::sqrt", [](const hybridlang::Arguments& args) {
        const auto value = hybridlang::Jit::asNumber(args.at(0));
        return hybridlang::Value{std::sqrt(value)};
    });

    jit.compile(R"(
        let result = std::sqrt(input_value);
        println("sqrt =", result);
    )");
    jit.run();
    std::cout << output.str();
    std::cout << "read back through Jit::getGlobal: "
              << hybridlang::Jit::asNumber(jit.getGlobal("result")) << '\n';
}
