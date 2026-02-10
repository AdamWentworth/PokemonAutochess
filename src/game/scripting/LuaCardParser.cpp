#include "game/scripting/LuaCardParser.h"

namespace game::scripting {
namespace {

CardType parseCardType(const std::string& typeName) {
    if (typeName == "Starter") return CardType::Starter;
    if (typeName == "Item") return CardType::Item;
    return CardType::Shop;
}

bool parseCardTable(sol::table table, std::vector<CardData>& out) {
    out.clear();
    for (auto&& kv : table) {
        if (kv.second.get_type() != sol::type::table) continue;
        sol::table row = kv.second.as<sol::table>();

        CardData card;
        card.pokemonName = row.get_or("name", std::string());
        card.cost = row.get_or("cost", 0);
        card.level = row.get_or("level", 0);
        card.label = row.get_or("label", std::string());
        card.imagePath = row.get_or("image", std::string());
        card.type = parseCardType(row.get_or("type", std::string("Shop")));

        auto uvOpt = row.get<sol::optional<sol::table>>("uv");
        if (uvOpt) {
            const sol::table uv = *uvOpt;
            auto u0 = uv.get<sol::optional<float>>(1);
            auto v0 = uv.get<sol::optional<float>>(2);
            auto u1 = uv.get<sol::optional<float>>(3);
            auto v1 = uv.get<sol::optional<float>>(4);
            if (u0 && v0 && u1 && v1) {
                card.uvMin = { *u0, *v0 };
                card.uvMax = { *u1, *v1 };
            }
        }

        if (!card.pokemonName.empty() || !card.label.empty()) {
            out.push_back(std::move(card));
        }
    }
    return true;
}

} // namespace

bool parseCardList(sol::protected_function fn, std::vector<CardData>& out, std::string* outError) {
    out.clear();
    if (!fn.valid()) {
        if (outError) *outError = "card list function is missing";
        return false;
    }

    sol::protected_function_result result = fn();
    if (!result.valid()) {
        if (outError) {
            sol::error err = result;
            *outError = err.what();
        }
        return false;
    }

    if (result.get_type() != sol::type::table) {
        if (outError) *outError = "card list function did not return a table";
        return false;
    }

    return parseCardTable(result.get<sol::table>(), out);
}

} // namespace game::scripting
