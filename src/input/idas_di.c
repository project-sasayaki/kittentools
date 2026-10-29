#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <assert.h>
#include "backend.h"
#include "idas_input.h"
#include "idas_di.h"
#include "../config/config.h"
#include "../log/log.h"

static uint8_t idas_di_decode_pov(DWORD pov);
static void idas_di_jvs_read_buttons(uint8_t *gamebtn_out);
static void idas_di_jvs_read_shifter(uint8_t *gear_out);
static void idas_di_jvs_read_analogs(struct idas_io_analog_state *out);
static void idas_di_jvs_read_system(uint8_t *sysbtn_out);
static void idas_di_ffb_init(void);
static void idas_di_ffb_toggle(bool active);
static void idas_di_ffb_constant_force(uint8_t direction, uint8_t force);
static void idas_di_ffb_rumble(uint8_t force, uint8_t period);
static void idas_di_ffb_damper(uint8_t force);

static BOOL CALLBACK idas_di_enum_callback(const DIDEVICEINSTANCEA *dev, void *data);
static BOOL CALLBACK idas_di_enum_callback_pedals(const DIDEVICEINSTANCEA *dev, void *data);
static BOOL CALLBACK idas_di_enum_callback_shifter(const DIDEVICEINSTANCEA *dev, void *data);
HRESULT idas_di_dev_start(IDirectInputDevice8A *dev);
static void update_or_create_effect(LPDIRECTINPUTEFFECT* effect_ptr, REFGUID guid, DIEFFECT* fx);

struct idas_di_axis {
    char name[4];
    size_t off;
};

union idas_di_state {
    DIJOYSTATE2 st;
    uint8_t bytes[sizeof(DIJOYSTATE2)];
};

static const struct idas_di_axis idas_di_axes[] = {
    { .name = "X",     .off = DIJOFS_X },
    { .name = "Y",     .off = DIJOFS_Y },
    { .name = "Z",     .off = DIJOFS_Z },
    { .name = "RX",    .off = DIJOFS_RX },
    { .name = "RY",    .off = DIJOFS_RY },
    { .name = "RZ",    .off = DIJOFS_RZ },
    { .name = "U",     .off = DIJOFS_SLIDER(0) },
    { .name = "V",     .off = DIJOFS_SLIDER(1) },
};

static const struct idas_io_backend idas_di_backend = {
    .jvs_read_buttons       = idas_di_jvs_read_buttons,
    .jvs_read_shifter       = idas_di_jvs_read_shifter,
    .jvs_read_analogs       = idas_di_jvs_read_analogs,
    .jvs_read_system        = idas_di_jvs_read_system,
    .ffb_init               = idas_di_ffb_init,
    .ffb_req_toggle         = idas_di_ffb_toggle,
    .ffb_req_constant_force = idas_di_ffb_constant_force,
    .ffb_req_rumble         = idas_di_ffb_rumble,
    .ffb_req_damper         = idas_di_ffb_damper
};

static HWND idas_di_wnd;
static IDirectInput8A *idas_di_api;
static IDirectInputDevice8A *idas_di_dev;
static IDirectInputDevice8A *idas_di_pedals;
static IDirectInputDevice8A *idas_di_shifter;
static size_t idas_di_off_brake;
static size_t idas_di_off_accel;
static int idas_di_shift_dn;
static int idas_di_shift_up;
static int idas_di_view_chg;
static int idas_di_start;
static int idas_di_up;
static int idas_di_down;
static int idas_di_left;
static int idas_di_right;
static int idas_di_coin;
static bool idas_di_use_pedals;
static bool idas_di_reverse_brake_axis;
static bool idas_di_reverse_accel_axis;

/* Individual DI Effects */
static IDirectInputEffect *idas_di_fx;
static IDirectInputEffect *idas_di_fx_spring;
static IDirectInputEffect *idas_di_fx_rumble;
static IDirectInputEffect *idas_di_fx_damper;

/* Max FFB Board value is 127 */
static const double idas_di_ffb_scale = 127.0;
static const double idas_di_damper_spring_ratio = 0.5;

inline bool str_ieq(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return lhs == rhs;
    }

    return _stricmp(lhs, rhs) == 0;
}

HRESULT io_wnd_create(char *lpClassName, HINSTANCE inst, HWND *out)
{
    HRESULT hr;
    WNDCLASSEXA wcx;  
    ATOM atom;
    HWND hwnd;

    assert(inst != NULL); 
    assert(out != NULL);

    *out = NULL;

    memset(&wcx, 0, sizeof(wcx));
    wcx.cbSize = sizeof(wcx);
    wcx.lpfnWndProc = DefWindowProcA;
    wcx.hInstance = inst;
    wcx.lpszClassName = lpClassName; 

    atom = RegisterClassExA(&wcx);  

    if (atom == 0) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        log_error(plfBoot, "RegisterClassExA failed: %08x", (int) hr);
        return hr;
    }

    hwnd = CreateWindowExA(  
            0,
            (char *) (intptr_t) atom, 
            lpClassName, 
            0,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            HWND_MESSAGE,
            NULL,
            inst,
            NULL);

    if (hwnd == NULL) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        log_error(plfBoot, "CreateWindowExA failed: %08x", (int) hr);
        return hr;
    }

    *out = hwnd;

    return S_OK;
}

HRESULT idas_di_init(const struct idas_io_backend **backend)
{
    HINSTANCE inst = GetModuleHandle(NULL);
    HRESULT hr;

    hr = idas_di_config_apply();

    if (FAILED(hr)) {
        return hr;
    }

    assert(backend != NULL);

    *backend = NULL;

    hr = io_wnd_create("IDASIO", inst, &idas_di_wnd);

    if (FAILED(hr)) {
        return hr;
    }

    hr = DirectInput8Create(
        inst, 
        DIRECTINPUT_VERSION,
        &IID_IDirectInput8,
        (void **)&idas_di_api, 
        NULL); 

    if (FAILED(hr)) {
        log_error(plfBoot, "DirectInput: API create failed: %08x", (int) hr);
        return hr;
    }

    hr = IDirectInput8_EnumDevices(
            idas_di_api,
            DI8DEVCLASS_GAMECTRL,
            idas_di_enum_callback,
            NULL,
            DIEDFL_ATTACHEDONLY);

    if (FAILED(hr)) {
        log_error(plfBoot, "DirectInput: EnumDevices failed: %08x", (int) hr);
        return hr;
    }

    if (idas_di_dev == NULL) {
        log_error(plfBoot, "Wheel: Controller not found");

        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    hr = idas_di_dev_start(idas_di_dev);

    if (FAILED(hr)) {
        return hr;
    }

    if (Config.dinput.pedalsName[0] != '\0') {
        hr = IDirectInput8_EnumDevices(
                idas_di_api,
                DI8DEVCLASS_GAMECTRL,
                idas_di_enum_callback_pedals,
                NULL,
                DIEDFL_ATTACHEDONLY);

        if (FAILED(hr)) {
            log_error(plfBoot, "DirectInput: EnumDevices failed: %08x", (int) hr);

            return hr;
        }

        if (idas_di_pedals == NULL) {
            log_error(plfBoot, "Pedals: Controller not found");

            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }

        hr = idas_di_dev_start(idas_di_pedals);

        if (FAILED(hr)) {
            return hr;
        }

        idas_di_use_pedals = true;
    } else {
        idas_di_use_pedals = false;
    }

    if (Config.dinput.shifterName[0] != '\0') {    
        hr = IDirectInput8_EnumDevices(
                idas_di_api,
                DI8DEVCLASS_GAMECTRL,
                idas_di_enum_callback_shifter,
                NULL,
                DIEDFL_ATTACHEDONLY);

        if (FAILED(hr)) {
            log_error(plfBoot, "DirectInput: EnumDevices failed: %08x", (int) hr);

            return hr;
        }

        if (idas_di_shifter == NULL) {
            log_error(plfBoot, "Shifter: Controller not found");

            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }

        hr = idas_di_dev_start(idas_di_shifter);

        if (FAILED(hr)) {
            return hr;
        }
    }

    log_info(plfBoot, "DirectInput Controller initialized");
    *backend = &idas_di_backend;

    return S_OK;
}

static const struct idas_di_axis *idas_di_get_axis(const char *name)
{
    const struct idas_di_axis *axis;
    unsigned int i;
    for (i = 0 ; i < _countof(idas_di_axes) ; i++) {
        if (strcmp(name, idas_di_axes[i].name) == 0) {
            axis = &idas_di_axes[i];
            return axis;
        }
    }

    return NULL;
}

HRESULT idas_di_config_apply()
{
    const struct idas_di_axis *brake_axis;
    const struct idas_di_axis *accel_axis;

    brake_axis = idas_di_get_axis(Config.dinput.brakeAxis);
    accel_axis = idas_di_get_axis(Config.dinput.accelAxis);

    if (brake_axis == NULL) {
        log_error(plfBoot, "Wheel: Invalid brake axis: %s", Config.dinput.brakeAxis);

        return E_INVALIDARG;
    }

    if (accel_axis == NULL) {
        log_error(plfBoot, "Wheel: Invalid accel axis: %s", Config.dinput.accelAxis);

        return E_INVALIDARG;
    }

    if (Config.dinput.start > 128) {
        log_error(plfBoot, "Wheel: Invalid start button: %i", Config.dinput.start);

        return E_INVALIDARG;
    }

    if (Config.dinput.viewChg > 128) {
        log_error(plfBoot, "Wheel: Invalid view change button: %i", Config.dinput.viewChg);

        return E_INVALIDARG;
    }

    if (Config.dinput.shiftDn > 128) {
        log_error(plfBoot, "Wheel: Invalid shift down button: %i", Config.dinput.shiftDn);

        return E_INVALIDARG;
    }

    if (Config.dinput.shiftUp > 128) {
        log_error(plfBoot, "Wheel: Invalid shift up button: %i", Config.dinput.shiftUp);

        return E_INVALIDARG;
    }

    if (Config.dinput.up > 128) {
        log_error(plfBoot, "Wheel: Invalid up button: %i", Config.dinput.up);

        return E_INVALIDARG;
    }

    if (Config.dinput.down > 128) {
        log_error(plfBoot, "Wheel: Invalid down button: %i", Config.dinput.down);

        return E_INVALIDARG;
    }

    if (Config.dinput.left > 128) {
        log_error(plfBoot, "Wheel: Invalid left button: %i", Config.dinput.left);

        return E_INVALIDARG;
    }

    if (Config.dinput.right > 128) {
        log_error(plfBoot, "Wheel: Invalid right button: %i", Config.dinput.right);

        return E_INVALIDARG;
    }

    if (Config.dinput.coin > 128) {
        log_error(plfBoot, "Wheel: Invalid coin button: %i", Config.dinput.coin);

        return E_INVALIDARG;
    }

    /* Print some debug output to make sure config works... */

    log_warn(plfMisc, "Wheel: --- Begin configuration ---");
    log_warn(plfMisc, "Wheel: Device name . . . . : Contains \"%s\"", Config.dinput.deviceName);
    if (Config.dinput.pedalsName[0] == '\0') {
        log_warn(plfMisc, "Wheel: Brake axis  . . . . : %s", brake_axis->name); 
        log_warn(plfMisc, "Wheel: Accel axis  . . . . : %s", accel_axis->name);
    }
    log_warn(plfMisc, "Wheel: Start button  . . . : %i", Config.dinput.start);
    log_warn(plfMisc, "Wheel: View Change button  : %i", Config.dinput.viewChg);
    log_warn(plfMisc, "Wheel: Shift Down button . : %i", Config.dinput.shiftDn);
    log_warn(plfMisc, "Wheel: Shift Up button . . : %i", Config.dinput.shiftUp);
    log_warn(plfMisc, "Wheel: Up button . . . . . : %i (0 = POV only)", Config.dinput.up);
    log_warn(plfMisc, "Wheel: Down button . . . . : %i (0 = POV only)", Config.dinput.down);
    log_warn(plfMisc, "Wheel: Left button . . . . : %i (0 = POV only)", Config.dinput.left);
    log_warn(plfMisc, "Wheel: Right button . . . : %i (0 = POV only)", Config.dinput.right);
    log_warn(plfMisc, "Wheel: Coin button . . . . : %i (0 = disabled)", Config.dinput.coin);
    log_warn(plfMisc, "Wheel: Reverse Brake Axis  : %i", Config.dinput.reverseBrakeAxis);
    log_warn(plfMisc, "Wheel: Reverse Accel Axis  : %i", Config.dinput.reverseAccelAxis);
    log_warn(plfMisc, "Wheel: ---  End  configuration ---");
    
    if (Config.dinput.pedalsName[0] != '\0') {
        log_warn(plfMisc, "Pedals: --- Begin configuration ---");
        log_warn(plfMisc, "Pedals: Device name . . . : Contains \"%s\"", Config.dinput.pedalsName);
        log_warn(plfMisc, "Pedals: Brake axis  . . . . : %s", brake_axis->name);
        log_warn(plfMisc, "Pedals: Accel axis  . . . . : %s", accel_axis->name);
        log_warn(plfMisc, "Pedals: ---  End  configuration ---");
    }

    if (Config.dinput.shifterName[0] != '\0') {
        log_warn(plfMisc, "Shifter: --- Begin configuration ---");
        log_warn(plfMisc, "Shifter: Device name . . . : Contains \"%s\"", Config.dinput.shifterName);
        log_warn(plfMisc, "Shifter: ---  End  configuration ---");
    }

    idas_di_off_brake = brake_axis->off;
    idas_di_off_accel = accel_axis->off;
    idas_di_start = Config.dinput.start;
    idas_di_view_chg = Config.dinput.viewChg;
    idas_di_shift_dn = Config.dinput.shiftDn;
    idas_di_shift_up = Config.dinput.shiftUp;
    idas_di_up = Config.dinput.up;
    idas_di_down = Config.dinput.down;
    idas_di_left = Config.dinput.left;
    idas_di_right = Config.dinput.right;
    idas_di_coin = Config.dinput.coin;
    idas_di_reverse_brake_axis = Config.dinput.reverseBrakeAxis;
    idas_di_reverse_accel_axis = Config.dinput.reverseAccelAxis;

        /* FFB configuration */
    if (Config.ffb.constantForceStrength < 0 || Config.ffb.constantForceStrength > 100) {
        log_error(plfBoot, "Wheel: Invalid constant force strength: %i", Config.ffb.constantForceStrength);

        return E_INVALIDARG;
    }
    
    if (Config.ffb.rumbleStrength < 0 || Config.ffb.rumbleStrength > 100) {
        log_error(plfBoot, "Wheel: Invalid rumble strength: %i", Config.ffb.rumbleStrength);

        return E_INVALIDARG;
    }

    if (Config.ffb.damperStrength < 0 || Config.ffb.damperStrength > 100) {
        log_error(plfBoot, "Wheel: Invalid damper strength: %i", Config.ffb.damperStrength);

        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT idas_di_dev_start(IDirectInputDevice8A *dev) {
    bool exclusive = true;
    HRESULT hr;

    assert(dev != NULL);
    assert(idas_di_wnd != NULL);

    hr = IDirectInputDevice8_SetCooperativeLevel(
        dev,
        idas_di_wnd,
        DISCL_BACKGROUND | DISCL_EXCLUSIVE);

    if (FAILED(hr)) {
        log_error(plfBoot, "DirectInput: SetCooperativeLevel exclusive failed: %08x", (int)hr);

        hr = IDirectInputDevice8_SetCooperativeLevel(
            dev,
            idas_di_wnd,
            DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);

        if (FAILED(hr)) {
            log_error(plfBoot, "DirectInput: SetCooperativeLevel non-exclusive failed: %08x", (int)hr);
            return hr;
        }

        exclusive = false;
        log_info(plfBoot, "DirectInput: Using non-exclusive mode; force feedback may be unavailable");
    }

    hr = IDirectInputDevice8_SetDataFormat(dev, &c_dfDIJoystick2);

    if (FAILED(hr)) {
        log_error(plfBoot, "DirectInput: SetDataFormat failed: %08x", (int)hr);
        return hr;
    }
    hr = IDirectInputDevice8_Acquire(dev);

    if (FAILED(hr) && exclusive) {
        log_warn(plfBoot, "DirectInput: Exclusive Acquire failed: %08x", (int)hr);

        hr = IDirectInputDevice8_SetCooperativeLevel(
            dev,
            idas_di_wnd,
            DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);

        if (SUCCEEDED(hr)) {
            hr = IDirectInputDevice8_Acquire(dev);
        }
    }

    if (FAILED(hr)) {
        log_error(plfBoot, "DirectInput: Acquire failed: %08x", (int)hr);
        return hr;
    }

    return hr;
}

static BOOL CALLBACK idas_di_enum_callback(const DIDEVICEINSTANCEA *dev, void *data)
{
    HRESULT hr;
    if (strstr(dev->tszProductName, Config.dinput.deviceName) == NULL) {
        return DIENUM_CONTINUE;
    }

    log_info(plfBoot, "Wheel: Using DirectInput device \"%s\"", dev->tszProductName);

    hr = IDirectInput8_CreateDevice(
            idas_di_api,
            &dev->guidInstance,
            &idas_di_dev,
            NULL);

    if (FAILED(hr)) {
        log_error(plfBoot, "Wheel: CreateDevice failed: %08x", (int)hr);
    }

    return DIENUM_STOP;
}

static BOOL CALLBACK idas_di_enum_callback_pedals(const DIDEVICEINSTANCEA *dev, void *data)
{
    HRESULT hr;
    if (strstr(dev->tszProductName, Config.dinput.pedalsName) == NULL) {
        return DIENUM_CONTINUE;
    }

    log_info(plfBoot, "Pedals: Using DirectInput device \"%s\"", dev->tszProductName);

    hr = IDirectInput8_CreateDevice(
            idas_di_api,
            &dev->guidInstance,
            &idas_di_pedals,
            NULL);

    if (FAILED(hr)) {
        log_error(plfBoot, "Pedals: CreateDevice failed: %08x", (int)hr);
    }

    return DIENUM_STOP;
}

static BOOL CALLBACK idas_di_enum_callback_shifter(const DIDEVICEINSTANCEA *dev, void *data)
{
    HRESULT hr;
    if (strstr(dev->tszProductName, Config.dinput.shifterName) == NULL) {
        return DIENUM_CONTINUE;
    }

    log_info(plfBoot, "Shifter: Using DirectInput device \"%s\"", dev->tszProductName);

    hr = IDirectInput8_CreateDevice(
            idas_di_api,
            &dev->guidInstance,
            &idas_di_shifter,
            NULL);

    if (FAILED(hr)) {
        log_error(plfBoot, "Shifter: CreateDevice failed: %08x", (int)hr);
    }

    return DIENUM_STOP;
}

HRESULT di_dev_poll(
    IDirectInputDevice8A *dev, union idas_di_state *out)
{
    HRESULT hr;
    MSG msg;

    assert(dev != NULL);
    assert(idas_di_wnd != NULL);
    assert(out != NULL);

    memset(out, 0, sizeof(*out));

    while (PeekMessageA(&msg, idas_di_wnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    hr = IDirectInputDevice8_GetDeviceState(
        dev,
        sizeof(out->st),
        &out->st);

    if (FAILED(hr)) {
        log_error(plfBoot, "DirectInput: GetDeviceState error: %08x", (int)hr);
    }

    return hr;
}

static uint8_t idas_di_decode_pov(DWORD pov)
{
    switch (pov) {
        case 0:     return IDAS_IO_GAMEBTN_UP;
        case 4500:  return IDAS_IO_GAMEBTN_UP | IDAS_IO_GAMEBTN_RIGHT;
        case 9000:  return IDAS_IO_GAMEBTN_RIGHT;
        case 13500: return IDAS_IO_GAMEBTN_RIGHT | IDAS_IO_GAMEBTN_DOWN;
        case 18000: return IDAS_IO_GAMEBTN_DOWN;
        case 22500: return IDAS_IO_GAMEBTN_DOWN | IDAS_IO_GAMEBTN_LEFT;
        case 27000: return IDAS_IO_GAMEBTN_LEFT;
        case 31500: return IDAS_IO_GAMEBTN_LEFT | IDAS_IO_GAMEBTN_UP;
        default:    return 0;
    }
}

static void idas_di_jvs_read_buttons(uint8_t *gamebtn_out)
{
    union idas_di_state state;
    uint8_t gamebtn;
    HRESULT hr;

    assert(gamebtn_out != NULL);

    hr = di_dev_poll(idas_di_dev, &state);

    if (FAILED(hr)) {
        return;
    }

    gamebtn = idas_di_decode_pov(state.st.rgdwPOV[0]);

    if (idas_di_start && state.st.rgbButtons[idas_di_start - 1]) {
        gamebtn |= IDAS_IO_GAMEBTN_START;
    }

    if (idas_di_view_chg && state.st.rgbButtons[idas_di_view_chg - 1]) {
        gamebtn |= IDAS_IO_GAMEBTN_VIEW_CHANGE;
    }

    /* Button-based D-pad, for wheels without a POV/hat (e.g. MOZA R5 Base).
       OR-ed with the POV decode so either source works. */
    if (idas_di_up && state.st.rgbButtons[idas_di_up - 1]) {
        gamebtn |= IDAS_IO_GAMEBTN_UP;
    }

    if (idas_di_down && state.st.rgbButtons[idas_di_down - 1]) {
        gamebtn |= IDAS_IO_GAMEBTN_DOWN;
    }

    if (idas_di_left && state.st.rgbButtons[idas_di_left - 1]) {
        gamebtn |= IDAS_IO_GAMEBTN_LEFT;
    }

    if (idas_di_right && state.st.rgbButtons[idas_di_right - 1]) {
        gamebtn |= IDAS_IO_GAMEBTN_RIGHT;
    }

    *gamebtn_out = gamebtn;
}

static void idas_di_jvs_read_shifter(uint8_t *gear_out)
{
    union idas_di_state state;
    uint8_t gamebtn = 0;
    HRESULT hr;

    assert(gear_out != NULL);

    IDirectInputDevice8A *dev_to_use = idas_di_dev;
    if (idas_di_shifter != NULL) {
        dev_to_use = idas_di_shifter;
    }

    hr = di_dev_poll(dev_to_use, &state);

    if (FAILED(hr)) {
        return;
    }

    if (idas_di_shift_dn && state.st.rgbButtons[idas_di_shift_dn - 1]) {
        gamebtn |= IDAS_IO_GAMEBTN_SHIFTDN;
    }

    if (idas_di_shift_up && state.st.rgbButtons[idas_di_shift_up - 1]) {
        gamebtn |= IDAS_IO_GAMEBTN_SHIFTUP;
    }

    *gear_out = gamebtn;
}

static void idas_di_jvs_read_analogs(struct idas_io_analog_state *out)
{
    union idas_di_state state;
    union idas_di_state pedals_state;
    const LONG *brake;
    const LONG *accel;
    HRESULT hr;

    assert(out != NULL);

    hr = di_dev_poll(idas_di_dev, &state);

    if (FAILED(hr)) {
        return;
    }

    if (idas_di_use_pedals) {
        hr = di_dev_poll(idas_di_pedals, &pedals_state);

        if (FAILED(hr)) {
            return;
        }

        brake = (LONG *) &pedals_state.bytes[idas_di_off_brake];
        accel = (LONG *) &pedals_state.bytes[idas_di_off_accel];
    } else {
        brake = (LONG *) &state.bytes[idas_di_off_brake];
        accel = (LONG *) &state.bytes[idas_di_off_accel];
    }

    out->wheel = state.st.lX - 32768;

    if (idas_di_reverse_brake_axis) {
        out->brake = *brake;
    } else {
        out->brake = 65535 - *brake;
    }

    if (idas_di_reverse_accel_axis) {
        out->accel = *accel;
    } else {
        out->accel = 65535 - *accel;
    }
}

static void idas_di_jvs_read_system(uint8_t *sysbtn_out)
{
    union idas_di_state state;
    uint8_t sysbtn;
    HRESULT hr;

    assert(sysbtn_out != NULL);

    sysbtn = 0;

    hr = di_dev_poll(idas_di_dev, &state);

    if (FAILED(hr)) {
        *sysbtn_out = sysbtn;

        return;
    }

    if (idas_di_coin && state.st.rgbButtons[idas_di_coin - 1]) {
        sysbtn |= IDAS_IO_SYSBTN_COIN;
    }

    *sysbtn_out = sysbtn;
}

static void idas_di_ffb_init(void) {
}

static void idas_di_ffb_toggle(bool active) {
    if (active) {
        return;
    }

    /* Stop and release all effects */
    if (idas_di_fx != NULL) {
        IDirectInputEffect_Stop(idas_di_fx);
        IDirectInputEffect_Release(idas_di_fx);
        idas_di_fx = NULL;
    }

    if (idas_di_fx_rumble != NULL) {
        IDirectInputEffect_Stop(idas_di_fx_rumble);
        IDirectInputEffect_Release(idas_di_fx_rumble);
        idas_di_fx_rumble = NULL;
    }

    if (idas_di_fx_spring) {
        IDirectInputEffect_Stop(idas_di_fx_spring);
        IDirectInputEffect_Release(idas_di_fx_spring);
        idas_di_fx_spring = NULL;
    }

    if (idas_di_fx_damper != NULL) {
        IDirectInputEffect_Stop(idas_di_fx_damper);
        IDirectInputEffect_Release(idas_di_fx_damper);
        idas_di_fx_damper = NULL;
    }
}

static void idas_di_ffb_constant_force(uint8_t direction_ffb, uint8_t force) {
    /* DI expects a magnitude in the range of -10.000 to 10.000 */
    uint16_t ffb_strength = Config.ffb.constantForceStrength * 100;
    if (ffb_strength == 0) {
        return;
    }

    DWORD axis;
    LONG direction;
    DIEFFECT fx;
    DICONSTANTFORCE cf;
    HRESULT hr;

    /* Direction 0: move to the right, 1: move to the left */
    LONG magnitude = (LONG)(((double)force / idas_di_ffb_scale) * ffb_strength);
    cf.lMagnitude = (direction_ffb == 0) ? -magnitude : magnitude;

    axis = DIJOFS_X;
    direction = 0;

    memset(&fx, 0, sizeof(fx));
    fx.dwSize = sizeof(fx);
    fx.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    fx.dwDuration = INFINITE;
    fx.dwGain = DI_FFNOMINALMAX;
    fx.dwTriggerButton = DIEB_NOTRIGGER;
    fx.dwTriggerRepeatInterval = INFINITE;
    fx.cAxes = 1;
    fx.rgdwAxes = &axis;
    fx.rglDirection = &direction;
    fx.cbTypeSpecificParams = sizeof(cf);
    fx.lpvTypeSpecificParams = &cf;

    /* Check if the effect already exists */
    if (idas_di_fx != NULL) {
        hr = IDirectInputEffect_SetParameters(idas_di_fx, &fx, DIEP_TYPESPECIFICPARAMS);
        if (SUCCEEDED(hr)) {
            return; // Successfully updated existing effect
        }
        else {
            log_error(plfBoot, "DirectInput: Failed to update constant force feedback, recreating effect: %08x", (int)hr);
            IDirectInputEffect_Stop(idas_di_fx);
            IDirectInputEffect_Release(idas_di_fx);
            idas_di_fx = NULL; // Reset the pointer
        }
    }

    update_or_create_effect(&idas_di_fx, &GUID_ConstantForce, &fx);
}

static void idas_di_ffb_rumble(uint8_t force, uint8_t period) {
    /* DI expects a magnitude in the range of -10.000 to 10.000 */
    uint16_t ffb_strength = Config.ffb.rumbleStrength * 100;
    if (ffb_strength == 0) {
        return;
    }

    uint32_t ffb_duration = Config.ffb.rumbleDuration;

    DWORD axis;
    LONG direction;
    DIEFFECT fx;
    DIPERIODIC pe;

    DWORD duration = (DWORD)((double)force * ffb_duration);

    memset(&pe, 0, sizeof(pe));
    pe.dwMagnitude = (DWORD)(((double)force / idas_di_ffb_scale) * ffb_strength);
    pe.lOffset = 0;
    pe.dwPhase = 0;
    pe.dwPeriod = duration;

    axis = DIJOFS_X;
    direction = 0;

    memset(&fx, 0, sizeof(fx));
    fx.dwSize = sizeof(fx);
    fx.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    fx.dwDuration = INFINITE;
    fx.dwGain = DI_FFNOMINALMAX;
    fx.dwTriggerButton = DIEB_NOTRIGGER;
    fx.dwTriggerRepeatInterval = INFINITE;
    fx.cAxes = 1;
    fx.rgdwAxes = &axis;
    fx.rglDirection = &direction;
    fx.cbTypeSpecificParams = sizeof(pe);
    fx.lpvTypeSpecificParams = &pe;

    update_or_create_effect(&idas_di_fx_rumble, &GUID_Sine, &fx);
}

static void idas_di_ffb_damper(uint8_t force) {
    uint32_t ffb_base_damper = Config.ffb.baseDamperStrength;
    uint32_t ffb_deadband = Config.ffb.deadBand;
    /* DI expects a coefficient in the range of -10.000 to 10.000 */
    uint16_t ffb_strength = Config.ffb.damperStrength * 100;
    if (ffb_strength == 0) {
        if (idas_di_fx_spring) {
            IDirectInputEffect_Stop(idas_di_fx_spring);
            IDirectInputEffect_Release(idas_di_fx_spring);
            idas_di_fx_spring = NULL;
        }
        if (idas_di_fx_damper) {
            IDirectInputEffect_Stop(idas_di_fx_damper);
            IDirectInputEffect_Release(idas_di_fx_damper);
            idas_di_fx_damper = NULL;
        }
        return;
    }

    DWORD axis = DIJOFS_X;
    LONG direction = 0;
    DIEFFECT fx;
    DICONDITION cond;

    memset(&cond, 0, sizeof(cond));
    cond.lPositiveCoefficient = (LONG)(((uint32_t)force * ffb_strength) / idas_di_ffb_scale);
    cond.lNegativeCoefficient = cond.lPositiveCoefficient;
    cond.dwPositiveSaturation = DI_FFNOMINALMAX;
    cond.dwNegativeSaturation = DI_FFNOMINALMAX;
    cond.lDeadBand = (DI_FFNOMINALMAX * (LONG)ffb_deadband) / 1000;

    memset(&fx, 0, sizeof(fx));
    fx.dwSize = sizeof(fx);
    fx.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    fx.dwDuration = INFINITE;
    fx.dwGain = DI_FFNOMINALMAX;
    fx.dwTriggerButton = DIEB_NOTRIGGER;
    fx.dwTriggerRepeatInterval = INFINITE;
    fx.cAxes = 1;
    fx.rgdwAxes = &axis;
    fx.rglDirection = &direction;
    fx.cbTypeSpecificParams = sizeof(cond);
    fx.lpvTypeSpecificParams = &cond;

    update_or_create_effect(&idas_di_fx_spring, &GUID_Spring, &fx);

    /* DAMPER (resistance with baseline)  */
    memset(&cond, 0, sizeof(cond));

    LONG nominal = (LONG)(( (LONG)force * ffb_strength * idas_di_damper_spring_ratio ) / idas_di_ffb_scale);
    LONG min_baseline = (LONG)(DI_FFNOMINALMAX * (ffb_base_damper / 100.0));

    if (nominal < min_baseline) {
        nominal = min_baseline;
    }

    cond.lPositiveCoefficient = nominal;
    cond.lNegativeCoefficient = nominal;
    cond.dwPositiveSaturation = DI_FFNOMINALMAX;
    cond.dwNegativeSaturation = DI_FFNOMINALMAX;

    memset(&fx, 0, sizeof(fx));
    fx.dwSize = sizeof(fx);
    fx.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    fx.dwDuration = INFINITE;
    fx.dwGain = DI_FFNOMINALMAX;
    fx.dwTriggerButton = DIEB_NOTRIGGER;
    fx.dwTriggerRepeatInterval = INFINITE;
    fx.cAxes = 1;
    fx.rgdwAxes = &axis;
    fx.rglDirection = &direction;
    fx.cbTypeSpecificParams = sizeof(cond);
    fx.lpvTypeSpecificParams = &cond;

    update_or_create_effect(&idas_di_fx_damper, &GUID_Damper, &fx);

}

static void update_or_create_effect(LPDIRECTINPUTEFFECT* effect_ptr, REFGUID guid, DIEFFECT* fx) {
    HRESULT hr;

    if (*effect_ptr != NULL) {
        hr = IDirectInputEffect_SetParameters(*effect_ptr, fx, DIEP_TYPESPECIFICPARAMS);
        if (SUCCEEDED(hr)) {
            return;
        } else {
            log_error(plfFFB, "DirectInput: SetParameters failed, recreating effect: %08x", (int)hr);
            IDirectInputEffect_Stop(*effect_ptr);
            IDirectInputEffect_Release(*effect_ptr);
            *effect_ptr = NULL;
        }
    }

    IDirectInputEffect* obj = NULL;
    hr = IDirectInputDevice8_CreateEffect(idas_di_dev, guid, fx, &obj, NULL);
    if (FAILED(hr)) {
        log_error(plfFFB, "DirectInput: CreateEffect failed: %08x", (int)hr);
        return;
    }

    hr = IDirectInputEffect_Start(obj, fx->dwDuration, 0);
    if (FAILED(hr)) {
        log_error(plfFFB, "DirectInput: Effect start failed: %08x", (int)hr);
        IDirectInputEffect_Release(obj);
        return;
    }

    *effect_ptr = obj;
}