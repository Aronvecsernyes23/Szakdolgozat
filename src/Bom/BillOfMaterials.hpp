#pragma once

#include "Pipeline/PipeNetwork.hpp"

#include <string>
#include <vector>

namespace plumbcad
{
    struct BomItem
    {
        PipeElementType elementType{PipeElementType::StraightPipe};

        std::string name;
        std::string pipeSystem;
        std::string material;
        std::string sizeText;

        int quantity{0};

        // Straight pipes are primarily reported by total linear length.
        // For fittings this remains 0 and quantity is the relevant value.
        float totalLengthM{0.0f};
    };

    struct BomSummary
    {
        std::vector<BomItem> items;

        int totalNetworkElementCount{0};
        int straightPipePieceCount{0};
        int fittingPieceCount{0};
        float totalStraightPipeLengthM{0.0f};
    };

    BomSummary buildBillOfMaterials(const PipeNetwork &network);

    bool exportBillOfMaterialsCsv(const BomSummary &summary, const std::string &path);
}
