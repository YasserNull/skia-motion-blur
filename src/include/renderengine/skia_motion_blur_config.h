#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MB_CONFIG_MAX_TEXT 16384
#define MB_CONFIG_DEFAULTS { .temp_settings = true, .strength = 3.0f, .exposure_ms = 16.667f, .max_layers = 3, .history_scale = 2, \
    .history_timeout_ms = 150, .memory_limit_mb = 48 }

typedef struct {
    char **items;
    size_t count;
} MbStringList;

typedef struct {
    bool enabled;
    bool debug;
    bool temp_settings;
    bool log;
    float strength;
    float exposure_ms;
    unsigned max_layers;
    unsigned history_scale;
    unsigned history_timeout_ms;
    unsigned memory_limit_mb;
    uint64_t generation;
    MbStringList packages;
    MbStringList layer_prefixes;
    MbStringList excluded_layers;
} MbConfig;

// Free owned heap configurations with mb_config_free, not mb_config_release.
MbConfig *mb_config_create(void);
void mb_config_free(MbConfig *config);
void mb_config_init(MbConfig *config);
void mb_config_destroy(MbConfig *config);
// Initialize the result before parsing. Invalid input leaves it unchanged.
bool mb_parse_config(const char *text, size_t size, MbConfig *result);
bool mb_config_allows_package(const MbConfig *config, const char *package);
bool mb_config_matches(const MbConfig *config, const char *name);
bool mb_is_surface_flinger(void);
// Allocation failure returns NULL; shared snapshots remain immutable.
const MbConfig *mb_config_acquire(void);
// Release each acquired immutable configuration snapshot exactly once.
void mb_config_release(const MbConfig *config);
