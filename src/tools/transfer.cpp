// Copy and move engine from the command line (ROADMAP 6.3). Not installed.
//
//   nordend-transfer [--move] [--force-copy] [--threads N] SOURCE_DIR DESTINATION_DIR [NAME...]
//   nordend-transfer --trash SOURCE_DIR NAME...
//   nordend-transfer --erase SOURCE_DIR NAME...
//
// Copies (or moves) NAMEs (default: everything in SOURCE_DIR) as Ctrl+V does, conflicts
// skipped, and prints files, bytes, time and rate. Compare with: time cp -a SOURCE_DIR/. DEST.
// --threads N: how many threads copy a folder's files (default: Nordend's choice).
// --trash moves NAMEs to the trash as Delete does (ROADMAP 6.4): the real one, from
// $XDG_DATA_HOME or $HOME. --erase deletes NAMEs for good as Shift+Delete does (ROADMAP 6.4d),
// sidecars along: compare with time rm -rf. A SOURCE_DIR inside an archive ("x.tar.gz" or
// "x.zip/docs") extracts, by the engine in its cage as Ctrl+V does (0.9.0): compare with tar x.
#include "nordend/fs/transfer.hpp"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "nordend/core/worker_pool.hpp"
#include "nordend/engines/engines.hpp"
#include "nordend/fs/archive_folder.hpp"
#include "nordend/fs/erase.hpp"
#include "nordend/fs/extract.hpp"
#include "nordend/fs/trash.hpp"
#include "nordend/isolate/helpers.hpp"

namespace fs = nordend::fs;

int main(int argc, char** argv) {
    const std::span args{argv + 1, static_cast<std::size_t>(argc > 0 ? argc - 1 : 0)};
    fs::TransferRequest request;
    std::vector<std::string> positional;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const char* const arg = args[i];
        const std::string_view option{arg};
        if (option == "--threads" && i + 1 < args.size()) {
            const std::string_view count{args[++i]};
            std::size_t threads = 0;
            const auto [end, failed] =
                std::from_chars(count.data(), count.data() + count.size(), threads);
            if (failed != std::errc{} || end != count.data() + count.size() || threads == 0 ||
                threads > 64) {
                std::fputs("nordend-transfer: --threads wants a number from 1 to 64\n", stderr);
                return 2;
            }
            request.threads = threads;
        } else if (option == "--move") {
            request.kind = fs::TransferKind::move;
        } else if (option == "--trash") {
            request.kind = fs::TransferKind::trash;
        } else if (option == "--erase") {
            request.kind = fs::TransferKind::erase;
        } else if (option == "--force-copy") {
            request.force_copy = true;
        } else {
            positional.emplace_back(arg);
        }
    }
    const bool trash = request.kind == fs::TransferKind::trash;
    const bool erase = request.kind == fs::TransferKind::erase;
    if (positional.size() < 2) {
        std::fputs(
            "usage: nordend-transfer [--move] [--force-copy] [--threads N] SOURCE_DIR "
            "DESTINATION_DIR "
            "[NAME...]\n"
            "       nordend-transfer --trash SOURCE_DIR NAME...\n"
            "       nordend-transfer --erase SOURCE_DIR NAME...\n",
            stderr);
        return 2;
    }
    if (trash) {
        // No destination: the home trash; every name is given.
        positional.insert(positional.begin() + 1, fs::home_trash().string());
    } else if (erase) {
        positional.insert(positional.begin() + 1, std::string{});  // no destination
    }
    try {
        request.source = positional[0];
        request.destination = positional[1];
        request.names.assign(positional.begin() + 2, positional.end());
        const std::optional<fs::ArchiveLocation> archive = fs::find_archive(request.source);
        if (archive) {
            // The engines read it, as in Nordend; the names default to the folder's in it.
            auto registry =
                std::make_shared<const nordend::engines::Registry>(nordend::engines::Registry::load(
                    nordend::engines::default_directory(), nordend::engines::locate_program));
            nordend::isolate::use_engines(*registry);
            nordend::engines::set_installed(std::move(registry));
            if (request.names.empty()) {
                const auto tree = fs::archive_tree(archive->archive, nullptr);
                if (const auto* entries = tree->folder(archive->inner); entries != nullptr) {
                    for (const fs::Entry& entry : *entries) {
                        request.names.push_back(entry.name);
                    }
                }
            }
        } else if (request.names.empty()) {
            for (const auto& entry : std::filesystem::directory_iterator{request.source}) {
                request.names.push_back(entry.path().filename().string());
            }
        }
        const auto start = std::chrono::steady_clock::now();
        const fs::TransferCallbacks callbacks{
            .progress = nullptr,
            .resolve = [](const fs::Conflict&) { return fs::Resolution::skip; }};
        const nordend::core::CancelToken token;
        fs::TransferResult result;
        if (trash) {
            result = fs::run_trash(request, token, callbacks);
        } else if (erase) {
            result = fs::run_erase(request, token, callbacks);
        } else if (archive) {
            result = fs::run_extract(request, token, callbacks);
        } else {
            result = fs::run_transfer(request, token, callbacks);
        }
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (!result.refused.empty()) {
            std::fprintf(stderr, "refused: %s\n", result.refused.c_str());
            return 1;
        }
        for (const std::string& error : result.errors) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
        }
        if (trash) {
            std::printf("%zu items to the trash in %.3f s\n", result.files_done, seconds);
            return result.errors.empty() ? 0 : 1;
        }
        if (erase) {
            std::printf("%zu files and folders (%.1f MB) deleted in %.3f s\n", result.files_done,
                        static_cast<double>(result.bytes_done) / 1e6, seconds);
            return result.errors.empty() ? 0 : 1;
        }
        std::printf("%zu files, %.1f MB in %.3f s: %.0f MB/s\n", result.files_done,
                    static_cast<double>(result.bytes_done) / 1e6, seconds,
                    static_cast<double>(result.bytes_done) / 1e6 / seconds);
        return result.errors.empty() ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "nordend-transfer: %s\n", e.what());
        return 1;
    }
}
