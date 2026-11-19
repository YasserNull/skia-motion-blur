#define _POSIX_C_SOURCE 200809L
#include <renderengine/skia_motion_blur_config.h>
#include <utils.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *value) {
    value += strspn(value, " \t\r");
    size_t size = strlen(value);
    while (size && strchr(" \t\r", value[size - 1])) value[--size] = '\0';
    return value;
}

static void clear_list(MbStringList *list) {
    for (size_t i = 0; i < list->count; ++i) free(list->items[i]);
    free(list->items);
    *list = (MbStringList){0};
}

void mb_config_init(MbConfig *config) { *config = (MbConfig)MB_CONFIG_DEFAULTS; }

MbConfig *mb_config_create(void) {
    MbConfig *config = malloc(sizeof(*config));
    if (config) mb_config_init(config);
    return config;
}

void mb_config_free(MbConfig *config) {
    if (!config) return;
    mb_config_destroy(config);
    free(config);
}

void mb_config_destroy(MbConfig *config) {
    clear_list(&config->packages);
    clear_list(&config->layer_prefixes);
    clear_list(&config->excluded_layers);
    mb_config_init(config);
}

static bool parse_list(char *value, MbStringList *out, bool package) {
    clear_list(out);
    while (*value) {
        char *comma = strchr(value, ',');
        if (comma) *comma = '\0';
        char *part = trim(value);
        size_t size = strlen(part);
        if (!size || size > 160 || out->count >= 64 || strchr(part, '*')) return false;
        if (package && (!strchr(part, '.') || strspn(part,
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._") != size)) return false;
        char *copy = strdup(part);
        if (!copy) return false;
        char **items = realloc(out->items, (out->count + 1) * sizeof(*items));
        if (!items) { free(copy); return false; }
        out->items = items;
        out->items[out->count++] = copy;
        if (!comma) break;
        value = comma + 1;
        if (!*value) return false;
    }
    return true;
}

static bool number(const char *value, unsigned *out, unsigned minimum, unsigned maximum) {
    if (!*value || strspn(value, "0123456789") != strlen(value)) return false;
    unsigned parsed = 0;
    for (; *value; ++value) {
        unsigned digit = (unsigned)(*value - '0');
        // Check the bound before multiplication to avoid wrapping large inputs.
        if (parsed > maximum / 10 || (parsed == maximum / 10 && digit > maximum % 10)) return false;
        parsed = parsed * 10 + digit;
    }
    if (parsed < minimum) return false;
    *out = parsed;
    return true;
}

static bool boolean(const char *value, bool *out) {
    if (!strcmp(value, "1") || !strcmp(value, "true")) *out = true;
    else if (!strcmp(value, "0") || !strcmp(value, "false")) *out = false;
    else return false;
    return true;
}

bool mb_config_allows_package(const MbConfig *config, const char *package) {
    if (!config->enabled || config->strength <= 0 || !package || !*package) return false;
    for (size_t i = 0; i < config->packages.count; ++i) {
        const char *allowed = config->packages.items[i];
        if (!strcmp(allowed, "*") || !strcmp(allowed, package)) return true;
    }
    return false;
}

static bool boundary(char c) {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '.' || c == '_');
}

bool mb_config_matches(const MbConfig *config, const char *name) {
    if (!config->enabled || config->strength <= 0 || !name || !*name || strstr(name, "SurfaceView")) return false;
    for (size_t i = 0; i < config->excluded_layers.count; ++i)
        if (strstr(name, config->excluded_layers.items[i])) return false;
    for (size_t i = 0; i < config->packages.count; ++i)
        if (!strcmp(config->packages.items[i], "*")) return true;
    for (size_t i = 0; i < config->layer_prefixes.count; ++i) {
        const char *prefix = config->layer_prefixes.items[i];
        if (!strncmp(name, prefix, strlen(prefix))) return true;
    }
    for (size_t i = 0; i < config->packages.count; ++i) {
        const char *package = config->packages.items[i];
        size_t size = strlen(package);
        const char *at = name;
        while ((at = strstr(at, package))) {
            if ((at == name || boundary(at[-1])) && (!at[size] || boundary(at[size]))) return true;
            ++at;
        }
    }
    return false;
}

bool mb_parse_config(const char *text, size_t size, MbConfig *result) {
    if (!text || !result || size > MB_CONFIG_MAX_TEXT || memchr(text, '\0', size)) return false;
    char *copy = malloc(size + 1);
    if (!copy) return false;
    memcpy(copy, text, size);
    copy[size] = '\0';
    MbConfig *next = mb_config_create();
    if (!next) { free(copy); return false; }
    bool valid = true;
    for (char *at = copy; *at;) {
        char *newline = strchr(at, '\n');
        if (newline) *newline = '\0';
        char *line = trim(at);
        at = newline ? newline + 1 : at + strlen(at);
        if (!*line || *line == '#') continue;
        char *equal = strchr(line, '=');
        if (!equal) { valid = false; break; }
        *equal = '\0';
        const char *key = trim(line);
        char *value = trim(equal + 1);
        valid = false;
        if (!strcmp(key, "enabled")) valid = boolean(value, &next->enabled);
        else if (!strcmp(key, "debug")) valid = boolean(value, &next->debug);
        else if (!strcmp(key, "temp_settings")) valid = boolean(value, &next->temp_settings);
        else if (!strcmp(key, "log")) valid = boolean(value, &next->log);
        else if (!strcmp(key, "strength")) valid = mb_parse_float(value, &next->strength, 0.0f, 8.0f);
        else if (!strcmp(key, "exposure_ms")) valid = mb_parse_float(value, &next->exposure_ms, 1.0f, 50.0f);
        else if (!strcmp(key, "max_layers")) valid = number(value, &next->max_layers, 1, 8);
        else if (!strcmp(key, "history_scale")) valid = number(value, &next->history_scale, 1, 4);
        else if (!strcmp(key, "history_timeout_ms")) valid = number(value, &next->history_timeout_ms, 16, 250);
        else if (!strcmp(key, "memory_limit_mb")) valid = number(value, &next->memory_limit_mb, 8, 128);
        else if (!strcmp(key, "allow_packages")) {
            if (!strcmp(value, "*")) {
                clear_list(&next->packages);
                next->packages.items = malloc(sizeof(*next->packages.items));
                if (next->packages.items) {
                    next->packages.items[0] = strdup("*");
                    if (next->packages.items[0]) { next->packages.count = 1; valid = true; }
                }
            } else valid = parse_list(value, &next->packages, true);
        } else if (!strcmp(key, "allow_layer_prefixes")) valid = parse_list(value, &next->layer_prefixes, false);
        else if (!strcmp(key, "exclude_layers")) valid = parse_list(value, &next->excluded_layers, false);
        if (!valid) break;
    }
    free(copy);
    // Publish the new configuration only after every key parses successfully.
    if (valid) {
        mb_config_destroy(result);
        *result = *next;
        // The result now owns the lists; only the temporary container is freed.
        free(next);
    } else mb_config_free(next);
    return valid;
}
