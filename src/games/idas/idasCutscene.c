#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include <windows.h>
#include <MinHook.h>

#include "idas.h"
#include "idasCutscene.h"
#include "../log/log.h"
#include "../../elfLoader/symbol.h"

#define IDAS_CANVAS_WIDTH 1360.0f
#define IDAS_CANVAS_HEIGHT 768.0f
#define IDAS_MIN_FRAMEBUFFER_DIMENSION 320u
#define IDAS_MAX_FRAMEBUFFER_DIMENSION 16384u

typedef struct IDAS_CutsceneLayout {
    float scale;
    float offsetX;
    uint32_t width;
    uint32_t height;
} IDAS_CutsceneLayout;

typedef uintptr_t (__cdecl *IDAS_ThisDrawFn)(void *self);
typedef void *(__cdecl *IDAS_Vertex5Fn)(float x, float y, float z,
                                        float u, float v);
typedef void *(__cdecl *IDAS_RectangleFn)(float x1, float y1, float x2,
                                          float y2, float depth);

static IDAS_ThisDrawFn nextSkipPromptDraw;
static IDAS_Vertex5Fn nextVertex5;
static IDAS_ThisDrawFn nextTimeAttackDraw;
static IDAS_RectangleFn nextTimeAttackRectangle;

static const uint32_t *windowWidth;
static const uint32_t *windowHeight;

static __thread unsigned int skipPromptDepth;
static __thread unsigned int timeAttackDepth;
static __thread bool skipPromptLayoutValid;
static __thread bool timeAttackLayoutValid;
static __thread IDAS_CutsceneLayout skipPromptLayout;
static __thread IDAS_CutsceneLayout timeAttackLayout;

static bool makeLayout(IDAS_CutsceneLayout *layout)
{
    uint32_t width;
    uint32_t height;
    float scale;
    float offsetX;

    if (!layout || !windowWidth || !windowHeight)
        return false;

    width = *windowWidth;
    height = *windowHeight;
    if (width < IDAS_MIN_FRAMEBUFFER_DIMENSION ||
        height < IDAS_MIN_FRAMEBUFFER_DIMENSION ||
        width > IDAS_MAX_FRAMEBUFFER_DIMENSION ||
        height > IDAS_MAX_FRAMEBUFFER_DIMENSION)
        return false;

    scale = (float)height / IDAS_CANVAS_HEIGHT;
    offsetX = ((float)width - IDAS_CANVAS_WIDTH * scale) * 0.5f;
    if (!isfinite(scale) || !isfinite(offsetX) || scale <= 0.0f)
        return false;

    layout->scale = scale;
    layout->offsetX = offsetX;
    layout->width = width;
    layout->height = height;
    return true;
}

static float layoutX(const IDAS_CutsceneLayout *layout, float x)
{
    return layout->offsetX + x * layout->scale;
}

static float layoutY(const IDAS_CutsceneLayout *layout, float y)
{
    return y * layout->scale;
}

static float layoutCenteredX(const IDAS_CutsceneLayout *layout, float x)
{
    float center = (float)layout->width * 0.5f;
    return center + (x - center) * layout->scale;
}

static float layoutCenteredY(const IDAS_CutsceneLayout *layout, float y)
{
    float center = (float)layout->height * 0.5f;
    return center + (y - center) * layout->scale;
}

static uintptr_t __cdecl hookSkipPromptDraw(void *self)
{
    uintptr_t result;

    ++skipPromptDepth;
    if (skipPromptDepth == 1u)
        skipPromptLayoutValid = makeLayout(&skipPromptLayout);

    result = nextSkipPromptDraw(self);

    --skipPromptDepth;
    if (skipPromptDepth == 0u)
        skipPromptLayoutValid = false;
    return result;
}

static void * __cdecl hookVertex5(float x, float y, float z, float u, float v)
{
    if (skipPromptDepth != 0u && skipPromptLayoutValid) {
        x = layoutX(&skipPromptLayout, x);
        y = layoutY(&skipPromptLayout, y);
    }
    return nextVertex5(x, y, z, u, v);
}

static uintptr_t __cdecl hookTimeAttackDraw(void *self)
{
    uintptr_t result;

    ++timeAttackDepth;
    if (timeAttackDepth == 1u)
        timeAttackLayoutValid = makeLayout(&timeAttackLayout);

    result = nextTimeAttackDraw(self);

    --timeAttackDepth;
    if (timeAttackDepth == 0u)
        timeAttackLayoutValid = false;
    return result;
}

static void * __cdecl hookTimeAttackRectangle(float x1, float y1, float x2,
                                               float y2, float depth)
{
    if (timeAttackDepth != 0u && timeAttackLayoutValid) {
        /* ID4/ID5 initialize these coordinates relative to the live
         * framebuffer center, unlike the top-left-authored skip prompt. */
        x1 = layoutCenteredX(&timeAttackLayout, x1);
        y1 = layoutCenteredY(&timeAttackLayout, y1);
        x2 = layoutCenteredX(&timeAttackLayout, x2);
        y2 = layoutCenteredY(&timeAttackLayout, y2);
    }
    return nextTimeAttackRectangle(x1, y1, x2, y2, depth);
}

static bool createHook(LPVOID address, void *replacement, void **original,
                       const char *name)
{
    MH_STATUS status = MH_CreateHook(address, replacement, (LPVOID *)original);
    if (status == MH_OK)
        return true;

    log_error(plfBridges, "Failed to install IDAS %s hook: %d", name,
              (int)status);
    return false;
}

void installIDASCutsceneHooks()
{
    bool ok = true;

    windowWidth = (const uint32_t*)get_variable_symbol_ptr("_ZN8_sSystem13ulWindowSizeXE");
    windowHeight = (const uint32_t*)get_variable_symbol_ptr("_ZN8_sSystem13ulWindowSizeYE");

    ok &= createHook(get_native_symbol_ptr("_ZN12cDemoVsRival4drawEv"), hookSkipPromptDraw,
                        (void **)&nextSkipPromptDraw, "skip prompt draw");
    ok &= createHook(get_native_symbol_ptr("_ZN10cPrimitive9setVertexEfffff"), hookVertex5,
                        (void **)&nextVertex5, "skip prompt vertex");
    ok &= createHook(get_native_symbol_ptr("_ZN20cAcroResultTABalloon4drawEv"), hookTimeAttackDraw,
                        (void **)&nextTimeAttackDraw, "TA balloon draw");
    ok &= createHook(get_native_symbol_ptr("_ZN10cPrimitive14setRectanglePTEfffff"),
                        hookTimeAttackRectangle,
                        (void **)&nextTimeAttackRectangle,
                        "TA balloon rectangle");

    if (ok)
        log_info(plfBridges, "Installed scoped IDAS cutscene layout hooks");
}
