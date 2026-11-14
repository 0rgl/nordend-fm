#include "nordend/views/actions.hpp"

#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "nordend/core/config.hpp"
#include "nordend/fs/mime.hpp"

using nordend::core::Config;
using nordend::fs::MimeDatabase;
using nordend::views::action_fits;
using nordend::views::ActionTarget;

namespace {

Config::Action with_patterns(std::vector<std::string> patterns) {
    Config::Action action;
    action.name = "Act";
    action.exec = {"tool"};
    action.patterns = std::move(patterns);
    return action;
}

}  // namespace

TEST_CASE("actions: patterns are globs on the whole name, case left aside") {
    const auto mime = MimeDatabase::load();
    const Config::Action tar = with_patterns({"*.tar.gz", "*.zip"});
    const auto fits = [&](const char* name) {
        const std::vector<ActionTarget> one{{.name = name}};
        return action_fits(tar, one, *mime);
    };
    CHECK(fits("photos.tar.gz"));
    CHECK(fits("PHOTOS.TAR.GZ"));
    CHECK(fits("a.zip"));
    CHECK(fits(".hidden.zip"));
    CHECK_FALSE(fits("notes.gz"));  // *.tar.gz is the whole name's
    CHECK_FALSE(fits("a.zip.txt"));
    CHECK_FALSE(fits("zip"));
    // No patterns and no types: any name.
    const std::vector<ActionTarget> any{{.name = "whatever"}};
    CHECK(action_fits(with_patterns({}), any, *mime));
}

TEST_CASE("actions: types take their subtypes; a pattern or a type is enough") {
    const auto mime = MimeDatabase::load();
    Config::Action text = with_patterns({});
    text.mime = {"text/plain"};
    const auto as = [&](const char* name, const char* type) {
        const std::vector<ActionTarget> one{{.name = name, .mime = mime->find(type)}};
        return action_fits(text, one, *mime);
    };
    CHECK(as("a.txt", "text/plain"));
    CHECK(as("a.cpp", "text/x-c++src"));  // a text/plain by inheritance
    CHECK_FALSE(as("a.png", "image/png"));
    const std::vector<ActionTarget> unknown{{.name = "data"}};  // not sniffed yet
    CHECK_FALSE(action_fits(text, unknown, *mime));
    text.patterns = {"data"};
    CHECK(action_fits(text, unknown, *mime));
    CHECK(as("a.png", "image/png") == false);
    text.mime = {"no/such-type"};
    CHECK_FALSE(as("a.txt", "text/plain"));
}

TEST_CASE("actions: kinds and how many: every entry must fit, or none is offered") {
    const auto mime = MimeDatabase::load();
    Config::Action zip = with_patterns({"*.zip"});
    const std::vector<ActionTarget> two{{.name = "a.zip"}, {.name = "b.zip"}};
    const std::vector<ActionTarget> mixed{{.name = "a.zip"}, {.name = "b.txt"}};
    const std::vector<ActionTarget> folder{{.name = "x.zip", .folder = true}};
    CHECK(action_fits(zip, two, *mime));
    CHECK_FALSE(action_fits(zip, mixed, *mime));   // a command never gets what it was not for
    CHECK_FALSE(action_fits(zip, folder, *mime));  // files by default
    zip.folders = true;
    CHECK(action_fits(zip, folder, *mime));
    zip.files = false;
    CHECK_FALSE(action_fits(zip, two, *mime));
    zip.files = true;
    zip.min = 3;
    CHECK_FALSE(action_fits(zip, two, *mime));
    zip.min = 1;
    zip.max = 1;
    CHECK_FALSE(action_fits(zip, two, *mime));
    CHECK(action_fits(zip, std::vector<ActionTarget>{{.name = "a.zip"}}, *mime));
    CHECK_FALSE(action_fits(zip, std::vector<ActionTarget>{}, *mime));
}
