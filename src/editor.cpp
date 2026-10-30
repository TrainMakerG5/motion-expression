#include <windows.h>
#include <cstdint>
#include <plugin2.h>
#include "bindings.h"

#include <filesystem>
#include <iomanip>
#include <locale>
#include <algorithm>
#include <atomic>
#include <map>
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

struct Target {
    OBJECT_HANDLE object = nullptr;
    int64_t object_id = 0;
    int scene_id = 0;
    OBJECT_LAYER_FRAME position{};
    std::wstring effect;
    std::wstring item;
    bool bound = false;
};

struct Panel {
    HWND window = nullptr;
    HWND code = nullptr;
    HWND enabled = nullptr;
    HWND status = nullptr;
    HWND target_label = nullptr;
    HWND targets = nullptr;
    HWND help = nullptr;
    HWND preview = nullptr;
    HWND buttons[4]{};
    HFONT code_font = nullptr;
    Editor current;
    std::vector<Target> items;
    bool loading = false;
    bool dirty = false;
    bool preview_scheduled = false;
    unsigned int generation = 0;
};

Panel panel;
std::atomic<HWND> event_window{nullptr};
std::atomic<bool> refresh_queued{false};
std::atomic<bool> preview_queued{false};
std::atomic<unsigned int> project_generation{0};
constexpr UINT refresh_message = WM_APP + 1;
constexpr UINT preview_message = WM_APP + 2;
constexpr UINT_PTR preview_timer = 2;

void schedule_preview() {
    if (panel.window && !panel.preview_scheduled) {
        panel.preview_scheduled = SetTimer(panel.window, preview_timer, 100, nullptr) != 0;
    }
}

void queue_preview(void*) {
    HWND window = event_window.load();
    if (window && !preview_queued.exchange(true)) {
        if (!PostMessageW(window, preview_message, 0, 0)) { preview_queued = false; }
    }
}

void queue_refresh(void*) {
    HWND window = event_window.load();
    if (window && !refresh_queued.exchange(true)) {
        if (!PostMessageW(window, refresh_message, 0, 0)) { refresh_queued = false; }
    }
}

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

HWND control(HWND window, const wchar_t* kind, const wchar_t* label, DWORD style, int x, int y, int width, int height, int id) {
    HWND result = CreateWindowExW(kind == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, kind, label,
        WS_CHILD | WS_VISIBLE | style, x, y, width, height, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), plugin_instance, nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return result;
}

void status(const std::wstring& text) { SetWindowTextW(panel.status, text.c_str()); }

void set_editable(bool enabled) {
    EnableWindow(panel.code, enabled);
    EnableWindow(panel.enabled, enabled);
    for (HWND button : panel.buttons) { EnableWindow(button, enabled); }
}

void layout() {
    if (!panel.code) { return; }
    RECT rectangle{};
    GetClientRect(panel.window, &rectangle);
    const double scale = GetDpiForWindow(panel.window) / 96.0;
    const auto pixels = [scale](int value) { return static_cast<int>(value * scale); };
    const int margin = pixels(10), gap = pixels(6), row = pixels(26);
    const int width = (std::max)(static_cast<int>(rectangle.right) - margin * 2, pixels(80));
    int y = margin;
    MoveWindow(panel.target_label, margin, y, width, row, TRUE);
    y += row + gap;
    MoveWindow(panel.targets, margin, y, width, pixels(280), TRUE);
    y += row + gap;
    MoveWindow(panel.enabled, margin, y, width, row, TRUE);
    y += row + gap;
    MoveWindow(panel.preview, margin, y, width, pixels(72), TRUE);
    y += pixels(72) + gap;
    const bool show_help = rectangle.bottom > pixels(460);
    ShowWindow(panel.help, show_help ? SW_SHOW : SW_HIDE);
    if (show_help) {
        MoveWindow(panel.help, margin, y, width, pixels(36), TRUE);
        y += pixels(36) + gap;
    }
    const int columns = width >= pixels(520) ? 4 : 2;
    const int button_rows = 4 / columns;
    const int button_y = rectangle.bottom - margin - button_rows * (row + gap) + gap;
    const int status_height = pixels(44);
    const int status_y = button_y - gap - status_height;
    MoveWindow(panel.code, margin, y, width, (std::max)(status_y - gap - y, pixels(32)), TRUE);
    MoveWindow(panel.status, margin, status_y, width, status_height, TRUE);
    const int button_width = (width - gap * (columns - 1)) / columns;
    for (int i = 0; i < 4; ++i) {
        MoveWindow(panel.buttons[i], margin + (i % columns) * (button_width + gap), button_y + (i / columns) * (row + gap), button_width, row, TRUE);
    }
}

struct TargetList {
    EDIT_INFO info{};
    std::vector<Target> items;
};

void collect_targets(void* opaque, EDIT_SECTION* edit) {
    auto& list = *static_cast<TargetList*>(opaque);
    auto object = edit->get_focus_object();
    if (!object) { return; }
    const char* alias_data = edit->get_object_alias(object);
    if (!alias_data) { return; }
    const std::string alias(alias_data);
    const auto position = edit->get_object_layer_frame(object);
    const auto object_id = edit->get_object_id(object);
    std::istringstream stream(alias);
    std::string line;
    std::wstring effect;
    std::map<std::wstring, int> occurrences;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        if (!line.empty() && line.front() == '[') { effect.clear(); continue; }
        const auto equals = line.find('=');
        if (equals == std::string::npos) { continue; }
        const auto key = line.substr(0, equals);
        const auto value = line.substr(equals + 1);
        if (key == "effect.name") {
            const auto name = wide(value);
            const int index = occurrences[name]++;
            effect = name + (index ? L":" + std::to_wstring(index) : L"");
        } else if (!effect.empty()) {
            const auto item = wide(key);
            TRACK_INFO track{};
            if (edit->get_object_track_info(object, effect.c_str(), item.c_str(), &track, sizeof(track))) {
                list.items.push_back({object, object_id, list.info.scene_id, position, effect, item, expression::track_binding(value) > 0});
            }
        }
    }
    std::stable_sort(list.items.begin(), list.items.end(), [](const Target& left, const Target& right) { return left.bound && !right.bound; });
}

void inspect_target(void* opaque, EDIT_SECTION* edit) {
    auto& editor = *static_cast<Editor*>(opaque);
    auto object = edit->find_object(editor.position.layer, editor.position.start);
    if (!object || editor.info.scene_id != editor.scene_id || edit->get_object_id(object) != editor.object_id) {
        editor.error = "対象のオブジェクトが変わっています。項目を選び直してください。";
        return;
    }
    editor.object = object;
    try { inspect(&editor, edit); }
    catch (const std::exception& exception) { editor.error = exception.what(); }
}

Editor snapshot(const Target& target) {
    Editor result;
    result.object_id = target.object_id;
    result.scene_id = target.scene_id;
    result.position = target.position;
    result.effect = target.effect;
    result.item = target.item;
    edit_handle->get_edit_info(&result.info, sizeof(result.info));
    if (!edit_handle->call_read_section_param(&result, inspect_target)) { result.error = "現在は項目を読み取れません。"; }
    return result;
}

Target current_target() {
    const auto& current = panel.current;
    return {current.object, current.object_id, current.scene_id, current.position, current.effect, current.item, expression::track_binding(current.raw) > 0};
}

bool same_target(const Target& left, const Target& right) {
    return left.object_id == right.object_id && left.scene_id == right.scene_id && left.effect == right.effect && left.item == right.item;
}

std::wstring number_text(double value) {
    std::wostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(10) << (value == 0 ? 0 : value);
    return stream.str();
}

void preview_text(const std::wstring& text) {
    const int length = GetWindowTextLengthW(panel.preview);
    std::wstring previous(length + 1, L'\0');
    GetWindowTextW(panel.preview, previous.data(), length + 1);
    previous.resize(length);
    if (previous != text) { SetWindowTextW(panel.preview, text.c_str()); }
}

struct NativeValue {
    const Editor* editor = nullptr;
    double value = 0;
    bool ok = false;
};

void read_native_value(void* opaque, EDIT_SECTION* edit) {
    auto& result = *static_cast<NativeValue*>(opaque);
    const auto& target = *result.editor;
    auto object = edit->find_object(target.position.layer, target.position.start);
    if (!object || edit->get_object_id(object) != target.object_id) { return; }
    result.ok = edit->get_object_track_value(object, target.effect.c_str(), target.item.c_str(), target.info.frame, &result.value);
}

void update_preview() {
    if (panel.generation != project_generation.load()) {
        preview_text(L"プロジェクトを読み込み中です。");
        queue_refresh(nullptr);
        return;
    }
    if (!panel.current.ready) {
        preview_text(L"現在値: —\n" + (panel.current.error.empty() ? L"数値項目を選択してください。" : wide(panel.current.error)));
        return;
    }
    const auto fresh = snapshot(current_target());
    if (!fresh.ready) { preview_text(L"現在値: —\n" + wide(fresh.error)); return; }
    std::wostringstream position;
    position.imbue(std::locale::classic());
    position << L"シーン " << fresh.info.frame + 1 << L" f / time " << std::fixed << std::setprecision(3) << fresh.time << L" 秒\n";
    if (fresh.info.frame < fresh.position.start || fresh.info.frame > fresh.position.end) {
        preview_text(position.str() + L"現在値: —（オブジェクトの範囲外）");
        return;
    }

    std::wstring error_text;
    bool saved_valid = true;
    if (expression::track_binding(fresh.raw) && fresh.is_enabled) {
        // 保存済みの式の失敗を先に確認して、プレビューによるホストのエラーダイアログを防ぎます。
        double result = 0;
        const char* error = nullptr;
        saved_valid = evaluate_source(fresh.source.c_str(), fresh.value, fresh.time, fresh.frame, &result, &error) != 0;
        if (!saved_valid) { error_text = L"保存済みの式: " + wide(error ? error : "JavaScript error"); }
    }
    NativeValue actual{&fresh};
    if (saved_valid) { edit_handle->call_read_section_param(&actual, read_native_value); }
    std::wstring text = position.str() + L"元の値: " + number_text(fresh.value) + L" / 現在値: "
        + (actual.ok ? number_text(actual.value) : L"エラー");
    if (!fresh.is_enabled) { text += L"（式は無効）"; }
    if (panel.dirty) {
        double draft = fresh.value;
        const bool enabled = SendMessageW(panel.enabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const auto source = code_text(panel.code);
        const char* error = nullptr;
        const bool valid = !enabled || evaluate_source(source.c_str(), fresh.value, fresh.time, fresh.frame, &draft, &error) != 0;
        text += L"\n未適用の式: " + (valid ? number_text(draft) : L"エラー");
        if (!enabled) { text += L"（式は無効）"; }
        if (!valid) { error_text = wide(error ? error : "JavaScript error"); }
    }
    if (!error_text.empty()) { text += L"\n" + error_text; }
    preview_text(text);
}

void display(Editor next) {
    const bool same_code = panel.current.source == next.source && panel.current.object_id == next.object_id
        && panel.current.effect == next.effect && panel.current.item == next.item;
    panel.current = std::move(next);
    panel.loading = true;
    panel.dirty = false;
    const auto label = L"Layer " + std::to_wstring(panel.current.position.layer + 1) + L" / " + panel.current.effect + L" / " + panel.current.item;
    SetWindowTextW(panel.target_label, label.c_str());
    if (!same_code) { SetWindowTextW(panel.code, wide(panel.current.source).c_str()); }
    SendMessageW(panel.enabled, BM_SETCHECK, panel.current.is_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    set_editable(panel.current.ready);
    panel.loading = false;
    if (!panel.current.ready) { status(wide(panel.current.error)); }
    schedule_preview();
}

void refresh() {
    if (panel.generation != project_generation.load()) {
        panel.generation = project_generation.load();
        panel.dirty = false;
        panel.current = Editor{};
    }
    if (panel.dirty) { return; }
    TargetList list;
    edit_handle->get_edit_info(&list.info, sizeof(list.info));
    if (!edit_handle->call_read_section_param(&list, collect_targets)) { return; }
    int selected = list.items.empty() ? -1 : 0;
    const auto previous = current_target();
    for (size_t i = 0; i < list.items.size(); ++i) {
        if (same_target(list.items[i], previous)) { selected = static_cast<int>(i); break; }
    }
    panel.items = std::move(list.items);
    panel.loading = true;
    SendMessageW(panel.targets, CB_RESETCONTENT, 0, 0);
    for (const auto& target : panel.items) {
        const std::wstring label = (target.bound ? L"[JS] " : L"") + target.effect + L" / " + target.item;
        SendMessageW(panel.targets, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(panel.targets, CB_SETCURSEL, selected, 0);
    panel.loading = false;
    if (selected >= 0) {
        display(snapshot(panel.items[selected]));
    } else {
        panel.current = Editor{};
        panel.loading = true;
        SetWindowTextW(panel.code, L"");
        panel.loading = false;
        SetWindowTextW(panel.target_label, L"タイムラインでオブジェクトを選択してください");
        set_editable(false);
        status(L"選択したオブジェクトの数値項目とJS式を、ここで編集できます。");
        schedule_preview();
    }
}

void command(int id) {
    if (id == IDCANCEL) {
        panel.dirty = false;
        panel.current.source.clear();
        refresh();
        status(L"未適用の変更を破棄しました。");
        return;
    }
    if (!panel.current.ready) { return; }
    auto fresh = snapshot(current_target());
    if (!fresh.ready || fresh.raw != panel.current.raw) {
        status(fresh.ready ? L"対象の設定が変わっています。「変更を破棄」で読み直してください。" : wide(fresh.error));
        return;
    }
    panel.current.info = fresh.info;
    panel.current.value = fresh.value;
    panel.current.time = fresh.time;
    panel.current.frame = fresh.frame;
    panel.current.source = code_text(panel.code);
    panel.current.is_enabled = SendMessageW(panel.enabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
    double result = 0;
    const char* error = nullptr;
    if (id != 102 && panel.current.is_enabled && !evaluate_source(panel.current.source.c_str(), panel.current.value,
        panel.current.time, panel.current.frame, &result, &error)) {
        status(wide(error ? error : "JavaScript error"));
        return;
    }
    if (id == 101) {
        status(panel.current.is_enabled ? L"現在時刻の計算結果: " + std::to_wstring(result) : L"式は無効です。元の値を使用します。");
        return;
    }
    panel.current.error.clear();
    panel.current.changed = id == IDOK;
    if (!edit_handle->call_edit_section_param(&panel.current, apply)) {
        panel.current.error = "現在は編集できません。再生や書き出しを停止してください。";
    }
    if (!panel.current.error.empty()) { status(wide(panel.current.error)); return; }
    panel.dirty = false;
    // ホストの書式に正規化された値を再取得して、次の編集の基準にします。
    display(snapshot(current_target()));
    status(id == 102 ? L"式を外して元の移動方法に戻しました。" : L"適用しました。パネルを開いたままタイムラインを操作できます。");
    queue_refresh(nullptr);
}

LRESULT CALLBACK editor_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == refresh_message) {
        refresh_queued = false;
        try { refresh(); } catch (const std::exception& exception) { status(wide(exception.what())); }
        schedule_preview();
        return 0;
    }
    if (message == preview_message) {
        preview_queued = false;
        schedule_preview();
        return 0;
    }
    if (message == WM_TIMER && wparam == preview_timer) {
        KillTimer(hwnd, preview_timer);
        panel.preview_scheduled = false;
        try { update_preview(); }
        catch (const std::exception& exception) { preview_text(L"現在値: —\n" + wide(exception.what())); }
        return 0;
    }
    if (message == WM_SIZE) { layout(); return 0; }
    if (message == WM_COMMAND && !panel.loading) {
        const int id = LOWORD(wparam), notification = HIWORD(wparam);
        if ((id == 104 && notification == EN_CHANGE) || (id == 103 && notification == BN_CLICKED)) {
            panel.dirty = true;
            status(L"未適用の変更があります。「適用」で反映します。");
            schedule_preview();
            return 0;
        }
        if (id == 106 && notification == CBN_SELCHANGE) {
            if (panel.dirty) {
                for (size_t i = 0; i < panel.items.size(); ++i) {
                    if (same_target(panel.items[i], current_target())) { SendMessageW(panel.targets, CB_SETCURSEL, i, 0); break; }
                }
                status(L"変更を適用するか破棄してから、別の項目へ切り替えてください。");
                return 0;
            }
            const int index = static_cast<int>(SendMessageW(panel.targets, CB_GETCURSEL, 0, 0));
            if (index >= 0 && index < static_cast<int>(panel.items.size())) { display(snapshot(panel.items[index])); status(L"最後の式の値を、この項目の数値に適用します。"); }
            return 0;
        }
        if (id == IDOK || id == IDCANCEL || id == 101 || id == 102) {
            try { command(id); } catch (const std::exception& exception) { status(wide(exception.what())); }
            return 0;
        }
    }
    if (message == WM_CLOSE) { ShowWindow(hwnd, SW_HIDE); return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void create_panel(HOST_APP_TABLE* host) {
    panel.window = CreateWindowExW(WS_EX_CONTROLPARENT, L"ExpressionEditor", L"JSエクスプレッション",
        WS_POPUP | WS_CLIPCHILDREN, 0, 0, 720, 480, nullptr, nullptr, plugin_instance, nullptr);
    if (!panel.window) { throw std::runtime_error("Could not create the expression panel."); }
    panel.target_label = control(panel.window, L"STATIC", L"タイムラインでオブジェクトを選択してください", 0, 0, 0, 0, 0, 107);
    panel.targets = control(panel.window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 0, 0, 0, 106);
    panel.enabled = control(panel.window, L"BUTTON", L"式を有効にする", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 0, 0, 103);
    panel.preview = control(panel.window, L"STATIC", L"現在値: —\n数値項目を選択してください。", 0, 0, 0, 0, 0, 109);
    panel.help = control(panel.window, L"STATIC", L"value: 元の値 / time: 秒 / frame: フレーム\n最後の式の数値が、この項目の値になります。", 0, 0, 0, 0, 0, 108);
    panel.code = control(panel.window, L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN
        | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, 0, 0, 0, 0, 104);
    SendMessageW(panel.code, EM_SETLIMITTEXT, 65536, 0);
    panel.code_font = CreateFontW(-MulDiv(11, GetDpiForWindow(panel.window), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    SendMessageW(panel.code, WM_SETFONT, reinterpret_cast<WPARAM>(panel.code_font), TRUE);
    panel.status = control(panel.window, L"STATIC", L"数値項目の右クリックからも、このパネルへ移動できます。", 0, 0, 0, 0, 0, 105);
    panel.buttons[0] = control(panel.window, L"BUTTON", L"計算を確認", WS_TABSTOP, 0, 0, 0, 0, 101);
    panel.buttons[1] = control(panel.window, L"BUTTON", L"元設定に戻す", WS_TABSTOP, 0, 0, 0, 0, 102);
    panel.buttons[2] = control(panel.window, L"BUTTON", L"適用", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, IDOK);
    panel.buttons[3] = control(panel.window, L"BUTTON", L"変更を破棄", WS_TABSTOP, 0, 0, 0, 0, IDCANCEL);
    set_editable(false);
    layout();
    host->register_window_client(L"JSエクスプレッション", panel.window);
    event_window = panel.window;
    for (auto event : {EVENT_TYPE::CHANGE_FOCUS_OBJECT, EVENT_TYPE::UPDATE_OBJECT, EVENT_TYPE::CHANGE_EDIT_SCENE}) {
        host->register_event_listener(event, nullptr, queue_refresh);
    }
    host->register_event_listener(EVENT_TYPE::CHANGE_EDIT_FRAME, nullptr, queue_preview);
    queue_refresh(nullptr);
}

void open_editor(void*, OBJECT_HANDLE object, LPCWSTR effect, LPCWSTR item) {
    if (!effect || !item) { return; }
    try {
        Editor next;
        next.object = object;
        next.effect = effect;
        next.item = item;
        edit_handle->get_edit_info(&next.info, sizeof(next.info));
        edit_handle->call_read_section_param(&next, [](void* opaque, EDIT_SECTION* edit) {
            try { inspect(opaque, edit); }
            catch (const std::exception& exception) { static_cast<Editor*>(opaque)->error = exception.what(); }
        });
        if (panel.dirty) {
            status(L"編集中の式を保持しています。変更を適用するか破棄してから、別の項目へ移動してください。");
        } else {
            display(std::move(next));
            queue_refresh(nullptr);
        }
        ShowWindow(GetParent(panel.window), SW_SHOW);
        ShowWindow(panel.window, SW_SHOW);
        SetFocus(panel.code);
    } catch (const std::exception& exception) { status(wide(exception.what())); }
}

void load_project(PROJECT_FILE* project) {
    const char* saved = project->get_param_string("bindings");
    if (!import_bindings(saved ? saved : "EXPR2;")) {
        import_bindings("EXPR2;");
        MessageBoxW(owner, L"保存されたJS式を読み込めません。", L"JSエクスプレッション", MB_OK | MB_ICONERROR);
    }
    ++project_generation;
    queue_refresh(nullptr);
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
        host->set_plugin_information(L"Expression Editor 0.4.0 — JavaScript式編集パネル");
        host->register_object_item_menu_param(L"JSエクスプレッションを編集", false, nullptr, open_editor);
        host->register_project_load_handler(load_project);
        host->register_project_save_handler(save_project);
        create_panel(host);
    } catch (const std::exception& exception) {
        MessageBoxW(nullptr, wide(exception.what()).c_str(), L"Expression Editor", MB_OK | MB_ICONERROR);
    }
}

extern "C" __declspec(dllexport) void UninitializePlugin() {
    event_window = nullptr;
    if (panel.window) { KillTimer(panel.window, preview_timer); }
    if (panel.window && IsWindow(panel.window)) { DestroyWindow(panel.window); }
    if (panel.code_font) { DeleteObject(panel.code_font); panel.code_font = nullptr; }
    if (native_module) { FreeLibrary(native_module); native_module = nullptr; }
}
