#include "App/ApplicationInternal.hpp"
#include "Bom/BillOfMaterials.hpp"
#include "Validation/ValidationReport.hpp"
#include <glad/gl.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

namespace plumbcad
{
    namespace
    {
        bool validationFailed(const std::vector<ValidationMessage> &messages)
        {
            for (const ValidationMessage &message : messages)
            {
                if (message.severity != ValidationSeverity::Info) return true;
            }

            return false;
        }
    }

    Application::~Application()
    {
        shutdownImGui();
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }

    void Application::initWindow()
    {
        if (!glfwInit()) throw std::runtime_error("GLFW init failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        m_window = glfwCreateWindow(m_width, m_height, "PlumbCAD - OpenGL + ImGui", nullptr, nullptr);
        if (!m_window) throw std::runtime_error("Window creation failed");
        glfwMakeContextCurrent(m_window);
        glfwSwapInterval(1);
        glfwSetWindowUserPointer(m_window, this);
        glfwSetScrollCallback(m_window, &Application::scrollCallback);
        if (!gladLoadGL(glfwGetProcAddress)) throw std::runtime_error("GLAD load failed");
    }

    void Application::initImGui()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        // Magyar ékezetes betűk javítása.
        // Az ImGui alapértelmezett beépített fontja több környezetben nem tartalmazza
        // az ő/ű/á/é/í/ó/ö/ü karaktereket, ezért azok ? jelként jelenhetnek meg.
        // Windows alatt betöltünk egy rendszerfontot latin kiegészítő karaktertartománnyal.
        ImGuiIO &io = ImGui::GetIO();
        static const ImWchar hungarianGlyphRanges[] = {0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
                                                       0x0100,
                                                       0x017F, // Latin Extended-A, ide tartozik az ő és ű is
                                                       0};

        const char *fontCandidates[] = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                                        "C:/Windows/Fonts/calibri.ttf"};

        bool loadedHungarianFont = false;
        for (const char *fontPath : fontCandidates)
        {
            std::ifstream fontFile(fontPath);
            if (fontFile.good())
            {
                io.Fonts->AddFontFromFileTTF(fontPath, 16.0f, nullptr, hungarianGlyphRanges);
                loadedHungarianFont = true;
                break;
            }
        }

        if (!loadedHungarianFont)
        {
            io.Fonts->AddFontDefault();
        }

        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOpenGL(m_window, true);
        ImGui_ImplOpenGL3_Init("#version 330");
    }

    void Application::shutdownImGui()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void Application::seedScene()
    {
        SceneObject grid;
        grid.id = 999999;
        grid.name = "Szerkesztő rács";
        grid.mesh = MeshFactory::makeGrid();
        grid.color = {0.25f, 0.25f, 0.25f};
        grid.selectable = false;
        m_objects.push_back(grid);
        m_renderer.uploadObject(grid, true);

        // A világ origó és a központi X/Y/Z tengelyek mindig ugyanahhoz
        // a globális koordináta-rendszerhez tartoznak.
        initializeWorldAxisReference();

        addPipeObject(2.0f, 80.0f);
        addPipeObject(1.5f, 50.0f);
        if (m_objects.size() > 2)
        {
            m_objects[2].transform.position = {1.2f, 0.5f, 0.0f};
            m_objects[2].transform.rotationEuler.y = 1.5708f;
        }
        updateEndpointMarkers();
    }

    int Application::run(const std::string& startupProject)
    {
        if (!startupProject.empty())
        {
            if (!loadProject(startupProject))
                throw std::runtime_error("Nem sikerült betölteni a projektet: " + startupProject);
            m_projectPath = startupProject;
            setStatusMessage("Projekt betöltve: " + m_projectPath);
        }

        PCAD_INFO("Application started");
        m_lastFrameTime = glfwGetTime();
        while (!glfwWindowShouldClose(m_window))
        {
            const double currentTime = glfwGetTime();
            const float deltaTime = static_cast<float>(currentTime - m_lastFrameTime);
            m_lastFrameTime = currentTime;

            glfwPollEvents();
            handleInput(deltaTime);
            glfwGetFramebufferSize(m_window, &m_width, &m_height);

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // A kijelzett forgatási szög NEM egy ideiglenes műveleti számláló.
            // Minden frame-ben a tényleges objektum-geometriából számoljuk újra.
            // Emiatt kijelölésváltás után is ugyanaz az abszolút szög jelenik meg,
            // és a kézzel beírt érték valódi célérték marad.

            m_rotationCurrentDegrees = currentCadRotationDegrees();

            UiActions actions = m_ui.draw(
                m_network, m_objects, m_architecturalObstacles, m_selectedId, m_transformMode,
                m_moveConnectedGroup, m_moveAxisConstraint, m_rotateConnectedGroup, m_rotateAxisConstraint,
                m_rotationSnapDegrees, m_rotationCurrentDegrees, m_fittingRotationFixedEndpoint,
                m_camera.orthographic, m_wireframeMode, m_routeStartId, m_routeStartEndpoint, m_routeEndId,
                m_routeEndEndpoint, m_routeGridSizeM, m_routeClearanceM, m_routePipeTypeIndex,
                m_routeValidationProfile, static_cast<int>(m_lastRouteCreatedIds.size()),
                m_pressureDesignPressureBar, m_pressureTestPressureBar, m_pressureHoldTimeMin,
                m_pressureInitialMeasuredPressureBar, m_pressureFinalMeasuredPressureBar, m_pressureAllowedDropBar,
                m_validationStartId, m_validationStartEndpoint, m_validationEndId, m_validationEndEndpoint,
                m_flowRateLps, m_hydraulicFlowRateLps, m_hydraulicDensityKgM3, m_hydraulicKinematicViscosityM2s,
                m_hydraulicMaxTotalPressureLossBar, m_routeAlternatives, m_selectedRouteAlternative,
                m_validationMessages, m_validationHasRun, m_statusMessage);
            if (actions.toggleProjectionMode)
            {
                m_camera.orthographic = !m_camera.orthographic;

                if (m_camera.orthographic)
                {
                    // Első ORTO bekapcsolásnál használható CAD-lépték.
                    m_camera.orthoHeight = std::clamp(m_camera.orthoHeight, 2.0f, 40.0f);

                    setStatusMessage("3D ortografikus vetület aktív.");
                }
                else
                {
                    setStatusMessage("3D perspektivikus vetület aktív.");
                }
            }

            if (actions.toggleWireframeMode)
            {
                m_wireframeMode = !m_wireframeMode;

                setStatusMessage(m_wireframeMode ? "Wireframe nézet aktív." : "Árnyalt / kitöltött nézet aktív.");
            }

            if (actions.applyExactRotation)
            {
                // ABSZOLÚT célérték:
                // ha az idom ténylegesen 30°-on áll és 90°-ot írsz be,
                // +60° korrekció történik. Ha később újra kijelölöd és 45°-ot
                // írsz be, -45° korrekció történik — NEM +45° hozzáadás.
                const float targetDegrees = actions.exactRotationDegrees;

                const float actualDegrees = currentCadRotationDegrees();

                float deltaDegrees = std::remainder(targetDegrees - actualDegrees, 360.0f);

                if (deltaDegrees <= -180.0f)
                {
                    deltaDegrees += 360.0f;
                }
                else if (deltaDegrees > 180.0f)
                {
                    deltaDegrees -= 360.0f;
                }

                if (std::abs(deltaDegrees) > 0.0001f)
                {
                    pushUndoSnapshot();

                    if (applyCadRotationDegrees(deltaDegrees))
                    {
                        m_rotationCurrentDegrees = currentCadRotationDegrees();

                        setStatusMessage("Abszolút forgatási cél alkalmazva: " + std::to_string(targetDegrees) +
                                         "°.");
                    }
                    else if (!m_undoStack.empty())
                    {
                        m_undoStack.pop_back();
                    }
                }
                else
                {
                    m_rotationCurrentDegrees = actualDegrees;

                    setStatusMessage("Az idom már a megadott abszolút szögön áll.");
                }
            }

            if (actions.routeSetStart) setRouteStart(actions.routeObjectId, actions.routeEndpoint);
            if (actions.routeSetEnd) setRouteEnd(actions.routeObjectId, actions.routeEndpoint);
            if (actions.intentionalOpenEndChanged)
            {
                pushUndoSnapshot();

                if (m_network.setIntentionalOpenEnd(actions.intentionalOpenEndObjectId,
                                                    actions.intentionalOpenEndEndpoint,
                                                    actions.intentionalOpenEndValue))
                {
                    m_validationHasRun = false;
                    m_validationMessages.clear();

                    setStatusMessage(actions.intentionalOpenEndValue
                                         ? "Csővég szándékos nyitott végpontként megjelölve."
                                         : "Csővég szándékos végpont jelölése törölve.");
                }
                else if (!m_undoStack.empty())
                {
                    m_undoStack.pop_back();
                    setStatusMessage("A csővég szándékos végpont jelölése nem módosítható.");
                }
            }

            if (actions.validationSetStart)
                setValidationStart(actions.validationObjectId, actions.validationEndpoint);
            if (actions.validationSetEnd) setValidationEnd(actions.validationObjectId, actions.validationEndpoint);
            if (actions.validationClear) clearValidationPath();

            if (actions.routeClear) clearRouteSelection();
            if (actions.routeSearch)
            {
                searchAutomaticRoutes();
            }
            if (actions.routePreviewChanged)
            {
                rebuildRoutePreview();
            }
            if (actions.routeApplyValidationProfileToLastBuilt)
            {
                pushUndoSnapshot();
                applyRouteValidationProfileToLastBuilt();
            }
            if (actions.routeBuildSelected)
            {
                // A keresési eredményeket külön megőrizzük. A build rollback
                // visszaállítja a hálózatot, de a felhasználó ne veszítse el
                // az előnézetet és a kiválasztott alternatívát egy build-hiba miatt.
                const std::vector<RouteAlternative> routeAlternativesBeforeBuild = m_routeAlternatives;
                const int selectedRouteAlternativeBeforeBuild = m_selectedRouteAlternative;

                pushUndoSnapshot();

                if (!buildSelectedAutomaticRoute())
                {
                    const std::string buildFailureMessage =
                        m_statusMessage.empty() ? "Ismeretlen útvonal-létrehozási hiba." : m_statusMessage;

                    if (!m_undoStack.empty())
                    {
                        // Az útvonalépítés több objektumot hoz létre egymás után.
                        // Hiba esetén egyetlen félkész elem se maradjon bent.
                        const UndoSnapshot beforeRouteBuild = m_undoStack.back();

                        m_undoStack.pop_back();
                        restoreUndoSnapshot(beforeRouteBuild);
                    }

                    m_routeAlternatives = routeAlternativesBeforeBuild;

                    m_selectedRouteAlternative =
                        std::clamp(selectedRouteAlternativeBeforeBuild, 0,
                                   std::max(0, static_cast<int>(m_routeAlternatives.size()) - 1));

                    if (!m_routeAlternatives.empty())
                    {
                        rebuildRoutePreview();
                    }

                    setStatusMessage("ÚTVONAL LÉTREHOZÁSA SIKERTELEN: " + buildFailureMessage);
                }
            }
            if (actions.setStraightPipeLength)
            {
                pushUndoSnapshot();
                if (!resizeStraightPipe(actions.straightPipeLengthObjectId, actions.straightPipeLengthM) &&
                    !m_undoStack.empty())
                {
                    m_undoStack.pop_back();
                }
            }
            if (actions.deleteElement)
            {
                pushUndoSnapshot();

                if (!deleteElement(actions.deleteElementObjectId) && !m_undoStack.empty())
                {
                    m_undoStack.pop_back();
                }
            }

            if (actions.setCatalogPipeType)
            {
                PipeElement *pipeBeforeTypeChange = m_network.find(actions.catalogPipeTypeObjectId);

                const int oldTypeIndex = pipeBeforeTypeChange ? pipeBeforeTypeChange->pipeTypeIndex : -1;

                if (oldTypeIndex != actions.catalogPipeTypeIndex)
                {
                    pushUndoSnapshot();

                    if (m_network.setPipeType(actions.catalogPipeTypeObjectId, actions.catalogPipeTypeIndex))
                    {
                        rebuildPipeMesh(actions.catalogPipeTypeObjectId);

                        m_validationHasRun = false;
                        m_validationMessages.clear();
                        m_routeAlternatives.clear();
                        m_selectedRouteAlternative = 0;

                        clearRoutePreview();
                        updateEndpointMarkers();

                        const PipeElement *changedPipe = m_network.find(actions.catalogPipeTypeObjectId);

                        if (changedPipe)
                        {
                            const auto &catalog = m_network.catalog();

                            std::string typeName = changedPipe->material;

                            if (changedPipe->pipeTypeIndex >= 0 &&
                                changedPipe->pipeTypeIndex < static_cast<int>(catalog.size()))
                            {
                                typeName = catalog[changedPipe->pipeTypeIndex].name;
                            }

                            setStatusMessage("Csőtípus módosítva: " + typeName + " | Ø" +
                                             std::to_string(changedPipe->diameterMm) + " mm.");
                        }
                    }
                    else if (!m_undoStack.empty())
                    {
                        m_undoStack.pop_back();
                        setStatusMessage("A csőtípus nem módosítható.");
                    }
                }
            }

            if (actions.setCatalogPipeSize)
            {
                PipeElement *pipeBeforeSizeChange = m_network.find(actions.catalogPipeSizeObjectId);

                const int oldSizeIndex = pipeBeforeSizeChange ? pipeBeforeSizeChange->pipeSizeIndex : -1;

                if (oldSizeIndex != actions.catalogPipeSizeIndex)
                {
                    pushUndoSnapshot();

                    if (m_network.setPipeSize(actions.catalogPipeSizeObjectId, actions.catalogPipeSizeIndex))
                    {
                        rebuildPipeMesh(actions.catalogPipeSizeObjectId);

                        m_validationHasRun = false;

                        m_validationMessages.clear();
                        m_routeAlternatives.clear();
                        m_selectedRouteAlternative = 0;
                        clearRoutePreview();
                        updateEndpointMarkers();

                        const PipeElement *changedPipe = m_network.find(actions.catalogPipeSizeObjectId);

                        if (changedPipe)
                        {
                            setStatusMessage("Méret módosítva: Ø" + std::to_string(changedPipe->diameterMm) +
                                             " mm, falvastagság " + std::to_string(changedPipe->wallThicknessMm) +
                                             " mm.");
                        }
                        else
                        {
                            setStatusMessage("Katalógusméret módosítva.");
                        }
                    }
                    else if (!m_undoStack.empty())
                    {
                        m_undoStack.pop_back();

                        setStatusMessage("A katalógusméret nem módosítható.");
                    }
                }
            }

            if (actions.setReducerSecondarySize)
            {
                pushUndoSnapshot();

                if (m_network.setReducerSecondarySize(actions.reducerObjectId, actions.reducerSecondarySizeIndex))
                {
                    rebuildPipeMesh(actions.reducerObjectId);

                    m_validationHasRun = false;
                    m_validationMessages.clear();
                    m_routeAlternatives.clear();
                    clearRoutePreview();
                    updateEndpointMarkers();

                    setStatusMessage("Szűkítő másodlagos katalógusmérete módosítva.");
                }
                else if (!m_undoStack.empty())
                {
                    m_undoStack.pop_back();

                    setStatusMessage("A szűkítő másodlagos mérete nem módosítható.");
                }
            }

            if (actions.setArchitecturalObstacleSize)
            {
                pushUndoSnapshot();
                if (!setArchitecturalObstacleSize(actions.architecturalObstacleObjectId,
                                                  actions.architecturalObstacleSizeM) &&
                    !m_undoStack.empty())
                    m_undoStack.pop_back();
            }
            if (actions.setArchitecturalObstacleBlocking)
            {
                pushUndoSnapshot();
                if (!setArchitecturalObstacleBlocking(actions.architecturalObstacleObjectId,
                                                      actions.architecturalObstacleBlocksRouting) &&
                    !m_undoStack.empty())
                    m_undoStack.pop_back();
            }
            if (actions.addWall)
            {
                pushUndoSnapshot();
                addArchitecturalObstacle(ArchitecturalObstacleType::Wall);
            }
            if (actions.addSlab)
            {
                pushUndoSnapshot();
                addArchitecturalObstacle(ArchitecturalObstacleType::Slab);
            }
            if (actions.addShaft)
            {
                pushUndoSnapshot();
                addArchitecturalObstacle(ArchitecturalObstacleType::Shaft);
            }
            if (actions.addForbiddenZone)
            {
                pushUndoSnapshot();
                addArchitecturalObstacle(ArchitecturalObstacleType::ForbiddenZone);
            }
            if (actions.addPipe)
            {
                pushUndoSnapshot();
                addPipeObject(1.0f, 40.0f);
            }
            if (actions.addElbow90)
            {
                pushUndoSnapshot();
                addElbow90Object(DefaultCompactElbow90BendRadiusM, 40.0f);
            }
            if (actions.addElbow45)
            {
                pushUndoSnapshot();
                addElbow45Object(DefaultElbow45BendRadiusM, 40.0f);
            }
            if (actions.addTee)
            {
                pushUndoSnapshot();
                addTeeObject(DefaultCompactTeeMainLengthM, DefaultCompactTeeBranchLengthM, 40.0f);
            }
            if (actions.addReducer)
            {
                pushUndoSnapshot();
                addReducerObject(0.25f, 50.0f, 40.0f);
            }
            if (actions.addValve)
            {
                pushUndoSnapshot();
                addValveObject(0.24f, 40.0f);
            }
            if (actions.addEndCap)
            {
                pushUndoSnapshot();
                addEndCapObject(0.10f, 40.0f);
            }
            if (actions.autoAttach)
            {
                pushUndoSnapshot();
                autoAttachElementToSelected(actions.autoAttachType, actions.autoAttachEndpoint);
            }
            if (actions.manualFittingRotate)
            {
                pushUndoSnapshot();
                applyManualFittingRotation(actions.manualRotateDegX, actions.manualRotateDegY,
                                           actions.manualRotateDegZ);
            }
            if (actions.exportBomCsv)
            {
                const BomSummary bom = buildBillOfMaterials(m_network);

                if (exportBillOfMaterialsCsv(bom, m_bomExportPath))
                {
                    setStatusMessage("Darabjegyzék exportálva: " + m_bomExportPath + " | " +
                                     std::to_string(bom.totalNetworkElementCount) + " hálózati elem, " +
                                     std::to_string(bom.items.size()) + " összesített tétel.");
                }
                else
                {
                    setStatusMessage("Darabjegyzék exportálási hiba: " + m_bomExportPath);
                }
            }

            if (actions.saveProject)
            {
                if (saveProject(m_projectPath))
                {
                    setStatusMessage("Projekt mentve: " + m_projectPath);
                }
                else
                {
                    setStatusMessage("Mentési hiba: nem sikerült írni a fájlt: " + m_projectPath);
                }
            }
            if (actions.loadProject || !actions.testProjectToLoad.empty())
            {
                const std::string path = actions.testProjectToLoad.empty() ? m_projectPath : actions.testProjectToLoad;
                pushUndoSnapshot();
                if (loadProject(path))
                {
                    m_projectPath = path;
                    if (!actions.testProjectToLoad.empty()) m_ui.showScenePanel();
                    setStatusMessage("Projekt betöltve: " + path);
                }
                else
                {
                    setStatusMessage("Betöltési hiba: nem található vagy hibás a fájl: " + path);
                }
            }
            if (actions.runValidation)
            {
                m_validationMessages = m_validator.validateNetwork(
                    m_network, m_objects, m_pressureDesignPressureBar, m_pressureTestPressureBar,
                    m_pressureHoldTimeMin, m_pressureInitialMeasuredPressureBar,
                    m_pressureFinalMeasuredPressureBar, m_pressureAllowedDropBar, m_validationStartId,
                    m_validationStartEndpoint, m_validationEndId, m_validationEndEndpoint, m_flowRateLps,
                    m_hydraulicFlowRateLps, m_hydraulicDensityKgM3, m_hydraulicKinematicViscosityM2s,
                    m_hydraulicMaxTotalPressureLossBar);

                m_validationHasRun = true;

                setStatusMessage(validationFailed(m_validationMessages)
                                     ? "Összes validáció és számítás kész: NEM FELELT MEG."
                                     : "Összes validáció és számítás kész: MEGFELELT.");
            }
            if (actions.runValidationOpenEnds)
            {
                m_validationMessages = m_validator.validateOpenEnds(m_network);

                m_validationHasRun = true;

                setStatusMessage(validationFailed(m_validationMessages)
                                     ? "Nyitott csővégek ellenőrzése kész: NEM FELELT MEG."
                                     : "Nyitott csővégek ellenőrzése kész: MEGFELELT.");
            }
            if (actions.runValidationIsolated)
            {
                m_validationMessages = m_validator.validateIsolatedElements(m_network);

                m_validationHasRun = true;

                setStatusMessage(validationFailed(m_validationMessages)
                                     ? "Izolált elemek ellenőrzése kész: NEM FELELT MEG."
                                     : "Izolált elemek ellenőrzése kész: MEGFELELT.");
            }
            if (actions.runValidationConnected)
            {
                m_validationMessages = m_validator.validateConnectedComponents(m_network);

                m_validationHasRun = true;

                setStatusMessage(validationFailed(m_validationMessages)
                                     ? "Hálózat összefüggőségi ellenőrzése kész: NEM FELELT MEG."
                                     : "Hálózat összefüggőségi ellenőrzése kész: MEGFELELT.");
            }
            if (actions.runValidationMszBasic)
            {
                m_validationMessages = m_validator.validateMszEn806BasicData(m_network);

                m_validationHasRun = true;

                setStatusMessage(validationFailed(m_validationMessages)
                                     ? "Vízhálózati alapadat-ellenőrzés kész: NEM FELELT MEG."
                                     : "Vízhálózati alapadat-ellenőrzés kész: MEGFELELT.");
            }
            if (actions.runValidationPressure)
            {
                m_validationMessages = m_validator.validateMszEn806PressureTest(
                    m_network, m_validationStartId, m_pressureDesignPressureBar, m_pressureTestPressureBar,
                    m_pressureHoldTimeMin, m_pressureInitialMeasuredPressureBar,
                    m_pressureFinalMeasuredPressureBar, m_pressureAllowedDropBar);
                m_validationHasRun = true;
                setStatusMessage(validationFailed(m_validationMessages)
                                     ? "Projekt nyomáspróba kész: NEM FELELT MEG."
                                     : "Projekt nyomáspróba kész: MEGFELELT.");
            }
            if (actions.runValidationFlow)
            {
                m_validationMessages = m_validator.validateMszEn806FlowVelocity(
                    m_network, m_objects, m_validationStartId, m_validationStartEndpoint, m_validationEndId,
                    m_validationEndEndpoint, m_flowRateLps);
                m_validationHasRun = true;
                setStatusMessage(validationFailed(m_validationMessages)
                                     ? "Nyílt vízellátási sebességvizsgálat kész: NEM FELELT MEG."
                                     : "Nyílt vízellátási sebességvizsgálat kész: MEGFELELT.");
            }
            if (actions.runValidationMszEn1610)
            {
                m_validationMessages = m_validator.validateMszEn1610Drainage(
                    m_network, m_objects, m_validationStartId, m_validationStartEndpoint, m_validationEndId,
                    m_validationEndEndpoint, m_flowRateLps);
                m_validationHasRun = true;
                setStatusMessage(validationFailed(m_validationMessages)
                                     ? "Nyílt lefolyási Table 6 ellenőrzés kész: NEM FELELT MEG."
                                     : "Nyílt lefolyási Table 6 ellenőrzés kész: MEGFELELT.");
            }
            if (actions.runValidationDarcyWeisbach)
            {
                m_validationMessages = m_validator.validateDarcyWeisbachPressureLoss(
                    m_network, m_objects, m_validationStartId, m_validationStartEndpoint, m_validationEndId,
                    m_validationEndEndpoint, m_hydraulicFlowRateLps, m_hydraulicDensityKgM3,
                    m_hydraulicKinematicViscosityM2s, m_hydraulicMaxTotalPressureLossBar);
                m_validationHasRun = true;
                setStatusMessage(validationFailed(m_validationMessages)
                                     ? "Darcy-Weisbach számítás kész: NEM FELELT MEG."
                                     : "Darcy-Weisbach számítás kész: MEGFELELT.");
            }

            if (actions.exportValidationCsv && m_validationHasRun && !m_validationMessages.empty())
            {
                constexpr auto path = "plumbcad_validation.csv";
                if (exportValidationCsv(m_validationMessages, path))
                    setStatusMessage(std::string("Validációs CSV mentve: ") + path);
                else
                    setStatusMessage(std::string("Validációs CSV mentési hiba: ") + path);
            }

            // A transzformációs gizmo az aktuális kijelölést és az ebben a
            // frame-ben választott Mozgatás/Forgatás módot követi.
            updateTransformGizmo();

            ImGui::Render();
            std::vector<SceneObject> renderObjects = m_objects;
            // A sárga A* előnézet csak renderelési segédobjektum:
            // nem része a PipeNetworknek, nem mentődik és nem jelölhető ki.
            renderObjects.insert(renderObjects.end(), m_routePreviewObjects.begin(), m_routePreviewObjects.end());
            appendEndpointMarkers(renderObjects);

            // A központi világ XYZ referencia LOGIKÁJA megmarad
            // (origó=(0,0,0), Y függőleges, XZ vízszintes sík),
            // de a három nagy világ-tengely nyilat szándékosan NEM rendereljük.
            // A kijelölt elem kis transzformációs gizmoja ettől függetlenül látható.

            // A gizmo utolsóként kerül a listába és renderOnTop=true,
            // ezért CAD-szerűen az objektum fölött is jól látható.
            renderObjects.insert(renderObjects.end(), m_transformGizmoObjects.begin(),
                                 m_transformGizmoObjects.end());

            m_renderer.render(renderObjects, m_camera, m_width, m_height, m_selectedId, m_wireframeMode);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(m_window);
        }
        return 0;
    }
}
