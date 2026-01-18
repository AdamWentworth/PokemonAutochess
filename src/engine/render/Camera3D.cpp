// Camera3D.cpp

#include "Camera3D.h"

#include <algorithm>
#include <cmath>

Camera3D::Camera3D(float fovDeg, float aspect, float nearPlane, float farPlane)
    : position(0.0f, 15.0f, 15.0f),    // Higher Y, back Z
      target(0.0f, 0.0f, 0.0f),
      upVector(0.0f, 1.0f, 0.0f),
      fov(glm::radians(fovDeg)),
      aspectRatio(aspect),
      nearZ(nearPlane),
      farZ(farPlane)
{}

void Camera3D::setPosition(const glm::vec3& pos) {
    position = pos;
}

void Camera3D::lookAt(const glm::vec3& tgt, const glm::vec3& up) {
    target = tgt;
    upVector = up;
}

void Camera3D::move(const glm::vec3& delta) {
    position += delta;
    target += delta;
}

void Camera3D::zoom(float delta) {
    position += glm::normalize(target - position) * delta;
}

void Camera3D::orbit(float yawDeltaRad, float pitchDeltaRad) {
    glm::vec3 offset = position - target;
    float r = glm::length(offset);
    if (r < 1e-5f) return;

    // Current spherical angles (Y up)
    float yaw   = std::atan2(offset.x, offset.z);
    float pitch = std::asin(std::clamp(offset.y / r, -1.0f, 1.0f));

    yaw   += yawDeltaRad;
    pitch += pitchDeltaRad;

    // Clamp pitch to avoid flipping / going below the board too much
    const float kMinPitch = 0.15f; // ~8.6 deg above horizon
    const float kMaxPitch = 1.45f; // ~83 deg
    pitch = std::clamp(pitch, kMinPitch, kMaxPitch);

    float cp = std::cos(pitch);
    float sp = std::sin(pitch);
    float sy = std::sin(yaw);
    float cy = std::cos(yaw);

    glm::vec3 newOffset;
    newOffset.x = r * sy * cp;
    newOffset.z = r * cy * cp;
    newOffset.y = r * sp;

    position = target + newOffset;
}

glm::mat4 Camera3D::getViewMatrix() const {
    return glm::lookAt(position, target, upVector);
}

glm::mat4 Camera3D::getProjectionMatrix() const {
    return glm::perspective(fov, aspectRatio, nearZ, farZ);
}

glm::vec3 Camera3D::getDirection() const {
    return glm::normalize(target - position);
}
