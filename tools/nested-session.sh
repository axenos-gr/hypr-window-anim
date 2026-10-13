#!/usr/bin/env bash
# Opens a throwaway nested Hyprland in a window on workspace 10 of the host and prints its
# instance signature. Risky plugin tests go there, never into the host session.
#   HWA_CONFIG_DIR   config dir the nested plugin reads (default: tools/dev-config)
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CONF="$ROOT/tools/nested.conf"
CFGDIR="${HWA_CONFIG_DIR:-$ROOT/tools/dev-config}"
RUNDIR="$XDG_RUNTIME_DIR/hypr"

before=$(ls "$RUNDIR" 2>/dev/null || true)

# exec'd by the host so the window lands on workspace 10 without stealing focus
hyprctl dispatch exec "[workspace 10 silent] env HWA_CONFIG_DIR=$CFGDIR Hyprland -c $CONF" >/dev/null

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
