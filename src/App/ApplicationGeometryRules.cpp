#include "App/ApplicationInternal.hpp"

namespace plumbcad
{
    bool Application::findElbowConnection(ObjectId straightPipeId, int &straightPointIndex, ObjectId &elbowId,
                                          int &elbowPointIndex) const
    {
        straightPointIndex = -1;
        elbowId = InvalidObjectId;
        elbowPointIndex = -1;

        const PipeElement *straight = m_network.find(straightPipeId);
        if (!straight || straight->type != PipeElementType::StraightPipe) return false;

        for (int i = 0; i < static_cast<int>(straight->connections.size()); ++i)
        {
            const ConnectionPoint &point = straight->connections[i];
            if (point.isOpen()) continue;

            const PipeElement *other = m_network.find(point.connectedElementId);
            if (!other || !isDirectionalFittingType(other->type)) continue;
            if (point.connectedPointIndex < 0 ||
                point.connectedPointIndex >= static_cast<int>(other->connections.size()))
                continue;

            straightPointIndex = i;
            elbowId = point.connectedElementId;
            elbowPointIndex = point.connectedPointIndex;
            return true;
        }

        return false;
    }

    bool Application::isDirectionalFittingType(PipeElementType type) const
    {
        return type == PipeElementType::Elbow90 || type == PipeElementType::Elbow45 ||
               type == PipeElementType::AlignmentElbow || type == PipeElementType::Tee ||
               type == PipeElementType::Reducer || type == PipeElementType::Valve ||
               type == PipeElementType::EndCap;
    }

    float Application::elbowAngleRadians(const PipeElement &element) const
    {
        if (element.type == PipeElementType::Elbow45)
        {
            return 0.78539816339f;
        }

        if (element.type == PipeElementType::AlignmentElbow)
        {
            return std::clamp(element.lengthM / std::max(element.bendRadiusM, 0.1f), 0.001f, 3.12413936107f);
        }

        return 1.57079632679f;
    }

    glm::vec3 Application::elbowConnectorDirectionLocal(const PipeElement &element, int elbowPointIndex) const
    {
        if (elbowPointIndex == 0)
        {
            return glm::vec3{0.0f, 0.0f, -1.0f};
        }

        const float angle = elbowAngleRadians(element);

        return glm::normalize(glm::vec3{-std::sin(angle), 0.0f, std::cos(angle)});
    }

    glm::vec3 Application::fittingConnectorDirectionLocal(const PipeElement &element, int pointIndex) const
    {
        if (element.type == PipeElementType::Tee)
        {
            if (pointIndex == 0) return glm::vec3{0.0f, 0.0f, -1.0f};

            if (pointIndex == 1) return glm::vec3{0.0f, 0.0f, 1.0f};

            return glm::vec3{1.0f, 0.0f, 0.0f};
        }

        if (element.type == PipeElementType::Reducer || element.type == PipeElementType::Valve ||
            element.type == PipeElementType::EndCap)
        {
            return pointIndex == 0 ? glm::vec3{0.0f, 0.0f, -1.0f} : glm::vec3{0.0f, 0.0f, 1.0f};
        }

        return elbowConnectorDirectionLocal(element, pointIndex);
    }

    glm::vec3 Application::fittingConnectorDirectionWorld(ObjectId fittingId, int pointIndex) const
    {
        const SceneObject *object = findSceneObject(fittingId);
        const PipeElement *fitting = m_network.find(fittingId);
        if (!object || !fitting || !isDirectionalFittingType(fitting->type)) return glm::vec3{0.0f, 0.0f, 1.0f};

        glm::vec3 localDirection = fittingConnectorDirectionLocal(*fitting, pointIndex);
        glm::vec3 worldDirection = glm::vec3(object->transform.matrix() * glm::vec4(localDirection, 0.0f));
        if (glm::length2(worldDirection) < 0.000001f) return glm::vec3{0.0f, 0.0f, 1.0f};
        return glm::normalize(worldDirection);
    }

    glm::vec3 Application::straightContinuationDirectionWorld(ObjectId straightPipeId,
                                                              int straightPointIndex) const
    {
        const PipeElement *straight = m_network.find(straightPipeId);
        if (!straight || straight->type != PipeElementType::StraightPipe) return glm::vec3{0.0f, 0.0f, 1.0f};
        if (straightPointIndex < 0 || straightPointIndex >= static_cast<int>(straight->connections.size()))
            return glm::vec3{0.0f, 0.0f, 1.0f};

        const int otherPointIndex = straightPointIndex == 0 ? 1 : 0;
        glm::vec3 direction = connectionWorldPosition(straightPipeId, otherPointIndex) -
                              connectionWorldPosition(straightPipeId, straightPointIndex);
        if (glm::length2(direction) < 0.000001f) return glm::vec3{0.0f, 0.0f, 1.0f};
        return glm::normalize(direction);
    }

    void Application::setObjectRotationFromLocalDirection(SceneObject &object, const glm::vec3 &localDirection,
                                                          const glm::vec3 &worldDirection)
    {
        glm::vec3 from = glm::normalize(localDirection);
        glm::vec3 to = glm::normalize(worldDirection);
        if (glm::length2(from) < 0.000001f || glm::length2(to) < 0.000001f) return;

        glm::quat q = glm::rotation(from, to);
        glm::mat4 rot = glm::mat4_cast(q);

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        glm::extractEulerAngleXYZ(rot, x, y, z);
        object.transform.rotationEuler = {x, y, z};
    }

    std::vector<ObjectId> Application::connectedComponentAwayFromFixedEndpoint(ObjectId objectId,
                                                                               int fixedPointIndex) const
    {
        std::vector<ObjectId> result;
        const PipeElement *root = m_network.find(objectId);
        if (objectId == InvalidObjectId || !root) return result;

        result.push_back(objectId);
        std::vector<ObjectId> stack;

        // A fix csatlakozás felőli hálózatrészt nem mozgatjuk.
        // A többi csatlakozott ágon lévő elemek viszont együtt fordulnak az idommal,
        // hogy a kapcsolat ne szakadjon szét vizuálisan.
        for (int i = 0; i < static_cast<int>(root->connections.size()); ++i)
        {
            if (i == fixedPointIndex) continue;
            const auto &point = root->connections[i];
            if (!point.isOpen()) stack.push_back(point.connectedElementId);
        }

        while (!stack.empty())
        {
            const ObjectId current = stack.back();
            stack.pop_back();

            if (current == InvalidObjectId) continue;
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

    void Application::rotateObjectRigidAroundWorldAxis(ObjectId objectId, const glm::vec3 &anchorWorld,
                                                       const glm::vec3 &axisWorld, float angleRadians)
    {
        SceneObject *object = findSceneObject(objectId);
        if (!object) return;

        glm::vec3 axis = glm::normalize(axisWorld);
        if (glm::length2(axis) < 0.000001f) return;

        const glm::mat4 currentModel = object->transform.matrix();
        const glm::mat4 worldRotation = glm::translate(glm::mat4{1.0f}, anchorWorld) *
                                        glm::rotate(glm::mat4{1.0f}, angleRadians, axis) *
                                        glm::translate(glm::mat4{1.0f}, -anchorWorld);
        const glm::mat4 newModel = worldRotation * currentModel;

        object->transform.position = glm::vec3(newModel[3]);

        glm::mat4 rotationOnly = newModel;
        rotationOnly[3] = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        glm::extractEulerAngleXYZ(rotationOnly, x, y, z);
        object->transform.rotationEuler = {x, y, z};
        object->transform.scale = {1.0f, 1.0f, 1.0f};
    }

    void Application::rotateConnectedAssemblyAroundFixedEndpoint(ObjectId objectId, int fixedPointIndex,
                                                                 float angleRadians)
    {
        const PipeElement *pipe = m_network.find(objectId);
        if (!pipe) return;
        if (fixedPointIndex < 0 || fixedPointIndex >= static_cast<int>(pipe->connections.size())) return;

        glm::vec3 anchorWorld = connectionWorldPosition(objectId, fixedPointIndex);
        glm::vec3 axisWorld = fittingConnectorDirectionWorld(objectId, fixedPointIndex);
        if (glm::length2(axisWorld) < 0.000001f) axisWorld = glm::vec3{0.0f, 1.0f, 0.0f};

        std::vector<ObjectId> group = connectedComponentAwayFromFixedEndpoint(objectId, fixedPointIndex);
        if (group.empty()) group.push_back(objectId);

        for (ObjectId id : group)
        {
            rotateObjectRigidAroundWorldAxis(id, anchorWorld, axisWorld, angleRadians);
        }

        // Biztonsági korrekció: a választott fix csatlakozás pontosan a helyén maradjon.
        const glm::vec3 anchorAfter = connectionWorldPosition(objectId, fixedPointIndex);
        const glm::vec3 correction = anchorWorld - anchorAfter;
        if (glm::length2(correction) > 0.0000001f)
        {
            translateObjects(group, correction);
        }
    }

    glm::vec3 Application::cadRotationAxisWorld() const
    {
        switch (m_rotateAxisConstraint)
        {
        case RotateAxisConstraint::X:
            return glm::vec3{1.0f, 0.0f, 0.0f};

        case RotateAxisConstraint::Z:
            return glm::vec3{0.0f, 0.0f, 1.0f};

        case RotateAxisConstraint::Y:
        default:
            return glm::vec3{0.0f, 1.0f, 0.0f};
        }
    }

    float Application::currentCadRotationDegrees() const
    {
        const SceneObject *selectedObject = findSceneObject(m_selectedId);

        if (!selectedObject)
        {
            return 0.0f;
        }

        auto normalizeSignedDegrees = [](float degrees) {
            float normalized = std::remainder(degrees, 360.0f);

            if (normalized <= -180.0f)
            {
                normalized += 360.0f;
            }
            else if (normalized > 180.0f)
            {
                normalized -= 360.0f;
            }

            return normalized;
        };

        const PipeElement *selectedPipe = m_network.find(m_selectedId);

        const bool connectorPivotFitting =
            selectedPipe &&
            (selectedPipe->type == PipeElementType::Elbow90 || selectedPipe->type == PipeElementType::Elbow45 ||
             selectedPipe->type == PipeElementType::Tee) &&
            connectedEndpointCount(m_selectedId) > 0;

        if (connectorPivotFitting)
        {
            const bool selectedFixedEndpointValid =
                m_fittingRotationFixedEndpoint >= 0 &&
                m_fittingRotationFixedEndpoint < static_cast<int>(selectedPipe->connections.size()) &&
                !selectedPipe->connections[m_fittingRotationFixedEndpoint].isOpen();

            const int fixedEndpoint =
                selectedFixedEndpointValid ? m_fittingRotationFixedEndpoint : firstConnectedEndpoint(m_selectedId);

            if (fixedEndpoint >= 0)
            {
                glm::vec3 axisWorld = fittingConnectorDirectionWorld(m_selectedId, fixedEndpoint);

                if (glm::length2(axisWorld) > 0.000001f)
                {
                    axisWorld = glm::normalize(axisWorld);

                    // A 90°/45°/T-idom alapgeometriája lokálisan az XZ síkban van,
                    // ezért a lokális +Y a fitting síkjának normálisa.
                    // Ebből egy valódi, kiválasztástól független abszolút "roll"
                    // számolható a fix csatlakozás tengelye körül.
                    glm::vec3 fittingPlaneNormalWorld =
                        glm::vec3(selectedObject->transform.matrix() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f});

                    fittingPlaneNormalWorld -= axisWorld * glm::dot(fittingPlaneNormalWorld, axisWorld);

                    // 0° referencia:
                    // elsődlegesen a világ +Y (függőleges), ezért vízszintes
                    // csatlakozási tengelynél 0° = vízszintes idomsík.
                    glm::vec3 zeroReference = glm::vec3{0.0f, 1.0f, 0.0f};

                    zeroReference -= axisWorld * glm::dot(zeroReference, axisWorld);

                    // Függőleges csatlakozási tengelynél a +Y nem használható
                    // referenciának; ilyenkor a világ +X, majd +Z a tartalék.
                    if (glm::length2(zeroReference) < 0.000001f)
                    {
                        zeroReference = glm::vec3{1.0f, 0.0f, 0.0f};

                        zeroReference -= axisWorld * glm::dot(zeroReference, axisWorld);
                    }

                    if (glm::length2(zeroReference) < 0.000001f)
                    {
                        zeroReference = glm::vec3{0.0f, 0.0f, 1.0f};

                        zeroReference -= axisWorld * glm::dot(zeroReference, axisWorld);
                    }

                    if (glm::length2(fittingPlaneNormalWorld) > 0.000001f &&
                        glm::length2(zeroReference) > 0.000001f)
                    {
                        fittingPlaneNormalWorld = glm::normalize(fittingPlaneNormalWorld);

                        zeroReference = glm::normalize(zeroReference);

                        const float sinAngle =
                            glm::dot(axisWorld, glm::cross(zeroReference, fittingPlaneNormalWorld));

                        const float cosAngle =
                            glm::clamp(glm::dot(zeroReference, fittingPlaneNormalWorld), -1.0f, 1.0f);

                        constexpr float RadToDeg = 57.29577951308232f;

                        return normalizeSignedDegrees(std::atan2(sinAngle, cosAngle) * RadToDeg);
                    }
                }
            }
        }

        // Szabad/world-axis forgatásnál az aktuálisan kiválasztott globális
        // X/Y/Z komponens a célérték alapja. Így újrakijelölés után sem nulláról
        // indul a kézi "Kívánt szög".
        constexpr float RadToDeg = 57.29577951308232f;

        float radians = selectedObject->transform.rotationEuler.y;

        switch (m_rotateAxisConstraint)
        {
        case RotateAxisConstraint::X:
            radians = selectedObject->transform.rotationEuler.x;
            break;

        case RotateAxisConstraint::Z:
            radians = selectedObject->transform.rotationEuler.z;
            break;

        case RotateAxisConstraint::Y:
        default:
            break;
        }

        return normalizeSignedDegrees(radians * RadToDeg);
    }

    bool Application::rotateCadObjectGroupByRadians(const std::vector<ObjectId> &objectIds,
                                                    const glm::vec3 &pivotWorld, const glm::vec3 &axisWorld,
                                                    float angleRadians)
    {
        if (objectIds.empty() || std::abs(angleRadians) < 0.0000001f)
        {
            return false;
        }

        bool rotatedAny = false;

        for (ObjectId id : objectIds)
        {
            if (!findSceneObject(id)) continue;

            rotateObjectRigidAroundWorldAxis(id, pivotWorld, axisWorld, angleRadians);

            rotatedAny = true;
        }

        if (rotatedAny)
        {
            // A kapcsolat-gráfot szándékosan nem módosítjuk.
            // Ha minden kapcsolt elem ugyanazzal a merev transzformációval
            // forog, a közös csatlakozási pontok geometriailag is együtt maradnak.
            updateEndpointMarkers();
        }

        return rotatedAny;
    }

    bool Application::rotateCadSelectionByRadians(float angleRadians)
    {
        if (std::abs(angleRadians) < 0.0000001f)
        {
            return false;
        }

        SceneObject *selectedObject = findSceneObject(m_selectedId);

        if (!selectedObject) return false;

        const PipeElement *selectedPipe = m_network.find(m_selectedId);

        const bool connectorPivotFitting =
            selectedPipe &&
            (selectedPipe->type == PipeElementType::Elbow90 || selectedPipe->type == PipeElementType::Elbow45 ||
             selectedPipe->type == PipeElementType::Tee) &&
            connectedEndpointCount(m_selectedId) > 0;

        if (connectorPivotFitting)
        {
            const bool selectedFixedEndpointValid =
                m_fittingRotationFixedEndpoint >= 0 &&
                m_fittingRotationFixedEndpoint < static_cast<int>(selectedPipe->connections.size()) &&
                !selectedPipe->connections[m_fittingRotationFixedEndpoint].isOpen();

            const int fixedEndpoint =
                selectedFixedEndpointValid ? m_fittingRotationFixedEndpoint : firstConnectedEndpoint(m_selectedId);

            m_fittingRotationFixedEndpoint = fixedEndpoint;

            if (fixedEndpoint >= 0)
            {
                const glm::vec3 pivotWorld = connectionWorldPosition(m_selectedId, fixedEndpoint);

                glm::vec3 axisWorld = fittingConnectorDirectionWorld(m_selectedId, fixedEndpoint);

                if (glm::length2(axisWorld) < 0.000001f)
                {
                    axisWorld = glm::vec3{0.0f, 1.0f, 0.0f};
                }

                axisWorld = glm::normalize(axisWorld);

                std::vector<ObjectId> group = connectedComponentAwayFromFixedEndpoint(m_selectedId, fixedEndpoint);

                if (group.empty())
                {
                    group.push_back(m_selectedId);
                }

                // A fix csatlakozás céloldala nem forog.
                // Ez a szabály független attól, hogy ott egyenes cső vagy idom van.
                return rotateCadObjectGroupByRadians(group, pivotWorld, axisWorld, angleRadians);
            }
        }

        const glm::vec3 pivotWorld = selectedObject->transform.position;

        const glm::vec3 axisWorld = cadRotationAxisWorld();

        std::vector<ObjectId> group;

        if (m_rotateConnectedGroup && m_network.find(m_selectedId))
        {
            group = connectedComponent(m_selectedId);
        }

        if (group.empty())
        {
            group.push_back(m_selectedId);
        }

        if (!m_rotateConnectedGroup && m_network.find(m_selectedId) && hasConnectedEndpoint(m_selectedId))
        {
            m_network.disconnectAll(m_selectedId);
        }

        return rotateCadObjectGroupByRadians(group, pivotWorld, axisWorld, angleRadians);
    }

    bool Application::applyCadRotationDegrees(float degrees)
    {
        if (m_selectedId == InvalidObjectId)
        {
            setStatusMessage("Forgatás sikertelen: nincs kijelölt objektum.");

            return false;
        }

        if (std::abs(degrees) < 0.0001f)
        {
            setStatusMessage("Forgatás: a megadott szög 0°.");

            return false;
        }

        constexpr float DegToRad = 0.01745329251994329577f;

        if (!rotateCadSelectionByRadians(degrees * DegToRad))
        {
            setStatusMessage("Forgatás sikertelen.");

            return false;
        }

        const PipeElement *selectedPipeAfterRotation = m_network.find(m_selectedId);

        const bool connectorPivotRotation = selectedPipeAfterRotation &&
                                            (selectedPipeAfterRotation->type == PipeElementType::Elbow90 ||
                                             selectedPipeAfterRotation->type == PipeElementType::Elbow45 ||
                                             selectedPipeAfterRotation->type == PipeElementType::Tee) &&
                                            connectedEndpointCount(m_selectedId) > 0;

        const char *axisName = connectorPivotRotation ? "csatlakozási tengely" : "Y";

        switch (m_rotateAxisConstraint)
        {
        case RotateAxisConstraint::X:
            if (!connectorPivotRotation) axisName = "X";
            break;

        case RotateAxisConstraint::Z:
            if (!connectorPivotRotation) axisName = "Z";
            break;

        case RotateAxisConstraint::Y:
        default:
            break;
        }

        const int rotationSnapConnections = trySnapAfterRotation();

        if (rotationSnapConnections <= 0)
        {
            setStatusMessage("Pontos CAD forgatás alkalmazva: " + std::to_string(degrees) +
                             "° | tengely: " + axisName +
                             (m_rotateConnectedGroup ? " | kapcsolt hálózatrész együtt forgatva."
                                                     : " | csak a kijelölt elem forgatva."));
        }

        return true;
    }

    void Application::alignStraightPipeToStraight(ObjectId movingStraightId, int movingPointIndex,
                                                  ObjectId targetStraightId, int targetPointIndex)
    {
        PipeElement *movingPipe = m_network.find(movingStraightId);
        SceneObject *movingObject = findSceneObject(movingStraightId);
        const PipeElement *targetPipe = m_network.find(targetStraightId);
        if (!movingPipe || !movingObject || !targetPipe) return;
        if (movingPipe->type != PipeElementType::StraightPipe || targetPipe->type != PipeElementType::StraightPipe)
            return;
        if (movingPointIndex < 0 || movingPointIndex >= static_cast<int>(movingPipe->connections.size())) return;
        if (targetPointIndex < 0 || targetPointIndex >= static_cast<int>(targetPipe->connections.size())) return;

        const glm::vec3 anchorWorld = connectionWorldPosition(targetStraightId, targetPointIndex);

        // A cél egyenes cső adott végpontjánál kifelé mutató irány.
        // A rákapcsolt csőnek ugyanebbe az egyenes tengelybe kell esnie,
        // és a célcsőtől kifelé kell folytatódnia, nem tetszőleges szögben.
        // A straightContinuationDirectionWorld() a cél végponttól a cső másik vége felé mutat,
        // nekünk viszont a célcső végpontjából kifelé, a cső folytatásának irányába kell menni.
        // Ezért itt az ellentétes irányt használjuk.
        glm::vec3 outwardDirection = -straightContinuationDirectionWorld(targetStraightId, targetPointIndex);
        if (glm::length2(outwardDirection) < 0.000001f) outwardDirection = glm::vec3{0.0f, 0.0f, 1.0f};

        // A henger lokális tengelye +Z.
        // Ha a mozgó cső 0. végpontja csatlakozik, akkor a +Z irány a szabad vég felé mutasson.
        // Ha az 1. végpont csatlakozik, akkor a +Z irány ennek ellentéte.
        glm::vec3 localPlusZDirection = (movingPointIndex == 0) ? outwardDirection : -outwardDirection;
        localPlusZDirection = glm::normalize(localPlusZDirection);

        const float yaw = std::asin(std::clamp(localPlusZDirection.x, -1.0f, 1.0f));
        const float pitch = std::atan2(-localPlusZDirection.y, localPlusZDirection.z);
        movingObject->transform.rotationEuler = {pitch, yaw, 0.0f};
        movingObject->transform.scale = {1.0f, 1.0f, 1.0f};

        // A kapcsolt végpont pontosan a célcső végén marad, a cső többi része pedig
        // kizárólag a célcső tengelyének folytatásába kerül.
        movingObject->transform.position = anchorWorld + outwardDirection * (movingPipe->lengthM * 0.5f);
    }

    void Application::alignElbowToStraight(ObjectId elbowId, int elbowPointIndex, ObjectId straightPipeId,
                                           int straightPointIndex)
    {
        PipeElement *elbow = m_network.find(elbowId);
        SceneObject *elbowObject = findSceneObject(elbowId);
        const PipeElement *straight = m_network.find(straightPipeId);
        if (!elbow || !elbowObject || !straight) return;
        if (!isDirectionalFittingType(elbow->type) || straight->type != PipeElementType::StraightPipe) return;
        if (elbowPointIndex < 0 || elbowPointIndex >= static_cast<int>(elbow->connections.size())) return;

        const glm::vec3 anchorWorld = connectionWorldPosition(straightPipeId, straightPointIndex);
        const glm::vec3 targetDirection = straightContinuationDirectionWorld(straightPipeId, straightPointIndex);

        // A könyök lokális csatlakozási irányát az egyenes cső tengelyéhez igazítjuk.
        // Így akkor is helyes lesz az irány, ha nem az egyenes csövet húzod a könyökre,
        // hanem a 90° idomot snap-eled rá egy már meglévő egyenes csővégre.
        const glm::vec3 elbowLocalDirection = fittingConnectorDirectionLocal(*elbow, elbowPointIndex);

        setObjectRotationFromLocalDirection(*elbowObject, elbowLocalDirection, targetDirection);
        elbowObject->transform.scale = {1.0f, 1.0f, 1.0f};

        // A forgatás után az adott könyök-végpontot pontosan az egyenes csővégre tesszük.
        const glm::vec3 elbowPointAfterRotation = connectionWorldPosition(elbowId, elbowPointIndex);
        elbowObject->transform.position += anchorWorld - elbowPointAfterRotation;
    }

    void Application::alignElbowToElbow(ObjectId movingElbowId, int movingElbowPointIndex, ObjectId targetElbowId,
                                        int targetElbowPointIndex)
    {
        PipeElement *movingElbow = m_network.find(movingElbowId);
        SceneObject *movingObject = findSceneObject(movingElbowId);
        const PipeElement *targetElbow = m_network.find(targetElbowId);
        if (!movingElbow || !movingObject || !targetElbow) return;
        if (!isDirectionalFittingType(movingElbow->type) || !isDirectionalFittingType(targetElbow->type)) return;
        if (movingElbowPointIndex < 0 ||
            movingElbowPointIndex >= static_cast<int>(movingElbow->connections.size()))
            return;
        if (targetElbowPointIndex < 0 ||
            targetElbowPointIndex >= static_cast<int>(targetElbow->connections.size()))
            return;

        const glm::vec3 anchorWorld = connectionWorldPosition(targetElbowId, targetElbowPointIndex);

        // Könyök-könyök kapcsolatnál a két csatlakozási tengelynek egy egyenesbe kell esnie.
        // Mivel mindkét irány a saját idomból kifelé mutat, egymással ellentétes irányba állítjuk őket.
        const glm::vec3 targetOutwardDirection =
            fittingConnectorDirectionWorld(targetElbowId, targetElbowPointIndex);
        const glm::vec3 requiredMovingDirection = -targetOutwardDirection;
        const glm::vec3 movingLocalDirection = fittingConnectorDirectionLocal(*movingElbow, movingElbowPointIndex);

        setObjectRotationFromLocalDirection(*movingObject, movingLocalDirection, requiredMovingDirection);
        movingObject->transform.scale = {1.0f, 1.0f, 1.0f};

        // A kiválasztott könyök csatlakozási pontja pontosan a cél könyök végpontjára kerül.
        const glm::vec3 movingPointAfterRotation = connectionWorldPosition(movingElbowId, movingElbowPointIndex);
        movingObject->transform.position += anchorWorld - movingPointAfterRotation;
    }

    void Application::alignStraightPipeToElbow(ObjectId straightPipeId)
    {
        PipeElement *straight = m_network.find(straightPipeId);
        SceneObject *straightObject = findSceneObject(straightPipeId);
        if (!straight || !straightObject || straight->type != PipeElementType::StraightPipe) return;

        int straightPointIndex = -1;
        ObjectId elbowId = InvalidObjectId;
        int elbowPointIndex = -1;
        if (!findElbowConnection(straightPipeId, straightPointIndex, elbowId, elbowPointIndex)) return;

        const glm::vec3 anchorWorld = connectionWorldPosition(elbowId, elbowPointIndex);
        const glm::vec3 outwardDirection = fittingConnectorDirectionWorld(elbowId, elbowPointIndex);

        // A henger lokális tengelye +Z. Ha a 0. végpont csatlakozik a könyökhöz,
        // a +Z irány kifelé mutat. Ha az 1. végpont csatlakozik, akkor fordítva.
        glm::vec3 localPlusZDirection = (straightPointIndex == 0) ? outwardDirection : -outwardDirection;
        localPlusZDirection = glm::normalize(localPlusZDirection);

        const float yaw = std::asin(std::clamp(localPlusZDirection.x, -1.0f, 1.0f));
        const float pitch = std::atan2(-localPlusZDirection.y, localPlusZDirection.z);
        straightObject->transform.rotationEuler = {pitch, yaw, 0.0f};

        // A cső kapcsolt vége pontosan a könyök végpontján marad,
        // a másik vége pedig a könyök csőtengelyével párhuzamos irányba áll.
        straightObject->transform.position = anchorWorld + outwardDirection * (straight->lengthM * 0.5f);
        straightObject->transform.scale = {1.0f, 1.0f, 1.0f};
    }

    void Application::enforceElbowStraightConstraints()
    {
        for (const auto &[id, pipe] : m_network.elements())
        {
            if (pipe.type == PipeElementType::StraightPipe)
            {
                alignStraightPipeToElbow(id);
            }
        }
    }
}
