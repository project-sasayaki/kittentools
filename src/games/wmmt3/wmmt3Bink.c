#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <MinHook.h>

#include "wmmt3.h"
#include "../../elfLoader/symbol.h"
#include "../../log/log.h"

/* Bink's Linux shim uses a pthread mutex as an event, including signalling it
 * from a non-owner thread. Use an actual event for its single-object waits.
 * Its multiple-object wait handles callback mutexes, which stay in the guest. */
typedef struct {
    int type;
    HANDLE handle;
} BinkEvent;

enum { BINK_EVENT = 2 };

static int (*next_close)(void *);
static DWORD (*next_wait)(void *, DWORD);

static void *bink_create_event(void *attributes, int manualReset, int initialState, const char *name)
{
    (void)attributes;
    (void)name;
    BinkEvent *event = malloc(sizeof(*event));
    if (!event)
        return NULL;
    event->type = BINK_EVENT;
    event->handle = CreateEventA(NULL, manualReset, initialState, NULL);
    if (!event->handle) {
        free(event);
        return NULL;
    }
    return event;
}

static int bink_set_event(BinkEvent *event)
{
    return event && SetEvent(event->handle);
}

static DWORD bink_wait(void *object, DWORD timeout)
{
    BinkEvent *event = object;
    if (event && event->type == BINK_EVENT)
        return WaitForSingleObject(event->handle, timeout);
    return next_wait(object, timeout);
}

static int bink_close(void *object)
{
    BinkEvent *event = object;
    if (event && event->type == BINK_EVENT) {
        BOOL closed = CloseHandle(event->handle);
        if (closed)
            free(event);
        return closed;
    }
    return next_close(object);
}

void wmmt3_install_bink_events(void)
{
    /* WMMT3's older Bink build does not export this set of wrappers. */
    if (!get_native_symbol_ptr("RADCB_allocate_handler"))
        return;

    const struct {
        const char *name;
        void *replacement;
        void **original;
    } hooks[] = {
        {"CreateEvent", bink_create_event, NULL},
        {"SetEvent", bink_set_event, NULL},
        {"WaitForSingleObject", bink_wait, (void **)&next_wait},
        {"CloseHandle", bink_close, (void **)&next_close},
    };

    for (size_t i = 0; i < ARRAYSIZE(hooks); ++i) {
        if (wmmt3_hook(hooks[i].name, hooks[i].replacement, hooks[i].original))
            continue;

        /* Mixed guest/native event handles would be unsafe. Hooks are enabled
         * only after prepatch, so roll back the entire group on any failure. */
        for (size_t j = 0; j < i; ++j)
            MH_RemoveHook(get_native_symbol_ptr(hooks[j].name));
        log_warn(plfWmmt3, "Bink: could not hook %s; using guest synchronization", hooks[i].name);
        return;
    }

    log_info(plfWmmt3, "Bink: using Windows events for movie callbacks");
}
