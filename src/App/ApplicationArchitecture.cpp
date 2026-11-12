#include "App/ApplicationInternal.hpp"

namespace plumbcad
{
    namespace
    {
        glm::vec3 defaultArchitecturalObstacleSize(ArchitecturalObstacleType type)
        {
            switch (type)
            {
            case ArchitecturalObstacleType::Wall: return {3.00f, 2.80f, 0.20f};
            case ArchitecturalObstacleType::Slab: return {3.00f, 0.20f, 3.00f};
            case ArchitecturalObstacleType::Shaft: return {1.00f, 3.00f, 1.00f};
            case ArchitecturalObstacleType::ForbiddenZone: return {2.00f, 2.00f, 2.00f};
            }
            return {1.0f,1.0f,1.0f};
        }
        glm::vec3 architecturalObstacleColor(
            ArchitecturalObstacleType type)
        {
            (void)type;

            // Minden építészeti akadály egységesen piros.
            // Így a csőhálózat (kék) és az akadálytér (piros)
            // már pusztán szín alapján is azonnal elkülönül.
            return {0.86f, 0.16f, 0.14f};
        }
    }

    ObjectId Application::allocateArchitecturalObjectId() const
    {
        constexpr ObjectId FirstArchitectureId = 3000000000u;
        constexpr ObjectId LastArchitectureId = 3799999999u;
        for (ObjectId id = FirstArchitectureId; id < LastArchitectureId; ++id)
        {
            if (m_network.find(id) || findSceneObject(id)) continue;
            return id;
        }
        return InvalidObjectId;
    }

    SceneObject Application::makeSceneObjectForArchitecturalObstacle(const ArchitecturalObstacle& obstacle) const
    {
        SceneObject object;
        object.id = obstacle.id;
        object.name = architecturalObstacleTypeName(obstacle.type);
        object.mesh = MeshFactory::makeBox(obstacle.sizeM);
        object.color = architecturalObstacleColor(obstacle.type);
        object.selectable = true;
        return object;
    }

    ArchitecturalObstacle* Application::findArchitecturalObstacle(ObjectId id)
    {
        for (auto& obstacle : m_architecturalObstacles) if (obstacle.id == id) return &obstacle;
        return nullptr;
    }
    const ArchitecturalObstacle* Application::findArchitecturalObstacle(ObjectId id) const
    {
        for (const auto& obstacle : m_architecturalObstacles) if (obstacle.id == id) return &obstacle;
        return nullptr;
    }

    ObjectId Application::addArchitecturalObstacle(ArchitecturalObstacleType type)
    {
        const ObjectId id = allocateArchitecturalObjectId();
        if (id == InvalidObjectId)
        {
            setStatusMessage("Nem sikerült új építészeti objektumazonosítót foglalni.");
            return InvalidObjectId;
        }
        ArchitecturalObstacle obstacle;
        obstacle.id = id;
        obstacle.type = type;
        obstacle.sizeM = defaultArchitecturalObstacleSize(type);
        obstacle.blocksRouting = true;
        SceneObject object = makeSceneObjectForArchitecturalObstacle(obstacle);
        if (type == ArchitecturalObstacleType::Slab)
            object.transform.position = {0.0f, -obstacle.sizeM.y * 0.5f, 0.0f};
        else
            object.transform.position = {0.0f, obstacle.sizeM.y * 0.5f, 0.0f};
        m_architecturalObstacles.push_back(obstacle);
        m_objects.push_back(object);
        m_renderer.uploadObject(m_objects.back());
        m_selectedId = id;
        m_routeAlternatives.clear();
        m_selectedRouteAlternative = 0;
        clearRoutePreview();
        setStatusMessage(std::string(architecturalObstacleTypeName(type)) + " hozzáadva. Mozgatás: 1-es eszköz, forgatás: 2-es eszköz; méret az Építészet panelen.");
        return id;
    }

    void Application::rebuildArchitecturalObstacleMesh(ObjectId id)
    {
        ArchitecturalObstacle* obstacle = findArchitecturalObstacle(id);
        SceneObject* object = findSceneObject(id);
        if (!obstacle || !object) return;
        object->mesh = MeshFactory::makeBox(obstacle->sizeM);
        object->transform.scale = {1.0f,1.0f,1.0f};
        m_renderer.uploadObject(*object);
    }

    bool Application::setArchitecturalObstacleSize(ObjectId id, const glm::vec3& requestedSizeM)
    {
        ArchitecturalObstacle* obstacle = findArchitecturalObstacle(id);
        if (!obstacle) return false;
        const glm::vec3 newSize{
            std::clamp(requestedSizeM.x,0.05f,100.0f),
            std::clamp(requestedSizeM.y,0.05f,100.0f),
            std::clamp(requestedSizeM.z,0.05f,100.0f)};
        obstacle->sizeM = newSize;
        rebuildArchitecturalObstacleMesh(id);
        m_routeAlternatives.clear();
        m_selectedRouteAlternative = 0;
        clearRoutePreview();
        setStatusMessage(std::string(architecturalObstacleTypeName(obstacle->type)) + " mérete módosítva.");
        return true;
    }

    bool Application::setArchitecturalObstacleBlocking(ObjectId id, bool blocksRouting)
    {
        ArchitecturalObstacle* obstacle = findArchitecturalObstacle(id);
        if (!obstacle) return false;
        obstacle->blocksRouting = blocksRouting;
        m_routeAlternatives.clear();
        m_selectedRouteAlternative = 0;
        clearRoutePreview();
        setStatusMessage(blocksRouting ? "Az építészeti objektum A* akadályként aktív." : "Az építészeti objektum A* akadálykezelése kikapcsolva.");
        return true;
    }

    bool Application::deleteArchitecturalObstacle(ObjectId id)
    {
        const ArchitecturalObstacle* obstacle = findArchitecturalObstacle(id);
        if (!obstacle) return false;
        const std::string name = architecturalObstacleTypeName(obstacle->type);
        m_architecturalObstacles.erase(std::remove_if(m_architecturalObstacles.begin(),m_architecturalObstacles.end(),[id](const auto& o){return o.id==id;}),m_architecturalObstacles.end());
        m_objects.erase(std::remove_if(m_objects.begin(),m_objects.end(),[id](const SceneObject& o){return o.id==id;}),m_objects.end());
        m_renderer.removeObject(id);
        if (m_selectedId == id) m_selectedId = InvalidObjectId;
        m_routeAlternatives.clear();
        m_selectedRouteAlternative = 0;
        clearRoutePreview();
        setStatusMessage(name + " törölve.");
        return true;
    }
}
