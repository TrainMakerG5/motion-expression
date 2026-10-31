#include <windows.h>
#include <module2.h>
#include "engine.h"
#include "bindings.h"

#include <exception>
#include <cmath>

namespace {
expression::Bindings bindings;
thread_local std::string exported_text;
}

extern "C" __declspec(dllexport) int ExpressionAddBinding(const char* source, const char* original) {
    try { return bindings.add({source ? source : "", original ? original : ""}); }
    catch (...) { return 0; }
}

extern "C" __declspec(dllexport) const char* ExpressionBindingSource(int id) {
    try { exported_text = bindings.get(id).source; }
    catch (...) { return nullptr; }
    return exported_text.c_str();
}

extern "C" __declspec(dllexport) const char* ExpressionBindingOriginal(int id) {
    try { exported_text = bindings.get(id).original; }
    catch (...) { return nullptr; }
    return exported_text.c_str();
}

extern "C" __declspec(dllexport) const char* ExpressionExportBindings() {
    try { exported_text = bindings.serialize(); return exported_text.c_str(); }
    catch (...) { return nullptr; }
}

extern "C" __declspec(dllexport) int ExpressionImportBindings(const char* data) {
    return data && bindings.restore(data) ? 1 : 0;
}

extern "C" __declspec(dllexport) int ExpressionEvaluateBinding(int id, double value, double time, double frame,
    double* output, const char** error) {
    try {
        const auto binding = bindings.get(id);
        const auto result = expression::evaluate_scalar(binding.source, value, time, frame);
        if (result.ok && output) { *output = result.value; return 1; }
        exported_text = result.error;
    } catch (const std::exception& exception) { exported_text = exception.what(); }
    catch (...) { exported_text = "予期しない式のエラーが発生しました。"; }
    if (error) { *error = exported_text.c_str(); }
    return 0;
}

extern "C" __declspec(dllexport) int ExpressionEvaluateSource(const char* source, double value, double time, double frame,
    double* output, const char** error) {
    try {
        const auto result = expression::evaluate_scalar(source ? source : "", value, time, frame);
        if (result.ok && output) { *output = result.value; return 1; }
        exported_text = result.error;
    } catch (const std::exception& exception) { exported_text = exception.what(); }
    catch (...) { exported_text = "予期しない式のエラーが発生しました。"; }
    if (error) { *error = exported_text.c_str(); }
    return 0;
}

namespace {
void evaluate(SCRIPT_MODULE_PARAM* param) {
    try {
        const int count = param->get_param_num();
        if (count != 5 && count != 6) {
            param->set_error("Expression.evaluate には source, time, frame, object, controls と、省略可能な表示設定を指定してください。");
            return;
        }
        const char* source = param->get_param_string(0);
        if (!source) {
            param->set_error("JavaScriptのコードは文字列で指定してください。");
            return;
        }
        expression::Input input;
        input.time = param->get_param_double(1);
        input.frame = param->get_param_double(2);
        for (size_t i = 0; i < input.values.size(); ++i) {
            input.values[i] = param->get_param_table_double(3, expression::property_names[i]);
        }
        constexpr const char* names[] = {"a", "b", "c"};
        for (size_t i = 0; i < input.controls.size(); ++i) {
            input.controls[i] = param->get_param_table_double(4, names[i]);
        }
        if (count == 6) {
            const char* text = param->get_param_table_string(5, "text");
            input.text = text ? text : "";
            const double color = param->get_param_table_double(5, "color");
            if (!std::isfinite(color) || color < 0 || color > 0xffffff || std::floor(color) != color) {
                param->set_error("文字色には0x000000から0xffffffの整数を指定してください。");
                return;
            }
            input.color = static_cast<int>(color);
            input.visible = param->get_param_table_boolean(5, "visible");
        }
        auto result = expression::evaluate(source, input);
        param->push_result_boolean(result.ok);
        if (result.ok) {
            auto keys = expression::property_names;
            param->push_result_table_double(keys.data(), result.values.data(), static_cast<int>(keys.size()));
            param->push_result_string(result.text.c_str());
            param->push_result_int(result.color);
            param->push_result_boolean(result.visible);
            std::vector<const char*> effects;
            for (const auto& value : result.effects) { effects.push_back(value.c_str()); }
            param->push_result_array_string(effects.data(), static_cast<int>(effects.size()));
        } else {
            param->push_result_string(result.error.c_str());
        }
    } catch (const std::exception& exception) {
        param->set_error(exception.what());
    } catch (...) {
        param->set_error("Expressionで予期しないエラーが発生しました。");
    }
}

SCRIPT_MODULE_FUNCTION functions[] = {
    {L"evaluate", evaluate},
    {nullptr, nullptr}
};

SCRIPT_MODULE_TABLE table = {
    L"Expression 0.2.0 — JavaScriptエクスプレッション (QuickJS-ng)", functions
};
}

extern "C" __declspec(dllexport) SCRIPT_MODULE_TABLE* GetScriptModuleTable() {
    return &table;
}
