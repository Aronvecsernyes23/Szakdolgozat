#pragma once
#include "Core/Types.hpp"
#include <glm/glm.hpp>
#include <string>

namespace plumbcad
{
    enum class ArchitecturalObstacleType
    {
        Wall,
        Slab,
        Shaft,
        ForbiddenZone
    };

    struct ArchitecturalObstacle
    {
        ObjectId id{InvalidObjectId};
        ArchitecturalObstacleType type{ArchitecturalObstacleType::Wall};
        glm::vec3 sizeM{3.0f, 2.8f, 0.20f};
        bool blocksRouting{true};
    };

    inline const char* architecturalObstacleTypeName(ArchitecturalObstacleType type)
    {
        switch (type)
        {
        case ArchitecturalObstacleType::Wall: return "Fal";
        case ArchitecturalObstacleType::Slab: return "Födém";
        case ArchitecturalObstacleType::Shaft: return "Akna";
        case ArchitecturalObstacleType::ForbiddenZone: return "Tiltott zóna";
        }
        return "Építészeti akadály";
    }

    inline const char* architecturalObstacleTypeToken(ArchitecturalObstacleType type)
    {
        switch (type)
        {
        case ArchitecturalObstacleType::Wall: return "Wall";
        case ArchitecturalObstacleType::Slab: return "Slab";
        case ArchitecturalObstacleType::Shaft: return "Shaft";
        case ArchitecturalObstacleType::ForbiddenZone: return "ForbiddenZone";
        }
        return "Wall";
    }

    inline bool architecturalObstacleTypeFromToken(const std::string& token, ArchitecturalObstacleType& type)
    {
        if (token == "Wall") { type = ArchitecturalObstacleType::Wall; return true; }
        if (token == "Slab") { type = ArchitecturalObstacleType::Slab; return true; }
        if (token == "Shaft") { type = ArchitecturalObstacleType::Shaft; return true; }
        if (token == "ForbiddenZone") { type = ArchitecturalObstacleType::ForbiddenZone; return true; }
        return false;
    }
}
