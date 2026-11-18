#pragma once

#include <stdbool.h>

bool mb_write_control(const char *path, const char *text);
int mb_parse_composition(const char *parcel);
int mb_composition(void);
void mb_restore_composition(void);
