#include "Bom/BillOfMaterials.hpp"
#include "Core/Csv.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace plumbcad
{
    namespace
    {
        const char *bomElementName(PipeElementType type)
        {
            switch (type)
            {
            case PipeElementType::StraightPipe:
                return "Egyenes cső";

            case PipeElementType::Elbow90:
                return "90° könyök";

            case PipeElementType::Elbow45:
                return "45° könyök";

            case PipeElementType::AlignmentElbow:
                return "Végpont-illesztő könyök";

            case PipeElementType::Tee:
                return "T-idom";

            case PipeElementType::Reducer:
                return "Szűkítő";

            case PipeElementType::Valve:
                return "Szelep";

            case PipeElementType::EndCap:
                return "Végzáró";
            }

            return "Csőelem";
        }

        std::string pipeSystemName(const PipeNetwork &network, const PipeElement &element)
        {
            const auto &catalog = network.catalog();

            if (element.pipeTypeIndex >= 0 && element.pipeTypeIndex < static_cast<int>(catalog.size()))
            {
                return catalog[element.pipeTypeIndex].name;
            }

            return "Ismeretlen csőtípus";
        }

        std::string fixed1(float value)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << value;
            return oss.str();
        }

        std::string fixed2(float value)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << value;
            return oss.str();
        }

        std::string sizeDescription(const PipeElement &element)
        {
            std::ostringstream oss;

            if (element.type == PipeElementType::Reducer)
            {
                oss << "Ø" << fixed1(element.diameterMm) << " → Ø" << fixed1(element.secondaryDiameterMm) << " mm";

                return oss.str();
            }

            oss << "Ø" << fixed1(element.diameterMm) << " × " << fixed1(element.wallThicknessMm) << " mm";

            if (element.type == PipeElementType::Elbow90)
            {
                oss << " | R=" << fixed2(element.bendRadiusM) << " m";
            }
            else if (element.type == PipeElementType::Elbow45)
            {
                oss << " | R=" << fixed2(element.bendRadiusM) << " m";
            }
            else if (element.type == PipeElementType::AlignmentElbow)
            {
                const float radius = std::max(element.bendRadiusM, 0.0001f);

                const float angleDeg = element.lengthM / radius * 57.2957795131f;

                oss << " | " << fixed1(angleDeg) << "° | R=" << fixed2(element.bendRadiusM) << " m";
            }

            return oss.str();
        }

        std::string groupKey(const PipeElement &element)
        {
            std::ostringstream key;

            key << static_cast<int>(element.type) << '|' << element.pipeTypeIndex << '|' << element.pipeSizeIndex
                << '|' << std::fixed << std::setprecision(3) << element.diameterMm << '|'
                << element.wallThicknessMm;

            if (element.type == PipeElementType::Reducer)
            {
                key << '|' << element.secondaryPipeSizeIndex << '|' << element.secondaryDiameterMm << '|'
                    << element.secondaryWallThicknessMm;
            }
            else if (element.type == PipeElementType::Elbow90 || element.type == PipeElementType::Elbow45)
            {
                key << '|' << element.bendRadiusM;
            }
            else if (element.type == PipeElementType::AlignmentElbow)
            {
                const float radius = std::max(element.bendRadiusM, 0.0001f);

                const float angleDeg = element.lengthM / radius * 57.2957795131f;

                // Group automatic adapters only when the physical angle
                // and radius are effectively the same.
                const float roundedAngle = std::round(angleDeg * 10.0f) / 10.0f;

                key << '|' << element.bendRadiusM << '|' << roundedAngle;
            }

            return key.str();
        }
    }

    BomSummary buildBillOfMaterials(const PipeNetwork &network)
    {
        BomSummary summary;

        std::map<std::string, BomItem> grouped;

        summary.totalNetworkElementCount = static_cast<int>(network.elements().size());

        for (const auto &[id, element] : network.elements())
        {
            (void)id;

            const std::string key = groupKey(element);

            auto [it, inserted] = grouped.try_emplace(key);

            BomItem &item = it->second;

            if (inserted)
            {
                item.elementType = element.type;

                item.name = bomElementName(element.type);

                item.pipeSystem = pipeSystemName(network, element);

                item.material = element.material;

                item.sizeText = sizeDescription(element);
            }

            ++item.quantity;

            if (element.type == PipeElementType::StraightPipe)
            {
                item.totalLengthM += std::max(0.0f, element.lengthM);

                ++summary.straightPipePieceCount;

                summary.totalStraightPipeLengthM += std::max(0.0f, element.lengthM);
            }
            else
            {
                ++summary.fittingPieceCount;
            }
        }

        summary.items.reserve(grouped.size());

        for (auto &[key, item] : grouped)
        {
            (void)key;

            summary.items.push_back(std::move(item));
        }

        std::sort(summary.items.begin(), summary.items.end(), [](const BomItem &a, const BomItem &b) {
            if (a.elementType != b.elementType)
            {
                return static_cast<int>(a.elementType) < static_cast<int>(b.elementType);
            }

            if (a.pipeSystem != b.pipeSystem)
            {
                return a.pipeSystem < b.pipeSystem;
            }

            return a.sizeText < b.sizeText;
        });

        return summary;
    }

    bool exportBillOfMaterialsCsv(const BomSummary &summary, const std::string &path)
    {
        std::ofstream out(path, std::ios::binary);

        if (!out) return false;

        // UTF-8 BOM for convenient opening in Hungarian Excel.
        out.write("\xEF\xBB\xBF", 3);

        out << "Megnevezés;"
            << "Csőtípus;"
            << "Anyag;"
            << "Méret;"
            << "Darab [db];"
            << "Összes hossz [m]\n";

        for (const BomItem &item : summary.items)
        {
            out << csvCell(item.name) << ';' << csvCell(item.pipeSystem) << ';' << csvCell(item.material) << ';'
                << csvCell(item.sizeText) << ';' << item.quantity << ';';

            if (item.elementType == PipeElementType::StraightPipe)
            {
                out << std::fixed << std::setprecision(3) << item.totalLengthM;
            }

            out << '\n';
        }

        out << '\n';
        out << "ÖSSZESÍTÉS;;;;;\n";
        out << "Hálózati elemek összesen;;;;" << summary.totalNetworkElementCount << ";\n";
        out << "Egyenes csődarabok;;;;" << summary.straightPipePieceCount << ";\n";
        out << "Idomok / szerelvények;;;;" << summary.fittingPieceCount << ";\n";
        out << "Egyenes cső teljes hossza;;;;;" << std::fixed << std::setprecision(3)
            << summary.totalStraightPipeLengthM << '\n';

        return static_cast<bool>(out);
    }
}
