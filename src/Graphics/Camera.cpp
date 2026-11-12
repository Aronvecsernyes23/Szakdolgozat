#include "Graphics/Camera.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

namespace plumbcad
{
    glm::vec3 Camera::forward() const
    {
        const float yaw = glm::radians(yawDeg);
        const float pitch = glm::radians(pitchDeg);

        glm::vec3 direction;
        direction.x = std::cos(yaw) * std::cos(pitch);
        direction.y = std::sin(pitch);
        direction.z = std::sin(yaw) * std::cos(pitch);
        return glm::normalize(direction);
    }

    glm::vec3 Camera::right() const
    {
        const glm::vec3 f = forward();

        glm::vec3 referenceUp{0.0f, 1.0f, 0.0f};

        // Pontos felülnézetben a forward párhuzamos a világ Y tengelyével,
        // ezért a klasszikus cross(forward, worldUp) nullvektor lenne.
        if (std::abs(glm::dot(f, referenceUp)) > 0.999f)
        {
            referenceUp = glm::vec3{0.0f, 0.0f, -1.0f};
        }

        return glm::normalize(glm::cross(f, referenceUp));
    }

    glm::vec3 Camera::up() const
    {
        return glm::normalize(glm::cross(right(), forward()));
    }

    void Camera::addRotation(float deltaYawDeg, float deltaPitchDeg)
    {
        yawDeg += deltaYawDeg;
        pitchDeg = std::clamp(pitchDeg + deltaPitchDeg, -89.0f, 89.0f);
    }

    void Camera::addZoom(float deltaFovDeg)
    {
        if (orthographic)
        {
            // Ortografikus módban nincs FOV: a látható világmagasság változik.
            orthoHeight = std::clamp(orthoHeight + deltaFovDeg * 0.50f, 0.25f, 120.0f);

            return;
        }

        fovDeg = std::clamp(fovDeg + deltaFovDeg, 20.0f, 80.0f);
    }

    glm::mat4 Camera::view() const
    {
        return glm::lookAt(position, position + forward(), up());
    }

    glm::mat4 Camera::projection(float aspect) const
    {
        const float safeAspect = std::max(0.01f, aspect);

        if (orthographic)
        {
            const float halfHeight = std::max(0.125f, orthoHeight * 0.5f);

            const float halfWidth = halfHeight * safeAspect;

            return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, 0.01f, 500.0f);
        }

        return glm::perspective(glm::radians(fovDeg), safeAspect, 0.01f, 500.0f);
    }
}
