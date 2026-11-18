#define _POSIX_C_SOURCE 200809L
#include "policy.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static bool preload_allowed(void) {
    if (!mb_supported(mb_sdk()) || access(MB_PAYLOAD_PATH, R_OK)) return false;
    // The counter must already exist as a regular file; never follow a symlink.
    int fd = open(MB_COUNTER_PATH, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    char buffer[32] = {0};
    struct stat metadata = {0};
    bool valid = fstat(fd, &metadata) == 0 && S_ISREG(metadata.st_mode) &&
                 metadata.st_size > 0 && metadata.st_size < (off_t)sizeof(buffer);
    ssize_t bytes = valid ? read(fd, buffer, sizeof(buffer) - 1) : -1;
    unsigned count = 0;
    char trailing = 0;
    valid = bytes > 0 && sscanf(buffer, "%u %c", &count, &trailing) == 1 && count < 2;
    if (valid) {
        int size = snprintf(buffer, sizeof(buffer), "%u\n", count + 1);
        // Persist the attempt before loading so repeated exits exhaust the limit.
        valid = lseek(fd, 0, SEEK_SET) == 0 && ftruncate(fd, 0) == 0 &&
                write(fd, buffer, (size_t)size) == size && fsync(fd) == 0;
    }
    close(fd);
    return valid;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--probe")) {
        printf("Skia Motion Blur launcher; sdk=%d; strategy=%s; pointer_bits=%zu\n",
               mb_sdk(), mb_strategy(mb_sdk()), sizeof(void *) * 8);
        return 0;
    }
    if (getuid() != 1000 || !argv[0] || strcmp(argv[0], "/system/bin/surfaceflinger")) return 126;
    bool inject = preload_allowed();
    const char *environment = getenv("LD_PRELOAD");
    char *inherited = environment ? strdup(environment) : NULL;
    if (environment && !inherited) inject = false;
    if (inject) {
        size_t size = strlen(MB_PAYLOAD_PATH) + (inherited && *inherited ? strlen(inherited) + 1 : 0) + 1;
        char *preload = malloc(size);
        if (preload) {
            snprintf(preload, size, "%s%s%s", MB_PAYLOAD_PATH,
                     inherited && *inherited ? ":" : "", inherited && *inherited ? inherited : "");
            setenv("LD_PRELOAD", preload, 1);
            free(preload);
        }
    }
    execv(MB_STOCK_PATH, argv);
    // This retry handles exec failures; crashes use the persisted attempt limit.
    if (inherited && *inherited) setenv("LD_PRELOAD", inherited, 1);
    else if (!environment || inherited) unsetenv("LD_PRELOAD");
    free(inherited);
    execv(MB_STOCK_PATH, argv);
    return 127;
}
