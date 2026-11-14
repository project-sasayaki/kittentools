/* P1: bit 14 service, 7..4 shifter, 1 view change, 0 intrude; test is system bit 7. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "wmmt3.h"
#include "wmmt3Input.h"
#include "../../config/config.h"
#include "../../devices/jvs.h"
#include "../../devices/namcoCardReader.h"
#include "../../input/backend.h"
#include "../../input/idas_di.h"
#include "../../input/idas_input.h"
#include "../../input/idas_xi.h"
#include "../../log/log.h"

#define SW_SERVICE (1u << 14)
#define SW_UP      (1u << 7)
#define SW_DOWN    (1u << 6)
#define SW_LEFT    (1u << 5)
#define SW_RIGHT   (1u << 4)
#define SW_VIEW    (1u << 1)
#define SW_INTRUDE (1u << 0)
#define SYSTEM_TEST 0x80

#define ACCEL_MIN 35000
#define ACCEL_MAX 55480
#define BRAKE_MIN 29000
#define BRAKE_MAX 49480
#define STEERING_CENTRE 0x7FFF
#define STEERING_FULL_RANGE 128

#define WHEEL_BUTTONS 128

/* The calibration this board is built around; a dump's testmode.lua values
 * leave the pedals half pressed at rest. */
static int calibration_gas(const void *self) { (void)self; return ACCEL_MIN; }
static int calibration_brake(const void *self) { (void)self; return BRAKE_MIN; }
static int calibration_steering(const void *self) { (void)self; return STEERING_CENTRE; }

static void install_calibration(void)
{
    static const struct { const char *name; void *fn; } hooks[] = {
        {"_ZNK10clTestMode17getCalibrationGasEv", (void *)calibration_gas},
        {"_ZNK10clTestMode19getCalibrationBrakeEv", (void *)calibration_brake},
        {"_ZNK10clTestMode22getCalibrationSteeringEv", (void *)calibration_steering},
    };
    int installed = 0;
    for (size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++)
        installed += wmmt3_hook(hooks[i].name, hooks[i].fn, NULL);
    log_info(plfWmmt3, "Calibration: %d of 3 answered (gas %d, brake %d, steering centre %d)",
             installed, ACCEL_MIN, BRAKE_MIN, STEERING_CENTRE);
}

static const struct idas_io_backend *g_backend;
static uint32_t g_intrudePad;      /* gamepad only */
static volatile uint16_t g_wheel = 0x8000;

uint16_t wmmt3_input_wheel_position(void)
{
    return (uint16_t)((uint32_t)g_wheel * 1023 / 65535); /* centre 0x1ff */
}
static int g_gear;          /* 0 neutral, 1..6 */
static bool g_shiftUpHeld, g_shiftDownHeld;
static bool g_coinHeld;
static uint16_t g_coins;

static bool gamepad(void)
{
    return Config.wmmt3.input_mode == INPUT_XINPUT;
}

static bool key_down(int virtualKey)
{
    return virtualKey && (GetAsyncKeyState(virtualKey) & 0x8000);
}

/* Numbered from 1; 0 is no button. */
static bool wheel_button(const uint8_t *buttons, int number)
{
    return buttons && number > 0 && number <= WHEEL_BUTTONS && (buttons[number - 1] & 0x80);
}

static uint16_t gear_switches(int gear)
{
    switch (gear)
    {
        case 1: return SW_LEFT | SW_UP;
        case 2: return SW_LEFT | SW_DOWN;
        case 3: return SW_UP;
        case 4: return SW_DOWN;
        case 5: return SW_RIGHT | SW_UP;
        case 6: return SW_RIGHT | SW_DOWN;
        default: return 0;
    }
}

static void read_switches(struct io3_switch_state *out)
{
    memset(out, 0, sizeof(*out));
    uint8_t buttons = 0, shifter = 0;
    if (g_backend)
    {
        g_backend->jvs_read_buttons(&buttons);
        g_backend->jvs_read_shifter(&shifter);
    }

    bool up = shifter & IDAS_IO_GAMEBTN_SHIFTUP;
    bool down = shifter & IDAS_IO_GAMEBTN_SHIFTDN;
    if (up && !g_shiftUpHeld && g_gear < 6)
        g_gear++;
    if (down && !g_shiftDownHeld && g_gear > 0)
        g_gear--;
    g_shiftUpHeld = up;
    g_shiftDownHeld = down;

    uint16_t navigation = 0;
    if (buttons & IDAS_IO_GAMEBTN_UP)
        navigation |= SW_UP;
    if (buttons & IDAS_IO_GAMEBTN_DOWN)
        navigation |= SW_DOWN;
    if (buttons & IDAS_IO_GAMEBTN_LEFT)
        navigation |= SW_LEFT;
    if (buttons & IDAS_IO_GAMEBTN_RIGHT)
        navigation |= SW_RIGHT;

    if (navigation)
        g_gear = 0;
    out->p1 = navigation | gear_switches(g_gear);

    uint8_t held[WHEEL_BUTTONS];
    const uint8_t *wheel = !gamepad() && idas_di_read_buttons(held) ? held : NULL;

    if (key_down(Config.wmmt3.service_key) || wheel_button(wheel, Config.wmmt3.dinput_service))
        out->p1 |= SW_SERVICE;
    if (key_down(Config.wmmt3.intrude_key) || wheel_button(wheel, Config.wmmt3.dinput_intrude) ||
        idas_xi_button_held(g_intrudePad))
        out->p1 |= SW_INTRUDE;
    if (buttons & IDAS_IO_GAMEBTN_VIEW_CHANGE)
        out->p1 |= SW_VIEW;

    if (key_down(Config.wmmt3.test_key) || wheel_button(wheel, Config.wmmt3.dinput_test))
        out->system = SYSTEM_TEST;

    static bool cardHeld;
    bool card = key_down(Config.wmmt3.card_key) || wheel_button(wheel, Config.wmmt3.dinput_card);
    if (card && !cardHeld)
        namcoCardReaderRequestInsert();
    cardHeld = card;
}

static uint16_t window(int value, int minimum, int maximum)
{
    if (value < 0)
        value = 0;
    if (value > 65535)
        value = 65535;
    return (uint16_t)(minimum + (long)value * (maximum - minimum) / 65535);
}

static void read_analogs(uint16_t *analogs, uint8_t count)
{
    struct idas_io_analog_state state;
    memset(&state, 0, sizeof(state));
    if (g_backend)
        g_backend->jvs_read_analogs(&state);

    /* KITTEN_WMMT3_KEYBOARD=1 */
    static int keyboard = -1;
    if (keyboard < 0)
    {
        const char *value = getenv("KITTEN_WMMT3_KEYBOARD");
        keyboard = value && *value == '1';
    }
    if (keyboard)
    {
        if (GetAsyncKeyState(VK_UP) & 0x8000)
            state.accel = (int16_t)0xFFFF;
        if (GetAsyncKeyState(VK_DOWN) & 0x8000)
            state.brake = (int16_t)0xFFFF;
        if (GetAsyncKeyState(VK_LEFT) & 0x8000)
            state.wheel = -24000;
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
            state.wheel = 24000;
    }

    int wheel = 0x8000 + (state.wheel * Config.wmmt3.steering_range) / STEERING_FULL_RANGE;
    if (wheel < 0)
        wheel = 0;
    if (wheel > 0xFFFF)
        wheel = 0xFFFF;

    g_wheel = (uint16_t)wheel;
    uint16_t values[3] = {
        (uint16_t)wheel,
        window((uint16_t)state.accel, ACCEL_MIN, ACCEL_MAX),
        window((uint16_t)state.brake, BRAKE_MIN, BRAKE_MAX),
    };
    for (uint8_t i = 0; i < count && i < 8; i++)
        analogs[i] = i < 3 ? values[i] : 0;
}

static void read_coin_counter(uint8_t slot, uint16_t *out)
{
    if (slot > 0)
    {
        *out = 0;
        return;
    }

    uint8_t system = 0;
    if (g_backend && g_backend->jvs_read_system)
        g_backend->jvs_read_system(&system);
    bool pressed = (system & IDAS_IO_SYSBTN_COIN) || key_down(Config.wmmt3.coin_key);
    if (pressed && !g_coinHeld)
        g_coins++;
    g_coinHeld = pressed;
    *out = g_coins;
}

static JVS_OPS g_ops = {
    .read_switches = read_switches,
    .read_analogs = read_analogs,
    .read_coin_counter = read_coin_counter,
};

const struct idas_io_backend *wmmt3_input_backend(void)
{
    return g_backend;
}

/* The gamepad backend is shared with other games and reads [xinput]. */
static void configure_gamepad(void)
{
    Config.xinput.deviceIndex = Config.wmmt3.xinput_device;
    Config.xinput.start = Config.wmmt3.xinput_start;
    Config.xinput.viewChg = Config.wmmt3.xinput_view_change;
    Config.xinput.up = Config.wmmt3.xinput_up;
    Config.xinput.down = Config.wmmt3.xinput_down;
    Config.xinput.left = Config.wmmt3.xinput_left;
    Config.xinput.right = Config.wmmt3.xinput_right;
    Config.xinput.shiftDn = Config.wmmt3.xinput_shift_down;
    Config.xinput.shiftUp = Config.wmmt3.xinput_shift_up;
    Config.xinput.accel = Config.wmmt3.xinput_accel;
    Config.xinput.brake = Config.wmmt3.xinput_brake;
    Config.xinput.coin = Config.wmmt3.xinput_coin;
    Config.xinput.singleStickSteering = Config.wmmt3.xinput_single_stick_steering;
    Config.xinput.linearSteering = Config.wmmt3.xinput_linear_steering;
    Config.xinput.steeringExponent = Config.wmmt3.xinput_steering_exponent;
    Config.xinput.leftStickDeadzone = Config.wmmt3.xinput_left_deadzone;
    Config.xinput.rightStickDeadzone = Config.wmmt3.xinput_right_deadzone;
}

/* The steering wheel backend is shared with other games and reads [dinput] and [ffb]. */
static void configure_wheel(void)
{
    Config.dinput.deviceName = Config.wmmt3.dinput_wheel;
    Config.dinput.pedalsName = Config.wmmt3.dinput_pedals;
    Config.dinput.shifterName = Config.wmmt3.dinput_shifter;
    Config.dinput.accelAxis = Config.wmmt3.dinput_accel_axis;
    Config.dinput.brakeAxis = Config.wmmt3.dinput_brake_axis;
    Config.dinput.reverseAccelAxis = Config.wmmt3.dinput_reverse_accel_axis;
    Config.dinput.reverseBrakeAxis = Config.wmmt3.dinput_reverse_brake_axis;
    Config.dinput.start = Config.wmmt3.dinput_start;
    Config.dinput.viewChg = Config.wmmt3.dinput_view_change;
    Config.dinput.shiftDn = Config.wmmt3.dinput_shift_down;
    Config.dinput.shiftUp = Config.wmmt3.dinput_shift_up;
    Config.dinput.up = Config.wmmt3.dinput_up;
    Config.dinput.down = Config.wmmt3.dinput_down;
    Config.dinput.left = Config.wmmt3.dinput_left;
    Config.dinput.right = Config.wmmt3.dinput_right;
    Config.dinput.coin = Config.wmmt3.dinput_coin;

    Config.ffb.constantForceStrength = Config.wmmt3.ffb_constant_force;
    Config.ffb.damperStrength = Config.wmmt3.ffb_damper;
    Config.ffb.baseDamperStrength = Config.wmmt3.ffb_base_damper;
    Config.ffb.deadBand = Config.wmmt3.ffb_dead_band;
}

void wmmt3_input_init(void)
{
    initJVS(NAMCO_FCA1);
    getJVSIO()->ops = &g_ops;
    install_calibration();

    if (gamepad())
    {
        configure_gamepad();
        idas_xi_init(&g_backend);
        g_intrudePad = idas_xi_button_mask(Config.wmmt3.xinput_intrude, "[wmmt3] xinput_intrude");
    }
    else
    {
        configure_wheel();
        idas_di_init(&g_backend);
        idas_di_config_apply();
    }
    log_info(plfWmmt3, "Controls: %s", gamepad() ? "XInput gamepad" : "DirectInput steering wheel");
}
