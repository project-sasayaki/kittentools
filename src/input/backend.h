#pragma once

#include <stdint.h>
#include <stdbool.h>

struct idas_io_analog_state {
    int16_t wheel;
    int16_t accel;
    int16_t brake;
};

struct idas_io_backend {
    void (*jvs_read_buttons)(uint8_t *gamebtn);
    void (*jvs_read_shifter)(uint8_t *gear);
    void (*jvs_read_analogs)(struct idas_io_analog_state *state);
    void (*jvs_read_system)(uint8_t *sysbtn_out);
    void (*ffb_init)(void);
    void (*ffb_req_toggle)(bool active);
    void (*ffb_req_constant_force)(uint8_t direction, uint8_t force);
    void (*ffb_req_rumble)(uint8_t period, uint8_t force);
    void (*ffb_req_damper)(uint8_t force);
};