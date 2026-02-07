// tests/TestDeterminism.cpp
#include <string>
#include <cmath>

#include "engine/core/Random.h"
#include "engine/core/TimeSources.h"

bool test_rng_determinism(std::string& outFail) {
    engine::XorShift32 a(1234u);
    engine::XorShift32 b(1234u);

    for (int i = 0; i < 5; ++i) {
        if (a.nextU32() != b.nextU32()) {
            outFail = "XorShift32 sequence differs for same seed";
            return false;
        }
    }

    engine::XorShift32 c(1u);
    const std::uint32_t first = c.nextU32();
    c.reseed(1u);
    if (first != c.nextU32()) {
        outFail = "XorShift32 reseed did not reproduce first value";
        return false;
    }

    engine::XorShift32 r(7u);
    for (int i = 0; i < 100; ++i) {
        const int v = engine::random::rangeInclusive(r, -2, 2);
        if (v < -2 || v > 2) {
            outFail = "rangeInclusive returned out-of-range value";
            return false;
        }
    }

    return true;
}

bool test_manual_time_source(std::string& outFail) {
    engine::ManualTimeSource t;
    if (std::fabs(t.nowSeconds()) > 1e-9) {
        outFail = "ManualTimeSource should start at 0.0";
        return false;
    }

    t.advance(0.25);
    if (std::fabs(t.nowSeconds() - 0.25) > 1e-9) {
        outFail = "ManualTimeSource did not advance to 0.25";
        return false;
    }

    t.advance(1.0);
    if (std::fabs(t.nowSeconds() - 1.25) > 1e-9) {
        outFail = "ManualTimeSource did not advance to 1.25";
        return false;
    }

    return true;
}
