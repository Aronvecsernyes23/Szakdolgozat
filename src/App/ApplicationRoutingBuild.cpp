#include "App/ApplicationInternal.hpp"
#include "Routing/RouteGeometry.hpp"
#include <cmath>
#include <vector>
#include <limits>

namespace plumbcad
{
    using namespace routing;

    void Application::applyRouteValidationProfileToElement(ObjectId id)
    {
        PipeElement *pipe = m_network.find(id);

        if (!pipe) return;

        applyPipeValidationProfile(*pipe, m_routeValidationProfile);

        if (pipe->type != PipeElementType::StraightPipe)
        {
            pipe->drainageCheckEnabled = false;
        }
    }

    void Application::applyRouteValidationProfileToLastBuilt()
    {
        int appliedCount = 0;

        for (ObjectId id : m_lastRouteCreatedIds)
        {
            if (!m_network.find(id)) continue;

            applyRouteValidationProfileToElement(id);
            ++appliedCount;
        }

        m_validationHasRun = false;
        m_validationMessages.clear();

        setStatusMessage(appliedCount > 0
                             ? "Validációs sablon alkalmazva az utolsó A* útvonal " +
                                   std::to_string(appliedCount) + " elemére."
                             : "Nincs elérhető korábbi A* útvonal a csoportos validációs beállításhoz.");
    }

    bool Application::buildSelectedAutomaticRoute()
    {
        PipeElement *startPipe = m_network.find(m_routeStartId);
        PipeElement *endPipe = m_network.find(m_routeEndId);
        if (!startPipe || !endPipe || m_routeStartEndpoint < 0 || m_routeEndEndpoint < 0 ||
            m_routeStartEndpoint >= static_cast<int>(startPipe->connections.size()) ||
            m_routeEndEndpoint >= static_cast<int>(endPipe->connections.size()))
        {
            setStatusMessage("A kiválasztott útvonal létrehozásához válassz kezdő- és végpontot.");
            return false;
        }
        if (!startPipe->connections[m_routeStartEndpoint].isOpen() ||
            !endPipe->connections[m_routeEndEndpoint].isOpen())
        {
            setStatusMessage("A kiválasztott útvonalpontok közül valamelyik már csatlakoztatva van.");
            return false;
        }
        if (m_routeAlternatives.empty() || m_selectedRouteAlternative < 0 ||
            m_selectedRouteAlternative >= static_cast<int>(m_routeAlternatives.size()))
        {
            setStatusMessage("Előbb keress útvonal-lehetőségeket, majd válassz egyet a listából.");
            return false;
        }

        const RouteAlternative selectedAlternative = m_routeAlternatives[m_selectedRouteAlternative];

        const std::vector<glm::vec3> points = selectedAlternative.worldPoints;

        const glm::vec3 currentStartWorld = connectionWorldPosition(m_routeStartId, m_routeStartEndpoint);
        const glm::vec3 currentEndWorld = connectionWorldPosition(m_routeEndId, m_routeEndEndpoint);
        glm::vec3 currentActualStartOut = routeEndpointOutwardDirection(m_routeStartId, m_routeStartEndpoint);

        glm::vec3 currentActualEndOut = routeEndpointOutwardDirection(m_routeEndId, m_routeEndEndpoint);

        if (glm::length2(currentActualStartOut) < 0.000001f || glm::length2(currentActualEndOut) < 0.000001f)
        {
            setStatusMessage("A kezdő- vagy végpont iránya nem határozható meg.");
            return false;
        }

        currentActualStartOut = glm::normalize(currentActualStartOut);

        currentActualEndOut = glm::normalize(currentActualEndOut);

        if (points.empty() || glm::length(currentStartWorld - selectedAlternative.startEndpointWorld) > 0.005f ||
            glm::length(currentEndWorld - selectedAlternative.endEndpointWorld) > 0.005f ||
            glm::length(points.front() - selectedAlternative.startEndpointWorld) > 0.005f ||
            glm::length(points.back() - selectedAlternative.mainRouteEndWorld) > 0.005f ||
            glm::dot(currentActualStartOut, selectedAlternative.startOutwardDirection) < 0.9995f ||
            glm::dot(currentActualEndOut, selectedAlternative.endOutwardDirection) < 0.9995f)
        {
            setStatusMessage(
                "A hálózat vagy a kiválasztott csővég forgatása megváltozott az útvonalkeresés óta. Futtasd újra az útvonalkeresést.");
            return false;
        }

        const glm::vec3 currentStartOut = selectedAlternative.startOutwardDirection;

        const glm::vec3 currentEndOut = selectedAlternative.endOutwardDirection;

        if (!routeHasCorrectEndpointDirections(points, currentStartOut, currentEndOut))
        {
            setStatusMessage(
                "A kiválasztott útvonal nem a megfelelő irányból érkezik a végponthoz. Futtasd újra az útvonalkeresést.");
            return false;
        }

        const float routeStartDiameterMm = pipeEndpointDiameterMm(*startPipe, m_routeStartEndpoint);

        const float endDiameterMm = pipeEndpointDiameterMm(*endPipe, m_routeEndEndpoint);
        const RouteCatalogPlan catalogPlan =
            selectRouteCatalog(routeStartDiameterMm, endDiameterMm, startPipe->pipeTypeIndex);
        if (catalogPlan.pipeSize < 0 || catalogPlan != selectedAlternative.catalogPlan)
        {
            setStatusMessage("A csőméret vagy a csőkategória megváltozott. Futtasd újra az A* keresést.");
            return false;
        }
        const auto &routeCatalog = m_network.catalog();
        const int sourceTypeIndex = catalogPlan.pipeType;
        const int sourceSizeIndex = catalogPlan.pipeSize;
        const auto &sourceType = routeCatalog[sourceTypeIndex];
        const float sourceDiameterMm = sourceType.sizes[sourceSizeIndex].diameterMm;
        const bool hasEndReducer = catalogPlan.reducerType >= 0;
        if (std::abs(sourceDiameterMm - endDiameterMm) >= 0.001f && !hasEndReducer)
        {
            setStatusMessage("Az A* END átmérőjéhez nincs mindkét méretet tartalmazó szűkítőkatalógus.");
            return false;
        }
        if (hasEndReducer && !routeEndReducerIsClear(selectedAlternative.mainRouteEndWorld, currentEndWorld,
                                                     std::max(sourceDiameterMm, endDiameterMm)))
        {
            setStatusMessage("Az A* END szűkítője az útvonalkeresés óta akadályba ütközne.");
            return false;
        }

        const float bendRadius = std::max(0.12f, (routeStartDiameterMm / 1000.0f) * 1.5f);

        const float routedPipeRadiusM =
            std::max(0.005f, std::max(routeStartDiameterMm, sourceDiameterMm) * 0.0005f);

        std::vector<RouteObstacle> currentValidationObstacles =
            buildRoutingObstacles(m_routeStartId, m_routeEndId, routedPipeRadiusM);

        const std::vector<RouteObstacle> currentEndpointObstacles =
            buildRoutingEndpointObstacles(m_routeStartId, m_routeEndId, routedPipeRadiusM);

        currentValidationObstacles.insert(currentValidationObstacles.end(), currentEndpointObstacles.begin(),
                                          currentEndpointObstacles.end());

        if (selectedAlternative.buildPrimitives.empty())
        {
            setStatusMessage(
                "A kiválasztott útvonalhoz nincs eltárolt végleges építési geometria. Futtasd újra az útvonalkeresést.");
            return false;
        }

        // We only re-check whether the environment changed after search.
        // Geometry itself is NOT recalculated here: these are the exact
        // tangent points and elbow endpoints accepted by SEARCH.
        if (routeBuildPlanHitsObstacles(selectedAlternative.buildPrimitives, bendRadius,
                                        currentValidationObstacles))
        {
            setStatusMessage(
                "Az útvonal környezete megváltozott a keresés óta: az eltárolt végleges csőgeometria most akadályt keresztez.");
            return false;
        }

        ObjectId previousId = m_routeStartId;
        int previousPoint = m_routeStartEndpoint;
        ObjectId lastCreatedId = InvalidObjectId;
        std::vector<ObjectId> createdRouteIds;

        // ---------------------------------------------------------
        // MAIN ROUTE BUILD
        //
        // From this point on we DO NOT calculate tangent points from
        // worldPoints again. We build exactly the RouteBuildPrimitive list
        // that was produced, collision-checked and previewed by SEARCH.
        // ---------------------------------------------------------
        for (const RouteBuildPrimitive &primitive : selectedAlternative.buildPrimitives)
        {
            const glm::vec3 expectedPrimitiveStart = primitive.startWorld;

            const glm::vec3 currentTailWorld = connectionWorldPosition(previousId, previousPoint);

            // Small floating-point drift is tolerated, but a real network
            // change must require a new search.
            if (glm::length(currentTailWorld - expectedPrimitiveStart) > 0.006f)
            {
                setStatusMessage(
                    "Az eltárolt útvonal kezdő tangenciapontja már nem esik a jelenlegi hálózati csatlakozásra. Futtasd újra az útvonalkeresést.");
                return false;
            }

            if (primitive.kind == RouteBuildPrimitiveKind::Straight)
            {
                const glm::vec3 segmentDelta = primitive.endWorld - primitive.startWorld;

                const float length = glm::length(segmentDelta);

                if (length < MinStraightPipeLengthM - 0.001f)
                {
                    setStatusMessage("Az útvonalkeresésből eltárolt egyenes csőszakasz túl rövid.");
                    return false;
                }

                const glm::vec3 segmentDirection = segmentDelta / length;

                const int straightPieceCount =
                    std::max(1, static_cast<int>(std::ceil(length / MaxStraightPipeLengthM)));

                const float nominalPieceLength = length / static_cast<float>(straightPieceCount);

                if (nominalPieceLength < MinStraightPipeLengthM - 0.001f ||
                    nominalPieceLength > MaxStraightPipeLengthM + 0.001f)
                {
                    setStatusMessage("Az eltárolt egyenes szakasz nem bontható 0.04-8.00 m közötti csőelemekre.");
                    return false;
                }

                for (int pieceIndex = 0; pieceIndex < straightPieceCount; ++pieceIndex)
                {
                    const float t0 = static_cast<float>(pieceIndex) / static_cast<float>(straightPieceCount);

                    const float t1 = static_cast<float>(pieceIndex + 1) / static_cast<float>(straightPieceCount);

                    const glm::vec3 pieceStart = glm::mix(primitive.startWorld, primitive.endWorld, t0);

                    const glm::vec3 pieceEnd = pieceIndex + 1 == straightPieceCount
                                                   ? primitive.endWorld
                                                   : glm::mix(primitive.startWorld, primitive.endWorld, t1);

                    const float pieceLength = glm::length(pieceEnd - pieceStart);

                    const ObjectId pieceId = m_network.addStraightPipe(pieceLength, sourceDiameterMm);

                    m_network.setPipeType(pieceId, sourceTypeIndex);

                    m_network.setPipeSize(pieceId, sourceSizeIndex);

                    PipeElement *straightPipe = m_network.find(pieceId);

                    if (!straightPipe)
                    {
                        setStatusMessage("A létrehozott egyenes cső hálózati eleme nem érhető el.");
                        return false;
                    }

                    applyRouteValidationProfileToElement(pieceId);

                    createdRouteIds.push_back(pieceId);

                    m_network.setPipeLength(pieceId, pieceLength);

                    SceneObject straightObject = makeSceneObjectForPipe(*straightPipe);

                    straightObject.transform.position = (pieceStart + pieceEnd) * 0.5f;

                    // Exact axis orientation from the stored build primitive.
                    setObjectRotationFromLocalDirection(straightObject, glm::vec3{0.0f, 0.0f, 1.0f},
                                                        segmentDirection);

                    straightObject.transform.scale = glm::vec3{1.0f};

                    m_objects.push_back(straightObject);

                    m_renderer.uploadObject(m_objects.back());

                    SceneObject *createdStraight = findSceneObject(pieceId);

                    if (!createdStraight)
                    {
                        setStatusMessage("A létrehozott egyenes cső jelenetobjektuma nem érhető el.");
                        return false;
                    }

                    // Anchor endpoint 0 exactly to the previous physical tail.
                    const glm::vec3 actualStart = connectionWorldPosition(pieceId, 0);

                    const glm::vec3 desiredStart = connectionWorldPosition(previousId, previousPoint);

                    createdStraight->transform.position += desiredStart - actualStart;

                    const glm::vec3 actualEnd = connectionWorldPosition(pieceId, 1);

                    // This checks the actual generated object against the
                    // already-approved SEARCH primitive. It does not derive
                    // any new route geometry.
                    if (glm::length(actualEnd - pieceEnd) > 0.006f)
                    {
                        setStatusMessage(
                            "Az eltárolt egyenes csőgeometria létrehozása numerikusan eltért a keresési geometriától.");
                        return false;
                    }

                    m_network.connect(previousId, previousPoint, pieceId, 0);

                    previousId = pieceId;
                    previousPoint = 1;
                    lastCreatedId = pieceId;
                }

                continue;
            }

            const bool use45 = primitive.kind == RouteBuildPrimitiveKind::Elbow45;

            const float primitiveBendRadius = primitive.bendRadiusM > 0.0f ? primitive.bendRadiusM : bendRadius;

            const ObjectId elbowId = use45 ? m_network.addElbow45(primitiveBendRadius, sourceDiameterMm)
                                           : m_network.addElbow90(primitiveBendRadius, sourceDiameterMm);

            m_network.setPipeType(elbowId, sourceTypeIndex);

            m_network.setPipeSize(elbowId, sourceSizeIndex);

            PipeElement *elbowPipe = m_network.find(elbowId);

            if (!elbowPipe)
            {
                setStatusMessage("A létrehozott 45°/90° könyök hálózati eleme nem érhető el.");
                return false;
            }

            applyRouteValidationProfileToElement(elbowId);

            createdRouteIds.push_back(elbowId);

            elbowPipe->bendRadiusM = primitiveBendRadius;
            elbowPipe->lengthM = primitiveBendRadius * primitive.angleRadians;

            SceneObject elbowObject = makeSceneObjectForPipe(*elbowPipe);

            // A könyök szöge továbbra is csak 45° vagy 90°,
            // de a teljes idom szabadon elforgatható 3D-ben.
            // Így a szabványos könyök közvetlenül követheti egy 9°/13°-ban
            // álló csővég térbeli irányát automatikus AlignmentElbow nélkül.
            elbowObject.transform.rotationEuler =
                exactElbowEulerForDirections(primitive.incomingDirection, primitive.outgoingDirection);

            elbowObject.transform.scale = glm::vec3{1.0f};

            m_objects.push_back(elbowObject);

            m_renderer.uploadObject(m_objects.back());

            SceneObject *createdElbow = findSceneObject(elbowId);

            if (!createdElbow)
            {
                setStatusMessage("Nem sikerült elérni a létrehozott könyök objektumát.");
                return false;
            }

            const glm::vec3 elbowStartAfterRotation = connectionWorldPosition(elbowId, 0);

            const glm::vec3 desiredElbowStart = connectionWorldPosition(previousId, previousPoint);

            createdElbow->transform.position += desiredElbowStart - elbowStartAfterRotation;

            const glm::vec3 actualElbowEnd = connectionWorldPosition(elbowId, 1);

            const glm::vec3 actualElbowStartTravel = -glm::normalize(fittingConnectorDirectionWorld(elbowId, 0));

            const glm::vec3 actualElbowEndTravel = glm::normalize(fittingConnectorDirectionWorld(elbowId, 1));

            if (glm::dot(actualElbowStartTravel, glm::normalize(primitive.incomingDirection)) < 0.999f ||
                glm::dot(actualElbowEndTravel, glm::normalize(primitive.outgoingDirection)) < 0.999f)
            {
                setStatusMessage(
                    "A szabványos 45°/90° könyök térbeli iránya eltért az A* által elfogadott iránytól.");
                return false;
            }

            if (glm::length(actualElbowEnd - primitive.endWorld) > 0.015f)
            {
                setStatusMessage(
                    use45
                        ? "A 45° könyök tényleges mesh-geometriája nem tudta lekövetni az útvonalkeresésben elfogadott tangenciapontokat."
                        : "A 90° könyök tényleges mesh-geometriája nem tudta lekövetni az útvonalkeresésben elfogadott tangenciapontokat.");
                return false;
            }

            m_network.connect(previousId, previousPoint, elbowId, 0);

            previousId = elbowId;
            previousPoint = 1;
            lastCreatedId = elbowId;
        }

        // The main A* construction must finish at the exact main-route
        // endpoint already stored during search. We no longer resize or
        // redirect the last straight during build.
        const glm::vec3 builtMainEnd = connectionWorldPosition(previousId, previousPoint);

        if (glm::length(builtMainEnd - selectedAlternative.mainRouteEndWorld) > 0.008f)
        {
            setStatusMessage("A létrehozott fő útvonal vége eltért az útvonalkeresésben elfogadott végponttól.");
            return false;
        }

        ObjectId finalTailId = previousId;
        int finalTailPoint = previousPoint;

        if (hasEndReducer)
        {
            const glm::vec3 reducerStart = selectedAlternative.mainRouteEndWorld;
            const float length = glm::distance(reducerStart, selectedAlternative.endEndpointWorld);
            const ObjectId reducerId = m_network.addReducer(length, sourceDiameterMm, endDiameterMm);
            m_network.setPipeType(reducerId, catalogPlan.reducerType);
            m_network.setPipeSize(reducerId, catalogPlan.reducerInletSize);
            m_network.setReducerSecondarySize(reducerId, catalogPlan.reducerOutletSize);
            const auto *reducer = m_network.find(reducerId);
            SceneObject object = makeSceneObjectForPipe(*reducer);
            object.transform.position = (reducerStart + selectedAlternative.endEndpointWorld) * 0.5f;
            setObjectRotationFromLocalDirection(object, {0.0f, 0.0f, 1.0f}, -currentActualEndOut);
            m_objects.push_back(std::move(object));
            m_renderer.uploadObject(m_objects.back());

            if (glm::distance(connectionWorldPosition(reducerId, 0), builtMainEnd) > 0.003f ||
                glm::distance(connectionWorldPosition(reducerId, 1), currentEndWorld) > 0.003f ||
                glm::dot(glm::normalize(routeEndpointOutwardDirection(finalTailId, finalTailPoint)),
                         currentActualEndOut) > -0.995f)
            {
                setStatusMessage("Az A* END szűkítője nem illeszkedik a végpontokhoz. Futtasd újra a keresést.");
                return false;
            }

            m_network.connect(finalTailId, finalTailPoint, reducerId, 0);
            m_network.connect(reducerId, 1, m_routeEndId, m_routeEndEndpoint);
            createdRouteIds.push_back(reducerId);
            lastCreatedId = reducerId;
        }
        else
        {
            const glm::vec3 finalTailOut =
                glm::normalize(routeEndpointOutwardDirection(finalTailId, finalTailPoint));

            const glm::vec3 targetOut =
                glm::normalize(routeEndpointOutwardDirection(m_routeEndId, m_routeEndEndpoint));

            if (glm::dot(finalTailOut, targetOut) > -0.995f)
            {
                setStatusMessage("Az eltárolt útvonal végső tengelye nem illeszkedik a cél csővéghez.");
                return false;
            }

            m_network.connect(finalTailId, finalTailPoint, m_routeEndId, m_routeEndEndpoint);
        }

        cleanupInvalidConnections();

        if (lastCreatedId != InvalidObjectId) m_selectedId = lastCreatedId;

        const int segmentCount = selectedAlternative.segmentCount;

        const int bend45Count = selectedAlternative.bend45Count;

        const int bend90Count = selectedAlternative.bend90Count;

        const bool startsWithBend = selectedAlternative.startsWithBend;

        const bool endsWithBend = selectedAlternative.endsWithBend;

        int createdStraightCount = 0;
        for (ObjectId id : createdRouteIds)
        {
            const PipeElement *createdPipe = m_network.find(id);

            if (createdPipe && createdPipe->type == PipeElementType::StraightPipe)
            {
                ++createdStraightCount;
            }
        }

        const std::string successMessage =
            "Kiválasztott A* útvonal elkészült | kategória: " + sourceType.name + " | Ø" +
            std::to_string(sourceDiameterMm) + " mm | " + std::to_string(segmentCount) + " geometriai szakasz, " +
            std::to_string(createdStraightCount) + " db egyenes cső (max. 8 m/db), " +
            std::to_string(bend45Count) + " db 45° könyök, " + std::to_string(bend90Count) + " db 90° könyök" +
            (startsWithBend ? ", könyökkel indul" : "") + (endsWithBend ? ", könyökkel érkezik" : "") +
            (hasEndReducer ? ", 1 db szűkítő az END pontnál" : "") + ".";

        m_lastRouteCreatedIds = std::move(createdRouteIds);
        // Az A* kiválasztott kezdő- és végpontja automatikusan
        // a közös validációs START/END útvonal lesz.
        // A nyomáspróba is a közös START elem kapcsolt hálózatrészét használja.
        m_validationStartId = m_routeStartId;
        m_validationStartEndpoint = m_routeStartEndpoint;
        m_validationEndId = m_routeEndId;
        m_validationEndEndpoint = m_routeEndEndpoint;

        m_routeStartId = InvalidObjectId;
        m_routeStartEndpoint = -1;
        m_routeEndId = InvalidObjectId;
        m_routeEndEndpoint = -1;
        m_routePipeTypeIndex = -1;
        m_routeAlternatives.clear();
        m_selectedRouteAlternative = 0;
        clearRoutePreview();
        updateEndpointMarkers();
        setStatusMessage(successMessage);
        return true;
    }

}
