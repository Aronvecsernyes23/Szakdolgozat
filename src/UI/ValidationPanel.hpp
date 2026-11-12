#pragma once

#include "Core/Types.hpp"
#include "Validation/Validation.hpp"

#include <vector>

namespace plumbcad
{
    struct UiActions;

    struct ValidationPanelModel
    {
        ObjectId &selectedId;

        float &pressureDesignPressureBar;
        float &pressureTestPressureBar;
        float &pressureHoldTimeMin;
        float &pressureInitialMeasuredPressureBar;
        float &pressureFinalMeasuredPressureBar;
        float &pressureAllowedDropBar;

        ObjectId validationStartId;
        int validationStartEndpoint;
        ObjectId validationEndId;
        int validationEndEndpoint;

        float &flowRateLps;

        float &hydraulicFlowRateLps;
        float &hydraulicDensityKgM3;
        float &hydraulicKinematicViscosityM2s;
        float &hydraulicMaxTotalPressureLossBar;

        const std::vector<ValidationMessage> &messages;
        bool validationHasRun;
        const std::string &statusMessage;
    };

    void drawValidationPanel(const ValidationPanelModel &model, UiActions &actions);
}
