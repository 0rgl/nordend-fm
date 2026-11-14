#pragma once

#include <span>
#include <string_view>

#include "nordend/core/config.hpp"
#include "nordend/fs/mime.hpp"

namespace nordend::views {

// One selected entry, as an action's filters see it.
struct ActionTarget {
    std::string_view name;
    bool folder{false};
    fs::MimeId mime{fs::kUnknownMime};  // as known so far (unknown: only patterns can match)
};

// The action fits the selection: as many entries as min and max allow, every one of a kind it
// takes, and every one matching its patterns or its types (either is enough; neither given:
// any). Patterns are globs on the whole name, case left aside ("*.TAR.GZ" is "*.tar.gz"); a type
// takes its subtypes, as Open With does.
[[nodiscard]] bool action_fits(const core::Config::Action& action,
                               std::span<const ActionTarget> targets,
                               const fs::MimeDatabase& database);

}  // namespace nordend::views
