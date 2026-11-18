mb_extract_surfaceflinger() {
    for MB_STOCK_PID in $(pidof surfaceflinger); do
        case "$MB_STOCK_PID" in
            ''|*[!0-9]*) continue ;;
        esac
        MB_STOCK_PROC=/proc/$MB_STOCK_PID
        # Verify the service identity before opening its executable.
        MB_STOCK_UID=$(awk '$1 == "Uid:" { print $2 }' "$MB_STOCK_PROC/status" 2>/dev/null)
        [ "$MB_STOCK_UID" = 1000 ] || continue
        MB_STOCK_CMD=$(tr '\000' '\n' < "$MB_STOCK_PROC/cmdline" 2>/dev/null | head -n 1)
        [ "$MB_STOCK_CMD" = /system/bin/surfaceflinger ] || continue
        MB_STOCK_EXECUTABLE=$(readlink "$MB_STOCK_PROC/exe") || continue
        # Opening /proc/PID/exe reads the running inode through system overlays.
        if cp -L "$MB_STOCK_PROC/exe" "$1.tmp" &&
           cmp -s "$MB_STOCK_PROC/exe" "$1.tmp"; then
            if mv -f "$1.tmp" "$1"; then
                MB_STOCK_SOURCE=$MB_STOCK_PROC/exe
                return 0
            fi
        fi
        rm -f "$1.tmp"
    done
    return 1
}
