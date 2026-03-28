#include <cmath>
#include <string>

#include "engine/render/Camera3D.h"
#include "game/systems/CameraPanClamp.h"

bool test_camera_pan_clamp_contract(std::string& outFail) {
    using game::camera_pan_clamp::Bounds;
    using game::camera_pan_clamp::buildBoardSafeBounds;
    using game::camera_pan_clamp::clampCameraPan;

    const Bounds bounds = buildBoardSafeBounds(8, 8, 1.2f);
    if (!(bounds.minX < 0.0f && bounds.maxX > 0.0f &&
          bounds.minZ < 0.0f && bounds.maxZ > 0.0f)) {
        outFail =
            "camera pan clamp should build a board-centered clamp box.";
        return false;
    }

    {
        Camera3D camera(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
        const glm::vec3 oldOffset = camera.getPosition() - camera.getTarget();
        camera.move({8.0f, 0.0f, -6.0f});

        clampCameraPan(camera, bounds);

        const glm::vec3 target = camera.getTarget();
        const glm::vec3 newOffset = camera.getPosition() - target;
        if (target.x < bounds.minX || target.x > bounds.maxX ||
            target.z < bounds.minZ || target.z > bounds.maxZ) {
            outFail =
                "camera pan clamp should keep the target inside the board-safe bounds.";
            return false;
        }
        if (std::fabs(target.y + 1.0f) > 0.0001f) {
            outFail =
                "camera pan clamp should not change the target height.";
            return false;
        }
        if (std::fabs(newOffset.x - oldOffset.x) > 0.0001f ||
            std::fabs(newOffset.y - oldOffset.y) > 0.0001f ||
            std::fabs(newOffset.z - oldOffset.z) > 0.0001f) {
            outFail =
                "camera pan clamp should preserve the camera's orbit/zoom offset.";
            return false;
        }
    }

    {
        Camera3D camera(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
        const glm::vec3 oldPosition = camera.getPosition();
        const glm::vec3 oldTarget = camera.getTarget();

        clampCameraPan(camera, bounds);

        if (glm::distance(camera.getPosition(), oldPosition) > 0.0001f ||
            glm::distance(camera.getTarget(), oldTarget) > 0.0001f) {
            outFail =
                "camera pan clamp should leave already-safe camera framing unchanged.";
            return false;
        }
    }

    {
        Camera3D camera(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
        const float defaultDistance =
            glm::distance(camera.getPosition(), camera.getTarget());

        camera.zoom(3.5f);
        camera.zoom(-999.0f);

        const float zoomedOutDistance =
            glm::distance(camera.getPosition(), camera.getTarget());
        if (std::fabs(zoomedOutDistance - defaultDistance) > 0.0001f) {
            outFail =
                "camera zoom should not retract farther than the startup default distance.";
            return false;
        }
    }

    {
        Camera3D camera(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
        camera.zoom(999.0f);

        const float zoomedInDistance =
            glm::distance(camera.getPosition(), camera.getTarget());
        if (!(zoomedInDistance < 6.0f)) {
            outFail =
                "camera zoom should allow a closer VFX inspection distance than the old board clamp.";
            return false;
        }
    }

    return true;
}
