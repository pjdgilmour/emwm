#!/bin/sh
# Each invocation uses a fresh X server and an isolated WM configuration.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
xpid=
cmpid=
wmpid=
cleanup() {
    [ -z "$cmpid" ] || kill "$cmpid" 2>/dev/null || :
    [ -z "$wmpid" ] || kill "$wmpid" 2>/dev/null || :
    [ -z "$xpid" ] || kill "$xpid" 2>/dev/null || :
    if [ "${1:-0}" != 0 ]; then tail -60 "$work/wm.log" "$work/x.log" "$work/picom.log" 2>/dev/null || :; fi
    rm -rf "$work"
}
trap 'cleanup $?' EXIT
trap 'exit 1' HUP INT TERM
${CC:-cc} ${CPPFLAGS:-} ${CFLAGS:--O2 -Wall -Wextra} tests/close-button.c -o "$work/test" ${LDFLAGS:-} -lX11 -lXtst -lXrender -lXcomposite
${XVFB:-Xvfb} -displayfd 3 -screen 0 1024x768x24 -nolisten tcp 3>"$work/display" >"$work/x.log" 2>&1 &
xpid=$!
i=0
while [ ! -s "$work/display" ]; do
    i=$((i + 1))
    [ "$i" -lt 50 ] || exit 1
    sleep .1
done
DISPLAY=:$(cat "$work/display")
export DISPLAY
# Avoid inheriting the user's resource file or session manager.
unset SESSION_MANAGER
XENVIRONMENT="$work/resources"
export XENVIRONMENT
if [ "${XFT_TITLE:-0}" = 1 ]; then
    cp src/Emwm.ad "$XENVIRONMENT"
else
    : >"$XENVIRONMENT"
fi
cat >>"$XENVIRONMENT" <<RESOURCES
Emwm*clientAutoPlace: False
Emwm*interactivePlacement: False
Emwm*keyboardFocusPolicy: explicit
Emwm*focusAutoRaise: False
Emwm*useIconBox: False
Emwm*workspaceCount: 1
Emwm*frameStyle: ${FRAME_STYLE:-recessed}
Emwm*matteWidth: ${MATTE_WIDTH:-0}
Emwm*client*background: #8c8c8c
Emwm*client*activeBackground: #7399ba
RESOURCES
if [ "${DECOUPLED_TITLE:-0}" = 1 ]; then
    echo 'Emwm*client*title*activeBackground: #6989a5' >>"$XENVIRONMENT"
fi
${EMWM_BINARY:-./src/emwm} -display "$DISPLAY" -xrm "Emwm*configFile: $PWD/src/system.emwmrc" >"$work/wm.log" 2>&1 &
wmpid=$!
sleep 1
kill -0 "$wmpid"
if [ -n "${ARGB_CLIENTS:-}" ]; then
    cat >"$work/picom.conf" <<CONFIG
backend = "xrender";
vsync = false;
shadow = false;
fading = false;
frame-opacity = 1.0;
corner-radius = 0;
blur-method = "none";
unredir-if-possible = false;
CONFIG
    ${PICOM:-picom} --config "$work/picom.conf" >"$work/picom.log" 2>&1 &
    cmpid=$!
    sleep 1
    kill -0 "$cmpid"
fi
if [ -n "${EMWM_TEST_CAPTURE:-}" ]; then
    (sleep 2; import -display "$DISPLAY" -window root "$EMWM_TEST_CAPTURE") &
fi
"$work/test"
kill -0 "$wmpid"
