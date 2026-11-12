#include "Validation/ValidationReport.hpp"
#include "Core/Csv.hpp"

#include <algorithm>
#include <fstream>
#include <numeric>

namespace plumbcad
{
    std::vector<std::size_t> validationResultOrder(const std::vector<ValidationMessage> &messages)
    {
        const auto priority = [](const ValidationMessage &message) {
            if (message.severity == ValidationSeverity::Error) return 0;
            if (message.severity == ValidationSeverity::Warning) return 1;
            return message.isReference ? 3 : 2;
        };

        std::vector<std::size_t> order(messages.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return priority(messages[a]) < priority(messages[b]);
        });
        return order;
    }

    const char *validationResultName(const ValidationMessage &message)
    {
        if (message.isReference) return "REFERENCIA";

        switch (message.severity)
        {
        case ValidationSeverity::Info:
            return "MEGFELELT";
        case ValidationSeverity::Warning:
            return "FIGYELMEZTETÉS";
        case ValidationSeverity::Error:
            return "NEM FELELT MEG";
        }
        return "-";
    }

    bool exportValidationCsv(const std::vector<ValidationMessage> &messages, const std::string &path)
    {
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;

        // UTF-8 BOM and a semicolon separator match the bill-of-materials export.
        out.write("\xEF\xBB\xBF", 3);
        out << "ID;Végpont;Vizsgálat;Eredmény;Részletek\r\n";
        for (const auto index : validationResultOrder(messages))
        {
            const auto &message = messages[index];
            if (message.objectId != InvalidObjectId) out << message.objectId;
            out << ';';
            if (message.endpointIndex >= 0) out << message.endpointIndex;
            out << ';' << csvCell(message.ruleName) << ';' << validationResultName(message) << ';'
                << csvCell(message.text) << "\r\n";
        }

        out.close();
        return static_cast<bool>(out);
    }
}
