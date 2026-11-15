#include "nordend/portal/file_manager.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nordend/core/file.hpp"
#include "nordend/core/log.hpp"
#include "nordend/platform/launcher.hpp"

namespace nordend::portal {

std::vector<std::string> show_arguments(Show show, const std::vector<std::string>& uris) {
    std::vector<std::string> paths;
    for (const std::string& uri : uris) {
        if (const std::optional<std::filesystem::path> path = core::path_from_uri(uri)) {
            paths.push_back(path->string());
        }
    }
    if (paths.empty()) {
        return {};
    }
    std::vector<std::string> arguments;
    switch (show) {
        case Show::folders:
            break;
        case Show::items:
            arguments.emplace_back("--reveal");
            break;
        case Show::properties:
            arguments.emplace_back("--properties");
            break;
    }
    arguments.emplace_back("--");  // a path that starts with "-" is still a path
    arguments.insert(arguments.end(), paths.begin(), paths.end());
    return arguments;
}

struct FileManager::Impl {
    Impl(core::EventLoop& event_loop, platform::BusLoop& bus_loop, std::filesystem::path nordend)
        : loop{event_loop}, bus{bus_loop}, program{std::move(nordend)}, launcher{event_loop} {}

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    ~Impl() {
        if (idle_timer != 0) {
            loop.remove(idle_timer);
        }
    }

    core::EventLoop& loop;
    platform::BusLoop& bus;
    std::filesystem::path program;
    platform::Launcher launcher;
    std::unique_ptr<sdbus::IObject> object;
    std::size_t started{0};
    std::chrono::milliseconds idle_after{0};
    std::function<void()> idle;
    core::EventLoop::SourceId idle_timer{0};

    // (Re)starts the idle countdown.
    void rest() {
        if (idle_timer != 0) {
            loop.remove(idle_timer);
            idle_timer = 0;
        }
        if (idle && idle_after.count() > 0) {
            idle_timer = loop.add_timer(idle_after, std::chrono::milliseconds{0}, [this] {
                idle_timer = 0;
                idle();
            });
        }
    }

    // A call: nordend started with what it asks, the reply already on its way.
    void show(Show what, const char* method, const std::vector<std::string>& uris,
              const std::string& startup_id) {
        rest();
        std::vector<std::string> argv = show_arguments(what, uris);
        if (argv.empty()) {
            log::info("file manager: {} with no local file, nothing to show", method);
            return;
        }
        argv.insert(argv.begin(), program.string());
        std::vector<std::string> env;
        if (!startup_id.empty()) {
            env.push_back("XDG_ACTIVATION_TOKEN=" + startup_id);
            env.push_back("DESKTOP_STARTUP_ID=" + startup_id);
        }
        // After the method returns: the caller has its reply before anything is started.
        loop.post([this, argv = std::move(argv), env = std::move(env), method] {
            const std::error_code error =
                launcher.spawn(argv, {}, env, [](const platform::Launcher::Exit& /*exit*/) {});
            if (error) {
                log::error("file manager: {}: cannot start {}: {}", method, program.string(),
                           error.message());
                return;
            }
            ++started;
            log::info("file manager: {} ({} argument(s))", method, argv.size() - 1);
        });
    }
};

FileManager::FileManager(core::EventLoop& loop, sdbus::IConnection& connection,
                         platform::BusLoop& bus, std::filesystem::path program)
    : impl_{std::make_unique<Impl>(loop, bus, std::move(program))} {
    Impl& impl = *impl_;
    impl.object = sdbus::createObject(connection, sdbus::ObjectPath{std::string{kFileManagerPath}});
    const auto method = [&impl](const char* name, Show what) {
        return sdbus::registerMethod(name)
            .withInputParamNames("uris", "startup_id")
            .implementedAs([&impl, name, what](const std::vector<std::string>& uris,
                                               const std::string& startup_id) {
                impl.show(what, name, uris, startup_id);
            });
    };
    impl.object
        ->addVTable(method("ShowFolders", Show::folders), method("ShowItems", Show::items),
                    method("ShowItemProperties", Show::properties))
        .forInterface(std::string{kFileManagerInterface});
}

FileManager::~FileManager() = default;

void FileManager::set_idle_handler(std::chrono::milliseconds after, std::function<void()> idle) {
    impl_->idle_after = after;
    impl_->idle = std::move(idle);
    impl_->rest();
}

std::size_t FileManager::started() const noexcept { return impl_->started; }

}  // namespace nordend::portal
