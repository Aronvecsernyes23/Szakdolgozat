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
        m_endpointMarkers.clear();

        for (const auto &[id, pipe] : m_network.elements())
        {
            for (int pointIndex = 0; pointIndex < static_cast<int>(pipe.connections.size()); ++pointIndex)
            {
                SceneObject marker;
                marker.id = EndpointMarkerBaseId + id * 10u + static_cast<ObjectId>(pointIndex);
                const bool isRouteStart = id == m_routeStartId && pointIndex == m_routeStartEndpoint;
                const bool isRouteEnd = id == m_routeEndId && pointIndex == m_routeEndEndpoint;
                const bool isValidationStart =
                    id == m_validationStartId && pointIndex == m_validationStartEndpoint;
                const bool isValidationEnd = id == m_validationEndId && pointIndex == m_validationEndEndpoint;

                if (isRouteStart)
                {
                    marker.name = "Automatikus útvonal kezdőpont";
                }
                else if (isRouteEnd)
                {
                    marker.name = "Automatikus útvonal végpont";
                }
                else if (isValidationStart)
                {
                    marker.name = "Validáció START";
                }
                else if (isValidationEnd)
                {
                    marker.name = "Validáció END";
                }
                else
                {
                    marker.name = pipe.connections[pointIndex].isOpen() ? "Nyitott csővég" : "Kapcsolt csővég";
                }

                const float endpointDiameterMm = pipeEndpointDiameterMm(pipe, pointIndex);

                const float pipeRadiusM = std::max(0.005f, (endpointDiameterMm / 1000.0f) * 0.5f);
                const float ringThicknessM = std::max(0.0035f, pipeRadiusM * 0.22f);
                marker.mesh = MeshFactory::makeEndpointRing(pipeRadiusM, ringThicknessM);
                marker.transform.position = connectionWorldPosition(id, pointIndex);
                marker.transform.scale = glm::vec3{1.0f};

                glm::vec3 localDirection{0.0f, 0.0f, 1.0f};
                if (pipe.type == PipeElementType::StraightPipe)
                {
                    localDirection = pointIndex == 0 ? glm::vec3{0.0f, 0.0f, -1.0f} : glm::vec3{0.0f, 0.0f, 1.0f};
                }
                else if (isDirectionalFittingType(pipe.type))
                {
                    localDirection = fittingConnectorDirectionLocal(pipe, pointIndex);
                }

                const SceneObject *parentObject = findSceneObject(id);
                if (parentObject)
                {
                    glm::vec3 worldDirection =
                        glm::vec3(parentObject->transform.matrix() * glm::vec4(localDirection, 0.0f));
                    if (glm::length2(worldDirection) > 0.000001f)
                    {
                        setObjectRotationFromLocalDirection(marker, glm::vec3{0.0f, 0.0f, 1.0f},
                                                            glm::normalize(worldDirection));
                    }
                }

                // Endpoint színek:
                // - kijelölt elem: A=cián, B=magenta, C=narancs
                // - nem kijelölt elem: nyitott=piros, kapcsolt=zöld
                const bool isSelectedElement = id == m_selectedId;

                if (isSelectedElement)
                {
                    switch (pointIndex)
                    {
                    case 0:
                        marker.color = glm::vec3{0.10f, 0.90f, 1.00f};
                        break;
                    case 1:
                        marker.color = glm::vec3{1.00f, 0.20f, 0.85f};
                        break;
                    default:
                        marker.color = glm::vec3{1.00f, 0.58f, 0.10f};
                        break;
                    }

                    marker.transform.scale = glm::vec3{1.0f};
                }
                else
                {
                    marker.color = pipe.connections[pointIndex].isOpen() ? glm::vec3{1.00f, 0.18f, 0.12f}
                                                                         : glm::vec3{0.12f, 0.95f, 0.28f};

                    marker.transform.scale = glm::vec3{1.0f};
                }
                marker.selectable = false;
                m_endpointMarkers.push_back(marker);

                // A jelölő mesh mérete a cső aktuális átmérőjéből készül, ezért minden frissítéskor
                // újra feltöltjük. Így csőméret-váltás/skálázás után a piros/zöld kör is együtt változik.
                m_renderer.uploadObject(marker);
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
