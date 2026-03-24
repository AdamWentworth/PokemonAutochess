#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"

#include "game/runtime/render_model_cache/RenderModelCache.h"

#include <string_view>
#include <string>

bool test_shared_projected_unit_backend_mesh_support_contract(std::string& outFail) {
    namespace support = game::runtime::shared_projected_unit_backend_mesh_support;
    using game::runtime::render_model::MeshData;

    if (support::selectUniformTriangleIndex(0u, 0u, 10u) != 0u ||
        support::selectUniformTriangleIndex(3u, 10u, 2u) != 1u) {
        outFail = "Projected mesh support should clamp uniform triangle sampling bounds.";
        return false;
    }

    {
        MeshData mesh;
        mesh.submeshMeshIndex = {2, -1, 0};
        mesh.meshIndexToNode = {7, 11, 19};
        const auto fallback = support::buildSubmeshNodeFallback(mesh);
        if (fallback.size() != 3u || fallback[0] != 19 || fallback[1] != -1 || fallback[2] != 7) {
            outFail = "Projected mesh support should derive submesh node fallback entries from mesh-to-node mappings.";
            return false;
        }
    }

    {
        const std::string unsplit =
            support::makeIndexedGeometryCacheKey("__mesh__", 4u, 4u, 8u);
        const std::string split =
            support::makeIndexedGeometryCacheKey("__mesh__", 4u, 11u, 8u);
        if (unsplit != "__mesh__#submesh_geom:4" ||
            split != "__mesh__#submesh_geom:4#split:11") {
            outFail = "Projected mesh support should format indexed geometry cache keys consistently.";
            return false;
        }
    }

    {
        game::runtime::shared_world_batches::WorldIndexedBatch batch;
        batch.geometryCacheKey = "__mesh__#submesh_geom:6#split:12";
        if (support::resolveBatchBaseSubmeshIndex(batch, 2u) != 6u) {
            outFail = "Projected mesh support should recover base submesh indices from geometry cache keys.";
            return false;
        }
    }

    {
        MeshData mesh;
        mesh.nodeSkin = {3, 3, -1};
        if (support::resolveDefaultSkinNodeIndex(&mesh) != 0) {
            outFail = "Projected mesh support should return the first node using a shared skin.";
            return false;
        }
        mesh.nodeSkin = {1, 2};
        if (support::resolveDefaultSkinNodeIndex(&mesh) != -1) {
            outFail = "Projected mesh support should reject meshes with multiple distinct node skins.";
            return false;
        }
    }

    {
        if (!support::backendUsesAuthoredTailFireMeshPlayback(nullptr) ||
            !support::backendUsesAuthoredTailFireMeshPlayback("opengl") ||
            !support::backendUsesAuthoredTailFireMeshPlayback("d3d12")) {
            outFail = "Projected mesh support should keep authored tail-fire mesh playback available on all backends.";
            return false;
        }
    }

    {
        if (!support::backendUsesGpuClipSkinningForUnit(nullptr, std::string_view("charmander")) ||
            !support::backendUsesGpuClipSkinningForUnit("opengl", std::string_view("charmander")) ||
            support::backendUsesGpuClipSkinningForUnit("d3d12", std::string_view("charmander")) ||
            !support::backendUsesGpuClipSkinningForUnit("d3d12", std::string_view("pikachu"))) {
            outFail = "Projected mesh support should disable GPU clip skinning only for D3D12 tail-fire playback species.";
            return false;
        }
    }

    return true;
}
