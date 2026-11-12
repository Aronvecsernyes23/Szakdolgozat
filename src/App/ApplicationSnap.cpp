#include "App/ApplicationInternal.hpp"

namespace plumbcad
{
    void Application::setPipeEndpointToWorld(ObjectId objectId, int draggedPointIndex, int anchorPointIndex,
                                             const glm::vec3 &targetWorld, bool keepCurrentLength)
    {
        PipeElement *pipe = m_network.find(objectId);
        SceneObject *object = findSceneObject(objectId);
        if (!pipe || !object) return;
        if (pipe->type != PipeElementType::StraightPipe) return;
        if (draggedPointIndex < 0 || draggedPointIndex >= static_cast<int>(pipe->connections.size())) return;
        if (anchorPointIndex < 0 || anchorPointIndex >= static_cast<int>(pipe->connections.size())) return;
        if (draggedPointIndex == anchorPointIndex) return;

        const glm::vec3 anchorWorld = connectionWorldPosition(objectId, anchorPointIndex);
        glm::vec3 direction = targetWorld - anchorWorld;
        float targetDistance = glm::length(direction);
        if (targetDistance < 0.1f) return;
        direction /= targetDistance;

        const float newLength = keepCurrentLength
                                    ? pipe->lengthM
                                    : std::clamp(targetDistance, MinStraightPipeLengthM, MaxStraightPipeLengthM);

        // A henger lokális tengelye +Z. Ha az 1-es végpontot húzzuk a 0-ás fixponthoz képest,
        // akkor +Z mutat a húzott vég felé. Ellenkező esetben fordítva.
        glm::vec3 localPlusZDirection = direction;
        if (draggedPointIndex == 0 && anchorPointIndex == 1)
        {
            localPlusZDirection = -direction;
        }

        // A korábbi verzió csak Y tengely körüli yaw értéket állított, ezért endpoint-húzáskor
        // a cső azonnal vízszintes síkba ugrott. Itt a +Z lokális csőtengelyt teljes 3D irányba
        // fordítjuk: yaw + pitch is készül, a hossz viszont változatlan maradhat.
        localPlusZDirection = glm::normalize(localPlusZDirection);
        const float yaw = std::asin(std::clamp(localPlusZDirection.x, -1.0f, 1.0f));
        const float pitch = std::atan2(-localPlusZDirection.y, localPlusZDirection.z);
        object->transform.rotationEuler = {pitch, yaw, 0.0f};

        if (!keepCurrentLength)
        {
            m_network.setPipeLength(objectId, newLength);
            rebuildPipeMesh(objectId);
        }

        const glm::vec3 draggedWorld = anchorWorld + direction * newLength;
        object->transform.position = (anchorWorld + draggedWorld) * 0.5f;
    }

    int Application::connectAllNearbyOpenEndpoints(ObjectId objectId, const std::vector<ObjectId> &ignoredIds)
    {
        PipeElement *selectedPipe = m_network.find(objectId);
        if (!selectedPipe) return 0;

        int connectedCount = 0;
        bool changed = true;

        // Több körben futtatjuk, mert egy elemnek több nyitott csatlakozási pontja is
        // közel kerülhet más elemekhez. Egy körben minden még nyitott véghez a legközelebbi
        // kompatibilis nyitott végpontot keressük meg.
        while (changed)
        {
            changed = false;
            selectedPipe = m_network.find(objectId);
            if (!selectedPipe) break;

            for (int selectedPointIndex = 0;
                 selectedPointIndex < static_cast<int>(selectedPipe->connections.size()); ++selectedPointIndex)
            {
                selectedPipe = m_network.find(objectId);
                if (!selectedPipe) break;
                if (selectedPointIndex < 0 ||
                    selectedPointIndex >= static_cast<int>(selectedPipe->connections.size()))
                    continue;
                if (!selectedPipe->connections[selectedPointIndex].isOpen()) continue;

                const glm::vec3 selectedWorld = connectionWorldPosition(objectId, selectedPointIndex);
                ObjectId bestTargetId = InvalidObjectId;
                int bestTargetPoint = -1;
                float bestDistance = SnapDistanceMeters;

                for (const auto &[targetId, targetPipe] : m_network.elements())
                {
                    if (targetId == objectId) continue;
                    if (std::find(ignoredIds.begin(), ignoredIds.end(), targetId) != ignoredIds.end()) continue;

                    for (int targetPointIndex = 0;
                         targetPointIndex < static_cast<int>(targetPipe.connections.size()); ++targetPointIndex)
                    {
                        if (!targetPipe.connections[targetPointIndex].isOpen()) continue;

                        const glm::vec3 targetWorld = connectionWorldPosition(targetId, targetPointIndex);
                        const float distance = glm::length(targetWorld - selectedWorld);
                        if (distance < bestDistance)
                        {
                            bestDistance = distance;
                            bestTargetId = targetId;
                            bestTargetPoint = targetPointIndex;
                        }
                    }
                }

                if (bestTargetId != InvalidObjectId && bestTargetPoint >= 0)
                {
                    m_network.connect(objectId, selectedPointIndex, bestTargetId, bestTargetPoint);
                    ++connectedCount;
                    changed = true;
                }
            }
        }

        return connectedCount;
    }

    int Application::trySnapAfterRotation()
    {
        if (m_selectedId == InvalidObjectId)
        {
            return 0;
        }

        const PipeElement *selectedPipe = m_network.find(m_selectedId);

        if (!selectedPipe)
        {
            return 0;
        }

        const bool connectorPivotFitting =
            (selectedPipe->type == PipeElementType::Elbow90 || selectedPipe->type == PipeElementType::Elbow45 ||
             selectedPipe->type == PipeElementType::Tee) &&
            connectedEndpointCount(m_selectedId) > 0;

        if (connectorPivotFitting)
        {
            int fixedEndpoint = -1;

            if (m_constrainedElbowRotate && m_dragAnchorPointIndex >= 0)
            {
                fixedEndpoint = m_dragAnchorPointIndex;
            }
            else
            {
                const bool selectedFixedEndpointValid =
                    m_fittingRotationFixedEndpoint >= 0 &&
                    m_fittingRotationFixedEndpoint < static_cast<int>(selectedPipe->connections.size()) &&
                    !selectedPipe->connections[m_fittingRotationFixedEndpoint].isOpen();

                fixedEndpoint = selectedFixedEndpointValid ? m_fittingRotationFixedEndpoint
                                                           : firstConnectedEndpoint(m_selectedId);

                m_fittingRotationFixedEndpoint = fixedEndpoint;
            }

            if (fixedEndpoint < 0)
            {
                return 0;
            }

            const glm::vec3 pivotWorld = connectionWorldPosition(m_selectedId, fixedEndpoint);

            glm::vec3 axisWorld = fittingConnectorDirectionWorld(m_selectedId, fixedEndpoint);

            if (glm::length2(axisWorld) < 0.000001f)
            {
                return 0;
            }

            axisWorld = glm::normalize(axisWorld);

            std::vector<ObjectId> constrainedMovingIds =
                (m_constrainedElbowRotate && !m_dragObjectIds.empty())
                    ? m_dragObjectIds
                    : connectedComponentAwayFromFixedEndpoint(m_selectedId, fixedEndpoint);

            if (constrainedMovingIds.empty())
            {
                constrainedMovingIds.push_back(m_selectedId);
            }

            ObjectId bestMovingId = InvalidObjectId;

            int bestMovingPoint = -1;

            ObjectId bestTargetId = InvalidObjectId;

            int bestTargetPoint = -1;

            float bestDistance = SnapDistanceMeters;

            for (ObjectId movingId : constrainedMovingIds)
            {
                const PipeElement *movingPipe = m_network.find(movingId);

                if (!movingPipe) continue;

                for (int movingPointIndex = 0; movingPointIndex < static_cast<int>(movingPipe->connections.size());
                     ++movingPointIndex)
                {
                    if (!movingPipe->connections[movingPointIndex].isOpen())
                    {
                        continue;
                    }

                    const glm::vec3 movingWorld = connectionWorldPosition(movingId, movingPointIndex);

                    for (const auto &[targetId, targetPipe] : m_network.elements())
                    {
                        if (std::find(constrainedMovingIds.begin(), constrainedMovingIds.end(), targetId) !=
                            constrainedMovingIds.end())
                        {
                            continue;
                        }

                        for (int targetPointIndex = 0;
                             targetPointIndex < static_cast<int>(targetPipe.connections.size());
                             ++targetPointIndex)
                        {
                            if (!targetPipe.connections[targetPointIndex].isOpen())
                            {
                                continue;
                            }

                            const glm::vec3 targetWorld = connectionWorldPosition(targetId, targetPointIndex);

                            const float distance = glm::length(targetWorld - movingWorld);

                            if (distance < bestDistance)
                            {
                                bestDistance = distance;

                                bestMovingId = movingId;

                                bestMovingPoint = movingPointIndex;

                                bestTargetId = targetId;

                                bestTargetPoint = targetPointIndex;
                            }
                        }
                    }
                }
            }

            if (bestMovingId == InvalidObjectId || bestTargetId == InvalidObjectId)
            {
                return 0;
            }

            auto endpointOutwardDirection = [&](ObjectId id, int pointIndex) {
                const PipeElement *pipe = m_network.find(id);

                if (!pipe)
                {
                    return glm::vec3{0.0f, 0.0f, 1.0f};
                }

                if (pipe->type == PipeElementType::StraightPipe)
                {
                    glm::vec3 direction = -straightContinuationDirectionWorld(id, pointIndex);

                    if (glm::length2(direction) > 0.000001f)
                    {
                        return glm::normalize(direction);
                    }
                }
                else if (isDirectionalFittingType(pipe->type))
                {
                    glm::vec3 direction = fittingConnectorDirectionWorld(id, pointIndex);

                    if (glm::length2(direction) > 0.000001f)
                    {
                        return glm::normalize(direction);
                    }
                }

                return glm::vec3{0.0f, 0.0f, 1.0f};
            };

            const glm::vec3 currentMovingDirection = endpointOutwardDirection(bestMovingId, bestMovingPoint);

            const glm::vec3 requiredMovingDirection = -endpointOutwardDirection(bestTargetId, bestTargetPoint);

            glm::vec3 projectedCurrent =
                currentMovingDirection - axisWorld * glm::dot(currentMovingDirection, axisWorld);

            glm::vec3 projectedRequired =
                requiredMovingDirection - axisWorld * glm::dot(requiredMovingDirection, axisWorld);

            float correctionAngle = 0.0f;

            if (glm::length2(projectedCurrent) > 0.000001f && glm::length2(projectedRequired) > 0.000001f)
            {
                projectedCurrent = glm::normalize(projectedCurrent);

                projectedRequired = glm::normalize(projectedRequired);

                correctionAngle =
                    std::atan2(glm::dot(axisWorld, glm::cross(projectedCurrent, projectedRequired)),
                               glm::clamp(glm::dot(projectedCurrent, projectedRequired), -1.0f, 1.0f));
            }
            else
            {
                // Ha a csatlakozási irányból nem számítható stabil szög,
                // a végpont pozícióját vetítjük a fix tengelyre merőleges síkra.
                glm::vec3 currentRadius = connectionWorldPosition(bestMovingId, bestMovingPoint) - pivotWorld;

                glm::vec3 targetRadius = connectionWorldPosition(bestTargetId, bestTargetPoint) - pivotWorld;

                currentRadius -= axisWorld * glm::dot(currentRadius, axisWorld);

                targetRadius -= axisWorld * glm::dot(targetRadius, axisWorld);

                if (glm::length2(currentRadius) > 0.000001f && glm::length2(targetRadius) > 0.000001f)
                {
                    currentRadius = glm::normalize(currentRadius);

                    targetRadius = glm::normalize(targetRadius);

                    correctionAngle = std::atan2(glm::dot(axisWorld, glm::cross(currentRadius, targetRadius)),
                                                 glm::clamp(glm::dot(currentRadius, targetRadius), -1.0f, 1.0f));
                }
            }

            if (std::abs(correctionAngle) > 0.0000001f)
            {
                rotateCadObjectGroupByRadians(constrainedMovingIds, pivotWorld, axisWorld, correctionAngle);
            }

            const float finalDistance = glm::length(connectionWorldPosition(bestMovingId, bestMovingPoint) -
                                                    connectionWorldPosition(bestTargetId, bestTargetPoint));

            if (finalDistance > SnapDistanceMeters)
            {
                // A fix oldalt nem tolhatjuk el. Ha a szabad vég csak úgy
                // érné el a célt, hogy a rögzített csatlakozás elmozdul,
                // akkor nem hozunk létre hibás kapcsolatot.
                return 0;
            }

            m_network.connect(bestMovingId, bestMovingPoint, bestTargetId, bestTargetPoint);

            updateEndpointMarkers();

            setStatusMessage("Forgatás kész: a rögzített csatlakozás a helyén maradt, "
                             "a szabad csővég pedig a cél csatlakozási irányához igazodva kapcsolódott.");

            return 1;
        }

        std::vector<ObjectId> movingIds;

        if (m_rotateConnectedGroup)
        {
            movingIds = connectedComponent(m_selectedId);
        }

        if (movingIds.empty())
        {
            movingIds.push_back(m_selectedId);
        }

        // Először csak azt ellenőrizzük, van-e egyáltalán olyan külső,
        // nyitott csővég, amely a normál SnapDistanceMeters távolságon belül van.
        // Maga az igazítás lent a trySnapSelectedPipe(true) rutinra van bízva,
        // tehát PONTOSAN ugyanaz a geometriai logika fut, mint Mozgatás után.
        bool hasNearbyExternalEndpoint = false;

        for (ObjectId movingId : movingIds)
        {
            const PipeElement *movingPipe = m_network.find(movingId);

            if (!movingPipe) continue;

            for (int movingPointIndex = 0; movingPointIndex < static_cast<int>(movingPipe->connections.size());
                 ++movingPointIndex)
            {
                if (!movingPipe->connections[movingPointIndex].isOpen())
                {
                    continue;
                }

                const glm::vec3 movingWorld = connectionWorldPosition(movingId, movingPointIndex);

                for (const auto &[targetId, targetPipe] : m_network.elements())
                {
                    if (std::find(movingIds.begin(), movingIds.end(), targetId) != movingIds.end())
                    {
                        continue;
                    }

                    for (int targetPointIndex = 0;
                         targetPointIndex < static_cast<int>(targetPipe.connections.size()); ++targetPointIndex)
                    {
                        if (!targetPipe.connections[targetPointIndex].isOpen())
                        {
                            continue;
                        }

                        const glm::vec3 targetWorld = connectionWorldPosition(targetId, targetPointIndex);

                        if (glm::length(targetWorld - movingWorld) < SnapDistanceMeters)
                        {
                            hasNearbyExternalEndpoint = true;

                            break;
                        }
                    }

                    if (hasNearbyExternalEndpoint) break;
                }

                if (hasNearbyExternalEndpoint) break;
            }

            if (hasNearbyExternalEndpoint) break;
        }

        if (!hasNearbyExternalEndpoint)
        {
            return 0;
        }

        // A normál mozgatási snap a m_dragObjectIds alapján dönti el,
        // hogy csak egy elemet vagy egy teljes részhálózatot kell mereven
        // igazítania. Forgatás után ideiglenesen ugyanazt a csoportot adjuk át.
        //
        // Ennek eredménye:
        // - idom -> idom: csatlakozási tengelyek egymással szembe igazodnak
        // - idom -> egyenes: az idom a cső tengelyéhez igazodik
        // - egyenes -> idom: az egyenes az idom irányához igazodik
        // - egyenes -> egyenes: tengelyfolytatásba áll
        // - teljes kapcsolt hálózat esetén az egész csoport ugyanazt a merev
        //   transzformációt követi.
        const std::vector<ObjectId> previousDragObjectIds = m_dragObjectIds;

        m_dragObjectIds = movingIds;

        int openExternalConnectionsBefore = 0;

        for (ObjectId movingId : movingIds)
        {
            const PipeElement *pipe = m_network.find(movingId);

            if (!pipe) continue;

            for (const ConnectionPoint &point : pipe->connections)
            {
                if (!point.isOpen() &&
                    std::find(movingIds.begin(), movingIds.end(), point.connectedElementId) == movingIds.end())
                {
                    ++openExternalConnectionsBefore;
                }
            }
        }

        // FONTOS: ugyanaz a függvény, ami Mozgatás elengedésekor is fut.
        // Nem csak összekapcsol, hanem a csatlakozó elemet/csoportot is
        // helyzetben ÉS irányban a cél elemhez igazítja.
        trySnapSelectedPipe(true);

        int openExternalConnectionsAfter = 0;

        for (ObjectId movingId : movingIds)
        {
            const PipeElement *pipe = m_network.find(movingId);

            if (!pipe) continue;

            for (const ConnectionPoint &point : pipe->connections)
            {
                if (!point.isOpen() &&
                    std::find(movingIds.begin(), movingIds.end(), point.connectedElementId) == movingIds.end())
                {
                    ++openExternalConnectionsAfter;
                }
            }
        }

        m_dragObjectIds = previousDragObjectIds;

        const int newConnectionEnds = std::max(0, openExternalConnectionsAfter - openExternalConnectionsBefore);

        if (newConnectionEnds > 0)
        {
            updateEndpointMarkers();

            return newConnectionEnds;
        }

        return 0;
    }

    void Application::trySnapSelectedPipe(bool allowTranslation)
    {
        PipeElement *selectedPipe = m_network.find(m_selectedId);
        SceneObject *selectedObject = findSceneObject(m_selectedId);
        if (!selectedPipe || !selectedObject) return;

        const bool groupSnap =
            allowTranslation && !m_dragObjectIds.empty() &&
            std::find(m_dragObjectIds.begin(), m_dragObjectIds.end(), m_selectedId) != m_dragObjectIds.end();

        const std::vector<ObjectId> ignoredIds =
            m_dragObjectIds.empty() ? std::vector<ObjectId>{m_selectedId} : m_dragObjectIds;
        const std::vector<ObjectId> movingIds = groupSnap ? m_dragObjectIds : std::vector<ObjectId>{m_selectedId};

        ObjectId bestMovingId = InvalidObjectId;
        int bestMovingPoint = -1;
        ObjectId bestTargetId = InvalidObjectId;
        int bestTargetPoint = -1;
        float bestDistance = SnapDistanceMeters;

        // Csoportos mozgatásnál nem csak a kijelölt cső végpontjait vizsgáljuk,
        // hanem a teljes mozgatott részhálózat összes nyitott végpontját.
        // Így az a cső/idom fog rákapcsolódni és ahhoz igazodni, amelyik ténylegesen
        // közel került a másik részhálózat nyitott végéhez.
        for (ObjectId movingId : movingIds)
        {
            const PipeElement *movingPipe = m_network.find(movingId);
            if (!movingPipe) continue;

            for (int movingPointIndex = 0; movingPointIndex < static_cast<int>(movingPipe->connections.size());
                 ++movingPointIndex)
            {
                if (!movingPipe->connections[movingPointIndex].isOpen()) continue;
                const glm::vec3 movingWorld = connectionWorldPosition(movingId, movingPointIndex);

                for (const auto &[targetId, targetPipe] : m_network.elements())
                {
                    if (std::find(ignoredIds.begin(), ignoredIds.end(), targetId) != ignoredIds.end()) continue;

                    for (int targetPointIndex = 0;
                         targetPointIndex < static_cast<int>(targetPipe.connections.size()); ++targetPointIndex)
                    {
                        if (!targetPipe.connections[targetPointIndex].isOpen()) continue;

                        const glm::vec3 targetWorld = connectionWorldPosition(targetId, targetPointIndex);
                        const float distance = glm::length(targetWorld - movingWorld);
                        if (distance < bestDistance)
                        {
                            bestDistance = distance;
                            bestMovingId = movingId;
                            bestMovingPoint = movingPointIndex;
                            bestTargetId = targetId;
                            bestTargetPoint = targetPointIndex;
                        }
                    }
                }
            }
        }

        if (bestMovingId == InvalidObjectId || bestMovingPoint < 0 || bestTargetId == InvalidObjectId) return;

        SceneObject *movingObject = findSceneObject(bestMovingId);
        if (!movingObject) return;

        std::vector<std::pair<ObjectId, glm::mat4>> groupBeforeSnap;
        if (groupSnap)
        {
            for (ObjectId id : m_dragObjectIds)
            {
                if (const SceneObject *object = findSceneObject(id))
                {
                    groupBeforeSnap.push_back({id, object->transform.matrix()});
                }
            }
        }

        const glm::mat4 movingBeforeSnap = movingObject->transform.matrix();

        const glm::vec3 movingWorld = connectionWorldPosition(bestMovingId, bestMovingPoint);
        const glm::vec3 targetWorld = connectionWorldPosition(bestTargetId, bestTargetPoint);
        const glm::vec3 snapDelta = targetWorld - movingWorld;

        if (allowTranslation)
        {
            if (groupSnap)
            {
                // Csoportos snap-nél először csak azt az elemet mozgatjuk/igazítjuk,
                // amelyik ténylegesen csatlakozik. Ezután kiszámoljuk ennek teljes
                // transzformációját, és ugyanazt visszük rá a teljes részhálózatra.
                movingObject->transform.position += snapDelta;
            }
            else if (!m_dragObjectIds.empty())
            {
                translateObjects(m_dragObjectIds, snapDelta);
            }
            else
            {
                movingObject->transform.position += snapDelta;
            }
        }
        else if (m_dragHasAnchor && bestMovingId == m_selectedId && bestMovingPoint != m_dragAnchorPointIndex)
        {
            setPipeEndpointToWorld(bestMovingId, bestMovingPoint, m_dragAnchorPointIndex, targetWorld, false);
        }

        m_network.connect(bestMovingId, bestMovingPoint, bestTargetId, bestTargetPoint);

        const PipeElement *movingAfterConnect = m_network.find(bestMovingId);
        const PipeElement *targetAfterConnect = m_network.find(bestTargetId);

        if (movingAfterConnect && targetAfterConnect && isDirectionalFittingType(movingAfterConnect->type) &&
            isDirectionalFittingType(targetAfterConnect->type))
        {
            alignElbowToElbow(bestMovingId, bestMovingPoint, bestTargetId, bestTargetPoint);
            enforceElbowStraightConstraints();
            setStatusMessage(
                groupSnap
                    ? "Snap kész: a részhálózat kapcsolódó idomja igazodott, és a teljes részhálózat követte."
                    : "Snap kész: a könyökidom a másik könyök csatlakozási tengelyéhez igazítva kapcsolódott.");
        }
        else if (movingAfterConnect && targetAfterConnect && isDirectionalFittingType(movingAfterConnect->type) &&
                 targetAfterConnect->type == PipeElementType::StraightPipe)
        {
            alignElbowToStraight(bestMovingId, bestMovingPoint, bestTargetId, bestTargetPoint);
            enforceElbowStraightConstraints();
            setStatusMessage(
                groupSnap
                    ? "Snap kész: a részhálózat kapcsolódó idomja az egyenes cső tengelyéhez igazodott, és a teljes részhálózat követte."
                    : "Snap kész: a könyökidom az egyenes cső tengelyéhez igazítva kapcsolódott.");
        }
        else if (movingAfterConnect && targetAfterConnect &&
                 movingAfterConnect->type == PipeElementType::StraightPipe &&
                 isDirectionalFittingType(targetAfterConnect->type))
        {
            alignStraightPipeToElbow(bestMovingId);
            setStatusMessage(
                groupSnap
                    ? "Snap kész: a részhálózat kapcsolódó egyenes csöve az idom csatlakozási irányához igazodott, és a teljes részhálózat követte."
                    : "Snap kész: az egyenes cső a könyökidom csatlakozási irányához igazítva kapcsolódott.");
        }
        else if (movingAfterConnect && targetAfterConnect &&
                 movingAfterConnect->type == PipeElementType::StraightPipe &&
                 targetAfterConnect->type == PipeElementType::StraightPipe)
        {
            if (groupSnap)
            {
                // Csoportos mozgatás + egyenes-egyenes kapcsolat esetén a teljes
                // részhálózatot kell a cél cső tengelyéhez igazítani, nem csak a
                // csatlakozó egyenes csövet. Ezért itt közvetlenül kiszámoljuk azt
                // a merev transzformációt, amely a kapcsolódó cső jelenlegi irányát
                // a cél cső folytatási irányába forgatja, majd a kapcsolódó végpontot
                // pontosan a cél végpontra teszi.
                glm::vec3 currentDirection = straightContinuationDirectionWorld(bestMovingId, bestMovingPoint);
                glm::vec3 requiredDirection = -straightContinuationDirectionWorld(bestTargetId, bestTargetPoint);

                if (glm::length2(currentDirection) < 0.000001f) currentDirection = glm::vec3{0.0f, 0.0f, 1.0f};
                if (glm::length2(requiredDirection) < 0.000001f) requiredDirection = glm::vec3{0.0f, 0.0f, 1.0f};

                currentDirection = glm::normalize(currentDirection);
                requiredDirection = glm::normalize(requiredDirection);

                const glm::quat rotation = glm::rotation(currentDirection, requiredDirection);
                const glm::mat4 groupAlignment = glm::translate(glm::mat4{1.0f}, targetWorld) *
                                                 glm::mat4_cast(rotation) *
                                                 glm::translate(glm::mat4{1.0f}, -movingWorld);

                for (const auto &[id, beforeMatrix] : groupBeforeSnap)
                {
                    applyWorldMatrixToObject(id, groupAlignment * beforeMatrix);
                }

                setStatusMessage(
                    "Snap kész: a teljes részhálózat az egyenes célcső tengelyének folytatásába igazodott.");
            }
            else
            {
                alignStraightPipeToStraight(bestMovingId, bestMovingPoint, bestTargetId, bestTargetPoint);
                enforceElbowStraightConstraints();
                setStatusMessage(
                    "Snap kész: az egyenes cső kizárólag a másik egyenes cső tengelyének folytatásába igazítva kapcsolódott.");
            }
        }
        else
        {
            enforceElbowStraightConstraints();
            setStatusMessage(
                groupSnap
                    ? "Snap kész: a részhálózat kapcsolódott és a teljes csoport együtt mozdult."
                    : (allowTranslation
                           ? "Snap kész: csővég pontosan összekapcsolva."
                           : "Snap kész: a közeli csővégek logikailag összekapcsolva, a rögzített vég a helyén maradt."));
        }

        if (groupSnap)
        {
            const bool straightToStraightGroupSnap = movingAfterConnect && targetAfterConnect &&
                                                     movingAfterConnect->type == PipeElementType::StraightPipe &&
                                                     targetAfterConnect->type == PipeElementType::StraightPipe;

            if (!straightToStraightGroupSnap)
            {
                const glm::mat4 movingAfterSnap = movingObject->transform.matrix();
                const glm::mat4 groupDelta = movingAfterSnap * glm::inverse(movingBeforeSnap);

                for (const auto &[id, beforeMatrix] : groupBeforeSnap)
                {
                    if (id == bestMovingId) continue;
                    const glm::mat4 newMatrix = groupDelta * beforeMatrix;
                    applyWorldMatrixToObject(id, newMatrix);
                }
            }
        }

        int extraConnections = 0;
        if (groupSnap)
        {
            for (ObjectId id : m_dragObjectIds)
            {
                extraConnections += connectAllNearbyOpenEndpoints(id, ignoredIds);
            }
        }
        else
        {
            extraConnections = connectAllNearbyOpenEndpoints(bestMovingId, ignoredIds);
        }

        if (extraConnections > 0)
        {
            enforceElbowStraightConstraints();
            updateEndpointMarkers();
            setStatusMessage(
                groupSnap
                    ? "Snap kész: a mozgatott részhálózat igazodott és minden közeli, illeszthető ponthoz kapcsolódott. Extra kapcsolatok: " +
                          std::to_string(extraConnections) + "."
                    : "Snap kész: az elem minden közeli, illeszthető csatlakozási ponthoz kapcsolódott. Extra kapcsolatok: " +
                          std::to_string(extraConnections) + ".");
        }
    }
}
