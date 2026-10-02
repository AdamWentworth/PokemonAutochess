#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTransforms.h"
#include "engine/core/Environment.h"
namespace {
bool backendClipSkinningEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_CLIP_SKINNING");
        if (!env.has_value()) return true;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

bool backendD3D12GpuSkinNodeGlobalsEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_D3D12_GPU_SKIN_NODE_GLOBALS");
        // Default on for D3D12: this path lets the vertex shader compose
        // jointGlobal * inverseBind so the CPU can stop building final joint
        // matrices for every clip-skinned batch.
        if (!env.has_value()) return true;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

bool backendOpenGLGpuSkinNodeGlobalsEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_OPENGL_GPU_SKIN_NODE_GLOBALS");
        // Default on for OpenGL too, but the transport differs from D3D12: the
        // paired payload is uploaded via a world skin UBO instead of plain
        // matrix uniforms.
        if (!env.has_value()) return true;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

}
namespace game::runtime::shared_projected_unit_backend_mesh_transforms {
void Resolver::initialize(const shared_projected_unit_backend_mesh::Args& args,
    const shared_projected_unit_backend_mesh_prep::PreparedState& prep) {
    const std::string backend = args.backendId ? args.backendId : "";
    engine::models::transforms::Resolver::initialize({
        .mesh = prep.mesh,
        .nodeGlobals = prep.scenePose && prep.scenePose->hasScenePose ? &prep.scenePose->nodeGlobals : nullptr,
        .modelMatrix = prep.modelM,
        .hasClipPose = prep.scenePose && prep.scenePose->hasClipPose,
        .positionOnly = prep.usePositionOnlyVertexPath,
        .clipSkinning = backendClipSkinningEnabled() && args.enableClipSkinning,
        .gpuSkinning = args.enableGpuClipSkinning,
        .gpuBindPose = args.enableGpuBindPoseSkinning,
        .pairedSkinMatrices = args.enableGpuClipSkinning &&
            ((backend == "d3d12" && backendD3D12GpuSkinNodeGlobalsEnabled()) ||
             (backend == "opengl" && backendOpenGLGpuSkinNodeGlobalsEnabled())),
        .deformLocal = [&args, &prep](glm::vec3 position) {
            return runtime::render_prep_pose::deformLocalVertex(*args.unit, *args.pose,
                position, prep.mesh->boundsMin, prep.mesh->boundsMax, args.worldCellSize);
        }
    });
}
}
