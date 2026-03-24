#include "game/scripting/LuaTextMenuParser.h"

#include <algorithm>
#include <cmath>

namespace game::scripting {

bool parseTextMenuEntries(sol::protected_function fn,
                          std::vector<TextMenuEntryData>& out,
                          std::string* outError) {
    out.clear();
    if (!fn.valid()) {
        if (outError) *outError = "text menu function is missing";
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
        if (outError) *outError = "text menu function did not return a table";
        return false;
    }

    sol::table table = result.get<sol::table>();
    for (auto&& kv : table) {
        if (kv.second.get_type() != sol::type::table) continue;
        sol::table row = kv.second.as<sol::table>();

        TextMenuEntryData entry;
        entry.id = row.get_or("id", std::string());
        if (entry.id.empty()) {
            entry.id = row.get_or("name", std::string());
        }
        entry.label = row.get_or("label", entry.id);
        if (entry.id.empty()) entry.id = entry.label;
        if (entry.id.empty() || entry.label.empty()) continue;

        if (auto v = row.get<sol::optional<float>>("scale")) {
            entry.scale = std::max(0.1f, *v);
        }
        if (auto v = row.get<sol::optional<bool>>("enabled")) {
            entry.enabled = *v;
        }
        if (auto v = row.get<sol::optional<bool>>("bold")) {
            entry.bold = *v;
        }
        if (auto v = row.get<sol::optional<bool>>("underline")) {
            entry.underline = *v;
        }
        if (auto v = row.get<sol::optional<float>>("x_frac")) {
            entry.hasCustomX = true;
            entry.xFrac = std::clamp(*v, 0.0f, 1.0f);
        }
        if (auto v = row.get<sol::optional<float>>("y_frac")) {
            entry.hasCustomY = true;
            entry.yFrac = std::clamp(*v, 0.0f, 1.0f);
        }
        if (auto v = row.get<sol::optional<std::string>>("anchor")) {
            entry.anchorCenter = (*v != "left");
        }

        auto colorOpt = row.get<sol::optional<sol::table>>("color");
        if (colorOpt) {
            const sol::table color = *colorOpt;
            auto r = color.get<sol::optional<float>>(1);
            auto g = color.get<sol::optional<float>>(2);
            auto b = color.get<sol::optional<float>>(3);
            if (r && g && b) {
                entry.hasColor = true;
                entry.colorR = std::clamp(*r, 0.0f, 1.0f);
                entry.colorG = std::clamp(*g, 0.0f, 1.0f);
                entry.colorB = std::clamp(*b, 0.0f, 1.0f);
            }
        }

        auto sliderOpt = row.get<sol::optional<sol::table>>("slider");
        if (sliderOpt) {
            const sol::table slider = *sliderOpt;
            float sliderMin = slider.get_or("min", 0.0f);
            float sliderMax = slider.get_or("max", 1.0f);
            if (sliderMax < sliderMin) {
                std::swap(sliderMin, sliderMax);
            }

            entry.hasSlider = true;
            entry.sliderMin = sliderMin;
            entry.sliderMax = sliderMax;
            entry.sliderStep = std::fabs(slider.get_or("step", 0.0f));
            entry.sliderWidthFrac = std::clamp(slider.get_or("width_frac", 0.2f), 0.08f, 0.45f);
            entry.sliderValue = std::clamp(slider.get_or("value", sliderMin), sliderMin, sliderMax);
            entry.sliderValueLabel = slider.get_or("value_label", std::string());

            const float sliderRange = entry.sliderMax - entry.sliderMin;
            if (entry.sliderStep > 0.0f && sliderRange > 0.0f) {
                const float steps = std::round((entry.sliderValue - entry.sliderMin) / entry.sliderStep);
                entry.sliderValue = std::clamp(entry.sliderMin + steps * entry.sliderStep,
                                               entry.sliderMin,
                                               entry.sliderMax);
            }
        }

        out.push_back(std::move(entry));
    }

    return true;
}

} // namespace game::scripting
