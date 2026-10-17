#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
xpid=; wmpid=
cleanup() {
    [ -z "$wmpid" ] || kill "$wmpid" 2>/dev/null || :
    [ -z "$xpid" ] || kill "$xpid" 2>/dev/null || :
    if [ "$1" != 0 ]; then cat "$work/wm.log" "$work/x.log" 2>/dev/null || :; fi
    rm -rf "$work"
}
trap 'cleanup $?' EXIT
trap 'exit 1' HUP INT TERM
${CC:-cc} -O2 -Wall -Wextra tests/tiling.c -o "$work/test" -lX11 -lXtst -lXrandr
${XVFB:-Xvfb} -displayfd 3 -screen 0 1025x769x24 -nolisten tcp -noreset 3>"$work/display" >"$work/x.log" 2>&1 &
xpid=$!
i=0
while [ ! -s "$work/display" ]; do i=$((i+1)); [ "$i" -lt 50 ] || exit 1; sleep .1; done
DISPLAY=:$(cat "$work/display")
HOME="$work/home"
mkdir -p "$HOME"
XENVIRONMENT="$work/resources"
export DISPLAY HOME XENVIRONMENT
unset SESSION_MANAGER
cat >"$work/rc" <<RC
Keys TilingTest
{
$(sed -n '/Super<Key>/p' src/system.emwmrc)
Super Ctrl<Key>Home window f.tile top-left
Super Ctrl<Key>Prior window f.tile top-right
Super Ctrl<Key>End window f.tile bottom-left
Super Ctrl<Key>Next window f.tile bottom-right
Super Ctrl<Key>r window f.tile restore
Alt<Key>F10 window f.maximize
Alt<Key>F5 window f.restore
Alt<Key>F9 window f.minimize
}
Keys NoTiling
{
Alt<Key>F5 window f.restore
}
RC
cat >"$XENVIRONMENT" <<RES
Emwm*configFile: ${TILE_CONFIG:-$work/rc}
Emwm*keyBindings: ${TILE_BINDINGS:-TilingTest}
Emwm*clientAutoPlace: False
Emwm*interactivePlacement: False
Emwm*keyboardFocusPolicy: explicit
Emwm*focusAutoRaise: False
Emwm*useIconBox: False
Emwm*workspaceCount: 1
Emwm*matteWidth: ${MATTE_WIDTH:-0}
Emwm*frameStyle: ${FRAME_STYLE:-recessed}
RES
${EMWM_BINARY:-./src/emwm} -display "$DISPLAY" >"$work/wm.log" 2>&1 &
wmpid=$!
sleep 1
kill -0 "$wmpid"
timeout 60 "$work/test"
kill -0 "$wmpid"
