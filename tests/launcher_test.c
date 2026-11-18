#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int launcher_entry(int argc, char **argv);
static char counter[32], observed_preload[4096], executed[4096];
static size_t counter_size;
static bool payload, counter_open, write_ok, sync_ok, fail_first_exec;
static unsigned exec_count;
static jmp_buf finished;

static void reset(void) {
    strcpy(counter, "0\n");
    counter_size = 2;
    payload = counter_open = write_ok = sync_ok = true;
    fail_first_exec = false;
    exec_count = 0;
    executed[0] = observed_preload[0] = '\0';
    unsetenv("LD_PRELOAD");
}

static void launch(void) {
    char name[] = "/system/bin/surfaceflinger";
    char *args[] = {name, NULL};
    if (!setjmp(finished)) { launcher_entry(1, args); assert(false); }
    assert(exec_count == (fail_first_exec ? 2u : 1u));
    assert(!strcmp(executed, "/data/misc/skia-motion-blur/runtime/surfaceflinger"));
}

uid_t __wrap_getuid(void) { return 1000; }
int __wrap___system_property_get(const char *name, char *value) {
    (void)name;
    strcpy(value, "33");
    return 2;
}
int __wrap_access(const char *path, int mode) {
    (void)mode;
    assert(!strcmp(path, "/data/misc/skia-motion-blur/runtime/libskiamotionblur.so"));
    return payload ? 0 : -1;
}
int __wrap_open(const char *path, int flags, ...) {
    assert(!strcmp(path, "/data/misc/skia-motion-blur/skia-motion-blur.launches"));
    assert(!(flags & O_CREAT));
    return counter_open ? 99 : -1;
}
int __wrap_fstat(int fd, struct stat *metadata) {
    assert(fd == 99);
    metadata->st_mode = S_IFREG | 0644;
    metadata->st_size = (off_t)counter_size;
    return 0;
}
ssize_t __wrap_read(int fd, void *data, size_t size) {
    assert(fd == 99 && size >= counter_size);
    memcpy(data, counter, counter_size);
    return (ssize_t)counter_size;
}
off_t __wrap_lseek(int fd, off_t offset, int origin) { (void)origin; assert(fd == 99); return offset; }
int __wrap_ftruncate(int fd, off_t size) { (void)size; assert(fd == 99); return 0; }
ssize_t __wrap_write(int fd, const void *data, size_t size) {
    assert(fd == 99 && size < sizeof(counter));
    if (!write_ok) return -1;
    memcpy(counter, data, size);
    counter[size] = '\0';
    counter_size = size;
    return (ssize_t)size;
}
int __wrap_fsync(int fd) { assert(fd == 99); return sync_ok ? 0 : -1; }
int __wrap_close(int fd) { assert(fd == 99); return 0; }
int __wrap_execv(const char *path, char *const *args) {
    assert(!strcmp(args[0], "/system/bin/surfaceflinger"));
    ++exec_count;
    snprintf(executed, sizeof(executed), "%s", path);
    const char *preload = getenv("LD_PRELOAD");
    snprintf(observed_preload, sizeof(observed_preload), "%s", preload ? preload : "");
    if (fail_first_exec && exec_count == 1) return -1;
    // A successful exec replaces the process; this models its exit in tests.
    longjmp(finished, 1);
}

int main(void) {
    reset();
    launch();
    assert(!strcmp(counter, "1\n") && strstr(observed_preload, "runtime/libskiamotionblur.so"));
    exec_count = 0;
    launch();
    assert(!strcmp(counter, "2\n"));
    unsetenv("LD_PRELOAD");
    exec_count = 0;
    launch();
    assert(!strcmp(counter, "2\n") && !*observed_preload);
    const char *invalid[] = {"", "\0\0\0\0\0\0\0\0\0\0", "bad", "0 trailing"};
    const size_t sizes[] = {0, 10, 3, 10};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        reset();
        memcpy(counter, invalid[i], sizes[i]);
        counter_size = sizes[i];
        launch();
        assert(!*observed_preload);
    }
    reset(); payload = false; launch(); assert(!*observed_preload);
    reset(); counter_open = false; launch(); assert(!*observed_preload);
    reset(); write_ok = false; launch(); assert(!*observed_preload);
    reset(); sync_ok = false; launch(); assert(!*observed_preload);
    reset(); setenv("LD_PRELOAD", "/existing.so", 1); launch();
    assert(!strcmp(observed_preload, "/data/misc/skia-motion-blur/runtime/libskiamotionblur.so:/existing.so"));
    reset(); setenv("LD_PRELOAD", "/existing.so", 1); fail_first_exec = true; launch();
    assert(!strcmp(observed_preload, "/existing.so"));
    puts("PASS: prepared executable, missing payload/counter, corrupt counter, attempt limit, write/sync failure, inherited preload and fallback");
}
