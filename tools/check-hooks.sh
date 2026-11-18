#!/usr/bin/env bash
# Run after every Hyprland update: the plugin hooks these exact symbols.
set -euo pipefail

BIN="${1:-/usr/bin/Hyprland}"
SYMBOLS=(
    _ZN7Desktop4View7CWindow9sendCloseEv
    _ZN7Desktop14CWindowFadeout6createEN9Hyprutils6Memory14CSharedPointerINS_4View7CWindowEEENS3_IN6Render12IFramebufferEEEfS9_
    _ZN6Render13IHyprRenderer14renderFadeoutsERNS_14CRenderContextEN9Hyprutils6Memory14CSharedPointerIN7Monitor8CMonitorEEEN7Desktop13eFadeoutPlaneENS5_IN9Workspace12CHLWorkspaceEEENS_10eSceneModeE
)

status=0
DYN=$(nm -D -j "$BIN")
DISASM=$(objdump -d --no-show-raw-insn "$BIN")

for s in "${SYMBOLS[@]}"; do
    name=$(c++filt "$s")
    if ! grep -qx "$s" <<<"$DYN"; then
        echo "MISSING  $name"
        status=1
        continue
    fi
    calls=$(grep -cE "(call|jmp) +[0-9a-f]+ <$s(@@Base)?>$" <<<"$DISASM" || true)
    if [ "$calls" -eq 0 ]; then
        echo "UNCALLED $name (inlined everywhere? the hook would never fire)"
        status=1
    else
        echo "ok       $name ($calls call sites)"
    fi
done

exit $status
