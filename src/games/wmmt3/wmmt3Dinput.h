#pragma once
#include <stdbool.h>
#include <stdint.h>

/* All calls belong to the WMMT3 FFB worker. */
void wmmt3_di_ffb_apply(const uint8_t frame[10], bool running);
void wmmt3_di_ffb_close(void);
bool wmmt3_di_ffb_init(void);
void wmmt3_di_ffb_motor(bool running);
