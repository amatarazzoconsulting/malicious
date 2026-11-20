#pragma once

#include <any>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace hybridlang {

struct Object;
using ObjectPtr = std::shared_ptr<Object>;
using Value = std::variant<std::monostate, std::int64_t, double, bool, std::string, ObjectPtr>;
using Arguments = std::vector<Value>;

struct Object {
    std::string className;
    std::unordered_map<std::string, Value> fields;
    std::any native;
};

class Jit {
public:
    using NativeFunction = std::function<Value(const Arguments&)>;
    using ClassFactory = std::function<ObjectPtr(const Arguments&)>;
    using NativeMethod = std::function<Value(const ObjectPtr&, const Arguments&)>;
    using FieldGetter = std::function<Value(const ObjectPtr&)>;
    using FieldSetter = std::function<void(const ObjectPtr&, const Value&)>;

    Jit();
    Jit(const Jit&) = delete;
    Jit& operator=(const Jit&) = delete;
    Jit(Jit&&) = delete;
    Jit& operator=(Jit&&) = delete;

    void compile(const std::string& source);
    Value run(std::size_t maxSteps = 100'000);

    void writeBinary(std::ostream& output) const;
    void writeBinary(const std::string& path) const;
    void loadBinary(std::istream& input);
    void loadBinary(const std::string& path);

    void registerFunction(std::string sourceName, NativeFunction function);
    void registerKeyword(std::string sourceName, NativeFunction function);
    void addClass(std::string name, ClassFactory factory);
    void addMethod(std::string className, std::string methodName, NativeMethod method);
    void addField(
        std::string className,
        std::string fieldName,
        FieldGetter getter,
        FieldSetter setter
    );

    template <class T, class Factory>
    void addNativeClass(std::string name, Factory factory) {
        using FactoryResult = std::invoke_result_t<Factory, const Arguments&>;
        static_assert(
            std::is_convertible_v<FactoryResult, std::shared_ptr<T>>,
            "native class factory must return std::shared_ptr<T>"
        );
        const auto className = name;
        addClass(
            std::move(name),
            [className, factory = std::move(factory)](const Arguments& arguments) {
                std::shared_ptr<T> instance = std::invoke(factory, arguments);
                if (!instance) {
                    throw std::runtime_error("class factory returned a null instance");
                }
                auto object = std::make_shared<Object>();
                object->className = className;
                object->native = std::move(instance);
                return object;
            }
        );
    }

    template <class T, class Method>
    void addNativeMethod(std::string className, std::string methodName, Method method) {
        addMethod(
            std::move(className),
            std::move(methodName),
            [method = std::move(method)](const ObjectPtr& object, const Arguments& args) {
                const auto instance =
                    std::any_cast<std::shared_ptr<T>>(&object->native);
                if (instance == nullptr || !*instance) {
                    throw std::runtime_error("host object type does not match registered class");
                }
                return std::invoke(method, **instance, args);
            }
        );
    }

    template <class T, class Getter, class Setter>
    void addNativeField(
        std::string className,
        std::string fieldName,
        Getter getter,
        Setter setter
    ) {
        addField(
            std::move(className),
            std::move(fieldName),
            [getter = std::move(getter)](const ObjectPtr& object) {
                const auto instance =
                    std::any_cast<std::shared_ptr<T>>(&object->native);
                if (instance == nullptr || !*instance) {
                    throw std::runtime_error("host object type does not match registered class");
                }
                return std::invoke(getter, **instance);
            },
            [setter = std::move(setter)](const ObjectPtr& object, const Value& value) {
                const auto instance =
                    std::any_cast<std::shared_ptr<T>>(&object->native);
                if (instance == nullptr || !*instance) {
                    throw std::runtime_error("host object type does not match registered class");
                }
                std::invoke(setter, **instance, value);
            }
        );
    }

    void setInput(std::istream& input) noexcept;
    void setOutput(std::ostream& output) noexcept;
    void setGlobal(std::string name, Value value);
    Value getGlobal(const std::string& name) const;

    static std::int64_t asInteger(const Value& value);
    static double asNumber(const Value& value);
    static std::string asString(const Value& value);
    static std::string toString(const Value& value);

private:
    enum class Op : std::uint8_t {
        Constant = 1,
        Load,
        Store,
        Pop,
        Add,
        Subtract,
        Multiply,
        Divide,
        Modulo,
        Negate,
        LogicalNot,
        BitwiseNot,
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        LogicalAnd,
        LogicalOr,
        BitwiseAnd,
        BitwiseOr,
        BitwiseXor,
        ShiftLeft,
        ShiftRight,
        JumpIfFalse,
        Jump,
        CallFunction,
        CallMethod,
        LoadField,
        StoreField,
        Halt
    };

    struct Instruction {
        Op op;
        std::int64_t operand = 0;
        std::string text;
        Value constant;
    };

    struct FieldBinding {
        FieldGetter getter;
        FieldSetter setter;
    };

    friend class Parser;

    void registerClassFactory(std::string name, ClassFactory factory);
    void registerMethodBinding(std::string className, std::string methodName, NativeMethod method);
    void registerFieldBinding(
        std::string className,
        std::string fieldName,
        FieldGetter getter,
        FieldSetter setter
    );
    void verifyCode(const std::vector<Instruction>& code) const;

    std::vector<Instruction> code_;
    std::unordered_map<std::string, Value> globals_;
    std::unordered_map<std::string, NativeFunction> functions_;
    std::unordered_map<std::string, ClassFactory> classes_;
    std::unordered_map<std::string, NativeMethod> methods_;
    std::unordered_map<std::string, FieldBinding> fields_;
    std::istream* input_;
    std::ostream* output_;
    std::size_t outputCharacters_ = 0;
};

} // namespace hybridlang
