#define _POSIX_C_SOURCE 200809L
#include <utils.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t mb_millis(void) {
    struct timespec value = {0};
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}

unsigned mb_div_ceil(unsigned value, unsigned divisor) {
    return value / divisor + (value % divisor != 0);
}

size_t mb_read_text(const char *path, char *buffer, size_t capacity) {
    if (!capacity) return 0;
    FILE *file = fopen(path, "re");
    size_t size = file ? fread(buffer, 1, capacity - 1, file) : 0;
    if (file) fclose(file);
    buffer[size] = '\0';
    return size;
}

char *mb_command(const char *text) {
    FILE *pipe = popen(text, "re");
    if (!pipe) return NULL;
    char *output = calloc(1, 2048);
    size_t size = 0, capacity = 2048;
    char buffer[2048];
    while (output && fgets(buffer, sizeof(buffer), pipe) && size < 1048576) {
        size_t count = strlen(buffer);
        if (size + count + 1 > capacity) {
            size_t next_capacity = capacity * 2;
            char *next = realloc(output, next_capacity);
            if (!next) { free(output); output = NULL; break; }
            output = next;
            capacity = next_capacity;
        }
        memcpy(output + size, buffer, count + 1);
        size += count;
    }
    int status = pclose(pipe);
    // Discard partial output when the command exits unsuccessfully.
    if (status && output) output[0] = '\0';
    return output;
}

bool mb_parse_float(const char *value, float *result, float minimum, float maximum) {
    char *end = NULL;
    float parsed = strtof(value, &end);
    if (!*value || end != value + strlen(value) || !isfinite(parsed) || parsed < minimum || parsed > maximum) return false;
    *result = parsed;
    return true;
}
