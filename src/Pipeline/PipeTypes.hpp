#pragma once
#include "Core/Types.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <string>
#include <vector>

namespace plumbcad
{
    inline constexpr float MinStraightPipeLengthM = 0.04f;
    inline constexpr float MaxStraightPipeLengthM = 8.0f;

    inline constexpr float DefaultElbow45BendRadiusM = 0.12f;
    inline constexpr float DefaultCompactElbow90BendRadiusM = 0.06f;
    inline constexpr float DefaultCompactTeeMainLengthM = 0.14f;
    inline constexpr float DefaultCompactTeeBranchLengthM = 0.07f;

    enum class PipeElementType
    {
        StraightPipe,
        Elbow90,
        Elbow45,
        AlignmentElbow,
        Tee,
        Reducer,
        Valve,
        EndCap
    };

    struct PipeCatalogSize
    {
        float diameterMm{50.0f};
        float wallThicknessMm{3.0f};
    };

    struct PipeTypeDefinition
    {
        std::string name;
        std::string material;
        std::vector<PipeCatalogSize> sizes;
    };

    struct PipeValidationProfile
    {
        bool drainageCheckEnabled{true};
        float drainageMinAngleDeg{0.50f};
        float drainageMaxAngleDeg{10.00f};

        // Darcy-Weisbach: A* egyenes csövek érdességi sablonja.
        float hydraulicRoughnessMm{0.007f};
    };

    struct ConnectionPoint
    {
        glm::vec3 localPosition{};
        ObjectId connectedElementId{InvalidObjectId};
        int connectedPointIndex{-1};

        // Ha true, a csatlakozási pont szándékosan marad nyitva
        // (pl. hálózati bemenet, kifolyó vagy későbbi csatlakozás).
        // Csak nyitott csővégnél értelmezett.
        bool intentionalOpenEnd{false};

        [[nodiscard]] bool isOpen() const { return connectedElementId == InvalidObjectId; }
    };

    struct PipeElement
    {
        ObjectId id{InvalidObjectId};
        PipeElementType type{PipeElementType::StraightPipe};
        int pipeTypeIndex{0};
        int pipeSizeIndex{0};

        // Elsődleges katalógusméret. Normál cső/idom esetén minden
        // csatlakozás ezt használja.
        float diameterMm{50.0f};
        float wallThicknessMm{3.0f};

        // Szűkítő másodlagos (1. végponti) katalógusmérete.
        // Más elemtípusoknál secondaryPipeSizeIndex = -1.
        int secondaryPipeSizeIndex{-1};
        float secondaryDiameterMm{0.0f};
        float secondaryWallThicknessMm{0.0f};
        float lengthM{1.0f};
        float bendRadiusM{0.12f};
        float designPressureBar{6.0f};
        std::string material{"PPR"};

        // MSZ EN 1610-hoz kapcsolódó, szakdolgozati prototípus-szintű
        // gravitációs lefolyás ellenőrzési adatok. A minimum/maximum lejtés
        // projektparaméter, nem beégetett szabvány-határérték.
        bool drainageCheckEnabled{true};

        float drainageMinAngleDeg{0.50f};
        float drainageMaxAngleDeg{10.00f};

        // Darcy-Weisbach START -> END számításhoz használt csőérdesség.
        // Q, sűrűség, viszkozitás és max. teljes veszteség globális paraméter.
        float hydraulicRoughnessMm{0.007f};

        std::vector<ConnectionPoint> connections;
    };

    inline float pipeEndpointDiameterMm(const PipeElement &pipe, int endpointIndex)
    {
        if (pipe.type == PipeElementType::Reducer && endpointIndex == 1 && pipe.secondaryDiameterMm > 0.0f)
        {
            return pipe.secondaryDiameterMm;
        }

        return pipe.diameterMm;
    }

    inline float pipeEndpointWallThicknessMm(const PipeElement &pipe, int endpointIndex)
    {
        if (pipe.type == PipeElementType::Reducer && endpointIndex == 1 && pipe.secondaryWallThicknessMm > 0.0f)
        {
            return pipe.secondaryWallThicknessMm;
        }

        return pipe.wallThicknessMm;
    }

    inline int pipeEndpointSizeIndex(const PipeElement &pipe, int endpointIndex)
    {
        if (pipe.type == PipeElementType::Reducer && endpointIndex == 1 && pipe.secondaryPipeSizeIndex >= 0)
        {
            return pipe.secondaryPipeSizeIndex;
        }

        return pipe.pipeSizeIndex;
    }

    inline float pipeMaximumDiameterMm(const PipeElement &pipe)
    {
        if (pipe.type == PipeElementType::Reducer && pipe.secondaryDiameterMm > 0.0f)
        {
            return std::max(pipe.diameterMm, pipe.secondaryDiameterMm);
        }

        return pipe.diameterMm;
    }

    inline void applyPipeValidationProfile(PipeElement &pipe, const PipeValidationProfile &profile)
    {
        pipe.drainageCheckEnabled = profile.drainageCheckEnabled;
        pipe.drainageMinAngleDeg = profile.drainageMinAngleDeg;
        pipe.drainageMaxAngleDeg = profile.drainageMaxAngleDeg;

        pipe.hydraulicRoughnessMm = profile.hydraulicRoughnessMm;
    }
}
