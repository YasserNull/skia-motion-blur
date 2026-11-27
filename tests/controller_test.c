#define main controller_entry
#include "../src/runtime/controller.c"
#undef main
#include <assert.h>
#include <stdarg.h>

static unsigned log_count;
static int log_priority;
static char log_text[128];

void mb_input_monitor_start(void) {}

int __android_log_print(int priority, const char *tag, const char *format, ...) {
    assert(!strcmp(tag, "SkiaMotionBlur"));
    ++log_count;
    log_priority = priority;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(log_text, sizeof(log_text), format, arguments);
    va_end(arguments);
    return 0;
}

int main(void) {
    assert(!mb_log_enabled());
    unsigned evaluated = 0;
    MB_LOG(false, ANDROID_LOG_INFO, "Disabled %u", ++evaluated);
    MB_LOG(false, ANDROID_LOG_WARN, "Disabled warning");
    assert(!log_count && !evaluated);
    MB_LOG(true, ANDROID_LOG_INFO, "Controller: %s", "active");
    assert(log_count == 1 && log_priority == ANDROID_LOG_INFO && !strcmp(log_text, "Controller: active"));
    MB_LOG(true, ANDROID_LOG_INFO, "Renderer event=%s", "draw");
    assert(log_count == 2 && !strcmp(log_text, "Renderer event=draw"));
    MB_LOG(true, ANDROID_LOG_WARN, "Failure: %d", -1);
    assert(log_count == 3 && log_priority == ANDROID_LOG_WARN && !strcmp(log_text, "Failure: -1"));
    assert(mb_parse_composition(NULL) == -1);
    assert(mb_parse_composition("") == -1);
    assert(mb_parse_composition("Parcel(Error: permission denied)") == -1);
    assert(mb_parse_composition("0x00000000: 00000000 00000000 12345678 00000000 00000000") == 0);
    assert(mb_parse_composition("Parcel(\n  0x00000000: 00000000 00000000 12345678 00000000\n  0x00000010: 00000001\n)") == 1);
    assert(mb_parse_composition("0x00000000: 00000000 00000000 12345678 00000000 00000002") == -1);
    assert(mb_parse_composition("0x00000000: 00000001 00000000 12345678 00000000 00000001") == -1);
    assert(mb_parse_composition("0x00000000: 00000000 00000000 12345678 00000001 00000001") == -1);
    assert(mb_parse_composition("0x00000000: 00000000 00000000 12345678 00000000") == -1);
    assert(mb_parse_composition("0x00000000: 00000000 00000000 12345678 00000000 00000001 00000000") == -1);
    assert(mb_parse_composition("0x00000000: 00000000 00000000 12345678 00000000 invalid!") == -1);
    assert(mb_parse_composition("0x00000000: 00000000 00000000 12345678 00000000 00000001    '....'") == 1);
    assert(same_package(NULL, "") && !same_package("com.example.app", "com.example.other"));
    assert(config_check_due(0, 0));
    assert(!config_check_due(2999, 3000));
    assert(config_check_due(3000, 3000));
    MbConfig *settings = mb_config_create();
    assert(settings);
    assert(focus_interval(settings, true, true) == 5000);
    const char *active_settings = "enabled=1\nallow_packages=*";
    assert(mb_parse_config(active_settings, strlen(active_settings), settings));
    assert(focus_interval(settings, true, true) == 400);
    assert(focus_interval(settings, true, false) == 5000);
    mb_config_free(settings);
    puts("PASS: unified log switch/tag, composition state, malformed/unsupported parcels and foreground transitions");
}
