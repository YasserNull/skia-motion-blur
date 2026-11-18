#define _POSIX_C_SOURCE 200809L
static char test_config_path[4096];
#define SKIA_MOTION_BLUR_CONFIG_PATH test_config_path
#include "../src/config_cache.c"
#include <assert.h>
#include <errno.h>

static bool allocation_failure, read_failure;
void *__real_calloc(size_t count, size_t size);
FILE *__real_fopen(const char *path, const char *mode);

void *__wrap_calloc(size_t count, size_t size) {
    return allocation_failure ? NULL : __real_calloc(count, size);
}

FILE *__wrap_fopen(const char *path, const char *mode) {
    if (read_failure && !strcmp(path, test_config_path) && mode[0] == 'r') {
        errno = EACCES;
        return NULL;
    }
    return __real_fopen(path, mode);
}

static void write_settings(unsigned strength) {
    char temporary[sizeof(test_config_path) + 5];
    snprintf(temporary, sizeof(temporary), "%s.tmp", test_config_path);
    FILE *file = fopen(temporary, "w");
    assert(file);
    assert(fprintf(file, "enabled=1\nstrength=%u\nallow_packages=*\n", strength) > 0);
    assert(!fclose(file));
    assert(!rename(temporary, test_config_path));
}

static const MbConfig *load(void) {
    checked = 0;
    return mb_config_acquire();
}

int main(void) {
    const char *temporary_root = getenv("TMPDIR");
    char directory[4000];
    int size = snprintf(directory, sizeof(directory), "%s/skia-config-XXXXXX", temporary_root ? temporary_root : "/tmp");
    assert(size > 0 && (size_t)size < sizeof(directory));
    assert(mkdtemp(directory));
    snprintf(test_config_path, sizeof(test_config_path), "%s/config.prop", directory);
    assert(!mb_is_surface_flinger());
    surface_flinger = true;
    write_settings(1);
    const MbConfig *first = load();
    assert(first->enabled && first->strength == 1);
    write_settings(2);
    allocation_failure = true;
    const MbConfig *failed = load();
    assert(failed->strength == 1);
    mb_config_release(failed);
    allocation_failure = false;
    const MbConfig *second = load();
    assert(second->enabled && second->strength == 2);
    assert(first->strength == 1 && first->generation < second->generation);
    mb_config_release(second);
    write_settings(3);
    read_failure = true;
    failed = load();
    assert(!failed->enabled);
    mb_config_release(failed);
    read_failure = false;
    const MbConfig *recovered = load();
    assert(recovered->enabled && recovered->strength == 3);
    mb_config_release(recovered);
    assert(!unlink(test_config_path));
    const MbConfig *missing = load();
    assert(!missing->enabled);
    mb_config_release(missing);
    write_settings(4);
    recovered = load();
    assert(recovered->enabled && recovered->strength == 4);
    mb_config_release(recovered);
    mb_config_release(first);
    assert(!unlink(test_config_path) && !rmdir(directory));
    puts("PASS: snapshot ownership, allocation/read retries, deletion and atomic replacement");
}
