// Camera3D.cpp

#include "Camera3D.h"

Camera3D::Camera3D(float fovDeg, float aspect, float nearPlane, float farPlane)
    : position(0.0f, 5.0f, 10.0f),
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

glm::mat4 Camera3D::getViewMatrix() const {
    return glm::lookAt(position, target, upVector);
}

glm::mat4 Camera3D::getProjectionMatrix() const {
    return glm::perspective(fov, aspectRatio, nearZ, farZ);
}
