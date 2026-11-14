#pragma once
#include <windows.h>
#include "backend.h"

void idas_xi_init(const struct idas_io_backend **backend);

/* For a switch outside the shared set: the mask of a button named as in [xinput]
   (0 when unknown or "none"; key names the setting in the error), and whether it is held. */
uint32_t idas_xi_button_mask(const char *spec, const char *key);
bool idas_xi_button_held(uint32_t mask);
