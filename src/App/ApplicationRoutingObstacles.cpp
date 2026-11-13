#include "App/ApplicationInternal.hpp"
#include "Routing/RouteGeometry.hpp"
#include <cmath>
#include <limits>

namespace plumbcad
{
    using namespace routing;

    bool Application::routeEndReducerIsClear(const glm::vec3 &start, const glm::vec3 &end, float diameterMm) const
    {
        const float radiusM = diameterMm * 0.0005f;
        const auto blocksReducer = [&](const RouteObstacle &obstacle) {
            return segmentIntersectsRouteObstacle(start, end, obstacle);
        };
        const auto obstacles = buildRoutingObstacles(m_routeStartId, m_routeEndId, radiusM);
        const auto endpoints = buildRoutingEndpointObstacles(m_routeStartId, m_routeEndId, radiusM);
        return std::none_of(obstacles.begin(), obstacles.end(), blocksReducer) &&
               std::none_of(endpoints.begin(), endpoints.end(), blocksReducer);
    }

    std::vector<RouteObstacle> Application::buildRoutingObstacles(ObjectId startId, ObjectId endId,
                                                                  float routedPipeRadiusM) const
    {
        std::vector<RouteObstacle> result;

        // A fizikai csőtesten való áthaladás mindig tiltott.
        // Az Útvonal panelen megadott akadálytávolság mindig érvényes.
        const float extraClearance = std::max(0.0f, m_routeClearanceM);

        routedPipeRadiusM = std::max(0.005f, routedPipeRadiusM);

        for (const SceneObject &object : m_objects)
        {
            // A kiválasztott start/end elemet itt szándékosan kihagyjuk.
            // Így a közvetlen 45°/90° könyökkel induló/érkező alternatívák
            // továbbra is létrejöhetnek.
            if (!object.selectable || object.id == startId || object.id == endId)
            {
                continue;
            }

            const ArchitecturalObstacle *architecturalObstacle = findArchitecturalObstacle(object.id);

            if (architecturalObstacle && !architecturalObstacle->blocksRouting)
            {
                continue;
            }

            const PipeElement *pipe = m_network.find(object.id);

            if (pipe && pipe->type == PipeElementType::StraightPipe && pipe->connections.size() >= 2)
            {
                // Egyenes cső: a valódi csőtengely körül építünk akadályláncot.
                // Nem az egész elforgatott objektum egyetlen nagy AABB-jét használjuk.
                const glm::vec3 p0 = connectionWorldPosition(object.id, 0);
                const glm::vec3 p1 = connectionWorldPosition(object.id, 1);
                const float length = glm::length(p1 - p0);
                if (length < 0.0001f) continue;

                const float existingPipeRadiusM =
                    std::max(0.005f, (pipeMaximumDiameterMm(*pipe) / 1000.0f) * 0.5f);

                // A két cső sugara összeadódik: így nemcsak a középvonal,
                // hanem maga az új csőtest sem lóghat bele a meglévőbe.
                const float collisionRadius = existingPipeRadiusM + routedPipeRadiusM + extraClearance + 0.01f;

                const float pieceLength = std::clamp(collisionRadius * 1.5f, 0.06f, 0.18f);

                const int pieceCount = std::max(1, static_cast<int>(std::ceil(length / pieceLength)));

                for (int piece = 0; piece < pieceCount; ++piece)
                {
                    const float t0 = static_cast<float>(piece) / static_cast<float>(pieceCount);
                    const float t1 = static_cast<float>(piece + 1) / static_cast<float>(pieceCount);

                    const glm::vec3 a = glm::mix(p0, p1, t0);
                    const glm::vec3 b = glm::mix(p0, p1, t1);
                    const glm::vec3 padding{collisionRadius};

                    result.push_back({glm::min(a, b) - padding, glm::max(a, b) + padding});
                }

                continue;
            }

            // The clearance box uses the original footprint when the display mesh has added detail.
            if (object.mesh.vertices.empty()) continue;

            glm::vec3 minP{std::numeric_limits<float>::max()};
            glm::vec3 maxP{std::numeric_limits<float>::lowest()};
            const glm::mat4 model = object.transform.matrix();

            const auto expand = [&](const glm::vec3& position)
            {
                const glm::vec3 p = glm::vec3(model * glm::vec4(position, 1.0f));
                minP = glm::min(minP, p);
                maxP = glm::max(maxP, p);
            };
            if (object.mesh.routingBoundsVertices.empty())
            {
                for (const Vertex& vertex : object.mesh.vertices) expand(vertex.position);
            }
            else
            {
                for (const auto& position : object.mesh.routingBoundsVertices) expand(position);
            }

            const glm::vec3 padding{routedPipeRadiusM + extraClearance + 0.01f};

            result.push_back({minP - padding, maxP + padding});
        }

        return result;
    }

    std::vector<RouteObstacle> Application::buildRoutingEndpointObstacles(ObjectId startId, ObjectId endId,
                                                                          float routedPipeRadiusM) const
    {
        std::vector<RouteObstacle> result;

        const float extraClearance = std::max(0.0f, m_routeClearanceM);

        routedPipeRadiusM = std::max(0.005f, routedPipeRadiusM);

        for (const auto &[id, pipe] : m_network.elements())
        {
            // A kiválasztott start/end elem saját többi végpontja ne blokkolja
            // a közvetlen könyökös indulást/érkezést.
            if (id == startId || id == endId) continue;

            const float existingPipeRadiusM = std::max(0.005f, (pipeMaximumDiameterMm(pipe) / 1000.0f) * 0.5f);

            const float endpointClearance =
                std::max(0.06f, existingPipeRadiusM + routedPipeRadiusM + extraClearance + 0.02f);

            const glm::vec3 half{endpointClearance};

            for (int pointIndex = 0; pointIndex < static_cast<int>(pipe.connections.size()); ++pointIndex)
            {
                const glm::vec3 center = connectionWorldPosition(id, pointIndex);

                result.push_back({center - half, center + half});
            }
        }

        return result;
    }

}
