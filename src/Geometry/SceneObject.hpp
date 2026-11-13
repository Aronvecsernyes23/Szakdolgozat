#pragma once
#include "Core/Types.hpp"
#include "Geometry/Transform.hpp"
#include "Geometry/MeshData.hpp"
#include <glm/glm.hpp>
#include <string>

namespace plumbcad
{
    struct SceneObject
    {
        ObjectId id{InvalidObjectId};
        std::string name;
        Transform transform;
        MeshData mesh;
        glm::vec3 color{0.4f, 0.7f, 1.0f};
        bool selectable{true};

        // CAD segédgrafika (pl. transzformációs tengelygizmo) esetén
        // a jelölés maradjon látható akkor is, ha részben az objektum belsejéből indul.
        bool renderOnTop{false};
        bool unlit{false};
    };
}
