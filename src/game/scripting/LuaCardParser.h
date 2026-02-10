#pragma once

#include "engine/ui/Card.h"

#include <sol/sol.hpp>

#include <string>
#include <vector>

namespace game::scripting {

bool parseCardList(sol::protected_function fn, std::vector<CardData>& out, std::string* outError = nullptr);

} // namespace game::scripting
