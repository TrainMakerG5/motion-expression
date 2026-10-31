#include <windows.h>
#include <cstdint>
#include <plugin2.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>

namespace {
EDIT_HANDLE* host = nullptr;
std::filesystem::path output;
std::ofstream report;
std::string failure;
int (*add_binding)(const char*, const char*) = nullptr;
OBJECT_HANDLE test_object = nullptr;
void require(bool condition, const char* message);

HWND find_panel() {
    HWND result = nullptr;
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process != GetCurrentProcessId()) { return TRUE; }
        EnumChildWindows(window, [](HWND child, LPARAM destination) -> BOOL {
            wchar_t name[128]{};
            GetClassNameW(child, name, 128);
            if (std::wstring(name) == L"ExpressionEditor") { *reinterpret_cast<HWND*>(destination) = child; }
            return TRUE;
        }, parameter);
        return *reinterpret_cast<HWND*>(parameter) ? FALSE : TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

void find_test_object(EDIT_SECTION* edit) { test_object = edit->find_object(0, 0); }

void type_code(HWND window, const wchar_t* code) {
    auto input = GetDlgItem(window, 104);
    SendMessageW(input, EM_SETSEL, 0, -1);
    SendMessageW(input, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(code));
}

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
    open(test_object, L"標準描画", item);
    HWND window = find_panel();
    require(window && GetParent(window) && (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD), "editor is a registered host child panel");
    require(IsWindowEnabled(host->get_host_app_window()), "timeline remains enabled while editing");
    type_code(window, code);
    if (invalid) {
        SendMessageW(window, WM_COMMAND, IDOK, 0);
        wchar_t status[1024]{};
        GetWindowTextW(GetDlgItem(window, 105), status, 1024);
        require(IsWindow(window) && std::wstring(status).find(L"有限の数値") != std::wstring::npos, "panel displays numeric errors without closing");
        SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
    } else {
        SendMessageW(window, WM_COMMAND, 101, 0);
        wchar_t status[1024]{};
        GetWindowTextW(GetDlgItem(window, 105), status, 1024);
        require(std::wstring(status).find(L"計算結果") != std::wstring::npos, "panel previews JS code");
        SendMessageW(window, WM_COMMAND, IDOK, 0);
        GetWindowTextW(GetDlgItem(window, 105), status, 1024);
        require(IsWindow(window) && std::wstring(status).find(L"適用しました") != std::wstring::npos, "applying leaves the panel open");
    }
}

void check_panel_targets() {
    HWND window = find_panel();
    require(window != nullptr, "persistent panel remains available");
    SendMessageW(window, WM_APP + 1, 0, 0);
    HWND targets = GetDlgItem(window, 106);
    const int count = static_cast<int>(SendMessageW(targets, CB_GETCOUNT, 0, 0));
    require(count > 3, "selected object exposes numeric parameters in the panel");
    int blur_index = -1;
    for (int i = 0; i < count; ++i) {
        wchar_t item[1024]{};
        SendMessageW(targets, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(item));
        if (std::wstring(item).find(L"ぼかし / 範囲") != std::wstring::npos) { blur_index = i; break; }
    }
    require(blur_index >= 0, "bound effect parameters appear in the selector");
    SendMessageW(targets, CB_SETCURSEL, blur_index, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(106, CBN_SELCHANGE), 0);
    wchar_t code[1024]{};
    GetWindowTextW(GetDlgItem(window, 104), code, 1024);
    require(std::wstring(code) == L"value + time * 30", "selecting a parameter displays its saved expression");
    type_code(window, L"value + 999");
    SendMessageW(targets, CB_SETCURSEL, 0, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(106, CBN_SELCHANGE), 0);
    SendMessageW(window, WM_APP + 1, 0, 0);
    GetWindowTextW(GetDlgItem(window, 104), code, 1024);
    require(std::wstring(code) == L"value + 999" && SendMessageW(targets, CB_GETCURSEL, 0, 0) == blur_index, "unapplied code survives selection attempts and host updates");
    SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
    GetWindowTextW(GetDlgItem(window, 104), code, 1024);
    require(std::wstring(code) == L"value + time * 30", "discard reloads the saved expression");
    SetWindowPos(window, nullptr, 0, 0, 460, 400, SWP_NOMOVE | SWP_NOZORDER);
    RECT small{};
    GetWindowRect(GetDlgItem(window, 104), &small);
    SetWindowPos(window, nullptr, 0, 0, 800, 520, SWP_NOMOVE | SWP_NOZORDER);
    RECT large{};
    GetWindowRect(GetDlgItem(window, 104), &large);
    require(large.right - large.left > small.right - small.left && large.bottom - large.top > small.bottom - small.top, "code area resizes with the panel");
}

std::wstring window_text(HWND window) {
    std::wstring text(GetWindowTextLengthW(window) + 1, L'\0');
    GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
    text.resize(text.size() - 1);
    return text;
}

bool wait_preview(HWND window, const std::function<bool(const std::wstring&)>& matches) {
    const auto deadline = GetTickCount64() + 3000;
    while (GetTickCount64() < deadline) {
        MSG message{};
        for (int count = 0; count < 100 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (matches(window_text(GetDlgItem(window, 109)))) { return true; }
        Sleep(10);
    }
    const auto text = window_text(GetDlgItem(window, 109));
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string encoded(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), encoded.data(), length, nullptr, nullptr);
    report << "Last preview: " << encoded << std::endl;
    return false;
}

void move_cursor(int frame) {
    require(host->call_edit_section_param(&frame, [](void* opaque, EDIT_SECTION* edit) {
        edit->set_cursor_layer_frame(0, *static_cast<int*>(opaque));
    }), "timeline cursor moves");
}

void check_live_preview() {
    auto module = GetModuleHandleW(L"ExpressionEditor.aux2");
    auto open = reinterpret_cast<void (*)(OBJECT_HANDLE, LPCWSTR, LPCWSTR)>(GetProcAddress(module, "ExpressionOpenEditor"));
    require(host->call_edit_section(find_test_object) && test_object, "live preview target resolves");
    open(test_object, L"標準描画", L"X");
    HWND window = find_panel();
    move_cursor(15);
    require(wait_preview(window, [](const auto& text) {
        return text.find(L"シーン 16 f") != std::wstring::npos && text.find(L"現在値: 40") != std::wstring::npos;
    }), "live numeric preview follows actual timeline events");
    type_code(window, L"value + time * 120");
    require(wait_preview(window, [](const auto& text) {
        return text.find(L"元の値: 10 / 現在値: 40") != std::wstring::npos && text.find(L"未適用の式: 70") != std::wstring::npos;
    }), "draft preview is separate from the saved parameter value");
    move_cursor(30);
    require(wait_preview(window, [](const auto& text) {
        return text.find(L"シーン 31 f") != std::wstring::npos && text.find(L"現在値: 70") != std::wstring::npos
            && text.find(L"未適用の式: 130") != std::wstring::npos;
    }), "timeline scrubbing updates saved and draft values together");
    require(window_text(GetDlgItem(window, 104)) == L"value + time * 120", "scrubbing keeps the draft code intact");
    type_code(window, L"value / 0");
    require(wait_preview(window, [](const auto& text) {
        return text.find(L"現在値: 70") != std::wstring::npos && text.find(L"未適用の式: エラー") != std::wstring::npos;
    }), "draft errors keep the actual value visible");
    SendMessageW(GetDlgItem(window, 103), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(103, BN_CLICKED), 0);
    require(wait_preview(window, [](const auto& text) {
        return text.find(L"現在値: 70") != std::wstring::npos && text.find(L"未適用の式: 10（式は無効）") != std::wstring::npos;
    }), "disabling a draft previews the original value without evaluating invalid code");
    bool unchanged = false;
    require(host->call_read_section_param(&unchanged, [](void* opaque, EDIT_SECTION* edit) {
        double value = 0;
        *static_cast<bool*>(opaque) = edit->get_object_track_value(edit->find_object(0, 0), L"標準描画", L"X", 30, &value)
            && std::abs(value - 70) < 0.001;
    }) && unchanged, "automatic preview never applies draft changes");
    SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
    require(wait_preview(window, [](const auto& text) {
        return text.find(L"現在値: 70") != std::wstring::npos && text.find(L"未適用") == std::wstring::npos;
    }), "discard removes the draft numeric preview");
    move_cursor(0);
    require(wait_preview(window, [](const auto& text) {
        return text.find(L"シーン 1 f") != std::wstring::npos && text.find(L"現在値: 10") != std::wstring::npos;
    }), "returning to the first frame refreshes the displayed number");
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
        edit->set_focus_object(object);
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
            check_panel_targets();
            check_live_preview();
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
