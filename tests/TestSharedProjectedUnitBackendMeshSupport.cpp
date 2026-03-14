#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"

#include "game/runtime/render_model_cache/RenderModelCache.h"

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
        MeshData rigidMesh;
        rigidMesh.vertices.resize(3);
        for (auto& vertex : rigidMesh.vertices) {
            vertex.w0 = 1.0f;
            vertex.j0 = 0u;
        }
        rigidMesh.indices = {0u, 1u, 2u};
        rigidMesh.triangleSubmesh = {0u};
        rigidMesh.triangleNodeIndex = {0};
        rigidMesh.nodeSkin = {0};
        pac_model_types::SkinData rigidSkin;
        rigidSkin.joints = {0};
        rigidSkin.inverseBind = {glm::mat4(1.0f)};
        rigidMesh.skins = {rigidSkin};

        const auto* rigidCache =
            support::ensureFastTexturedMeshTemplateCache(&rigidMesh, {}, 1u);
        if (!rigidCache ||
            rigidCache->batches.size() != 1u ||
            rigidCache->batches[0].rigidJointIndex != 0u) {
            outFail =
                "Projected mesh support should detect rigid single-joint batches in the fast textured cache.";
            return false;
        }
    }

    {
        MeshData mixedMesh;
        mixedMesh.vertices.resize(3);
        mixedMesh.vertices[0].w0 = 1.0f;
        mixedMesh.vertices[0].j0 = 0u;
        mixedMesh.vertices[1].w0 = 1.0f;
        mixedMesh.vertices[1].j0 = 1u;
        mixedMesh.vertices[2].w0 = 1.0f;
        mixedMesh.vertices[2].j0 = 0u;
        mixedMesh.indices = {0u, 1u, 2u};
        mixedMesh.triangleSubmesh = {0u};
        mixedMesh.triangleNodeIndex = {0};
        mixedMesh.nodeSkin = {0};
        pac_model_types::SkinData mixedSkin;
        mixedSkin.joints = {0, 1};
        mixedSkin.inverseBind = {glm::mat4(1.0f), glm::mat4(1.0f)};
        mixedMesh.skins = {mixedSkin};

        const auto* mixedCache =
            support::ensureFastTexturedMeshTemplateCache(&mixedMesh, {}, 1u);
        if (!mixedCache ||
            mixedCache->batches.size() != 1u ||
            mixedCache->batches[0].rigidJointIndex != support::kInvalidRigidJointIndex) {
            outFail =
                "Projected mesh support should leave mixed-joint batches on the skinned path.";
            return false;
        }
    }

    return true;
}
