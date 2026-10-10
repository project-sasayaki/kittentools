#include <assert.h>
#include "../log/log.h"
#include "../devices/jvs.h"
#include "../devices/motorboard.h"
#include "../config/config.h"
#include "idas_input.h"
#include "idas_di.h"
#include "idas_xi.h"
#include "backend.h"


static const struct idas_io_backend *idas_io_backend;
static bool idas_io_coin;
static uint16_t idas_io_coins;

extern JVSIO io;

static void idas_input_read_switches(struct io3_switch_state *out);
static void idas_input_read_analogs(uint16_t *analogs, uint8_t nanalogs);
static void idas_input_read_coin_counter(uint8_t slot_no, uint16_t *out);
static void idas_io_ffb_toggle(bool active);
static void idas_io_ffb_constant_force(uint8_t direction, uint8_t force);
static void idas_io_ffb_rumble(uint8_t period, uint8_t force);
static void idas_io_ffb_damper(uint8_t force);

static const struct JVS_OPS idas_input_ops = {
    .read_switches      = idas_input_read_switches,
    .read_analogs       = idas_input_read_analogs,
    .read_coin_counter  = idas_input_read_coin_counter,
};

static const struct ffb_ops idas_ffb_ops = {
    .req_toggle             = idas_io_ffb_toggle,
    .req_constant_force     = idas_io_ffb_constant_force,
    .req_rumble             = idas_io_ffb_rumble,
    .req_damper             = idas_io_ffb_damper,
};


void idas_input_init() {
    io.ops = (JVS_OPS *)&idas_input_ops;
    if (Config.io3.mode == INPUT_XINPUT)
        idas_xi_init(&idas_io_backend);
    else
        idas_di_init(&idas_io_backend);
    ffb_init(&idas_ffb_ops);
}

static void idas_input_read_switches(struct io3_switch_state *out) {
    uint8_t gamebtn = 0;
    uint8_t gear = 0;

    if ( !out)
        return;

    idas_io_backend->jvs_read_buttons(&gamebtn);
    idas_io_backend->jvs_read_shifter(&gear);

    /* Update gameplay buttons */

    if (gamebtn & IDAS_IO_GAMEBTN_UP) {
        out->p1 |= 1 << 13;
    }

    if (gamebtn & IDAS_IO_GAMEBTN_DOWN) {
        out->p1 |= 1 << 12;
    }

    if (gamebtn & IDAS_IO_GAMEBTN_LEFT) {
        out->p1 |= 1 << 11;
    }

    if (gamebtn & IDAS_IO_GAMEBTN_RIGHT) {
        out->p1 |= 1 << 10;
    }

    if (gamebtn & IDAS_IO_GAMEBTN_START) {
        out->p1 |= 1 << 15;
    }

    if (gamebtn & IDAS_IO_GAMEBTN_VIEW_CHANGE) {
        out->p1 |= 1 << 9;
    }

    if (gear & IDAS_IO_GAMEBTN_SHIFTDN) {
        out->p2 |= 1 << 12;
    }

    if (gear & IDAS_IO_GAMEBTN_SHIFTUP) {
        out->p2 |= 1 << 13;
    }

    if (GetAsyncKeyState(Config.io3.test) & 0x8000) {
        out->system = 0x80;
    } else {
        out->system = 0;
    }

    if (GetAsyncKeyState(Config.io3.service) & 0x8000) {
        out->p1 |= 1 << 14;
    }

    return;
}

static void idas_input_read_analogs(uint16_t *analogs, uint8_t nanalogs) {
    struct idas_io_analog_state state;

    if ( !analogs)
        return;

    memset(&state, 0, sizeof(state));
    idas_io_backend->jvs_read_analogs(&state);

    state.wheel = (state.wheel * Config.io3.restrict_) / 128;

    if (nanalogs > 0) {
        analogs[0] = 0x8000 + state.wheel;
    }

    if (nanalogs > 1) {
        analogs[1] = state.accel;
    }

    if (nanalogs > 2) {
        analogs[2] = state.brake;
    }
    return;
}

static void idas_input_read_coin_counter(uint8_t slot_no, uint16_t *out) {
    bool coin_pressed;
    uint8_t sysbtn = 0;

    if ( !out)
        return;
    
    if (slot_no > 0)
        return;

    /* Pull coin from the device (wheel button / XInput button) if the backend
       supports it, then OR with the keyboard coin fallback. */
    if (idas_io_backend != NULL && idas_io_backend->jvs_read_system != NULL) {
        idas_io_backend->jvs_read_system(&sysbtn);
    }

    coin_pressed = (sysbtn & IDAS_IO_SYSBTN_COIN) ||
        (Config.io3.coin && (GetAsyncKeyState(Config.io3.coin) & 0x8000));

    if (coin_pressed) {
        if (!idas_io_coin) {
            idas_io_coin = true;
            idas_io_coins++;
        }
    } else {
        idas_io_coin = false;
    }

    *out = idas_io_coins;
}

static void idas_io_ffb_toggle(bool active)
{
    if ( !idas_io_backend) {
        log_error(plfBoot, "IDAS IO Backend not initialized");
        return;
    }
    idas_io_backend->ffb_req_toggle(active);
}

static void idas_io_ffb_constant_force(uint8_t direction, uint8_t force)
{
    if ( !idas_io_backend) {
        log_error(plfBoot, "IDAS IO Backend not initialized");
        return;
    }
    idas_io_backend->ffb_req_constant_force(direction, force);
}

static void idas_io_ffb_rumble(uint8_t period, uint8_t force)
{
    if ( !idas_io_backend) {
        log_error(plfBoot, "IDAS IO Backend not initialized");
        return;
    }
    idas_io_backend->ffb_req_rumble(period, force);
}

static void idas_io_ffb_damper(uint8_t force)
{
    if ( !idas_io_backend) {
        log_error(plfBoot, "IDAS IO Backend not initialized");
        return;
    }
    idas_io_backend->ffb_req_damper(force);
}