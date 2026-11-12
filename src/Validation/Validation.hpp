#pragma once
#include "Pipeline/PipeNetwork.hpp"
#include "Geometry/SceneObject.hpp"
#include <string>
#include <vector>

namespace plumbcad
{
    enum class ValidationSeverity
    {
        Info,
        Warning,
        Error
    };

    struct ValidationMessage
    {
        ValidationSeverity severity{ValidationSeverity::Info};
        ObjectId objectId{InvalidObjectId};
        int endpointIndex{-1};
        std::string ruleName;
        std::string text;
        bool isReference{false};
        bool isRedundantSummary{false};
    };

    class NetworkValidator
    {
      public:
        std::vector<ValidationMessage> validateOpenEnds(const PipeNetwork &network) const;
        std::vector<ValidationMessage> validateIsolatedElements(const PipeNetwork &network) const;
        std::vector<ValidationMessage> validateConnectedComponents(const PipeNetwork &network) const;
        std::vector<ValidationMessage> validateMszEn806BasicData(const PipeNetwork &network) const;
        std::vector<ValidationMessage> validateMszEn806PressureTest(
            const PipeNetwork &network, ObjectId networkRootId, float designPressureBar, float testPressureBar,
            float holdTimeMin, float initialMeasuredPressureBar, float finalMeasuredPressureBar,
            float allowedPressureDropBar) const;
        std::vector<ValidationMessage> validateMszEn806FlowVelocity(const PipeNetwork &network,
                                                                    const std::vector<SceneObject> &objects,
                                                                    ObjectId startId, int startEndpoint,
                                                                    ObjectId endId, int endEndpoint,
                                                                    float flowRateLps) const;
        std::vector<ValidationMessage> validateMszEn1610Drainage(const PipeNetwork &network,
                                                                 const std::vector<SceneObject> &objects,
                                                                 ObjectId startId, int startEndpoint,
                                                                 ObjectId endId, int endEndpoint,
                                                                 float peakFlowRateLps) const;
        std::vector<ValidationMessage> validateDarcyWeisbachPressureLoss(
            const PipeNetwork &network, const std::vector<SceneObject> &objects, ObjectId startId,
            int startEndpoint, ObjectId endId, int endEndpoint, float flowRateLps, float densityKgM3,
            float kinematicViscosityM2s, float maxTotalPressureLossBar) const;
        std::vector<ValidationMessage> validateNetwork(
            const PipeNetwork &network, const std::vector<SceneObject> &objects, float pressureDesignPressureBar,
            float pressureTestPressureBar, float pressureHoldTimeMin, float pressureInitialMeasuredPressureBar,
            float pressureFinalMeasuredPressureBar, float pressureAllowedDropBar, ObjectId validationStartId,
            int validationStartEndpoint, ObjectId validationEndId, int validationEndEndpoint, float flowRateLps,
            float hydraulicFlowRateLps, float hydraulicDensityKgM3, float hydraulicKinematicViscosityM2s,
            float hydraulicMaxTotalPressureLossBar) const;
    };
}
