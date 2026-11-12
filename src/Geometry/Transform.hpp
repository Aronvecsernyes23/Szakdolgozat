#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace plumbcad
{
    struct Transform
    {
        glm::vec3 position{0.0f};
        glm::vec3 rotationEuler{0.0f};
        glm::vec3 scale{1.0f};

        [[nodiscard]] glm::mat4 matrix() const
        {
            glm::mat4 m{1.0f};
            m = glm::translate(m, position);
            m = glm::rotate(m, rotationEuler.x, {1, 0, 0});
            m = glm::rotate(m, rotationEuler.y, {0, 1, 0});
            m = glm::rotate(m, rotationEuler.z, {0, 0, 1});
            m = glm::scale(m, scale);
            return m;
        }
    };
}
