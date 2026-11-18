#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <utils.h>
#include <sys/system_properties.h>

#define MB_STOCK_PATH "/data/misc/skia-motion-blur/runtime/surfaceflinger"
#define MB_PAYLOAD_PATH "/data/misc/skia-motion-blur/runtime/libskiamotionblur.so"
#define MB_COUNTER_PATH "/data/misc/skia-motion-blur/skia-motion-blur.launches"
#define MB_OWNED_PATH "/data/misc/skia-motion-blur/skia-motion-blur.owned"
#define MB_LEASE_PATH "/data/misc/skia-motion-blur/skia-motion-blur.active"

static inline int mb_sdk(void) {
    char value[PROP_VALUE_MAX] = {0};
    __system_property_get("ro.build.version.sdk", value);
    int number = 0;
    sscanf(value, "%d", &number);
    return number;
}

static inline const char *mb_strategy(int version) {
    if (version >= 31 && version <= 33) return "egl-khr";
    if (version >= 34 && version <= 36) return "egl-khr-core";
    return "unsupported";
}

static inline bool mb_supported(int version) { return version >= 31 && version <= 36; }

static inline uint64_t mb_lease(void) {
    FILE *file = fopen(MB_LEASE_PATH, "re");
    if (!file) return 0;
    unsigned long long deadline = 0, session = 0;
    int count = fscanf(file, "%llu %llu", &deadline, &session);
    fclose(file);
    uint64_t now = mb_millis();
    if (count < 2 || deadline <= now || deadline > now + 2000) return 0;
    return session;
}
