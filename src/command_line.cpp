#include "pch.h"

#include "command_line.h"

namespace command_line {
    bool Parse_Options(int argc, const char* const* argv, char prefix, const s_option_definition* definitions, size_t definition_count, std::vector<s_option>* options, std::string* error) {
        if (options == nullptr || error == nullptr) {
            return false;
        }
        options->clear();
        error->clear();
        if (argc < 1 || argv == nullptr || definitions == nullptr || (prefix != '/' && prefix != '-')) {
            *error = "invalid command-line parser arguments";
            return false;
        }

        std::vector<bool> seen(definition_count, false);
        for (int index = 1; index < argc; ++index) {
            if (argv[index] == nullptr) {
                *error = "invalid null command-line argument";
                return false;
            }
            const std::string argument = argv[index];
            if (argument.empty() || argument[0] != prefix) {
                *error = "unexpected argument '" + argument + "'";
                return false;
            }
            const size_t equals = argument.find('=');
            const std::string name = argument.substr(1, equals == std::string::npos ? equals : equals - 1);
            size_t definition_index = 0;
            while (definition_index < definition_count && name != definitions[definition_index].name) {
                ++definition_index;
            }
            if (definition_index == definition_count) {
                *error = "unknown option '" + argument + "'";
                return false;
            }
            const std::string spelling = std::string(1, prefix) + name;
            if (seen[definition_index]) {
                *error = "duplicate option '" + spelling + "'";
                return false;
            }
            seen[definition_index] = true;
            s_option option = {definition_index, {}};
            if (definitions[definition_index].requires_value) {
                if (equals != std::string::npos) {
                    option.value = argument.substr(equals + 1);
                } else if (index + 1 < argc && argv[index + 1] != nullptr && argv[index + 1][0] != prefix) {
                    option.value = argv[++index];
                }
                if (option.value.empty()) {
                    *error = spelling + " requires a value";
                    return false;
                }
            } else if (equals != std::string::npos) {
                *error = spelling + " does not accept a value";
                return false;
            }
            options->push_back(std::move(option));
        }
        return true;
    }
} // namespace command_line
