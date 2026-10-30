#include "engine.h"

#include <quickjs.h>
#include <chrono>
#include <cmath>
#include <memory>

namespace expression {
namespace {
using Clock = std::chrono::steady_clock;

struct Value {
    JSContext* context;
    JSValue value;
    ~Value() { JS_FreeValue(context, value); }
    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;
    Value(JSContext* context, JSValue value) : context(context), value(value) {}
};

int interrupt(JSRuntime*, void* opaque) {
    return Clock::now() >= *static_cast<Clock::time_point*>(opaque);
}

std::string exception_message(JSContext* context) {
    Value exception(context, JS_GetException(context));
    const char* message = JS_ToCString(context, exception.value);
    if (!message) {
        return "JavaScript failed (execution limit or memory limit).";
    }
    std::string result(message);
    JS_FreeCString(context, message);
    return result;
}
}

Result evaluate(std::string_view source, const Input& input) {
    Result result;
    result.values = input.values;
    if (source.size() > 64 * 1024) {
        result.error = "JavaScript source exceeds 64 KiB.";
        return result;
    }
    auto runtime = std::unique_ptr<JSRuntime, decltype(&JS_FreeRuntime)>(JS_NewRuntime(), JS_FreeRuntime);
    if (!runtime) {
        result.error = "Could not create JavaScript runtime.";
        return result;
    }
    JS_SetMemoryLimit(runtime.get(), 16 * 1024 * 1024);
    JS_SetMaxStackSize(runtime.get(), 256 * 1024);
    auto deadline = Clock::now() + std::chrono::milliseconds(50);
    JS_SetInterruptHandler(runtime.get(), interrupt, &deadline);
    auto context = std::unique_ptr<JSContext, decltype(&JS_FreeContext)>(JS_NewContext(runtime.get()), JS_FreeContext);
    if (!context) {
        result.error = "Could not create JavaScript context.";
        return result;
    }
    auto* ctx = context.get();
    Value object(ctx, JS_NewObject(ctx));
    Value controls(ctx, JS_NewObject(ctx));
    if (JS_IsException(object.value) || JS_IsException(controls.value)) {
        result.error = exception_message(ctx);
        return result;
    }
    for (size_t i = 0; i < property_names.size(); ++i) {
        if (JS_SetPropertyStr(ctx, object.value, property_names[i], JS_NewFloat64(ctx, input.values[i])) < 0) {
            result.error = exception_message(ctx);
            return result;
        }
    }
    constexpr const char* control_names[] = {"a", "b", "c"};
    for (size_t i = 0; i < input.controls.size(); ++i) {
        if (JS_DefinePropertyValueStr(ctx, controls.value, control_names[i],
            JS_NewFloat64(ctx, input.controls[i]), JS_PROP_ENUMERABLE) < 0) {
            result.error = exception_message(ctx);
            return result;
        }
    }
    std::string wrapped = "(function(obj, time, frame, controls) {\n\"use strict\";\n";
    wrapped.append(source);
    wrapped += "\n})";
    Value function(ctx, JS_Eval(ctx, wrapped.c_str(), wrapped.size(), "expression.js", JS_EVAL_TYPE_GLOBAL));
    if (JS_IsException(function.value)) {
        result.error = exception_message(ctx);
        return result;
    }
    JSValue arguments[] = {object.value, JS_NewFloat64(ctx, input.time), JS_NewFloat64(ctx, input.frame), controls.value};
    Value returned(ctx, JS_Call(ctx, function.value, JS_UNDEFINED, 4, arguments));
    JS_FreeValue(ctx, arguments[1]);
    JS_FreeValue(ctx, arguments[2]);
    if (JS_IsException(returned.value)) {
        result.error = exception_message(ctx);
        return result;
    }
    auto values = input.values;
    for (size_t i = 0; i < property_names.size(); ++i) {
        Value value(ctx, JS_GetPropertyStr(ctx, object.value, property_names[i]));
        if (JS_IsException(value.value)) {
            result.error = exception_message(ctx);
            return result;
        }
        if (!JS_IsNumber(value.value) || JS_ToFloat64(ctx, &values[i], value.value) < 0 || !std::isfinite(values[i])) {
            result.error = std::string("obj.") + property_names[i] + " must be a finite number.";
            return result;
        }
    }
    if (values[9] < 0 || values[9] > 1) {
        result.error = "obj.opacity must be between 0 and 1.";
        return result;
    }
    result.values = values;
    result.ok = true;
    return result;
}

ScalarResult evaluate_scalar(std::string_view source, double value, double time, double frame) {
    ScalarResult result{false, value, {}};
    if (source.size() > 64 * 1024) {
        result.error = "JavaScript source exceeds 64 KiB.";
        return result;
    }
    auto runtime = std::unique_ptr<JSRuntime, decltype(&JS_FreeRuntime)>(JS_NewRuntime(), JS_FreeRuntime);
    if (!runtime) {
        result.error = "Could not create JavaScript runtime.";
        return result;
    }
    JS_SetMemoryLimit(runtime.get(), 16 * 1024 * 1024);
    JS_SetMaxStackSize(runtime.get(), 256 * 1024);
    auto deadline = Clock::now() + std::chrono::milliseconds(50);
    JS_SetInterruptHandler(runtime.get(), interrupt, &deadline);
    auto context = std::unique_ptr<JSContext, decltype(&JS_FreeContext)>(JS_NewContext(runtime.get()), JS_FreeContext);
    if (!context) {
        result.error = "Could not create JavaScript context.";
        return result;
    }
    auto* ctx = context.get();
    Value global(ctx, JS_GetGlobalObject(ctx));
    const char* names[] = {"value", "time", "frame"};
    const double values[] = {value, time, frame};
    for (size_t i = 0; i < 3; ++i) {
        if (!std::isfinite(values[i])) {
            result.error = std::string(names[i]) + " must be a finite number.";
            return result;
        }
        if (JS_DefinePropertyValueStr(ctx, global.value, names[i], JS_NewFloat64(ctx, values[i]), JS_PROP_ENUMERABLE) < 0) {
            result.error = exception_message(ctx);
            return result;
        }
    }
    const std::string code(source);
    Value returned(ctx, JS_Eval(ctx, code.c_str(), code.size(), "expression.js", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT));
    if (JS_IsException(returned.value)) {
        result.error = exception_message(ctx);
        return result;
    }
    double calculated = value;
    if (!JS_IsNumber(returned.value) || JS_ToFloat64(ctx, &calculated, returned.value) < 0 || !std::isfinite(calculated)) {
        result.error = "The last expression must produce a finite number.";
        return result;
    }
    result.ok = true;
    result.value = calculated;
    return result;
}
}
