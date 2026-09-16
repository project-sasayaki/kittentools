#include <windows.h>
#include <xinput.h>
#include <math.h>
#include <assert.h>
#include "backend.h"
#include "idas_input.h"
#include "../config/config.h"
#include "../log/log.h"

static void idas_xi_jvs_read_buttons(uint8_t *gamebtn_out);
static void idas_xi_jvs_read_shifter(uint8_t *gear_out);
static void idas_xi_jvs_read_analogs(struct idas_io_analog_state *out);
static void idas_xi_ffb_init(void);
static void idas_xi_ffb_toggle(bool active);
static void idas_xi_ffb_constant_force(uint8_t direction, uint8_t force);
static void idas_xi_ffb_rumble(uint8_t force, uint8_t period);
static void idas_xi_ffb_damper(uint8_t force);



static const struct idas_io_backend idas_xi_backend = {
    .jvs_read_buttons       = idas_xi_jvs_read_buttons,
    .jvs_read_shifter       = idas_xi_jvs_read_shifter,
    .jvs_read_analogs       = idas_xi_jvs_read_analogs,
    .ffb_init               = idas_xi_ffb_init,
    .ffb_req_toggle         = idas_xi_ffb_toggle,
    .ffb_req_constant_force = idas_xi_ffb_constant_force,
    .ffb_req_rumble         = idas_xi_ffb_rumble,
    .ffb_req_damper         = idas_xi_ffb_damper
};

const uint16_t max_stick_value = 32767;
const uint16_t max_wheel_value = 24831;

void idas_xi_init(const struct idas_io_backend **backend)
{
    assert(backend != NULL);
    *backend = &idas_xi_backend;
    log_info(plfBoot, "XInput controller initialized");
}

static void idas_xi_jvs_read_buttons(uint8_t *gamebtn_out)
{
    uint8_t gamebtn;
    XINPUT_STATE xi;
    WORD xb;

    assert(gamebtn_out != NULL);

    gamebtn = 0;

    memset(&xi, 0, sizeof(xi));
    XInputGetState(0, &xi);
    xb = xi.Gamepad.wButtons;

    if (xb & XINPUT_GAMEPAD_DPAD_UP) {
        gamebtn |= IDAS_IO_GAMEBTN_UP;
    }

    if (xb & XINPUT_GAMEPAD_DPAD_DOWN) {
        gamebtn |= IDAS_IO_GAMEBTN_DOWN;
    }

    if (xb & XINPUT_GAMEPAD_DPAD_LEFT) {
        gamebtn |= IDAS_IO_GAMEBTN_LEFT;
    }

    if (xb & XINPUT_GAMEPAD_DPAD_RIGHT) {
        gamebtn |= IDAS_IO_GAMEBTN_RIGHT;
    }

    if (xb & (XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_A)) {
        gamebtn |= IDAS_IO_GAMEBTN_START;
    }

    if (xb & (XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_B)) {
        gamebtn |= IDAS_IO_GAMEBTN_VIEW_CHANGE;
    }

    *gamebtn_out = gamebtn;
}

static void idas_xi_jvs_read_shifter(uint8_t *gear_out)
{
    uint8_t gear = 0;
    XINPUT_STATE xi;
    WORD xb;

    assert(gear_out != NULL);

    memset(&xi, 0, sizeof(xi));
    XInputGetState(0, &xi);
    xb = xi.Gamepad.wButtons;

    if (xb & (XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_LEFT_SHOULDER)) {
        gear |= IDAS_IO_GAMEBTN_SHIFTDN;
    }

    if (xb & (XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_RIGHT_SHOULDER)) {
        gear |= IDAS_IO_GAMEBTN_SHIFTUP;
    }

    *gear_out = gear;
}

static int16_t calculate_norm_steering(int16_t axis, uint16_t deadzone, bool linear_steering, int exponent) {
    if (axis == 0) {
        return 0;
    }

    float magnitude = sqrt(axis*axis);
    float norm_axis = axis / magnitude;  /* direction sign: -1.0 or +1.0 */
    float norm_magnitude = 0.0;

    if (magnitude > deadzone)
    {
        if (magnitude > max_stick_value) magnitude = max_stick_value;
        magnitude -= deadzone;
        norm_magnitude = magnitude / (max_stick_value - deadzone);
    } else
    {
        norm_magnitude = 0.0;
    }

    float exp;
    if (linear_steering) {
        exp = 1.0f;
    } else {
        exp = (float)exponent;
        if (exp < 1.0f) exp = 1.0f;  /* safety clamp against bad config */
    }

    return (int16_t)(norm_axis * powf(norm_magnitude, exp) * max_wheel_value);
}

static void idas_xi_jvs_read_analogs(struct idas_io_analog_state *out)
{
    XINPUT_STATE xi;
    uint16_t left;
    uint16_t right;

    assert(out != NULL);

    memset(&xi, 0, sizeof(xi));
    XInputGetState(0, &xi);

    left = xi.Gamepad.sThumbLX;
    right = xi.Gamepad.sThumbRX;
    
    // normalize the steering axis
    left = calculate_norm_steering(left, Config.xinput.leftStickDeadzone, Config.xinput.linearSteering, Config.xinput.steeringExponent);
    right = calculate_norm_steering(right, Config.xinput.rightStickDeadzone, Config.xinput.linearSteering, Config.xinput.steeringExponent);

    if (Config.xinput.singleStickSteering) {
        out->wheel = left;
    } else {
        out->wheel = (left + right) / 2;
    }

    out->accel = xi.Gamepad.bRightTrigger << 8;
    out->brake = xi.Gamepad.bLeftTrigger << 8;
}


static void idas_xi_ffb_init(void) {
    return;
}

static void idas_xi_ffb_toggle(bool active) {
    XINPUT_VIBRATION vibration;

    memset(&vibration, 0, sizeof(vibration));

    XInputSetState(0, &vibration);
}

static void idas_xi_ffb_constant_force(uint8_t direction, uint8_t force) {
    return;
}

static void idas_xi_ffb_rumble(uint8_t force, uint8_t period) {
    XINPUT_VIBRATION vibration;
    uint16_t strength = force * 516 * (Config.ffb.rumbleStrength / 100.0f);

    memset(&vibration, 0, sizeof(vibration));
    vibration.wLeftMotorSpeed = strength;
    vibration.wRightMotorSpeed = strength;

    XInputSetState(0, &vibration);
}

static void idas_xi_ffb_damper(uint8_t force) {
    return;
}