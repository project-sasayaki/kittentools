#pragma once

#include <stddef.h>
#include <stdint.h>

void card_print_begin(void);

void card_print_text(const char *cardPath, const uint8_t *data, size_t length);
