#include <renderengine/skia_motion_blur_config.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool parse(const char *text, MbConfig *config) {
    return mb_parse_config(text, strlen(text), config);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    MbConfig *config = mb_config_create();
    assert(config);
    assert(!config->enabled && config->strength == 3 && config->history_scale == 2);
    assert(parse("enabled=1\nallow_packages=com.android.settings,com.example.app\nstrength=0.25\n", config));
    assert(mb_config_allows_package(config, "com.android.settings"));
    assert(!mb_config_allows_package(config, "com.android.settings.extra"));
    assert(mb_config_matches(config, "com.android.settings/.Settings#42"));
    assert(!mb_config_matches(config, "fake.com.android.settings/.Settings"));
    assert(!mb_config_matches(config, "SurfaceView[com.android.settings/.Settings]"));
    const char *invalid[] = {"strength=nan", "strength=inf", "strength=-1", "strength=8.01", "strength=9",
        "enabled=2", "debug=maybe", "unknown=1", "enabled", "history_scale=0", "history_scale=5",
        "history_scale=+2", "history_scale=99999999999999999999999", "history_timeout_ms=15",
        "history_timeout_ms=251", "max_layers=0", "max_layers=9", "memory_limit_mb=7", "memory_limit_mb=129",
        "allow_packages=com.example.*", "allow_packages=*,com.example.app", "allow_packages=com.example.app,*",
        "allow_packages=com.example.app,", "allow_packages=com.example.app,  ", "allow_layer_prefixes=*",
        "exposure_ms=0.99", "exposure_ms=50.01", "exposure_ms=nan", "exposure_ms=inf",
        "temp_settings=maybe", "log=2", "log=maybe", "quality=ultra"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!parse(invalid[i], config));
        assert(config->enabled && config->strength == 0.25f);
        assert(mb_config_allows_package(config, "com.example.app"));
    }
    assert(parse("exposure_ms=1\ntemp_settings=0\nlog=true\nquality=high\n", config));
    assert(config->exposure_ms == 1 && !config->temp_settings && config->log && config->history_scale == 1);
    assert(parse("log=true\n", config) && config->log);
    assert(parse("log=0\n", config) && !config->log);
    assert(parse("exposure_ms=50\nquality=balanced\n", config));
    assert(config->exposure_ms == 50 && config->history_scale == 2);
    assert(parse("quality=low\n", config) && config->history_scale == 4);
    assert(parse("quality=high\nhistory_scale=3\n", config) && config->history_scale == 3);
    assert(parse("history_scale=3\nquality=high\n", config) && config->history_scale == 1);
    assert(parse("# defaults\r\n enabled = true\r\nallow_packages=*\n", config));
    assert(config->temp_settings && !config->log && config->exposure_ms > 16.66f && config->exposure_ms < 16.68f);
    assert(config->strength == 3 && mb_config_allows_package(config, "com.example.game"));
    assert(!mb_config_allows_package(config, ""));
    assert(parse("enabled=1\nstrength=8\nallow_layer_prefixes=NotificationShade\nexclude_layers=Wallpaper\n", config));
    assert(config->strength == 8 && mb_config_matches(config, "NotificationShade#42"));
    assert(!mb_config_matches(config, "NotificationShade Wallpaper"));
    assert(parse("enabled=1\nstrength=0\nallow_packages=*\n", config));
    assert(!mb_config_allows_package(config, "com.example.game"));
    char oversized[MB_CONFIG_MAX_TEXT + 1];
    memset(oversized, 'x', sizeof(oversized));
    assert(!mb_parse_config(oversized, sizeof(oversized), config));
    const char embedded[] = "enabled=1\0\nallow_packages=*\n";
    assert(!mb_parse_config(embedded, sizeof(embedded) - 1, config));
    assert(parse("allow_packages=com.example.first\nallow_packages=com.example.second\n", config));
    assert(config->packages.count == 1 && !strcmp(config->packages.items[0], "com.example.second"));
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    char defaults[MB_CONFIG_MAX_TEXT + 1];
    size_t size = fread(defaults, 1, sizeof(defaults), file);
    fclose(file);
    assert(mb_parse_config(defaults, size, config));
    assert(config->enabled && config->strength == 3 && config->history_scale == 4);
    assert(!mb_is_surface_flinger());
    const MbConfig *first = mb_config_acquire(), *second = mb_config_acquire();
    assert(first && first == second && !first->enabled);
    mb_config_release(first);
    mb_config_release(second);
    mb_config_free(config);
    puts("PASS: exact packages, parser rollback, limits, repeated lists, default configuration and snapshot ownership");
}
