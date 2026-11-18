#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace command_line {
#if defined(_WIN32)
    inline constexpr char OPTION_PREFIX = '/';
#else
    inline constexpr char OPTION_PREFIX = '-';
#endif

    struct s_option_definition {
        const char* name;
        bool requires_value;
    };

    struct s_option {
        size_t definition_index;
        std::string value;
    };

    // Accepts native-prefix options with separate or '=value' arguments.
    // Unknown options, positional arguments, and duplicates are errors.
    bool Parse_Options(int argc, const char* const* argv, char prefix, const s_option_definition* definitions, size_t definition_count, std::vector<s_option>* options, std::string* error);
} // namespace command_line
