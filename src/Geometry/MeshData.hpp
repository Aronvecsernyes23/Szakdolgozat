#pragma once
#include <glm/glm.hpp>
#include <vector>

namespace plumbcad
{
    struct Vertex
    {
        glm::vec3 position{};
        glm::vec3 normal{};
    };

    struct MeshData
    {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
    };
}
