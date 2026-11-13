#include "App/ApplicationInternal.hpp"

namespace plumbcad
{
    glm::vec3 Application::pipeCategoryColor(int pipeTypeIndex) const
    {
        // Kategóriaszínek:
        // 0 PPR      = kék
        // 1 PVC-U    = szürke
        // 2 Réz      = rézbarna
        //
        // A kijelölt elem továbbra is a Renderer sárga kijelölési
        // színét kapja; ez csak az alap hálózati kategóriaszín.
        switch (pipeTypeIndex)
        {
        case 0:
            return glm::vec3{0.10f, 0.55f, 0.90f};

        case 1:
            return glm::vec3{0.48f, 0.52f, 0.56f};

        case 2:
            return glm::vec3{0.72f, 0.32f, 0.12f};

        default:
            return glm::vec3{0.55f, 0.58f, 0.62f};
        }
    }

    void Application::addPipeObject(float lengthM, float diameterMm)
    {
        ObjectId id = m_network.addStraightPipe(lengthM, diameterMm);
        const PipeElement *pipe = m_network.find(id);
        if (!pipe) return;
        SceneObject obj = makeSceneObjectForPipe(*pipe);
        obj.transform.position = {0.0f, 0.5f, 0.0f};
        m_objects.push_back(obj);
        m_renderer.uploadObject(obj);
        m_selectedId = id;
        updateEndpointMarkers();
    }

    bool Application::deleteElement(ObjectId id)
    {
        if (findArchitecturalObstacle(id))
        {
            return deleteArchitecturalObstacle(id);
        }

        PipeElement *pipe = m_network.find(id);

        if (!pipe)
        {
            setStatusMessage("Törlés sikertelen: a kijelölt csőelem nem található.");
            return false;
        }

        const ObjectId deletedId = id;

        // A törölt elemhez tartozó scene objectet és GPU mesht is eltávolítjuk.
        const bool networkRemoved = m_network.removeElement(deletedId);

        if (!networkRemoved)
        {
            setStatusMessage("Törlés sikertelen: a hálózati elem nem távolítható el.");
            return false;
        }

        m_objects.erase(std::remove_if(m_objects.begin(), m_objects.end(),
                                       [deletedId](const SceneObject &object) { return object.id == deletedId; }),
                        m_objects.end());

        m_renderer.removeObject(deletedId);

        // Ha az elem A* START/END pontként volt kijelölve, csak az érintett
        // pontot töröljük. Minden korábbi keresési alternatíva viszont elavult.
        if (m_routeStartId == deletedId)
        {
            m_routeStartId = InvalidObjectId;
            m_routeStartEndpoint = -1;
        }

        if (m_routeEndId == deletedId)
        {
            m_routeEndId = InvalidObjectId;
            m_routeEndEndpoint = -1;
        }

        m_routeAlternatives.clear();
        m_selectedRouteAlternative = 0;
        clearRoutePreview();

        // A közös validációs START/END hivatkozás se maradhat a törölt elemre.
        if (m_validationStartId == deletedId)
        {
            m_validationStartId = InvalidObjectId;
            m_validationStartEndpoint = -1;
        }

        if (m_validationEndId == deletedId)
        {
            m_validationEndId = InvalidObjectId;
            m_validationEndEndpoint = -1;
        }

        // Az "utolsó A* útvonal" listából is kivesszük az elemet.
        m_lastRouteCreatedIds.erase(
            std::remove(m_lastRouteCreatedIds.begin(), m_lastRouteCreatedIds.end(), deletedId),
            m_lastRouteCreatedIds.end());

        // Aktív szerkesztési állapotok ne hivatkozzanak a már nem létező elemre.
        m_selectedId = InvalidObjectId;
        m_objectDragActive = false;
        m_objectDragMoved = false;
        m_dragObjectIds.clear();
        m_dragAnchorPointIndex = -1;
        m_dragHasAnchor = false;
        m_constrainedElbowRotate = false;
        m_fittingRotationFixedEndpoint = 0;

        cleanupInvalidConnections();

        m_validationHasRun = false;
        m_validationMessages.clear();

        updateEndpointMarkers();

        setStatusMessage("Elem törölve: #" + std::to_string(deletedId) +
                         ". A szomszédos elemek megmaradtak; az érintett kapcsolataik nyitott végponttá váltak.");

        return true;
    }

    void Application::addElbow90Object(float bendRadiusM, float diameterMm)
    {
        ObjectId id = m_network.addElbow90(bendRadiusM, diameterMm);
        const PipeElement *pipe = m_network.find(id);
        SceneObject obj;
        obj.id = id;
        obj.name = "90 fokos könyök";
        const float radius = ((pipe ? pipe->diameterMm : diameterMm) / 1000.0f) * 0.5f;
        obj.mesh = MeshFactory::makeElbow90(radius, pipe ? pipe->bendRadiusM : bendRadiusM);
        obj.color = pipeCategoryColor(pipe ? pipe->pipeTypeIndex : 0);
        obj.transform.position = {0.0f, 0.5f, 0.0f};
        m_objects.push_back(obj);
        m_renderer.uploadObject(obj);
        m_selectedId = id;
        updateEndpointMarkers();
    }

    void Application::addElbow45Object(float bendRadiusM, float diameterMm)
    {
        ObjectId id = m_network.addElbow45(bendRadiusM, diameterMm);
        const PipeElement *pipe = m_network.find(id);
        SceneObject obj;
        obj.id = id;
        obj.name = "45 fokos könyök";
        const float radius = ((pipe ? pipe->diameterMm : diameterMm) / 1000.0f) * 0.5f;
        obj.mesh = MeshFactory::makeElbow45(radius, pipe ? pipe->bendRadiusM : bendRadiusM);
        obj.color = pipeCategoryColor(pipe ? pipe->pipeTypeIndex : 0);
        obj.transform.position = {0.0f, 0.5f, 0.0f};
        m_objects.push_back(obj);
        m_renderer.uploadObject(obj);
        m_selectedId = id;
        updateEndpointMarkers();
    }

    void Application::addTeeObject(float mainLengthM, float branchLengthM, float diameterMm)
    {
        ObjectId id = m_network.addTee(mainLengthM, branchLengthM, diameterMm);
        const PipeElement *pipe = m_network.find(id);
        SceneObject obj;
        obj.id = id;
        obj.name = "T-idom";
        const float radius = ((pipe ? pipe->diameterMm : diameterMm) / 1000.0f) * 0.5f;
        obj.mesh = MeshFactory::makeTee(radius, pipe ? pipe->lengthM : mainLengthM,
                                        pipe ? pipe->bendRadiusM : branchLengthM);
        obj.color = pipeCategoryColor(pipe ? pipe->pipeTypeIndex : 0);
        obj.transform.position = {0.0f, 0.5f, 0.0f};
        m_objects.push_back(obj);
        m_renderer.uploadObject(obj);
        m_selectedId = id;
        updateEndpointMarkers();
    }

    void Application::addReducerObject(float lengthM, float inletDiameterMm, float outletDiameterMm)
    {
        const ObjectId id = m_network.addReducer(lengthM, inletDiameterMm, outletDiameterMm);

        const PipeElement *pipe = m_network.find(id);

        if (!pipe) return;

        SceneObject obj = makeSceneObjectForPipe(*pipe);

        obj.transform.position = glm::vec3{0.0f, 0.5f, 0.0f};

        m_objects.push_back(obj);
        m_renderer.uploadObject(obj);

        m_selectedId = id;
        updateEndpointMarkers();
    }

    void Application::addValveObject(float lengthM, float diameterMm)
    {
        const ObjectId id = m_network.addValve(lengthM, diameterMm);

        const PipeElement *pipe = m_network.find(id);

        if (!pipe) return;

        SceneObject obj = makeSceneObjectForPipe(*pipe);

        obj.transform.position = glm::vec3{0.0f, 0.5f, 0.0f};

        m_objects.push_back(obj);
        m_renderer.uploadObject(obj);

        m_selectedId = id;
        updateEndpointMarkers();
    }

    void Application::addEndCapObject(float lengthM, float diameterMm)
    {
        const ObjectId id = m_network.addEndCap(lengthM, diameterMm);

        const PipeElement *pipe = m_network.find(id);

        if (!pipe) return;

        SceneObject obj = makeSceneObjectForPipe(*pipe);

        obj.transform.position = glm::vec3{0.0f, 0.5f, 0.0f};

        m_objects.push_back(obj);
        m_renderer.uploadObject(obj);

        m_selectedId = id;
        updateEndpointMarkers();
    }

    SceneObject Application::makeSceneObjectForPipe(const PipeElement &pipe) const
    {
        SceneObject obj;
        obj.id = pipe.id;
        const float radius = (pipe.diameterMm / 1000.0f) * 0.5f;

        if (pipe.type == PipeElementType::Elbow90)
        {
            obj.name = "90 fokos könyök";
            obj.mesh = MeshFactory::makeElbow90(radius, pipe.bendRadiusM);
            obj.color = {0.1f, 0.55f, 0.9f};
        }
        else if (pipe.type == PipeElementType::Elbow45)
        {
            obj.name = "45 fokos könyök";
            obj.mesh = MeshFactory::makeElbow45(radius, pipe.bendRadiusM);
            obj.color = {0.1f, 0.55f, 0.9f};
        }
        else if (pipe.type == PipeElementType::AlignmentElbow)
        {
            const float angle =
                std::clamp(pipe.lengthM / std::max(pipe.bendRadiusM, 0.1f), 0.001f, 3.12413936107f);

            obj.name = "Útvonal végpont-illesztő könyök";
            obj.mesh = MeshFactory::makeElbow(radius, pipe.bendRadiusM, angle,
                                              std::max(6, static_cast<int>(std::ceil(angle / 0.0872664626f))), 24);
            obj.color = {0.1f, 0.55f, 0.9f};
        }
        else if (pipe.type == PipeElementType::Tee)
        {
            obj.name = "T-idom";
            obj.mesh = MeshFactory::makeTee(radius, pipe.lengthM, pipe.bendRadiusM);
            obj.color = {0.1f, 0.55f, 0.9f};
        }
        else if (pipe.type == PipeElementType::Reducer)
        {
            const float outletRadius =
                (pipe.secondaryDiameterMm > 0.0f ? pipe.secondaryDiameterMm : pipe.diameterMm) / 1000.0f * 0.5f;

            obj.name = "Szűkítő";
            obj.mesh = MeshFactory::makeReducer(radius, outletRadius, pipe.lengthM);
            obj.color = {0.1f, 0.55f, 0.9f};
        }
        else if (pipe.type == PipeElementType::Valve)
        {
            obj.name = "Szelep";
            obj.mesh = MeshFactory::makeValve(radius, pipe.lengthM);
            obj.color = {0.1f, 0.55f, 0.9f};
        }
        else if (pipe.type == PipeElementType::EndCap)
        {
            obj.name = "Végzáró";
            obj.mesh = MeshFactory::makeEndCap(radius, pipe.lengthM);
            obj.color = {0.1f, 0.55f, 0.9f};
        }
        else
        {
            obj.name = "Csőszakasz";
            obj.mesh = MeshFactory::makePipe(radius, pipe.wallThicknessMm * 0.001f, pipe.lengthM);
            obj.color = {0.1f, 0.55f, 0.9f};
        }
        // A geometriai elemtípus (cső/könyök/T-idom/stb.) nem változtatja
        // a kategóriaszínt: az egész hálózati elem a katalóguskategória
        // színét kapja.
        obj.color = pipeCategoryColor(pipe.pipeTypeIndex);

        obj.selectable = true;
        return obj;
    }

    void Application::rebuildPipeMesh(ObjectId id)
    {
        PipeElement *pipe = m_network.find(id);
        SceneObject *object = findSceneObject(id);
        if (!pipe || !object) return;

        const float tubeRadius = (pipe->diameterMm / 1000.0f) * 0.5f;
        if (pipe->type == PipeElementType::Elbow90)
        {
            object->mesh = MeshFactory::makeElbow90(tubeRadius, pipe->bendRadiusM);
        }
        else if (pipe->type == PipeElementType::Elbow45)
        {
            object->mesh = MeshFactory::makeElbow45(tubeRadius, pipe->bendRadiusM);
        }
        else if (pipe->type == PipeElementType::AlignmentElbow)
        {
            const float angle =
                std::clamp(pipe->lengthM / std::max(pipe->bendRadiusM, 0.1f), 0.001f, 3.12413936107f);

            object->mesh =
                MeshFactory::makeElbow(tubeRadius, pipe->bendRadiusM, angle,
                                       std::max(6, static_cast<int>(std::ceil(angle / 0.0872664626f))), 24);
        }
        else if (pipe->type == PipeElementType::Tee)
        {
            object->mesh = MeshFactory::makeTee(tubeRadius, pipe->lengthM, pipe->bendRadiusM);
        }
        else if (pipe->type == PipeElementType::Reducer)
        {
            const float outletRadius =
                (pipe->secondaryDiameterMm > 0.0f ? pipe->secondaryDiameterMm : pipe->diameterMm) / 1000.0f * 0.5f;

            object->mesh = MeshFactory::makeReducer(tubeRadius, outletRadius, pipe->lengthM);
        }
        else if (pipe->type == PipeElementType::Valve)
        {
            object->mesh = MeshFactory::makeValve(tubeRadius, pipe->lengthM);
        }
        else if (pipe->type == PipeElementType::EndCap)
        {
            object->mesh = MeshFactory::makeEndCap(tubeRadius, pipe->lengthM);
        }
        else
        {
            object->mesh = MeshFactory::makePipe(tubeRadius, pipe->wallThicknessMm * 0.001f, pipe->lengthM);
        }
        object->color = pipeCategoryColor(pipe->pipeTypeIndex);

        object->transform.scale = {1.0f, 1.0f, 1.0f};
        m_renderer.uploadObject(*object);
    }

    bool Application::resizeStraightPipe(ObjectId id, float newLengthM)
    {
        PipeElement *pipe = m_network.find(id);
        SceneObject *object = findSceneObject(id);
        if (!pipe || !object || pipe->type != PipeElementType::StraightPipe)
        {
            setStatusMessage("Csak egyenes csőszakasz hossza módosítható.");
            return false;
        }

        newLengthM = std::clamp(newLengthM, MinStraightPipeLengthM, MaxStraightPipeLengthM);
        if (std::abs(pipe->lengthM - newLengthM) < 0.0001f)
        {
            setStatusMessage("A cső hossza nem változott.");
            return false;
        }

        if (pipe->connections.size() < 2)
        {
            setStatusMessage("A cső csatlakozási adatai hiányosak.");
            return false;
        }

        const glm::vec3 oldP0 = connectionWorldPosition(id, 0);
        const glm::vec3 oldP1 = connectionWorldPosition(id, 1);
        glm::vec3 axis = oldP1 - oldP0;
        if (glm::length2(axis) < 0.000001f)
        {
            setStatusMessage("A cső iránya nem határozható meg.");
            return false;
        }
        axis = glm::normalize(axis);

        // Ha van kapcsolt végpont, azt tartjuk fixen.
        // Ha mindkettő kapcsolt, a 0. vég marad fix, a másik oldalhoz tartozó
        // részhálózat pedig együtt mozdul a hosszváltozással.
        int fixedEndpoint = -1;
        if (!pipe->connections[0].isOpen())
            fixedEndpoint = 0;
        else if (!pipe->connections[1].isOpen())
            fixedEndpoint = 1;

        std::vector<ObjectId> movingObjects;
        glm::vec3 oldMovingEndpoint{0.0f};

        if (fixedEndpoint >= 0)
        {
            const int movingEndpoint = 1 - fixedEndpoint;
            oldMovingEndpoint = movingEndpoint == 0 ? oldP0 : oldP1;

            // Csak akkor van mit együtt mozgatni, ha a másik vég is kapcsolt.
            if (!pipe->connections[movingEndpoint].isOpen())
            {
                movingObjects = connectedComponentAwayFromFixedEndpoint(id, fixedEndpoint);
                movingObjects.erase(std::remove(movingObjects.begin(), movingObjects.end(), id),
                                    movingObjects.end());
            }
        }

        m_network.setPipeLength(id, newLengthM);
        rebuildPipeMesh(id);

        if (fixedEndpoint >= 0)
        {
            const glm::vec3 fixedWorld = fixedEndpoint == 0 ? oldP0 : oldP1;

            // A straight pipe lokális Z tengelye a 0. végtől az 1. vég felé mutat.
            object->transform.position = fixedEndpoint == 0 ? fixedWorld + axis * (newLengthM * 0.5f)
                                                            : fixedWorld - axis * (newLengthM * 0.5f);

            const int movingEndpoint = 1 - fixedEndpoint;
            const glm::vec3 newMovingEndpoint = connectionWorldPosition(id, movingEndpoint);
            const glm::vec3 delta = newMovingEndpoint - oldMovingEndpoint;

            if (!movingObjects.empty() && glm::length2(delta) > 0.00000001f)
            {
                translateObjects(movingObjects, delta);
            }
        }

        // A korábban keresett A* alternatívák geometria-változás után elavulhatnak.
        m_routeAlternatives.clear();
        m_selectedRouteAlternative = 0;

        updateEndpointMarkers();
        setStatusMessage("Egyenes cső hossza módosítva: " + std::to_string(newLengthM) + " m.");
        return true;
    }

    void Application::initializeWorldAxisReference()
    {
        if (!m_worldAxisObjects.empty()) return;

        constexpr ObjectId WorldAxisBaseId = 3820000000u;

        // Origó jelölő pontosan a világ (0,0,0) pontján.
        SceneObject origin;
        origin.id = WorldAxisBaseId;

        origin.name = "Világ origó (0,0,0)";

        origin.mesh = MeshFactory::makeBox(glm::vec3{0.07f, 0.07f, 0.07f});

        origin.color = glm::vec3{0.92f, 0.92f, 0.92f};

        origin.transform.position = glm::vec3{0.0f, 0.0f, 0.0f};

        origin.selectable = false;

        origin.renderOnTop = true;

        m_worldAxisObjects.push_back(origin);

        m_renderer.uploadObject(m_worldAxisObjects.back());

        const glm::vec3 directions[3] = {glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f},
                                         glm::vec3{0.0f, 0.0f, 1.0f}};

        // CAD-konvenció: X piros, Y zöld, Z kék.
        const glm::vec3 colors[3] = {glm::vec3{0.95f, 0.18f, 0.16f}, glm::vec3{0.18f, 0.88f, 0.28f},
                                     glm::vec3{0.18f, 0.42f, 1.00f}};

        const char *names[3] = {"Világ X tengely", "Világ Y tengely", "Világ Z tengely"};

        for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
        {
            SceneObject axis;
            axis.id = WorldAxisBaseId + 1u + static_cast<ObjectId>(axisIndex);

            axis.name = names[axisIndex];

            axis.mesh = MeshFactory::makeArrow(1.0f, 0.016f, 0.050f, 0.17f, 20);

            axis.color = colors[axisIndex];

            axis.transform.position = glm::vec3{0.0f, 0.0f, 0.0f};

            axis.selectable = false;

            axis.renderOnTop = true;

            setObjectRotationFromLocalDirection(axis, glm::vec3{0.0f, 0.0f, 1.0f}, directions[axisIndex]);

            m_worldAxisObjects.push_back(axis);

            m_renderer.uploadObject(m_worldAxisObjects.back());
        }
    }

    void Application::clearTransformGizmo()
    {
        for (const SceneObject &gizmo : m_transformGizmoObjects)
        {
            m_renderer.removeObject(gizmo.id);
        }

        m_transformGizmoObjects.clear();
    }

    void Application::updateTransformGizmo()
    {
        const bool showGizmo = m_selectedId != InvalidObjectId && (m_transformMode == TransformMode::Move ||
                                                                   m_transformMode == TransformMode::Rotate);

        const SceneObject *selectedObject = showGizmo ? findSceneObject(m_selectedId) : nullptr;

        if (!selectedObject)
        {
            clearTransformGizmo();
            return;
        }

        constexpr ObjectId GizmoBaseId = 3800000000u;

        // A gizmo képernyőn közel állandó méretűnek érződjön:
        // távolabb lévő objektumnál arányosan nagyobb világméretet kap.
        glm::vec3 gizmoPivot = selectedObject->transform.position;

        if (m_transformMode == TransformMode::Rotate)
        {
            const PipeElement *selectedPipe = m_network.find(m_selectedId);

            if (selectedPipe &&
                (selectedPipe->type == PipeElementType::Elbow90 ||
                 selectedPipe->type == PipeElementType::Elbow45 || selectedPipe->type == PipeElementType::Tee))
            {
                const bool selectedFixedEndpointValid =
                    m_fittingRotationFixedEndpoint >= 0 &&
                    m_fittingRotationFixedEndpoint < static_cast<int>(selectedPipe->connections.size()) &&
                    !selectedPipe->connections[m_fittingRotationFixedEndpoint].isOpen();

                const int fixedEndpoint = selectedFixedEndpointValid ? m_fittingRotationFixedEndpoint
                                                                     : firstConnectedEndpoint(m_selectedId);

                if (fixedEndpoint >= 0)
                {
                    gizmoPivot = connectionWorldPosition(m_selectedId, fixedEndpoint);
                }
            }
        }

        const float cameraDistance = std::max(0.25f, glm::length(m_camera.position - gizmoPivot));

        const float fovFactor = std::clamp(m_camera.fovDeg / 45.0f, 0.35f, 1.75f);

        const float gizmoLength = m_camera.orthographic
                                      ? std::clamp(m_camera.orthoHeight * 0.035f, 0.10f, 0.45f)
                                      : std::clamp(cameraDistance * 0.045f * fovFactor, 0.10f, 0.45f);

        const glm::vec3 axisDirections[3] = {glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f},
                                             glm::vec3{0.0f, 0.0f, 1.0f}};

        // CAD-konvenció:
        // X = piros, Y = zöld, Z = kék.
        const glm::vec3 axisColors[3] = {glm::vec3{0.95f, 0.18f, 0.16f}, glm::vec3{0.18f, 0.88f, 0.28f},
                                         glm::vec3{0.18f, 0.42f, 1.00f}};

        const char *axisNames[3] = {"X tengely", "Y tengely", "Z tengely"};

        if (m_transformGizmoObjects.size() != 3u)
        {
            clearTransformGizmo();

            for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
            {
                SceneObject gizmo;
                gizmo.id = GizmoBaseId + static_cast<ObjectId>(axisIndex);

                gizmo.name = std::string("Transzformációs gizmo - ") + axisNames[axisIndex];

                gizmo.mesh = MeshFactory::makeArrow(1.0f, 0.024f, 0.074f, 0.22f, 24);

                gizmo.color = axisColors[axisIndex];

                gizmo.selectable = false;

                gizmo.renderOnTop = true;

                setObjectRotationFromLocalDirection(gizmo, glm::vec3{0.0f, 0.0f, 1.0f}, axisDirections[axisIndex]);

                m_transformGizmoObjects.push_back(gizmo);

                m_renderer.uploadObject(m_transformGizmoObjects.back());
            }
        }

        for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
        {
            SceneObject &gizmo = m_transformGizmoObjects[axisIndex];

            gizmo.transform.position = gizmoPivot;

            gizmo.transform.scale = glm::vec3{gizmoLength};

            gizmo.color = axisColors[axisIndex];
        }
    }

    SceneObject *Application::findSceneObject(ObjectId id)
    {
        for (SceneObject &object : m_objects)
        {
            if (object.id == id)
            {
                return &object;
            }
        }
        return nullptr;
    }

    const SceneObject *Application::findSceneObject(ObjectId id) const
    {
        for (const auto &object : m_objects)
        {
            if (object.id == id) return &object;
        }
        return nullptr;
    }
}
