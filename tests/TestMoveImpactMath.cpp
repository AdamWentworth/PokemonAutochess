#include <cmath>
#include <string>

#include "game/PokemonInstance.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
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

    unit.modelScaleCorrection = 1.0f / 0.537488f;
    unit.speciesScale = 0.85f;
    unit.visualScale = 1.0f;
    unit.captureScale = 1.0f;
    if (!expect(nearf(computeModelWorldScaleForMoveImpact(unit, 0.537488f), 0.85f, 0.0005f),
                "Backend move-impact scale should include cached mesh modelScaleFactor when no legacy model is loaded.",
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

    {
        game::runtime::render_model::MeshData attackerMesh;
        attackerMesh.modelScaleFactor = 1.0f;
        attackerMesh.boundsMin = glm::vec3(-0.5f, 0.0f, -0.5f);
        attackerMesh.boundsMax = glm::vec3(0.5f, 2.0f, 0.5f);

        game::runtime::render_model::MeshData targetMesh;
        targetMesh.modelScaleFactor = 1.0f;
        targetMesh.boundsMin = glm::vec3(-0.5f, 0.0f, -0.5f);
        targetMesh.boundsMax = glm::vec3(0.5f, 1.0f, 0.5f);
        targetMesh.vertices.resize(4u);
        targetMesh.vertices[0].position = glm::vec3(-0.5f, 0.0f, -0.5f);
        targetMesh.vertices[1].position = glm::vec3(0.5f, 0.0f, -0.5f);
        targetMesh.vertices[2].position = glm::vec3(-0.5f, 1.0f, -0.5f);
        targetMesh.vertices[3].position = glm::vec3(0.5f, 1.0f, -0.5f);
        targetMesh.indices = {0u, 1u, 2u, 2u, 1u, 3u};

        PokemonInstance attacker;
        attacker.position = glm::vec3(0.0f, 0.0f, -2.0f);

        PokemonInstance target;
        target.position = glm::vec3(0.0f, 0.0f, 0.0f);

        const MoveImpactSurfacePoint impact =
            computeTargetSurfaceImpactPoint(target, &attacker, &targetMesh, &attackerMesh);

        if (!expect(impact.usedMeshSurface,
                    "Move-impact math should use authored mesh triangles when CPU mesh data is available.",
                    outFail)) {
            return false;
        }
        if (!expect(nearf(impact.position.z, -0.5f, 0.0005f),
                    "Move-impact surface point should land on the target surface nearest the attacker.",
                    outFail)) {
            return false;
        }
        if (!expect(nearf(impact.position.y, 0.5f, 0.0005f),
                    "Move-impact surface point should preserve the nearest surface boundary but retarget height toward the target's visual middle.",
                    outFail)) {
            return false;
        }
        if (!expect(std::fabs(impact.normal.z) > 0.9f,
                    "Move-impact surface point should return a sensible surface normal for the chosen triangle.",
                    outFail)) {
            return false;
        }
    }

    {
        game::runtime::render_model::MeshData targetMesh;
        targetMesh.modelScaleFactor = 1.0f;
        targetMesh.boundsMin = glm::vec3(-0.5f, 0.0f, -0.5f);
        targetMesh.boundsMax = glm::vec3(0.5f, 4.0f, 0.5f);
        targetMesh.vertices.resize(7u);
        targetMesh.vertices[0].position = glm::vec3(-0.5f, 0.0f, -0.5f);
        targetMesh.vertices[1].position = glm::vec3(0.5f, 0.0f, -0.5f);
        targetMesh.vertices[2].position = glm::vec3(-0.5f, 1.0f, -0.5f);
        targetMesh.vertices[3].position = glm::vec3(0.5f, 1.0f, -0.5f);
        targetMesh.vertices[4].position = glm::vec3(0.0f, 3.0f, -0.2f);
        targetMesh.vertices[5].position = glm::vec3(-0.1f, 4.0f, -0.2f);
        targetMesh.vertices[6].position = glm::vec3(0.1f, 4.0f, -0.2f);
        targetMesh.indices = {
            0u, 1u, 2u,
            2u, 1u, 3u,
            4u, 5u, 6u,
        };
        targetMesh.triangleNodeIndex = {0, 0, 1};
        targetMesh.nodeNames = {"body", "pm0001_00_00_right_tuta_mesh_shape"};

        game::runtime::render_model::MeshData attackerMesh;
        attackerMesh.modelScaleFactor = 1.0f;
        attackerMesh.boundsMin = glm::vec3(-0.5f, 0.0f, -0.5f);
        attackerMesh.boundsMax = glm::vec3(0.5f, 2.0f, 0.5f);

        PokemonInstance attacker;
        attacker.position = glm::vec3(0.0f, 0.0f, -2.0f);

        PokemonInstance target;
        target.name = "bulbasaur";
        target.position = glm::vec3(0.0f, 0.0f, 0.0f);

        const glm::vec3 center = computeMoveImpactWorldCenter(target, &targetMesh);
        if (!expect(nearf(center.y, 0.5f, 0.0005f),
                    "Bulbasaur move-impact center should ignore the real parsed tuta mesh node when deriving mesh bounds.",
                    outFail)) {
            return false;
        }

        const MoveImpactSurfacePoint impact =
            computeTargetSurfaceImpactPoint(target, &attacker, &targetMesh, &attackerMesh);
        if (!expect(nearf(impact.position.y, 0.5f, 0.0005f),
                    "Bulbasaur move-impact surface point should ignore the real parsed tuta mesh node instead of pulling the hit point upward.",
                    outFail)) {
            return false;
        }
    }

    return true;
}
