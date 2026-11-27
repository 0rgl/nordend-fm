#include "nordend/core/file.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <fcntl.h>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace nordend::core {
namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

constexpr std::string_view kHex = "0123456789ABCDEF";

// GLib leaves these unescaped in the path of a file URI (g_filename_to_uri, checked with gio).
bool unescaped(unsigned char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return true;
    }
    return std::string_view{"!&'()*+,=:@$~_-./"}.find(static_cast<char>(c)) !=
           std::string_view::npos;
}

std::error_code last_error() { return {errno, std::generic_category()}; }

// folder and the folders above it, those missing made rwx------.
std::error_code make_folders(const std::filesystem::path& folder) {
    std::filesystem::path made;
    for (const std::filesystem::path& part : folder) {
        made /= part;
        struct stat info{};
        if (::stat(made.c_str(), &info) == 0) {
            if (!S_ISDIR(info.st_mode)) {
                return std::make_error_code(std::errc::not_a_directory);
            }
            continue;
        }
        if (errno != ENOENT) {
            return last_error();
        }
        if (::mkdir(made.c_str(), 0700) != 0) {
            // Made meanwhile is fine when it is a folder.
            if (errno != EEXIST || ::stat(made.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
                return errno == EEXIST ? std::make_error_code(std::errc::not_a_directory)
                                       : last_error();
            }
        }
    }
    return {};
}

// The link at path put back as it is, so that what watches its folder (another window, a GTK
// program watching its bookmarks) hears of a change made to its target in another folder. Done
// as a rename over it, never leaving it missing; when it cannot be, the change is just unheard.
void touch_link(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path target = std::filesystem::read_symlink(path, error);
    if (error) {
        return;
    }
    std::string temporary = (path.parent_path() / ("." + path.filename().string() + ".XXXXXX"));
    const int fd = ::mkostemp(temporary.data(), O_CLOEXEC);  // a free name, made a link next
    if (fd < 0) {
        return;
    }
    ::close(fd);
    if (::unlink(temporary.c_str()) != 0 || ::symlink(target.c_str(), temporary.c_str()) != 0) {
        return;
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        ::unlink(temporary.c_str());
    }
}

// The regular file open at fd, to its end (which may have moved since fstat), at most
// max_bytes.
TextFile read_open_file(int fd, std::size_t max_bytes) {
    TextFile result{.exists = true, .text = {}, .error = {}};
    struct stat info{};
    if (::fstat(fd, &info) != 0) {
        result.error = last_error().message();
        return result;
    }
    if (!S_ISREG(info.st_mode)) {
        result.error = "not a regular file";
        return result;
    }
    if (std::cmp_greater(info.st_size, max_bytes)) {
        result.error = std::format("larger than {} bytes", max_bytes);
        return result;
    }
    std::string text;
    text.reserve(static_cast<std::size_t>(info.st_size));
    std::array<char, 65536> chunk{};
    for (;;) {
        const ssize_t read = ::read(fd, chunk.data(), chunk.size());
        if (read < 0 && errno == EINTR) {
            continue;
        }
        if (read < 0) {
            result.error = "read error";
            return result;
        }
        if (read == 0) {
            break;
        }
        if (static_cast<std::size_t>(read) > max_bytes - text.size()) {
            result.error = std::format("larger than {} bytes", max_bytes);
            return result;
        }
        text.append(chunk.data(), static_cast<std::size_t>(read));
    }
    result.text = std::move(text);
    return result;
}

}  // namespace

std::error_code make_private_folders(const std::filesystem::path& folder) {
    return make_folders(folder);
}

std::string file_uri(const std::filesystem::path& absolute) {
    std::string uri = "file://";
    for (const char ch : absolute.native()) {
        const auto c = static_cast<unsigned char>(ch);
        if (unescaped(c)) {
            uri.push_back(ch);
        } else {
            uri.push_back('%');
            uri.push_back(kHex[c >> 4]);
            uri.push_back(kHex[c & 0x0F]);
        }
    }
    return uri;
}

std::error_code write_file_atomically(const std::filesystem::path& given, std::string_view text,
                                      const WriteOptions& options) {
    // A link's target is written, beside it the hidden file: renamed over the link, it would put
    // a file in the link's place.
    std::filesystem::path path = given;
    struct stat old{};
    const bool link = ::lstat(given.c_str(), &old) == 0 && S_ISLNK(old.st_mode);
    if (link && options.through_links) {
        std::error_code error;
        path = std::filesystem::canonical(given, error);
        if (error) {
            return error;
        }
    }
    if (const std::error_code error = make_folders(path.parent_path())) {
        return error;
    }
    mode_t mode = options.mode;
    if (options.private_file) {
        // Its folder too, when an older Nordend made it with the umask's mode; not a folder a link
        // leads into, which is someone else's choice. Tried only: the file is still worth saving.
        if (!link) {
            static_cast<void>(::chmod(path.parent_path().c_str(), 0700));
        }
    } else if ((!link || options.through_links) && ::stat(path.c_str(), &old) == 0) {
        mode = old.st_mode & 07777;
    }
    std::string temporary = (path.parent_path() / ("." + path.filename().string() + ".XXXXXX"));
    const int fd = ::mkostemp(temporary.data(), O_CLOEXEC);
    if (fd < 0) {
        return last_error();
    }
    const auto fail = [&](std::error_code why) {
        ::close(fd);
        ::unlink(temporary.c_str());
        return why;
    };
    std::size_t written = 0;
    while (written < text.size()) {
        const ssize_t put = ::write(fd, text.data() + written, text.size() - written);
        if (put < 0) {
            if (errno == EINTR) {
                continue;
            }
            return fail(last_error());
        }
        written += static_cast<std::size_t>(put);
    }
    if (::fchmod(fd, mode) != 0 || ::fsync(fd) != 0) {
        return fail(last_error());
    }
    if (::close(fd) != 0) {
        ::unlink(temporary.c_str());
        return last_error();
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        const std::error_code why = last_error();
        ::unlink(temporary.c_str());
        return why;
    }
    if (link && options.through_links) {
        touch_link(given);
    }
    return {};
}

TextFile read_text_file(const std::filesystem::path& path, std::size_t max_bytes) {
    // Opened once, and looked at through that descriptor: the name may be swapped for a FIFO
    // after any check by name, and O_NONBLOCK keeps opening one from waiting for a writer.
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
    if (fd < 0 && (errno == ENOENT || errno == ENOTDIR)) {
        return {};
    }
    if (fd < 0) {
        return {.exists = true, .text = {}, .error = "cannot open: " + last_error().message()};
    }
    TextFile result = read_open_file(fd, max_bytes);
    ::close(fd);
    return result;
}

std::optional<std::filesystem::path> path_from_uri(std::string_view uri) {
    constexpr std::string_view scheme = "file://";
    if (uri.size() < scheme.size() ||
        !std::ranges::equal(uri.substr(0, scheme.size()), scheme, [](char a, char b) {
            return (a >= 'A' && a <= 'Z' ? static_cast<char>(a - 'A' + 'a') : a) == b;
        })) {
        return std::nullopt;
    }
    uri.remove_prefix(scheme.size());
    const std::size_t slash = uri.find('/');
    if (slash == std::string_view::npos) {
        return std::nullopt;
    }
    if (const std::string_view host = uri.substr(0, slash); !host.empty() && host != "localhost") {
        return std::nullopt;  // another machine's file
    }
    uri.remove_prefix(slash);
    uri = uri.substr(0, uri.find_first_of("?#"));
    std::string path;
    path.reserve(uri.size());
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            const int high = hex_value(uri[i + 1]);
            const int low = hex_value(uri[i + 2]);
            if (high >= 0 && low >= 0) {
                path.push_back(static_cast<char>((high << 4U) | low));
                i += 2;
                continue;
            }
        }
        path.push_back(uri[i]);
    }
    if (path.find('\0') != std::string::npos) {
        return std::nullopt;
    }
    std::filesystem::path normal = std::filesystem::path{path}.lexically_normal();
    if (!normal.has_filename() && normal.has_relative_path()) {
        normal = normal.parent_path();  // "file:///a/b/": the folder b
    }
    return normal;
}

}  // namespace nordend::core
