#define _POSIX_C_SOURCE 200809L
#include <renderengine/skia_motion_blur_config.h>
#include <utils.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef SKIA_MOTION_BLUR_CONFIG_PATH
#define SKIA_MOTION_BLUR_CONFIG_PATH "/data/misc/skia-motion-blur/skia-motion-blur.prop"
#endif

static pthread_once_t identity_once = PTHREAD_ONCE_INIT;
static bool surface_flinger;

static void check_identity(void) {
    char path[4096];
    ssize_t size = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (size <= 0 || getuid() != 1000) return;
    path[size] = '\0';
    if (strcmp(path, "/system/bin/surfaceflinger") &&
        strcmp(path, "/data/misc/skia-motion-blur/runtime/surfaceflinger")) return;
    mb_read_text("/proc/self/cmdline", path, sizeof(path));
    surface_flinger = !strcmp(path, "/system/bin/surfaceflinger");
}

bool mb_is_surface_flinger(void) {
    pthread_once(&identity_once, check_identity);
    return surface_flinger;
}

typedef struct {
    MbConfig config;
    size_t references;
} MbConfigSnapshot;

static pthread_mutex_t config_mutex = PTHREAD_MUTEX_INITIALIZER;
// The current snapshot owns one reference; readers keep replaced snapshots alive.
static MbConfigSnapshot *current;
static uint64_t checked;
static char *previous;
static size_t previous_size;
static struct stat previous_stat;
static bool have_stat, stat_valid;

static void release_snapshot(MbConfigSnapshot *snapshot) {
    if (--snapshot->references == 0) {
        mb_config_destroy(&snapshot->config);
        free(snapshot);
    }
}

static void reload_snapshot(bool exists) {
    char *text = malloc(MB_CONFIG_MAX_TEXT + 2);
    if (!text) { stat_valid = false; return; }
    struct stat st = {0};
    FILE *file = fopen(SKIA_MOTION_BLUR_CONFIG_PATH, "re");
    size_t size = 0;
    bool read_ok = !exists;
    if (file) {
        size = fread(text, 1, MB_CONFIG_MAX_TEXT + 1, file);
        read_ok = !ferror(file) && fstat(fileno(file), &st) == 0;
        read_ok = fclose(file) == 0 && read_ok;
        exists = true;
    }
    if (!read_ok) size = 0;
    text[size] = '\0';
    bool installed = true, parsed = true;
    if (!stat_valid || !previous || size != previous_size || memcmp(text, previous, size)) {
        MbConfigSnapshot *next = calloc(1, sizeof(*next));
        installed = next != NULL;
        if (next) {
            mb_config_init(&next->config);
            parsed = mb_parse_config(text, size, &next->config);
            next->config.generation = current->config.generation + 1;
            next->references = 1;
            char *compact = realloc(text, size + 1);
            if (compact) text = compact;
            free(previous);
            previous = text;
            previous_size = size;
            text = NULL;
            release_snapshot(current);
            current = next;
        }
    }
    // Failed loads must leave unchanged files eligible for another read.
    stat_valid = read_ok && installed && parsed;
    if (stat_valid) {
        if (exists) previous_stat = st;
        have_stat = exists;
    }
    free(text);
}

const MbConfig *mb_config_acquire(void) {
    pthread_mutex_lock(&config_mutex);
    if (!current) {
        current = calloc(1, sizeof(*current));
        if (!current) { pthread_mutex_unlock(&config_mutex); return NULL; }
        mb_config_init(&current->config);
        current->references = 1;
    }
    uint64_t millis = mb_millis();
    if (mb_is_surface_flinger() && millis - checked >= 1000) {
        checked = millis;
        struct stat st;
        bool exists = stat(SKIA_MOTION_BLUR_CONFIG_PATH, &st) == 0;
        // Inode changes detect atomic replacement even if size and time match.
        bool changed = !stat_valid || exists != have_stat || (exists &&
            (st.st_dev != previous_stat.st_dev || st.st_ino != previous_stat.st_ino ||
             st.st_size != previous_stat.st_size || st.st_mtim.tv_sec != previous_stat.st_mtim.tv_sec ||
             st.st_mtim.tv_nsec != previous_stat.st_mtim.tv_nsec));
        if (changed) reload_snapshot(exists);
    }
    ++current->references;
    const MbConfig *result = &current->config;
    pthread_mutex_unlock(&config_mutex);
    return result;
}

void mb_config_release(const MbConfig *config) {
    if (!config) return;
    pthread_mutex_lock(&config_mutex);
    release_snapshot((MbConfigSnapshot *)config);
    pthread_mutex_unlock(&config_mutex);
}
