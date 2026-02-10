#include <string>
#include <vector>

#include <sol/sol.hpp>

#include "game/scripting/LuaCardParser.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (!condition) {
        outFail = message;
        return false;
    }
    return true;
}

} // namespace

bool test_lua_card_parser_contract(std::string& outFail) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);

    lua.script(R"(
        function good_cards()
            return {
                {
                    name = "bulbasaur",
                    cost = 2,
                    level = 4,
                    label = "Bulbasaur [2g]",
                    image = "assets/images/bulbasaur.png",
                    uv = { 0.10, 0.20, 0.30, 0.40 },
                    type = "Shop"
                },
                {
                    name = "potion",
                    cost = 300,
                    label = "Potion $300",
                    type = "Item"
                },
                {
                    label = "Starter Pick",
                    type = "Starter"
                },
                {
                    junk = 1
                }
            }
        end

        function bad_cards()
            return "oops"
        end
    )");

    std::vector<CardData> cards;
    std::string parseError;

    sol::protected_function goodFn = lua["good_cards"];
    if (!expect(game::scripting::parseCardList(goodFn, cards, &parseError), "good_cards parse failed.", outFail)) return false;
    if (!expect(cards.size() == 3, "good_cards should yield 3 parsed cards.", outFail)) return false;

    if (!expect(cards[0].pokemonName == "bulbasaur", "first card name mismatch.", outFail)) return false;
    if (!expect(cards[0].cost == 2 && cards[0].level == 4, "first card scalar fields mismatch.", outFail)) return false;
    if (!expect(cards[0].label == "Bulbasaur [2g]", "first card label mismatch.", outFail)) return false;
    if (!expect(cards[0].imagePath == "assets/images/bulbasaur.png", "first card image path mismatch.", outFail)) return false;
    if (!expect(cards[0].type == CardType::Shop, "first card type mismatch.", outFail)) return false;
    if (!expect(cards[0].uvMin.x == 0.10f && cards[0].uvMin.y == 0.20f &&
                cards[0].uvMax.x == 0.30f && cards[0].uvMax.y == 0.40f,
                "first card UV parsing mismatch.", outFail)) return false;

    if (!expect(cards[1].type == CardType::Item, "second card type should be Item.", outFail)) return false;
    if (!expect(cards[2].type == CardType::Starter, "third card type should be Starter.", outFail)) return false;
    if (!expect(cards[2].pokemonName.empty() && !cards[2].label.empty(),
                "third card should keep label-only entry.", outFail)) return false;

    sol::protected_function badFn = lua["bad_cards"];
    if (!expect(!game::scripting::parseCardList(badFn, cards, &parseError), "bad_cards should fail parsing.", outFail)) return false;
    if (!expect(!parseError.empty(), "bad_cards failure should return a parse error.", outFail)) return false;

    sol::protected_function missingFn = lua["missing_cards"];
    parseError.clear();
    if (!expect(!game::scripting::parseCardList(missingFn, cards, &parseError), "missing function should fail parsing.", outFail)) return false;
    if (!expect(!parseError.empty(), "missing function failure should include parse error.", outFail)) return false;

    return true;
}
