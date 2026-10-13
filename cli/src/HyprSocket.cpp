#include "HyprSocket.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>

namespace hwa::cli {
    std::filesystem::path runtimeHyprDir() {
        const char* xdg = std::getenv("XDG_RUNTIME_DIR");
        return std::filesystem::path{xdg && *xdg ? xdg : std::format("/run/user/{}", getuid())} / "hypr";
    }

    std::vector<std::string> listInstances() {
        std::vector<std::string> out;
        std::error_code          ec;
        for (const auto& e : std::filesystem::directory_iterator(runtimeHyprDir(), ec)) {
            if (std::filesystem::exists(e.path() / ".socket.sock", ec))
                out.push_back(e.path().filename().string());
        }
        std::ranges::sort(out);
        return out;
    }

    std::expected<SInstance, std::string> resolveInstance(const std::string& explicitSignature) {
        std::string sig = explicitSignature;
        if (sig.empty()) {
            const char* env = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
            if (!env || !*env)
                return std::unexpected("HYPRLAND_INSTANCE_SIGNATURE is not set; pass --instance <signature>");
            sig = env;
        }

        if (sig.find('/') != std::string::npos)
            return std::unexpected(std::format("invalid instance signature '{}'", sig));

        SInstance inst{.signature = sig, .socket = runtimeHyprDir() / sig / ".socket.sock"};
        std::error_code ec;
        if (!std::filesystem::exists(inst.socket, ec))
            return std::unexpected(std::format("no Hyprland socket at {}", inst.socket.string()));
        return inst;
    }

    std::expected<std::string, std::string> request(const SInstance& inst, const std::string& req) {
        const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0)
            return std::unexpected(std::format("socket(): {}", std::strerror(errno)));

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        const auto path = inst.socket.string();
        if (path.size() >= sizeof(addr.sun_path)) {
            close(fd);
            return std::unexpected("socket path too long");
        }
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            const auto err = std::format("connect({}): {}", path, std::strerror(errno));
            close(fd);
            return std::unexpected(err);
        }

        timeval tv{.tv_sec = 10, .tv_usec = 0}; // plugin load runs nm
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (write(fd, req.data(), req.size()) != static_cast<ssize_t>(req.size())) {
            close(fd);
            return std::unexpected("short write to Hyprland socket");
        }

        std::string reply;
        char        buf[8192];
        while (true) {
            const auto n = read(fd, buf, sizeof(buf));
            if (n < 0) {
                const auto err = std::format("read: {}", std::strerror(errno));
                close(fd);
                return std::unexpected(err);
            }
            if (n == 0)
                break;
            reply.append(buf, n);
        }
        close(fd);
        return reply;
    }

    std::optional<std::string> jsonString(const std::string& json, const std::string& key) {
        const auto needle = std::format("\"{}\"", key);
        auto       pos    = json.find(needle);
        if (pos == std::string::npos)
            return std::nullopt;
        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos)
            return std::nullopt;
        pos = json.find('"', pos);
        if (pos == std::string::npos)
            return std::nullopt;

        std::string out;
        for (++pos; pos < json.size() && json[pos] != '"'; ++pos) {
            if (json[pos] == '\\' && pos + 1 < json.size())
                ++pos;
            out += json[pos];
        }
        return out;
    }
}
