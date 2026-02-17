#include <cmath>
#include <string>

#include "game/PokemonInstance.h"
#include "game/world/MoveImpactMath.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool nearf(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

}  // namespace

bool test_move_impact_math(std::string& outFail) {
    PokemonInstance unit;

    unit.modelScaleCorrection = 2.0f;
    unit.speciesScale = 0.5f;
    unit.visualScale = 3.0f;
    unit.captureScale = 0.25f;
    const float expected = 2.0f * 0.5f * 3.0f * 0.25f;
    if (!expect(nearf(computeModelWorldScaleForMoveImpact(unit), expected),
                "Model world scale should multiply non-negative correction factors.",
                outFail)) {
        return false;
    }

    unit.speciesScale = -1.0f;
    if (!expect(nearf(computeModelWorldScaleForMoveImpact(unit), 0.0f),
                "Negative species scale should clamp to zero contribution.",
                outFail)) {
        return false;
    }

    unit.modelScaleCorrection = -2.0f;
    unit.speciesScale = -3.0f;
    unit.visualScale = -4.0f;
    unit.captureScale = -5.0f;
    if (!expect(nearf(computeModelWorldScaleForMoveImpact(unit), 0.0f),
                "All negative correction factors should produce zero world scale.",
                outFail)) {
        return false;
    }

    return true;
}
