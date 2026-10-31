#include <windows.h>
#include <module2.h>
#include "engine.h"
#include "bindings.h"

#include <exception>

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
    catch (...) { exported_text = "Unexpected expression error."; }
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
    catch (...) { exported_text = "Unexpected expression error."; }
    if (error) { *error = exported_text.c_str(); }
    return 0;
}

namespace {
void evaluate(SCRIPT_MODULE_PARAM* param) {
    try {
        if (param->get_param_num() != 5) {
            param->set_error("Expression.evaluate expects source, time, frame, object, controls.");
            return;
        }
        const char* source = param->get_param_string(0);
        if (!source) {
            param->set_error("JavaScript source must be a string.");
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
        auto result = expression::evaluate(source, input);
        param->push_result_boolean(result.ok);
        if (result.ok) {
            auto keys = expression::property_names;
            param->push_result_table_double(keys.data(), result.values.data(), static_cast<int>(keys.size()));
        } else {
            param->push_result_string(result.error.c_str());
        }
    } catch (const std::exception& exception) {
        param->set_error(exception.what());
    } catch (...) {
        param->set_error("Unexpected error in Expression.");
    }
}

SCRIPT_MODULE_FUNCTION functions[] = {
    {L"evaluate", evaluate},
    {nullptr, nullptr}
};

SCRIPT_MODULE_TABLE table = {
    L"Expression 0.1.0 - JavaScript expressions (QuickJS-ng)", functions
};
}

extern "C" __declspec(dllexport) SCRIPT_MODULE_TABLE* GetScriptModuleTable() {
    return &table;
}
