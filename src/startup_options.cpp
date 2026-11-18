#include "pch.h"

#include <charconv>

#include "startup_options.h"

namespace startup {
    bool Parse_Startup_Options(int argc, const char* const* argv, s_startup_options* options, std::string* error, char prefix) {
        if (options == nullptr || error == nullptr) {
            return false;
        }
        *options = {};
        const command_line::s_option_definition definitions[] = {
            {"config", true},
            {"skip-intro", false},
            {"load-save", true},
        };
        std::vector<command_line::s_option> parsed;
        if (!command_line::Parse_Options(argc, argv, prefix, definitions, std::size(definitions), &parsed, error)) {
            return false;
        }
        for (const command_line::s_option& option : parsed) {
            switch (option.definition_index) {
                case 0:
                    options->config_path = option.value;
                    options->config_selected = true;
                    break;
                case 1:
                    options->skip_intro = true;
                    break;
                case 2: {
                    const char* begin = option.value.data();
                    const char* end = begin + option.value.size();
                    int32_t slot = 0;
                    const auto result = std::from_chars(begin, end, slot);
                    if (result.ec != std::errc() || result.ptr != end || slot < 1 || slot > 10) {
                        *error = std::string(1, prefix) + "load-save expects a save number from 1 to 10";
                        return false;
                    }
                    options->load_save = slot;
                    break;
                }
            }
        }
        return true;
    }
} // namespace startup
