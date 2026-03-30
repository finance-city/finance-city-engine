#include "Camera.hpp"
#include "src/utils/Logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>

Camera::Camera(float aspectRatio)
    : position(400.0f, 500.0f, 900.0f),  // Aerial view over building grid
      target(400.0f, 0.0f, 400.0f),        // Look at building grid center
      up(0.0f, 1.0f, 0.0f),
      yaw(glm::radians(0.0f)),
      pitch(glm::radians(47.0f)),
      distance(550.0f),
      aspectRatio(aspectRatio),
      fov(glm::radians(45.0f)),        // Narrower FOV for city overview
      nearPlane(1.0f),
      farPlane(5000.0f) {
    // Don't call updateCameraVectors() - use fixed position/target
    LOG_DEBUG("Camera") << "Initialized at pos=(400, 500, 900), target=(400, 0, 400), FOV=45deg";
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, target, up);
}

glm::mat4 Camera::getProjectionMatrix() const {
    glm::mat4 proj = glm::perspective(fov, aspectRatio, nearPlane, farPlane);
#ifndef __EMSCRIPTEN__
    // Vulkan NDC has Y pointing down, flip it
    // WebGPU uses OpenGL-style coordinates, so no flip needed
    proj[1][1] *= -1;
#endif
    return proj;
}

void Camera::rotate(float deltaX, float deltaY) {
    yaw -= deltaX * 0.005f;  // Rotation sensitivity (inverted)
    pitch -= deltaY * 0.005f;  // Rotation sensitivity (inverted)

    // Clamp pitch to avoid gimbal lock
    pitch = std::clamp(pitch, glm::radians(-89.0f), glm::radians(89.0f));

    updateCameraVectors();
}

void Camera::translate(float deltaX, float deltaY) {
    // Calculate right and up vectors
    glm::vec3 forward = glm::normalize(target - position);
    glm::vec3 right = glm::normalize(glm::cross(forward, up));
    glm::vec3 upVector = glm::normalize(glm::cross(right, forward));

    // Pan speed scales with zoom distance for consistent feel
    float panSpeed = std::max(10.0f, distance) * 0.002f;
    glm::vec3 translation = right * deltaX * panSpeed + upVector * deltaY * panSpeed;
    position += translation;
    target += translation;

    // Prevent camera from going below ground
    if (position.y < 10.0f) {
        float diff = 10.0f - position.y;
        position.y = 10.0f;
        target.y += diff;
    }
}

void Camera::pan(float deltaX, float deltaZ) {
    glm::vec3 forward = glm::normalize(target - position);
    glm::vec3 groundForward = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    glm::vec3 groundRight = glm::normalize(glm::vec3(-groundForward.z, 0.0f, groundForward.x));
    float speed = std::max(10.0f, distance) * 0.002f;
    glm::vec3 translation = groundRight * deltaX * speed + groundForward * deltaZ * speed;
    position += translation;
    target += translation;
}

void Camera::elevate(float delta) {
    float speed = std::max(10.0f, distance) * 0.002f;
    float dy = delta * speed;
    position.y += dy;
    target.y += dy;
    if (position.y < 10.0f) {
        float diff = 10.0f - position.y;
        position.y = 10.0f;
        target.y += diff;
    }
}

void Camera::zoom(float delta) {
    // Zoom speed scales with current distance for smooth near/far transitions
    distance -= delta * distance * 0.08f;
    distance = std::clamp(distance, 50.0f, 3000.0f);
    updateCameraVectors();
}

void Camera::setAspectRatio(float newAspectRatio) {
    aspectRatio = newAspectRatio;
}

void Camera::reset() {
    position = glm::vec3(400.0f, 500.0f, 900.0f);
    target = glm::vec3(400.0f, 0.0f, 400.0f);
    up = glm::vec3(0.0f, 1.0f, 0.0f);
    yaw = glm::radians(0.0f);
    pitch = glm::radians(47.0f);
    distance = 550.0f;
    updateCameraVectors();
}

void Camera::updateCameraVectors() {
    // Calculate new position based on spherical coordinates
    // Using yaw and pitch to orbit around the target
    float x = distance * cos(pitch) * sin(yaw);
    float y = distance * sin(pitch);
    float z = distance * cos(pitch) * cos(yaw);

    position = target + glm::vec3(x, y, z);

    // Prevent camera from going below ground
    if (position.y < 10.0f) {
        position.y = 10.0f;
    }

    up = glm::vec3(0.0f, 1.0f, 0.0f);
}
