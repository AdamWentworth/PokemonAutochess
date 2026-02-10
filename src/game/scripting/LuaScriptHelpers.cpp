#include "game/scripting/LuaScriptHelpers.h"

namespace game::scripting {

bool hasFunction(const sol::table& table, const char* name) {
    if (!name || *name == '\0') return false;
    const sol::object obj = table.raw_get<sol::object>(name);
    if (!obj.valid() || obj.get_type() != sol::type::function) return false;
    const sol::function fn = obj.as<sol::function>();
    return fn.valid();
}

bool hasAnyFunction(const sol::table& table, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (hasFunction(table, name)) return true;
    }
    return false;
}

sol::function resolveFunction(const sol::table& table, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (!name || *name == '\0') continue;
        const sol::object obj = table.raw_get<sol::object>(name);
        if (!obj.valid() || obj.get_type() != sol::type::function) continue;
        const sol::function fn = obj.as<sol::function>();
        if (fn.valid()) return fn;
    }
    return sol::function{};
}

std::optional<std::string> callStringFunction(const sol::table& table,
                                              std::initializer_list<const char*> names,
                                              std::string* outError) {
    sol::function fn = resolveFunction(table, names);
    if (!fn.valid()) {
        if (outError) *outError = "string function is missing";
        return std::nullopt;
    }

    sol::protected_function pfn = fn;
    sol::protected_function_result result = pfn();
    if (!result.valid()) {
        if (outError) {
            sol::error err = result;
            *outError = err.what();
        }
        return std::nullopt;
    }

    if (result.get_type() != sol::type::string) {
        if (outError) *outError = "function did not return a string";
        return std::nullopt;
    }

    return result.get<std::string>();
}

} // namespace game::scripting
