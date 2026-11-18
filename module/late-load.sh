#!/system/bin/sh
MODDIR=${0%/*}
# SurfaceFlinger must use the prepared executable from early boot.
touch "$MODDIR/skip_mount" "$MODDIR/disable"
echo "Late-load mode is unsupported. Use normal early boot, reinstall the module and reboot." > "$MODDIR/boot-error.txt"
exit 1
