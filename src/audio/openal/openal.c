/* The guest's static nForce OpenAL, forwarded to the installed OpenAL32.dll. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include <MinHook.h>

#include "openal.h"
#include "../../elfLoader/symbol.h"
#include "../../log/log.h"

static const char *const g_names[] = {
    "alEnable", "alDisable", "alIsEnabled", "alGetString", "alGetBooleanv", "alGetIntegerv",
    "alGetFloatv", "alGetDoublev", "alGetBoolean", "alGetInteger", "alGetFloat", "alGetDouble",
    "alGetError", "alIsExtensionPresent", "alGetProcAddress", "alGetEnumValue",
    "alListenerf", "alListener3f", "alListenerfv", "alListeneri", "alListener3i", "alListeneriv",
    "alGetListenerf", "alGetListener3f", "alGetListenerfv", "alGetListeneri", "alGetListener3i",
    "alGetListeneriv", "alGenSources", "alDeleteSources", "alIsSource", "alSourcef", "alSource3f",
    "alSourcefv", "alSourcei", "alSource3i", "alSourceiv", "alGetSourcef", "alGetSource3f",
    "alGetSourcefv", "alGetSourcei", "alGetSource3i", "alGetSourceiv", "alSourcePlayv",
    "alSourceStopv", "alSourceRewindv", "alSourcePausev", "alSourcePlay", "alSourceStop",
    "alSourceRewind", "alSourcePause", "alSourceQueueBuffers", "alSourceUnqueueBuffers",
    "alGenBuffers", "alDeleteBuffers", "alIsBuffer", "alBufferData", "alBufferf", "alBuffer3f",
    "alBufferfv", "alBufferi", "alBuffer3i", "alBufferiv", "alGetBufferf", "alGetBuffer3f",
    "alGetBufferfv", "alGetBufferi", "alGetBuffer3i", "alGetBufferiv", "alDopplerFactor",
    "alDopplerVelocity", "alSpeedOfSound", "alDistanceModel",
    "alcCreateContext", "alcMakeContextCurrent", "alcProcessContext", "alcSuspendContext",
    "alcDestroyContext", "alcGetCurrentContext", "alcGetContextsDevice", "alcOpenDevice",
    "alcCloseDevice", "alcGetError", "alcIsExtensionPresent", "alcGetProcAddress",
    "alcGetEnumValue", "alcGetString", "alcGetIntegerv",
};

/* nForce-only extensions, unused by the game. */
static void nv_extension_unused(void)
{
}

int openal_install_hooks(const char *hostDll)
{
    static int done;
    if (done)
        return 0;
    done = 1;

    if (!hostDll || !*hostDll)
    {
        log_warn(plfMisc, "OpenAL: no library configured; the game will be silent");
        return 0;
    }
    HMODULE dll = LoadLibraryA(hostDll);
    if (!dll)
    {
        log_warn(plfMisc, "OpenAL: \"%s\" could not be loaded (error %lu); the game will be silent. "
                 "Install OpenAL 1.1 or OpenAL Soft (32-bit), as with the Cg Toolkit.",
                 hostDll, (unsigned long)GetLastError());
        return 0;
    }

    int installed = 0, missing = 0;
    for (size_t i = 0; i < sizeof(g_names) / sizeof(g_names[0]); i++)
    {
        void *guest = get_native_symbol_ptr(g_names[i]);
        if (!guest)
            continue;
        void *host = (void *)GetProcAddress(dll, g_names[i]);
        if (!host)
        {
            missing++;
            log_warn(plfMisc, "OpenAL: %s does not export %s; the guest's own code keeps it", hostDll, g_names[i]);
            continue;
        }
        if (MH_CreateHook(guest, host, NULL) == MH_OK)
            installed++;
        else
            log_warn(plfMisc, "OpenAL: could not hook %s", g_names[i]);
    }

    static const char *const nvOnly[] = {"alBufferDataNV", "alChannelVolumeNV"};
    for (size_t i = 0; i < sizeof(nvOnly) / sizeof(nvOnly[0]); i++)
    {
        void *guest = get_native_symbol_ptr(nvOnly[i]);
        if (guest && MH_CreateHook(guest, (void *)nv_extension_unused, NULL) == MH_OK)
            installed++;
    }

    char path[MAX_PATH] = "";
    GetModuleFileNameA(dll, path, sizeof(path));
    log_info(plfMisc, "OpenAL: %d entry points on %s%s", installed, path,
             missing ? " (some missing, see above)" : "");
    return installed;
}
