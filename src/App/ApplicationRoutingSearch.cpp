#include "App/ApplicationInternal.hpp"
#include "Routing/RouteGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>
#include <vector>

namespace plumbcad
{
    using namespace routing;

    namespace
    {
        struct TerminalMode
        {
            glm::vec3 portal{0.0f};
            std::vector<glm::vec3> fixedPoints;
            int directionIndex{-1};
            bool directBend{false};
            float terminalPenaltyM{0.0f};
            float score{0.0f};
        };

        struct SearchAttempt
        {
            float maxStepM;
            float paddingM;
            int modeLimit;
            int pairLimit;
            int preferenceCount;
            int maxExpandedNodes;
            int totalExpandedBudget;
        };

        float terminalBendPenalty(RouteBendKind kind)
        {
            if (kind == RouteBendKind::Deg45) return 0.06f;

            if (kind == RouteBendKind::Deg90) return 0.12f;

            return 0.0f;
        }

        void appendUniquePoint(std::vector<glm::vec3> &points, const glm::vec3 &point)
        {
            if (points.empty() || glm::length(points.back() - point) > 0.001f)
            {
                points.push_back(point);
            }
        }

        glm::vec3 stablePerpendicular(const glm::vec3 &direction)
        {
            const glm::vec3 d = glm::normalize(direction);

            glm::vec3 candidate =
                std::abs(d.y) < 0.85f ? glm::vec3{0.0f, 1.0f, 0.0f} : glm::vec3{1.0f, 0.0f, 0.0f};

            glm::vec3 perpendicular = glm::cross(d, candidate);

            if (glm::length2(perpendicular) < 0.000001f)
            {
                perpendicular = glm::cross(d, glm::vec3{0.0f, 0.0f, 1.0f});
            }

            return glm::normalize(perpendicular);
        }

        bool buildTwo90StartTerminal(const glm::vec3 &endpointWorld, const glm::vec3 &actualStartDirection,
                                     const glm::vec3 &routingDirection, float bendRadius, float sideSign,
                                     TerminalMode &outMode)
        {
            const glm::vec3 a = glm::normalize(actualStartDirection);

            const glm::vec3 b = glm::normalize(routingDirection);

            glm::vec3 middle = glm::cross(a, b);

            if (glm::length2(middle) < 0.000001f)
            {
                middle = stablePerpendicular(a);
            }
            else
            {
                middle = glm::normalize(middle);
            }

            middle *= sideSign >= 0.0f ? 1.0f : -1.0f;

            // A köztes irány mindkét irányra merőleges:
            // a -> middle = 90°, middle -> b = 90°.
            if (std::abs(glm::dot(a, middle)) > 0.002f || std::abs(glm::dot(middle, b)) > 0.002f)
            {
                return false;
            }

            const float r90 = routeEffectiveBendRadius(RouteBendKind::Deg90, bendRadius);

            const glm::vec3 corner1 = endpointWorld + a * r90;

            const glm::vec3 corner2 = corner1 + middle * (2.0f * r90);

            const glm::vec3 portal = corner2 + b * r90;

            outMode.portal = portal;
            outMode.fixedPoints = {endpointWorld, corner1, corner2};

            outMode.directBend = true;

            outMode.terminalPenaltyM = 2.0f * terminalBendPenalty(RouteBendKind::Deg90);

            return true;
        }

        bool buildTwo90EndTerminal(const glm::vec3 &endpointWorld, const glm::vec3 &routingIncomingDirection,
                                   const glm::vec3 &finalTravelDirection, float bendRadius, float sideSign,
                                   TerminalMode &outMode)
        {
            const glm::vec3 a = glm::normalize(routingIncomingDirection);

            const glm::vec3 b = glm::normalize(finalTravelDirection);

            glm::vec3 middle = glm::cross(a, b);

            if (glm::length2(middle) < 0.000001f)
            {
                middle = stablePerpendicular(a);
            }
            else
            {
                middle = glm::normalize(middle);
            }

            middle *= sideSign >= 0.0f ? 1.0f : -1.0f;

            if (std::abs(glm::dot(a, middle)) > 0.002f || std::abs(glm::dot(middle, b)) > 0.002f)
            {
                return false;
            }

            const float r90 = routeEffectiveBendRadius(RouteBendKind::Deg90, bendRadius);

            const glm::vec3 corner2 = endpointWorld - b * r90;

            const glm::vec3 corner1 = corner2 - middle * (2.0f * r90);

            const glm::vec3 portal = corner1 - a * r90;

            outMode.portal = portal;
            outMode.fixedPoints = {corner1, corner2, endpointWorld};

            outMode.directBend = true;

            outMode.terminalPenaltyM = 2.0f * terminalBendPenalty(RouteBendKind::Deg90);

            return true;
        }

        std::vector<int> bestModeIndices(const std::vector<TerminalMode> &modes, int limit)
        {
            std::vector<int> indices(modes.size());
            std::iota(indices.begin(), indices.end(), 0);

            std::sort(indices.begin(), indices.end(), [&](int a, int b) {
                // A sima egyenes mód mindig kapjon kis előnyt,
                // de a könyökös lehetőségek is maradjanak versenyben.
                const float scoreA = modes[a].score + (modes[a].directBend ? 0.02f : 0.0f);

                const float scoreB = modes[b].score + (modes[b].directBend ? 0.02f : 0.0f);

                return scoreA < scoreB;
            });

            if (limit > 0 && static_cast<int>(indices.size()) > limit)
            {
                indices.resize(limit);
            }

            return indices;
        }
    }

    bool Application::searchAutomaticRoutes()
    {
        PipeElement *startPipe = m_network.find(m_routeStartId);

        PipeElement *endPipe = m_network.find(m_routeEndId);

        m_routeAlternatives.clear();
        m_selectedRouteAlternative = 0;
        clearRoutePreview();

        if (!startPipe || !endPipe || m_routeStartEndpoint < 0 || m_routeEndEndpoint < 0 ||
            m_routeStartEndpoint >= static_cast<int>(startPipe->connections.size()) ||
            m_routeEndEndpoint >= static_cast<int>(endPipe->connections.size()))
        {
            setStatusMessage("Az útvonalkereséshez válassz kezdő- és végpontot.");
            return false;
        }

        if (!startPipe->connections[m_routeStartEndpoint].isOpen() ||
            !endPipe->connections[m_routeEndEndpoint].isOpen())
        {
            setStatusMessage("A kiválasztott útvonalpontok közül valamelyik már csatlakoztatva van.");
            return false;
        }

        if (m_routeStartId == m_routeEndId && m_routeStartEndpoint == m_routeEndEndpoint)
        {
            setStatusMessage("A kezdő- és végpont nem lehet ugyanaz.");
            return false;
        }

        const glm::vec3 startWorld = connectionWorldPosition(m_routeStartId, m_routeStartEndpoint);

        const glm::vec3 endWorld = connectionWorldPosition(m_routeEndId, m_routeEndEndpoint);

        glm::vec3 actualStartOut = routeEndpointOutwardDirection(m_routeStartId, m_routeStartEndpoint);

        glm::vec3 actualEndOut = routeEndpointOutwardDirection(m_routeEndId, m_routeEndEndpoint);

        if (glm::length2(actualStartOut) < 0.000001f) actualStartOut = glm::vec3{0.0f, 0.0f, 1.0f};

        if (glm::length2(actualEndOut) < 0.000001f) actualEndOut = glm::vec3{0.0f, 0.0f, 1.0f};

        actualStartOut = glm::normalize(actualStartOut);

        actualEndOut = glm::normalize(actualEndOut);

        // A START/END valódi irányát nem kerekítjük.
        // Viszont automatikus AlignmentElbow / végpont-illesztő idom sincs.
        //
        // A belső A* továbbra is 18 diszkrét haladási iránnyal keres,
        // de a tetszőlegesen elforgatott START/END csővéget kizárólag
        // meglévő szabványos 45°/90° könyökök kötik ehhez a belső hálóhoz.
        //
        // Ha egyetlen 45°/90° könyök nem elég, két 90° könyököt használunk
        // egymás után, szabad 3D forgatási síkkal. Matematikailag így
        // tetszőleges csővég-irány átvezethető egy választott routing irányba
        // anélkül, hogy új, tetszőleges szögű idomtípust kellene létrehozni.
        const glm::vec3 finalTravelDirection = -actualEndOut;

        const float routeStartDiameterMm = pipeEndpointDiameterMm(*startPipe, m_routeStartEndpoint);

        const float bendRadius = std::max(0.12f, (routeStartDiameterMm / 1000.0f) * 1.5f);

        const glm::vec3 mainRouteStartWorld = startWorld;

        const float endDiameterMm = pipeEndpointDiameterMm(*endPipe, m_routeEndEndpoint);
        const RouteCatalogPlan catalogPlan =
            selectRouteCatalog(routeStartDiameterMm, endDiameterMm, startPipe->pipeTypeIndex);
        if (catalogPlan.pipeSize < 0)
        {
            setStatusMessage("A kiválasztott A* csőkategóriához nincs katalógusméret.");
            return false;
        }
        const float routedDiameterMm =
            m_network.catalog()[catalogPlan.pipeType].sizes[catalogPlan.pipeSize].diameterMm;
        const bool needsReducer = std::abs(routedDiameterMm - endDiameterMm) >= 0.001f;
        if (needsReducer && catalogPlan.reducerType < 0)
        {
            setStatusMessage("Az A* END átmérőjéhez nincs mindkét méretet tartalmazó szűkítőkatalógus.");
            return false;
        }

        const float maximumDiameterMm = std::max(routedDiameterMm, endDiameterMm);
        const float reducerLength = needsReducer ? std::max(0.15f, maximumDiameterMm * 0.0015f) : 0.0f;
        const glm::vec3 mainRouteEndWorld = endWorld + actualEndOut * reducerLength;
        if (needsReducer && !routeEndReducerIsClear(mainRouteEndWorld, endWorld, maximumDiameterMm))
        {
            setStatusMessage("Az A* END szűkítője akadályba ütközne.");
            return false;
        }

        const float routedPipeRadiusM =
            std::max(0.005f, std::max(routeStartDiameterMm, routedDiameterMm) * 0.0005f);

        const float requestedGrid = std::clamp(m_routeGridSizeM, 0.15f, 2.0f);

        std::vector<RouteObstacle> obstacles =
            buildRoutingObstacles(m_routeStartId, m_routeEndId, routedPipeRadiusM);

        const std::vector<RouteObstacle> endpointObstacles =
            buildRoutingEndpointObstacles(m_routeStartId, m_routeEndId, routedPipeRadiusM);

        obstacles.insert(obstacles.end(), endpointObstacles.begin(), endpointObstacles.end());

        const glm::vec3 preferredTravel = glm::length2(mainRouteEndWorld - mainRouteStartWorld) > 0.000001f
                                              ? glm::normalize(mainRouteEndWorld - mainRouteStartWorld)
                                              : actualStartOut;

        constexpr float DirectionMatchToleranceRad = 0.00436332313f; // 0.25°

        constexpr float FixedBendToleranceRad = 0.01221730476f; // 0.7°

        constexpr float Deg45Rad = 0.78539816339f;

        constexpr float Deg90Rad = 1.57079632679f;

        // -----------------------------------------------------
        // START portálok
        // -----------------------------------------------------
        std::vector<TerminalMode> startModes;

        for (int directionIndex = 0; directionIndex < RoutingDirectionCount; ++directionIndex)
        {
            const glm::vec3 outgoing = routingDirectionVector(directionIndex);

            const float angle = routeDirectionAngleRadians(actualStartOut, outgoing);

            if (angle <= DirectionMatchToleranceRad)
            {
                TerminalMode straight;
                straight.portal = mainRouteStartWorld;
                straight.fixedPoints = {mainRouteStartWorld};
                straight.directionIndex = directionIndex;

                straight.directBend = false;

                straight.terminalPenaltyM = 0.0f;
                straight.score = glm::length(mainRouteEndWorld - mainRouteStartWorld);

                startModes.push_back(std::move(straight));
            }
            else if (std::abs(angle - Deg45Rad) <= FixedBendToleranceRad ||
                     std::abs(angle - Deg90Rad) <= FixedBendToleranceRad)
            {
                const RouteBendKind kind = std::abs(angle - Deg45Rad) <= FixedBendToleranceRad
                                               ? RouteBendKind::Deg45
                                               : RouteBendKind::Deg90;

                const float exactFixedAngle = routeBendAngleRadians(kind);

                const float tangent =
                    routeEffectiveBendRadius(kind, bendRadius) * std::tan(exactFixedAngle * 0.5f);

                const glm::vec3 corner = mainRouteStartWorld + actualStartOut * tangent;

                const glm::vec3 portal = corner + outgoing * tangent;

                const std::vector<glm::vec3> localGeometry{mainRouteStartWorld, corner, portal};

                if (routeCanBeBuilt(localGeometry, bendRadius) &&
                    !routeGeometryHitsObstacles(localGeometry, bendRadius, obstacles))
                {
                    TerminalMode mode;
                    mode.portal = portal;
                    mode.fixedPoints = {mainRouteStartWorld, corner};
                    mode.directionIndex = directionIndex;

                    mode.directBend = true;

                    mode.terminalPenaltyM = terminalBendPenalty(kind);

                    const float headingPenalty = std::max(0.0f, 1.0f - glm::dot(outgoing, preferredTravel));

                    mode.score =
                        glm::length(mainRouteEndWorld - portal) + mode.terminalPenaltyM + headingPenalty * 0.20f;

                    startModes.push_back(std::move(mode));
                }
            }

            // Általános fallback: két valódi 90° könyök.
            for (float sideSign : {1.0f, -1.0f})
            {
                TerminalMode mode;

                if (!buildTwo90StartTerminal(mainRouteStartWorld, actualStartOut, outgoing, bendRadius, sideSign,
                                             mode))
                {
                    continue;
                }

                mode.directionIndex = directionIndex;

                std::vector<glm::vec3> localGeometry = mode.fixedPoints;

                appendUniquePoint(localGeometry, mode.portal);

                if (!routeCanBeBuilt(localGeometry, bendRadius) ||
                    routeGeometryHitsObstacles(localGeometry, bendRadius, obstacles))
                {
                    continue;
                }

                const float headingPenalty = std::max(0.0f, 1.0f - glm::dot(outgoing, preferredTravel));

                mode.score =
                    glm::length(mainRouteEndWorld - mode.portal) + mode.terminalPenaltyM + headingPenalty * 0.20f;

                startModes.push_back(std::move(mode));
            }
        }

        // -----------------------------------------------------
        // END portálok
        // -----------------------------------------------------
        std::vector<TerminalMode> endModes;

        for (int directionIndex = 0; directionIndex < RoutingDirectionCount; ++directionIndex)
        {
            const glm::vec3 incoming = routingDirectionVector(directionIndex);

            const float angle = routeDirectionAngleRadians(incoming, finalTravelDirection);

            if (angle <= DirectionMatchToleranceRad)
            {
                TerminalMode straight;
                straight.portal = mainRouteEndWorld;
                straight.fixedPoints = {mainRouteEndWorld};
                straight.directionIndex = directionIndex;

                straight.directBend = false;

                straight.terminalPenaltyM = 0.0f;
                straight.score = glm::length(mainRouteEndWorld - mainRouteStartWorld);

                endModes.push_back(std::move(straight));
            }
            else if (std::abs(angle - Deg45Rad) <= FixedBendToleranceRad ||
                     std::abs(angle - Deg90Rad) <= FixedBendToleranceRad)
            {
                const RouteBendKind kind = std::abs(angle - Deg45Rad) <= FixedBendToleranceRad
                                               ? RouteBendKind::Deg45
                                               : RouteBendKind::Deg90;

                const float exactFixedAngle = routeBendAngleRadians(kind);

                const float tangent =
                    routeEffectiveBendRadius(kind, bendRadius) * std::tan(exactFixedAngle * 0.5f);

                const glm::vec3 corner = mainRouteEndWorld - finalTravelDirection * tangent;

                const glm::vec3 portal = corner - incoming * tangent;

                const std::vector<glm::vec3> localGeometry{portal, corner, mainRouteEndWorld};

                if (routeCanBeBuilt(localGeometry, bendRadius) &&
                    !routeGeometryHitsObstacles(localGeometry, bendRadius, obstacles))
                {
                    TerminalMode mode;
                    mode.portal = portal;
                    mode.fixedPoints = {corner, mainRouteEndWorld};
                    mode.directionIndex = directionIndex;

                    mode.directBend = true;

                    mode.terminalPenaltyM = terminalBendPenalty(kind);

                    const float headingPenalty = std::max(0.0f, 1.0f - glm::dot(incoming, preferredTravel));

                    mode.score =
                        glm::length(portal - mainRouteStartWorld) + mode.terminalPenaltyM + headingPenalty * 0.20f;

                    endModes.push_back(std::move(mode));
                }
            }

            for (float sideSign : {1.0f, -1.0f})
            {
                TerminalMode mode;

                if (!buildTwo90EndTerminal(mainRouteEndWorld, incoming, finalTravelDirection, bendRadius, sideSign,
                                           mode))
                {
                    continue;
                }

                mode.directionIndex = directionIndex;

                std::vector<glm::vec3> localGeometry;
                localGeometry.push_back(mode.portal);

                for (const glm::vec3 &point : mode.fixedPoints)
                {
                    appendUniquePoint(localGeometry, point);
                }

                if (!routeCanBeBuilt(localGeometry, bendRadius) ||
                    routeGeometryHitsObstacles(localGeometry, bendRadius, obstacles))
                {
                    continue;
                }

                const float headingPenalty = std::max(0.0f, 1.0f - glm::dot(incoming, preferredTravel));

                mode.score = glm::length(mode.portal - mainRouteStartWorld) + mode.terminalPenaltyM +
                             headingPenalty * 0.20f;

                endModes.push_back(std::move(mode));
            }
        }

        if (startModes.empty() || endModes.empty())
        {
            setStatusMessage("A kezdő- vagy végpont körül nincs geometriailag használható routing irány.");
            return false;
        }

        const std::array<SearchAttempt, 3> attempts{
            {{requestedGrid, std::max(0.80f, requestedGrid * 3.0f), 7, 14, 1, 35000, 90000},
             {std::max(0.14f, requestedGrid * 0.70f), std::max(1.50f, requestedGrid * 5.0f), 10, 22, 1, 65000,
              220000},
             {std::max(0.10f, requestedGrid * 0.50f), std::max(2.50f, requestedGrid * 7.0f), 14, 32, 1, 90000,
              360000}}};

        auto appendAlternative = [&](const TerminalMode &startMode, const TerminalMode &endMode,
                                     const RouteResult &route) -> bool {
            if (!route.success) return false;

            std::vector<glm::vec3> points;

            for (const glm::vec3 &point : startMode.fixedPoints)
            {
                appendUniquePoint(points, point);
            }

            for (const glm::vec3 &point : route.worldPoints)
            {
                appendUniquePoint(points, point);
            }

            for (const glm::vec3 &point : endMode.fixedPoints)
            {
                appendUniquePoint(points, point);
            }

            points = simplifyRoutePoints(points);

            if (points.size() < 2) return false;

            if (!routeHasCorrectEndpointDirections(points, actualStartOut, actualEndOut))
            {
                return false;
            }

            std::vector<RouteBuildPrimitive> buildPrimitives;

            if (!buildRouteBuildPlan(points, bendRadius, buildPrimitives))
            {
                return false;
            }

            // EXACTLY the same primitives are later used for preview and build.
            // No second tangent calculation is allowed in the build step.
            if (routeBuildPlanHitsObstacles(buildPrimitives, bendRadius, obstacles))
            {
                return false;
            }

            for (const RouteAlternative &existing : m_routeAlternatives)
            {
                if (sameRoute(existing.worldPoints, points))
                {
                    return false;
                }
            }

            RouteAlternative alternative;
            alternative.worldPoints = std::move(points);

            alternative.buildPrimitives = std::move(buildPrimitives);

            alternative.catalogPlan = catalogPlan;
            alternative.totalLengthM =
                routeBuildPlanPhysicalLength(alternative.buildPrimitives, bendRadius) + reducerLength;

            countRouteBends(alternative.worldPoints, alternative.bend45Count, alternative.bend90Count);

            alternative.startsWithBend = startMode.directBend;

            alternative.endsWithBend = endMode.directBend;

            alternative.startEndpointWorld = startWorld;
            alternative.endEndpointWorld = endWorld;
            alternative.startOutwardDirection = actualStartOut;
            alternative.endOutwardDirection = actualEndOut;
            alternative.mainRouteEndWorld = mainRouteEndWorld;

            alternative.segmentCount =
                alternative.worldPoints.size() >= 2 ? static_cast<int>(alternative.worldPoints.size()) - 1 : 0;

            m_routeAlternatives.push_back(std::move(alternative));

            return true;
        };

        for (std::size_t attemptIndex = 0; attemptIndex < attempts.size(); ++attemptIndex)
        {
            const SearchAttempt &attempt = attempts[attemptIndex];

            const std::vector<int> selectedStarts = bestModeIndices(startModes, attempt.modeLimit);

            const std::vector<int> selectedEnds = bestModeIndices(endModes, attempt.modeLimit);

            struct Pair
            {
                int startIndex;
                int endIndex;
                float score;
            };

            std::vector<Pair> pairs;

            for (int startIndex : selectedStarts)
            {
                for (int endIndex : selectedEnds)
                {
                    const TerminalMode &s = startModes[startIndex];

                    const TerminalMode &e = endModes[endIndex];

                    const float score = glm::length(e.portal - s.portal) + s.terminalPenaltyM + e.terminalPenaltyM;

                    pairs.push_back({startIndex, endIndex, score});
                }
            }

            std::sort(pairs.begin(), pairs.end(), [](const Pair &a, const Pair &b) { return a.score < b.score; });

            if (static_cast<int>(pairs.size()) > attempt.pairLimit)
            {
                pairs.resize(attempt.pairLimit);
            }

            int attemptExpandedNodes = 0;

            for (const Pair &pair : pairs)
            {
                if (m_routeAlternatives.size() >= 6 || attemptExpandedNodes >= attempt.totalExpandedBudget)
                {
                    break;
                }

                const TerminalMode &startMode = startModes[pair.startIndex];

                const TerminalMode &endMode = endModes[pair.endIndex];

                if (glm::length(endMode.portal - startMode.portal) < 0.04f)
                {
                    continue;
                }

                for (int preference = 0; preference < attempt.preferenceCount; ++preference)
                {
                    RouteRequest request;
                    request.startWorld = startMode.portal;
                    request.endWorld = endMode.portal;
                    request.maxGridStepM = attempt.maxStepM;
                    request.boundsPaddingM = attempt.paddingM;
                    request.bend45PenaltyM = 0.05f;
                    request.bend90PenaltyM = 0.10f;
                    request.minimumSegmentM = 0.04f;
                    request.goalConnectorRangeM = std::max(1.25f, attempt.maxStepM * 5.0f);
                    request.maxExpandedNodes = attempt.maxExpandedNodes;
                    request.directionPreference = preference + static_cast<int>(attemptIndex) * 5;
                    request.startDirectionIndex = startMode.directionIndex;
                    request.endDirectionIndex = endMode.directionIndex;
                    request.obstacles = obstacles;

                    const RouteResult route = m_router.findRoute(request);

                    attemptExpandedNodes += route.expandedNodes;

                    appendAlternative(startMode, endMode, route);

                    if (m_routeAlternatives.size() >= 6 || attemptExpandedNodes >= attempt.totalExpandedBudget)
                    {
                        break;
                    }
                }
            }

            // Ha már van több jó megoldás, nem kell a legfinomabb
            // és legdrágább fallback keresést is lefuttatni.
            if (m_routeAlternatives.size() >= 6) break;
        }

        std::sort(m_routeAlternatives.begin(), m_routeAlternatives.end(),
                  [](const RouteAlternative &a, const RouteAlternative &b) {
                      const float scoreA = a.totalLengthM + static_cast<float>(a.bend45Count) * 0.06f +
                                           static_cast<float>(a.bend90Count) * 0.12f;

                      const float scoreB = b.totalLengthM + static_cast<float>(b.bend45Count) * 0.06f +
                                           static_cast<float>(b.bend90Count) * 0.12f;

                      if (std::abs(scoreA - scoreB) > 0.005f) return scoreA < scoreB;

                      return a.totalLengthM < b.totalLengthM;
                  });

        if (m_routeAlternatives.empty())
        {
            setStatusMessage(
                "Az A* nem talált megépíthető útvonalat az exact START/END irányokkal a három kontrollált keresési lépcsőben.");
            return false;
        }

        m_selectedRouteAlternative = 0;
        rebuildRoutePreview();

        setStatusMessage("Szabványos idomos A* keresés kész: " + std::to_string(m_routeAlternatives.size()) +
                         " megépíthető útvonal." +
                         (needsReducer ? " Mindegyik az END pontnál szűkítőt tartalmaz." : ""));

        return true;
    }
}
