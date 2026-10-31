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

struct EffectQueue {
    std::vector<std::string> values;
    size_t count = 0;
    size_t bytes = 0;
};

bool read_string(JSContext* ctx, JSValueConst value, std::string& output) {
    size_t length = 0;
    const char* text = JS_ToCStringLen(ctx, &length, value);
    if (!text) { return false; }
    output.assign(text, length);
    JS_FreeCString(ctx, text);
    return true;
}

bool host_string(const std::string& value) {
    return value.size() <= 65536 && value.find('\0') == std::string::npos;
}

JSValue queue_effect(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    try {
        if (argc != 2 || !JS_IsString(argv[0]) || !JS_IsObject(argv[1]) || JS_IsArray(argv[1])) {
            return JS_ThrowTypeError(ctx, "obj.effect にはエフェクト名と設定項目のオブジェクトを指定してください。");
        }
        auto& queue = *static_cast<EffectQueue*>(JS_GetContextOpaque(ctx));
        if (queue.count >= 64) { return JS_ThrowRangeError(ctx, "エフェクトは1回の実行につき64件まで指定できます。"); }
        std::string name;
        if (!read_string(ctx, argv[0], name)) { return JS_EXCEPTION; }
        if (name.empty() || !host_string(name)) {
            return JS_ThrowTypeError(ctx, "エフェクト名は空でない64 KiB以内の文字列にしてください。NUL文字は使用できません。");
        }
        JSPropertyEnum* properties = nullptr;
        uint32_t count = 0;
        if (JS_GetOwnPropertyNames(ctx, &properties, &count, argv[1], JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
            return JS_EXCEPTION;
        }
        struct Properties {
            JSContext* ctx;
            JSPropertyEnum* data;
            uint32_t count;
            ~Properties() {
                for (uint32_t i = 0; i < count; ++i) { JS_FreeAtom(ctx, data[i].atom); }
                js_free(ctx, data);
            }
        } owned{ctx, properties, count};
        if (count > 64) { return JS_ThrowRangeError(ctx, "1件のエフェクトに指定できる設定項目は64件までです。"); }
        std::vector<std::string> command{name, std::to_string(count)};
        size_t bytes = name.size();
        for (uint32_t i = 0; i < count; ++i) {
            Value key(ctx, JS_AtomToString(ctx, properties[i].atom));
            Value value(ctx, JS_GetProperty(ctx, argv[1], properties[i].atom));
            if (JS_IsException(key.value) || JS_IsException(value.value)) { return JS_EXCEPTION; }
            std::string item, text;
            if (!read_string(ctx, key.value, item)) { return JS_EXCEPTION; }
            if (item.empty() || !host_string(item)) {
                return JS_ThrowTypeError(ctx, "設定項目名は空でない64 KiB以内の文字列にしてください。NUL文字は使用できません。");
            }
            std::string type;
            if (JS_IsBool(value.value)) {
                type = "number";
                text = JS_ToBool(ctx, value.value) ? "1" : "0";
            } else if (JS_IsNumber(value.value)) {
                double number = 0;
                if (JS_ToFloat64(ctx, &number, value.value) < 0) { return JS_EXCEPTION; }
                if (!std::isfinite(number)) { return JS_ThrowTypeError(ctx, "エフェクトの数値には有限の値を指定してください。"); }
                type = "number";
                if (!read_string(ctx, value.value, text)) { return JS_EXCEPTION; }
            } else if (JS_IsString(value.value)) {
                type = "string";
                if (!read_string(ctx, value.value, text)) { return JS_EXCEPTION; }
            } else {
                return JS_ThrowTypeError(ctx, "エフェクトの設定値には数値・文字列・真偽値を指定してください。");
            }
            if (!host_string(text)) { return JS_ThrowRangeError(ctx, "設定値は64 KiB以内にしてください。NUL文字は使用できません。"); }
            bytes += item.size() + text.size() + type.size();
            command.insert(command.end(), {std::move(item), std::move(type), std::move(text)});
        }
        if (queue.count >= 64) { return JS_ThrowRangeError(ctx, "エフェクトは1回の実行につき64件まで指定できます。"); }
        if (queue.bytes + bytes > 256 * 1024) { return JS_ThrowRangeError(ctx, "エフェクトの設定値の合計が256 KiBを超えています。"); }
        queue.values.insert(queue.values.end(), command.begin(), command.end());
        queue.bytes += bytes;
        ++queue.count;
        return JS_UNDEFINED;
    } catch (...) {
        return JS_ThrowInternalError(ctx, "エフェクトの設定を作成できませんでした。");
    }
}

int interrupt(JSRuntime*, void* opaque) {
    return Clock::now() >= *static_cast<Clock::time_point*>(opaque);
}

std::string exception_message(JSContext* context) {
    Value exception(context, JS_GetException(context));
    const char* message = JS_ToCString(context, exception.value);
    if (!message) {
        return "JavaScriptの実行に失敗しました（実行時間またはメモリの上限を超えました）。";
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
        result.error = "JavaScriptのコードが 64 KiB を超えています。";
        return result;
    }
    auto runtime = std::unique_ptr<JSRuntime, decltype(&JS_FreeRuntime)>(JS_NewRuntime(), JS_FreeRuntime);
    if (!runtime) {
        result.error = "JavaScriptランタイムを作成できませんでした。";
        return result;
    }
    JS_SetMemoryLimit(runtime.get(), 16 * 1024 * 1024);
    JS_SetMaxStackSize(runtime.get(), 256 * 1024);
    auto deadline = Clock::now() + std::chrono::milliseconds(50);
    JS_SetInterruptHandler(runtime.get(), interrupt, &deadline);
    auto context = std::unique_ptr<JSContext, decltype(&JS_FreeContext)>(JS_NewContext(runtime.get()), JS_FreeContext);
    if (!context) {
        result.error = "JavaScriptコンテキストを作成できませんでした。";
        return result;
    }
    auto* ctx = context.get();
    EffectQueue effects;
    JS_SetContextOpaque(ctx, &effects);
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
    if (!host_string(input.text) || input.color < 0 || input.color > 0xffffff) {
        result.error = "元の文字列または色が不正です。";
        return result;
    }
    if (JS_SetPropertyStr(ctx, object.value, "text", JS_NewStringLen(ctx, input.text.data(), input.text.size())) < 0
        || JS_SetPropertyStr(ctx, object.value, "color", JS_NewInt32(ctx, input.color)) < 0
        || JS_SetPropertyStr(ctx, object.value, "visible", JS_NewBool(ctx, input.visible)) < 0
        || JS_DefinePropertyValueStr(ctx, object.value, "effect", JS_NewCFunction(ctx, queue_effect, "effect", 2), JS_PROP_ENUMERABLE) < 0) {
        result.error = exception_message(ctx);
        return result;
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
            result.error = std::string("obj.") + property_names[i] + " には有限の数値を指定してください。";
            return result;
        }
    }
    if (values[9] < 0 || values[9] > 1) {
        result.error = "obj.opacity には 0 から 1 の範囲の数値を指定してください。";
        return result;
    }
    Value text(ctx, JS_GetPropertyStr(ctx, object.value, "text"));
    Value color(ctx, JS_GetPropertyStr(ctx, object.value, "color"));
    Value visible(ctx, JS_GetPropertyStr(ctx, object.value, "visible"));
    if (JS_IsException(text.value) || JS_IsException(color.value) || JS_IsException(visible.value)) {
        result.error = exception_message(ctx);
        return result;
    }
    std::string content;
    double rgb = 0;
    if (!JS_IsString(text.value) || !read_string(ctx, text.value, content) || !host_string(content)) {
        result.error = "obj.text には64 KiB以内の文字列を指定してください。NUL文字は使用できません。";
        return result;
    }
    if (!JS_IsNumber(color.value) || JS_ToFloat64(ctx, &rgb, color.value) < 0
        || !std::isfinite(rgb) || rgb < 0 || rgb > 0xffffff || std::floor(rgb) != rgb) {
        result.error = "obj.color には0x000000から0xffffffの整数を指定してください。";
        return result;
    }
    if (!JS_IsBool(visible.value)) {
        result.error = "obj.visible にはtrueまたはfalseを指定してください。";
        return result;
    }
    result.text = std::move(content);
    result.color = static_cast<int>(rgb);
    result.visible = JS_ToBool(ctx, visible.value) != 0;
    result.effects = std::move(effects.values);
    result.values = values;
    result.ok = true;
    return result;
}

ScalarResult evaluate_scalar(std::string_view source, double value, double time, double frame) {
    ScalarResult result{false, value, {}};
    if (source.size() > 64 * 1024) {
        result.error = "JavaScriptのコードが 64 KiB を超えています。";
        return result;
    }
    auto runtime = std::unique_ptr<JSRuntime, decltype(&JS_FreeRuntime)>(JS_NewRuntime(), JS_FreeRuntime);
    if (!runtime) {
        result.error = "JavaScriptランタイムを作成できませんでした。";
        return result;
    }
    JS_SetMemoryLimit(runtime.get(), 16 * 1024 * 1024);
    JS_SetMaxStackSize(runtime.get(), 256 * 1024);
    auto deadline = Clock::now() + std::chrono::milliseconds(50);
    JS_SetInterruptHandler(runtime.get(), interrupt, &deadline);
    auto context = std::unique_ptr<JSContext, decltype(&JS_FreeContext)>(JS_NewContext(runtime.get()), JS_FreeContext);
    if (!context) {
        result.error = "JavaScriptコンテキストを作成できませんでした。";
        return result;
    }
    auto* ctx = context.get();
    Value global(ctx, JS_GetGlobalObject(ctx));
    const char* names[] = {"value", "time", "frame"};
    const double values[] = {value, time, frame};
    for (size_t i = 0; i < 3; ++i) {
        if (!std::isfinite(values[i])) {
            result.error = std::string(names[i]) + " には有限の数値を指定してください。";
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
        result.error = "最後の式は有限の数値になる必要があります。";
        return result;
    }
    result.ok = true;
    result.value = calculated;
    return result;
}
}
