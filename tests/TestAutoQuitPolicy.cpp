#include "game/runtime/AutoQuitPolicy.h"

#include <string>

bool test_auto_quit_policy_contract(std::string& outFail) {
    using game::runtime::auto_quit::Policy;
    using game::runtime::auto_quit::parsePositiveDouble;
    using game::runtime::auto_quit::parsePositiveInt;
    using game::runtime::auto_quit::shouldTrigger;

    {
        const auto value = parsePositiveDouble("1.25");
        if (!value.has_value() || *value <= 1.2 || *value >= 1.3) {
            outFail = "parsePositiveDouble should parse positive floating values";
            return false;
        }
    }

    if (parsePositiveDouble("0").has_value() ||
        parsePositiveDouble("-1").has_value() ||
        parsePositiveDouble("abc").has_value()) {
        outFail = "parsePositiveDouble should reject non-positive/invalid values";
        return false;
    }

    {
        const auto value = parsePositiveInt("60");
        if (!value.has_value() || *value != 60) {
            outFail = "parsePositiveInt should parse positive integer values";
            return false;
        }
    }

    if (parsePositiveInt("0").has_value() ||
        parsePositiveInt("-2").has_value() ||
        parsePositiveInt("x").has_value()) {
        outFail = "parsePositiveInt should reject non-positive/invalid values";
        return false;
    }

    {
        Policy p;
        p.maxSeconds = 2.0;
        p.maxFrames = 120;
        if (!p.enabled()) {
            outFail = "Policy with limits should be enabled";
            return false;
        }
        if (shouldTrigger(p, 1.0, 100)) {
            outFail = "shouldTrigger should not fire before frame/second thresholds";
            return false;
        }
        if (!shouldTrigger(p, 2.1, 100)) {
            outFail = "shouldTrigger should fire when second threshold is reached";
            return false;
        }
        if (!shouldTrigger(p, 1.0, 120)) {
            outFail = "shouldTrigger should fire when frame threshold is reached";
            return false;
        }
    }

    {
        Policy p;
        if (p.enabled()) {
            outFail = "Default policy should be disabled";
            return false;
        }
        if (shouldTrigger(p, 999.0, 9999)) {
            outFail = "Disabled policy should not trigger";
            return false;
        }
    }

    return true;
}
