#include "hybridlang/jit.hpp"

#include <iostream>
#include <memory>
#include <sstream>

namespace {

struct Counter {
    explicit Counter(std::int64_t initial) : value(initial) {}
    std::int64_t value;
};

} // namespace

int main() {
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

    jit.compile(R"(
        let counter = Counter(10);
        counter.increment(2);
        counter.value = counter.value + 3;
        println(counter.value);
    )");
    jit.run();
    std::cout << output.str();
}
