#include <windows.h>
#include <module2.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
struct lua_State;
using LuaFunction = int (*)(lua_State*);
constexpr int globals = -10002;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

template<typename T>
T load(HMODULE module, const char* name) {
    auto address = GetProcAddress(module, name);
    require(address != nullptr, name);
    return reinterpret_cast<T>(address);
}

struct Lua {
    lua_State* (*newstate)();
    void (*openlibs)(lua_State*);
    void (*close)(lua_State*);
    int (*loadstring)(lua_State*, const char*);
    int (*pcall)(lua_State*, int, int, int);
    int (*gettop)(lua_State*);
    void (*settop)(lua_State*, int);
    const char* (*tolstring)(lua_State*, int, size_t*);
    double (*tonumber)(lua_State*, int);
    int (*toboolean)(lua_State*, int);
    void (*getfield)(lua_State*, int, const char*);
    void (*setfield)(lua_State*, int, const char*);
    void (*pushnumber)(lua_State*, double);
    void (*pushboolean)(lua_State*, int);
    void (*pushstring)(lua_State*, const char*);
    void (*pushcclosure)(lua_State*, LuaFunction, int);
    void (*createtable)(lua_State*, int, int);
    void (*rawseti)(lua_State*, int, int);
};

Lua lua{};
lua_State* state = nullptr;
SCRIPT_MODULE_TABLE* module_table = nullptr;
int return_count = 0;
std::string host_error;

int param_count() { return lua.gettop(state); }
const char* param_string(int index) { return lua.tolstring(state, index + 1, nullptr); }
double param_double(int index) { return lua.tonumber(state, index + 1); }
double param_table_double(int index, const char* key) {
    lua.getfield(state, index + 1, key);
    double value = lua.tonumber(state, -1);
    lua.settop(state, -2);
    return value;
}
const char* param_table_string(int index, const char* key) {
    lua.getfield(state, index + 1, key);
    const char* value = lua.tolstring(state, -1, nullptr);
    lua.settop(state, -2);
    return value;
}
bool param_table_boolean(int index, const char* key) {
    lua.getfield(state, index + 1, key);
    const bool value = lua.toboolean(state, -1) != 0;
    lua.settop(state, -2);
    return value;
}
void result_int(int value) {
    lua.pushnumber(state, value);
    ++return_count;
}
void result_array(const char** values, int count) {
    lua.createtable(state, count, 0);
    for (int i = 0; i < count; ++i) {
        lua.pushstring(state, values[i]);
        lua.rawseti(state, -2, i + 1);
    }
    ++return_count;
}
void result_boolean(bool value) {
    lua.pushboolean(state, value);
    ++return_count;
}
void result_string(const char* value) {
    lua.pushstring(state, value);
    ++return_count;
}
void result_table(const char** keys, double* values, int count) {
    lua.createtable(state, 0, count);
    for (int i = 0; i < count; ++i) {
        lua.pushnumber(state, values[i]);
        lua.setfield(state, -2, keys[i]);
    }
    ++return_count;
}
void set_error(const char* error) { host_error = error; }

int evaluate(lua_State*) {
    return_count = 0;
    host_error.clear();
    SCRIPT_MODULE_PARAM param{};
    param.get_param_num = param_count;
    param.get_param_string = param_string;
    param.get_param_double = param_double;
    param.get_param_table_double = param_table_double;
    param.get_param_table_string = param_table_string;
    param.get_param_table_boolean = param_table_boolean;
    param.push_result_boolean = result_boolean;
    param.push_result_string = result_string;
    param.push_result_table_double = result_table;
    param.push_result_int = result_int;
    param.push_result_array_string = result_array;
    param.set_error = set_error;
    module_table->functions[0].func(&param);
    require(host_error.empty(), host_error.c_str());
    return return_count;
}

void run(const std::string& chunk) {
    int status = lua.loadstring(state, chunk.c_str());
    if (status == 0) { status = lua.pcall(state, 0, 0, 0); }
    if (status != 0) {
        std::cerr << lua.tolstring(state, -1, nullptr) << '\n';
        std::exit(1);
    }
}

std::string setup = R"(
obj = {ox=20, oy=30, oz=40, rx=1, ry=2, rz=3, sx=1, sy=1, sz=1, alpha=1, time=0.25, frame=15}
obj.module = function(name) assert(name == 'Expression'); return {evaluate=__evaluate} end
control_a, control_b, control_c = 100, 2, 7
)";
}

int wmain(int argc, wchar_t** argv) {
    require(argc == 4, "expected Lua DLL, module and effect paths");
    HMODULE lua_module = LoadLibraryW(argv[1]);
    require(lua_module != nullptr, "AviUtl2 Lua DLL loads");
    lua.newstate = load<decltype(lua.newstate)>(lua_module, "luaL_newstate");
    lua.openlibs = load<decltype(lua.openlibs)>(lua_module, "luaL_openlibs");
    lua.close = load<decltype(lua.close)>(lua_module, "lua_close");
    lua.loadstring = load<decltype(lua.loadstring)>(lua_module, "luaL_loadstring");
    lua.pcall = load<decltype(lua.pcall)>(lua_module, "lua_pcall");
    lua.gettop = load<decltype(lua.gettop)>(lua_module, "lua_gettop");
    lua.settop = load<decltype(lua.settop)>(lua_module, "lua_settop");
    lua.tolstring = load<decltype(lua.tolstring)>(lua_module, "lua_tolstring");
    lua.tonumber = load<decltype(lua.tonumber)>(lua_module, "lua_tonumber");
    lua.toboolean = load<decltype(lua.toboolean)>(lua_module, "lua_toboolean");
    lua.getfield = load<decltype(lua.getfield)>(lua_module, "lua_getfield");
    lua.setfield = load<decltype(lua.setfield)>(lua_module, "lua_setfield");
    lua.pushnumber = load<decltype(lua.pushnumber)>(lua_module, "lua_pushnumber");
    lua.pushboolean = load<decltype(lua.pushboolean)>(lua_module, "lua_pushboolean");
    lua.pushstring = load<decltype(lua.pushstring)>(lua_module, "lua_pushstring");
    lua.pushcclosure = load<decltype(lua.pushcclosure)>(lua_module, "lua_pushcclosure");
    lua.createtable = load<decltype(lua.createtable)>(lua_module, "lua_createtable");
    lua.rawseti = load<decltype(lua.rawseti)>(lua_module, "lua_rawseti");

    HMODULE expression_module = LoadLibraryW(argv[2]);
    require(expression_module != nullptr, "Expression.mod2 loads");
    module_table = load<SCRIPT_MODULE_TABLE* (*)()>(expression_module, "GetScriptModuleTable")();
    require(module_table && module_table->functions && std::wstring(module_table->functions[0].name) == L"evaluate", "SDK function table exports evaluate");
    state = lua.newstate();
    require(state != nullptr, "Lua state starts");
    lua.openlibs(state);
    lua.pushcclosure(state, evaluate, 0);
    lua.setfield(state, globals, "__evaluate");
    const auto folder = std::filesystem::path(argv[3]).parent_path();
    run("package.path = [==[" + folder.u8string() + "/?.lua;]==] .. package.path");

    std::ifstream file(std::filesystem::path(argv[3]), std::ios::binary);
    require(file.good(), "effect script is readable");
    std::string effect{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const std::string marker = "--text@source:JavaScript,";
    auto begin = effect.find(marker);
    require(begin != std::string::npos, "default expression exists");
    begin += marker.size();
    std::string source = effect.substr(begin, effect.find('\n', begin) - begin);
    for (size_t position = 0; (position = source.find("\\n", position)) != std::string::npos; ++position) {
        source.replace(position, 2, "\n");
    }
    run(setup + "source = [==[" + source + "]==]\n" + effect +
        "\nassert(math.abs(obj.ox - (20 + math.sin(0.5) * 100)) < 1e-9); assert(obj.alpha == 1)");
    run(setup + R"(
source = [[obj.x += controls.c; obj.y = 12; obj.z = 13;
obj.rotationX = 21; obj.rotationY = 22; obj.rotation = frame;
obj.scaleX = 2; obj.scaleY = 3; obj.scaleZ = 4; obj.opacity = 0.4;]]
)" + effect + R"(
assert(obj.ox == 27 and obj.oy == 12 and obj.oz == 13)
assert(obj.rx == 21 and obj.ry == 22 and obj.rz == 15)
assert(obj.sx == 2 and obj.sy == 3 and obj.sz == 4 and obj.alpha == 0.4)
)");
    run(setup + "source = [[obj.x = 999; obj.opacity = NaN;]]\n"
        "local success, message = pcall(function()\n" + effect + "\nend)\n"
        "assert(not success and string.find(message, 'JS制御', 1, true)); assert(obj.ox == 20)");

    run(setup + R"(
local calls = {}
obj.effect = function(...) calls[#calls + 1] = {...} end
source = [[
obj.visible = false;
obj.effect('単色化', {'色': 'ff8800', '輝度を保持する': true});
if (time > 0) obj.effect('ぼかし', {'範囲': frame * 2});
]]
)" + effect + R"(
assert(obj.alpha == 0 and #calls == 2)
assert(calls[1][1] == '単色化' and calls[1][2] == '色' and calls[1][3] == 'ff8800')
assert(calls[1][4] == '輝度を保持する' and calls[1][5] == 1)
assert(calls[2][1] == 'ぼかし' and calls[2][3] == 30)
)");
    run(setup + R"(
obj.effect = function() error('invalid code must not execute effects') end
source = [[obj.effect('ぼかし', {範囲: 20}); obj.visible = 0;]]
)" + "local success = pcall(function()\n" + effect + "\nend); assert(not success and obj.ox == 20)");

    std::ifstream text_file(folder / L"JSテキスト.obj2", std::ios::binary);
    require(text_file.good(), "JS text object script is readable");
    const std::string text_effect{std::istreambuf_iterator<char>(text_file), std::istreambuf_iterator<char>()};
    const std::string text_setup = setup + R"(
base_text, text_color, text_visible = '元の文字列', 0x123456, true
font_name, font_size = 'MS UI Gothic', 64
local rendered, colored, calls
calls = {}
obj.setfont = function(name, size, decoration, color)
    assert(name == font_name and size == font_size and decoration == 0)
    colored = color
end
obj.load = function(kind, text) assert(kind == 'text'); rendered = text end
obj.effect = function(...) calls[#calls + 1] = {...} end
)";
    run(text_setup + R"(
source = [[obj.text += '\n残り' + String(controls.a - frame); obj.color = 0xff8800;
obj.visible = frame < 30; obj.x += 4; obj.effect('ぼかし', {範囲: 2});]]
)" + text_effect + R"(
assert(rendered == '元の文字列\n残り85' and colored == 0xff8800)
assert(obj.ox == 24 and obj.alpha == 1 and #calls == 1)
)");
    run(text_setup + "source = [[obj.visible = false;]]\n" + text_effect +
        "\nassert(rendered == base_text and colored == text_color and obj.alpha == 0)");
    run(text_setup + "text_visible = false; source = [[]]\n" + text_effect +
        "\nassert(rendered == base_text and obj.alpha == 0)");
    run(text_setup + "source = [[obj.text = 123;]]\nlocal success = pcall(function()\n" + text_effect +
        "\nend); assert(not success and rendered == nil and colored == nil and #calls == 0)");

    if (std::wstring(argv[1]).find(L"luaJIT") != std::wstring::npos) {
        auto add = load<int (*)(const char*, const char*)>(expression_module, "ExpressionAddBinding");
        auto export_data = load<const char* (*)()>(expression_module, "ExpressionExportBindings");
        auto import_data = load<int (*)(const char*)>(expression_module, "ExpressionImportBindings");
        const int id = add("const speed = 60; value + time * speed + frame;", "10");
        require(id > 0, "native binding is created");
        std::string saved = export_data();
        require(import_data("EXPR2;") == 1 && import_data(saved.c_str()) == 1, "native binding survives project serialization");
        std::ifstream track_file(folder / L"@Expression.tra2", std::ios::binary);
        require(track_file.good(), "parameter script is readable");
        std::string track{std::istreambuf_iterator<char>(track_file), std::istreambuf_iterator<char>()};
        track.erase(0, track.find('\n') + 1);
        const std::string prepare = "package.path = [==[" + folder.u8string() + "/?.lua;]==] .. package.path\n"
            "binding_id=" + std::to_string(id) + R"(
local points = {10}
local enabled = 1
obj = {getpoint = function(key)
    if key == 'num' then return #points end
    if key == 'index' then return 0.5 end
    if key == 'param' then return binding_id, enabled end
    if key == 'time' then return 0.5 end
    if key == 'framerate' then return 30 end
    return points[key + 1]
end}
local function track_value()
)";
        run(prepare + track + R"(
end
assert(track_value() == 55)
points = {0, 100}
assert(track_value() == 95)
enabled = 0
assert(track_value() == 50)
)");
    }
    lua.close(state);
    FreeLibrary(expression_module);
    FreeLibrary(lua_module);
    std::cout << "AviUtl2 Lua -> Expression.mod2 -> JavaScript -> Lua checks passed.\n";
}
