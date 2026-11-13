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
#include "../../elfLoader/symbol.h"

#define SW_SERVICE (1u << 14)
#define SW_UP      (1u << 7)
#define SW_DOWN    (1u << 6)
#define SW_LEFT    (1u << 5)
#define SW_RIGHT   (1u << 4)
#define SW_VIEW    (1u << 1)
#define SW_INTRUDE (1u << 0)

#define ACCEL_MIN 35000
#define ACCEL_MAX 55480
#define BRAKE_MIN 29000
#define BRAKE_MAX 49480
#define STEERING_CENTRE 0x7FFF
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
    uint8_t raw[128];
    bool haveRaw = Config.io3.mode != INPUT_XINPUT && idas_di_read_buttons(raw);

    bool up = shifter & IDAS_IO_GAMEBTN_SHIFTUP;
    bool down = shifter & IDAS_IO_GAMEBTN_SHIFTDN;
    int directGear = 0;
    bool directGearRead = false;
    if (Config.io3.mode != INPUT_XINPUT) {
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

#define WHEEL_BUTTON(n) (haveRaw && (n) > 0 && (n) <= 128 && (raw[(n) - 1] & 0x80))

    if ((Config.io3.service && (GetAsyncKeyState(Config.io3.service) & 0x8000)) ||
        WHEEL_BUTTON(Config.wmmt3.service_button))
        out->p1 |= SW_SERVICE;
    if (WHEEL_BUTTON(Config.wmmt3.intrude_button))
        out->p1 |= SW_INTRUDE;
    if (buttons & IDAS_IO_GAMEBTN_VIEW_CHANGE)
        out->p1 |= SW_VIEW;

    if ((Config.io3.test && (GetAsyncKeyState(Config.io3.test) & 0x8000)) ||
        WHEEL_BUTTON(Config.wmmt3.test_button))
        out->system = 0x80;

    {
        static bool cardHeld;
        bool down = Config.wmmt3.card_key && (GetAsyncKeyState(Config.wmmt3.card_key) & 0x8000);
        if (Config.wmmt3.card_button > 0 && Config.io3.mode != INPUT_XINPUT)
            down = down || WHEEL_BUTTON(Config.wmmt3.card_button);
        if (down && !cardHeld)
            namcoCardReaderRequestInsert();
        cardHeld = down;
    }
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

    int wheel = 0x8000 + (state.wheel * Config.io3.restrict_) / 128;
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
    bool pressed = (system & IDAS_IO_SYSBTN_COIN) ||
                   (Config.io3.coin && (GetAsyncKeyState(Config.io3.coin) & 0x8000));
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

void wmmt3_input_init(void)
{
    g_have_gear_bindings = false;
    g_gear_buttons[0] = Config.dinput.gear1;
    g_gear_buttons[1] = Config.dinput.gear2;
    g_gear_buttons[2] = Config.dinput.gear3;
    g_gear_buttons[3] = Config.dinput.gear4;
    g_gear_buttons[4] = Config.dinput.gear5;
    g_gear_buttons[5] = Config.dinput.gear6;
    for (size_t i = 0; i < MAX_GEARS; i++) {
        if (g_gear_buttons[i] > 0) {
            g_have_gear_bindings = true;
            break;
        }
    }

    initJVS(NAMCO_FCA1);
    getJVSIO()->ops = &g_ops;
    install_calibration();

    if (Config.io3.mode == INPUT_XINPUT)
        idas_xi_init(&g_backend);
    else
    {
        idas_di_init(&g_backend);
        idas_di_config_apply();
    }
    log_info(plfWmmt3, "Controls: %s", Config.io3.mode == INPUT_XINPUT ? "XInput" : "DirectInput");
}
