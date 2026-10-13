#include "Commands.hpp"
#include "HyprSocket.hpp"

#include "hwa/Assets.hpp"
#include "hwa/Config.hpp"
#include "hwa/Effect.hpp"
#include "hwa/Paths.hpp"
#include "hwa/ShaderAssembly.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <print>
#include <set>
#include <sstream>

namespace hwa::cli {
    namespace {
        constexpr std::string_view PLUGIN_NAME = "hypr-window-anim";

        std::filesystem::path selfDir() {
            std::error_code ec;
            auto            exe = std::filesystem::read_symlink("/proc/self/exe", ec);
            return ec ? std::filesystem::path{} : exe.parent_path();
        }

        std::filesystem::path findPlugin(const SOptions& o) {
            if (!o.pluginPath.empty())
                return std::filesystem::absolute(o.pluginPath);
            if (const char* env = std::getenv("HWA_PLUGIN"); env && *env)
                return std::filesystem::absolute(env);

            std::error_code ec;
            const auto      dir = selfDir();
            for (const auto& cand : {dir / "hypr-window-anim.so", dir / ".." / "lib" / "hypr-window-anim" / "hypr-window-anim.so"}) {
                if (std::filesystem::exists(cand, ec))
                    return std::filesystem::weakly_canonical(cand, ec);
            }
            return HWA_PLUGIN_PATH;
        }

        std::string trim(std::string s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
                s.pop_back();
            size_t i = 0;
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
                ++i;
            return s.substr(i);
        }

        std::optional<std::string> readText(const std::filesystem::path& p) {
            std::ifstream in{p};
            if (!in)
                return std::nullopt;
            std::stringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        std::expected<SInstance, std::string> instanceOrPrint(const SOptions& o) {
            auto inst = resolveInstance(o.instance);
            if (!inst)
                std::println(stderr, "error: {}", inst.error());
            return inst;
        }

        bool pluginLoaded(const SInstance& inst) {
            auto list = request(inst, "j/plugin list");
            return list && list->find(std::format("\"name\": \"{}\"", PLUGIN_NAME)) != std::string::npos;
        }

        struct SAbiCheck {
            std::string built;
            std::string running;
            bool        ok = false;
        };

        std::expected<SAbiCheck, std::string> checkAbi(const SInstance& inst, const std::filesystem::path& plugin) {
            const auto sidecar = std::filesystem::path{plugin.string() + ".abi"};
            auto       built   = readText(sidecar);
            if (!built)
                return std::unexpected(std::format("missing ABI file {} (rebuild the plugin)", sidecar.string()));

            auto ver = request(inst, "j/version");
            if (!ver)
                return std::unexpected(ver.error());
            auto running = jsonString(*ver, "abiHash");
            if (!running)
                return std::unexpected("Hyprland did not report abiHash (too old?)");

            SAbiCheck r{.built = trim(*built), .running = *running};
            r.ok = r.built == r.running;
            return r;
        }

        int printReply(const std::expected<std::string, std::string>& reply) {
            if (!reply) {
                std::println(stderr, "error: {}", reply.error());
                return 1;
            }
            if (*reply == "unknown request") {
                std::println(stderr, "error: plugin is not loaded in this instance (run: hypr-window-anim load)");
                return 1;
            }
            std::print("{}", *reply);
            if (!reply->ends_with('\n'))
                std::println("");
            return reply->starts_with("error") ? 1 : 0;
        }

        // "" when clean
        std::expected<std::string, std::string> validateOffline(const std::string& source) {
            char tmpl[] = "/tmp/hwa-check-XXXXXX.frag";
            int  fd     = mkstemps(tmpl, 5);
            if (fd < 0)
                return std::unexpected("cannot create temp file");
            if (write(fd, source.data(), source.size()) != static_cast<ssize_t>(source.size())) {
                close(fd);
                unlink(tmpl);
                return std::unexpected("cannot write temp file");
            }
            close(fd);

            FILE* p = popen(std::format("glslangValidator '{}' 2>&1", tmpl).c_str(), "r");
            if (!p) {
                unlink(tmpl);
                return std::unexpected("cannot run glslangValidator");
            }
            std::string out;
            char        buf[4096];
            while (fgets(buf, sizeof(buf), p))
                out += buf;
            const int rc = pclose(p);
            unlink(tmpl);

            if (rc == 0)
                return std::string{};

            // drop the echoed temp path line
            std::stringstream in{out};
            std::string       line, diag;
            while (std::getline(in, line)) {
                if (line.find(tmpl) != std::string::npos || line.empty())
                    continue;
                diag += line + "\n";
            }
            return diag.empty() ? out : diag;
        }
    }

    int cmdInit(const SOptions& o) {
        const auto      file = configFile();
        std::error_code ec;
        std::filesystem::create_directories(userEffectsDir(), ec);
        if (ec) {
            std::println(stderr, "error: cannot create {}: {}", userEffectsDir().string(), ec.message());
            return 1;
        }

        if (std::filesystem::exists(file, ec) && !o.force) {
            std::println("{} already exists (use --force to overwrite)", file.string());
            return 0;
        }

        std::ofstream out{file};
        out << assets::find("config/default.toml").value_or("");
        if (!out) {
            std::println(stderr, "error: cannot write {}", file.string());
            return 1;
        }
        std::println("wrote {}\ncustom effects go to {}", file.string(), userEffectsDir().string());
        return 0;
    }

    int cmdLoad(const SOptions& o) {
        auto inst = instanceOrPrint(o);
        if (!inst)
            return 1;

        const auto      plugin = findPlugin(o);
        std::error_code ec;
        if (!std::filesystem::exists(plugin, ec)) {
            std::println(stderr, "error: plugin not found at {}", plugin.string());
            return 1;
        }

        std::println("instance: {}\nplugin:   {}", inst->signature, plugin.string());
        std::fflush(stdout); // keep order with stderr diagnostics

        auto abi = checkAbi(*inst, plugin);
        if (!abi) {
            std::println(stderr, "error: {}", abi.error());
            return 1;
        }
        if (!abi->ok) {
            std::println(stderr, "refusing to load: plugin was built for\n  {}\nbut Hyprland is\n  {}\nrebuild the plugin against the installed Hyprland headers.", abi->built,
                         abi->running);
            return 2;
        }

        if (pluginLoaded(*inst)) {
            std::println("already loaded");
            return 0;
        }

        auto reply = request(*inst, std::format("plugin load {}", plugin.string()));
        if (!reply || trim(*reply) != "ok") {
            std::println(stderr, "error: load failed: {}", reply ? *reply : reply.error());
            return 1;
        }

        if (!pluginLoaded(*inst)) {
            std::println(stderr, "error: Hyprland rejected the plugin, see hyprland.log");
            return 1;
        }

        std::println("loaded");
        return 0;
    }

    int cmdUnload(const SOptions& o) {
        auto inst = instanceOrPrint(o);
        if (!inst)
            return 1;

        auto info = request(*inst, "j/hwanim version");
        if (!info || *info == "unknown request") {
            std::println("not loaded");
            return 0;
        }

        auto path = jsonString(*info, "path");
        if (!path) {
            std::println(stderr, "error: plugin did not report its path");
            return 1;
        }

        auto reply = request(*inst, std::format("plugin unload {}", *path));
        return printReply(reply);
    }

    int cmdRestart(const SOptions& o) {
        if (const int rc = cmdUnload(o); rc != 0)
            return rc;
        return cmdLoad(o);
    }

    int cmdStatus(const SOptions& o) {
        const int rc = cmdPassthrough(o, "status");
        if (rc != 0)
            return rc;

        auto inst = resolveInstance(o.instance);
        auto info = inst ? request(*inst, "j/hwanim version") : std::unexpected(std::string{});
        if (!info)
            return rc;

        const auto path = jsonString(*info, "path");
        const auto pos  = info->find("\"loadedMtime\": ");
        if (!path || pos == std::string::npos)
            return rc;

        const auto  loaded = std::stoll(info->substr(pos + 15));
        struct stat st{};
        if (stat(path->c_str(), &st) == 0 && st.st_mtime > loaded)
            std::println("note: a newer build is installed at {}; run: hypr-window-anim restart", *path);
        return rc;
    }

    int cmdPassthrough(const SOptions& o, const std::string& sub) {
        auto inst = instanceOrPrint(o);
        if (!inst)
            return 1;

        std::string req = std::format("hwanim {}", sub);
        for (const auto& a : o.args)
            req += " " + a;
        return printReply(request(*inst, req));
    }

    int cmdCheck(const SOptions& o) {
        // Online: plugin's config dir and real driver
        if (!o.offline && !o.all) {
            if (auto inst = resolveInstance(o.instance); inst && pluginLoaded(*inst)) {
                std::string req = "hwanim check";
                for (const auto& a : o.args)
                    req += " " + a;
                auto reply = request(*inst, req);
                if (!reply) {
                    std::println(stderr, "error: {}", reply.error());
                    return 1;
                }
                const bool ok = reply->starts_with("ok");
                std::print("{}", reply->substr(reply->find('\n') + 1));
                return ok ? 0 : 1;
            }
            std::println("note: plugin not reachable, validating offline with glslangValidator");
        }

        int  failures = 0;
        auto cfg      = loadConfigFile(configFile());
        if (!cfg) {
            std::println("config: ERROR {}", cfg.error());
            ++failures;
        } else {
            std::println("config: ok ({})", configFile().string());
            for (const auto& w : cfg->warnings)
                std::println("  warning: {}", w);
        }

        std::set<std::string> names{o.args.begin(), o.args.end()};
        if (o.all) {
            for (const auto& e : listEffects())
                names.insert(e.name);
        } else if (names.empty() && cfg) {
            names = configuredEffects(*cfg);
            for (const auto& [n, _] : cfg->effects)
                names.insert(n);
        }

        for (const auto& name : names) {
            auto src = loadEffect(name);
            if (!src) {
                std::println("effect {}: ERROR {}", name, src.error());
                ++failures;
                continue;
            }

            auto diag = validateOffline(assembleFragment(src->body));
            if (!diag) {
                std::println("effect {}: ERROR {}", name, diag.error());
                ++failures;
            } else if (!diag->empty()) {
                std::println("effect {} ({}): ERROR (source 1 = effect file, 2 = prelude, 3 = bmw-common)\n{}", name, src->origin, *diag);
                ++failures;
            } else
                std::println("effect {} ({}): ok", name, src->origin);
        }

        return failures ? 1 : 0;
    }

    int cmdDoctor(const SOptions& o) {
        int  problems = 0;
        auto report   = [&](bool ok, const std::string& what) {
            std::println("[{}] {}", ok ? " ok " : "FAIL", what);
            problems += ok ? 0 : 1;
        };

        report(std::system("command -v nm >/dev/null 2>&1") == 0, "nm available (Hyprland needs it to resolve hooks)");
        report(std::system("command -v glslangValidator >/dev/null 2>&1") == 0, "glslangValidator available (offline shader check)");

        const auto      plugin = findPlugin(o);
        std::error_code ec;
        report(std::filesystem::exists(plugin, ec), std::format("plugin at {}", plugin.string()));

        auto cfg = loadConfigFile(configFile());
        report(cfg.has_value(), std::format("config {}{}", configFile().string(), cfg ? "" : ": " + cfg.error()));

        auto inst = resolveInstance(o.instance);
        report(inst.has_value(), inst ? std::format("Hyprland instance {}", inst->signature) : inst.error());
        if (inst) {
            auto abi = checkAbi(*inst, plugin);
            report(abi && abi->ok, abi ? std::format("ABI built={} running={}", abi->built, abi->running) : abi.error());
            std::println("[info] plugin {}loaded", pluginLoaded(*inst) ? "" : "not ");
        }

        return problems ? 1 : 0;
    }

    int cmdInstances(const SOptions&) {
        const char* current = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
        for (const auto& sig : listInstances())
            std::println("{}{}", sig, current && sig == current ? "  (current)" : "");
        return 0;
    }
}
