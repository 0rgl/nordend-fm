#include "nordend/core/command_line.hpp"

#include <utility>

namespace nordend::core {

std::optional<std::vector<std::string>> split_command_line(std::string_view line) {
    // A subset of shell quoting, as GLib's g_shell_parse_argv reads it: the spec's double quotes,
    // and also single quotes and backslashes outside quotes, which real desktop files use.
    std::vector<std::string> args;
    std::string arg;
    bool in_arg = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == ' ' || c == '\t' || c == '\n') {
            if (in_arg) {
                args.push_back(std::move(arg));
                arg.clear();
                in_arg = false;
            }
            continue;
        }
        in_arg = true;
        if (c == '"') {
            for (++i;; ++i) {
                if (i == line.size()) {
                    return std::nullopt;  // not closed
                }
                if (line[i] == '"') {
                    break;
                }
                if (line[i] == '\\' && i + 1 < line.size() &&
                    std::string_view{"\"`$\\"}.find(line[i + 1]) != std::string_view::npos) {
                    ++i;
                }
                arg.push_back(line[i]);
            }
        } else if (c == '\'') {
            const std::size_t end = line.find('\'', i + 1);
            if (end == std::string_view::npos) {
                return std::nullopt;
            }
            arg += line.substr(i + 1, end - i - 1);
            i = end;
        } else if (c == '\\' && i + 1 < line.size()) {
            arg.push_back(line[++i]);
        } else {
            arg.push_back(c);
        }
    }
    if (in_arg) {
        args.push_back(std::move(arg));
    }
    if (args.empty()) {
        return std::nullopt;
    }
    return args;
}

}  // namespace nordend::core
