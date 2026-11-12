#include "Routing/RouteGeometry.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace plumbcad::routing
{
    namespace
    {
        bool collinearForward(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c)
        {
            const glm::vec3 ab = b - a;
            const glm::vec3 bc = c - b;

            if (glm::length2(ab) < 0.0000001f || glm::length2(bc) < 0.0000001f)
            {
                return true;
            }

            return glm::dot(glm::normalize(ab), glm::normalize(bc)) > 0.9999f;
        }

        void appendSegment(std::vector<RouteLineSegment> &result, const glm::vec3 &a, const glm::vec3 &b)
        {
            if (glm::length2(b - a) < 0.0000001f) return;

            result.push_back({a, b});
        }
    }

    float routeDirectionAngleRadians(const glm::vec3 &incoming, const glm::vec3 &outgoing)
    {
        if (glm::length2(incoming) < 0.000001f || glm::length2(outgoing) < 0.000001f)
        {
            return 0.0f;
        }

        return std::acos(std::clamp(glm::dot(glm::normalize(incoming), glm::normalize(outgoing)), -1.0f, 1.0f));
    }

    glm::vec3 exactElbowEulerForDirections(const glm::vec3 &incoming, const glm::vec3 &outgoing)
    {
        glm::vec3 a = glm::normalize(incoming);

        glm::vec3 b = glm::normalize(outgoing);

        glm::vec3 turnNormal = glm::cross(a, b);

        if (glm::length2(turnNormal) < 0.0000001f)
        {
            return discreteRouteEulerForDirection(a);
        }

        turnNormal = glm::normalize(turnNormal);

        const glm::vec3 centerDirection = glm::normalize(glm::cross(turnNormal, a));

        // Lokális könyök:
        // +Z = belépő haladási irány
        // -Y = fordulási sík normálja
        // -X = a kör középpontja felé mutató sugár a kezdőponton.
        const glm::vec3 xWorld = -centerDirection;

        const glm::vec3 yWorld = -turnNormal;

        const glm::vec3 zWorld = a;

        glm::mat4 rotation{1.0f};
        rotation[0] = glm::vec4(xWorld, 0.0f);
        rotation[1] = glm::vec4(yWorld, 0.0f);
        rotation[2] = glm::vec4(zWorld, 0.0f);

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        glm::extractEulerAngleXYZ(rotation, x, y, z);

        return glm::vec3{x, y, z};
    }

    std::vector<glm::vec3> simplifyRoutePoints(const std::vector<glm::vec3> &input)
    {
        if (input.size() <= 2) return input;

        std::vector<glm::vec3> result;
        result.reserve(input.size());
        result.push_back(input.front());

        for (std::size_t i = 1; i + 1 < input.size(); ++i)
        {
            if (!collinearForward(result.back(), input[i], input[i + 1]))
            {
                result.push_back(input[i]);
            }
        }

        result.push_back(input.back());
        return result;
    }

    bool sameRoute(const std::vector<glm::vec3> &a, const std::vector<glm::vec3> &b)
    {
        if (a.size() != b.size()) return false;

        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (glm::length(a[i] - b[i]) > 0.02f) return false;
        }

        return true;
    }

    bool segmentIntersectsRouteObstacle(const glm::vec3 &a, const glm::vec3 &b, const RouteObstacle &obstacle)
    {
        const glm::vec3 d = b - a;
        float tMin = 0.0f;
        float tMax = 1.0f;

        for (int axis = 0; axis < 3; ++axis)
        {
            const float origin = a[axis];
            const float delta = d[axis];
            const float minValue = obstacle.minPoint[axis];
            const float maxValue = obstacle.maxPoint[axis];

            if (std::abs(delta) < 0.000001f)
            {
                if (origin < minValue || origin > maxValue)
                {
                    return false;
                }

                continue;
            }

            float t1 = (minValue - origin) / delta;
            float t2 = (maxValue - origin) / delta;

            if (t1 > t2) std::swap(t1, t2);

            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);

            if (tMin > tMax) return false;
        }

        return true;
    }

    bool routePointInsideObstacle(const glm::vec3 &point, const RouteObstacle &obstacle, float toleranceM)
    {
        return point.x >= obstacle.minPoint.x - toleranceM && point.x <= obstacle.maxPoint.x + toleranceM &&
               point.y >= obstacle.minPoint.y - toleranceM && point.y <= obstacle.maxPoint.y + toleranceM &&
               point.z >= obstacle.minPoint.z - toleranceM && point.z <= obstacle.maxPoint.z + toleranceM;
    }

    bool routeHasCorrectEndpointDirections(const std::vector<glm::vec3> &points, const glm::vec3 &startOut,
                                           const glm::vec3 &endOut)
    {
        if (points.size() < 2) return false;

        const glm::vec3 firstDelta = points[1] - points[0];

        const glm::vec3 lastDelta = points.back() - points[points.size() - 2];

        if (glm::length2(firstDelta) < 0.000001f || glm::length2(lastDelta) < 0.000001f)
        {
            return false;
        }

        const glm::vec3 firstDir = glm::normalize(firstDelta);

        const glm::vec3 lastDir = glm::normalize(lastDelta);

        return glm::dot(firstDir, glm::normalize(startOut)) >= 0.995f &&
               glm::dot(lastDir, -glm::normalize(endOut)) >= 0.995f;
    }

    RouteBendKind routeBendKind(const glm::vec3 &incoming, const glm::vec3 &outgoing)
    {
        if (glm::length2(incoming) < 0.000001f || glm::length2(outgoing) < 0.000001f)
        {
            return RouteBendKind::Invalid;
        }

        const glm::vec3 a = glm::normalize(incoming);

        const glm::vec3 b = glm::normalize(outgoing);

        const float dotValue = std::clamp(glm::dot(a, b), -1.0f, 1.0f);

        if (dotValue > 0.999f) return RouteBendKind::None;

        constexpr float SqrtHalf = 0.70710678118f;

        if (std::abs(dotValue - SqrtHalf) < 0.025f) return RouteBendKind::Deg45;

        if (std::abs(dotValue) < 0.025f) return RouteBendKind::Deg90;

        // Az A* fizikai útvonala kizárólag a katalógusban már létező
        // 45° és 90° könyököket használhatja.
        return RouteBendKind::Invalid;
    }

    float routeBendAngleRadians(RouteBendKind kind)
    {
        if (kind == RouteBendKind::Deg45) return 0.78539816339f;

        if (kind == RouteBendKind::Deg90) return 1.57079632679f;

        return 0.0f;
    }

    float routeEffectiveBendRadius(RouteBendKind kind, float referenceBendRadius)
    {
        referenceBendRadius = std::max(referenceBendRadius, 0.02f);

        if (kind == RouteBendKind::Deg90)
        {
            return referenceBendRadius * 0.50f;
        }

        return referenceBendRadius;
    }

    float routeCornerTangentDistance(const std::vector<glm::vec3> &points, std::size_t cornerIndex,
                                     float bendRadius)
    {
        if (cornerIndex == 0 || cornerIndex + 1 >= points.size())
        {
            return 0.0f;
        }

        const RouteBendKind kind = routeBendKind(points[cornerIndex] - points[cornerIndex - 1],
                                                 points[cornerIndex + 1] - points[cornerIndex]);

        if (kind != RouteBendKind::Deg45 && kind != RouteBendKind::Deg90)
        {
            return 0.0f;
        }

        const float angle = routeBendAngleRadians(kind);

        return routeEffectiveBendRadius(kind, bendRadius) * std::tan(angle * 0.5f);
    }

    void countRouteBends(const std::vector<glm::vec3> &points, int &bend45Count, int &bend90Count)
    {
        bend45Count = 0;
        bend90Count = 0;

        for (std::size_t i = 1; i + 1 < points.size(); ++i)
        {
            const RouteBendKind kind = routeBendKind(points[i] - points[i - 1], points[i + 1] - points[i]);

            if (kind == RouteBendKind::Deg45)
                ++bend45Count;
            else if (kind == RouteBendKind::Deg90)
                ++bend90Count;
        }
    }

    glm::vec3 discreteRouteEulerForDirection(const glm::vec3 &direction)
    {
        const glm::vec3 wanted = glm::normalize(direction);

        constexpr float Pi = 3.14159265359f;
        constexpr float QuarterPi = 0.78539816339f;

        constexpr std::array<float, 8> Angles{{-Pi, -3.0f * QuarterPi, -2.0f * QuarterPi, -QuarterPi, 0.0f,
                                               QuarterPi, 2.0f * QuarterPi, 3.0f * QuarterPi}};

        float bestScore = -1000.0f;
        glm::vec3 best{0.0f};

        for (float rx : Angles)
        {
            for (float ry : Angles)
            {
                for (float rz : Angles)
                {
                    glm::mat4 rotation{1.0f};
                    rotation = glm::rotate(rotation, rx, glm::vec3{1.0f, 0.0f, 0.0f});
                    rotation = glm::rotate(rotation, ry, glm::vec3{0.0f, 1.0f, 0.0f});
                    rotation = glm::rotate(rotation, rz, glm::vec3{0.0f, 0.0f, 1.0f});

                    const glm::vec3 worldZ =
                        glm::normalize(glm::vec3(rotation * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}));

                    const float score = glm::dot(worldZ, wanted);

                    if (score > bestScore)
                    {
                        bestScore = score;
                        best = glm::vec3{rx, ry, rz};
                    }
                }
            }
        }

        return best;
    }

    bool buildRouteBuildPlan(const std::vector<glm::vec3> &points, float bendRadius,
                             std::vector<RouteBuildPrimitive> &outPrimitives)
    {
        outPrimitives.clear();

        if (points.size() < 2) return false;

        // A teljes fizikai útvonal minden irányváltása kizárólag
        // valódi 45° vagy 90° könyök lehet.
        for (std::size_t cornerIndex = 1; cornerIndex + 1 < points.size(); ++cornerIndex)
        {
            const RouteBendKind kind = routeBendKind(points[cornerIndex] - points[cornerIndex - 1],
                                                     points[cornerIndex + 1] - points[cornerIndex]);

            if (kind != RouteBendKind::Deg45 && kind != RouteBendKind::Deg90)
            {
                return false;
            }
        }

        for (std::size_t segment = 0; segment + 1 < points.size(); ++segment)
        {
            const glm::vec3 rawDelta = points[segment + 1] - points[segment];

            const float rawLength = glm::length(rawDelta);

            if (rawLength < 0.0001f) return false;

            const glm::vec3 direction = rawDelta / rawLength;

            const float startTrim = segment > 0 ? routeCornerTangentDistance(points, segment, bendRadius) : 0.0f;

            const float endTrim = segment + 1 < points.size() - 1
                                      ? routeCornerTangentDistance(points, segment + 1, bendRadius)
                                      : 0.0f;

            const float remaining = rawLength - startTrim - endTrim;

            const bool directStart = segment == 0 && endTrim > 0.0f && std::abs(remaining) <= 0.015f;

            const bool directEnd =
                segment + 1 == points.size() - 1 && startTrim > 0.0f && std::abs(remaining) <= 0.015f;

            // Két szabványos könyök közvetlenül egymás után is állhat:
            // a közös egyenes tangenciaszakasz ilyenkor pontosan 0 hosszú.
            const bool directBetweenBends = segment > 0 && segment + 1 < points.size() - 1 && startTrim > 0.0f &&
                                            endTrim > 0.0f && std::abs(remaining) <= 0.015f;

            if (!directStart && !directEnd && !directBetweenBends)
            {
                if (remaining < 0.039f) return false;

                RouteBuildPrimitive straight;
                straight.kind = RouteBuildPrimitiveKind::Straight;
                straight.startWorld = points[segment] + direction * startTrim;
                straight.endWorld = points[segment + 1] - direction * endTrim;
                straight.incomingDirection = direction;
                straight.outgoingDirection = direction;
                straight.cornerWorld = (straight.startWorld + straight.endWorld) * 0.5f;
                straight.angleRadians = 0.0f;

                outPrimitives.push_back(straight);
            }

            if (segment + 1 < points.size() - 1)
            {
                const glm::vec3 corner = points[segment + 1];

                const glm::vec3 outgoing = glm::normalize(points[segment + 2] - points[segment + 1]);

                const RouteBendKind kind = routeBendKind(direction, outgoing);

                if (kind != RouteBendKind::Deg45 && kind != RouteBendKind::Deg90)
                {
                    return false;
                }

                const float angle = routeBendAngleRadians(kind);

                const float primitiveBendRadius = routeEffectiveBendRadius(kind, bendRadius);

                const float tangentDistance = primitiveBendRadius * std::tan(angle * 0.5f);

                RouteBuildPrimitive elbow;
                elbow.kind = kind == RouteBendKind::Deg45 ? RouteBuildPrimitiveKind::Elbow45
                                                          : RouteBuildPrimitiveKind::Elbow90;
                elbow.startWorld = corner - direction * tangentDistance;
                elbow.endWorld = corner + outgoing * tangentDistance;
                elbow.incomingDirection = direction;
                elbow.outgoingDirection = outgoing;
                elbow.cornerWorld = corner;
                elbow.angleRadians = angle;
                elbow.bendRadiusM = primitiveBendRadius;

                outPrimitives.push_back(elbow);
            }
        }

        return !outPrimitives.empty();
    }

    std::vector<RouteLineSegment> buildRouteBuildPlanCenterlineSegments(
        const std::vector<RouteBuildPrimitive> &primitives, float bendRadius)
    {
        std::vector<RouteLineSegment> result;

        for (const RouteBuildPrimitive &primitive : primitives)
        {
            if (primitive.kind == RouteBuildPrimitiveKind::Straight)
            {
                appendSegment(result, primitive.startWorld, primitive.endWorld);

                continue;
            }

            glm::vec3 normal = glm::cross(primitive.incomingDirection, primitive.outgoingDirection);

            if (glm::length2(normal) < 0.000001f)
            {
                continue;
            }

            normal = glm::normalize(normal);

            const glm::vec3 centerDirection = glm::normalize(glm::cross(normal, primitive.incomingDirection));

            const float primitiveBendRadius = primitive.bendRadiusM > 0.0f ? primitive.bendRadiusM : bendRadius;

            const glm::vec3 center = primitive.startWorld + centerDirection * primitiveBendRadius;

            const glm::vec3 radialStart = primitive.startWorld - center;

            const int samples = primitive.kind == RouteBuildPrimitiveKind::Elbow45 ? 6 : 10;

            glm::vec3 previous = primitive.startWorld;

            for (int sample = 1; sample <= samples; ++sample)
            {
                const float t = static_cast<float>(sample) / static_cast<float>(samples);

                const glm::mat4 rotation = glm::rotate(glm::mat4{1.0f}, primitive.angleRadians * t, normal);

                glm::vec3 current = center + glm::vec3(rotation * glm::vec4(radialStart, 0.0f));

                if (sample == samples)
                {
                    // Important: use the exact tangent point stored by SEARCH,
                    // not a re-derived point.
                    current = primitive.endWorld;
                }

                appendSegment(result, previous, current);

                previous = current;
            }
        }

        return result;
    }

    bool routeBuildPlanHitsObstacles(const std::vector<RouteBuildPrimitive> &primitives, float bendRadius,
                                     const std::vector<RouteObstacle> &obstacles)
    {
        if (primitives.empty()) return false;

        const std::vector<RouteLineSegment> geometry =
            buildRouteBuildPlanCenterlineSegments(primitives, bendRadius);

        if (geometry.empty()) return false;

        const glm::vec3 exactStart = primitives.front().startWorld;

        const glm::vec3 exactEnd = primitives.back().endWorld;

        for (const RouteObstacle &obstacle : obstacles)
        {
            const bool startInside = routePointInsideObstacle(exactStart, obstacle);

            const bool endInside = routePointInsideObstacle(exactEnd, obstacle);

            bool hasExitedStartObstacle = !startInside;

            // A végponti obstacle-ba csak az útvonal legvégén szabad belépni.
            // Meghatározzuk az első olyan szegmenst, amelynek vége már benne van.
            std::size_t endEntrySegment = geometry.size();

            if (endInside)
            {
                for (std::size_t reverseIndex = geometry.size(); reverseIndex > 0; --reverseIndex)
                {
                    const std::size_t index = reverseIndex - 1;

                    if (!routePointInsideObstacle(geometry[index].a, obstacle))
                    {
                        endEntrySegment = index;
                        break;
                    }

                    endEntrySegment = index;
                }
            }

            for (std::size_t segmentIndex = 0; segmentIndex < geometry.size(); ++segmentIndex)
            {
                const RouteLineSegment &segment = geometry[segmentIndex];

                const bool aInside = routePointInsideObstacle(segment.a, obstacle);

                const bool bInside = routePointInsideObstacle(segment.b, obstacle);

                if (startInside && !hasExitedStartObstacle)
                {
                    // Az exact START pontot tartalmazó obstacle-ból a saját
                    // csatlakozási irány mentén közvetlenül kiléphetünk.
                    if (!bInside)
                    {
                        hasExitedStartObstacle = true;
                    }

                    continue;
                }

                if (endInside && segmentIndex >= endEntrySegment)
                {
                    // Ugyanez visszafelé az exact END pontnál:
                    // a végső fizikai szakasz beléphet a célcsatlakozás
                    // körüli kibővített obstacle-ba.
                    continue;
                }

                if (segmentIntersectsRouteObstacle(segment.a, segment.b, obstacle))
                {
                    return true;
                }

                // Ha egy start-obstacle-ból már kiléptünk, később nem
                // léphetünk vissza ugyanabba.
                if (startInside && hasExitedStartObstacle && (aInside || bInside))
                {
                    return true;
                }
            }
        }

        return false;
    }

    float routeBuildPlanPhysicalLength(const std::vector<RouteBuildPrimitive> &primitives, float bendRadius)
    {
        float total = 0.0f;

        for (const RouteBuildPrimitive &primitive : primitives)
        {
            if (primitive.kind == RouteBuildPrimitiveKind::Straight)
            {
                total += glm::length(primitive.endWorld - primitive.startWorld);
            }
            else
            {
                total +=
                    (primitive.bendRadiusM > 0.0f ? primitive.bendRadiusM : bendRadius) * primitive.angleRadians;
            }
        }

        return total;
    }

    bool routeCanBeBuilt(const std::vector<glm::vec3> &points, float bendRadius)
    {
        std::vector<RouteBuildPrimitive> primitives;

        return buildRouteBuildPlan(points, bendRadius, primitives);
    }

    bool routeGeometryHitsObstacles(const std::vector<glm::vec3> &points, float bendRadius,
                                    const std::vector<RouteObstacle> &obstacles)
    {
        std::vector<RouteBuildPrimitive> primitives;

        if (!buildRouteBuildPlan(points, bendRadius, primitives))
        {
            return true;
        }

        return routeBuildPlanHitsObstacles(primitives, bendRadius, obstacles);
    }

}
