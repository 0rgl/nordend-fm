// A folder's size benchmark (the info pane, 0.7.5). Not installed.
//
//   nordend-bench-size [--runs N] DIR...
//
// Per run and folder, fs::measure as the info pane calls it (on this thread, the pane's options):
// the time, what it found and how many times it told its progress. Run 1 after dropping the
// caches is the cold number (`echo 3 | sudo tee /proc/sys/vm/drop_caches` before it).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include "nordend/core/worker_pool.hpp"
#include "nordend/fs/measure.hpp"

int main(int argc, char** argv) {
    try {
        int runs = 3;
        std::vector<std::string> folders;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--runs" && i + 1 < argc) {
                runs = std::atoi(argv[++i]);
            } else {
                folders.emplace_back(arg);
            }
        }
        if (folders.empty()) {
            std::fputs("usage: nordend-bench-size [--runs N] DIR...\n", stderr);
            return 2;
        }
        for (int run = 1; run <= runs; ++run) {
            for (const std::string& folder : folders) {
                int told = 0;
                const nordend::core::CancelToken token;
                const auto start = std::chrono::steady_clock::now();
                const nordend::fs::Tally tally = nordend::fs::measure(
                    folder, {}, token, [&told](const nordend::fs::Tally& /*tally*/) { ++told; });
                const double ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - start)
                                      .count();
                std::printf(
                    "run %d  %s  %9.1f ms  %9llu files  %7llu folders  %15llu bytes  "
                    "unreadable %llu  not entered %llu%s  progress told %d\n",
                    run, folder.c_str(), ms, static_cast<unsigned long long>(tally.files),
                    static_cast<unsigned long long>(tally.folders),
                    static_cast<unsigned long long>(tally.bytes),
                    static_cast<unsigned long long>(tally.unreadable),
                    static_cast<unsigned long long>(tally.not_entered),
                    tally.remote ? "  remote: not walked" : "", told);
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "nordend-bench-size: %s\n", e.what());
        return 1;
    }
}
