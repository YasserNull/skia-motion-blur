#pragma once

#include <android/log.h>
#include <renderengine/skia_motion_blur_config.h>

#define MB_LOG(enabled, priority, ...) do { \
    if (enabled) __android_log_print(priority, "SkiaMotionBlur", __VA_ARGS__); \
} while (0)

static inline bool mb_log_enabled(void) {
    const MbConfig *config = mb_config_acquire();
    bool enabled = config && config->log;
    mb_config_release(config);
    return enabled;
}
