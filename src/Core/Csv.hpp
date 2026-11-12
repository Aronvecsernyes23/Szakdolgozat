#pragma once

#include <string>

namespace plumbcad
{
    inline std::string csvCell(const std::string &value)
    {
        bool quote = false;

        for (char ch : value)
        {
            if (ch == ';' || ch == '"' || ch == '\n' || ch == '\r')
            {
                quote = true;
                break;
            }
        }

        if (!quote) return value;

        std::string result;
        result.reserve(value.size() + 2);

        result.push_back('"');

        for (char ch : value)
        {
            if (ch == '"') result.push_back('"');

            result.push_back(ch);
        }

        result.push_back('"');

        return result;
    }
}
