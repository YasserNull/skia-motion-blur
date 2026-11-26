#!/system/bin/sh
# Run from a root shell with a scrollable page open in portrait.
set -eu
PKG=${1:-com.android.settings}
DATA=/data/misc/skia-motion-blur
CONFIG=$DATA/skia-motion-blur.prop
STATUS=$DATA/skia-motion-blur.status
GLES=$DATA/skia-motion-blur.gles

fail() { echo "$*" >&2; exit 1; }

field() {
    awk -F= -v key="$1" '{
        gsub(/^[ \t]+|[ \t\r]+$/, "", $1)
        if ($1 == key) { gsub(/^[ \t]+|[ \t\r]+$/, "", $2); value=$2 }
    } END { print value }' "$2"
}

[ -r "$STATUS" ] && [ -r "$CONFIG" ] || fail "Run as root after the module starts."
[ "$(field foreground "$STATUS")" = "$PKG" ] || fail "Open a scrollable page in $PKG and wait for the controller."
# Compare against the settings actually mirrored by the controller.
settings=$(cat "$CONFIG")
SOURCE=$(dirname "$0")/skia-motion-blur.prop
[ -r "$SOURCE" ] && [ "$(cat "$SOURCE")" = "$settings" ] ||
    fail "The controller has not loaded the current module settings yet."
ENABLED=$(field enabled "$CONFIG")
case "$ENABLED" in
    1|true) ENABLED=1 ;;
    *) ENABLED=0 ;;
esac
case "$(field debug "$CONFIG")" in 1|true) fail "Use debug=0 to measure normal rendering." ;; esac
SIZE=$(wm size | sed -n 's/.*size: \([0-9][0-9]*x[0-9][0-9]*\).*/\1/p' | tail -n 1)
[ -n "$SIZE" ] || fail "Display size unavailable."
W=${SIZE%x*}
H=${SIZE#*x}
X=$((W / 2))
Y1=$((H / 4))
Y2=$((H * 3 / 4))

scroll_pair() {
    input swipe "$X" "$Y2" "$X" "$Y1" 450
    sleep 0.2
    input swipe "$X" "$Y1" "$X" "$Y2" 450
    sleep 0.2
}

snapshot() {
    # Read the timestamp and selected GPU paths from the same frame.
    cat "$GLES" 2>/dev/null || :
}

before=$(snapshot)
scroll_pair
if [ "$ENABLED" = 1 ]; then
    warm=$(snapshot)
    [ "$warm" != "$before" ] && echo "$warm" | grep -q '^guide=' &&
        echo "$warm" | grep -q '^composite=' ||
        fail "No fresh GLES frame. Wait for live reload, then retry."
else
    [ ! -e "$DATA/skia-motion-blur.active" ] || fail "The effect is still active."
fi
dumpsys gfxinfo "$PKG" reset >/dev/null
i=0
while [ "$i" -lt 6 ]; do
    scroll_pair
    i=$((i + 1))
done
result=$(dumpsys gfxinfo "$PKG")
[ "$(field foreground "$STATUS")" = "$PKG" ] && [ "$(cat "$CONFIG")" = "$settings" ] ||
    fail "The foreground app or settings changed during this run."
if [ "$ENABLED" = 1 ]; then
    after=$(snapshot)
    [ "$after" != "$warm" ] && echo "$after" | grep -q '^guide=' &&
        echo "$after" | grep -q '^composite=' ||
        fail "No fresh GLES heartbeat during the run."
    echo "$after" | sed '1d'
else
    [ ! -e "$DATA/skia-motion-blur.active" ] || fail "The effect became active during this run."
    echo "enabled=0"
fi
echo "$result" | awk '
    /Total frames rendered:/ { if ($NF + 0 > 0) frames=1; print }
    /Janky frames:|50th percentile:|95th percentile:/ { print }
    END { if (!frames) { print "No app frame samples"; exit 1 } }'
