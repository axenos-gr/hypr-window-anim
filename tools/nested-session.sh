#!/usr/bin/env bash
# Opens a throwaway nested Hyprland in a window on workspace 10 of the host and prints its
# instance signature. Risky plugin tests go there, never into the host session.
#   HWA_CONFIG_DIR   config dir the nested plugin reads (default: tools/dev-config)
#   HWA_NESTED_CONF  Hyprland config for the nested session (default: tools/nested.lua)
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CONF="${HWA_NESTED_CONF:-$ROOT/tools/nested.lua}"
CFGDIR="${HWA_CONFIG_DIR:-$ROOT/tools/dev-config}"
RUNDIR="$XDG_RUNTIME_DIR/hypr"

before=$(ls "$RUNDIR" 2>/dev/null || true)

# exec'd by the host so the window lands on workspace 10 without stealing focus
CMD="env HWA_CONFIG_DIR=$CFGDIR Hyprland -c $CONF"
# Lua hosts (0.57+) take a Lua expression; hyprlang hosts the classic form
out=$(hyprctl dispatch "hl.dsp.exec_cmd(\"$CMD\", { workspace = \"10 silent\" })" 2>&1 || true)
if [ "$out" != "ok" ]; then
    hyprctl dispatch exec "[workspace 10 silent] $CMD" >/dev/null
fi

for _ in $(seq 1 100); do
    for d in $(ls "$RUNDIR"); do
        if ! grep -qx "$d" <<<"$before" && [ -S "$RUNDIR/$d/.socket.sock" ]; then
            echo "$d"
            exit 0
        fi
    done
    sleep 0.1
done

echo "nested Hyprland did not come up" >&2
exit 1
