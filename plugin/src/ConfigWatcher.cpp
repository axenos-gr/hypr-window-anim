#include "ConfigWatcher.hpp"
#include "Plugin.hpp"

#include "hwa/Paths.hpp"

#include <hyprland/src/Compositor.hpp>

#include <sys/inotify.h>
#include <unistd.h>

#include <format>

namespace hwa::plugin::watcher {
    namespace {
        int                 g_fd     = -1;
        wl_event_source*    g_source = nullptr;
        SP<CEventLoopTimer> g_debounce;

        void                watch(const std::filesystem::path& dir) {
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec))
                return;
            if (inotify_add_watch(g_fd, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE) < 0)
                logError(std::format("inotify on {} failed", dir.string()));
        }

        int onReadable(int fd, uint32_t, void*) {
            alignas(inotify_event) char buf[4096];
            while (read(fd, buf, sizeof(buf)) > 0) {
                // any change reloads
            }

            if (g_unloading)
                return 0;

            // editors write in several steps
            cancelTimer(g_debounce);
            g_debounce = addTimer(std::chrono::milliseconds{200}, [] {
                g_debounce.reset();
                reloadConfig(true);
            });
            return 0;
        }
    }

    void start() {
        stop();

        g_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (g_fd < 0) {
            logError("inotify_init1 failed, config hot-reload disabled");
            return;
        }

        watch(configDir());
        watch(userEffectsDir());

        g_source = wl_event_loop_add_fd(g_pCompositor->m_wlEventLoop, g_fd, WL_EVENT_READABLE, onReadable, nullptr);
    }

    void stop() {
        cancelTimer(g_debounce);
        if (g_source) {
            wl_event_source_remove(g_source);
            g_source = nullptr;
        }
        if (g_fd >= 0) {
            close(g_fd);
            g_fd = -1;
        }
    }
}
