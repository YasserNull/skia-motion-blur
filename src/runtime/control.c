#define _POSIX_C_SOURCE 200809L
#include "control.h"
#include "policy.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

bool mb_write_control(const char *path, const char *text) {
    char temporary[PATH_MAX];
    int count = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (count < 0 || (size_t)count >= sizeof(temporary)) return false;
    FILE *file = fopen(temporary, "we");
    if (!file) return false;
    size_t size = strlen(text);
    bool valid = fwrite(text, 1, size, file) == size;
    valid = fclose(file) == 0 && valid;
    // Set ownership and SELinux context before publishing the complete file.
    if (chown(temporary, 1000, 1003) || chmod(temporary, 0644)) valid = false;
    static const char label[] = "u:object_r:skia_motion_blur_data_file:s0";
    if (setxattr(temporary, "security.selinux", label, sizeof(label), 0)) valid = false;
    if (!valid || rename(temporary, path)) { unlink(temporary); return false; }
    return true;
}

int mb_parse_composition(const char *parcel) {
    if (!parcel) return -1;
    unsigned words[5];
    size_t count = 0;
    const char *at = parcel;
    while ((at = strstr(at, "0x"))) {
        const char *hex = at + 2;
        size_t digits = strspn(hex, "0123456789abcdefABCDEF");
        at = hex + digits;
        if (!digits || at[0] != ':' || at[1] != ' ') continue;
        at += 2;
        const char *end = strchr(at, '\n');
        if (!end) end = at + strlen(at);
        while (at < end) {
            while (at < end && *at == ' ') ++at;
            if ((size_t)(end - at) < 8 || strspn(at, "0123456789abcdefABCDEF") < 8) break;
            if (count == 5) return -1;
            char word[9];
            memcpy(word, at, 8);
            word[8] = '\0';
            words[count++] = (unsigned)strtoul(word, NULL, 16);
            at += 8;
        }
        at = end;
    }
    // Accept only the expected parcel layout and a boolean composition state.
    if (count != 5 || words[0] || words[1] || words[3] || words[4] > 1) return -1;
    return (int)words[4];
}

int mb_composition(void) {
    char *parcel = mb_command("/system/bin/service call SurfaceFlinger 1010 2>/dev/null");
    int state = mb_parse_composition(parcel);
    free(parcel);
    return state;
}

void mb_restore_composition(void) {
    // The ownership marker limits restoration to changes made by this module.
    if (!access(MB_OWNED_PATH, F_OK)) {
        if (mb_composition() == 1) free(mb_command("/system/bin/service call SurfaceFlinger 1008 i32 0 2>/dev/null"));
        unlink(MB_OWNED_PATH);
    }
}
