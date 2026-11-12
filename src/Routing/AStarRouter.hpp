#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace plumbcad
{
    struct RouteObstacle
    {
        glm::vec3 minPoint{0.0f};
        glm::vec3 maxPoint{0.0f};
    };

    // 0..17:
    // 0..5   = +/-X, +/-Y, +/-Z
    // 6..17  = XY, XZ, YZ síkbeli 45° irányok
    constexpr int RoutingDirectionCount = 18;

    [[nodiscard]] glm::vec3 routingDirectionVector(int index);
    [[nodiscard]] int routingDirectionIndex(const glm::vec3& direction);

    struct RouteRequest
    {
        glm::vec3 startWorld{0.0f};
        glm::vec3 endWorld{0.0f};

        // Ez MOST valóban a keresési rács lépése.
        // Nem finomítja automatikusan 5 mm környékére a végpont miatt.
        float maxGridStepM{0.35f};
        float boundsPaddingM{1.0f};

        float bend45PenaltyM{0.05f};
        float bend90PenaltyM{0.10f};

        // A folytonos, pontos célcsatlakozóban használt minimális szakasz.
        float minimumSegmentM{0.04f};

        // Háromszakaszos pontos célcsatlakozót csak a cél közelében próbálunk,
        // így a keresés gyors marad.
        float goalConnectorRangeM{1.50f};

        int maxExpandedNodes{60000};
        int directionPreference{0};

        // A start portál után kötelező első irány.
        // A célba érkező legutolsó irány.
        int startDirectionIndex{-1};
        int endDirectionIndex{-1};

        std::vector<RouteObstacle> obstacles;
    };

    struct RouteResult
    {
        bool success{false};
        std::vector<glm::vec3> worldPoints;
        int expandedNodes{0};
        std::string message;
    };

    enum class RouteBuildPrimitiveKind
    {
        Straight,
        Elbow45,
        Elbow90
    };

    struct RouteBuildPrimitive
    {
        RouteBuildPrimitiveKind kind{RouteBuildPrimitiveKind::Straight};

        // EXACT geometry accepted during search.
        // Build and preview both use these same points.
        glm::vec3 startWorld{0.0f};
        glm::vec3 endWorld{0.0f};

        // Used by elbows. For straight primitives incoming/outgoing are the
        // same axis direction.
        glm::vec3 incomingDirection{0.0f, 0.0f, 1.0f};
        glm::vec3 outgoingDirection{0.0f, 0.0f, 1.0f};

        glm::vec3 cornerWorld{0.0f};
        float angleRadians{0.0f};
        float bendRadiusM{0.0f};
    };

    struct RouteCatalogPlan
    {
        int pipeType{-1};
        int pipeSize{-1};
        int reducerType{-1};
        int reducerInletSize{-1};
        int reducerOutletSize{-1};

        bool operator==(const RouteCatalogPlan&) const = default;
    };

    struct RouteAlternative
    {
        std::vector<glm::vec3> worldPoints;

        // The definitive construction plan generated and validated during
        // route search. The final-build step must not derive a second,
        // slightly different tangent geometry from worldPoints.
        std::vector<RouteBuildPrimitive> buildPrimitives;
        RouteCatalogPlan catalogPlan;
        float totalLengthM{0.0f};
        int bend45Count{0};
        int bend90Count{0};
        bool startsWithBend{false};
        bool endsWithBend{false};

        glm::vec3 startEndpointWorld{0.0f};
        glm::vec3 endEndpointWorld{0.0f};
        glm::vec3 startOutwardDirection{0.0f, 0.0f, 1.0f};
        glm::vec3 endOutwardDirection{0.0f, 0.0f, 1.0f};
        glm::vec3 mainRouteEndWorld{0.0f};

        int segmentCount{0};
    };

    class AStarRouter
    {
    public:
        [[nodiscard]] RouteResult findRoute(const RouteRequest& request) const;
    };
}
