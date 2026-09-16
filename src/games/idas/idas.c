#include <winsock2.h>
#include <math.h>
#include <stddef.h>
#include "idas.h"
#include "idasCutscene.h"
#include "idasNetwork.h"
#include "../../log/log.h"
#include "../../elfLoader/symbol.h"
#include "../../hooks/file.h"
#include "../../hooks/amLib.h"
#include "../../devices/securityBoard.h"
#include "../../devices/jvs.h"
#include "../../graphics/fpsLimiter.h"
#include "../../graphics/gui.h"
#include "../../graphics/gpuVendor.h"
#include "../../input/idas_input.h"
#include "../../input/idas_di.h"
#include <MinHook.h>

static float idShiftX = 0.0;
static float idShiftY = 0.0;

static const IDAS_InitialData* init = NULL;

int (__cdecl *next_drawBalloon)(void* this, void* a2, float a3, float a4, float a5, float a6, float a7, bool a8);
int (__cdecl *next_cCreditdisplayTexture)(int a1, int a2, uint8_t a3, int a4, int a5);
double (__cdecl *next_sFont_put)(void *this, float a2, float a3, uint16_t a4);
int (__cdecl *next_sFix_put)(int a1, int a2, int a3, char *format);
int (__cdecl *next_setClipRect)(int a1, int *a2);
int (__cdecl *next_drawtext)(void *this, void *a2, float a3, float a4);
void *(__cdecl *next_CreateFrameBuffer)(
    void *renderer, uint32_t width, uint32_t height,
    uint32_t multiSample, uint32_t multiSampleQuality,
    uint32_t colorFormat, uint32_t depthBits, uint32_t stencilBits,
    uint32_t createDepth, void *colorTexture, void *depthTexture);

typedef void (*sWinProc_MakeWindow_t)(void);
static sWinProc_MakeWindow_t next_MakeWindow = NULL;
typedef void (__attribute__((regparm(3)))* sGLH_GET_EXTPROC_notype_t)(void** param_1, const char* param_2, bool param_3);
static sGLH_GET_EXTPROC_notype_t next_sGLH_GET_EXTPROC_notype = NULL;

void hook_MakeWindow(void)
{
    int *ulFlags = get_variable_symbol_ptr("_ZN8_sSystem7ulFlagsE");

    if (ulFlags != NULL)
    {
        *ulFlags &= ~0x40;
    }

    return next_MakeWindow();
}

void *hook_CreateFrameBuffer(
    void *renderer, uint32_t width, uint32_t height,
    uint32_t multiSample, uint32_t multiSampleQuality,
    uint32_t colorFormat, uint32_t depthBits, uint32_t stencilBits,
    uint32_t createDepth, void *colorTexture, void *depthTexture)
{
    if (width == 0x550 && height == 0x300)
    {
        width = (uint32_t)Config.display.width;
        height = (uint32_t)Config.display.height;
    }

    return next_CreateFrameBuffer(
        renderer, width, height, multiSample, multiSampleQuality,
        colorFormat, depthBits, stencilBits, createDepth,
        colorTexture, depthTexture);
}

static void CalculateShiftForBallon(int W, int H, float *ShiftX, float *ShiftY, bool right)
{
    int baseWidth1 = 1360, baseHeight1 = 768;
    int baseWidth2 = 2560, baseHeight2 = 1440;

    float knownShiftX1 = 0, knownShiftY1 = 0;
    float knownShiftX2 = 250, knownShiftY2 = 100;
    if (right)
        knownShiftX2 = 900;

    *ShiftX = ((float)(W - baseWidth1) / (baseWidth2 - baseWidth1)) * (knownShiftX2 - knownShiftX1);
    *ShiftY = ((float)(H - baseHeight1) / (baseHeight2 - baseHeight1)) * (knownShiftY2 - knownShiftY1);
}

int hook_drawBalloon(void* this, void* a2, float a3, float a4, float a5, float a6, float a7, bool a8)
{
    CalculateShiftForBallon(Config.display.width, Config.display.height, &idShiftX, &idShiftY, a8);
    return next_drawBalloon(this, a2, a3 + idShiftX, a4 + idShiftY, a5, a6, a7, a8);
}

int hook_cCreditdisplayTexture(int a1, int a2, uint8_t a3, int a4, int a5)
{
    if (a4 > 200)
        a4 = (int)(((float)Config.display.width / 2) - ((1360.0 / 2) - a4));
    a5 = (int)((float)Config.display.height * (a5 / 768.0));

    return next_cCreditdisplayTexture(a1, a2, a3, a4, a5);
}

double hook_sFont_put(void *this, float a2, float a3, uint16_t a4)
{
    return next_sFont_put(this, a2 + idShiftX, a3 + idShiftY, a4);
}

int hook_sFix_put(int a1, int a2, int a3, char *format)
{
    a2 = (int)((Config.display.height / 768.0) * (768 >> 5));
    return next_sFix_put(a1, a2, a3, format);
}

int hook_setClipRect(int a1, int *a2)
{ 
    if (a2[0] + (Config.display.width >>1) == 14)
        a2[0] = -666;
    if (a2[0] + (Config.display.width >>1) == 572)
        a2[0] = -108;

    if (a2[2] + (Config.display.width >>1) == 454)
        a2[2] = -226;
    if (a2[2] + (Config.display.width >>1) == 1234)
        a2[2] = 554;
    
    if (a2[1] != 0)
    {
        a2[1] = 384;
        a2[3] = -384;
    }
    else a2[3] = 512;

    return next_setClipRect(a1, a2);
}

int hook_drawtext(void *this, void *a2, float a3, float a4)
{
    return next_drawtext(this, a2, a3 + idShiftX, a4 + idShiftY);
}

void __attribute__((regparm(3))) hook_sGLH_GET_EXTPROC_notype(void** param_1, const char* param_2, bool param_3)
{
    next_sGLH_GET_EXTPROC_notype(param_1, param_2, param_3);
    if (!strcmp(param_2, "glRenderbufferStorageMultisample"))
        *param_1 = 0;
}

void init_idas_prepatch(uint32_t crc)
{
    char *gpuName = getGPUVendorName();
    bool isServerBox = false;

    initJVS(SEGA_TYPE_3);
    idas_input_init();

    for (size_t i = 0; i < ARRAYSIZE(g_IDASVersions); ++i) {
        if (g_IDASVersions[i].crc == crc) {
            init = g_IDASVersions[i].data;
            break;
        }
    }

    if (!init)
    {
        log_error(plfBoot, "Unkown CRC 0x%08x", crc);
        MessageBox(NULL, "This ELF is not supported by Kittentools yet, thought it will still try to run it, be ware of any possible crashes.", NULL, MB_OK);
    }
    

    log_warn(plfMisc, "  KittenTools By Project Sasayaki");
    log_warn(plfMisc, "  GAME:             %s", init ? init->verName : "UNKNOWN");
    log_warn(plfMisc, "  GAME ID:          %s", init ? init->gameID : "UNKNOWN");
    log_warn(plfMisc, "  GPU:              %s", gpuName);
    if (Config.display.render_scale <= 1)
        log_warn(plfMisc, "  Render Scale:     Disable");
    else
        log_warn(plfMisc, "  Render Scale:     %dx", Config.display.render_scale);
    if (Config.display.msaa_samples <= 0)
        log_warn(plfMisc, "  MSAA:             Disable");
    else
        log_warn(plfMisc, "  MSAA:             %dx", Config.display.msaa_samples);
    if (Config.display.anisotropic <= 0)
        log_warn(plfMisc, "  Anisotropic:      Disable");
    else
        log_warn(plfMisc, "  Anisotropic:      %dx", Config.display.anisotropic);
    log_warn(plfMisc, "  Enable Network:   %s", Config.network.enable ? "YES" : "NO");
    log_warn(plfMisc, "  INPUT MODE:       %s", Config.io3.mode == 1 ? "XInput" :
                                                Config.io3.mode == 2 ? "DInput" : 
                                                                       "UNKNOWN");
    isServerBox = init_idas_network(init);
    log_warn(plfMisc, "  IS_SERVERBOX:     %s", isServerBox ? "YES" : "NO");
    initFpsLimiter(596, 10, !isServerBox);
    log_warn(plfMisc, "  SimulateNetwork:  %s", Config.network.simulateNetwork ? "YES" : "NO");
    log_warn(plfMisc, "  Keychip:          %s", Config.sysconf.keychip_serial);

    installIDASCutsceneHooks();

    if (Config.io3.mode == INPUT_DINPUT)
        idas_di_config_apply(); 

    if (init) {
        setupGameId(init->gameID);

        if (init->fixResolution.enabled)
        {
            log_debug(plfBoot, "Overriding resolution to %dx%d", init->fixResolution.width, init->fixResolution.height);
            Config.display.width = init->fixResolution.width;
            Config.display.height = init->fixResolution.height;
        }

        if (crc == EXP20_SBTS_REVA_SERVERBOX)
            patchMachineCode(init->networkPatch.skipInitAddress.addr, init->networkPatch.skipInitAddress.newStr);

        // Resolution patches
        // TODO: could be modified in strcpy;
        // setVariable(get_variable_symbol_ptr("_ZN8_sSystem13ulWindowSizeXE"), Config.display.width);
        // setVariable(get_variable_symbol_ptr("_ZN8_sSystem13ulWindowSizeYE"), Config.display.height);
        setVariable(init->resolutionPatch.maxWidthAddr.addr, 1);
        setVariable(init->resolutionPatch.MaxHeightAddr.addr, 1);
        setVariable(init->resolutionPatch.targetWidthAddr.addr, Config.display.width);
        setVariable(init->resolutionPatch.targetHeightAddr.addr, Config.display.height);
    }

    MH_CreateHook((LPVOID)get_native_symbol_ptr("_ZN2es23sGLH_GET_EXTPROC_notypeEPPvPKcb"), hook_sGLH_GET_EXTPROC_notype, (void**)&next_sGLH_GET_EXTPROC_notype);
    MH_CreateHook((LPVOID)get_native_symbol_ptr("_ZN9cRenderer17createFrameBufferEttmmN7_sScene14TEXTURE_FORMATEmmbPN8_sSerial9_sTextureES4_"), hook_CreateFrameBuffer, (void**)&next_CreateFrameBuffer);

    // Display patches
    MH_CreateHook((LPVOID)get_native_symbol_ptr("_Z19sWinProc_MakeWindowv"), hook_MakeWindow, (LPVOID*)&next_MakeWindow);
    MH_CreateHook((LPVOID)get_native_symbol_ptr("_ZN14cShaderUtility18isExistNewerSourceEPKcS1_S1_PS1_"), stubRetOne, NULL);
    MH_CreateHook((LPVOID)get_native_symbol_ptr("_ZN10cPrimitive14setRectanglePTEfffff"), hook_drawBalloon, (void**)&next_drawBalloon);
    MH_CreateHook((LPVOID)get_native_symbol_ptr("_ZN7cCredit14displayTextureEPNS_9__texuvwhEiii"), hook_cCreditdisplayTexture, (void**)&next_cCreditdisplayTexture);
    MH_CreateHook((LPVOID)get_native_symbol_ptr("_ZN6_sFont3putEfft"), hook_sFont_put, (void**)&next_sFont_put);
    MH_CreateHook((LPVOID)get_native_symbol_ptr("_ZN9_sFiTFont11setClipRectEPKN8_sSerial6_sRectE"), hook_setClipRect, (void**)&next_setClipRect);

}
