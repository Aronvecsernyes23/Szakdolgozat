#pragma once
#include <glm/glm.hpp>

namespace plumbcad
{
    class Camera
    {
    public:
        glm::vec3 position{3.0f, 2.0f, 6.0f};

        // FPS jellegű kamera: yaw = bal-jobb forgás, pitch = fel-le nézés.
        float yawDeg{-90.0f};
        float pitchDeg{-15.0f};
        float fovDeg{45.0f};

        // Valódi ortografikus vetület.
        // orthoHeight = a függőlegesen látható világméret méterben.
        bool orthographic{false};
        float orthoHeight{12.0f};

        float moveSpeed{4.0f};
        float mouseSensitivity{0.12f};

        [[nodiscard]] glm::vec3 forward() const;
        [[nodiscard]] glm::vec3 right() const;
        [[nodiscard]] glm::vec3 up() const;

        void addRotation(float deltaYawDeg, float deltaPitchDeg);
        void addZoom(float deltaFovDeg);

        [[nodiscard]] glm::mat4 view() const;
        [[nodiscard]] glm::mat4 projection(float aspect) const;
    };
}
