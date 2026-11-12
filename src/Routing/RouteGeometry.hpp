#pragma once

#include "Routing/AStarRouter.hpp"

#include <glm/glm.hpp>
#include <cstddef>
#include <vector>

namespace plumbcad::routing
{
    enum class RouteBendKind
    {
        None,
        Deg45,
        Deg90,
        Invalid
    };

    struct RouteLineSegment
    {
        glm::vec3 a{0.0f};
        glm::vec3 b{0.0f};
    };

    float routeDirectionAngleRadians(const glm::vec3 &incoming, const glm::vec3 &outgoing);

    glm::vec3 exactElbowEulerForDirections(const glm::vec3 &incoming, const glm::vec3 &outgoing);

    std::vector<glm::vec3> simplifyRoutePoints(const std::vector<glm::vec3> &input);

    bool sameRoute(const std::vector<glm::vec3> &a, const std::vector<glm::vec3> &b);

    bool segmentIntersectsRouteObstacle(const glm::vec3 &a, const glm::vec3 &b, const RouteObstacle &obstacle);

    bool routePointInsideObstacle(const glm::vec3 &point, const RouteObstacle &obstacle,
                                  float toleranceM = 0.0001f);

    bool routeHasCorrectEndpointDirections(const std::vector<glm::vec3> &points, const glm::vec3 &startOut,
                                           const glm::vec3 &endOut);

    RouteBendKind routeBendKind(const glm::vec3 &incoming, const glm::vec3 &outgoing);

    float routeBendAngleRadians(RouteBendKind kind);

    float routeEffectiveBendRadius(RouteBendKind kind, float referenceBendRadius);

    float routeCornerTangentDistance(const std::vector<glm::vec3> &points, std::size_t cornerIndex,
                                     float bendRadius);

    void countRouteBends(const std::vector<glm::vec3> &points, int &bend45Count, int &bend90Count);

    glm::vec3 discreteRouteEulerForDirection(const glm::vec3 &direction);

    bool buildRouteBuildPlan(const std::vector<glm::vec3> &points, float bendRadius,
                             std::vector<RouteBuildPrimitive> &outPrimitives);

    bool routeBuildPlanHitsObstacles(const std::vector<RouteBuildPrimitive> &primitives, float bendRadius,
                                     const std::vector<RouteObstacle> &obstacles);

    std::vector<RouteLineSegment> buildRouteBuildPlanCenterlineSegments(
        const std::vector<RouteBuildPrimitive> &primitives, float bendRadius);

    float routeBuildPlanPhysicalLength(const std::vector<RouteBuildPrimitive> &primitives, float bendRadius);

    bool routeCanBeBuilt(const std::vector<glm::vec3> &points, float bendRadius);

    bool routeGeometryHitsObstacles(const std::vector<glm::vec3> &points, float bendRadius,
                                    const std::vector<RouteObstacle> &obstacles);

}
