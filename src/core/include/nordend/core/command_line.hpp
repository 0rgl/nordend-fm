#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nordend::core {

// A command line split into arguments by the Desktop Entry spec's quoting rules (an Exec value,
// a config.toml action): arguments are separated by spaces; a double-quoted argument may hold
// spaces, and inside it \" \` \$ \\ stand for the character; single quotes and backslashes
// outside quotes work as in a shell, as GLib reads real desktop files. Nothing else of a shell:
// no variables, no globs. Field codes (%f, ...) are left in place. nullopt when a quote is not
// closed or nothing is left.
[[nodiscard]] std::optional<std::vector<std::string>> split_command_line(std::string_view line);

}  // namespace nordend::core
