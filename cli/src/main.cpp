#include "Commands.hpp"

#include <print>
#include <string_view>

using namespace hwa::cli;

static constexpr std::string_view USAGE = R"(usage: hypr-window-anim [--instance SIG] <command> [args]

commands:
  init [--force]               write ~/.config/hypr-window-anim/config.toml
  load [--plugin PATH]         check the ABI, then load the plugin into Hyprland
  unload                       unload the plugin
  restart                      unload + load, e.g. after installing a new build
  reload                       reload config and shaders
  status                       plugin state and active effects
  list                         available effects
  preview [effect]             play an effect on the active window without closing it
  check [--offline] [--all] [effect...]
                               validate config and shaders
  doctor                       diagnose the setup
  instances                    list running Hyprland instances
  version

--instance SIG targets a specific Hyprland (e.g. a nested test session);
default is $HYPRLAND_INSTANCE_SIGNATURE.
)";

int main(int argc, char** argv) {
    SOptions         opts;
    std::string_view command;

    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if ((a == "--instance" || a == "-i") && i + 1 < argc)
            opts.instance = argv[++i];
        else if (a == "--plugin" && i + 1 < argc)
            opts.pluginPath = argv[++i];
        else if (a == "--force")
            opts.force = true;
        else if (a == "--offline")
            opts.offline = true;
        else if (a == "--all")
            opts.all = true;
        else if (a == "-h" || a == "--help") {
            std::print("{}", USAGE);
            return 0;
        } else if (command.empty())
            command = a;
        else
            opts.args.emplace_back(a);
    }

    if (command == "init")
        return cmdInit(opts);
    if (command == "load")
        return cmdLoad(opts);
    if (command == "unload")
        return cmdUnload(opts);
    if (command == "restart")
        return cmdRestart(opts);
    if (command == "status")
        return cmdStatus(opts);
    if (command == "reload" || command == "list" || command == "preview")
        return cmdPassthrough(opts, std::string{command});
    if (command == "check")
        return cmdCheck(opts);
    if (command == "doctor")
        return cmdDoctor(opts);
    if (command == "instances")
        return cmdInstances(opts);
    if (command == "version") {
        std::println("hypr-window-anim {}", HWA_VERSION);
        return 0;
    }

    std::print(stderr, "{}", USAGE);
    return command.empty() ? 0 : 1;
}
