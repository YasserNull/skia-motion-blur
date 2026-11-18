#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint64_t mb_millis(void);
unsigned mb_div_ceil(unsigned value, unsigned divisor);
size_t mb_read_text(const char *path, char *buffer, size_t capacity);
// The caller frees the returned command output.
char *mb_command(const char *text);
bool mb_parse_float(const char *value, float *result, float minimum, float maximum);
