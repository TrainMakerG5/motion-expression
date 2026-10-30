#include <windows.h>
#include <cstdint>
#include <plugin2.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
EDIT_HANDLE* host = nullptr;
std::filesystem::path output;
std::ofstream report;
std::string failure;
int (*add_binding)(const char*, const char*) = nullptr;
OBJECT_HANDLE test_object = nullptr;
void require(bool condition, const char* message);

void find_test_object(EDIT_SECTION* edit) { test_object = edit->find_object(0, 0); }

void check_editor_value(EDIT_SECTION* edit) {
    double value = 0;
    if (!edit->get_object_track_value(edit->find_object(0, 0), L"標準描画", L"X", 15, &value) || std::abs(value - 17) > 0.001) {
        failure = "editor-applied expression did not reach the parameter";
    }
}

void exercise_editor(bool invalid, const wchar_t* code, const wchar_t* item = L"X") {
    auto editor_module = GetModuleHandleW(L"ExpressionEditor.aux2");
    auto open = reinterpret_cast<void (*)(OBJECT_HANDLE, LPCWSTR, LPCWSTR)>(GetProcAddress(editor_module, "ExpressionOpenEditor"));
    require(open != nullptr, "editor entry point exists");
    require(host->call_edit_section(find_test_object) && test_object, "editor target resolves");
    bool success = false;
    std::thread interaction([&]() {
        HWND window = nullptr;
        for (int attempt = 0; attempt < 100 && !window; ++attempt) {
            window = FindWindowW(L"ExpressionEditor", nullptr);
            Sleep(50);
        }
        if (!window) { return; }
        SetWindowTextW(GetDlgItem(window, 104), code);
        if (invalid) {
            SendMessageW(window, WM_COMMAND, IDOK, 0);
            wchar_t status[1024]{};
            GetWindowTextW(GetDlgItem(window, 105), status, 1024);
            success = IsWindow(window) && std::wstring(status).find(L"finite number") != std::wstring::npos;
            PostMessageW(window, WM_COMMAND, IDCANCEL, 0);
        } else {
            SendMessageW(window, WM_COMMAND, 101, 0);
            wchar_t status[1024]{};
            GetWindowTextW(GetDlgItem(window, 105), status, 1024);
            success = std::wstring(status).find(L"計算結果") != std::wstring::npos;
            PostMessageW(window, WM_COMMAND, IDOK, 0);
        }
    });
    open(test_object, L"標準描画", item);
    interaction.join();
    require(success, invalid ? "editor displays numeric errors without closing" : "editor previews and applies JS code");
}

void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
    report << "OK " << message << std::endl;
}

std::string track(int id, bool enabled = true) {
    return "10,JSエクスプレッション@Expression,0|" + std::to_string(id) + (enabled ? ",1" : ",0");
}

void verify(EDIT_SECTION* edit) {
    auto object = edit->find_object(0, 0);
    require(object != nullptr, "test object exists");
    TRACK_INFO info{};
    require(edit->get_object_track_info(object, L"標準描画", L"X", &info, sizeof(info)), "parameter is a native track");
    for (int frame : {0, 15, 30}) {
        double value = 0;
        require(edit->get_object_track_value(object, L"標準描画", L"X", frame, &value), "native host evaluates the JS track");
        report << "frame " << frame << ": " << value << std::endl;
        require(std::abs(value - (10 + frame * 2)) < 0.001, "value and time match native host frames");
    }
    double blur = 0;
    require(edit->get_object_track_value(object, L"ぼかし", L"範囲", 15, &blur), "effect parameter evaluates");
    require(std::abs(blur - 25) < 0.001, "JS controls an effect parameter as well as coordinates");
}

void create(EDIT_SECTION* edit) {
    try {
        auto object = edit->create_object_from_alias(
            "[Object]\n[Object.0]\neffect.name=テキスト\nサイズ=50\nテキスト=JS Expression\n"
            "[Object.1]\neffect.name=ぼかし\n範囲=10\n[Object.2]\neffect.name=標準描画\nX=10\n", 0, 0, 90);
        require(object != nullptr, "native host creates the demo object");
        const int id = add_binding("value + time * 60", "10");
        require(id > 0, "expression registers");
        const auto serialized = track(id);
        require(edit->set_object_item_value(object, L"標準描画", L"X", serialized.c_str()), "expression attaches to X");
        const int blur_id = add_binding("value + time * 30", "10");
        require(edit->set_object_item_value(object, L"ぼかし", L"範囲", track(blur_id).c_str()), "expression attaches to blur");
        verify(edit);
        require(edit->set_object_item_value(object, L"標準描画", L"X", track(id, false).c_str()), "expression disables");
        double value = 0;
        require(edit->get_object_track_value(object, L"標準描画", L"X", 15, &value) && std::abs(value - 10) < 0.001, "disabled expression returns the base value");
        require(edit->set_object_item_value(object, L"標準描画", L"X", serialized.c_str()), "expression re-enables");
    } catch (const std::exception& exception) { failure = exception.what(); }
}

void check_loaded(EDIT_SECTION* edit) {
    try { verify(edit); }
    catch (const std::exception& exception) { failure = exception.what(); }
}

LRESULT CALLBACK probe_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_TIMER) {
        KillTimer(hwnd, 1);
        report.open(output, std::ios::binary);
        report << "AviUtl2 parameter-expression host check" << std::endl;
        try {
            auto native = GetModuleHandleW(L"Expression.mod2");
            require(native != nullptr, "JS module loaded");
            add_binding = reinterpret_cast<decltype(add_binding)>(GetProcAddress(native, "ExpressionAddBinding"));
            require(add_binding != nullptr, "binding API exported");
            require(GetModuleHandleW(L"ExpressionEditor.aux2") != nullptr, "parameter editor plugin loaded");
            require(host->create_project(640, 360, 30, 1, 48000, {0, 0, 0, 255}, false), "test project starts");
            require(host->call_edit_section(create), "edit section runs");
            require(failure.empty(), failure.empty() ? "initial checks pass" : failure.c_str());
            exercise_editor(false, L"const offset = 7;\r\nvalue + offset;");
            require(host->call_edit_section(check_editor_value) && failure.empty(), "editor changes only the selected parameter");
            exercise_editor(false, L"value + 5", L"Y");
            exercise_editor(true, L"value / 0");
            exercise_editor(false, L"value + time * 60");
            auto project = output.parent_path() / L"Expression-demo.aup2";
            require(host->save_project_file(project.c_str()), "demo project saves");
            auto import_data = reinterpret_cast<int (*)(const char*)>(GetProcAddress(native, "ExpressionImportBindings"));
            require(import_data && import_data("EXPR2;"), "runtime binding cache clears before reopen");
            require(host->open_project_file(project.c_str(), false), "demo project reopens");
            require(host->call_edit_section(check_loaded), "restored project evaluates");
            require(failure.empty(), failure.empty() ? "restored checks pass" : failure.c_str());
            report << "PASS" << std::endl;
        } catch (const std::exception& exception) {
            report << "FAIL " << exception.what() << std::endl;
        }
        report.close();
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
}

extern "C" __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* app) {
    wchar_t path[32768]{};
    if (!GetEnvironmentVariableW(L"EXPRESSION_PROBE_OUTPUT", path, 32768)) { return; }
    output = path;
    app->set_plugin_information(L"Expression test-only native host probe");
    host = app->create_edit_handle();
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = probe_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"ExpressionHostProbe";
    RegisterClassW(&window_class);
    auto window = CreateWindowW(window_class.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, window_class.hInstance, nullptr);
    SetTimer(window, 1, 2500, nullptr);
}
