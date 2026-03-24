#include <string>
#include <vector>

#include <sol/sol.hpp>

#include "game/scripting/LuaTextMenuParser.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (!condition) {
        outFail = message;
        return false;
    }
    return true;
}

} // namespace

bool test_lua_text_menu_parser_contract(std::string& outFail) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);

    lua.script(R"(
        function good_menu()
            return {
                {
                    id = "start",
                    label = "Start",
                    scale = 1.25,
                    enabled = true,
                    bold = true,
                    underline = false,
                    x_frac = 1.2,
                    y_frac = -0.3,
                    anchor = "center",
                    color = { 1.2, 0.5, -0.1 }
                },
                {
                    name = "settings",
                    -- label omitted on purpose, should fallback to id/name
                    enabled = false,
                    anchor = "left"
                },
                {
                    label = "LabelOnly"
                },
                {
                    id = "audio_master",
                    label = "Master",
                    slider = {
                        min = 0,
                        max = 100,
                        value = 77,
                        step = 10,
                        width_frac = 0.55,
                        value_label = "77%"
                    }
                },
                {
                    junk = 1
                }
            }
        end

        function bad_menu()
            return "oops"
        end
    )");

    std::vector<game::scripting::TextMenuEntryData> entries;
    std::string parseError;

    sol::protected_function goodFn = lua["good_menu"];
    if (!expect(game::scripting::parseTextMenuEntries(goodFn, entries, &parseError),
                "good_menu parse failed.", outFail)) return false;
    if (!expect(entries.size() == 4, "good_menu should yield 4 parsed entries.", outFail)) return false;

    const auto& a = entries[0];
    if (!expect(a.id == "start" && a.label == "Start", "entry 0 id/label mismatch.", outFail)) return false;
    if (!expect(a.scale == 1.25f, "entry 0 scale mismatch.", outFail)) return false;
    if (!expect(a.bold && !a.underline && a.enabled, "entry 0 flag parsing mismatch.", outFail)) return false;
    if (!expect(a.hasCustomX && a.hasCustomY, "entry 0 custom axis flags mismatch.", outFail)) return false;
    if (!expect(a.xFrac == 1.0f && a.yFrac == 0.0f, "entry 0 frac clamping mismatch.", outFail)) return false;
    if (!expect(a.anchorCenter, "entry 0 anchor parsing mismatch.", outFail)) return false;
    if (!expect(a.hasColor, "entry 0 color should be set.", outFail)) return false;
    if (!expect(a.colorR == 1.0f && a.colorG == 0.5f && a.colorB == 0.0f,
                "entry 0 color clamping mismatch.", outFail)) return false;

    const auto& b = entries[1];
    if (!expect(b.id == "settings" && b.label == "settings", "entry 1 id/label fallback mismatch.", outFail)) return false;
    if (!expect(!b.enabled, "entry 1 enabled flag mismatch.", outFail)) return false;
    if (!expect(!b.anchorCenter, "entry 1 anchor should be left.", outFail)) return false;

    const auto& c = entries[2];
    if (!expect(c.id == "LabelOnly" && c.label == "LabelOnly", "entry 2 label-only fallback mismatch.", outFail)) return false;

    const auto& d = entries[3];
    if (!expect(d.id == "audio_master" && d.hasSlider, "entry 3 slider metadata missing.", outFail)) return false;
    if (!expect(d.sliderMin == 0.0f && d.sliderMax == 100.0f, "entry 3 slider range mismatch.", outFail)) return false;
    if (!expect(d.sliderValue == 80.0f, "entry 3 slider value should quantize to nearest step.", outFail)) return false;
    if (!expect(d.sliderStep == 10.0f, "entry 3 slider step mismatch.", outFail)) return false;
    if (!expect(d.sliderWidthFrac == 0.45f, "entry 3 slider width should clamp.", outFail)) return false;
    if (!expect(d.sliderValueLabel == "77%", "entry 3 slider value label mismatch.", outFail)) return false;

    sol::protected_function badFn = lua["bad_menu"];
    if (!expect(!game::scripting::parseTextMenuEntries(badFn, entries, &parseError),
                "bad_menu should fail parsing.", outFail)) return false;
    if (!expect(!parseError.empty(), "bad_menu failure should return parse error.", outFail)) return false;

    sol::protected_function missingFn = lua["missing_menu"];
    parseError.clear();
    if (!expect(!game::scripting::parseTextMenuEntries(missingFn, entries, &parseError),
                "missing_menu should fail parsing.", outFail)) return false;
    if (!expect(!parseError.empty(), "missing_menu failure should include parse error.", outFail)) return false;

    return true;
}
