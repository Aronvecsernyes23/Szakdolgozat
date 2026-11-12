#include "Routing/AStarRouter.hpp"

#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
#include <vector>

namespace plumbcad
{
    namespace
    {
        constexpr float SqrtHalf = 0.70710678118f;
        constexpr float SqrtTwo = 1.41421356237f;

        struct GridDirection
        {
            int x;
            int y;
            int z;
            float lengthFactor;
        };

        constexpr std::array<GridDirection, RoutingDirectionCount> Directions{{{1, 0, 0, 1.0f},
                                                                               {-1, 0, 0, 1.0f},
                                                                               {0, 1, 0, 1.0f},
                                                                               {0, -1, 0, 1.0f},
                                                                               {0, 0, 1, 1.0f},
                                                                               {0, 0, -1, 1.0f},

                                                                               {1, 1, 0, SqrtTwo},
                                                                               {1, -1, 0, SqrtTwo},
                                                                               {-1, 1, 0, SqrtTwo},
                                                                               {-1, -1, 0, SqrtTwo},

                                                                               {1, 0, 1, SqrtTwo},
                                                                               {1, 0, -1, SqrtTwo},
                                                                               {-1, 0, 1, SqrtTwo},
                                                                               {-1, 0, -1, SqrtTwo},

                                                                               {0, 1, 1, SqrtTwo},
                                                                               {0, 1, -1, SqrtTwo},
                                                                               {0, -1, 1, SqrtTwo},
                                                                               {0, -1, -1, SqrtTwo}}};

        struct GridCoord
        {
            int x{0};
            int y{0};
            int z{0};

            bool operator==(const GridCoord &other) const { return x == other.x && y == other.y && z == other.z; }
        };

        struct State
        {
            GridCoord coord{};
            int direction{-1};

            bool operator==(const State &other) const
            {
                return coord == other.coord && direction == other.direction;
            }
        };

        struct StateHash
        {
            std::size_t operator()(const State &state) const noexcept
            {
                std::size_t h = static_cast<std::size_t>(static_cast<unsigned int>(state.coord.x)) * 73856093u;
                h ^= static_cast<std::size_t>(static_cast<unsigned int>(state.coord.y)) * 19349663u;
                h ^= static_cast<std::size_t>(static_cast<unsigned int>(state.coord.z)) * 83492791u;
                h ^= static_cast<std::size_t>(state.direction + 2) * 2654435761u;
                return h;
            }
        };

        struct Record
        {
            float g{std::numeric_limits<float>::max()};
            State parent{};
            bool hasParent{false};
            bool closed{false};
        };

        struct QueueEntry
        {
            State state{};
            float f{0.0f};
            int insertionOrder{0};

            bool operator<(const QueueEntry &other) const
            {
                if (std::abs(f - other.f) > 0.000001f) return f > other.f;
                return insertionOrder > other.insertionOrder;
            }
        };

        enum class TurnKind
        {
            Straight,
            Deg45,
            Deg90,
            Invalid
        };

        TurnKind classifyTurn(int fromDirection, int toDirection)
        {
            if (toDirection < 0) return TurnKind::Invalid;

            if (fromDirection < 0) return TurnKind::Straight;

            const glm::vec3 a = routingDirectionVector(fromDirection);
            const glm::vec3 b = routingDirectionVector(toDirection);

            const float dotValue = std::clamp(glm::dot(a, b), -1.0f, 1.0f);

            if (dotValue > 0.999f) return TurnKind::Straight;

            if (std::abs(dotValue - SqrtHalf) < 0.002f) return TurnKind::Deg45;

            if (std::abs(dotValue) < 0.002f) return TurnKind::Deg90;

            return TurnKind::Invalid;
        }

        float turnPenalty(TurnKind kind, const RouteRequest &request)
        {
            if (kind == TurnKind::Deg45) return request.bend45PenaltyM;

            if (kind == TurnKind::Deg90) return request.bend90PenaltyM;

            return 0.0f;
        }

        glm::vec3 worldFromGrid(const glm::vec3 &start, float step, const GridCoord &c)
        {
            return start + glm::vec3{static_cast<float>(c.x) * step, static_cast<float>(c.y) * step,
                                     static_cast<float>(c.z) * step};
        }

        bool insideObstacle(const glm::vec3 &p, const std::vector<RouteObstacle> &obstacles)
        {
            for (const RouteObstacle &obstacle : obstacles)
            {
                if (p.x >= obstacle.minPoint.x && p.x <= obstacle.maxPoint.x && p.y >= obstacle.minPoint.y &&
                    p.y <= obstacle.maxPoint.y && p.z >= obstacle.minPoint.z && p.z <= obstacle.maxPoint.z)
                {
                    return true;
                }
            }

            return false;
        }

        bool segmentIntersectsAabb(const glm::vec3 &a, const glm::vec3 &b, const RouteObstacle &obstacle)
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

        bool segmentHitsObstacle(const glm::vec3 &a, const glm::vec3 &b,
                                 const std::vector<RouteObstacle> &obstacles)
        {
            for (const RouteObstacle &obstacle : obstacles)
            {
                if (segmentIntersectsAabb(a, b, obstacle))
                {
                    return true;
                }
            }

            return false;
        }

        float heuristic(const glm::vec3 &world, const glm::vec3 &goal)
        {
            return glm::length(goal - world);
        }

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

        std::vector<glm::vec3> simplify(const std::vector<glm::vec3> &points)
        {
            if (points.size() <= 2) return points;

            std::vector<glm::vec3> result;
            result.reserve(points.size());
            result.push_back(points.front());

            for (std::size_t i = 1; i + 1 < points.size(); ++i)
            {
                if (!collinearForward(result.back(), points[i], points[i + 1]))
                {
                    result.push_back(points[i]);
                }
            }

            result.push_back(points.back());
            return result;
        }

        void appendUnique(std::vector<glm::vec3> &points, const glm::vec3 &point)
        {
            if (points.empty() || glm::length(points.back() - point) > 0.001f)
            {
                points.push_back(point);
            }
        }

        bool positiveLength(float value, float minimum)
        {
            return value >= minimum - 0.001f;
        }

        bool solveTwoDirections(const glm::vec3 &delta, const glm::vec3 &u, const glm::vec3 &v, float &a, float &b)
        {
            const float c = glm::dot(u, v);

            const float determinant = 1.0f - c * c;

            if (std::abs(determinant) < 0.0001f) return false;

            const float du = glm::dot(delta, u);

            const float dv = glm::dot(delta, v);

            a = (du - c * dv) / determinant;

            b = (dv - c * du) / determinant;

            const glm::vec3 residual = delta - u * a - v * b;

            return glm::length(residual) < 0.003f;
        }

        bool solveThreeDirections(const glm::vec3 &delta, const glm::vec3 &u, const glm::vec3 &v,
                                  const glm::vec3 &w, float &a, float &b, float &c)
        {
            const float determinant = glm::dot(u, glm::cross(v, w));

            if (std::abs(determinant) < 0.0001f) return false;

            a = glm::dot(delta, glm::cross(v, w)) / determinant;

            b = glm::dot(u, glm::cross(delta, w)) / determinant;

            c = glm::dot(u, glm::cross(v, delta)) / determinant;

            const glm::vec3 residual = delta - u * a - v * b - w * c;

            return glm::length(residual) < 0.004f;
        }

        struct GoalConnector
        {
            bool success{false};
            std::vector<glm::vec3> points;
            float cost{0.0f};
        };

        GoalConnector tryGoalConnector(const glm::vec3 &currentWorld, int currentHeading,
                                       const RouteRequest &request)
        {
            GoalConnector best;

            if (request.endDirectionIndex < 0 || request.endDirectionIndex >= RoutingDirectionCount)
            {
                return best;
            }

            const glm::vec3 goal = request.endWorld;

            const glm::vec3 finalDirection = routingDirectionVector(request.endDirectionIndex);

            const glm::vec3 delta = goal - currentWorld;

            const float distance = glm::length(delta);

            if (distance < 0.001f) return best;

            auto consider = [&](const std::vector<glm::vec3> &points, float length, float penalty) {
                float cost = length + penalty;

                if (!best.success || cost < best.cost)
                {
                    best.success = true;
                    best.points = points;
                    best.cost = cost;
                }
            };

            // -------------------------------------------------
            // 1 szakasz: current -> exact goal
            // -------------------------------------------------
            {
                const glm::vec3 direction = delta / distance;

                if (glm::dot(direction, finalDirection) > 0.9995f)
                {
                    const TurnKind turn = classifyTurn(currentHeading, request.endDirectionIndex);

                    if (turn != TurnKind::Invalid && !segmentHitsObstacle(currentWorld, goal, request.obstacles))
                    {
                        consider({goal}, distance, turnPenalty(turn, request));
                    }
                }
            }

            // -------------------------------------------------
            // 2 szakasz:
            // current --d1--> corner --finalDir--> exact goal
            // -------------------------------------------------
            for (int d1Index = 0; d1Index < RoutingDirectionCount; ++d1Index)
            {
                const TurnKind firstTurn = classifyTurn(currentHeading, d1Index);

                const TurnKind finalTurn = classifyTurn(d1Index, request.endDirectionIndex);

                if (firstTurn == TurnKind::Invalid || finalTurn == TurnKind::Invalid)
                {
                    continue;
                }

                const glm::vec3 d1 = routingDirectionVector(d1Index);

                float a = 0.0f;
                float b = 0.0f;

                if (!solveTwoDirections(delta, d1, finalDirection, a, b))
                {
                    continue;
                }

                if (!positiveLength(a, request.minimumSegmentM) || !positiveLength(b, request.minimumSegmentM))
                {
                    continue;
                }

                const glm::vec3 corner = currentWorld + d1 * a;

                if (segmentHitsObstacle(currentWorld, corner, request.obstacles) ||
                    segmentHitsObstacle(corner, goal, request.obstacles))
                {
                    continue;
                }

                consider({corner, goal}, a + b, turnPenalty(firstTurn, request) + turnPenalty(finalTurn, request));
            }

            // -------------------------------------------------
            // 3 szakasz csak a cél közelében.
            // Ez oldja meg azt az esetet, amikor a rácspont nem esik
            // ugyanabba a síkba a végponttal.
            // -------------------------------------------------
            if (distance <= std::max(request.goalConnectorRangeM, request.maxGridStepM * 3.0f))
            {
                for (int d1Index = 0; d1Index < RoutingDirectionCount; ++d1Index)
                {
                    const TurnKind firstTurn = classifyTurn(currentHeading, d1Index);

                    if (firstTurn == TurnKind::Invalid) continue;

                    const glm::vec3 d1 = routingDirectionVector(d1Index);

                    for (int d2Index = 0; d2Index < RoutingDirectionCount; ++d2Index)
                    {
                        const TurnKind secondTurn = classifyTurn(d1Index, d2Index);

                        const TurnKind finalTurn = classifyTurn(d2Index, request.endDirectionIndex);

                        if (secondTurn == TurnKind::Invalid || finalTurn == TurnKind::Invalid)
                        {
                            continue;
                        }

                        const glm::vec3 d2 = routingDirectionVector(d2Index);

                        float a = 0.0f;
                        float b = 0.0f;
                        float c = 0.0f;

                        if (!solveThreeDirections(delta, d1, d2, finalDirection, a, b, c))
                        {
                            continue;
                        }

                        if (!positiveLength(a, request.minimumSegmentM) ||
                            !positiveLength(b, request.minimumSegmentM) ||
                            !positiveLength(c, request.minimumSegmentM))
                        {
                            continue;
                        }

                        const glm::vec3 corner1 = currentWorld + d1 * a;

                        const glm::vec3 corner2 = corner1 + d2 * b;

                        if (segmentHitsObstacle(currentWorld, corner1, request.obstacles) ||
                            segmentHitsObstacle(corner1, corner2, request.obstacles) ||
                            segmentHitsObstacle(corner2, goal, request.obstacles))
                        {
                            continue;
                        }

                        consider({corner1, corner2, goal}, a + b + c,
                                 turnPenalty(firstTurn, request) + turnPenalty(secondTurn, request) +
                                     turnPenalty(finalTurn, request));
                    }
                }
            }

            return best;
        }
    }

    glm::vec3 routingDirectionVector(int index)
    {
        if (index < 0 || index >= RoutingDirectionCount)
        {
            return glm::vec3{0.0f, 0.0f, 1.0f};
        }

        const GridDirection d = Directions[index];

        return glm::normalize(
            glm::vec3{static_cast<float>(d.x), static_cast<float>(d.y), static_cast<float>(d.z)});
    }

    int routingDirectionIndex(const glm::vec3 &direction)
    {
        if (glm::length2(direction) < 0.0000001f)
        {
            return 4;
        }

        const glm::vec3 wanted = glm::normalize(direction);

        float bestDot = -1000.0f;
        int bestIndex = 0;

        for (int i = 0; i < RoutingDirectionCount; ++i)
        {
            const float score = glm::dot(wanted, routingDirectionVector(i));

            if (score > bestDot)
            {
                bestDot = score;
                bestIndex = i;
            }
        }

        return bestIndex;
    }

    RouteResult AStarRouter::findRoute(const RouteRequest &request) const
    {
        RouteResult result;

        const float step = std::clamp(request.maxGridStepM, 0.05f, 2.0f);

        if (glm::length(request.endWorld - request.startWorld) < 0.01f)
        {
            result.message = "A routing start és end portálpont túl közel van egymáshoz.";
            return result;
        }

        if (insideObstacle(request.startWorld, request.obstacles))
        {
            result.message = "A routing start portálpont akadály belsejében van.";
            return result;
        }

        // Az exact cél lehet akadályhatáron a kiválasztott célcső miatt,
        // de a selected end elem nincs az obstacle-listában.
        if (insideObstacle(request.endWorld, request.obstacles))
        {
            result.message = "A routing cél portálpont egy másik akadály belsejében van.";
            return result;
        }

        const glm::vec3 delta = request.endWorld - request.startWorld;

        const float padding = std::max(request.boundsPaddingM, step * 2.0f);

        const glm::vec3 minWorld = glm::min(request.startWorld, request.endWorld) - glm::vec3{padding};

        const glm::vec3 maxWorld = glm::max(request.startWorld, request.endWorld) + glm::vec3{padding};

        const int minX = static_cast<int>(std::floor((minWorld.x - request.startWorld.x) / step));

        const int maxX = static_cast<int>(std::ceil((maxWorld.x - request.startWorld.x) / step));

        const int minY = static_cast<int>(std::floor((minWorld.y - request.startWorld.y) / step));

        const int maxY = static_cast<int>(std::ceil((maxWorld.y - request.startWorld.y) / step));

        const int minZ = static_cast<int>(std::floor((minWorld.z - request.startWorld.z) / step));

        const int maxZ = static_cast<int>(std::ceil((maxWorld.z - request.startWorld.z) / step));

        auto inBounds = [&](const GridCoord &c) {
            return c.x >= minX && c.x <= maxX && c.y >= minY && c.y <= maxY && c.z >= minZ && c.z <= maxZ;
        };

        const GridCoord startCoord{0, 0, 0};

        const State startState{startCoord, request.startDirectionIndex};

        std::unordered_map<State, Record, StateHash> records;

        std::priority_queue<QueueEntry> open;

        records[startState].g = 0.0f;

        int insertionOrder = 0;

        open.push({startState, heuristic(request.startWorld, request.endWorld), insertionOrder++});

        const int preference = ((request.directionPreference % RoutingDirectionCount) + RoutingDirectionCount) %
                               RoutingDirectionCount;

        bool found = false;
        State bestGoalState{};
        GoalConnector bestConnector{};
        float bestGoalCost = std::numeric_limits<float>::max();

        while (!open.empty() && result.expandedNodes < request.maxExpandedNodes)
        {
            const QueueEntry entry = open.top();
            open.pop();

            Record &currentRecord = records[entry.state];

            if (currentRecord.closed) continue;

            // Ha már találtunk pontos célkapcsolatot, és az open listában
            // lévő legjobb alsó becslés sem lehet jobb, befejezhetjük.
            if (found && entry.f >= bestGoalCost - 0.0001f)
            {
                break;
            }

            currentRecord.closed = true;
            ++result.expandedNodes;

            const glm::vec3 currentWorld = worldFromGrid(request.startWorld, step, entry.state.coord);

            const GoalConnector connector = tryGoalConnector(currentWorld, entry.state.direction, request);

            if (connector.success)
            {
                const float totalCost = currentRecord.g + connector.cost;

                if (!found || totalCost < bestGoalCost)
                {
                    found = true;
                    bestGoalCost = totalCost;
                    bestGoalState = entry.state;
                    bestConnector = connector;
                }
            }

            for (int order = 0; order < RoutingDirectionCount; ++order)
            {
                const int direction = (order + preference) % RoutingDirectionCount;

                if (entry.state.coord == startCoord && request.startDirectionIndex >= 0 &&
                    direction != request.startDirectionIndex)
                {
                    continue;
                }

                const TurnKind turn = classifyTurn(entry.state.direction, direction);

                if (turn == TurnKind::Invalid) continue;

                const GridDirection d = Directions[direction];

                const GridCoord nextCoord{entry.state.coord.x + d.x, entry.state.coord.y + d.y,
                                          entry.state.coord.z + d.z};

                if (!inBounds(nextCoord)) continue;

                const glm::vec3 nextWorld = worldFromGrid(request.startWorld, step, nextCoord);

                if (insideObstacle(nextWorld, request.obstacles))
                {
                    continue;
                }

                if (segmentHitsObstacle(currentWorld, nextWorld, request.obstacles))
                {
                    continue;
                }

                float moveCost = step * d.lengthFactor + turnPenalty(turn, request);

                // Apró tie-break az alternatívákhoz.
                moveCost += static_cast<float>(order) * step * 0.00001f;

                const float tentative = currentRecord.g + moveCost;

                const State nextState{nextCoord, direction};

                Record &nextRecord = records[nextState];

                if (nextRecord.closed || tentative >= nextRecord.g - 0.000001f)
                {
                    continue;
                }

                nextRecord.g = tentative;
                nextRecord.parent = entry.state;
                nextRecord.hasParent = true;

                const float f = tentative + heuristic(nextWorld, request.endWorld);

                if (found && f >= bestGoalCost)
                {
                    continue;
                }

                open.push({nextState, f, insertionOrder++});
            }
        }

        if (!found)
        {
            result.message = result.expandedNodes >= request.maxExpandedNodes
                                 ? "Az A* elérte a keresési limitet."
                                 : "Az A* nem talált akadálymentes 45°/90° útvonalat.";
            return result;
        }

        std::vector<State> reversed;
        State current = bestGoalState;

        reversed.push_back(current);

        while (!(current == startState))
        {
            const auto it = records.find(current);

            if (it == records.end() || !it->second.hasParent)
            {
                result.message = "Az A* útvonal visszafejtése sikertelen.";
                return result;
            }

            current = it->second.parent;

            reversed.push_back(current);
        }

        std::reverse(reversed.begin(), reversed.end());

        std::vector<glm::vec3> points;
        points.reserve(reversed.size() + bestConnector.points.size());

        for (const State &state : reversed)
        {
            appendUnique(points, worldFromGrid(request.startWorld, step, state.coord));
        }

        for (const glm::vec3 &point : bestConnector.points)
        {
            appendUnique(points, point);
        }

        if (!points.empty())
        {
            points.front() = request.startWorld;
            points.back() = request.endWorld;
        }

        result.worldPoints = simplify(points);

        result.success = result.worldPoints.size() >= 2;

        result.message = result.success ? "Gyors 18 irányú A*: pontos célcsatlakozóval."
                                        : "Az A* nem adott használható útvonalat.";

        return result;
    }
}
