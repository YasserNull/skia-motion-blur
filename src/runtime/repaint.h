#pragma once
#include <stdbool.h>
#include <stdint.h>

bool mb_repaint_available(void);
void mb_request_repaint(uint64_t session);
