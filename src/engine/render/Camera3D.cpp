// Camera3D.cpp

#include "Camera3D.h"

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

glm::mat4 Camera3D::getViewMatrix() const {
    return glm::lookAt(position, target, upVector);
}

glm::mat4 Camera3D::getProjectionMatrix() const {
    return glm::perspective(fov, aspectRatio, nearZ, farZ);
}

glm::vec3 Camera3D::getDirection() const {
    return glm::normalize(target - position);
}