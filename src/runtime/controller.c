#define _POSIX_C_SOURCE 200809L
#include "policy.h"
#include "focus.h"
#include "control.h"
#include "input_monitor.h"
#include "log.h"
#include <renderengine/skia_motion_blur_config.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static void stop(int signal) { (void)signal; running = 0; }

static bool same_package(const char *first, const char *second) {
    return !strcmp(first ? first : "", second ? second : "");
}

static bool config_check_due(uint64_t now, uint64_t deadline) {
    return now >= deadline;
}

static uint64_t focus_interval(const MbConfig *config, bool valid, bool mirrored) {
    return valid && mirrored && config->enabled && config->strength > 0 && config->packages.count ? 400 : 5000;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--probe")) {
        printf("Skia Motion Blur controller; sdk=%d; strategy=%s\n", mb_sdk(), mb_strategy(mb_sdk()));
        return 0;
    }
    if (getuid() || argc != 2 || !mb_supported(mb_sdk())) return 1;
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGHUP, stop);
    mb_restore_composition();
    char disabled[PATH_MAX], removed[PATH_MAX];
    int disabled_size = snprintf(disabled, sizeof(disabled), "%s/disable", argv[1]);
    int removed_size = snprintf(removed, sizeof(removed), "%s/remove", argv[1]);
    char settings_path[PATH_MAX];
    int settings_size = snprintf(settings_path, sizeof(settings_path), "%s/skia-motion-blur.prop", argv[1]);
    if (disabled_size < 0 || (size_t)disabled_size >= sizeof(disabled) ||
        removed_size < 0 || (size_t)removed_size >= sizeof(removed) ||
        settings_size < 0 || (size_t)settings_size >= sizeof(settings_path)) return 1;
    char *previous_config = NULL;
    char *previous_package = NULL;
    const char *previous_reason = "";
    uint64_t session = 0, probe_started = 0;
    bool probe_blocked = false, owned = false;
    MbConfig *config = mb_config_create();
    if (!config) { unlink(MB_LEASE_PATH); return 1; }
    bool valid = false, mirrored = false;
    bool input_started = false;
    uint64_t next_config_check = 0, next_focus_check = 0;
    char *package = NULL;
    while (running && access(disabled, F_OK) && access(removed, F_OK)) {
        uint64_t now = mb_millis();
        if (config_check_due(now, next_config_check)) {
            char *source = malloc(MB_CONFIG_MAX_TEXT + 2);
            MbConfig *next = mb_config_create();
            if (!source || !next) { free(source); mb_config_free(next); break; }
            size_t size = mb_read_text(settings_path, source, MB_CONFIG_MAX_TEXT + 2);
            bool next_valid = size && mb_parse_config(source, size, next);
            next_config_check = now + 3000;
            if (!next_valid) strcpy(source, "enabled=0\n");
            if (!previous_config || strcmp(source, previous_config) || !mirrored) {
                mirrored = mb_write_control("/data/misc/skia-motion-blur/skia-motion-blur.prop", source);
                if (mirrored) {
                    char *compact = realloc(source, strlen(source) + 1);
                    if (compact) source = compact;
                    free(previous_config);
                    previous_config = source;
                    source = NULL;
                }
                session = 0;
                probe_started = 0;
                probe_blocked = false;
                next_focus_check = 0;
            }
            free(source);
            mb_config_free(config);
            config = next;
            valid = next_valid;
            if (!input_started && valid && mirrored && config->enabled && config->strength > 0) {
                mb_input_monitor_start();
                input_started = true;
            }
            if (config->log) {
                MB_LOG(config->log, ANDROID_LOG_INFO, "Controller config valid=%d mirrored=%d enabled=%d strength=%.2f exposure_ms=%.2f scale=%u timeout_ms=%u memory_mb=%u packages=%zu debug=%d",
                    valid, mirrored, config->enabled, config->strength,
                    config->exposure_ms, config->history_scale, config->history_timeout_ms,
                    config->memory_limit_mb, config->packages.count, config->debug);
            }
        }
        if (!next_focus_check || now >= next_focus_check) {
            char *display = mb_command("/system/bin/dumpsys window displays 2>/dev/null");
            char *next_package = mb_focused_package(display);
            free(display);
            free(package);
            package = next_package;
            next_focus_check = now + focus_interval(config, valid, mirrored);
            if (config->log) MB_LOG(config->log, ANDROID_LOG_INFO, "Focus foreground=%s", package ? package : "unknown");
        }
        if (!same_package(package, previous_package)) { probe_started = 0; probe_blocked = false; }
        bool active = valid && mirrored && mb_config_allows_package(config, package);
        const char *reason = !valid ? "invalid-config" : !mirrored ? "config-copy-failed" :
            !active ? "inactive-foreground" : "armed-awaiting-gles-output";
        if (active) {
            if (!probe_started) probe_started = mb_millis();
            char heartbeat[64];
            mb_read_text("/data/misc/skia-motion-blur/skia-motion-blur.gles", heartbeat, sizeof(heartbeat));
            unsigned long long last = 0;
            sscanf(heartbeat, "%llu", &last);
            bool ready = last >= probe_started && last <= mb_millis();
            if (mb_millis() - probe_started > 3500 && !ready) probe_blocked = true;
            if (probe_blocked) { active = false; reason = "no-eligible-gles-output"; }
            else if (ready) reason = "gles-output-detected";
        }
        if (active && !owned) {
            int state = mb_composition();
            // Record ownership first so restart cleanup can restore composition.
            if (state == 0 && mb_write_control(MB_OWNED_PATH, "1\n")) {
                free(mb_command("/system/bin/service call SurfaceFlinger 1008 i32 1 2>/dev/null"));
                owned = mb_composition() == 1;
                if (!owned) unlink(MB_OWNED_PATH);
            } else if (state != 1) active = false;
            if (!active || (state == 0 && !owned)) { active = false; reason = "unsupported-composition-control"; }
        }
        if (active) {
            if (!session || !same_package(package, previous_package)) session = mb_millis();
            char lease[64];
            // Eligibility expires if the controller stops refreshing this lease.
            snprintf(lease, sizeof(lease), "%llu %llu\n", (unsigned long long)(mb_millis() + 1200), (unsigned long long)session);
            mb_write_control(MB_LEASE_PATH, lease);
        } else {
            unlink(MB_LEASE_PATH);
            session = 0;
            if (owned) { mb_restore_composition(); owned = false; }
        }
        static uint64_t last_status_log;
        if (config->log && (!last_status_log || now - last_status_log >= 5000)) {
            MB_LOG(config->log, ANDROID_LOG_INFO, "Controller state=%s active=%d owned=%d session=%llu probe_blocked=%d mirrored=%d",
                                reason, active, owned, (unsigned long long)session, probe_blocked, mirrored);
            last_status_log = now;
        }
        if (strcmp(reason, previous_reason) || !same_package(package, previous_package)) {
            MB_LOG(config->log, ANDROID_LOG_INFO, "Controller: %s; foreground=%s",
                                reason, package && *package ? package : "unknown");
            const char *name = package ? package : "";
            size_t capacity = strlen(name) + 256;
            char *status = malloc(capacity);
            if (status) {
                snprintf(status, capacity, "sdk=%d\nstrategy=%s\nstate=%s\nforeground=%s\n",
                         mb_sdk(), mb_strategy(mb_sdk()), reason, name);
                mb_write_control("/data/misc/skia-motion-blur/skia-motion-blur.status", status);
                free(status);
            }
            previous_reason = reason;
        }
        free(previous_package);
        previous_package = package ? strdup(package) : NULL;
        const struct timespec delay = { .tv_nsec = 400000000 };
        nanosleep(&delay, NULL);
    }
    free(previous_package);
    free(package);
    free(previous_config);
    mb_config_free(config);
    unlink(MB_LEASE_PATH);
    mb_restore_composition();
    return 0;
}
