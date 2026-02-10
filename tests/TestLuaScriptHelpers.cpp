#include <string>

#include <sol/sol.hpp>

#include "game/scripting/LuaScriptHelpers.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (!condition) {
        outFail = message;
        return false;
    }
    return true;
}

} // namespace

bool test_lua_script_helpers(std::string& outFail) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);

    lua.script(R"(
        value_only = 42
        function fn_a() return "A" end
        function fn_b() return "B" end
        function fn_num() return 7 end
        function fn_err() error("boom") end
    )");

    sol::table table = lua.globals();
    if (!expect(game::scripting::hasFunction(table, "fn_a"), "hasFunction should find fn_a.", outFail)) return false;
    if (!expect(!game::scripting::hasFunction(table, "missing"), "hasFunction should fail for missing.", outFail)) return false;
    if (!expect(!game::scripting::hasFunction(table, "value_only"), "hasFunction should fail for non-function.", outFail)) return false;

    if (!expect(game::scripting::hasAnyFunction(table, {"missing", "fn_b"}), "hasAnyFunction should find fn_b.", outFail)) return false;
    if (!expect(!game::scripting::hasAnyFunction(table, {"missing", "value_only"}), "hasAnyFunction should fail for none.", outFail)) return false;

    {
        sol::function fn = game::scripting::resolveFunction(table, {"missing", "fn_b", "fn_a"});
        if (!expect(fn.valid(), "resolveFunction should return first valid function.", outFail)) return false;
        sol::protected_function pfn = fn;
        sol::protected_function_result r = pfn();
        if (!expect(r.valid() && r.get_type() == sol::type::string, "resolved fn should return string.", outFail)) return false;
        if (!expect(r.get<std::string>() == "B", "resolveFunction did not preserve candidate ordering.", outFail)) return false;
    }

    {
        std::string err;
        auto value = game::scripting::callStringFunction(table, {"missing", "fn_a"}, &err);
        if (!expect(value.has_value() && *value == "A", "callStringFunction should call fallback fn_a.", outFail)) return false;
    }

    {
        std::string err;
        auto value = game::scripting::callStringFunction(table, {"fn_num"}, &err);
        if (!expect(!value.has_value(), "callStringFunction should fail on non-string return.", outFail)) return false;
        if (!expect(!err.empty(), "callStringFunction should set error on non-string return.", outFail)) return false;
    }

    {
        std::string err;
        auto value = game::scripting::callStringFunction(table, {"fn_err"}, &err);
        if (!expect(!value.has_value(), "callStringFunction should fail on function error.", outFail)) return false;
        if (!expect(!err.empty(), "callStringFunction should set error on function error.", outFail)) return false;
    }

    {
        std::string err;
        auto value = game::scripting::callStringFunction(table, {"missing"}, &err);
        if (!expect(!value.has_value(), "callStringFunction should fail when function missing.", outFail)) return false;
        if (!expect(!err.empty(), "callStringFunction should set error when function missing.", outFail)) return false;
    }

    return true;
}
