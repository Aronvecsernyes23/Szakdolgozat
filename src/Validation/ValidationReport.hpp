#pragma once

#include "Validation/Validation.hpp"

namespace plumbcad
{
    std::vector<std::size_t> validationResultOrder(const std::vector<ValidationMessage>& messages);
    const char* validationResultName(const ValidationMessage& message);
    bool exportValidationCsv(const std::vector<ValidationMessage>& messages, const std::string& path);
}
