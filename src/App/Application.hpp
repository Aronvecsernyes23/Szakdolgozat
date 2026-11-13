#pragma once
#include "Architecture/ArchitecturalObstacle.hpp"
#include "Geometry/SceneObject.hpp"
#include "Graphics/Camera.hpp"
#include "Graphics/Renderer.hpp"
#include "Pipeline/PipeNetwork.hpp"
#include "UI/EditorUi.hpp"
#include "Validation/Validation.hpp"
#include "Routing/AStarRouter.hpp"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace plumbcad
{
    class Application
    {
        struct UndoSnapshot
        {
            PipeNetwork network;
            std::vector<SceneObject> objects;
            std::vector<ArchitecturalObstacle> architecturalObstacles;
            ObjectId selectedId{InvalidObjectId};
            std::vector<ObjectId> lastRouteCreatedIds;
            std::string projectPath;
            float pressureDesignPressureBar{};
            float pressureTestPressureBar{};
            float pressureHoldTimeMin{};
            float pressureInitialMeasuredPressureBar{};
            float pressureFinalMeasuredPressureBar{};
            float pressureAllowedDropBar{};
            float flowRateLps{};
            float flowMinVelocityMps{};
            float flowMaxVelocityMps{};
            float hydraulicFlowRateLps{};
            float hydraulicDensityKgM3{};
            float hydraulicKinematicViscosityM2s{};
            float hydraulicMaxTotalPressureLossBar{};

            ObjectId routeStartId{InvalidObjectId};
            int routeStartEndpoint{-1};
            ObjectId routeEndId{InvalidObjectId};
            int routeEndEndpoint{-1};

            ObjectId validationStartId{InvalidObjectId};
            int validationStartEndpoint{-1};
            ObjectId validationEndId{InvalidObjectId};
            int validationEndEndpoint{-1};
        };

    public:
        Application();
        ~Application();
        int run(const std::string& startupProject = {});
    private:
#ifdef PLUMBCAD_INTEGRATION_TESTS
        friend struct FunctionalTests;
#endif
        // --- Modulokra bontott Application ---
        // ApplicationLifecycle.cpp: ablak, ImGui, kezdő jelenet és fő ciklus
        // ApplicationObjects.cpp: cső/idom objektumok létrehozása és mesh frissítés
        // ApplicationPersistence.cpp: projekt mentés és betöltés
        // ApplicationNetwork.cpp: hálózati segédfüggvények, markerfrissítés, csoporttranszformáció
        // ApplicationGeometryRules.cpp: geometriai irányok, illesztési szabályok, forgatási kényszerek
        // ApplicationSnap.cpp: snap, csővég-illesztés és automatikus kapcsolódás
        // ApplicationInput.cpp: egér/billentyű kezelés, kameravezérlés, undo/redo
        // ApplicationFittings.cpp: idomforgatás és automatikus idom/cső rácsatolás
        void initWindow();
        void initImGui();
        void shutdownImGui();
        void seedScene();
        void addPipeObject(float lengthM, float diameterMm);
        void addElbow90Object(float bendRadiusM, float diameterMm);
        void addElbow45Object(float bendRadiusM, float diameterMm);
        void addTeeObject(float mainLengthM, float branchLengthM, float diameterMm);
        void addReducerObject(float lengthM, float inletDiameterMm, float outletDiameterMm);
        void addValveObject(float lengthM, float diameterMm);
        void addEndCapObject(float lengthM, float diameterMm);
        ObjectId addArchitecturalObstacle(ArchitecturalObstacleType type);
        bool setArchitecturalObstacleSize(ObjectId id, const glm::vec3& sizeM);
        bool setArchitecturalObstacleBlocking(ObjectId id, bool blocksRouting);
        bool deleteArchitecturalObstacle(ObjectId id);
        void rebuildArchitecturalObstacleMesh(ObjectId id);
        ArchitecturalObstacle* findArchitecturalObstacle(ObjectId id);
        const ArchitecturalObstacle* findArchitecturalObstacle(ObjectId id) const;
        SceneObject makeSceneObjectForArchitecturalObstacle(const ArchitecturalObstacle& obstacle) const;
        ObjectId allocateArchitecturalObjectId() const;
        bool deleteElement(ObjectId id);
        void autoAttachElementToSelected(PipeElementType newType, int targetPointIndex);
        void applyManualFittingRotation(float degreesX, float degreesY, float degreesZ);
        int selectedFittingRotationAnchorPoint(ObjectId objectId) const;
        glm::vec3 pipeCategoryColor(int pipeTypeIndex) const;
        SceneObject makeSceneObjectForPipe(const PipeElement& pipe) const;
        void rebuildPipeMesh(ObjectId id);
        bool resizeStraightPipe(ObjectId id, float newLengthM);
        bool saveProject(const std::string& path) const;
        bool loadProject(const std::string& path);
        void handleInput(float deltaTime);
        void onScroll(double yOffset);
        void handleMouseSelection();
        void handleObjectTransformInput();
        void switchTransformModeFromKeyboard();
        void updateEndpointMarkers();
        void appendEndpointMarkers(std::vector<SceneObject>& renderObjects) const;
        void initializeWorldAxisReference();
        void updateTransformGizmo();
        void clearTransformGizmo();
        void cleanupInvalidConnections();
        void trySnapSelectedPipe(bool allowTranslation);
        int trySnapAfterRotation();
        int connectAllNearbyOpenEndpoints(ObjectId objectId, const std::vector<ObjectId>& ignoredIds);
        int pickEndpointHandle(ObjectId objectId, double mouseX, double mouseY) const;
        bool mouseRayPlaneIntersection(double mouseX, double mouseY, const glm::vec3& planePoint, const glm::vec3& planeNormal, glm::vec3& outPoint) const;
        void setPipeEndpointToWorld(ObjectId objectId, int draggedPointIndex, int anchorPointIndex, const glm::vec3& targetWorld, bool keepCurrentLength);
        bool hasConnectedEndpoint(ObjectId objectId) const;
        int firstConnectedEndpoint(ObjectId objectId) const;
        int connectedEndpointCount(ObjectId objectId) const;
        bool isDirectionalFittingType(PipeElementType type) const;
        float elbowAngleRadians(const PipeElement& element) const;
        glm::vec3 elbowConnectorDirectionLocal(const PipeElement& element, int elbowPointIndex) const;
        glm::vec3 fittingConnectorDirectionLocal(const PipeElement& element, int pointIndex) const;
        glm::vec3 fittingConnectorDirectionWorld(ObjectId fittingId, int pointIndex) const;
        bool findElbowConnection(ObjectId straightPipeId, int& straightPointIndex, ObjectId& elbowId, int& elbowPointIndex) const;
        glm::vec3 straightContinuationDirectionWorld(ObjectId straightPipeId, int straightPointIndex) const;
        void alignStraightPipeToStraight(ObjectId movingStraightId, int movingPointIndex, ObjectId targetStraightId, int targetPointIndex);
        void setObjectRotationFromLocalDirection(SceneObject& object, const glm::vec3& localDirection, const glm::vec3& worldDirection);
        void rotateObjectRigidAroundWorldAxis(ObjectId objectId, const glm::vec3& anchorWorld, const glm::vec3& axisWorld, float angleRadians);
        void rotateConnectedAssemblyAroundFixedEndpoint(ObjectId objectId, int fixedPointIndex, float angleRadians);
        glm::vec3 cadRotationAxisWorld() const;
        float currentCadRotationDegrees() const;
        bool rotateCadObjectGroupByRadians(
            const std::vector<ObjectId>& objectIds,
            const glm::vec3& pivotWorld,
            const glm::vec3& axisWorld,
            float angleRadians);
        bool rotateCadSelectionByRadians(float angleRadians);
        bool applyCadRotationDegrees(float degrees);
        std::vector<ObjectId> connectedComponentAwayFromFixedEndpoint(ObjectId objectId, int fixedPointIndex) const;
        void alignElbowToStraight(ObjectId elbowId, int elbowPointIndex, ObjectId straightPipeId, int straightPointIndex);
        void alignElbowToElbow(ObjectId movingElbowId, int movingElbowPointIndex, ObjectId targetElbowId, int targetElbowPointIndex);
        void alignStraightPipeToElbow(ObjectId straightPipeId);
        void enforceElbowStraightConstraints();
        std::vector<ObjectId> connectedComponent(ObjectId objectId) const;
        void translateObjects(const std::vector<ObjectId>& objectIds, const glm::vec3& delta);
        void applyWorldMatrixToObject(ObjectId objectId, const glm::mat4& matrix);
        glm::vec3 connectionWorldPosition(ObjectId objectId, int pointIndex) const;
        void setStatusMessage(const std::string& message);
        void pushUndoSnapshot();
        UndoSnapshot makeUndoSnapshot() const;
        void restoreUndoSnapshot(const UndoSnapshot& snapshot);
        void undoLastAction();
        void redoLastAction();
        void handleUndoRedoShortcuts();
        void clearRouteSelection();
        void setRouteStart(ObjectId objectId, int endpointIndex);
        void setRouteEnd(ObjectId objectId, int endpointIndex);
        RouteCatalogPlan selectRouteCatalog(float startDiameterMm, float endDiameterMm, int startType) const;
        bool routeEndReducerIsClear(const glm::vec3& start, const glm::vec3& end, float diameterMm) const;
        bool searchAutomaticRoutes();
        bool buildSelectedAutomaticRoute();
        void applyRouteValidationProfileToElement(ObjectId id);
        void applyRouteValidationProfileToLastBuilt();
        void setValidationStart(ObjectId id, int endpoint);
        void setValidationEnd(ObjectId id, int endpoint);
        void clearValidationPath();
        void rebuildRoutePreview();
        void clearRoutePreview();
        glm::vec3 routeEndpointOutwardDirection(ObjectId objectId, int endpointIndex) const;
        std::vector<RouteObstacle> buildRoutingObstacles(
            ObjectId startId,
            ObjectId endId,
            float routedPipeRadiusM) const;
        std::vector<RouteObstacle> buildRoutingEndpointObstacles(
            ObjectId startId, ObjectId endId,
            float routedPipeRadiusM) const;
        ObjectId pickObject(double mouseX, double mouseY) const;
        SceneObject* findSceneObject(ObjectId id);
        const SceneObject* findSceneObject(ObjectId id) const;

        static void scrollCallback(GLFWwindow* window, double xOffset, double yOffset);

        GLFWwindow* m_window{nullptr};
        Renderer m_renderer;
        Camera m_camera;

        // Globális render-nézet: a wireframe csak megjelenítési állapot,
        // a geometriai/topológiai modellt nem módosítja.
        bool m_wireframeMode{false};

        PipeNetwork m_network;
        NetworkValidator m_validator;
        EditorUi m_ui;
        std::vector<SceneObject> m_objects;
        std::vector<ArchitecturalObstacle> m_architecturalObstacles;
        struct EndpointMarker
        {
            SceneObject object;
            ObjectId pipeId;
            int pointIndex;
            bool hasRole;
        };

        std::vector<EndpointMarker> m_endpointMarkers;

        // Központi világkoordináta-rendszer.
        // Origó = (0,0,0), Y a függőleges tengely, az XZ sík a vízszintes sík.
        std::vector<SceneObject> m_worldAxisObjects;

        // CAD-szerű X/Y/Z transzformációs tengelyek.
        // Csak Mozgatás és Forgatás módban, a kijelölt objektum közepén láthatók.
        std::vector<SceneObject> m_transformGizmoObjects;

        std::vector<ValidationMessage> m_validationMessages;
        bool m_validationHasRun{false};
        ObjectId m_selectedId{InvalidObjectId};
        int m_width{1280};
        int m_height{720};

        double m_lastFrameTime{0.0};
        double m_lastMouseX{0.0};
        double m_lastMouseY{0.0};
        bool m_firstMouseFrame{true};
        bool m_cameraLookActive{false};
        bool m_leftMouseWasDown{false};
        bool m_objectDragActive{false};
        bool m_objectDragMoved{false};
        double m_lastObjectDragX{0.0};
        double m_lastObjectDragY{0.0};
        TransformMode m_transformMode{TransformMode::Move};
        std::vector<ObjectId> m_dragObjectIds;
        int m_dragAnchorPointIndex{-1};
        glm::vec3 m_dragAnchorWorldPosition{0.0f};
        bool m_dragHasAnchor{false};
        bool m_moveConnectedGroup{false};
        MoveAxisConstraint m_moveAxisConstraint{MoveAxisConstraint::Free};

        // CAD-szerű forgatás: explicit világ-tengely, pontos szögkövetés és
        // opcionális teljes kapcsolt hálózatrész-forgatás.
        bool m_rotateConnectedGroup{false};
        RotateAxisConstraint m_rotateAxisConstraint{RotateAxisConstraint::Y};
        float m_rotationSnapDegrees{0.0f};

        // Az aktuális forgatási művelet szöge fokban.
        // Csak egyszerű float állapot: nincs ImGui input-élettartamhoz kötött
        // statikus szerkesztési állapot, így a Forgatás mód megnyitása stabil.
        float m_rotationCurrentDegrees{0.0f};

        bool m_rotateDragReady{false};
        bool m_rotateDragUseScreenAngle{false};
        glm::vec3 m_rotateDragPivotWorld{0.0f};
        glm::vec3 m_rotateDragAxisWorld{0.0f, 1.0f, 0.0f};
        glm::vec3 m_rotateDragStartVectorWorld{1.0f, 0.0f, 0.0f};
        float m_rotateDragAppliedAngleRad{0.0f};
        float m_rotateDragPivotScreenX{0.0f};
        float m_rotateDragPivotScreenY{0.0f};
        float m_rotateDragStartScreenAngleRad{0.0f};

        // CAD-szerű mozgatás: a húzás nem pixel-szorzóval történik,
        // hanem egy rögzített világ-síkon végzett sugár/sík metszéssel.
        bool m_moveDragPlaneReady{false};
        glm::vec3 m_moveDragPlanePoint{0.0f};
        glm::vec3 m_moveDragPlaneNormal{0.0f, 1.0f, 0.0f};
        glm::vec3 m_moveDragStartCursorWorld{0.0f};
        glm::vec3 m_moveDragStartSelectedPosition{0.0f};
        glm::vec3 m_moveDragAppliedDelta{0.0f};

        bool m_constrainedElbowRotate{false};
        glm::vec3 m_dragRotationAxisWorld{0.0f, 1.0f, 0.0f};
        int m_fittingRotationFixedEndpoint{0};
        std::string m_statusMessage{"Készen áll."};
        std::vector<UndoSnapshot> m_undoStack;
        std::vector<UndoSnapshot> m_redoStack;
        bool m_ctrlZWasDown{false};
        bool m_ctrlYWasDown{false};
        ObjectId m_routeStartId{InvalidObjectId};
        int m_routeStartEndpoint{-1};
        ObjectId m_routeEndId{InvalidObjectId};
        int m_routeEndEndpoint{-1};
        float m_routeGridSizeM{0.35f};
        float m_routeClearanceM{0.10f};

        // Az A* geometriai keresési logikája változatlan marad,
        // de a végleg létrehozott csövek/idomok katalóguskategóriája
        // külön kiválasztható.
        int m_routePipeTypeIndex{-1};

        PipeValidationProfile m_routeValidationProfile{};
        std::vector<ObjectId> m_lastRouteCreatedIds;

        float m_pressureDesignPressureBar{6.0f};
        float m_pressureTestPressureBar{9.0f};
        float m_pressureHoldTimeMin{30.0f};
        float m_pressureInitialMeasuredPressureBar{9.0f};
        float m_pressureFinalMeasuredPressureBar{9.0f};
        float m_pressureAllowedDropBar{0.20f};

        ObjectId m_validationStartId{InvalidObjectId};
        int m_validationStartEndpoint{-1};
        ObjectId m_validationEndId{InvalidObjectId};
        int m_validationEndEndpoint{-1};

        float m_flowRateLps{0.20f};

        // v8-v11 projektfájl-kompatibilitás miatt megőrzött legacy mezők.
        // A jelenlegi nyílt vízellátási profil fix 1.5 / 3.0 m/s határokat használ,
        // ezért ezek már nem részei az aktív validációs API-nak.
        float m_flowMinVelocityMps{0.10f};
        float m_flowMaxVelocityMps{2.00f};

        float m_hydraulicFlowRateLps{0.20f};
        float m_hydraulicDensityKgM3{998.2f};
        float m_hydraulicKinematicViscosityM2s{0.000001004f};
        float m_hydraulicMaxTotalPressureLossBar{0.50f};

        std::vector<RouteAlternative> m_routeAlternatives;
        std::vector<SceneObject> m_routePreviewObjects;
        int m_selectedRouteAlternative{0};
        AStarRouter m_router;
        std::string m_projectPath{"plumbcad_project.pcad"};
        std::string m_bomExportPath{"plumbcad_bom.csv"};
    };
}
