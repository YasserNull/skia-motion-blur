#define _POSIX_C_SOURCE 200809L
#include "focus.h"
#include <pthread.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

static pthread_once_t focus_once = PTHREAD_ONCE_INIT;
static regex_t focus;
static int focus_error;

static void compile_focus(void) {
    focus_error = regcomp(&focus,
        "mCurrentFocus=Window\\{[^}\r\n]*[[:space:]]([A-Za-z0-9_]+(\\.[A-Za-z0-9_]+)+)/[^}\r\n]*\\}",
        REG_EXTENDED);
}

char *mb_focused_package(const char *displays) {
    pthread_once(&focus_once, compile_focus);
    char *package = NULL;
    if (!displays || focus_error) return NULL;
    regmatch_t match[3];
    while (regexec(&focus, displays, 3, match, 0) == 0) {
        size_t size = (size_t)(match[1].rm_eo - match[1].rm_so);
        const char *candidate = displays + match[1].rm_so;
        // Conflicting focused packages across displays leave the foreground unknown.
        if (package && (strlen(package) != size || strncmp(package, candidate, size))) {
            free(package);
            return NULL;
        }
        if (!package) {
            package = strndup(candidate, size);
            if (!package) return NULL;
        }
        displays += match[0].rm_eo;
    }
    return package;
}
