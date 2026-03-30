#include "BuildingEntity.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

glm::mat4 BuildingEntity::getTransformMatrix() const {
    // Create transform matrix: T * R * S
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
    transform *= glm::mat4_cast(rotation);
    glm::vec3 scale = glm::vec3(baseScale.x, currentHeight, baseScale.z);
    transform = glm::scale(transform, scale);
    return transform;
}

glm::vec4 BuildingEntity::getColor() const {
    // Color based on price change percentage
    // Green for positive, red for negative, gray for neutral

    if (priceChangePercent > 5.0f) {
        // Strong positive: Bright green
        return glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    } else if (priceChangePercent > 0.5f) {
        // Moderate positive: Light green
        float intensity = glm::clamp(priceChangePercent / 5.0f, 0.0f, 1.0f);
        return glm::vec4(0.0f, 0.5f + intensity * 0.5f, 0.0f, 1.0f);
    } else if (priceChangePercent < -5.0f) {
        // Strong negative: Bright red
        return glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    } else if (priceChangePercent < -0.5f) {
        // Moderate negative: Light red
        float intensity = glm::clamp(-priceChangePercent / 5.0f, 0.0f, 1.0f);
        return glm::vec4(0.5f + intensity * 0.5f, 0.0f, 0.0f, 1.0f);
    } else {
        // Neutral: Bright cyan for high visibility
        return glm::vec4(0.0f, 0.8f, 0.8f, 1.0f);
    }
}
