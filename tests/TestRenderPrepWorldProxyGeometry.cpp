#include "game/runtime/render_prep/WorldProxyGeometry.h"

#include <cmath>
#include <string>

namespace {

bool approx(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

bool test_render_prep_world_proxy_geometry_contract(std::string& outFail) {
    using game::runtime::render_prep_proxy::UnitProxyExtents;
    using game::runtime::render_prep_proxy::computeShadowQuad;
    using game::runtime::render_prep_proxy::computeUnitProxyCorners;
    using game::runtime::render_prep_proxy::computeUnitProxyExtents;
    using game::runtime::render_prep_proxy::yawForward;
    using game::runtime::render_prep_proxy::yawRight;

    {
        PokemonInstance unit;
        unit.speciesScale = 1.0f;
        unit.modelScaleCorrection = 1.0f;
        unit.visualScale = 1.0f;
        unit.captureScale = 1.0f;
        const UnitProxyExtents ext = computeUnitProxyExtents(unit, 1.0f);
        if (!(ext.halfWidth > 0.05f && ext.halfDepth > 0.05f && ext.height > 0.1f)) {
            outFail = "computeUnitProxyExtents should produce positive dimensions";
            return false;
        }
    }

    {
        const glm::vec3 f = yawForward(0.0f);
        const glm::vec3 r = yawRight(0.0f);
        if (!approx(f.x, 0.0f) || !approx(f.z, 1.0f) ||
            !approx(r.x, 1.0f) || !approx(r.z, 0.0f)) {
            outFail = "yaw basis at 0 degrees mismatch";
            return false;
        }
    }

    {
        const glm::vec3 center(0.0f, 0.0f, 0.0f);
        const UnitProxyExtents ext{0.2f, 0.1f, 0.5f};
        const auto corners = computeUnitProxyCorners(center, ext, 90.0f, 0.02f);
        const float zAbs0 = std::fabs(corners.bottom[0].z);
        const float zAbs1 = std::fabs(corners.bottom[1].z);
        if (!approx(zAbs0, 0.2f) || !approx(zAbs1, 0.2f)) {
            outFail = "computeUnitProxyCorners should rotate footprint by yaw";
            return false;
        }
        if (!approx(corners.top[0].y - corners.bottom[0].y, 0.5f)) {
            outFail = "computeUnitProxyCorners top-bottom height mismatch";
            return false;
        }
    }

    {
        const auto shadow = computeShadowQuad(glm::vec3(1.0f, 0.0f, 2.0f), 0.3f, 0.2f, 0.0f, 0.01f);
        if (shadow.size() != 4u) {
            outFail = "computeShadowQuad should return 4 corners";
            return false;
        }
        if (!approx(shadow[0].y, 0.01f) || !approx(shadow[3].y, 0.01f)) {
            outFail = "computeShadowQuad should keep all corners on requested y-plane";
            return false;
        }
    }

    return true;
}




