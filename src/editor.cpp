#include <windows.h>
#include <cstdint>
#include <plugin2.h>
#include "bindings.h"

#include <filesystem>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
EDIT_HANDLE* edit_handle = nullptr;
HMODULE native_module = nullptr;
HINSTANCE plugin_instance = nullptr;
HWND owner = nullptr;
using EvaluateSource = int (*)(const char*, double, double, double, double*, const char**);
EvaluateSource evaluate_source = nullptr;
int (*add_binding)(const char*, const char*) = nullptr;
const char* (*binding_source)(int) = nullptr;
const char* (*binding_original)(int) = nullptr;
const char* (*export_bindings)() = nullptr;
int (*import_bindings)(const char*) = nullptr;

std::wstring wide(const std::string& text) {
    if (text.empty()) { return {}; }
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) { return L"文字列を読み込めません。"; }
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}

std::string utf8(const std::wstring& text) {
    if (text.empty()) { return {}; }
    int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}

template<typename T>
void symbol(T& target, const char* name) {
    target = reinterpret_cast<T>(GetProcAddress(native_module, name));
    if (!target) { throw std::runtime_error("Expression.mod2 must be updated together with the editor."); }
}

struct Editor {
    HWND window = nullptr;
    HWND code = nullptr;
    HWND enabled = nullptr;
    HWND status = nullptr;
    OBJECT_HANDLE object = nullptr;
    int64_t object_id = 0;
    int scene_id = 0;
    OBJECT_LAYER_FRAME position{};
    EDIT_INFO info{};
    std::wstring effect;
    std::wstring item;
    std::string raw;
    std::string original;
    std::string source = "value + Math.sin(time * 2) * 100";
    std::string error;
    bool is_enabled = true;
    bool ready = false;
    bool changed = false;
    double value = 0;
    double time = 0;
    double frame = 0;
};

void inspect(void* opaque, EDIT_SECTION* edit) {
    auto& editor = *static_cast<Editor*>(opaque);
    TRACK_INFO track{};
    if (!edit->get_object_track_info(editor.object, editor.effect.c_str(), editor.item.c_str(), &track, sizeof(track))) {
        editor.error = "数値のトラックバーを選んでください。";
        return;
    }
    const char* raw = edit->get_object_item_value(editor.object, editor.effect.c_str(), editor.item.c_str());
    if (!raw) { editor.error = "パラメーターを取得できません。"; return; }
    editor.raw = raw;
    editor.original = editor.raw;
    const int id = expression::track_binding(editor.raw);
    if (id) {
        const char* source = binding_source(id);
        if (!source) { editor.error = "プロジェクト内のJS式が見つかりません。"; return; }
        editor.source = source;
        const char* original = binding_original(id);
        if (!original) { editor.error = "元の設定が見つかりません。"; return; }
        editor.original = expression::detach_track(editor.raw, original);
        editor.is_enabled = editor.raw.back() == '1';
    } else if (track.mode && std::wstring(track.mode) != L"直線移動") {
        editor.error = "初版では固定値と直線移動に対応しています。他の移動方法からの変更には未対応です。";
        return;
    }
    try { expression::attach_track(editor.original, 1); }
    catch (...) { editor.error = "加減速・時間制御・参照式を含む項目にはまだ対応していません。"; return; }
    editor.position = edit->get_object_layer_frame(editor.object);
    editor.object_id = edit->get_object_id(editor.object);
    editor.scene_id = editor.info.scene_id;
    editor.frame = static_cast<double>(editor.info.frame - editor.position.start);
    editor.time = editor.frame * editor.info.scale / editor.info.rate;
    // 既存の式を二重に適用しないように、確認用の元の値を計算します。
    std::string points = editor.original;
    const auto mode = points.find(",直線移動,0");
    if (mode != std::string::npos) { points.resize(mode); }
    std::istringstream stream(points);
    std::string point;
    std::vector<double> values;
    while (std::getline(stream, point, ',')) { values.push_back(std::stod(point)); }
    editor.value = values.front();
    for (size_t section = 0; section + 1 < values.size(); ++section) {
        int start = edit->get_object_section_frame(editor.object, static_cast<int>(section));
        int end = section + 2 == values.size() ? editor.position.end : edit->get_object_section_frame(editor.object, static_cast<int>(section + 1));
        const double ratio = end > start ? std::clamp(static_cast<double>(editor.info.frame - start) / (end - start), 0.0, 1.0) : 0.0;
        editor.value = values[section] + (values[section + 1] - values[section]) * ratio;
        if (editor.info.frame <= end) { break; }
    }
    editor.ready = true;
}

std::string code_text(HWND control) {
    std::wstring text(GetWindowTextLengthW(control) + 1, L'\0');
    GetWindowTextW(control, text.data(), static_cast<int>(text.size()));
    text.resize(text.size() - 1);
    return utf8(text);
}

void apply(void* opaque, EDIT_SECTION* edit) {
    auto& editor = *static_cast<Editor*>(opaque);
    auto object = edit->find_object(editor.position.layer, editor.position.start);
    if (!object || edit->info->scene_id != editor.scene_id || edit->get_object_id(object) != editor.object_id) {
        editor.error = "対象のオブジェクトが変わっています。編集画面を開き直してください。";
        return;
    }
    const char* raw = edit->get_object_item_value(object, editor.effect.c_str(), editor.item.c_str());
    if (!raw || editor.raw != raw) { editor.error = "対象の設定が変わっています。編集画面を開き直してください。"; return; }
    std::string replacement = editor.original;
    if (editor.changed) {
        const int id = add_binding(editor.source.c_str(), editor.original.c_str());
        if (!id) { editor.error = "JS式を登録できません。"; return; }
        replacement = expression::attach_track(editor.original, id, editor.is_enabled);
    }
    if (!edit->set_object_item_value(object, editor.effect.c_str(), editor.item.c_str(), replacement.c_str())) {
        editor.error = "パラメーターへ適用できません。";
        return;
    }
    editor.raw = replacement;
    if (auto* project = edit->get_project_file(edit_handle)) {
        if (const char* saved = export_bindings()) { project->set_param_string("bindings", saved); }
    }
}

LRESULT CALLBACK editor_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* editor = reinterpret_cast<Editor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        editor = static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
    }
    if (editor && message == WM_COMMAND) {
        const int command = LOWORD(wparam);
        if (command == IDCANCEL) { DestroyWindow(hwnd); return 0; }
        if (command == IDOK || command == 101 || command == 102) {
            editor->source = code_text(editor->code);
            editor->is_enabled = SendMessageW(editor->enabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
            double result = 0;
            const char* error = nullptr;
            if (command != 102 && editor->is_enabled && !evaluate_source(editor->source.c_str(), editor->value, editor->time, editor->frame, &result, &error)) {
                SetWindowTextW(editor->status, wide(error ? error : "JavaScript error").c_str());
                return 0;
            }
            if (command == 101) {
                std::wstring status = editor->is_enabled ? L"現在時刻の計算結果: " + std::to_wstring(result) : L"式は無効です。元の値を使用します。";
                SetWindowTextW(editor->status, status.c_str());
                return 0;
            }
            editor->error.clear();
            editor->changed = command == IDOK;
            try {
                if (!edit_handle->call_edit_section_param(editor, apply)) { editor->error = "現在は編集できません。再生や書き出しを停止してください。"; }
            } catch (const std::exception& exception) { editor->error = exception.what(); }
            if (!editor->error.empty()) { SetWindowTextW(editor->status, wide(editor->error).c_str()); }
            else { DestroyWindow(hwnd); }
            return 0;
        }
    }
    if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

HWND control(HWND window, const wchar_t* kind, const wchar_t* label, DWORD style, int x, int y, int width, int height, int id) {
    HWND result = CreateWindowExW(kind == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, kind, label,
        WS_CHILD | WS_VISIBLE | style, x, y, width, height, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), plugin_instance, nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return result;
}

void open_editor(void*, OBJECT_HANDLE object, LPCWSTR effect, LPCWSTR item) {
    if (!effect || !item) { return; }
    Editor editor;
    editor.object = object;
    editor.effect = effect;
    editor.item = item;
    edit_handle->get_edit_info(&editor.info, sizeof(editor.info));
    try { edit_handle->call_read_section_param(&editor, inspect); }
    catch (const std::exception& exception) { editor.error = exception.what(); }
    if (!editor.ready) { MessageBoxW(owner, wide(editor.error).c_str(), L"JSエクスプレッション", MB_OK | MB_ICONINFORMATION); return; }
    std::wstring title = L"JSエクスプレッション — " + editor.effect + L" / " + editor.item;
    editor.window = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ExpressionEditor", title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 720, 510, owner, nullptr, plugin_instance, &editor);
    if (!editor.window) { return; }
    control(editor.window, L"STATIC", L"最後の式の値を、このパラメーターに適用します。value: 元の値 / time: 秒 / frame: フレーム", 0, 16, 14, 675, 24, 0);
    editor.enabled = control(editor.window, L"BUTTON", L"式を有効にする", BS_AUTOCHECKBOX | WS_TABSTOP, 16, 43, 200, 26, 103);
    SendMessageW(editor.enabled, BM_SETCHECK, editor.is_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    editor.code = control(editor.window, L"EDIT", wide(editor.source).c_str(),
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, 16, 80, 675, 260, 104);
    SendMessageW(editor.code, EM_SETLIMITTEXT, 65536, 0);
    editor.status = control(editor.window, L"STATIC", L"例: value + Math.sin(time * 2) * 100", 0, 16, 350, 675, 55, 105);
    control(editor.window, L"BUTTON", L"計算を確認", WS_TABSTOP, 16, 420, 130, 30, 101);
    control(editor.window, L"BUTTON", L"元設定に戻す", WS_TABSTOP, 156, 420, 140, 30, 102);
    control(editor.window, L"BUTTON", L"適用して閉じる", BS_DEFPUSHBUTTON | WS_TABSTOP, 406, 420, 150, 30, IDOK);
    control(editor.window, L"BUTTON", L"キャンセル", WS_TABSTOP, 566, 420, 125, 30, IDCANCEL);
    EnableWindow(owner, FALSE);
    ShowWindow(editor.window, SW_SHOW);
    SetFocus(editor.code);
    MSG message{};
    while (IsWindow(editor.window)) {
        const int status = GetMessageW(&message, nullptr, 0, 0);
        if (status <= 0) { if (status == 0) { PostQuitMessage(static_cast<int>(message.wParam)); } break; }
        if (!IsDialogMessageW(editor.window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (IsWindow(editor.window)) { DestroyWindow(editor.window); }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
}

void load_project(PROJECT_FILE* project) {
    const char* saved = project->get_param_string("bindings");
    if (!import_bindings(saved ? saved : "EXPR2;")) {
        import_bindings("EXPR2;");
        MessageBoxW(owner, L"保存されたJS式を読み込めません。", L"JSエクスプレッション", MB_OK | MB_ICONERROR);
    }
}

void save_project(PROJECT_FILE* project) {
    if (const char* saved = export_bindings()) { project->set_param_string("bindings", saved); }
}
}

extern "C" __declspec(dllexport) void ExpressionOpenEditor(OBJECT_HANDLE object, LPCWSTR effect, LPCWSTR item) {
    open_editor(nullptr, object, effect, item);
}

extern "C" __declspec(dllexport) DWORD RequiredVersion() { return 2011500; }

extern "C" __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    try {
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&RegisterPlugin), &plugin_instance);
        wchar_t filename[32768]{};
        GetModuleFileNameW(plugin_instance, filename, 32768);
        auto data = std::filesystem::path(filename).parent_path().parent_path().parent_path();
        native_module = LoadLibraryW((data / L"Script" / L"Expression" / L"Expression.mod2").c_str());
        if (!native_module) { throw std::runtime_error("Expression.mod2 could not be loaded."); }
        symbol(evaluate_source, "ExpressionEvaluateSource");
        symbol(add_binding, "ExpressionAddBinding");
        symbol(binding_source, "ExpressionBindingSource");
        symbol(binding_original, "ExpressionBindingOriginal");
        symbol(export_bindings, "ExpressionExportBindings");
        symbol(import_bindings, "ExpressionImportBindings");
        edit_handle = host->create_edit_handle();
        owner = edit_handle->get_host_app_window();
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = editor_proc;
        window_class.hInstance = plugin_instance;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        window_class.lpszClassName = L"ExpressionEditor";
        RegisterClassW(&window_class);
        host->set_plugin_information(L"Expression Editor 0.2.0 — パラメーターごとのJavaScript式");
        host->register_object_item_menu_param(L"JSエクスプレッションを編集", false, nullptr, open_editor);
        host->register_project_load_handler(load_project);
        host->register_project_save_handler(save_project);
    } catch (const std::exception& exception) {
        MessageBoxW(nullptr, wide(exception.what()).c_str(), L"Expression Editor", MB_OK | MB_ICONERROR);
    }
}

extern "C" __declspec(dllexport) void UninitializePlugin() {
    if (native_module) { FreeLibrary(native_module); native_module = nullptr; }
}
