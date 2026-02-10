#pragma once

#include <sol/sol.hpp>

#include <initializer_list>
#include <optional>
#include <string>

namespace game::scripting {

bool hasFunction(const sol::table& table, const char* name);
bool hasAnyFunction(const sol::table& table, std::initializer_list<const char*> names);
sol::function resolveFunction(const sol::table& table, std::initializer_list<const char*> names);

std::optional<std::string> callStringFunction(const sol::table& table,
                                              std::initializer_list<const char*> names,
                                              std::string* outError = nullptr);

} // namespace game::scripting
