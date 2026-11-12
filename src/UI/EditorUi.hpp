#pragma once
#include "Architecture/ArchitecturalObstacle.hpp"
#include "Geometry/SceneObject.hpp"
#include "Graphics/Camera.hpp"
#include "Pipeline/PipeNetwork.hpp"
#include "Pipeline/PipeTypes.hpp"
#include "Validation/Validation.hpp"
#include "Routing/AStarRouter.hpp"
#include <vector>
#include <string>

namespace plumbcad
{
    enum class TransformMode
    {
        Move = 0,
        Rotate = 1,
        Scale = 2
    };

    enum class MoveAxisConstraint
    {
        Free = 0,
        X = 1,
        Y = 2,
        Z = 3
    };

    enum class RotateAxisConstraint
    {
        X = 0,
        Y = 1,
        Z = 2
    };

    struct UiActions
    {
        bool addPipe{false};
        bool addElbow90{false};
        bool addElbow45{false};
        bool addTee{false};
        bool addReducer{false};
        bool addValve{false};
        bool addEndCap{false};
        bool addWall{false};
        bool addSlab{false};
        bool addShaft{false};
        bool addForbiddenZone{false};
        bool setArchitecturalObstacleSize{false};
        ObjectId architecturalObstacleObjectId{InvalidObjectId};
        glm::vec3 architecturalObstacleSizeM{1.0f};
        bool setArchitecturalObstacleBlocking{false};
        bool architecturalObstacleBlocksRouting{true};
        bool runValidation{false};
        bool runValidationOpenEnds{false};
        bool runValidationIsolated{false};
        bool runValidationConnected{false};
        bool runValidationMszBasic{false};
        bool runValidationPressure{false};
        bool runValidationFlow{false};
        bool runValidationMszEn1610{false};
        bool runValidationDarcyWeisbach{false};

        bool intentionalOpenEndChanged{false};
        ObjectId intentionalOpenEndObjectId{InvalidObjectId};
        int intentionalOpenEndEndpoint{-1};
        bool intentionalOpenEndValue{false};

        bool validationSetStart{false};
        bool validationSetEnd{false};
        bool validationClear{false};
        ObjectId validationObjectId{InvalidObjectId};
        int validationEndpoint{-1};
        bool saveProject{false};
        bool loadProject{false};
        bool exportBomCsv{false};
        bool exportValidationCsv{false};
        bool toggleProjectionMode{false};
        bool toggleWireframeMode{false};

        bool setStraightPipeLength{false};
        ObjectId straightPipeLengthObjectId{InvalidObjectId};
        float straightPipeLengthM{1.0f};

        bool deleteElement{false};
        ObjectId deleteElementObjectId{InvalidObjectId};

        bool setCatalogPipeType{false};
        ObjectId catalogPipeTypeObjectId{InvalidObjectId};
        int catalogPipeTypeIndex{-1};

        bool setCatalogPipeSize{false};
        ObjectId catalogPipeSizeObjectId{InvalidObjectId};
        int catalogPipeSizeIndex{-1};

        bool setReducerSecondarySize{false};
        ObjectId reducerObjectId{InvalidObjectId};
        int reducerSecondarySizeIndex{-1};

        bool autoAttach{false};
        int autoAttachEndpoint{-1};
        PipeElementType autoAttachType{PipeElementType::StraightPipe};
        bool routeSetStart{false};
        bool routeSetEnd{false};
        bool routeSearch{false};
        bool routePreviewChanged{false};
        bool routeBuildSelected{false};
        bool routeClear{false};
        bool routeApplyValidationProfileToLastBuilt{false};
        ObjectId routeObjectId{InvalidObjectId};
        int routeEndpoint{-1};
        bool manualFittingRotate{false};
        float manualRotateDegX{0.0f};
        float manualRotateDegY{0.0f};
        float manualRotateDegZ{0.0f};

        bool applyExactRotation{false};
        float exactRotationDegrees{0.0f};
    };

    class EditorUi
    {
      public:
        UiActions draw(
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
            const std::string &statusMessage);
    };
}
