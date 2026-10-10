#include <windows.h>
#include <xinput.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include "backend.h"
#include "idas_input.h"
#include "../config/config.h"
#include "../log/log.h"

enum {
    IDAS_XI_AXIS_TRIGGER_L = 0,
    IDAS_XI_AXIS_TRIGGER_R = 1,
    IDAS_XI_AXIS_BUTTON    = 2,
};

struct idas_xi_axis_map {
    int      kind;
    uint32_t btn_mask;
};

static void idas_xi_jvs_read_buttons(uint8_t *gamebtn_out);
static void idas_xi_jvs_read_shifter(uint8_t *gear_out);
static void idas_xi_jvs_read_analogs(struct idas_io_analog_state *out);
static void idas_xi_jvs_read_system(uint8_t *sysbtn_out);
static void idas_xi_ffb_init(void);
static void idas_xi_ffb_toggle(bool active);
static void idas_xi_ffb_constant_force(uint8_t direction, uint8_t force);
static void idas_xi_ffb_rumble(uint8_t period, uint8_t force);
static void idas_xi_ffb_damper(uint8_t force);
static HRESULT idas_xi_config_apply(void);
static uint32_t idas_xi_parse_button_map(const char *spec, const char *key);
static struct idas_xi_axis_map idas_xi_parse_axis_map(const char *spec, const char *key);
static uint16_t idas_xi_read_axis(const XINPUT_GAMEPAD *pad, const struct idas_xi_axis_map *m);
static bool idas_xi_map_pressed(const XINPUT_GAMEPAD *pad, uint32_t mask);


static const struct idas_io_backend idas_xi_backend = {
    .jvs_read_buttons   = idas_xi_jvs_read_buttons,
    .jvs_read_shifter   = idas_xi_jvs_read_shifter,
    .jvs_read_analogs   = idas_xi_jvs_read_analogs,
    .jvs_read_system    = idas_xi_jvs_read_system,
    .ffb_init           = idas_xi_ffb_init,
    .ffb_req_toggle     = idas_xi_ffb_toggle,
    .ffb_req_constant_force = idas_xi_ffb_constant_force,
    .ffb_req_rumble     = idas_xi_ffb_rumble,
    .ffb_req_damper     = idas_xi_ffb_damper,
};

const uint16_t max_stick_value = 32767;
/* Apply steering wheel restriction. Real cabs only report about 76% of
    the output value when the wheel is turned to either of its maximum positions. */
const uint16_t max_wheel_value = 24831;

/* XInput reports the triggers as separate bytes and not as part of wButtons,
   so they get pseudo bits above the 16 real button bits. */
#define IDAS_XI_BTN_LEFT_TRIGGER  0x10000u
#define IDAS_XI_BTN_RIGHT_TRIGGER 0x20000u

/* Missing from older SDK versions, and only ever reported by XInputGetStateEx */
#ifndef XINPUT_GAMEPAD_GUIDE
#define XINPUT_GAMEPAD_GUIDE 0x0400
#endif

struct idas_xi_button_name {
    const char *name;
    uint32_t mask;
};

static const struct idas_xi_button_name idas_xi_button_names[] = {
    { "DPAD_UP",        XINPUT_GAMEPAD_DPAD_UP },
    { "UP",             XINPUT_GAMEPAD_DPAD_UP },
    { "DPAD_DOWN",      XINPUT_GAMEPAD_DPAD_DOWN },
    { "DOWN",           XINPUT_GAMEPAD_DPAD_DOWN },
    { "DPAD_LEFT",      XINPUT_GAMEPAD_DPAD_LEFT },
    { "LEFT",           XINPUT_GAMEPAD_DPAD_LEFT },
    { "DPAD_RIGHT",     XINPUT_GAMEPAD_DPAD_RIGHT },
    { "RIGHT",          XINPUT_GAMEPAD_DPAD_RIGHT },
    { "START",          XINPUT_GAMEPAD_START },
    { "BACK",           XINPUT_GAMEPAD_BACK },
    { "SELECT",         XINPUT_GAMEPAD_BACK },
    /* GUIDE is only reported by the undocumented XInputGetStateEx */
    { "GUIDE",          XINPUT_GAMEPAD_GUIDE },
    { "LTHUMB",         XINPUT_GAMEPAD_LEFT_THUMB },
    { "LS",             XINPUT_GAMEPAD_LEFT_THUMB },
    { "LEFT_THUMB",     XINPUT_GAMEPAD_LEFT_THUMB },
    { "RTHUMB",         XINPUT_GAMEPAD_RIGHT_THUMB },
    { "RS",             XINPUT_GAMEPAD_RIGHT_THUMB },
    { "RIGHT_THUMB",    XINPUT_GAMEPAD_RIGHT_THUMB },
    { "LB",             XINPUT_GAMEPAD_LEFT_SHOULDER },
    { "LEFT_SHOULDER",  XINPUT_GAMEPAD_LEFT_SHOULDER },
    { "RB",             XINPUT_GAMEPAD_RIGHT_SHOULDER },
    { "RIGHT_SHOULDER", XINPUT_GAMEPAD_RIGHT_SHOULDER },
    { "LT",             IDAS_XI_BTN_LEFT_TRIGGER },
    { "LEFT_TRIGGER",   IDAS_XI_BTN_LEFT_TRIGGER },
    { "RT",             IDAS_XI_BTN_RIGHT_TRIGGER },
    { "RIGHT_TRIGGER",  IDAS_XI_BTN_RIGHT_TRIGGER },
    { "A",              XINPUT_GAMEPAD_A },
    { "B",              XINPUT_GAMEPAD_B },
    { "X",              XINPUT_GAMEPAD_X },
    { "Y",              XINPUT_GAMEPAD_Y },
};

static DWORD idas_xi_device_index;
static uint32_t idas_xi_btn_start;
static uint32_t idas_xi_btn_view_chg;
static uint32_t idas_xi_btn_up;
static uint32_t idas_xi_btn_down;
static uint32_t idas_xi_btn_left;
static uint32_t idas_xi_btn_right;
static uint32_t idas_xi_btn_shift_dn;
static uint32_t idas_xi_btn_shift_up;
static uint32_t idas_xi_btn_coin;

/* Throttle/brake axis bindings, parsed from [xinput] accel/brake. */
static struct idas_xi_axis_map idas_xi_accel_map;
static struct idas_xi_axis_map idas_xi_brake_map;

static bool idas_xi_token_to_mask(const char *token, uint32_t *mask_out)
{
    size_t i;

    assert(token != NULL);
    assert(mask_out != NULL);

    if (_stricmp(token, "none") == 0) {
        *mask_out = 0;

        return true;
    }

    for (i = 0 ; i < _countof(idas_xi_button_names) ; i++) {
        if (_stricmp(token, idas_xi_button_names[i].name) == 0) {
            *mask_out = idas_xi_button_names[i].mask;
            return true;
        }
    }

    *mask_out = 0;

    return false;
}

static uint32_t idas_xi_parse_button_map(const char *spec, const char *key)
{
    uint32_t mask;

    assert(spec != NULL);

    if (idas_xi_token_to_mask(spec, &mask)) {
        return mask;
    }

    log_error(plfBoot, "XInput: Unknown button \"%s\" for \"%s\", mapping disabled",
            spec, key);

    return 0;
}

static struct idas_xi_axis_map idas_xi_parse_axis_map(const char *spec, const char *key)
{
    struct idas_xi_axis_map map = { IDAS_XI_AXIS_BUTTON, 0 };
    uint32_t mask;

    if (spec != NULL && idas_xi_token_to_mask(spec, &mask)) {
        if (mask == IDAS_XI_BTN_LEFT_TRIGGER) {
            map.kind = IDAS_XI_AXIS_TRIGGER_L;
        } else if (mask == IDAS_XI_BTN_RIGHT_TRIGGER) {
            map.kind = IDAS_XI_AXIS_TRIGGER_R;
        } else {
            map.kind = IDAS_XI_AXIS_BUTTON;
            map.btn_mask = mask;
        }

        return map;
    }

    log_error(plfBoot, "XInput: Invalid %s mapping \"%s\", defaulting to %s",
            key, spec != NULL ? spec : "(null)",
            strcmp(key, "accel") == 0 ? "RT" : "LT");

    if (strcmp(key, "accel") == 0) {
        map.kind = IDAS_XI_AXIS_TRIGGER_R;
    } else {
        map.kind = IDAS_XI_AXIS_TRIGGER_L;
    }

    return map;
}

static bool idas_xi_map_pressed(const XINPUT_GAMEPAD *pad, uint32_t mask)
{
    assert(pad != NULL);

    /* The triggers are not part of wButtons */
    if (mask == IDAS_XI_BTN_LEFT_TRIGGER) {
        return pad->bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    }

    if (mask == IDAS_XI_BTN_RIGHT_TRIGGER) {
        return pad->bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    }

    return (pad->wButtons & mask) != 0;
}

static uint16_t idas_xi_read_axis(const XINPUT_GAMEPAD *pad, const struct idas_xi_axis_map *m)
{
    assert(pad != NULL);
    assert(m != NULL);

    switch (m->kind) {
    case IDAS_XI_AXIS_TRIGGER_L:
        return (uint16_t) pad->bLeftTrigger << 8;
    case IDAS_XI_AXIS_TRIGGER_R:
        return (uint16_t) pad->bRightTrigger << 8;
    case IDAS_XI_AXIS_BUTTON:
        return idas_xi_map_pressed(pad, m->btn_mask) ? 0xFFFF : 0;
    default:
        return 0;
    }
}

static bool idas_xi_get_state(XINPUT_STATE *out)
{
    assert(out != NULL);

    memset(out, 0, sizeof(*out));

    return XInputGetState(idas_xi_device_index, out) == ERROR_SUCCESS;
}

static HRESULT idas_xi_config_apply(void)
{
    int index;
    XINPUT_STATE xi;

    index = Config.xinput.deviceIndex;

    if (index < 0 || index >= XUSER_MAX_COUNT) {
        log_warn(plfBoot, "XInput: Invalid device index %i, using 0", index);

        index = 0;
    }

    idas_xi_device_index = (DWORD) index;

    idas_xi_btn_start = idas_xi_parse_button_map(Config.xinput.start, "start");
    idas_xi_btn_view_chg = idas_xi_parse_button_map(Config.xinput.viewChg, "viewChg");
    idas_xi_btn_up = idas_xi_parse_button_map(Config.xinput.up, "up");
    idas_xi_btn_down = idas_xi_parse_button_map(Config.xinput.down, "down");
    idas_xi_btn_left = idas_xi_parse_button_map(Config.xinput.left, "left");
    idas_xi_btn_right = idas_xi_parse_button_map(Config.xinput.right, "right");
    idas_xi_btn_shift_dn = idas_xi_parse_button_map(Config.xinput.shiftDn, "shiftDn");
    idas_xi_btn_shift_up = idas_xi_parse_button_map(Config.xinput.shiftUp, "shiftUp");
    idas_xi_btn_coin = idas_xi_parse_button_map(Config.xinput.coin, "coin");

    idas_xi_accel_map = idas_xi_parse_axis_map(Config.xinput.accel, "accel");
    idas_xi_brake_map = idas_xi_parse_axis_map(Config.xinput.brake, "brake");

    log_info(plfBoot, "XInput: --- Begin configuration ---");
    log_info(plfBoot, "XInput: Device index . . . . : %i", (int) idas_xi_device_index);
    log_info(plfBoot, "XInput: Start button . . . . : %s (0x%04X)",
            Config.xinput.start, idas_xi_btn_start);
    log_info(plfBoot, "XInput: View Change button . : %s (0x%04X)",
            Config.xinput.viewChg, idas_xi_btn_view_chg);
    log_info(plfBoot, "XInput: Up button  . . . . . : %s (0x%04X)",
            Config.xinput.up, idas_xi_btn_up);
    log_info(plfBoot, "XInput: Down button  . . . . : %s (0x%04X)",
            Config.xinput.down, idas_xi_btn_down);
    log_info(plfBoot, "XInput: Left button  . . . . : %s (0x%04X)",
            Config.xinput.left, idas_xi_btn_left);
    log_info(plfBoot, "XInput: Right button . . . . : %s (0x%04X)",
            Config.xinput.right, idas_xi_btn_right);
    log_info(plfBoot, "XInput: Shift Down button  . : %s (0x%04X)",
            Config.xinput.shiftDn, idas_xi_btn_shift_dn);
    log_info(plfBoot, "XInput: Shift Up button . . : %s (0x%04X)",
            Config.xinput.shiftUp, idas_xi_btn_shift_up);
    log_info(plfBoot, "XInput: Coin button . . . . .: %s (0x%04X)",
            Config.xinput.coin, idas_xi_btn_coin);
    log_info(plfBoot, "XInput: Accel source  . . . . : %s",
            Config.xinput.accel);
    log_info(plfBoot, "XInput: Brake source  . . . . : %s",
            Config.xinput.brake);
    log_info(plfBoot, "XInput: ---  End  configuration ---");

    /* Only warn here, the controller may be plugged in later on */
    if (!idas_xi_get_state(&xi)) {
        log_warn(plfBoot, "XInput: Controller %i is not connected", (int) idas_xi_device_index);
    }

    return S_OK;
}

void idas_xi_init(const struct idas_io_backend **backend)
{
    HRESULT hr;

    assert(backend != NULL);

    hr = idas_xi_config_apply();

    if (FAILED(hr)) {
        return;
    }

    log_info(plfBoot, "XInput controller initialized");
    *backend = &idas_xi_backend;
}

static void idas_xi_jvs_read_buttons(uint8_t *gamebtn_out)
{
    uint8_t gamebtn;
    XINPUT_STATE xi;

    assert(gamebtn_out != NULL);

    gamebtn = 0;

    if (idas_xi_get_state(&xi)) {
        if (idas_xi_map_pressed(&xi.Gamepad, idas_xi_btn_up)) {
            gamebtn |= IDAS_IO_GAMEBTN_UP;
        }

        if (idas_xi_map_pressed(&xi.Gamepad, idas_xi_btn_down)) {
            gamebtn |= IDAS_IO_GAMEBTN_DOWN;
        }

        if (idas_xi_map_pressed(&xi.Gamepad, idas_xi_btn_left)) {
            gamebtn |= IDAS_IO_GAMEBTN_LEFT;
        }

        if (idas_xi_map_pressed(&xi.Gamepad, idas_xi_btn_right)) {
            gamebtn |= IDAS_IO_GAMEBTN_RIGHT;
        }

        if (idas_xi_map_pressed(&xi.Gamepad, idas_xi_btn_start)) {
            gamebtn |= IDAS_IO_GAMEBTN_START;
        }

        if (idas_xi_map_pressed(&xi.Gamepad, idas_xi_btn_view_chg)) {
            gamebtn |= IDAS_IO_GAMEBTN_VIEW_CHANGE;
        }
    }

    *gamebtn_out = gamebtn;
}

static void idas_xi_jvs_read_shifter(uint8_t *gear_out)
{
    uint8_t gear = 0;
    XINPUT_STATE xi;

    assert(gear_out != NULL);

    if (idas_xi_get_state(&xi)) {
        if (idas_xi_map_pressed(&xi.Gamepad, idas_xi_btn_shift_dn)) {
            gear |= IDAS_IO_GAMEBTN_SHIFTDN;
        }

        if (idas_xi_map_pressed(&xi.Gamepad, idas_xi_btn_shift_up)) {
            gear |= IDAS_IO_GAMEBTN_SHIFTUP;
        }
    }

    *gear_out = gear;
}

static void idas_xi_jvs_read_system(uint8_t *sysbtn_out)
{
    uint8_t sysbtn;
    XINPUT_STATE xi;

    assert(sysbtn_out != NULL);

    sysbtn = 0;

    if (idas_xi_get_state(&xi)) {
        if (idas_xi_map_pressed(&xi.Gamepad, idas_xi_btn_coin)) {
            sysbtn |= IDAS_IO_SYSBTN_COIN;
        }
    }

    *sysbtn_out = sysbtn;
}

static int16_t calculate_norm_steering(int16_t axis, uint16_t deadzone, bool linear_steering, int exponent) {
    /* Guard against axis == 0: norm_axis below would be 0/0 = NaN, and
       (int16_t)NaN is undefined behavior. The stick is centered anyway. */
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

    /* Choose the response curve exponent. linear_steering keeps the legacy
       linear mapping (exponent 1) for backward compatibility; otherwise the
       configurable exponent applies. A higher exponent gives a flatter center
       (less sensitive around neutral, easier to hold a straight line) and a
       steeper edge, more like a real steering wheel. */
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

    if (!idas_xi_get_state(&xi)) {
        return;
    }

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

    out->accel = idas_xi_read_axis(&xi.Gamepad, &idas_xi_accel_map);
    out->brake = idas_xi_read_axis(&xi.Gamepad, &idas_xi_brake_map);
}


static void idas_xi_ffb_init(void) {
    return;
}

static void idas_xi_ffb_toggle(bool active) {
    XINPUT_VIBRATION vibration;

    memset(&vibration, 0, sizeof(vibration));

    XInputSetState(idas_xi_device_index, &vibration);
}

static void idas_xi_ffb_constant_force(uint8_t direction, uint8_t force) {
    return;
}

static void idas_xi_ffb_rumble(uint8_t period, uint8_t force) {
    XINPUT_VIBRATION vibration;
    uint16_t strength = force * 516 * (Config.ffb.rumbleStrength / 100.0f);

    memset(&vibration, 0, sizeof(vibration));
    vibration.wLeftMotorSpeed = strength;
    vibration.wRightMotorSpeed = strength;

    XInputSetState(idas_xi_device_index, &vibration);
}

static void idas_xi_ffb_damper(uint8_t force) {
    return;
}
