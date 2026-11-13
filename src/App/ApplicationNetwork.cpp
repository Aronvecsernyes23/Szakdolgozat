#include "App/ApplicationInternal.hpp"

namespace plumbcad
{
    glm::vec3 Application::connectionWorldPosition(ObjectId objectId, int pointIndex) const
    {
        const PipeElement *pipe = m_network.find(objectId);
        const SceneObject *object = findSceneObject(objectId);
        if (!pipe || !object) return glm::vec3{0.0f};
        if (pointIndex < 0 || pointIndex >= static_cast<int>(pipe->connections.size()))
            return object->transform.position;

        return glm::vec3(object->transform.matrix() *
                         glm::vec4(pipe->connections[pointIndex].localPosition, 1.0f));
    }

    void Application::cleanupInvalidConnections()
    {
        // A zöld jelölés csak akkor legyen zöld, ha a kapcsolat tényleg kétoldali ÉS
        // a két csővég fizikailag is ugyanott van. Ez eltünteti a beragadt, hibás zöld jelöléseket.
        std::vector<std::pair<ObjectId, int>> toDisconnect;
        constexpr float ConnectionToleranceMeters = 0.08f;

        for (const auto &[id, pipe] : m_network.elements())
        {
            for (int pointIndex = 0; pointIndex < static_cast<int>(pipe.connections.size()); ++pointIndex)
            {
                const auto &point = pipe.connections[pointIndex];
                if (point.isOpen()) continue;

                const PipeElement *otherPipe = m_network.find(point.connectedElementId);
                bool valid = otherPipe != nullptr && point.connectedPointIndex >= 0 &&
                             point.connectedPointIndex < static_cast<int>(otherPipe->connections.size());

                if (valid)
                {
                    const auto &otherPoint = otherPipe->connections[point.connectedPointIndex];
                    valid = otherPoint.connectedElementId == id && otherPoint.connectedPointIndex == pointIndex;
                }

                if (valid)
                {
                    const glm::vec3 a = connectionWorldPosition(id, pointIndex);
                    const glm::vec3 b =
                        connectionWorldPosition(point.connectedElementId, point.connectedPointIndex);
                    valid = glm::length(a - b) <= ConnectionToleranceMeters;
                }

                if (!valid)
                {
                    toDisconnect.emplace_back(id, pointIndex);
                }
            }
        }

        for (const auto &[id, pointIndex] : toDisconnect)
        {
            m_network.disconnect(id, pointIndex);
        }
    }

    void Application::updateEndpointMarkers()
    {
        cleanupInvalidConnections();
        for (const auto& marker : m_endpointMarkers) m_renderer.removeObject(marker.object.id);
        m_endpointMarkers.clear();

        for (const auto& [id, pipe] : m_network.elements())
        {
            const auto* parent = findSceneObject(id);
            if (!parent) continue;
            const glm::mat4 parentTransform = parent->transform.matrix();

            for (int pointIndex = 0; pointIndex < static_cast<int>(pipe.connections.size()); ++pointIndex)
            {
                const bool isRouteStart = id == m_routeStartId && pointIndex == m_routeStartEndpoint;
                const bool isRouteEnd = id == m_routeEndId && pointIndex == m_routeEndEndpoint;
                const bool isValidationStart = id == m_validationStartId && pointIndex == m_validationStartEndpoint;
                const bool isValidationEnd = id == m_validationEndId && pointIndex == m_validationEndEndpoint;
                const bool isMarked = isRouteStart || isRouteEnd || isValidationStart || isValidationEnd;
                if (!pipe.connections[pointIndex].isOpen() && !isMarked) continue;

                SceneObject marker;
                marker.id = EndpointMarkerBaseId + id * 10u + static_cast<ObjectId>(pointIndex);
                marker.selectable = false;
                marker.unlit = true;

                if (isRouteStart)
                {
                    marker.name = "Automatikus útvonal kezdőpont";
                    marker.color = {0.28f, 0.58f, 1.0f};
                }
                else if (isRouteEnd)
                {
                    marker.name = "Automatikus útvonal végpont";
                    marker.color = {0.77f, 0.40f, 1.0f};
                }
                else if (isValidationStart)
                {
                    marker.name = "Validáció START";
                    marker.color = {1.0f, 0.65f, 0.18f};
                }
                else if (isValidationEnd)
                {
                    marker.name = "Validáció END";
                    marker.color = {0.20f, 0.90f, 1.0f};
                }
                else
                {
                    marker.name = "Nyitott csővég";
                    marker.color = {1.0f, 0.28f, 0.22f};
                }

                const float radius = std::max(0.005f, pipeEndpointDiameterMm(pipe, pointIndex) * 0.0005f);
                const float width = std::max(0.0035f, radius * 0.22f);
                marker.mesh = MeshFactory::makeEndpointRing(radius, width, 32);

                const glm::vec3 localDirection = isDirectionalFittingType(pipe.type)
                    ? fittingConnectorDirectionLocal(pipe, pointIndex)
                    : glm::vec3{0.0f, 0.0f, pointIndex == 0 ? -1.0f : 1.0f};
                const glm::vec3 worldDirection = glm::vec3(parentTransform * glm::vec4(localDirection, 0.0f));
                const glm::vec3 outward = glm::length2(worldDirection) > 0.000001f
                    ? glm::normalize(worldDirection) : localDirection;

                // Offset the ring from the socket face so the two surfaces never share depth.
                const float offset = std::clamp(radius * 0.045f, 0.0008f, 0.003f);
                marker.transform.position = connectionWorldPosition(id, pointIndex) + outward * offset;
                setObjectRotationFromLocalDirection(marker, {0.0f, 0.0f, 1.0f}, outward);

                m_renderer.uploadObject(marker);
                m_endpointMarkers.push_back({std::move(marker), id, pointIndex, isMarked});
            }
        }
    }

    void Application::appendEndpointMarkers(std::vector<SceneObject>& renderObjects) const
    {
        for (const auto& marker : m_endpointMarkers)
        {
            auto& object = renderObjects.emplace_back(marker.object);
            if (marker.hasRole || marker.pipeId != m_selectedId) continue;

            switch (marker.pointIndex)
            {
            case 0: object.color = {0.10f, 0.90f, 1.00f}; break;
            case 1: object.color = {1.00f, 0.20f, 0.85f}; break;
            default: object.color = {1.00f, 0.58f, 0.10f}; break;
            }
        }
    }

    bool Application::hasConnectedEndpoint(ObjectId objectId) const
    {
        const PipeElement *pipe = m_network.find(objectId);
        if (!pipe) return false;
        for (const auto &point : pipe->connections)
        {
            if (!point.isOpen()) return true;
        }
        return false;
    }

    int Application::firstConnectedEndpoint(ObjectId objectId) const
    {
        const PipeElement *pipe = m_network.find(objectId);
        if (!pipe) return -1;
        for (int i = 0; i < static_cast<int>(pipe->connections.size()); ++i)
        {
            if (!pipe->connections[i].isOpen()) return i;
        }
        return -1;
    }

    int Application::connectedEndpointCount(ObjectId objectId) const
    {
        const PipeElement *pipe = m_network.find(objectId);
        if (!pipe) return 0;

        int count = 0;
        for (const auto &point : pipe->connections)
        {
            if (!point.isOpen()) ++count;
        }
        return count;
    }

    std::vector<ObjectId> Application::connectedComponent(ObjectId objectId) const
    {
        std::vector<ObjectId> result;
        if (objectId == InvalidObjectId || !m_network.find(objectId)) return result;

        std::vector<ObjectId> stack{objectId};
        while (!stack.empty())
        {
            const ObjectId current = stack.back();
            stack.pop_back();

            if (std::find(result.begin(), result.end(), current) != result.end()) continue;
            result.push_back(current);

            const PipeElement *pipe = m_network.find(current);
            if (!pipe) continue;
            for (const auto &point : pipe->connections)
            {
                if (!point.isOpen() &&
                    std::find(result.begin(), result.end(), point.connectedElementId) == result.end())
                {
                    stack.push_back(point.connectedElementId);
                }
            }
        }
        return result;
    }

    void Application::translateObjects(const std::vector<ObjectId> &objectIds, const glm::vec3 &delta)
    {
        for (ObjectId id : objectIds)
        {
            SceneObject *object = findSceneObject(id);
            if (object) object->transform.position += delta;
        }
    }

    void Application::applyWorldMatrixToObject(ObjectId objectId, const glm::mat4 &matrix)
    {
        SceneObject *object = findSceneObject(objectId);
        if (!object) return;

        object->transform.position = glm::vec3(matrix[3]);

        glm::vec3 scale{glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])),
                        glm::length(glm::vec3(matrix[2]))};
        if (scale.x < 0.000001f) scale.x = 1.0f;
        if (scale.y < 0.000001f) scale.y = 1.0f;
        if (scale.z < 0.000001f) scale.z = 1.0f;

        glm::mat4 rotationOnly = matrix;
        rotationOnly[0] /= scale.x;
        rotationOnly[1] /= scale.y;
        rotationOnly[2] /= scale.z;
        rotationOnly[3] = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        glm::extractEulerAngleXYZ(rotationOnly, x, y, z);

        object->transform.rotationEuler = {x, y, z};
        object->transform.scale = scale;
    }

}
