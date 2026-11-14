#include "nordend/views/actions.hpp"

#include <algorithm>
#include <fnmatch.h>
#include <string>
#include <utility>

namespace nordend::views {
namespace {

bool name_matches(const core::Config::Action& action, std::string_view name) {
    const std::string file{name};
    return std::ranges::any_of(action.patterns, [&file](const std::string& pattern) {
        return fnmatch(pattern.c_str(), file.c_str(), FNM_CASEFOLD) == 0;
    });
}

bool type_matches(const core::Config::Action& action, fs::MimeId mime,
                  const fs::MimeDatabase& database) {
    if (mime == fs::kUnknownMime) {
        return false;
    }
    return std::ranges::any_of(action.mime, [&](const std::string& type) {
        const fs::MimeId wanted = database.find(type);
        return wanted != fs::kUnknownMime && database.is_a(mime, wanted);
    });
}

}  // namespace

bool action_fits(const core::Config::Action& action, std::span<const ActionTarget> targets,
                 const fs::MimeDatabase& database) {
    const std::size_t count = targets.size();
    if (count == 0 || std::cmp_less(count, action.min) ||
        (action.max > 0 && std::cmp_greater(count, action.max))) {
        return false;
    }
    const bool any_name = action.patterns.empty() && action.mime.empty();
    return std::ranges::all_of(targets, [&](const ActionTarget& target) {
        if (target.folder ? !action.folders : !action.files) {
            return false;
        }
        return any_name || name_matches(action, target.name) ||
               type_matches(action, target.mime, database);
    });
}

}  // namespace nordend::views
