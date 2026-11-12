#include "UI/EditorUi.hpp"
#include "UI/ValidationPanel.hpp"
#include "Bom/BillOfMaterials.hpp"
#include <imgui.h>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>
#include <string>
#include <cstdio>
#include <limits>

namespace plumbcad
{
    namespace
    {
        SceneObject *findSelected(std::vector<SceneObject> &objects, ObjectId selectedId)
        {
            for (auto &object : objects)
            {
                if (object.id == selectedId) return &object;
            }
            return nullptr;
        }

        float radToDeg(float value)
        {
            return value * 180.0f / glm::pi<float>();
        }

        bool modeButton(const char *label, TransformMode current, TransformMode value)
        {
            if (current == value)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                const bool pressed = ImGui::Button(label);
                ImGui::PopStyleVar();
                return pressed;
            }
            return ImGui::Button(label);
        }

        const char *modeName(TransformMode mode)
        {
            switch (mode)
            {
            case TransformMode::Move:
                return "Mozgatás";
            case TransformMode::Rotate:
                return "Forgatás";
            case TransformMode::Scale:
                return "Méret";
            }
            return "Ismeretlen";
        }

        const char *endpointLetter(int endpointIndex)
        {
            switch (endpointIndex)
            {
            case 0:
                return "A";
            case 1:
                return "B";
            case 2:
                return "C";
            default:
                return "?";
            }
        }

        ImVec4 endpointUiColor(int endpointIndex)
        {
            switch (endpointIndex)
            {
            case 0:
                return ImVec4{0.10f, 0.90f, 1.00f, 1.00f};

            case 1:
                return ImVec4{1.00f, 0.20f, 0.85f, 1.00f};

            default:
                return ImVec4{1.00f, 0.58f, 0.10f, 1.00f};
            }
        }

        const char *panelName(int panel)
        {
            switch (panel)
            {
            case 0:
                return "Objektum";
            case 1:
                return "Jelenet";
            case 2:
                return "Validáció";
            case 3:
                return "Útmutató";
            case 4:
                return "Útvonal";
            case 5:
                return "Akadályok";
            case 6:
                return "Darabjegyzék";
            }
            return "Objektum";
        }
    }

    UiActions EditorUi::draw(
        PipeNetwork &network, std::vector<SceneObject> &objects,
        const std::vector<ArchitecturalObstacle> &architecturalObstacles, ObjectId &selectedId,
        TransformMode &transformMode, bool &moveConnectedGroup, MoveAxisConstraint &moveAxisConstraint,
        bool &rotateConnectedGroup, RotateAxisConstraint &rotateAxisConstraint, float &rotationSnapDegrees,
        float rotationCurrentDegrees, int &fittingRotationFixedEndpoint, bool orthographicProjection,
        bool wireframeMode, ObjectId routeStartId, int routeStartEndpoint, ObjectId routeEndId,
        int routeEndEndpoint, float &routeGridSizeM, float &routeClearanceM, int &routePipeTypeIndex,
        PipeValidationProfile &routeValidationProfile, int lastRouteCreatedElementCount,
        float &pressureDesignPressureBar, float &pressureTestPressureBar, float &pressureHoldTimeMin,
        float &pressureInitialMeasuredPressureBar, float &pressureFinalMeasuredPressureBar,
        float &pressureAllowedDropBar, ObjectId validationStartId, int validationStartEndpoint,
        ObjectId validationEndId, int validationEndEndpoint, float &flowRateLps, float &hydraulicFlowRateLps,
        float &hydraulicDensityKgM3, float &hydraulicKinematicViscosityM2s,
        float &hydraulicMaxTotalPressureLossBar, const std::vector<RouteAlternative> &routeAlternatives,
        int &selectedRouteAlternative, const std::vector<ValidationMessage> &messages, bool validationHasRun,
        const std::string &statusMessage)
    {
        UiActions actions;
        SceneObject *selectedObject = findSelected(objects, selectedId);

        const auto pipeCategoryUiColor = [](int pipeTypeIndex) {
            switch (pipeTypeIndex)
            {
            case 0:
                return ImVec4{0.10f, 0.55f, 0.90f, 1.0f};

            case 1:
                return ImVec4{0.48f, 0.52f, 0.56f, 1.0f};

            case 2:
                return ImVec4{0.72f, 0.32f, 0.12f, 1.0f};

            default:
                return ImVec4{0.65f, 0.65f, 0.68f, 1.0f};
            }
        };

        // Az egyenes cső hosszának szerkesztési értékét külön tároljuk,
        // hogy az InputFloat gépelés közben ne álljon vissza minden képkockában.
        static ObjectId straightLengthEditObjectId = InvalidObjectId;
        static float straightLengthEditM = 1.0f;
        if (selectedId != straightLengthEditObjectId)
        {
            straightLengthEditObjectId = selectedId;
            const PipeElement *selectedPipeForLength = network.find(selectedId);
            straightLengthEditM =
                (selectedPipeForLength && selectedPipeForLength->type == PipeElementType::StraightPipe)
                    ? selectedPipeForLength->lengthM
                    : 1.0f;
        }

        ImGuiIO &mainIo = ImGui::GetIO();
        const float toolbarHeight = 112.0f;
        const float leftPanelWidth = 440.0f;
        static int activeLeftPanel = 0;

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(mainIo.DisplaySize.x, toolbarHeight), ImGuiCond_Always);
        ImGui::Begin("Felső eszköztár", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoTitleBar);

        ImGui::Text("PlumbCAD");
        ImGui::SameLine();
        ImGui::TextDisabled("| alap funkciók");

        if (ImGui::Button("Új cső")) actions.addPipe = true;
        ImGui::SameLine();
        if (ImGui::Button("90° könyök")) actions.addElbow90 = true;
        ImGui::SameLine();
        if (ImGui::Button("45° könyök")) actions.addElbow45 = true;
        ImGui::SameLine();
        if (ImGui::Button("T-idom")) actions.addTee = true;
        ImGui::SameLine();
        if (ImGui::Button("Szűkítő")) actions.addReducer = true;
        ImGui::SameLine();
        if (ImGui::Button("Szelep")) actions.addValve = true;
        ImGui::SameLine();
        if (ImGui::Button("Végzáró")) actions.addEndCap = true;
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::Button("Mentés")) actions.saveProject = true;
        ImGui::SameLine();
        if (ImGui::Button("Betöltés")) actions.loadProject = true;
        ImGui::Separator();
        ImGui::TextDisabled("Bal oldali panel:");
        ImGui::SameLine();
        if (ImGui::Button("Objektum")) activeLeftPanel = 0;
        ImGui::SameLine();
        if (ImGui::Button("Jelenet")) activeLeftPanel = 1;
        ImGui::SameLine();
        if (ImGui::Button("Validáció panel")) activeLeftPanel = 2;
        ImGui::SameLine();
        if (ImGui::Button("Útmutató")) activeLeftPanel = 3;
        ImGui::SameLine();
        if (ImGui::Button("Útvonal")) activeLeftPanel = 4;
        ImGui::SameLine();
        if (ImGui::Button("Akadályok")) activeLeftPanel = 5;
        ImGui::SameLine();
        if (ImGui::Button("Darabjegyzék")) activeLeftPanel = 6;
        ImGui::SameLine();
        ImGui::TextDisabled("Aktív: %s", panelName(activeLeftPanel));

        ImGui::Separator();

        ImGui::TextDisabled("Nézet:");
        ImGui::SameLine();

        if (ImGui::Button(orthographicProjection ? "Vetület: ORTO" : "Vetület: perspektíva"))
        {
            actions.toggleProjectionMode = true;
        }

        ImGui::SameLine();

        if (ImGui::Button(wireframeMode ? "Wireframe: BE" : "Wireframe: KI"))
        {
            actions.toggleWireframeMode = true;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("| Ctrl+Z / Ctrl+Y");

        ImGui::End();

        const float transformWindowHeight = transformMode == TransformMode::Rotate  ? 500.0f
                                            : transformMode == TransformMode::Scale ? 292.0f
                                                                                    : 226.0f;

        // Az Objektum panel csatlakozási műveletei külön, jobb oldali
        // lebegő ablakban jelennek meg, közvetlenül a szerkesztési eszközök fölött.
        // Más bal oldali panelen ez az ablak nem jelenik meg.
        if (activeLeftPanel == 0)
        {
            const PipeElement *selectedPipeForConnectionWindow = network.find(selectedId);

            const float availableConnectionHeight =
                std::max(135.0f, mainIo.DisplaySize.y - toolbarHeight - transformWindowHeight - 44.0f);

            const float connectionWindowHeight =
                selectedPipeForConnectionWindow ? std::clamp(availableConnectionHeight, 220.0f, 470.0f) : 135.0f;

            ImGui::SetNextWindowPos(
                ImVec2{mainIo.DisplaySize.x - 12.0f, mainIo.DisplaySize.y - transformWindowHeight - 20.0f},
                ImGuiCond_Always, ImVec2{1.0f, 1.0f});

            ImGui::SetNextWindowSize(ImVec2{350.0f, connectionWindowHeight}, ImGuiCond_Always);

            ImGui::Begin("Csatlakozás / új elem", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings);

            if (!selectedObject)
            {
                ImGui::Text("Nincs kijelölt objektum.");
            }
            else if (!selectedPipeForConnectionWindow)
            {
                ImGui::Text("Kijelölt: %s", selectedObject->name.c_str());
                ImGui::Text("Nincs csőcsatlakozás.");
            }
            else
            {
                ImGui::Text("Kijelölt cél: %s | ID: %u", selectedObject->name.c_str(),
                            static_cast<unsigned int>(selectedId));

                ImGui::Separator();

                ImGui::BeginChild("FloatingConnectionActionScroll", ImVec2{0.0f, 0.0f}, false,
                                  ImGuiWindowFlags_AlwaysVerticalScrollbar);

                for (int i = 0; i < static_cast<int>(selectedPipeForConnectionWindow->connections.size()); ++i)
                {
                    const ConnectionPoint &cp = selectedPipeForConnectionWindow->connections[i];

                    ImGui::PushID(i);

                    if (i > 0) ImGui::Separator();

                    ImGui::TextColored(endpointUiColor(i), "%s csatlakozás", endpointLetter(i));

                    if (cp.isOpen())
                    {
                        ImGui::TextColored(endpointUiColor(i), "NYITOTT");

                        if (routeStartId == selectedId && routeStartEndpoint == i)
                        {
                            ImGui::Text("A* START: EZ A CSATLAKOZÁS");
                        }

                        if (routeEndId == selectedId && routeEndEndpoint == i)
                        {
                            ImGui::Text("A* END: EZ A CSATLAKOZÁS");
                        }

                        if (validationStartId == selectedId && validationStartEndpoint == i)
                        {
                            ImGui::Text("Validáció START: EZ A CSATLAKOZÁS");
                        }

                        if (validationEndId == selectedId && validationEndEndpoint == i)
                        {
                            ImGui::Text("Validáció END: EZ A CSATLAKOZÁS");
                        }

                        ImGui::Spacing();

                        if (ImGui::SmallButton("Egyenes cső"))
                        {
                            actions.autoAttach = true;
                            actions.autoAttachEndpoint = i;
                            actions.autoAttachType = PipeElementType::StraightPipe;
                        }

                        ImGui::SameLine();

                        if (ImGui::SmallButton("90°"))
                        {
                            actions.autoAttach = true;
                            actions.autoAttachEndpoint = i;
                            actions.autoAttachType = PipeElementType::Elbow90;
                        }

                        ImGui::SameLine();

                        if (ImGui::SmallButton("45°"))
                        {
                            actions.autoAttach = true;
                            actions.autoAttachEndpoint = i;
                            actions.autoAttachType = PipeElementType::Elbow45;
                        }

                        ImGui::SameLine();

                        if (ImGui::SmallButton("T-idom"))
                        {
                            actions.autoAttach = true;
                            actions.autoAttachEndpoint = i;
                            actions.autoAttachType = PipeElementType::Tee;
                        }

                        if (ImGui::SmallButton("Szűkítő"))
                        {
                            actions.autoAttach = true;
                            actions.autoAttachEndpoint = i;
                            actions.autoAttachType = PipeElementType::Reducer;
                        }

                        ImGui::SameLine();

                        if (ImGui::SmallButton("Szelep"))
                        {
                            actions.autoAttach = true;
                            actions.autoAttachEndpoint = i;
                            actions.autoAttachType = PipeElementType::Valve;
                        }

                        ImGui::SameLine();

                        if (ImGui::SmallButton("Végzáró"))
                        {
                            actions.autoAttach = true;
                            actions.autoAttachEndpoint = i;
                            actions.autoAttachType = PipeElementType::EndCap;
                        }

                        ImGui::Spacing();

                        if (ImGui::SmallButton("A* START"))
                        {
                            actions.routeSetStart = true;
                            actions.routeObjectId = selectedId;
                            actions.routeEndpoint = i;
                        }

                        ImGui::SameLine();

                        if (ImGui::SmallButton("A* END"))
                        {
                            actions.routeSetEnd = true;
                            actions.routeObjectId = selectedId;
                            actions.routeEndpoint = i;
                        }

                        bool intentionalOpenEnd = cp.intentionalOpenEnd;

                        if (ImGui::Checkbox("Szándékosan nyitott vég", &intentionalOpenEnd))
                        {
                            actions.intentionalOpenEndChanged = true;
                            actions.intentionalOpenEndObjectId = selectedId;
                            actions.intentionalOpenEndEndpoint = i;
                            actions.intentionalOpenEndValue = intentionalOpenEnd;
                        }
                    }
                    else
                    {
                        const SceneObject *connectedObject = findSelected(objects, cp.connectedElementId);

                        ImGui::Text("Állapot: KAPCSOLVA");

                        if (connectedObject)
                        {
                            ImGui::TextDisabled("Kapcsolódik: %s | ID: %u", connectedObject->name.c_str(),
                                                static_cast<unsigned int>(cp.connectedElementId));
                        }
                        else
                        {
                            ImGui::TextDisabled("Kapcsolódó elem ID: %u",
                                                static_cast<unsigned int>(cp.connectedElementId));
                        }
                    }

                    if (ImGui::SmallButton("Validáció START"))
                    {
                        actions.validationSetStart = true;
                        actions.validationObjectId = selectedId;
                        actions.validationEndpoint = i;
                    }

                    ImGui::SameLine();

                    if (ImGui::SmallButton("Validáció END"))
                    {
                        actions.validationSetEnd = true;
                        actions.validationObjectId = selectedId;
                        actions.validationEndpoint = i;
                    }

                    ImGui::PopID();
                }

                ImGui::EndChild();
            }

            ImGui::End();
        }

        // Mindig elérhető, fix jobb alsó szerkesztési eszköztár.
        // Nem függ attól, melyik bal oldali panel van megnyitva.
        const ImVec2 transformWindowSize{350.0f, transformWindowHeight};

        ImGui::SetNextWindowPos(ImVec2{mainIo.DisplaySize.x - 12.0f, mainIo.DisplaySize.y - 12.0f},
                                ImGuiCond_Always, ImVec2{1.0f, 1.0f});

        ImGui::SetNextWindowSize(transformWindowSize, ImGuiCond_Always);

        ImGui::Begin("Szerkesztési eszközök", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings);

        if (ImGui::BeginTable("FloatingTransformModeTable", 3, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();

            if (modeButton("Mozgatás (1)", transformMode, TransformMode::Move))
            {
                transformMode = TransformMode::Move;
            }

            ImGui::TableNextColumn();

            if (modeButton("Forgatás (2)", transformMode, TransformMode::Rotate))
            {
                transformMode = TransformMode::Rotate;
            }

            ImGui::TableNextColumn();

            if (modeButton("Méret (3)", transformMode, TransformMode::Scale))
            {
                transformMode = TransformMode::Scale;
            }

            ImGui::EndTable();
        }

        ImGui::Text("Aktív eszköz: %s", modeName(transformMode));

        ImGui::Separator();

        ImGui::Checkbox("Kapcsolt hálózatrész együtt mozgatása", &moveConnectedGroup);

        if (transformMode == TransformMode::Move)
        {
            ImGui::SeparatorText("Mozgatási tengely");

            if (ImGui::RadioButton("Szabad", moveAxisConstraint == MoveAxisConstraint::Free))
            {
                moveAxisConstraint = MoveAxisConstraint::Free;
            }

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.95f, 0.18f, 0.16f, 1.0f});

            if (ImGui::RadioButton("X", moveAxisConstraint == MoveAxisConstraint::X))
            {
                moveAxisConstraint = MoveAxisConstraint::X;
            }

            ImGui::PopStyleColor();

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.18f, 0.88f, 0.28f, 1.0f});

            if (ImGui::RadioButton("Y", moveAxisConstraint == MoveAxisConstraint::Y))
            {
                moveAxisConstraint = MoveAxisConstraint::Y;
            }

            ImGui::PopStyleColor();

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.18f, 0.42f, 1.00f, 1.0f});

            if (ImGui::RadioButton("Z", moveAxisConstraint == MoveAxisConstraint::Z))
            {
                moveAxisConstraint = MoveAxisConstraint::Z;
            }

            ImGui::PopStyleColor();

            const char *axisName = "Szabad";

            switch (moveAxisConstraint)
            {
            case MoveAxisConstraint::X:
                axisName = "X";
                break;

            case MoveAxisConstraint::Y:
                axisName = "Y";
                break;

            case MoveAxisConstraint::Z:
                axisName = "Z";
                break;

            case MoveAxisConstraint::Free:
            default:
                break;
            }

            ImGui::Text("Aktív mozgatás: %s", axisName);

            ImGui::TextDisabled("Ctrl: 10 mm raszter | Ctrl+Shift: 1 mm");
        }

        if (transformMode == TransformMode::Rotate)
        {
            const PipeElement *selectedPipeForRotation = network.find(selectedId);

            const bool connectorPivotRotation =
                selectedPipeForRotation &&
                (selectedPipeForRotation->type == PipeElementType::Elbow90 ||
                 selectedPipeForRotation->type == PipeElementType::Elbow45 ||
                 selectedPipeForRotation->type == PipeElementType::Tee) &&
                std::any_of(selectedPipeForRotation->connections.begin(),
                            selectedPipeForRotation->connections.end(),
                            [](const ConnectionPoint &point) { return !point.isOpen(); });

            ImGui::SeparatorText("Világ XYZ referencia");

            ImGui::TextColored(ImVec4{0.95f, 0.18f, 0.16f, 1.0f}, "X");

            ImGui::SameLine();

            ImGui::TextColored(ImVec4{0.18f, 0.88f, 0.28f, 1.0f}, "Y");

            ImGui::SameLine();

            ImGui::TextColored(ImVec4{0.18f, 0.42f, 1.00f, 1.0f}, "Z");

            ImGui::SameLine();

            ImGui::TextDisabled("| origó: (0.000, 0.000, 0.000)");

            ImGui::TextDisabled("Y = függőleges | XZ = vízszintes sík | Y=0 a vízszintes alapsík");

            if (selectedObject)
            {
                const float worldRotX = radToDeg(selectedObject->transform.rotationEuler.x);

                const float worldRotY = radToDeg(selectedObject->transform.rotationEuler.y);

                const float worldRotZ = radToDeg(selectedObject->transform.rotationEuler.z);

                ImGui::Text("Pozíció: X %.3f | Y %.3f | Z %.3f m", selectedObject->transform.position.x,
                            selectedObject->transform.position.y, selectedObject->transform.position.z);

                ImGui::Text("Abszolút világ-forgás: X %.2f° | Y %.2f° | Z %.2f°", worldRotX, worldRotY, worldRotZ);

                ImGui::TextDisabled("X=0°, Y=0°, Z=0° = az elem alaphelyzete a világ XYZ rendszerhez képest.");
            }

            if (connectorPivotRotation)
            {
                int firstConnected = -1;

                for (int endpointIndex = 0;
                     endpointIndex < static_cast<int>(selectedPipeForRotation->connections.size());
                     ++endpointIndex)
                {
                    if (!selectedPipeForRotation->connections[endpointIndex].isOpen())
                    {
                        firstConnected = endpointIndex;

                        break;
                    }
                }

                const bool selectedFixedEndpointIsConnected =
                    fittingRotationFixedEndpoint >= 0 &&
                    fittingRotationFixedEndpoint < static_cast<int>(selectedPipeForRotation->connections.size()) &&
                    !selectedPipeForRotation->connections[fittingRotationFixedEndpoint].isOpen();

                if (!selectedFixedEndpointIsConnected)
                {
                    fittingRotationFixedEndpoint = firstConnected;
                }

                const char *fittingRotationTitle =
                    selectedPipeForRotation->type == PipeElementType::Tee       ? "T-idom forgatási csatlakozás"
                    : selectedPipeForRotation->type == PipeElementType::Elbow90 ? "90° idom forgatási csatlakozás"
                                                                                : "45° idom forgatási csatlakozás";

                ImGui::SeparatorText(fittingRotationTitle);

                ImGui::TextDisabled("Válaszd ki, melyik már kapcsolt csatlakozás maradjon fix.");

                bool firstVisibleButton = true;

                for (int endpointIndex = 0;
                     endpointIndex < static_cast<int>(selectedPipeForRotation->connections.size());
                     ++endpointIndex)
                {
                    if (selectedPipeForRotation->connections[endpointIndex].isOpen())
                    {
                        continue;
                    }

                    if (!firstVisibleButton)
                    {
                        ImGui::SameLine();
                    }

                    firstVisibleButton = false;

                    ImGui::PushStyleColor(ImGuiCol_Text, endpointUiColor(endpointIndex));

                    const std::string endpointLabel = std::string(endpointLetter(endpointIndex)) +
                                                      " csatlakozás##fitting_rotation_pivot_" +
                                                      std::to_string(endpointIndex);

                    if (ImGui::RadioButton(endpointLabel.c_str(), fittingRotationFixedEndpoint == endpointIndex))
                    {
                        fittingRotationFixedEndpoint = endpointIndex;
                    }

                    ImGui::PopStyleColor();
                }

                if (fittingRotationFixedEndpoint >= 0)
                {
                    ImGui::Text("Fix forgáspont: %s csatlakozás", endpointLetter(fittingRotationFixedEndpoint));
                }
            }

            ImGui::SeparatorText("Forgatási tengely");

            if (connectorPivotRotation)
            {
                ImGui::TextDisabled("A kiválasztott kapcsolt csővég helye és tengelye lesz a forgási tengely.");
            }

            ImGui::BeginDisabled(connectorPivotRotation);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.95f, 0.18f, 0.16f, 1.0f});

            if (ImGui::RadioButton("X##rotate_axis", rotateAxisConstraint == RotateAxisConstraint::X))
            {
                rotateAxisConstraint = RotateAxisConstraint::X;
            }

            ImGui::PopStyleColor();

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.18f, 0.88f, 0.28f, 1.0f});

            if (ImGui::RadioButton("Y##rotate_axis", rotateAxisConstraint == RotateAxisConstraint::Y))
            {
                rotateAxisConstraint = RotateAxisConstraint::Y;
            }

            ImGui::PopStyleColor();

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.18f, 0.42f, 1.00f, 1.0f});

            if (ImGui::RadioButton("Z##rotate_axis", rotateAxisConstraint == RotateAxisConstraint::Z))
            {
                rotateAxisConstraint = RotateAxisConstraint::Z;
            }

            ImGui::PopStyleColor();

            ImGui::EndDisabled();

            ImGui::SameLine();

            ImGui::TextDisabled(connectorPivotRotation ? "| csatlakozási tengely" : "| világ-tengely");

            ImGui::BeginDisabled(connectorPivotRotation);

            ImGui::Checkbox("Teljes kapcsolt hálózat együtt forgatása", &rotateConnectedGroup);

            ImGui::EndDisabled();

            if (connectorPivotRotation)
            {
                ImGui::TextDisabled(
                    "A fix csatlakozás másik oldala nem mozog; az idom többi ágához kapcsolt elemek együtt követik a forgatást.");
            }
            else if (rotateConnectedGroup)
            {
                ImGui::TextDisabled(
                    "A kijelölt elemhez kapcsolódó összes cső és idom egy merev egységként forog.");
            }

            ImGui::SeparatorText("Szög raszter");

            if (ImGui::RadioButton("Szabad##rotation_snap", rotationSnapDegrees <= 0.0f))
            {
                rotationSnapDegrees = 0.0f;
            }

            ImGui::SameLine();

            if (ImGui::RadioButton("15°##rotation_snap", std::abs(rotationSnapDegrees - 15.0f) < 0.01f))
            {
                rotationSnapDegrees = 15.0f;
            }

            ImGui::SameLine();

            if (ImGui::RadioButton("5°##rotation_snap", std::abs(rotationSnapDegrees - 5.0f) < 0.01f))
            {
                rotationSnapDegrees = 5.0f;
            }

            ImGui::SameLine();

            if (ImGui::RadioButton("1°##rotation_snap", std::abs(rotationSnapDegrees - 1.0f) < 0.01f))
            {
                rotationSnapDegrees = 1.0f;
            }

            ImGui::SeparatorText("Forgatási szög");

            ImGui::Text("Aktuális abszolút szög: %.2f fok", rotationCurrentDegrees);

            static float exactRotationDegrees = 90.0f;

            ImGui::SetNextItemWidth(120.0f);

            ImGui::InputFloat("Kívánt szög [fok]", &exactRotationDegrees, 1.0f, 15.0f, "%.2f");

            ImGui::SameLine();

            if (ImGui::Button("Beállít"))
            {
                actions.applyExactRotation = true;

                actions.exactRotationDegrees = exactRotationDegrees;
            }

            ImGui::TextDisabled(
                connectorPivotRotation
                    ? "Egérrel: a kiválasztott kapcsolt csővég körül forgat."
                    : "Egérrel: a kijelölt elem középpontja körül, a világ X/Y/Z tengelyei szerint forgat.");

            ImGui::TextDisabled("A Kívánt szög ABSZOLÚT célérték: nem hozzáadódik a korábbi forgatáshoz.");

            ImGui::TextDisabled("Példa: jelenlegi 90°, beírt 45° -> az idom 45°-ra áll vissza.");
        }

        if (transformMode == TransformMode::Scale)
        {
            ImGui::SeparatorText("Katalógusméret");

            PipeElement *selectedPipeForSize = network.find(selectedId);

            if (!selectedPipeForSize)
            {
                ImGui::TextDisabled("Jelölj ki egy csövet vagy csőidomot a méret kiválasztásához.");
            }
            else
            {
                const auto &catalog = network.catalog();

                if (selectedPipeForSize->pipeTypeIndex < 0 ||
                    selectedPipeForSize->pipeTypeIndex >= static_cast<int>(catalog.size()))
                {
                    ImGui::TextDisabled("Ehhez az elemhez nem található érvényes katalógus.");
                }
                else
                {
                    const PipeTypeDefinition &pipeType = catalog[selectedPipeForSize->pipeTypeIndex];

                    ImGui::TextDisabled("Csőtípus és katalógusméret külön is választható.");

                    ImGui::SetNextItemWidth(-1.0f);

                    if (ImGui::BeginCombo("Csőtípus", pipeType.name.c_str()))
                    {
                        for (int typeIndex = 0; typeIndex < static_cast<int>(catalog.size()); ++typeIndex)
                        {
                            const bool selected = typeIndex == selectedPipeForSize->pipeTypeIndex;

                            const std::string label =
                                catalog[typeIndex].name + "##catalog_pipe_type_" + std::to_string(typeIndex);

                            if (ImGui::Selectable(label.c_str(), selected))
                            {
                                actions.setCatalogPipeType = true;

                                actions.catalogPipeTypeObjectId = selectedId;

                                actions.catalogPipeTypeIndex = typeIndex;
                            }

                            if (selected) ImGui::SetItemDefaultFocus();
                        }

                        ImGui::EndCombo();
                    }

                    ImGui::Text("Anyag: %s", pipeType.material.c_str());

                    if (pipeType.material == "PVC-U")
                    {
                        ImGui::TextDisabled("Table 6 méretek: Ø75 -> DN75 | Ø110 -> DN100 | Ø160 -> DN150.");
                    }
                    else
                    {
                        ImGui::TextDisabled("A vízellátási sebességprofil a számított belső átmérőt használja.");
                    }

                    const auto sizeText = [&](const PipeCatalogSize &size) {
                        char buffer[128];

                        if (pipeType.material == "PVC-U")
                        {
                            const int referenceDn =
                                size.diameterMm >= 150.0f ? 150 : (size.diameterMm >= 100.0f ? 100 : 75);

                            std::snprintf(buffer, sizeof(buffer), "DN%d ref. | Ø%.1f × %.1f mm", referenceDn,
                                          size.diameterMm, size.wallThicknessMm);
                        }
                        else
                        {
                            std::snprintf(buffer, sizeof(buffer), "Ø%.1f × %.1f mm", size.diameterMm,
                                          size.wallThicknessMm);
                        }

                        return std::string(buffer);
                    };

                    if (pipeType.sizes.empty())
                    {
                        ImGui::TextDisabled("Ehhez a csőtípushoz nincs megadott méret.");
                    }
                    else
                    {
                        const int currentPrimaryIndex = std::clamp(selectedPipeForSize->pipeSizeIndex, 0,
                                                                   static_cast<int>(pipeType.sizes.size()) - 1);

                        const std::string primaryPreview = sizeText(pipeType.sizes[currentPrimaryIndex]);

                        const char *primaryLabel =
                            selectedPipeForSize->type == PipeElementType::Reducer ? "Elsődleges méret" : "Méret";

                        ImGui::SetNextItemWidth(-1.0f);

                        if (ImGui::BeginCombo(primaryLabel, primaryPreview.c_str()))
                        {
                            for (int sizeIndex = 0; sizeIndex < static_cast<int>(pipeType.sizes.size());
                                 ++sizeIndex)
                            {
                                if (selectedPipeForSize->type == PipeElementType::Reducer &&
                                    selectedPipeForSize->secondaryPipeSizeIndex == sizeIndex)
                                {
                                    continue;
                                }

                                const std::string itemLabel = sizeText(pipeType.sizes[sizeIndex]) +
                                                              "##primary_size_" + std::to_string(sizeIndex);

                                const bool selected = sizeIndex == currentPrimaryIndex;

                                if (ImGui::Selectable(itemLabel.c_str(), selected))
                                {
                                    actions.setCatalogPipeSize = true;

                                    actions.catalogPipeSizeObjectId = selectedId;

                                    actions.catalogPipeSizeIndex = sizeIndex;
                                }

                                if (selected) ImGui::SetItemDefaultFocus();
                            }

                            ImGui::EndCombo();
                        }

                        if (selectedPipeForSize->type == PipeElementType::Reducer)
                        {
                            const int currentSecondaryIndex =
                                std::clamp(selectedPipeForSize->secondaryPipeSizeIndex, 0,
                                           static_cast<int>(pipeType.sizes.size()) - 1);

                            const std::string secondaryPreview = sizeText(pipeType.sizes[currentSecondaryIndex]);

                            ImGui::SetNextItemWidth(-1.0f);

                            if (ImGui::BeginCombo("Másodlagos méret", secondaryPreview.c_str()))
                            {
                                for (int sizeIndex = 0; sizeIndex < static_cast<int>(pipeType.sizes.size());
                                     ++sizeIndex)
                                {
                                    if (sizeIndex == selectedPipeForSize->pipeSizeIndex)
                                    {
                                        continue;
                                    }

                                    const std::string itemLabel = sizeText(pipeType.sizes[sizeIndex]) +
                                                                  "##secondary_size_" + std::to_string(sizeIndex);

                                    const bool selected = sizeIndex == currentSecondaryIndex;

                                    if (ImGui::Selectable(itemLabel.c_str(), selected))
                                    {
                                        actions.setReducerSecondarySize = true;

                                        actions.reducerObjectId = selectedId;

                                        actions.reducerSecondarySizeIndex = sizeIndex;
                                    }

                                    if (selected) ImGui::SetItemDefaultFocus();
                                }

                                ImGui::EndCombo();
                            }
                        }

                        ImGui::Text("Jelenlegi: Ø%.1f × %.1f mm", selectedPipeForSize->diameterMm,
                                    selectedPipeForSize->wallThicknessMm);

                        const float currentInnerDiameter =
                            selectedPipeForSize->diameterMm - 2.0f * selectedPipeForSize->wallThicknessMm;

                        ImGui::Text("Számított belső átmérő: %.1f mm", currentInnerDiameter);

                        if (pipeType.material == "PVC-U")
                        {
                            const int currentReferenceDn =
                                selectedPipeForSize->diameterMm >= 150.0f
                                    ? 150
                                    : (selectedPipeForSize->diameterMm >= 100.0f ? 100 : 75);

                            ImGui::Text("Lefolyási referencia: Table 6 DN%d", currentReferenceDn);
                        }

                        ImGui::TextDisabled("Csőtípus- vagy méretváltáskor a geometria automatikusan újraépül.");
                    }
                }
            }
        }

        ImGui::TextDisabled("Mindig elérhető | gyorsbillentyűk: 1 / 2 / 3");

        ImGui::End();

        if (activeLeftPanel != 0)
        {
            ImGui::SetNextWindowPos(ImVec2(0.0f, toolbarHeight), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, mainIo.DisplaySize.y - toolbarHeight),
                                     ImGuiCond_Always);
            ImGui::Begin("Bal oldali panel", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
            ImGui::Text("%s panel", panelName(activeLeftPanel));
            ImGui::SameLine();
            ImGui::TextDisabled("| felső eszköztárból váltható");

            if (activeLeftPanel == 1)
            {
                ImGui::SeparatorText("Áttekintés");
                const std::string selectedText =
                    selectedId == InvalidObjectId ? std::string("nincs") : std::to_string(selectedId);
                if (ImGui::BeginTable("SceneOverviewTable", 2,
                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("Csőelemek");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", static_cast<int>(network.elements().size()));

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("Kijelölt ID");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", selectedText.c_str());

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("Eszköz");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", modeName(transformMode));

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("Vetület");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", orthographicProjection ? "ortografikus" : "perspektivikus");

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("Megjelenítés");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", wireframeMode ? "wireframe" : "árnyalt / kitöltött");

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("Mozgatás");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", moveConnectedGroup ? "kapcsolt hálózatrész együtt" : "csak kijelölt elem");

                    ImGui::EndTable();
                }

                ImGui::Spacing();
                ImGui::BeginChild("StatusBox", ImVec2(0.0f, 54.0f), true);
                ImGui::TextDisabled("Állapot");
                ImGui::TextWrapped("%s", statusMessage.c_str());
                ImGui::EndChild();

                ImGui::SeparatorText("Objektum lista");
                ImGui::TextDisabled("Kattints egy elemre a kijelöléshez.");
                ImGui::BeginChild("ObjectListBox", ImVec2(0.0f, 145.0f), true);
                for (auto &object : objects)
                {
                    if (!object.selectable) continue;
                    const bool selected = object.id == selectedId;
                    const std::string label = object.name + "  |  ID: " + std::to_string(object.id);
                    if (ImGui::Selectable(label.c_str(), selected))
                    {
                        selectedId = object.id;
                    }
                }
                ImGui::EndChild();

                ImGui::SeparatorText("Kijelölt objektum");
                if (selectedObject)
                {
                    ImGui::TextWrapped(
                        "A kijelölt objektum adatai és módosítható beállításai a bal oldali Objektum panelen találhatók.");
                }
                else
                {
                    ImGui::TextDisabled(
                        "Nincs kijelölt objektum. Bal egérgombbal kattints egy csőre, vagy válaszd ki a listából.");
                }
            }
            else if (activeLeftPanel == 2)
            {
                ValidationPanelModel validationPanel{selectedId,
                                                     pressureDesignPressureBar,
                                                     pressureTestPressureBar,
                                                     pressureHoldTimeMin,
                                                     pressureInitialMeasuredPressureBar,
                                                     pressureFinalMeasuredPressureBar,
                                                     pressureAllowedDropBar,
                                                     validationStartId,
                                                     validationStartEndpoint,
                                                     validationEndId,
                                                     validationEndEndpoint,
                                                     flowRateLps,
                                                     hydraulicFlowRateLps,
                                                     hydraulicDensityKgM3,
                                                     hydraulicKinematicViscosityM2s,
                                                     hydraulicMaxTotalPressureLossBar,
                                                     messages,
                                                     validationHasRun,
                                                     statusMessage};

                drawValidationPanel(validationPanel, actions);
            }
            else if (activeLeftPanel == 3)
            {
                ImGui::SeparatorText("Útmutató");

                ImGui::SeparatorText("Nézet és kamera");
                ImGui::BulletText(
                    "3D módban a felső eszköztárból Perspektivikus / Ortografikus vetület választható.");
                ImGui::BulletText(
                    "Wireframe BE: a modellek háromszögháló-élei jelennek meg, kitöltött felületek nélkül.");
                ImGui::BulletText("3D nézetben: jobb egérgomb + egérmozgatás forgatja a kamerát.");
                ImGui::BulletText("3D nézetben: W/A/S/D mozgás, Q/E le-fel mozgás, Shift gyorsítás.");

                ImGui::SeparatorText("Objektumok létrehozása");
                ImGui::BulletText("Új cső: egyenes csőszakasz beszúrása.");
                ImGui::BulletText("90° könyök / 45° könyök / T-idom: irányváltó és elágazó idom beszúrása.");
                ImGui::BulletText("Szűkítő: két külön katalógusméretet összekötő átmeneti idom.");
                ImGui::BulletText("Szelep: kétcsatlakozásos szerelvény, Darcy prototípus K = 2.00.");
                ImGui::BulletText("Végzáró: egyetlen csatlakozású lezáró elem.");
                ImGui::BulletText(
                    "Nyitott csővégnél az Objektum panel megnyitásakor a jobb oldali „Csatlakozás / új elem” ablakból adhatsz hozzá új csövet vagy idomot.");
                ImGui::BulletText(
                    "Az A* START/END és Validáció START/END kiválasztás is ebben a külön csatlakozási ablakban található.");

                ImGui::SeparatorText("Színek és kijelölés");
                ImGui::BulletText("PPR: kék | PVC-U: szürke | Réz: rézbarna.");
                ImGui::BulletText("Minden építészeti akadály piros.");
                ImGui::BulletText("A kijelölt elem sárga.");
                ImGui::BulletText(
                    "Nyitott csatlakozók: A=cián, B=magenta, C=narancs; az Objektum panel ugyanezekkel a jelölésekkel hivatkozik rájuk.");

                ImGui::SeparatorText("Kijelölés és transzformáció");
                ImGui::BulletText("Bal kattintás: objektum kijelölése.");
                ImGui::BulletText(
                    "Az Objektum panel 'Kijelölt elem törlése' gombjával bármely cső vagy idom egyenként törölhető.");
                ImGui::BulletText("A jobb alsó Szerkesztési eszközök ablak mindig látható; 1: Mozgatás eszköz.");
                ImGui::BulletText(
                    "Ha az Objektum panel aktív, közvetlenül fölötte jelenik meg a Csatlakozás / új elem ablak.");
                ImGui::BulletText("2: Forgatás eszköz.");
                ImGui::BulletText(
                    "Mozgatás vagy Forgatás módban a kijelölt objektum közepén CAD-szerű X/Y/Z tengelynyilak jelennek meg.");
                ImGui::BulletText("Tengelyszínek: X = piros, Y = zöld, Z = kék.");
                ImGui::BulletText("3: Méret eszköz; a katalógusból választható ki a kívánt csőméret.");
                ImGui::BulletText("Bal egérhúzás: az aktív eszköz használata.");
                ImGui::BulletText(
                    "Mozgatáskor a tárgy a kurzor valódi 3D munkasíkját követi; nincs fix pixel-szorzó.");
                ImGui::BulletText(
                    "A jobb alsó Szerkesztési eszközök ablakban választható: Szabad, X, Y vagy Z tengelyű mozgatás.");
                ImGui::BulletText("Ctrl mozgatás közben: 10 mm világ-raszter; Ctrl+Shift: 1 mm finom raszter.");
                ImGui::BulletText("Forgatásnál a Szerkesztési eszközök ablakban X/Y/Z világ-tengely választható.");
                ImGui::BulletText(
                    "A jelenet központi világkoordináta-rendszere: origó (0,0,0), X piros, Y zöld, Z kék; az XZ sík vízszintes, Y felfelé mutat.");
                ImGui::BulletText(
                    "A Forgatás panel mutatja a kijelölt elem abszolút világ X/Y/Z forgásszögeit, így a szögek egyértelműen a központi rendszerhez viszonyíthatók.");
                ImGui::BulletText(
                    "Kapcsolt 90°/45°/T-idomnál a Forgatás eszközben kiválasztható, melyik már kapcsolt A/B/C csatlakozás legyen a fix forgáspont és tengely.");
                ImGui::BulletText(
                    "90° és 45° idomnál A/B, T-idomnál A/B/C közül csak a ténylegesen kapcsolt pontok választhatók.");
                ImGui::BulletText(
                    "A Teljes kapcsolt hálózat együtt forgatása bekapcsolva a kijelölt elem teljes kapcsolt komponense merev egységként fordul.");
                ImGui::BulletText(
                    "Forgatásnál az aktuális abszolút szög a tényleges idom-geometriából számolódik, ezért kijelölésváltás után sem nullázódik.");
                ImGui::BulletText(
                    "A Kívánt szög mező abszolút célérték: 90° után 45°-ot beírva az idom 45°-ra áll, nem további +45°-ot fordul.");
                ImGui::BulletText("Forgatásnál Szabad, 15°, 5° vagy 1° szögrács használható.");
                ImGui::BulletText(
                    "Forgatás befejezésekor az 5 cm-en belüli nyitott csővégre ugyanúgy ráilleszkedik és irányba igazodik az elem, mint Mozgatás után.");
                ImGui::BulletText("Shift húzás közben: finomabb mozgatás vagy forgatás.");

                ImGui::SeparatorText("Kapcsolt hálózatrész mozgatása");
                ImGui::BulletText("Ha be van kapcsolva, a kijelölt elemhez kapcsolt részhálózat együtt mozog.");
                ImGui::BulletText("Snap esetén a részhálózat teljes egésze igazodik a cél csőelemhez.");
                ImGui::BulletText("Ha ki van kapcsolva, csak a kijelölt elem mozog.");

                ImGui::SeparatorText("Snap és csatlakozás");
                ImGui::BulletText("Elengedéskor a közeli nyitott csővégek automatikusan összekapcsolódnak.");
                ImGui::BulletText(
                    "Egyenes-egyenes kapcsolatnál a mozgó rész a cél egyenes cső tengelyének folytatásába igazodik.");
                ImGui::BulletText("Idom-egyenes kapcsolatnál az idom/cső a csatlakozási tengelyhez igazodik.");
                ImGui::BulletText("Kék gyűrű: automatikus útvonal kezdőpont.");
                ImGui::BulletText("Lila gyűrű: automatikus útvonal végpont.");
                ImGui::BulletText("Narancs gyűrű: közös Validáció START.");
                ImGui::BulletText("Cián gyűrű: közös Validáció END.");
                ImGui::BulletText("Zöld gyűrű: már kapcsolt csatlakozás.");

                ImGui::SeparatorText("Kézi idomforgatás");
                ImGui::BulletText("Csatlakoztatott idomnál kiválasztható, melyik csatlakozás legyen fix.");
                ImGui::BulletText("A forgatás az aktív Forgatás eszközzel, egérrel történik.");
                ImGui::BulletText("A külön X/Y/Z fokmegadás szándékosan nincs a felületen.");

                ImGui::SeparatorText("Kompakt idomok");
                ImGui::BulletText(
                    "A 90° könyök kompaktabb: a 45° referencia-könyök hajlítási sugarának kb. felét használja.");
                ImGui::BulletText("A T-idom alapmérete 0.14 m főág / 0.07 m leágazás.");
                ImGui::BulletText("Az A* által létrehozott 90° könyökök is a kompakt geometriát használják.");

                ImGui::SeparatorText("Akadályok és A*");
                ImGui::BulletText("Az Építészet panelen fal, födém, akna és tiltott zóna hozható létre.");
                ImGui::BulletText("Az aktív építészeti objektumokat az A* automatikusan kikerüli.");

                ImGui::SeparatorText("Nyílt validációs referenciák");
                ImGui::BulletText(
                    "A nyomáspróba egy kijelölt referenciaelem teljes kapcsolt hálózatrészére fut; minden bemenete a Validáció panelen adható meg.");
                ImGui::BulletText(
                    "Az Objektum panelen egyetlen közös Validáció START és Validáció END pontot kell kiválasztani.");
                ImGui::BulletText(
                    "A nyílt vízellátási sebesség, Approved Document H Table 6 lejtés/kapacitás és Darcy-Weisbach ugyanazt a közös START -> END útvonalat használja.");
                ImGui::BulletText(
                    "A nyílt referencia-profilok ingyenesen hozzáférhető dokumentumokból származnak, de NEM MSZ EN 806 / MSZ EN 1610 megfelelőség-tanúsítások.");

                ImGui::SeparatorText("Validáció");
                ImGui::BulletText(
                    "A nyitott csővég ellenőrzés elfogadja a Csővégek résznél szándékos végpontként megjelölt nyitott csatlakozásokat.");
                ImGui::BulletText(
                    "Az izolált elem vizsgálat csak a nulla érvényes kapcsolattal rendelkező elemeket tekinti izoláltnak.");
                ImGui::BulletText(
                    "Az összefüggőségi vizsgálat minden elemet komponensekre bont, és hibának jelzi a közvetlenül összekapcsolt eltérő csőátmérőket is.");
                ImGui::BulletText(
                    "A vízhálózati alapadat-ellenőrzés a projekt összes cső/idom elemének geometriai és katalógus alapadatait vizsgálja.");
                ImGui::BulletText(
                    "A nyílt vízellátási sebességprofil START -> END útvonalon fut: 1.5 m/s ajánlott cél, 3.0 m/s NCC felső határ.");
                ImGui::BulletText(
                    "A közös Validáció START -> END irány határozza meg a gravitációs vízhaladást is.");
                ImGui::BulletText(
                    "A nyílt vízellátási sebesség, Approved Document H Table 6 és Darcy-Weisbach ugyanazt a közös START/END útvonalat használja.");
                ImGui::BulletText("Az eredménytáblázat sorára kattintva az érintett objektum kijelölhető.");

                ImGui::SeparatorText("Csőkategória színek");
                ImGui::BulletText("PPR: kék.");
                ImGui::BulletText("PVC-U foul-drain: szürke.");
                ImGui::BulletText("Rézcső: rézbarna.");
                ImGui::BulletText("A kijelölt elem továbbra is sárga kijelölési színt kap.");

                ImGui::SeparatorText("BOM / darabjegyzék");
                ImGui::BulletText(
                    "A Darabjegyzék panel automatikusan összesíti a csőhálózat csöveit, idomait és szerelvényeit.");
                ImGui::BulletText(
                    "Az egyenes csövek teljes beépített hossza csőtípus és méret szerint külön összegződik.");
                ImGui::BulletText("A darabjegyzék CSV fájlba exportálható: plumbcad_bom.csv.");

                ImGui::SeparatorText("Mentés és visszavonás");
                ImGui::BulletText("Mentés: projekt mentése a plumbcad_project.pcad fájlba.");
                ImGui::BulletText("Betöltés: projekt visszatöltése.");
                ImGui::BulletText("Ctrl+Z: utolsó művelet visszavonása.");
                ImGui::BulletText("Ctrl+Y: visszavont művelet újra végrehajtása.");
            }
            else if (activeLeftPanel == 4)
            {
                ImGui::SeparatorText("Automatikus csőnyomvonal (A*)");

                ImGui::BeginChild("RouteStatusBox", ImVec2(0.0f, 74.0f), true);
                ImGui::TextDisabled("Útvonal állapot");
                ImGui::TextWrapped("%s", statusMessage.c_str());
                ImGui::EndChild();
                ImGui::Spacing();
                if (routeStartId == InvalidObjectId)
                {
                    ImGui::Text("Kezdőpont: nincs");
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.10f, 0.55f, 1.00f, 1.00f), "Kezdőpont: %u / %d. vég",
                                       static_cast<unsigned int>(routeStartId), routeStartEndpoint);
                }

                if (routeEndId == InvalidObjectId)
                {
                    ImGui::Text("Végpont: nincs");
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.85f, 0.20f, 1.00f, 1.00f), "Végpont: %u / %d. vég",
                                       static_cast<unsigned int>(routeEndId), routeEndEndpoint);
                }

                ImGui::SeparatorText("Létrehozandó csőkategória");

                const auto &routeCatalog = network.catalog();

                if (!routeCatalog.empty())
                {
                    if (routePipeTypeIndex < 0 || routePipeTypeIndex >= static_cast<int>(routeCatalog.size()))
                    {
                        const PipeElement *routeStartPipe = network.find(routeStartId);

                        routePipeTypeIndex = routeStartPipe ? std::clamp(routeStartPipe->pipeTypeIndex, 0,
                                                                         static_cast<int>(routeCatalog.size()) - 1)
                                                            : 0;
                    }

                    const PipeTypeDefinition &selectedRouteType = routeCatalog[routePipeTypeIndex];

                    ImGui::SetNextItemWidth(-1.0f);

                    ImGui::PushStyleColor(ImGuiCol_Text, pipeCategoryUiColor(routePipeTypeIndex));

                    const bool routeTypeComboOpen =
                        ImGui::BeginCombo("##route_pipe_type", selectedRouteType.name.c_str());

                    ImGui::PopStyleColor();

                    if (routeTypeComboOpen)
                    {
                        for (int typeIndex = 0; typeIndex < static_cast<int>(routeCatalog.size()); ++typeIndex)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, pipeCategoryUiColor(typeIndex));

                            const bool selected = typeIndex == routePipeTypeIndex;

                            if (ImGui::Selectable(routeCatalog[typeIndex].name.c_str(), selected))
                            {
                                routePipeTypeIndex = typeIndex;

                                // A méretváltás módosíthatja az END szűkítőt és az akadálytávolságot.
                                if (!selected && !routeAlternatives.empty()) actions.routeSearch = true;
                            }

                            if (selected) ImGui::SetItemDefaultFocus();

                            ImGui::PopStyleColor();
                        }

                        ImGui::EndCombo();
                    }

                    const PipeElement *routeStartPipe = network.find(routeStartId);

                    if (routeStartPipe && !selectedRouteType.sizes.empty())
                    {
                        const float startDiameterMm = pipeEndpointDiameterMm(*routeStartPipe, routeStartEndpoint);

                        int closestSizeIndex = 0;

                        float closestDistance = std::numeric_limits<float>::max();

                        for (int sizeIndex = 0; sizeIndex < static_cast<int>(selectedRouteType.sizes.size());
                             ++sizeIndex)
                        {
                            const float distance =
                                std::abs(selectedRouteType.sizes[sizeIndex].diameterMm - startDiameterMm);

                            if (distance < closestDistance)
                            {
                                closestDistance = distance;

                                closestSizeIndex = sizeIndex;
                            }
                        }

                        const PipeCatalogSize &routeSize = selectedRouteType.sizes[closestSizeIndex];

                        ImGui::Text("A* létrehozási méret: Ø%.1f × %.1f mm", routeSize.diameterMm,
                                    routeSize.wallThicknessMm);

                        if (std::abs(routeSize.diameterMm - startDiameterMm) > 0.10f)
                        {
                            ImGui::TextWrapped(
                                "FIGYELEM: a választott kategóriában nincs Ø%.1f mm pontos méret. "
                                "A hálózati összefüggőség ellenőrzése a közvetlen átmérő-eltérést hibának fogja jelezni.",
                                startDiameterMm);
                        }
                    }
                }

                ImGui::InputFloat("Rácsméret [m]", &routeGridSizeM, 0.05f, 0.10f, "%.2f");
                routeGridSizeM = std::clamp(routeGridSizeM, 0.15f, 2.0f);
                ImGui::InputFloat("Akadálytávolság [m]", &routeClearanceM, 0.02f, 0.05f, "%.2f");
                routeClearanceM = std::clamp(routeClearanceM, 0.0f, 1.0f);
                ImGui::SeparatorText("Létrehozott útvonal validációi");
                ImGui::Checkbox("Nyílt lefolyási Table 6 validáció (csak egyenes csövek)##route_drainage_enabled",
                                &routeValidationProfile.drainageCheckEnabled);
                ImGui::SeparatorText("Darcy-Weisbach A* csőérdesség");
                ImGui::InputFloat("A* egyenes csövek érdessége [mm]##route_roughness",
                                  &routeValidationProfile.hydraulicRoughnessMm, 0.001f, 0.005f, "%.4f");
                routeValidationProfile.hydraulicRoughnessMm =
                    std::max(0.0f, routeValidationProfile.hydraulicRoughnessMm);
                if (lastRouteCreatedElementCount > 0)
                {
                    ImGui::Text("Legutóbbi A* útvonal: %d elem", lastRouteCreatedElementCount);

                    if (ImGui::Button("Validációs sablon újraalkalmazása", ImVec2(-1.0f, 0.0f)))
                    {
                        actions.routeApplyValidationProfileToLastBuilt = true;
                    }
                }

                if (ImGui::Button("Útvonal-lehetőségek keresése"))
                {
                    actions.routeSearch = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Útvonalpontok törlése"))
                {
                    actions.routeClear = true;
                }

                ImGui::SeparatorText("Talált útvonalak");
                if (routeAlternatives.empty())
                {
                    ImGui::Text("Nincs keresési eredmény.");
                }
                else
                {
                    if (selectedRouteAlternative < 0 ||
                        selectedRouteAlternative >= static_cast<int>(routeAlternatives.size()))
                    {
                        selectedRouteAlternative = 0;
                    }

                    ImGui::BeginChild("RouteAlternativeList", ImVec2(0.0f, 170.0f), true);
                    for (int i = 0; i < static_cast<int>(routeAlternatives.size()); ++i)
                    {
                        const RouteAlternative &alternative = routeAlternatives[i];
                        char label[280];
                        const char *endpointStyle =
                            alternative.startsWithBend && alternative.endsWithBend
                                ? " | kanyar: start+vég"
                                : (alternative.startsWithBend
                                       ? " | kanyar: start"
                                       : (alternative.endsWithBend ? " | kanyar: vég" : ""));

                        std::snprintf(label, sizeof(label), "%d. lehetőség | %.2f m | 45°:%d | 90°:%d%s", i + 1,
                                      alternative.totalLengthM, alternative.bend45Count, alternative.bend90Count,
                                      endpointStyle);
                        const bool selected = selectedRouteAlternative == i;
                        if (ImGui::Selectable(label, selected))
                        {
                            selectedRouteAlternative = i;
                            actions.routePreviewChanged = true;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndChild();

                    const RouteAlternative &selectedRoute = routeAlternatives[selectedRouteAlternative];
                    ImGui::SeparatorText("Útvonal előnézet");
                    ImGui::Text("Teljes hossz: %.2f m", selectedRoute.totalLengthM);
                    ImGui::Text("Egyenes szakaszok: %d", selectedRoute.segmentCount);
                    ImGui::Text("45° könyökök: %d", selectedRoute.bend45Count);
                    ImGui::Text("90° könyökök: %d", selectedRoute.bend90Count);
                    ImGui::Text("Kezdés: %s", selectedRoute.startsWithBend ? "közvetlen könyök" : "egyenes cső");
                    ImGui::Text("Érkezés: %s",
                                selectedRoute.catalogPlan.reducerType >= 0
                                    ? "szűkítő az END pontnál"
                                    : (selectedRoute.endsWithBend ? "közvetlen könyök" : "egyenes cső"));
                    ImGui::Spacing();
                    if (ImGui::Button("Ezt az útvonalat végleg létrehozom", ImVec2(-1.0f, 0.0f)))
                    {
                        actions.routeBuildSelected = true;
                    }
                }
            }
            else if (activeLeftPanel == 5)
            {
                ImGui::SeparatorText("Akadályok");
                ImGui::TextWrapped(
                    "Fal, födém, akna és tiltott térfogat hozható létre. Az aktív objektumokat az A* fizikai akadályként kezeli.");

                if (ImGui::BeginTable("ArchitectureCreateTable", 2, ImGuiTableFlags_SizingStretchSame))
                {
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Fal hozzáadása", ImVec2(-1.0f, 0.0f))) actions.addWall = true;
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Födém hozzáadása", ImVec2(-1.0f, 0.0f))) actions.addSlab = true;
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Akna hozzáadása", ImVec2(-1.0f, 0.0f))) actions.addShaft = true;
                    ImGui::TableNextColumn();
                    if (ImGui::Button("Tiltott zóna hozzáadása", ImVec2(-1.0f, 0.0f)))
                        actions.addForbiddenZone = true;
                    ImGui::EndTable();
                }

                ImGui::SeparatorText("Objektumok");
                ImGui::BeginChild("ArchitectureObjectList", ImVec2(0.0f, 150.0f), true);
                for (const ArchitecturalObstacle &obstacle : architecturalObstacles)
                {
                    const bool selected = obstacle.id == selectedId;
                    const std::string label = std::string(architecturalObstacleTypeName(obstacle.type)) +
                                              " | ID: " + std::to_string(obstacle.id);
                    if (ImGui::Selectable(label.c_str(), selected)) selectedId = obstacle.id;
                }
                if (architecturalObstacles.empty()) ImGui::TextDisabled("Még nincs építészeti akadály.");
                ImGui::EndChild();

                const ArchitecturalObstacle *selectedArchitecture = nullptr;
                for (const ArchitecturalObstacle &obstacle : architecturalObstacles)
                    if (obstacle.id == selectedId)
                    {
                        selectedArchitecture = &obstacle;
                        break;
                    }

                static ObjectId architectureSizeEditId = InvalidObjectId;
                static glm::vec3 architectureSizeEditM{1.0f};
                if (selectedArchitecture)
                {
                    if (architectureSizeEditId != selectedArchitecture->id)
                    {
                        architectureSizeEditId = selectedArchitecture->id;
                        architectureSizeEditM = selectedArchitecture->sizeM;
                    }
                    ImGui::SeparatorText("Kijelölt építészeti objektum");
                    ImGui::Text("Típus: %s", architecturalObstacleTypeName(selectedArchitecture->type));
                    if (const SceneObject *selectedScene = findSelected(objects, selectedArchitecture->id))
                    {
                        ImGui::Text("Pozíció: %.2f, %.2f, %.2f m", selectedScene->transform.position.x,
                                    selectedScene->transform.position.y, selectedScene->transform.position.z);
                        ImGui::Text("Forgatás: %.1f°, %.1f°, %.1f°",
                                    radToDeg(selectedScene->transform.rotationEuler.x),
                                    radToDeg(selectedScene->transform.rotationEuler.y),
                                    radToDeg(selectedScene->transform.rotationEuler.z));
                    }
                    ImGui::InputFloat3("Méret X/Y/Z [m]", &architectureSizeEditM.x, "%.2f");
                    architectureSizeEditM.x = std::clamp(architectureSizeEditM.x, 0.05f, 100.0f);
                    architectureSizeEditM.y = std::clamp(architectureSizeEditM.y, 0.05f, 100.0f);
                    architectureSizeEditM.z = std::clamp(architectureSizeEditM.z, 0.05f, 100.0f);
                    if (ImGui::Button("Méret alkalmazása", ImVec2(-1.0f, 0.0f)))
                    {
                        actions.setArchitecturalObstacleSize = true;
                        actions.architecturalObstacleObjectId = selectedArchitecture->id;
                        actions.architecturalObstacleSizeM = architectureSizeEditM;
                    }
                    bool blocksRouting = selectedArchitecture->blocksRouting;
                    if (ImGui::Checkbox("A* akadályként aktív", &blocksRouting))
                    {
                        actions.setArchitecturalObstacleBlocking = true;
                        actions.architecturalObstacleObjectId = selectedArchitecture->id;
                        actions.architecturalObstacleBlocksRouting = blocksRouting;
                    }
                    if (ImGui::Button("Kijelölt építészeti objektum törlése", ImVec2(-1.0f, 0.0f)))
                    {
                        actions.deleteElement = true;
                        actions.deleteElementObjectId = selectedArchitecture->id;
                    }
                    ImGui::TextWrapped(
                        "Mozgatás: 1-es eszköz. Forgatás: 2-es eszköz. A pontos X/Y/Z méret itt adható meg.");
                }
                else
                {
                    ImGui::TextDisabled("Válassz ki egy falat, födémet, aknát vagy tiltott zónát.");
                }
                ImGui::SeparatorText("A* viselkedés");
                ImGui::BulletText("A csőtest nem haladhat át aktív építészeti akadályon.");
                ImGui::BulletText("Az Útvonal panel akadálytávolsága ezekre az objektumokra is ráadódik.");
                ImGui::BulletText("Elforgatott akadálynál konzervatív világtérbeli befoglaló dobozt használunk.");
            }
            else if (activeLeftPanel == 6)
            {
                const BomSummary bom = buildBillOfMaterials(network);

                ImGui::SeparatorText("BOM / darabjegyzék");

                ImGui::TextWrapped("A lista közvetlenül a jelenlegi PipeNetworkből készül. "
                                   "Az építészeti akadályok nem részei a csőhálózati darabjegyzéknek.");

                if (ImGui::BeginTable("BomSummaryTable", 2,
                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("Hálózati elemek");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d db", bom.totalNetworkElementCount);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("Egyenes csődarabok");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d db", bom.straightPipePieceCount);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("Idomok / szerelvények");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d db", bom.fittingPieceCount);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("Egyenes cső teljes hossza");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.3f m", bom.totalStraightPipeLengthM);

                    ImGui::EndTable();
                }

                ImGui::Spacing();

                if (ImGui::Button("Darabjegyzék exportálása CSV-be", ImVec2(-1.0f, 0.0f)))
                {
                    actions.exportBomCsv = true;
                }

                ImGui::TextDisabled("Kimeneti fájl: plumbcad_bom.csv");

                ImGui::SeparatorText("Összesített tételek");

                if (bom.items.empty())
                {
                    ImGui::TextDisabled("A csőhálózat üres.");
                }
                else if (ImGui::BeginTable("BomItemsTable", 5,
                                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                               ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
                                           ImVec2(0.0f, 390.0f)))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);

                    ImGui::TableSetupColumn("Elem", ImGuiTableColumnFlags_WidthFixed, 125.0f);

                    ImGui::TableSetupColumn("Csőtípus / anyag", ImGuiTableColumnFlags_WidthFixed, 150.0f);

                    ImGui::TableSetupColumn("Méret", ImGuiTableColumnFlags_WidthFixed, 180.0f);

                    ImGui::TableSetupColumn("Db", ImGuiTableColumnFlags_WidthFixed, 50.0f);

                    ImGui::TableSetupColumn("Hossz [m]", ImGuiTableColumnFlags_WidthFixed, 80.0f);

                    ImGui::TableHeadersRow();

                    for (const BomItem &item : bom.items)
                    {
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextWrapped("%s", item.name.c_str());

                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextWrapped("%s", item.pipeSystem.c_str());

                        if (!item.material.empty())
                        {
                            ImGui::TextDisabled("%s", item.material.c_str());
                        }

                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextWrapped("%s", item.sizeText.c_str());

                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%d", item.quantity);

                        ImGui::TableSetColumnIndex(4);

                        if (item.elementType == PipeElementType::StraightPipe)
                        {
                            ImGui::Text("%.3f", item.totalLengthM);
                        }
                        else
                        {
                            ImGui::TextDisabled("-");
                        }
                    }

                    ImGui::EndTable();
                }

                ImGui::SeparatorText("Csoportosítás");

                ImGui::BulletText("Az egyenes csövek csőtípus és katalógusméret szerint összegezve jelennek meg.");

                ImGui::BulletText("Az egyeneseknél a darabszám mellett a teljes beépített hossz is látszik.");

                ImGui::BulletText("A szűkítőket a két csatlakozási átmérő szerint külön tételbe csoportosítja.");

                ImGui::BulletText(
                    "Az automatikus végpont-illesztő könyökök szög és hajlítási sugár szerint külön tételt kapnak.");
            }
            ImGui::End();
        }

        if (activeLeftPanel == 0 && !selectedObject)
        {
            ImGui::SetNextWindowPos(ImVec2(0.0f, toolbarHeight), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, mainIo.DisplaySize.y - toolbarHeight),
                                     ImGuiCond_Always);
            ImGui::Begin("Bal oldali objektum panel", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
            ImGui::Text("Objektum panel");
            ImGui::Separator();
            ImGui::Text("Nincs kijelölt objektum.");
            ImGui::End();
        }

        if (activeLeftPanel == 0 && selectedObject)
        {
            ImGui::SetNextWindowPos(ImVec2(0.0f, toolbarHeight), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, mainIo.DisplaySize.y - toolbarHeight),
                                     ImGuiCond_Always);
            ImGui::Begin("Bal oldali objektum panel", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
            ImGui::Text("Kijelölt: %s", selectedObject->name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("ID: %u", selectedObject->id);

            ImGui::SeparatorText("Objektum adatai");
            if (ImGui::BeginTable("LeftSelectedObjectTable", 2,
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Pozíció");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.2f, %.2f, %.2f", selectedObject->transform.position.x,
                            selectedObject->transform.position.y, selectedObject->transform.position.z);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Forgatás");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.1f°, %.1f°, %.1f°", radToDeg(selectedObject->transform.rotationEuler.x),
                            radToDeg(selectedObject->transform.rotationEuler.y),
                            radToDeg(selectedObject->transform.rotationEuler.z));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Transzformációs szorzó");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.2f, %.2f, %.2f", selectedObject->transform.scale.x,
                            selectedObject->transform.scale.y, selectedObject->transform.scale.z);

                ImGui::EndTable();
            }

            if (network.find(selectedId))
            {
                ImGui::SeparatorText("Elem törlése");

                if (ImGui::Button("Kijelölt elem törlése", ImVec2(-1.0f, 0.0f)))
                {
                    actions.deleteElement = true;
                    actions.deleteElementObjectId = selectedId;
                }
            }

            const ArchitecturalObstacle *selectedArchitectureForObjectPanel = nullptr;
            for (const ArchitecturalObstacle &obstacle : architecturalObstacles)
                if (obstacle.id == selectedId)
                {
                    selectedArchitectureForObjectPanel = &obstacle;
                    break;
                }
            if (selectedArchitectureForObjectPanel)
            {
                ImGui::SeparatorText("Építészeti objektum");
                ImGui::Text("Típus: %s", architecturalObstacleTypeName(selectedArchitectureForObjectPanel->type));
                ImGui::Text("Méret: %.2f x %.2f x %.2f m", selectedArchitectureForObjectPanel->sizeM.x,
                            selectedArchitectureForObjectPanel->sizeM.y,
                            selectedArchitectureForObjectPanel->sizeM.z);
                ImGui::Text("A* akadály: %s",
                            selectedArchitectureForObjectPanel->blocksRouting ? "aktív" : "kikapcsolva");
            }

            if (PipeElement *pipe = network.find(selectedId))
            {
                ImGui::SeparatorText("Kiválasztott cső adatai");
                const auto &catalog = network.catalog();
                const char *typeName = "Ismeretlen csőtípus";
                if (pipe->pipeTypeIndex >= 0 && pipe->pipeTypeIndex < static_cast<int>(catalog.size()))
                {
                    typeName = catalog[pipe->pipeTypeIndex].name.c_str();
                }
                const char *elementTypeName = "Egyenes csőszakasz";
                if (pipe->type == PipeElementType::Elbow90)
                    elementTypeName = "90 fokos könyök";
                else if (pipe->type == PipeElementType::Elbow45)
                    elementTypeName = "45 fokos könyök";
                else if (pipe->type == PipeElementType::AlignmentElbow)
                    elementTypeName = "Automatikus végpont-illesztő könyök";
                else if (pipe->type == PipeElementType::Tee)
                    elementTypeName = "T-idom";
                else if (pipe->type == PipeElementType::Reducer)
                    elementTypeName = "Szűkítő";
                else if (pipe->type == PipeElementType::Valve)
                    elementTypeName = "Szelep";
                else if (pipe->type == PipeElementType::EndCap)
                    elementTypeName = "Végzáró";
                ImGui::Text("Elem típusa: %s", elementTypeName);
                ImGui::Text("Csőtípus: %s", typeName);
                ImGui::Text("Katalógus méret index: %d", pipe->pipeSizeIndex);
                ImGui::Text("Átmérő: %.1f mm", pipe->diameterMm);
                ImGui::Text("Falvastagság: %.1f mm", pipe->wallThicknessMm);
                if (pipe->type == PipeElementType::Elbow90 || pipe->type == PipeElementType::Elbow45 ||
                    pipe->type == PipeElementType::AlignmentElbow)
                {
                    if (pipe->type == PipeElementType::AlignmentElbow)
                    {
                        const float angleDeg =
                            (pipe->lengthM / std::max(pipe->bendRadiusM, 0.1f)) * 57.2957795131f;

                        ImGui::Text("Könyök szöge: %.2f° (automatikus végpont-illesztés)", angleDeg);
                    }
                    else
                    {
                        ImGui::Text("Könyök szöge: %s", pipe->type == PipeElementType::Elbow45 ? "45°" : "90°");
                    }

                    ImGui::Text("Hajlítási sugár: %.2f m", pipe->bendRadiusM);
                    ImGui::Text("Ívhossz: %.3f m", pipe->lengthM);
                }
                else if (pipe->type == PipeElementType::Tee)
                {
                    ImGui::Text("Fő ág hossza: %.2f m", pipe->lengthM);
                    ImGui::Text("Leágazás hossza: %.2f m", pipe->bendRadiusM);
                }
                else if (pipe->type == PipeElementType::Reducer)
                {
                    ImGui::Text("Átmenet hossza: %.2f m", pipe->lengthM);
                    ImGui::Text("Elsődleges oldal átmérője: %.1f mm", pipe->diameterMm);
                    ImGui::Text("Másodlagos oldal átmérője: %.1f mm", pipe->secondaryDiameterMm);
                    ImGui::Text("Másodlagos oldal falvastagsága: %.1f mm", pipe->secondaryWallThicknessMm);

                    if (pipe->pipeTypeIndex >= 0 && pipe->pipeTypeIndex < static_cast<int>(catalog.size()))
                    {
                        const auto &sizes = catalog[pipe->pipeTypeIndex].sizes;

                        if (!sizes.empty())
                        {
                            const int currentSecondaryIndex =
                                std::clamp(pipe->secondaryPipeSizeIndex, 0, static_cast<int>(sizes.size()) - 1);

                            const std::string reducerPreview =
                                std::to_string(static_cast<int>(sizes[currentSecondaryIndex].diameterMm)) + " mm";

                            if (ImGui::BeginCombo("Másodlagos oldal katalógusmérete", reducerPreview.c_str()))
                            {
                                for (int sizeIndex = 0; sizeIndex < static_cast<int>(sizes.size()); ++sizeIndex)
                                {
                                    if (sizeIndex == pipe->pipeSizeIndex)
                                    {
                                        continue;
                                    }

                                    const std::string item =
                                        std::to_string(static_cast<int>(sizes[sizeIndex].diameterMm)) +
                                        " mm##reducer_secondary_" + std::to_string(sizeIndex);

                                    const bool selectedSecondary = sizeIndex == currentSecondaryIndex;

                                    if (ImGui::Selectable(item.c_str(), selectedSecondary))
                                    {
                                        actions.setReducerSecondarySize = true;
                                        actions.reducerObjectId = selectedId;
                                        actions.reducerSecondarySizeIndex = sizeIndex;
                                    }

                                    if (selectedSecondary) ImGui::SetItemDefaultFocus();
                                }

                                ImGui::EndCombo();
                            }
                        }
                    }
                }
                else if (pipe->type == PipeElementType::Valve)
                {
                    ImGui::Text("Szerelvényhossz: %.2f m", pipe->lengthM);
                }
                else if (pipe->type == PipeElementType::EndCap)
                {
                    ImGui::Text("Végzáró hossza: %.2f m", pipe->lengthM);
                }
                else
                {
                    ImGui::Text("Hossz: %.2f m", pipe->lengthM);
                }
                ImGui::Text("Anyag: %s", pipe->material.c_str());
                ImGui::Text("Tervezési nyomás: %.1f bar", pipe->designPressureBar);

                if (pipe->type == PipeElementType::StraightPipe)
                {
                    ImGui::SeparatorText("Egyenes cső hossza");

                    // Ha a hossz más művelet miatt változott (pl. új kijelölés / undo),
                    // és az editor még a régi értéket mutatná, a gomb alkalmazása után
                    // a tényleges modellérték kerül vissza.
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputFloat("Hossz [m]##straight_length", &straightLengthEditM, 0.05f, 0.25f, "%.3f");
                    straightLengthEditM =
                        std::clamp(straightLengthEditM, MinStraightPipeLengthM, MaxStraightPipeLengthM);

                    if (ImGui::Button("Hossz alkalmazása", ImVec2(-1.0f, 0.0f)))
                    {
                        actions.setStraightPipeLength = true;
                        actions.straightPipeLengthObjectId = selectedId;
                        actions.straightPipeLengthM = straightLengthEditM;
                    }
                }
            }

            if (PipeElement *pipe = network.find(selectedId))
            {
                if (pipe->type == PipeElementType::StraightPipe)
                {
                    ImGui::SeparatorText("Nyílt lefolyási referencia");
                    ImGui::Checkbox("Approved Document H Table 6 vizsgálata", &pipe->drainageCheckEnabled);
                }
                else
                {
                    pipe->drainageCheckEnabled = false;
                }

                ImGui::SeparatorText("Darcy-Weisbach");
                if (pipe->type == PipeElementType::StraightPipe)
                {
                    ImGui::InputFloat("Abszolút csőérdesség [mm]", &pipe->hydraulicRoughnessMm, 0.001f, 0.005f,
                                      "%.4f");
                    pipe->hydraulicRoughnessMm = std::max(0.0f, pipe->hydraulicRoughnessMm);
                }
                else if (pipe->type == PipeElementType::Elbow45)
                    ImGui::TextDisabled("45° könyök: prototípus K = 0.40.");
                else if (pipe->type == PipeElementType::Elbow90)
                    ImGui::TextDisabled("90° könyök: prototípus K = 0.90.");
                else if (pipe->type == PipeElementType::AlignmentElbow)
                    ImGui::TextDisabled(
                        "Automatikus végpont-illesztő könyök: a K érték a tényleges szögből arányosan becsült prototípusérték.");
                else if (pipe->type == PipeElementType::Tee)
                    ImGui::TextDisabled("T-idom: főág K = 0.60, leágazás K = 1.80.");
                else if (pipe->type == PipeElementType::Reducer)
                    ImGui::TextDisabled("Szűkítő: prototípus K = 0.30.");
                else if (pipe->type == PipeElementType::Valve)
                    ImGui::TextDisabled("Szelep: prototípus K = 2.00.");
                else if (pipe->type == PipeElementType::EndCap)
                    ImGui::TextDisabled("Végzáró: terminális elem; nincs rajta átmenő Darcy-út.");
                else
                    ImGui::TextDisabled("Ehhez az elemhez nincs külön Darcy veszteségi paraméter.");
            }
            else
            {
                ImGui::TextDisabled("Nincs kijelölt cső/idom.");
            }

            ImGui::SeparatorText("Gyors műveletek");
            if (ImGui::Button("Alaphelyzetbe állít"))
            {
                selectedObject->transform.position = {0.0f, 0.5f, 0.0f};
                selectedObject->transform.rotationEuler = {0.0f, 0.0f, 0.0f};
                selectedObject->transform.scale = {1.0f, 1.0f, 1.0f};
            }
            ImGui::SameLine();
            if (ImGui::Button("Kijelölés törlése"))
            {
                selectedId = InvalidObjectId;
            }
            ImGui::End();
        }

        return actions;
    }
}
