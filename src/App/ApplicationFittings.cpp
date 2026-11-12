#include "App/ApplicationInternal.hpp"

namespace plumbcad
{
    int Application::selectedFittingRotationAnchorPoint(ObjectId objectId) const
    {
        const PipeElement *pipe = m_network.find(objectId);
        if (!pipe) return -1;

        if (m_fittingRotationFixedEndpoint >= 0 &&
            m_fittingRotationFixedEndpoint < static_cast<int>(pipe->connections.size()) &&
            !pipe->connections[m_fittingRotationFixedEndpoint].isOpen())
        {
            return m_fittingRotationFixedEndpoint;
        }

        for (int i = 0; i < static_cast<int>(pipe->connections.size()); ++i)
        {
            if (!pipe->connections[i].isOpen()) return i;
        }

        return -1;
    }

    void Application::applyManualFittingRotation(float degreesX, float degreesY, float degreesZ)
    {
        PipeElement *pipe = m_network.find(m_selectedId);
        SceneObject *object = findSceneObject(m_selectedId);
        if (!pipe || !object)
        {
            setStatusMessage("Kézi forgatás sikertelen: nincs kijelölt elem.");
            return;
        }

        if (pipe->type == PipeElementType::StraightPipe)
        {
            setStatusMessage("Kézi tengelyenkénti forgatás csak idomokra engedélyezett, egyenes csőre nem.");
            return;
        }

        if (!isDirectionalFittingType(pipe->type))
        {
            setStatusMessage("Kézi forgatás sikertelen: ez az elemtípus nem támogatott.");
            return;
        }

        const float degToRad = 0.01745329251994329577f;
        const int connectedCount = connectedEndpointCount(m_selectedId);

        if (connectedCount > 0)
        {
            const int anchorPoint = selectedFittingRotationAnchorPoint(m_selectedId);
            if (anchorPoint < 0)
            {
                setStatusMessage("Kézi forgatás sikertelen: nincs érvényes fix csatlakozás.");
                return;
            }

            const glm::vec3 anchorWorld = connectionWorldPosition(m_selectedId, anchorPoint);
            glm::vec3 axisWorld = fittingConnectorDirectionWorld(m_selectedId, anchorPoint);
            if (glm::length2(axisWorld) < 0.000001f) axisWorld = glm::vec3{0.0f, 1.0f, 0.0f};

            // Csatlakoztatott idomnál a felhasználó által választott csatlakozás marad fix,
            // és az idom annak csatlakozási tengelye körül fordulhat.
            const float twistDegrees = degreesX + degreesY + degreesZ;
            if (std::abs(twistDegrees) < 0.0001f)
            {
                setStatusMessage("Kézi forgatás: nincs megadott fokérték.");
                return;
            }

            rotateConnectedAssemblyAroundFixedEndpoint(m_selectedId, anchorPoint, twistDegrees * degToRad);

            enforceElbowStraightConstraints();
            updateEndpointMarkers();
            setStatusMessage("Kézi idomforgatás alkalmazva: a " + std::to_string(anchorPoint) +
                             ". csatlakozás maradt fix, és a kapcsolt ágak együtt fordultak az idommal.");
            return;
        }

        // Ha az idom nincs csatlakoztatva, szabad kézi X/Y/Z forgatás engedélyezett.
        object->transform.rotationEuler.x += degreesX * degToRad;
        object->transform.rotationEuler.y += degreesY * degToRad;
        object->transform.rotationEuler.z += degreesZ * degToRad;

        updateEndpointMarkers();
        setStatusMessage("Kézi idomforgatás alkalmazva szabadon: X=" + std::to_string(degreesX) +
                         "°, Y=" + std::to_string(degreesY) + "°, Z=" + std::to_string(degreesZ) + "°.");
    }

    void Application::autoAttachElementToSelected(PipeElementType newType, int targetPointIndex)
    {
        PipeElement *targetPipe = m_network.find(m_selectedId);
        if (!targetPipe)
        {
            setStatusMessage("Automatikus csatolás sikertelen: nincs kijelölt csőelem.");
            return;
        }
        if (targetPointIndex < 0 || targetPointIndex >= static_cast<int>(targetPipe->connections.size()))
        {
            setStatusMessage("Automatikus csatolás sikertelen: hibás csővég index.");
            return;
        }
        if (!targetPipe->connections[targetPointIndex].isOpen())
        {
            setStatusMessage("Automatikus csatolás sikertelen: a kiválasztott csővég már kapcsolva van.");
            return;
        }

        // A cél végpont lehet egy szűkítő kisebb/nagyobb oldala is.
        // Emiatt nem az elem elsődleges átmérőjét, hanem a tényleges
        // csatlakozási végpont katalógusméretét örökítjük.
        const int targetTypeIndex = targetPipe->pipeTypeIndex;

        const int targetSizeIndex = pipeEndpointSizeIndex(*targetPipe, targetPointIndex);

        const float targetDiameterMm = pipeEndpointDiameterMm(*targetPipe, targetPointIndex);

        int reducerSecondarySizeIndex = targetSizeIndex;

        float reducerSecondaryDiameterMm = targetDiameterMm;

        const auto &catalog = m_network.catalog();

        if (targetTypeIndex >= 0 && targetTypeIndex < static_cast<int>(catalog.size()))
        {
            const auto &sizes = catalog[targetTypeIndex].sizes;

            if (!sizes.empty())
            {
                const int validTargetSize = std::clamp(targetSizeIndex, 0, static_cast<int>(sizes.size()) - 1);

                if (sizes.size() > 1)
                {
                    reducerSecondarySizeIndex = validTargetSize > 0 ? validTargetSize - 1 : 1;
                }
                else
                {
                    reducerSecondarySizeIndex = validTargetSize;
                }

                reducerSecondaryDiameterMm = sizes[reducerSecondarySizeIndex].diameterMm;
            }
        }

        ObjectId newId = InvalidObjectId;

        switch (newType)
        {
        case PipeElementType::StraightPipe:
            newId = m_network.addStraightPipe(1.0f, targetDiameterMm);
            break;

        case PipeElementType::Elbow90:
            newId = m_network.addElbow90(DefaultCompactElbow90BendRadiusM, targetDiameterMm);
            break;

        case PipeElementType::Elbow45:
            newId = m_network.addElbow45(DefaultElbow45BendRadiusM, targetDiameterMm);
            break;

        case PipeElementType::Tee:
            newId =
                m_network.addTee(DefaultCompactTeeMainLengthM, DefaultCompactTeeBranchLengthM, targetDiameterMm);
            break;

        case PipeElementType::Reducer:
            newId = m_network.addReducer(0.25f, targetDiameterMm, reducerSecondaryDiameterMm);
            break;

        case PipeElementType::Valve:
            newId = m_network.addValve(0.24f, targetDiameterMm);
            break;

        case PipeElementType::EndCap:
            newId = m_network.addEndCap(0.10f, targetDiameterMm);
            break;

        default:
            setStatusMessage("Automatikus csatolás sikertelen: ez az elemtípus még nem támogatott.");
            return;
        }

        PipeElement *newPipe = m_network.find(newId);
        if (!newPipe)
        {
            setStatusMessage("Automatikus csatolás sikertelen: az új elem nem jött létre.");
            return;
        }

        // Az új elem örökli a cél VÉGPONT csőtípusát és katalógusméretét.
        m_network.setPipeType(newId, targetTypeIndex);

        m_network.setPipeSize(newId, targetSizeIndex);

        if (newType == PipeElementType::Reducer)
        {
            m_network.setReducerSecondarySize(newId, reducerSecondarySizeIndex);
        }

        newPipe = m_network.find(newId);
        if (!newPipe) return;

        SceneObject newObject = makeSceneObjectForPipe(*newPipe);
        newObject.transform.position = connectionWorldPosition(m_selectedId, targetPointIndex);
        m_objects.push_back(newObject);

        // Fontos: az automatikusan létrehozott elem mesh-ét is fel kell tölteni GPU-ra.
        // Enélkül csak a csővég-jelölők látszanak, maga a cső/idom nem rajzolódik ki.
        m_renderer.uploadObject(m_objects.back());

        // Az új elem 0. csatlakozási pontját kötjük a kiválasztott nyitott véghez.
        // Ez egyértelmű és gyors munkafolyamatot ad: kijelölt csővég -> új elem kezdőpontja.
        const int newPointIndex = 0;
        m_network.connect(newId, newPointIndex, m_selectedId, targetPointIndex);

        const PipeElement *targetAfterConnect = m_network.find(m_selectedId);
        const PipeElement *newAfterConnect = m_network.find(newId);
        if (!targetAfterConnect || !newAfterConnect) return;

        if (newAfterConnect->type == PipeElementType::StraightPipe &&
            targetAfterConnect->type == PipeElementType::StraightPipe)
        {
            alignStraightPipeToStraight(newId, newPointIndex, m_selectedId, targetPointIndex);
        }
        else if (newAfterConnect->type == PipeElementType::StraightPipe &&
                 isDirectionalFittingType(targetAfterConnect->type))
        {
            alignStraightPipeToElbow(newId);
        }
        else if (isDirectionalFittingType(newAfterConnect->type) &&
                 targetAfterConnect->type == PipeElementType::StraightPipe)
        {
            alignElbowToStraight(newId, newPointIndex, m_selectedId, targetPointIndex);
        }
        else if (isDirectionalFittingType(newAfterConnect->type) &&
                 isDirectionalFittingType(targetAfterConnect->type))
        {
            alignElbowToElbow(newId, newPointIndex, m_selectedId, targetPointIndex);
        }
        else
        {
            const glm::vec3 targetWorld = connectionWorldPosition(m_selectedId, targetPointIndex);
            const glm::vec3 newPointWorld = connectionWorldPosition(newId, newPointIndex);
            if (SceneObject *obj = findSceneObject(newId))
            {
                obj->transform.position += targetWorld - newPointWorld;
            }
        }

        enforceElbowStraightConstraints();

        const int extraConnections = connectAllNearbyOpenEndpoints(newId, std::vector<ObjectId>{newId});
        enforceElbowStraightConstraints();
        updateEndpointMarkers();
        m_selectedId = newId;
        if (extraConnections > 0)
        {
            setStatusMessage(
                "Automatikus csatolás kész: az új elem több közeli csatlakozási ponthoz is kapcsolódott. Extra kapcsolatok: " +
                std::to_string(extraConnections) + ".");
        }
        else
        {
            setStatusMessage("Automatikus csatolás kész: az új elem létrejött, csatlakozott és irányba állt.");
        }
    }
}
