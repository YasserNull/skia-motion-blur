#!/system/bin/sh
MODDIR=${0%/*}
DATA=/data/misc/skia-motion-blur
fail() {
    # The root manager must skip the launcher if preparation fails.
  touch "$MODDIR/skip_mount" "$MODDIR/disable"
  echo "$1" > "$MODDIR/boot-error.txt"
  exit 1
}
verified() {
  EXPECTED=$(cat "$1") || return 1
  ACTUAL=$(sha256sum "$2") || return 1
  [ -n "$EXPECTED" ] && [ "${ACTUAL%% *}" = "$EXPECTED" ]
}
# Check firmware before preparing files or allowing the launcher mount.
verified "$MODDIR/stock.sha256" /system/bin/surfaceflinger || fail "Original SurfaceFlinger changed; reinstall module for this firmware"
verified "$MODDIR/stock.sha256" "$MODDIR/stock/surfaceflinger" || fail "Saved original compositor checksum mismatch; mount skipped"
verified "$MODDIR/payload.sha256" "$MODDIR/payload/libskiamotionblur.so" || fail "Payload checksum mismatch; mount skipped"
mkdir -p "$DATA/runtime" || fail "Cannot prepare runtime directory; mount skipped"
chown 0:1003 "$DATA" "$DATA/runtime" || fail "Cannot set runtime ownership; mount skipped"
chmod 0750 "$DATA" "$DATA/runtime" || fail "Cannot set runtime permissions; mount skipped"
chcon u:object_r:skia_motion_blur_data_file:s0 "$DATA" "$DATA/runtime" || fail "SELinux data type unavailable; mount skipped"
prepare() {
    # Publish each file only after its permissions and SELinux label are set.
  cp -f "$1" "$2.tmp" &&
  chown 0:1003 "$2.tmp" &&
  chmod "$3" "$2.tmp" &&
  chcon "$4" "$2.tmp" &&
  mv -f "$2.tmp" "$2"
}
prepare "$MODDIR/stock/surfaceflinger" "$DATA/runtime/surfaceflinger" 0550 u:object_r:surfaceflinger_exec:s0 || fail "Cannot prepare original compositor; mount skipped"
prepare "$MODDIR/payload/libskiamotionblur.so" "$DATA/runtime/libskiamotionblur.so" 0440 u:object_r:system_lib_file:s0 || fail "Cannot prepare payload; mount skipped"
verified "$MODDIR/stock.sha256" "$DATA/runtime/surfaceflinger" || fail "Prepared compositor checksum mismatch; mount skipped"
verified "$MODDIR/payload.sha256" "$DATA/runtime/libskiamotionblur.so" || fail "Prepared payload checksum mismatch; mount skipped"
rm -f "$DATA/skia-motion-blur.active" "$DATA/skia-motion-blur.owned"
echo 0 > "$DATA/skia-motion-blur.launches" || fail "Cannot initialize launch counter; mount skipped"
for FILE in skia-motion-blur.prop skia-motion-blur.launches skia-motion-blur.status skia-motion-blur.gles skia-motion-blur.touch; do
  touch "$DATA/$FILE" &&
  chown 1000:1003 "$DATA/$FILE" &&
  chmod 0644 "$DATA/$FILE" &&
  chcon u:object_r:skia_motion_blur_data_file:s0 "$DATA/$FILE" || fail "Cannot prepare control files; mount skipped"
done
: > "$DATA/skia-motion-blur.touch" || fail "Cannot reset touch activity"
# A loader failure here disables the module before the compositor is replaced.
env LD_PRELOAD="$DATA/runtime/libskiamotionblur.so" "$MODDIR/controller" --probe > "$MODDIR/loader-probe.txt" 2>&1 || fail "Payload loader probe failed; mount skipped"
sync || fail "Cannot persist prepared runtime; mount skipped"
