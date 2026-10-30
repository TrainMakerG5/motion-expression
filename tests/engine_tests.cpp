#include "engine.h"
#include "bindings.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    expression::Input input;
    input.time = 0.25;
    input.frame = 15;
    input.values[0] = 20;
    input.controls = {100, 2, 7};
    auto result = expression::evaluate(
        "const offsets = [controls.a, controls.c];\n"
        "function wave(t) { return Math.sin(t * Math.PI * controls.b); }\n"
        "if (frame === 15) { obj.x += wave(time) * offsets[0]; }\n"
        "obj.rotation = time * 360; obj.opacity = 0.5;", input);
    require(result.ok, "variables, arrays, functions and branching execute");
    require(std::abs(result.values[0] - 120) < 1e-9, "time and controls reach JavaScript");
    require(result.values[5] == 90 && result.values[9] == 0.5, "multiple properties return");
    require(result.values[6] == 1, "unchanged properties survive");

    for (const char* source : {"const = ;", "throw new Error('example');", "obj.x = NaN;",
        "obj.x = Infinity;", "obj.x = '20';", "delete obj.x;", "obj.opacity = 2;",
        "Object.defineProperty(obj, 'x', {get() {throw Error('getter');}});"}) {
        result = expression::evaluate(source, input);
        require(!result.ok && !result.error.empty(), "invalid expressions report errors");
        require(result.values == input.values, "failed expressions preserve original values");
    }
    const auto start = std::chrono::steady_clock::now();
    result = expression::evaluate("while (true) {}", input);
    require(!result.ok, "infinite loop is interrupted");
    require(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "interruption is bounded");
    result = expression::evaluate("globalThis.previous = 123;", input);
    require(result.ok, "first evaluation runs");
    result = expression::evaluate("if (typeof previous !== 'undefined') throw Error('leaked state');", input);
    require(result.ok, "evaluations do not retain frame history");
    result = expression::evaluate("if (typeof process !== 'undefined' || typeof require !== 'undefined' || typeof fetch !== 'undefined') throw Error('unexpected host API');", input);
    require(result.ok, "browser and Node APIs are absent");
    result = expression::evaluate(std::string(65537, ' '), input);
    require(!result.ok, "oversized source is rejected");

    auto scalar = expression::evaluate_scalar("value + Math.sin(time * Math.PI * 2) * 100", 20, 0.25, 15);
    require(scalar.ok && std::abs(scalar.value - 120) < 1e-9, "scalar expression uses the original value");
    scalar = expression::evaluate_scalar("const amplitude = 5;\nlet offset = frame * 2;\nvalue + offset + amplitude;", 10, 0.5, 15);
    require(scalar.ok && scalar.value == 45, "multiline scalar code returns its last expression");
    for (const char* source : {"'123'", "NaN", "Infinity", "undefined", "const amplitude = 5;", "value = 9;", "while (true) {}"}) {
        scalar = expression::evaluate_scalar(source, 12, 0, 0);
        require(!scalar.ok && scalar.value == 12 && !scalar.error.empty(), "bad scalar code keeps the original value and reports an error");
    }
    expression::Bindings bindings;
    const int first = bindings.add({"value + 1", "42.00"});
    const int second = bindings.add({"const text = '日本語\\n';\nvalue * 2;", "0.0,10.0,直線移動,0"});
    require(first != second && bindings.get(first).source == "value + 1", "bindings are independent immutable records");
    expression::Bindings restored;
    require(restored.restore(bindings.serialize()), "project data restores");
    require(restored.get(second).source == bindings.get(second).source, "Unicode and multiline code roundtrip");
    require(!restored.restore("EXPR2;1:zz:01;") && restored.get(first).original == "42.00", "corrupted data does not partially replace bindings");
    require(bindings.serialize().find_first_of("\r\n") == std::string::npos, "project SDK strings contain no raw newlines");
    require(restored.add({"value", "0"}) > second, "restored projects allocate fresh binding IDs");
    const auto attached = expression::attach_track("42.00", first);
    require(expression::track_binding(attached) == first, "stationary tracks bind to an expression");
    require(expression::detach_track("60.00,JSエクスプレッション@Expression,0|1,1", "42.00") == "60.00", "removing an expression keeps edited base values");
    require(expression::detach_track("0,200,JSエクスプレッション@Expression,0|2,1", "0,100,直線移動,0") == "0,200,直線移動,0", "removing an expression keeps edited keyframes");
    require(expression::attach_track("0.0,10.0,直線移動,0", second, false) == "0.0,10.0,JSエクスプレッション@Expression,0|2,0", "linear points and disabled state survive binding");
    bool unsupported = false;
    try { expression::attach_track("0,10,加減速移動,3", first); } catch (...) { unsupported = true; }
    require(unsupported, "unsupported movement is left untouched");
    std::cout << "Expression engine checks passed.\n";
}
