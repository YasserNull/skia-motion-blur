#!/system/bin/sh
# Keep runtime executables available while the launcher can still be mounted.
rm -f /data/misc/skia-motion-blur/skia-motion-blur.active
if [ -f /data/misc/skia-motion-blur/skia-motion-blur.owned ]; then
  service call SurfaceFlinger 1008 i32 0 >/dev/null 2>&1
fi
rm -f /data/misc/skia-motion-blur/skia-motion-blur.launches /data/misc/skia-motion-blur/skia-motion-blur.status /data/misc/skia-motion-blur/skia-motion-blur.owned /data/misc/skia-motion-blur/skia-motion-blur.gles
