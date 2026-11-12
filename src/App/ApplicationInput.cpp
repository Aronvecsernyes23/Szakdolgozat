#include "App/ApplicationInternal.hpp"

namespace plumbcad
{
    void Application::scrollCallback(GLFWwindow *window, double /*xOffset*/, double yOffset)
    {
        auto *app = static_cast<Application *>(glfwGetWindowUserPointer(window));
        if (app) app->onScroll(yOffset);
    }

    void Application::onScroll(double yOffset)
    {
        if (ImGui::GetIO().WantCaptureMouse) return;

        m_camera.addZoom(static_cast<float>(-yOffset * 2.0));
    }

    void Application::setStatusMessage(const std::string &message)
    {
        m_statusMessage = message;
    }

    int Application::pickEndpointHandle(ObjectId objectId, double mouseX, double mouseY) const
    {
        const PipeElement *pipe = m_network.find(objectId);
        if (!pipe) return -1;

        const float aspect = m_height == 0 ? 1.0f : static_cast<float>(m_width) / static_cast<float>(m_height);
        const glm::mat4 viewProj = m_camera.projection(aspect) * m_camera.view();
        int bestIndex = -1;
        float bestDistancePx = 22.0f;

        for (int i = 0; i < static_cast<int>(pipe->connections.size()); ++i)
        {
            const glm::vec3 world = connectionWorldPosition(objectId, i);
            glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
            if (clip.w <= 0.0001f) continue;
            clip /= clip.w;
            const float sx = (clip.x * 0.5f + 0.5f) * static_cast<float>(m_width);
            const float sy = (1.0f - (clip.y * 0.5f + 0.5f)) * static_cast<float>(m_height);
            const float dx = sx - static_cast<float>(mouseX);
            const float dy = sy - static_cast<float>(mouseY);
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance < bestDistancePx)
            {
                bestDistancePx = distance;
                bestIndex = i;
            }
        }

        return bestIndex;
    }

    bool Application::mouseRayPlaneIntersection(double mouseX, double mouseY, const glm::vec3 &planePoint,
                                                const glm::vec3 &planeNormal, glm::vec3 &outPoint) const
    {
        const float aspect = m_height == 0 ? 1.0f : static_cast<float>(m_width) / static_cast<float>(m_height);
        const glm::mat4 invViewProj = glm::inverse(m_camera.projection(aspect) * m_camera.view());

        const float ndcX = (2.0f * static_cast<float>(mouseX)) / static_cast<float>(m_width) - 1.0f;
        const float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY)) / static_cast<float>(m_height);
        glm::vec4 nearWorld = invViewProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
        glm::vec4 farWorld = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        nearWorld /= nearWorld.w;
        farWorld /= farWorld.w;

        const glm::vec3 origin = glm::vec3(nearWorld);
        const glm::vec3 dir = glm::normalize(glm::vec3(farWorld - nearWorld));
        const float denom = glm::dot(dir, planeNormal);
        if (std::abs(denom) < 0.00001f) return false;

        const float t = glm::dot(planePoint - origin, planeNormal) / denom;
        if (t < 0.0f) return false;
        outPoint = origin + dir * t;
        return true;
    }

    ObjectId Application::pickObject(double mouseX, double mouseY) const
    {
        const float aspect = m_height == 0 ? 1.0f : static_cast<float>(m_width) / static_cast<float>(m_height);
        const glm::mat4 view = m_camera.view();
        const glm::mat4 proj = m_camera.projection(aspect);
        const glm::mat4 invViewProj = glm::inverse(proj * view);

        const float ndcX = (2.0f * static_cast<float>(mouseX)) / static_cast<float>(m_width) - 1.0f;
        const float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY)) / static_cast<float>(m_height);

        const glm::vec4 nearClip{ndcX, ndcY, -1.0f, 1.0f};
        const glm::vec4 farClip{ndcX, ndcY, 1.0f, 1.0f};
        glm::vec4 nearWorld = invViewProj * nearClip;
        glm::vec4 farWorld = invViewProj * farClip;
        nearWorld /= nearWorld.w;
        farWorld /= farWorld.w;

        const glm::vec3 rayOrigin = glm::vec3(nearWorld);
        const glm::vec3 rayDir = glm::normalize(glm::vec3(farWorld - nearWorld));

        ObjectId bestId = InvalidObjectId;
        float bestT = std::numeric_limits<float>::max();

        for (const auto &object : m_objects)
        {
            if (!object.selectable) continue;

            const glm::mat4 invModel = glm::inverse(object.transform.matrix());
            const glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f));
            const glm::vec3 localDir = glm::normalize(glm::vec3(invModel * glm::vec4(rayDir, 0.0f)));

            float localT = 0.0f;
            if (intersectLocalAabb(object.mesh, localOrigin, localDir, localT))
            {
                const glm::vec3 localHit = localOrigin + localDir * localT;
                const glm::vec3 worldHit = glm::vec3(object.transform.matrix() * glm::vec4(localHit, 1.0f));
                const float worldT = glm::length(worldHit - rayOrigin);
                if (worldT < bestT)
                {
                    bestT = worldT;
                    bestId = object.id;
                }
            }
        }

        return bestId;
    }

    void Application::switchTransformModeFromKeyboard()
    {
        ImGuiIO &io = ImGui::GetIO();
        if (io.WantCaptureKeyboard) return;

        if (glfwGetKey(m_window, GLFW_KEY_1) == GLFW_PRESS) m_transformMode = TransformMode::Move;
        if (glfwGetKey(m_window, GLFW_KEY_2) == GLFW_PRESS) m_transformMode = TransformMode::Rotate;
        if (glfwGetKey(m_window, GLFW_KEY_3) == GLFW_PRESS) m_transformMode = TransformMode::Scale;
    }

    void Application::handleObjectTransformInput()
    {
        ImGuiIO &io = ImGui::GetIO();
        const bool leftMouseDown = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

        if (!leftMouseDown || m_selectedId == InvalidObjectId || io.WantCaptureMouse || m_cameraLookActive)
        {
            if (m_objectDragActive)
            {
                if (m_objectDragMoved)
                {
                    if (m_transformMode == TransformMode::Move)
                    {
                        trySnapSelectedPipe(true);
                    }
                    else if (m_transformMode == TransformMode::Scale)
                    {
                        trySnapSelectedPipe(false);
                    }

                    // Forgatás után nincs automatikus eltoló snap.
                    // Ha a teljes kapcsolt hálózat együtt forgott, akkor utólagos
                    // geometriai újraigazítást sem futtatunk: ugyanaz a merev
                    // transzformáció ment végig minden elemen, ezért a kapcsolatok
                    // eleve pontosan együtt maradtak.
                    if (!(m_transformMode == TransformMode::Rotate &&
                          (m_rotateConnectedGroup || m_constrainedElbowRotate)))
                    {
                        enforceElbowStraightConstraints();
                    }

                    if (m_transformMode == TransformMode::Rotate)
                    {
                        // A forgatás után a közeli nyitott csővéget automatikusan
                        // megkeressük, majd ugyanazzal a teljes geometriai snap/
                        // irányigazítással kapcsoljuk, mint Mozgatás után.
                        trySnapAfterRotation();
                    }

                    updateEndpointMarkers();
                }
                else
                {
                    // Egyszerű kattintásnál ne fusson snap/irányigazítás,
                    // mert ettől az idom visszaugorhatott vízszintes alapforgatásba.
                    updateEndpointMarkers();
                }
            }
            m_objectDragActive = false;
            m_objectDragMoved = false;
            m_constrainedElbowRotate = false;
            m_dragRotationAxisWorld = {0.0f, 1.0f, 0.0f};
            m_dragObjectIds.clear();
            m_dragAnchorPointIndex = -1;
            m_dragHasAnchor = false;

            m_moveDragPlaneReady = false;
            m_moveDragPlanePoint = glm::vec3{0.0f};
            m_moveDragPlaneNormal = glm::vec3{0.0f, 1.0f, 0.0f};
            m_moveDragStartCursorWorld = glm::vec3{0.0f};
            m_moveDragStartSelectedPosition = glm::vec3{0.0f};
            m_moveDragAppliedDelta = glm::vec3{0.0f};

            m_rotateDragReady = false;
            m_rotateDragUseScreenAngle = false;
            m_rotateDragPivotWorld = glm::vec3{0.0f};
            m_rotateDragAxisWorld = glm::vec3{0.0f, 1.0f, 0.0f};
            m_rotateDragStartVectorWorld = glm::vec3{1.0f, 0.0f, 0.0f};
            m_rotateDragAppliedAngleRad = 0.0f;
            m_rotateDragPivotScreenX = 0.0f;
            m_rotateDragPivotScreenY = 0.0f;
            m_rotateDragStartScreenAngleRad = 0.0f;

            return;
        }

        // A Méret eszköz nem húzás-alapú transzformáció.
        // A kívánt valós katalógusméretet a jobb alsó Szerkesztési eszközök
        // ablak listájából választjuk ki. A kijelöléskezelő ettől még lefut.
        if (m_transformMode == TransformMode::Scale)
        {
            return;
        }

        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(m_window, &mouseX, &mouseY);

        if (!m_objectDragActive)
        {
            const int endpointHit = pickEndpointHandle(m_selectedId, mouseX, mouseY);
            const PipeElement *selectedPipeForDrag = m_network.find(m_selectedId);

            const bool connectorPivotFitting = m_transformMode == TransformMode::Rotate && selectedPipeForDrag &&
                                               (selectedPipeForDrag->type == PipeElementType::Elbow90 ||
                                                selectedPipeForDrag->type == PipeElementType::Elbow45 ||
                                                selectedPipeForDrag->type == PipeElementType::Tee) &&
                                               connectedEndpointCount(m_selectedId) > 0;

            const bool endpointRotationStart =
                (m_transformMode == TransformMode::Rotate && endpointHit >= 0 && selectedPipeForDrag &&
                 selectedPipeForDrag->type == PipeElementType::StraightPipe);

            // Csak akkor kezdünk transzformációt, ha a húzás a kijelölt objektumon vagy forgatás módban a csővégen indul.
            if (!endpointRotationStart && pickObject(mouseX, mouseY) != m_selectedId) return;

            pushUndoSnapshot();

            m_objectDragActive = true;
            m_objectDragMoved = false;
            m_constrainedElbowRotate = false;
            m_dragRotationAxisWorld = {0.0f, 1.0f, 0.0f};
            m_dragObjectIds.clear();
            m_dragAnchorPointIndex = -1;
            m_dragHasAnchor = false;

            m_moveDragPlaneReady = false;
            m_moveDragAppliedDelta = glm::vec3{0.0f};

            m_rotateDragReady = false;
            m_rotateDragUseScreenAngle = false;
            m_rotateDragAppliedAngleRad = 0.0f;

            if (m_transformMode == TransformMode::Move)
            {
                if (m_moveConnectedGroup)
                {
                    // Külön opció: csak akkor mozog együtt a kapcsolt hálózatrész,
                    // ha ezt a felhasználó bekapcsolja a Szerkesztési eszközök ablakban.
                    m_dragObjectIds = connectedComponent(m_selectedId);
                    if (m_dragObjectIds.empty()) m_dragObjectIds.push_back(m_selectedId);
                    setStatusMessage("Együtt mozgatás: a kapcsolt elemek egy szerelvényként mozognak.");
                }
                else
                {
                    // Alapértelmezett: csak a kijelölt csőelem mozog.
                    // Fontos: a kapcsolatot nem kattintáskor bontjuk, hanem csak akkor,
                    // amikor tényleges egérhúzás történik.
                    m_dragObjectIds.push_back(m_selectedId);
                }

                // CAD-szerű mozgatási sík:
                // - 3D-ben a kattintás kezdeti mélységén, a kamera felé néző síkon.
                // A kurzor világpozícióját minden képkockában újraszámoljuk, tehát
                // nincs zoomtól és képernyőfelbontástól függő pixel-szorzó.
                const SceneObject *moveObject = findSceneObject(m_selectedId);

                if (moveObject)
                {
                    m_moveDragPlanePoint = moveObject->transform.position;

                    m_moveDragStartSelectedPosition = moveObject->transform.position;

                    m_moveDragPlaneNormal = glm::normalize(m_camera.forward());

                    glm::vec3 startCursorWorld{0.0f};

                    if (mouseRayPlaneIntersection(mouseX, mouseY, m_moveDragPlanePoint, m_moveDragPlaneNormal,
                                                  startCursorWorld))
                    {
                        m_moveDragStartCursorWorld = startCursorWorld;

                        m_moveDragAppliedDelta = glm::vec3{0.0f};

                        m_moveDragPlaneReady = true;

                        const char *axisText = "szabad";

                        switch (m_moveAxisConstraint)
                        {
                        case MoveAxisConstraint::X:
                            axisText = "X tengely";
                            break;

                        case MoveAxisConstraint::Y:
                            axisText = "Y tengely";
                            break;

                        case MoveAxisConstraint::Z:
                            axisText = "Z tengely";
                            break;

                        case MoveAxisConstraint::Free:
                        default:
                            break;
                        }

                        setStatusMessage(std::string("CAD mozgatás: ") + axisText +
                                         " | Ctrl = 10 mm raszter | Ctrl+Shift = 1 mm.");
                    }
                    else
                    {
                        setStatusMessage("Mozgatás nem indítható: a kurzorsík nem számítható ki.");
                    }
                }
            }
            else if (m_transformMode == TransformMode::Rotate)
            {
                m_dragObjectIds.clear();

                int fixedFittingEndpoint = -1;

                if (connectorPivotFitting)
                {
                    // 90° / 45° / T-idomnál is a Forgatás panelen kiválasztott,
                    // MÁR KAPCSOLT A/B/C végpont a fix forgáspont.
                    const bool selectedFixedEndpointValid =
                        m_fittingRotationFixedEndpoint >= 0 &&
                        m_fittingRotationFixedEndpoint <
                            static_cast<int>(selectedPipeForDrag->connections.size()) &&
                        !selectedPipeForDrag->connections[m_fittingRotationFixedEndpoint].isOpen();

                    fixedFittingEndpoint = selectedFixedEndpointValid ? m_fittingRotationFixedEndpoint
                                                                      : firstConnectedEndpoint(m_selectedId);

                    m_fittingRotationFixedEndpoint = fixedFittingEndpoint;

                    if (fixedFittingEndpoint >= 0)
                    {
                        m_constrainedElbowRotate = true;

                        m_dragAnchorPointIndex = fixedFittingEndpoint;

                        m_dragHasAnchor = true;

                        m_dragAnchorWorldPosition = connectionWorldPosition(m_selectedId, fixedFittingEndpoint);

                        m_dragRotationAxisWorld =
                            fittingConnectorDirectionWorld(m_selectedId, fixedFittingEndpoint);

                        if (glm::length2(m_dragRotationAxisWorld) < 0.000001f)
                        {
                            m_dragRotationAxisWorld = glm::vec3{0.0f, 1.0f, 0.0f};
                        }

                        m_dragRotationAxisWorld = glm::normalize(m_dragRotationAxisWorld);

                        // A fix csatlakozás másik oldalán lévő hálózatrész nem mozdul.
                        // A kiválasztott 90°/45°/T-idom és az annak többi ágára
                        // kapcsolt elemek együtt fordulnak a fix csatlakozás körül.
                        m_dragObjectIds =
                            connectedComponentAwayFromFixedEndpoint(m_selectedId, fixedFittingEndpoint);
                    }
                }

                if (!m_constrainedElbowRotate && m_rotateConnectedGroup && m_network.find(m_selectedId))
                {
                    m_dragObjectIds = connectedComponent(m_selectedId);
                }

                if (m_dragObjectIds.empty())
                {
                    m_dragObjectIds.push_back(m_selectedId);
                }

                const SceneObject *rotateObject = findSceneObject(m_selectedId);

                if (rotateObject)
                {
                    if (m_constrainedElbowRotate && m_dragHasAnchor)
                    {
                        // 90° / 45° / T-idom kapcsolt oldallal:
                        // a forgási középpont maga a kapcsolt csővég,
                        // a forgási tengely pedig annak csatlakozási tengelye.
                        m_rotateDragPivotWorld = m_dragAnchorWorldPosition;

                        m_rotateDragAxisWorld = m_dragRotationAxisWorld;
                    }
                    else
                    {
                        m_rotateDragPivotWorld = rotateObject->transform.position;

                        m_rotateDragAxisWorld = cadRotationAxisWorld();
                    }

                    glm::vec3 startHit{0.0f};

                    if (mouseRayPlaneIntersection(mouseX, mouseY, m_rotateDragPivotWorld, m_rotateDragAxisWorld,
                                                  startHit))
                    {
                        glm::vec3 startVector = startHit - m_rotateDragPivotWorld;

                        const float startLengthSquared = glm::length2(startVector);

                        if (startLengthSquared > 0.000001f)
                        {
                            m_rotateDragStartVectorWorld = glm::normalize(startVector);

                            m_rotateDragReady = true;

                            m_rotateDragUseScreenAngle = false;
                        }
                    }

                    if (!m_rotateDragReady)
                    {
                        const float aspect =
                            m_height == 0 ? 1.0f : static_cast<float>(m_width) / static_cast<float>(m_height);

                        const glm::mat4 viewProj = m_camera.projection(aspect) * m_camera.view();

                        glm::vec4 clip = viewProj * glm::vec4(m_rotateDragPivotWorld, 1.0f);

                        if (clip.w > 0.0001f)
                        {
                            clip /= clip.w;

                            m_rotateDragPivotScreenX = (clip.x * 0.5f + 0.5f) * static_cast<float>(m_width);

                            m_rotateDragPivotScreenY =
                                (1.0f - (clip.y * 0.5f + 0.5f)) * static_cast<float>(m_height);

                            const float screenDx = static_cast<float>(mouseX) - m_rotateDragPivotScreenX;

                            const float screenDy = static_cast<float>(mouseY) - m_rotateDragPivotScreenY;

                            if (screenDx * screenDx + screenDy * screenDy > 16.0f)
                            {
                                m_rotateDragStartScreenAngleRad = std::atan2(screenDy, screenDx);

                                m_rotateDragReady = true;

                                m_rotateDragUseScreenAngle = true;
                            }
                        }
                    }

                    if (m_rotateDragReady)
                    {
                        if (m_constrainedElbowRotate && m_dragHasAnchor)
                        {
                            const char endpointLetter =
                                static_cast<char>('A' + std::clamp(m_dragAnchorPointIndex, 0, 2));

                            setStatusMessage(std::string("Rögzített idomforgatás: ") + endpointLetter +
                                             " csatlakozás marad fix. "
                                             "A 90°/45°/T-idom csak ezen csatlakozás tengelye körül forog.");
                        }
                        else
                        {
                            const char *axisName = "Y";

                            switch (m_rotateAxisConstraint)
                            {
                            case RotateAxisConstraint::X:
                                axisName = "X";
                                break;

                            case RotateAxisConstraint::Z:
                                axisName = "Z";
                                break;

                            case RotateAxisConstraint::Y:
                            default:
                                break;
                            }

                            std::string rotationStatus =
                                std::string("CAD forgatás: ") + axisName + " tengely | " +
                                (m_rotationSnapDegrees > 0.0f ? "szögrács aktív." : "szabad szög.");

                            if (m_rotateConnectedGroup && m_network.find(m_selectedId))
                            {
                                rotationStatus +=
                                    " | teljes kapcsolt hálózat: " + std::to_string(m_dragObjectIds.size()) +
                                    " elem együtt forog.";
                            }

                            setStatusMessage(rotationStatus);
                        }
                    }
                    else
                    {
                        setStatusMessage("Forgatás nem indítható: húzd az egeret a forgásponttól távolabb.");
                    }
                }
            }

            m_lastObjectDragX = mouseX;
            m_lastObjectDragY = mouseY;
            return;
        }

        SceneObject *selectedObject = findSceneObject(m_selectedId);
        if (!selectedObject)
        {
            m_objectDragActive = false;
            return;
        }

        const float dx = static_cast<float>(mouseX - m_lastObjectDragX);
        const float dy = static_cast<float>(mouseY - m_lastObjectDragY);
        m_lastObjectDragX = mouseX;
        m_lastObjectDragY = mouseY;

        const bool fine = glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                          glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

        if (std::abs(dx) > 0.5f || std::abs(dy) > 0.5f)
        {
            m_objectDragMoved = true;
        }

        switch (m_transformMode)
        {
        case TransformMode::Move: {
            if (!m_moveDragPlaneReady) break;

            glm::vec3 cursorWorld{0.0f};

            if (!mouseRayPlaneIntersection(mouseX, mouseY, m_moveDragPlanePoint, m_moveDragPlaneNormal,
                                           cursorWorld))
            {
                break;
            }

            // A teljes elmozdulást mindig a húzás kezdőpontjához képest
            // számoljuk. Ettől nem halmozódik pixel- vagy lebegőpontos drift.
            glm::vec3 desiredTotalDelta = cursorWorld - m_moveDragStartCursorWorld;

            // A tengelyzár a jobb alsó Szerkesztési eszközök ablakból
            // választható ki, és addig marad aktív, amíg át nem állítjuk.
            const bool lockX = m_moveAxisConstraint == MoveAxisConstraint::X;

            const bool lockY = m_moveAxisConstraint == MoveAxisConstraint::Y;

            const bool lockZ = m_moveAxisConstraint == MoveAxisConstraint::Z;

            if (lockX)
            {
                desiredTotalDelta = glm::vec3{desiredTotalDelta.x, 0.0f, 0.0f};
            }
            else if (lockY)
            {
                desiredTotalDelta = glm::vec3{0.0f, desiredTotalDelta.y, 0.0f};
            }
            else if (lockZ)
            {
                desiredTotalDelta = glm::vec3{0.0f, 0.0f, desiredTotalDelta.z};
            }

            // Ctrl = abszolút világ-raszter.
            // Ctrl+Shift = 1 mm, egyébként 10 mm.
            const bool gridSnap = glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                                  glfwGetKey(m_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

            if (gridSnap)
            {
                const float gridStepM = fine ? 0.001f : 0.010f;

                glm::vec3 targetPosition = m_moveDragStartSelectedPosition + desiredTotalDelta;

                auto snapCoordinate = [gridStepM](float value) {
                    return std::round(value / gridStepM) * gridStepM;
                };

                if (lockX)
                {
                    targetPosition.x = snapCoordinate(targetPosition.x);

                    targetPosition.y = m_moveDragStartSelectedPosition.y;

                    targetPosition.z = m_moveDragStartSelectedPosition.z;
                }
                else if (lockY)
                {
                    targetPosition.x = m_moveDragStartSelectedPosition.x;

                    targetPosition.y = snapCoordinate(targetPosition.y);

                    targetPosition.z = m_moveDragStartSelectedPosition.z;
                }
                else if (lockZ)
                {
                    targetPosition.x = m_moveDragStartSelectedPosition.x;

                    targetPosition.y = m_moveDragStartSelectedPosition.y;

                    targetPosition.z = snapCoordinate(targetPosition.z);
                }
                else
                {
                    targetPosition.x = snapCoordinate(targetPosition.x);

                    targetPosition.y = snapCoordinate(targetPosition.y);

                    targetPosition.z = snapCoordinate(targetPosition.z);
                }

                desiredTotalDelta = targetPosition - m_moveDragStartSelectedPosition;
            }

            const glm::vec3 frameDelta = desiredTotalDelta - m_moveDragAppliedDelta;

            if (glm::length2(frameDelta) > 0.0000000001f)
            {
                if (m_objectDragMoved && !m_moveConnectedGroup && hasConnectedEndpoint(m_selectedId))
                {
                    m_network.disconnectAll(m_selectedId);

                    updateEndpointMarkers();

                    setStatusMessage("Külön mozgatás: a kijelölt elem kapcsolatai leváltak. "
                                     "Elengedéskor csak közeli csővégre fog snap-elni.");
                }

                translateObjects(m_dragObjectIds.empty() ? std::vector<ObjectId>{m_selectedId} : m_dragObjectIds,
                                 frameDelta);

                m_moveDragAppliedDelta = desiredTotalDelta;
            }

            break;
        }
        case TransformMode::Rotate: {
            if (!m_rotateDragReady) break;

            float desiredTotalAngle = 0.0f;

            if (!m_rotateDragUseScreenAngle)
            {
                glm::vec3 currentHit{0.0f};

                if (!mouseRayPlaneIntersection(mouseX, mouseY, m_rotateDragPivotWorld, m_rotateDragAxisWorld,
                                               currentHit))
                {
                    break;
                }

                glm::vec3 currentVector = currentHit - m_rotateDragPivotWorld;

                if (glm::length2(currentVector) < 0.000001f)
                {
                    break;
                }

                currentVector = glm::normalize(currentVector);

                const float sinAngle =
                    glm::dot(m_rotateDragAxisWorld, glm::cross(m_rotateDragStartVectorWorld, currentVector));

                const float cosAngle =
                    glm::clamp(glm::dot(m_rotateDragStartVectorWorld, currentVector), -1.0f, 1.0f);

                desiredTotalAngle = std::atan2(sinAngle, cosAngle);
            }
            else
            {
                const float screenDx = static_cast<float>(mouseX) - m_rotateDragPivotScreenX;

                const float screenDy = static_cast<float>(mouseY) - m_rotateDragPivotScreenY;

                if (screenDx * screenDx + screenDy * screenDy < 4.0f)
                {
                    break;
                }

                const float currentScreenAngle = std::atan2(screenDy, screenDx);

                desiredTotalAngle = currentScreenAngle - m_rotateDragStartScreenAngleRad;

                constexpr float Pi = 3.14159265359f;

                while (desiredTotalAngle > Pi)
                {
                    desiredTotalAngle -= 2.0f * Pi;
                }

                while (desiredTotalAngle < -Pi)
                {
                    desiredTotalAngle += 2.0f * Pi;
                }
            }

            // Opcionális CAD szögrács: a teljes forgatási szöget kvantáljuk,
            // nem az egyes frame-eket, ezért nincs felhalmozódó drift.
            if (m_rotationSnapDegrees > 0.0f)
            {
                constexpr float RadToDeg = 57.29577951308232f;

                constexpr float DegToRad = 0.01745329251994329577f;

                float degrees = desiredTotalAngle * RadToDeg;

                degrees = std::round(degrees / m_rotationSnapDegrees) * m_rotationSnapDegrees;

                desiredTotalAngle = degrees * DegToRad;
            }

            const float frameAngle = desiredTotalAngle - m_rotateDragAppliedAngleRad;

            if (std::abs(frameAngle) > 0.0000001f)
            {
                bool rotated = false;

                if (m_constrainedElbowRotate && m_dragHasAnchor)
                {
                    // A kapcsolt 90°/45°/T-idom mindig a rögzített
                    // csatlakozási pont és annak tengelye körül fordul.
                    rotated = rotateCadObjectGroupByRadians(m_dragObjectIds, m_dragAnchorWorldPosition,
                                                            m_dragRotationAxisWorld, frameAngle);
                }
                else if (m_rotateConnectedGroup && m_network.find(m_selectedId))
                {
                    rotated = rotateCadObjectGroupByRadians(m_dragObjectIds, m_rotateDragPivotWorld,
                                                            m_rotateDragAxisWorld, frameAngle);
                }
                else
                {
                    rotated = rotateCadSelectionByRadians(frameAngle);
                }

                if (rotated)
                {
                    m_rotateDragAppliedAngleRad = desiredTotalAngle;
                }
            }

            break;
        }
        case TransformMode::Scale: {
            // A Méret mód nem egérhúzásos skálázás.
            // A méretválasztás az UI katalóguslistájából történik.
            break;
        }
        }

        updateEndpointMarkers();
    }

    void Application::handleMouseSelection()
    {
        ImGuiIO &io = ImGui::GetIO();
        const bool leftMouseDown = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const bool clickedThisFrame = leftMouseDown && !m_leftMouseWasDown;
        m_leftMouseWasDown = leftMouseDown;

        // Ha éppen egy csővéget vagy csövet húzunk, a kijelöléskezelő ne írja felül
        // a kiválasztott elemet. Korábban emiatt endpoint-kattintáskor néha elveszett a kijelölés.
        if (m_objectDragActive) return;

        if (!clickedThisFrame || io.WantCaptureMouse || m_cameraLookActive) return;

        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(m_window, &mouseX, &mouseY);
        const ObjectId pickedId = pickObject(mouseX, mouseY);

        // Ha a kijelölt objektumra kattintottunk, a kattintásból transzformációs húzás indulhat.
        // Ha más objektumra kattintottunk, kijelölést váltunk.
        if (pickedId != InvalidObjectId && pickedId != m_selectedId)
        {
            m_selectedId = pickedId;
        }
        else if (pickedId == InvalidObjectId)
        {
            m_selectedId = InvalidObjectId;
        }
    }

    Application::UndoSnapshot Application::makeUndoSnapshot() const
    {
        UndoSnapshot snapshot;
        snapshot.network = m_network;
        snapshot.objects = m_objects;
        snapshot.architecturalObstacles = m_architecturalObstacles;
        snapshot.selectedId = m_selectedId;
        snapshot.lastRouteCreatedIds = m_lastRouteCreatedIds;
        snapshot.routeStartId = m_routeStartId;
        snapshot.routeStartEndpoint = m_routeStartEndpoint;
        snapshot.routeEndId = m_routeEndId;
        snapshot.routeEndEndpoint = m_routeEndEndpoint;
        snapshot.projectPath = m_projectPath;
        snapshot.pressureDesignPressureBar = m_pressureDesignPressureBar;
        snapshot.pressureTestPressureBar = m_pressureTestPressureBar;
        snapshot.pressureHoldTimeMin = m_pressureHoldTimeMin;
        snapshot.pressureInitialMeasuredPressureBar = m_pressureInitialMeasuredPressureBar;
        snapshot.pressureFinalMeasuredPressureBar = m_pressureFinalMeasuredPressureBar;
        snapshot.pressureAllowedDropBar = m_pressureAllowedDropBar;
        snapshot.flowRateLps = m_flowRateLps;
        snapshot.flowMinVelocityMps = m_flowMinVelocityMps;
        snapshot.flowMaxVelocityMps = m_flowMaxVelocityMps;
        snapshot.hydraulicFlowRateLps = m_hydraulicFlowRateLps;
        snapshot.hydraulicDensityKgM3 = m_hydraulicDensityKgM3;
        snapshot.hydraulicKinematicViscosityM2s = m_hydraulicKinematicViscosityM2s;
        snapshot.hydraulicMaxTotalPressureLossBar = m_hydraulicMaxTotalPressureLossBar;
        snapshot.validationStartId = m_validationStartId;
        snapshot.validationStartEndpoint = m_validationStartEndpoint;
        snapshot.validationEndId = m_validationEndId;
        snapshot.validationEndEndpoint = m_validationEndEndpoint;
        return snapshot;
    }

    void Application::restoreUndoSnapshot(const UndoSnapshot &snapshot)
    {
        // A snapshotból hiányzó régi scene objectek GPU meshét is felszabadítjuk.
        for (const SceneObject &oldObject : m_objects)
        {
            if (!oldObject.selectable) continue;

            const bool existsInSnapshot =
                std::any_of(snapshot.objects.begin(), snapshot.objects.end(),
                            [&](const SceneObject &object) { return object.id == oldObject.id; });

            if (!existsInSnapshot)
            {
                m_renderer.removeObject(oldObject.id);
            }
        }

        m_network = snapshot.network;
        m_objects = snapshot.objects;
        m_architecturalObstacles = snapshot.architecturalObstacles;
        m_selectedId = snapshot.selectedId;
        m_lastRouteCreatedIds = snapshot.lastRouteCreatedIds;

        m_routeStartId = snapshot.routeStartId;
        m_routeStartEndpoint = snapshot.routeStartEndpoint;
        m_routeEndId = snapshot.routeEndId;
        m_routeEndEndpoint = snapshot.routeEndEndpoint;

        m_projectPath = snapshot.projectPath;
        m_pressureDesignPressureBar = snapshot.pressureDesignPressureBar;
        m_pressureTestPressureBar = snapshot.pressureTestPressureBar;
        m_pressureHoldTimeMin = snapshot.pressureHoldTimeMin;
        m_pressureInitialMeasuredPressureBar = snapshot.pressureInitialMeasuredPressureBar;
        m_pressureFinalMeasuredPressureBar = snapshot.pressureFinalMeasuredPressureBar;
        m_pressureAllowedDropBar = snapshot.pressureAllowedDropBar;
        m_flowRateLps = snapshot.flowRateLps;
        m_flowMinVelocityMps = snapshot.flowMinVelocityMps;
        m_flowMaxVelocityMps = snapshot.flowMaxVelocityMps;
        m_hydraulicFlowRateLps = snapshot.hydraulicFlowRateLps;
        m_hydraulicDensityKgM3 = snapshot.hydraulicDensityKgM3;
        m_hydraulicKinematicViscosityM2s = snapshot.hydraulicKinematicViscosityM2s;
        m_hydraulicMaxTotalPressureLossBar = snapshot.hydraulicMaxTotalPressureLossBar;
        m_validationStartId = snapshot.validationStartId;
        m_validationStartEndpoint = snapshot.validationStartEndpoint;
        m_validationEndId = snapshot.validationEndId;
        m_validationEndEndpoint = snapshot.validationEndEndpoint;

        // Ha egy törölt elemet Undo hoz vissza, a GPU meshét is újra feltöltjük.
        for (const SceneObject &object : m_objects)
        {
            if (object.selectable)
            {
                m_renderer.uploadObject(object);
            }
        }

        m_routeAlternatives.clear();
        m_selectedRouteAlternative = 0;
        clearRoutePreview();

        m_objectDragActive = false;
        m_objectDragMoved = false;
        m_dragObjectIds.clear();
        m_dragAnchorPointIndex = -1;
        m_dragHasAnchor = false;
        m_constrainedElbowRotate = false;

        cleanupInvalidConnections();
        enforceElbowStraightConstraints();
        updateEndpointMarkers();

        m_validationHasRun = false;
        m_validationMessages.clear();
    }

    void Application::pushUndoSnapshot()
    {
        m_undoStack.push_back(makeUndoSnapshot());
        if (m_undoStack.size() > 50)
        {
            m_undoStack.erase(m_undoStack.begin());
        }
        m_redoStack.clear();
    }

    void Application::undoLastAction()
    {
        if (m_undoStack.empty())
        {
            setStatusMessage("Nincs visszavonható művelet.");
            return;
        }

        m_redoStack.push_back(makeUndoSnapshot());
        const UndoSnapshot snapshot = m_undoStack.back();
        m_undoStack.pop_back();
        restoreUndoSnapshot(snapshot);
        setStatusMessage("Visszavonás kész. Ctrl+Y = újra végrehajtás.");
    }

    void Application::redoLastAction()
    {
        if (m_redoStack.empty())
        {
            setStatusMessage("Nincs újra végrehajtható művelet.");
            return;
        }

        m_undoStack.push_back(makeUndoSnapshot());
        const UndoSnapshot snapshot = m_redoStack.back();
        m_redoStack.pop_back();
        restoreUndoSnapshot(snapshot);
        setStatusMessage("Újra végrehajtás kész. Ctrl+Z = visszavonás.");
    }

    void Application::handleUndoRedoShortcuts()
    {
        ImGuiIO &io = ImGui::GetIO();
        if (io.WantCaptureKeyboard) return;

        const bool ctrlDown = glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                              glfwGetKey(m_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

        // Magyar/QWERTZ kiosztásnál a Z és Y billentyűk felcserélődhetnek GLFW alatt,
        // ezért itt szándékosan megcseréljük őket: a felhasználói Ctrl+Z legyen visszavonás,
        // a felhasználói Ctrl+Y pedig újra végrehajtás.
        const bool zDown = ctrlDown && glfwGetKey(m_window, GLFW_KEY_Y) == GLFW_PRESS;
        const bool yDown = ctrlDown && glfwGetKey(m_window, GLFW_KEY_Z) == GLFW_PRESS;

        if (zDown && !m_ctrlZWasDown)
        {
            undoLastAction();
        }
        if (yDown && !m_ctrlYWasDown)
        {
            redoLastAction();
        }

        m_ctrlZWasDown = zDown;
        m_ctrlYWasDown = yDown;
    }

    void Application::handleInput(float deltaTime)
    {
        if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(m_window, true);

        ImGuiIO &io = ImGui::GetIO();

        handleUndoRedoShortcuts();

        const bool rightMouseDown = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        const bool canStartCameraLook = rightMouseDown && !io.WantCaptureMouse;

        if (canStartCameraLook)
        {
            if (!m_cameraLookActive)
            {
                m_cameraLookActive = true;
                m_firstMouseFrame = true;
                glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }
        }
        else
        {
            if (m_cameraLookActive)
            {
                m_cameraLookActive = false;
                m_firstMouseFrame = true;
                glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
        }

        if (m_cameraLookActive)
        {
            double mouseX = 0.0;
            double mouseY = 0.0;
            glfwGetCursorPos(m_window, &mouseX, &mouseY);

            if (m_firstMouseFrame)
            {
                m_lastMouseX = mouseX;
                m_lastMouseY = mouseY;
                m_firstMouseFrame = false;
            }

            const float deltaX = static_cast<float>(mouseX - m_lastMouseX);
            const float deltaY = static_cast<float>(m_lastMouseY - mouseY);
            m_lastMouseX = mouseX;
            m_lastMouseY = mouseY;

            m_camera.addRotation(deltaX * m_camera.mouseSensitivity, deltaY * m_camera.mouseSensitivity);
        }

        switchTransformModeFromKeyboard();
        handleObjectTransformInput();
        handleMouseSelection();

        if (io.WantCaptureKeyboard) return;

        float speed = m_camera.moveSpeed * deltaTime;
        if (glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
        {
            speed *= 3.0f;
        }

        const glm::vec3 worldUp{0.0f, 1.0f, 0.0f};

        const glm::vec3 forward = m_camera.forward();
        const glm::vec3 right = m_camera.right();

        if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) m_camera.position += forward * speed;
        if (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) m_camera.position -= forward * speed;
        if (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) m_camera.position += right * speed;
        if (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) m_camera.position -= right * speed;
        if (glfwGetKey(m_window, GLFW_KEY_E) == GLFW_PRESS) m_camera.position += worldUp * speed;
        if (glfwGetKey(m_window, GLFW_KEY_Q) == GLFW_PRESS) m_camera.position -= worldUp * speed;
    }
}
