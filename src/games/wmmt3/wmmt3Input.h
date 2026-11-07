#pragma once

#include <stdint.h>

uint16_t wmmt3_input_wheel_position(void);

void wmmt3_input_init(void);

struct idas_io_backend;
const struct idas_io_backend *wmmt3_input_backend(void);
