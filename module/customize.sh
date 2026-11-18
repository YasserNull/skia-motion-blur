#!/system/bin/sh
[ "$BOOTMODE" = true ] || abort "Install from the root manager app"
MODULE_HOT_INSTALL_REQUEST=false
# Manager identity takes precedence over Magisk compatibility version values.
if [ "${APATCH:-}" = true ]; then
    ROOT_MANAGER=APatch
elif [ "${KSU:-}" = true ]; then
    ROOT_MANAGER=KernelSU
else
    case "${MAGISK_VER_CODE:-}" in
        ''|*[!0-9]*) abort "Install using Magisk 21+, KernelSU or APatch" ;;
    esac
    # The type declaration in sepolicy.rule requires Magisk 21's policy syntax.
    [ "$MAGISK_VER_CODE" -ge 21000 ] || abort "Magisk 21 or newer is required; install from the Magisk app"
    if [ "$MAGISK_VER_CODE" -lt 26000 ]; then
        ui_print "! Magisk below 26 may have SELinux or Magic Mount compatibility issues."
        ui_print "! Installing Magisk 26 or newer is recommended."
    fi
    ROOT_MANAGER=Magisk
fi
SDK=$(getprop ro.build.version.sdk)
[ "$SDK" -ge 31 ] && [ "$SDK" -le 36 ] || abort "Supported range: Android 12–16 / API 31–36"
STOCK="$MODPATH/stock/surfaceflinger"
mkdir -p "$MODPATH/stock" || abort "Cannot create compositor snapshot directory"
. "$MODPATH/extract.sh"
mb_extract_surfaceflinger "$STOCK" || abort "Cannot capture the running system SurfaceFlinger executable"
MARKER=$(head -c 4 "$STOCK" | od -An -tx1 | tr -d ' \n')
[ "$MARKER" = 7f454c46 ] || abort "SurfaceFlinger is not an ELF executable"
# The compositor's ELF ABI determines which payload can run in its process.
CLASS=$(dd if="$STOCK" bs=1 skip=4 count=1 2>/dev/null | od -An -tu1 | tr -d ' \n')
MACHINE=$(dd if="$STOCK" bs=1 skip=18 count=2 2>/dev/null | od -An -tu2 | tr -d ' \n')
case "$MACHINE:$CLASS" in
  183:2) ABI=arm64-v8a; LIB=lib64 ;;
  40:1) ABI=armeabi-v7a; LIB=lib ;;
  62:2) ABI=x86_64; LIB=lib64 ;;
  3:1) ABI=x86; LIB=lib ;;
  *) abort "Unsupported SurfaceFlinger ABI: $MACHINE/$CLASS" ;;
esac
if cmp -s "$STOCK" "$MODPATH/bin/$ABI/launcher"; then
    abort "Cannot use the module launcher as the system SurfaceFlinger"
fi

ui_print "- Skia Motion Blur by @YasserNull"

if [ "$ROOT_MANAGER" != Magisk ]; then
    ui_print "- Enable system module mounting; a mounting metamodule may be required"
fi
ui_print "- API $SDK, compositor ABI $ABI"
ui_print "- Captured running SurfaceFlinger from $MB_STOCK_SOURCE"
mkdir -p "$MODPATH/system/bin" "$MODPATH/stock" "$MODPATH/payload"
HASH=$(sha256sum "$STOCK") || abort "Cannot hash captured compositor"
echo "${HASH%% *}" > "$MODPATH/stock.sha256" || abort "Cannot save compositor checksum"
printf 'pid=%s\nsource=%s\nexecutable=%s\nsha256=%s\n' \
    "$MB_STOCK_PID" "$MB_STOCK_SOURCE" "$MB_STOCK_EXECUTABLE" "${HASH%% *}" \
    > "$MODPATH/stock-source.txt" || abort "Cannot save compositor source"
cp -f "$MODPATH/bin/$ABI/launcher" "$MODPATH/system/bin/surfaceflinger" || abort "Launcher missing"
cp -f "$MODPATH/bin/$ABI/libskiamotionblur.so" "$MODPATH/payload/libskiamotionblur.so" || abort "Payload missing"
HASH=$(sha256sum "$MODPATH/payload/libskiamotionblur.so")
echo "${HASH%% *}" > "$MODPATH/payload.sha256"
cp -f "$MODPATH/bin/$ABI/controller" "$MODPATH/controller" || abort "Controller missing"
rm -rf "$MODPATH/bin"
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/system/bin/surfaceflinger" 0 2000 0755 u:object_r:surfaceflinger_exec:s0
for FILE in controller service.sh post-fs-data.sh late-load.sh uninstall.sh; do
  set_perm "$MODPATH/$FILE" 0 0 0755
done
"$MODPATH/controller" --probe || abort "Selected ABI cannot execute on this device"
Experimental: reboot required; compositor integration not device-verified"
