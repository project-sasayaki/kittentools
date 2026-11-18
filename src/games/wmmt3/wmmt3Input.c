/* P1: bit 14 service, 7..4 shifter, 1 view change, 0 intrude; test is system bit 7. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <string.h>

#include "wmmt3.h"
#include "wmmt3Input.h"
#include "../../config/config.h"
#include "wmmt3Config.h"
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
#define MAX_GEARS 6

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
static int g_gear_buttons[MAX_GEARS];
static bool g_have_gear_bindings;

static bool gamepad(void)
{
    return Wmmt3Config.input_mode == INPUT_XINPUT;
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
    int directGear = 0;
    bool directGearRead = false;
    if (!gamepad()) {
        uint8_t shifterRaw[128];
        if (g_have_gear_bindings && idas_di_read_shifter_buttons(shifterRaw)) {
            directGearRead = true;
            for (size_t i = 0; i < MAX_GEARS; i++) {
                int button = g_gear_buttons[i];
                if (button > 0 && button <= 128 && (shifterRaw[button - 1] & 0x80)) {
                    directGear = (int)i + 1;
                    break;
                }
            }
        }
    }

    if (directGearRead)
        g_gear = directGear;
    else {
        if (up && !g_shiftUpHeld && g_gear < MAX_GEARS)
            g_gear++;
        if (down && !g_shiftDownHeld && g_gear > 0)
            g_gear--;
    }
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

    if (key_down(Wmmt3Config.service_key) || wheel_button(wheel, Wmmt3Config.dinput_service))
        out->p1 |= SW_SERVICE;
    if (key_down(Wmmt3Config.intrude_key) || wheel_button(wheel, Wmmt3Config.dinput_intrude) ||
        idas_xi_button_held(g_intrudePad))
        out->p1 |= SW_INTRUDE;
    if (buttons & IDAS_IO_GAMEBTN_VIEW_CHANGE)
        out->p1 |= SW_VIEW;

    if (key_down(Wmmt3Config.test_key) || wheel_button(wheel, Wmmt3Config.dinput_test))
        out->system = SYSTEM_TEST;

    static bool cardHeld;
    bool card = key_down(Wmmt3Config.card_key) || wheel_button(wheel, Wmmt3Config.dinput_card);
    if (card && !cardHeld && !wmmt3_card_reader_external())
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

    int wheel = 0x8000 + (state.wheel * Wmmt3Config.steering_range) / STEERING_FULL_RANGE;
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
    bool pressed = (system & IDAS_IO_SYSBTN_COIN) || key_down(Wmmt3Config.coin_key);
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

/* The gamepad backend is shared with other games and reads [xinput] and [ffb]. Its up, down, left and right
 * reach read_switches() as the shifter's gate switches; the cabinet has no start button. */
static void configure_gamepad(void)
{
    Config.xinput.deviceIndex = Wmmt3Config.xinput_device;
    Config.xinput.start = "none";
    Config.xinput.viewChg = Wmmt3Config.xinput_view_change;
    Config.xinput.up = Wmmt3Config.xinput_lever_up;
    Config.xinput.down = Wmmt3Config.xinput_lever_down;
    Config.xinput.left = Wmmt3Config.xinput_lever_left;
    Config.xinput.right = Wmmt3Config.xinput_lever_right;
    Config.xinput.shiftDn = Wmmt3Config.xinput_shift_down;
    Config.xinput.shiftUp = Wmmt3Config.xinput_shift_up;
    Config.xinput.accel = Wmmt3Config.xinput_accel;
    Config.xinput.brake = Wmmt3Config.xinput_brake;
    Config.xinput.coin = Wmmt3Config.xinput_coin;
    Config.xinput.singleStickSteering = Wmmt3Config.xinput_single_stick_steering;
    Config.xinput.linearSteering = Wmmt3Config.xinput_linear_steering;
    Config.xinput.steeringExponent = Wmmt3Config.xinput_steering_exponent;
    Config.xinput.leftStickDeadzone = Wmmt3Config.xinput_left_deadzone;
    Config.xinput.rightStickDeadzone = Wmmt3Config.xinput_right_deadzone;

    int rumble = Wmmt3Config.xinput_rumble;
    Config.ffb.rumbleStrength = rumble < 0 ? 0 : rumble > 100 ? 100 : rumble;
}

/* The steering wheel backend is shared with other games and reads [dinput] and [ffb]. As on the
 * gamepad, its up, down, left and right are the shifter lever, and there is no start button. */
static void configure_wheel(void)
{
    Config.dinput.deviceName = Wmmt3Config.dinput_wheel;
    Config.dinput.pedalsName = Wmmt3Config.dinput_pedals;
    Config.dinput.shifterName = Wmmt3Config.dinput_shifter;
    Config.dinput.accelAxis = Wmmt3Config.dinput_accel_axis;
    Config.dinput.brakeAxis = Wmmt3Config.dinput_brake_axis;
    Config.dinput.reverseAccelAxis = Wmmt3Config.dinput_reverse_accel_axis;
    Config.dinput.reverseBrakeAxis = Wmmt3Config.dinput_reverse_brake_axis;
    Config.dinput.start = 0;
    Config.dinput.viewChg = Wmmt3Config.dinput_view_change;
    Config.dinput.shiftDn = Wmmt3Config.dinput_shift_down;
    Config.dinput.shiftUp = Wmmt3Config.dinput_shift_up;
    Config.dinput.up = Wmmt3Config.dinput_lever_up;
    Config.dinput.down = Wmmt3Config.dinput_lever_down;
    Config.dinput.left = Wmmt3Config.dinput_lever_left;
    Config.dinput.right = Wmmt3Config.dinput_lever_right;
    Config.dinput.coin = Wmmt3Config.dinput_coin;

    Config.ffb.constantForceStrength = Wmmt3Config.ffb_constant_force;
    Config.ffb.damperStrength = Wmmt3Config.ffb_damper;
    Config.ffb.baseDamperStrength = Wmmt3Config.ffb_base_damper;
    Config.ffb.deadBand = Wmmt3Config.ffb_dead_band;
}

void wmmt3_input_init(void)
{
    g_have_gear_bindings = false;
    g_gear_buttons[0] = Wmmt3Config.dinput_gear1;
    g_gear_buttons[1] = Wmmt3Config.dinput_gear2;
    g_gear_buttons[2] = Wmmt3Config.dinput_gear3;
    g_gear_buttons[3] = Wmmt3Config.dinput_gear4;
    g_gear_buttons[4] = Wmmt3Config.dinput_gear5;
    g_gear_buttons[5] = Wmmt3Config.dinput_gear6;
    for (size_t i = 0; i < MAX_GEARS; i++) {
        if (g_gear_buttons[i] > 0) {
            g_have_gear_bindings = true;
            break;
        }
    }

    initJVS(NAMCO_FCA1);
    getJVSIO()->ops = &g_ops;
    install_calibration();

    if (gamepad())
    {
        configure_gamepad();
        idas_xi_init(&g_backend);
        g_intrudePad = idas_xi_button_mask(Wmmt3Config.xinput_intrude, "[wmmt3] xinput_intrude");
    }
    else
    {
        configure_wheel();
        idas_di_init(&g_backend);
        idas_di_config_apply();
    }
    log_info(plfWmmt3, "Controls: %s", gamepad() ? "XInput gamepad" : "DirectInput steering wheel");
}
