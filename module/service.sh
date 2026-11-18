#!/system/bin/sh
MODDIR=${0%/*}
[ -e "$MODDIR/skip_mount" ] && exit 0
until [ "$(getprop sys.boot_completed)" = 1 ]; do sleep 2; done
if [ ! -x /data/misc/skia-motion-blur/runtime/surfaceflinger ] ||
   ! cmp -s "$MODDIR/system/bin/surfaceflinger" /system/bin/surfaceflinger; then
  echo "Module system files are not mounted. Enable system module mounting in your root manager; KernelSU/APatch may require a mounting metamodule. Reboot afterward." > "$MODDIR/boot-error.txt"
  exit 1
fi
# Shared storage may become available only after the user unlocks the device.
until [ -d /sdcard/Android ]; do sleep 2; done
if [ ! -e /sdcard/skia-motion-blur.prop ]; then
    cp "$MODDIR/skia-motion-blur.prop" /sdcard/skia-motion-blur.prop
fi
cleanup() {
    # Removing the lease stops new blur work while the controller shuts down.
  rm -f /data/misc/skia-motion-blur/skia-motion-blur.active
  [ -n "$CHILD" ] && kill -TERM "$CHILD" 2>/dev/null
  [ -n "$CHILD" ] && wait "$CHILD" 2>/dev/null
  return 0
}
trap 'cleanup; exit 0' TERM INT HUP
while [ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ]; do
  "$MODDIR/controller" "$MODDIR" >> "$MODDIR/controller.log" 2>&1 &
  CHILD=$!
  wait "$CHILD"
  CHILD=
  sleep 1
done
cleanup
