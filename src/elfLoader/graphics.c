#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glad/gl.h>
#include "graphics.h"
#include "glHooks.h"
#include "../hooks/x11.h"
#include "../hooks/glx.h"
#include "../hooks/glut.h"
#include "../graphics/gpuVendor.h"
#include "symbol.h"
#include "../log/log.h"
#include "../config/config.h"

#define CG_PROFILE_VP20 6146
#define CG_PROFILE_FP20 6147
#define CG_PROFILE_VP30 6148
#define CG_PROFILE_FP30 6149
#define CG_PROFILE_ARBVP1 6150
#define CG_PROFILE_FP40 6151
#define CG_PROFILE_ARBFP1 7000
#define CG_PROFILE_VP40 7001
#define CG_PROFILE_GLSLV 7007
#define CG_PROFILE_GLSLF 7008
#define CG_PROFILE_GLSLG 7009
#define CG_PROGRAM_STRING 4106
#define CG_PROGRAM_SOURCE 4112
#define CG_COMPILE_MANUAL 4114
#define SF_SHADER_CAPACITY (1024u * 1024u)
static char s_cachedVP[SF_SHADER_CAPACITY + 1u];
static char s_cachedFP[SF_SHADER_CAPACITY + 1u];
static int s_lastProfile = 0;
static LONG s_dumpedVertexPrograms = 0;
static LONG s_dumpedFragmentPrograms = 0;
static LONG s_loggedConstantBinding = 0;

void *(*next_CgCreateProgram)(size_t *context, int program_type, const char *program, int profile, const char *entry,
                                   const char **args);
const char* (*next_CgGetProgramString)(char *program, int pname);
int (*next_CgGLIsProfileSupported)(int profile);

static CRITICAL_SECTION g_shader_lock;
static LONG g_shader_lock_ready = 0;
static INIT_ONCE g_cg_patch_once = INIT_ONCE_STATIC_INIT;
static INIT_ONCE g_cg_api_once = INIT_ONCE_STATIC_INIT;

static void (__cdecl *next_CgSetAutoCompile)(size_t *context, int flag);
static int (__cdecl *next_CgGetAutoCompile)(size_t *context);
static void *(__cdecl *next_CgGetNamedParameter)(void *program, const char *name);
static void (__cdecl *next_CgSetParameterSemantic)(void *parameter,
                                                    const char *semantic);
static void (__cdecl *next_CgCompileProgram)(void *program);

typedef union
{
    FARPROC raw;
    void (__cdecl *setAutoCompile)(size_t *, int);
    int (__cdecl *getAutoCompile)(size_t *);
    void *(__cdecl *getNamedParameter)(void *, const char *);
    void (__cdecl *setParameterSemantic)(void *, const char *);
    void (__cdecl *compileProgram)(void *);
} CgApiProc;

typedef struct
{
    DWORD pointerOperandRva;
    DWORD originalStringRva;
    const char *original;
    const char *amdReplacement;
    const char *intelReplacement;
} CgFormatPointerPatch;

/* Cg 1.4 ARBVP format strings. Whitespace around '.' is accepted by the ARB
 * grammar but avoids ID5's hard-coded "program.local" -> "program.env"
 * rewrite, so relative constant arrays remain in the AMD-safe local space. */
static const CgFormatPointerPatch s_cg140001LocalFormatPatches[] = {
    {0x071625u, 0x177B18u, "PARAM c%d = program.local[%d];\n", "PARAM c%d = program .local[%d];\n", "PARAM c%d = program.env[%d];\n"},
    {0x071643u, 0x177AECu, "PARAM c%d[%d] = { program.local[%d..%d] };\n", "PARAM c%d[%d] = { program .local[%d..%d] };\n", "PARAM c%d[%d] = { program.env[%d..%d] };\n"},
    {0x09B0CFu, 0x17A534u, "program.local[%d],\n", "program .local[%d],\n", "program.env[%d],\n"},
    {0x09B0E1u, 0x17A51Cu, "program.local[%d..%d],\n", "program .local[%d..%d],\n", "program.env[%d..%d],\n"},
    {0x09B34Du, 0x17A4D4u, "program.local[%d] };\n", "program .local[%d] };\n", "program.env[%d] };\n"},
    {0x09B35Fu, 0x17A4B8u, "program.local[%d..%d] };\n", "program .local[%d..%d] };\n", "program.env[%d..%d] };\n"},
};

static const CgFormatPointerPatch s_cg150014LocalFormatPatches[] = {
    {0x0CBC2Eu, 0x1E0B24u, "program.local[%d],\n", "program .local[%d],\n", "program.env[%d],\n"},
    {0x0CBC40u, 0x1E0B38u, "program.local[%d..%d],\n", "program .local[%d..%d],\n", "program.env[%d..%d],\n"},
    {0x0CBEBEu, 0x1E0B98u, "program.local[%d] };\n", "program .local[%d] };\n", "program.env[%d] };\n"},
    {0x0CBED2u, 0x1E0BB0u, "program.local[%d..%d] };\n", "program .local[%d..%d] };\n", "program.env[%d..%d] };\n"},
};

static const CgFormatPointerPatch s_cg150019LocalFormatPatches[] = {
    {0x0CF56Au, 0x1E4B2Cu, "program.local[%d],\n", "program .local[%d],\n", "program.env[%d],\n"},
    {0x0CF57Cu, 0x1E4B40u, "program.local[%d..%d],\n", "program .local[%d..%d],\n", "program.env[%d..%d],\n"},
    {0x0CF7FAu, 0x1E4BA0u, "program.local[%d] };\n", "program .local[%d] };\n", "program.env[%d] };\n"},
    {0x0CF80Eu, 0x1E4BB8u, "program.local[%d..%d] };\n", "program .local[%d..%d] };\n", "program.env[%d..%d] };\n"},
};

typedef struct
{
    DWORD resourceRva;
    const char *semantic;
    DWORD originalResource;
    DWORD replacementResource;
    DWORD componentCount;
    DWORD resourceCount;
} CgSemanticResourcePatch;

static const CgSemanticResourcePatch s_cg140001SemanticPatches[] = {
    {0x10AC04u, "POSITION",     0x20u, 0x10u, 4u, 1u},
    {0x10AC20u, "BLENDWEIGHT",  0x21u, 0x11u, 1u, 1u},
    {0x10AC3Cu, "NORMAL",       0x22u, 0x12u, 4u, 1u},
    {0x10AC58u, "COLOR",        0x23u, 0x13u, 4u, 2u},
    {0x10AC90u, "TEXCOORD",     0x28u, 0x18u, 4u, 8u},
};

static const CgSemanticResourcePatch s_cg150014SemanticPatches[] = {
    {0x13B148u, "POSITION",     0x20u, 0x10u, 4u, 1u},
    {0x13B168u, "BLENDWEIGHT",  0x21u, 0x11u, 1u, 1u},
    {0x13B188u, "NORMAL",       0x22u, 0x12u, 4u, 1u},
    {0x13B1A8u, "COLOR",        0x23u, 0x13u, 4u, 2u},
    {0x13B1E8u, "TEXCOORD",     0x28u, 0x18u, 4u, 8u},
};

static const CgSemanticResourcePatch s_cg150019SemanticPatches[] = {
    {0x13F150u, "POSITION",     0x20u, 0x10u, 4u, 1u},
    {0x13F170u, "BLENDWEIGHT",  0x21u, 0x11u, 1u, 1u},
    {0x13F190u, "NORMAL",       0x22u, 0x12u, 4u, 1u},
    {0x13F1B0u, "COLOR",        0x23u, 0x13u, 4u, 2u},
    {0x13F1F0u, "TEXCOORD",     0x28u, 0x18u, 4u, 8u},
};

typedef struct
{
    DWORD timeDateStamp;
    DWORD sizeOfImage;
    const char *version;
    BOOL legacyDescriptor;
    const CgSemanticResourcePatch *semanticPatches;
    size_t semanticPatchCount;
    const CgFormatPointerPatch *localFormatPatches;
    size_t localFormatPatchCount;
} CgPatchTarget;

static const CgPatchTarget s_cgPatchTargets[] = {
    {0x44110640u, 0x1AC000u, "1.4.0001", TRUE,
     s_cg140001SemanticPatches,
     sizeof(s_cg140001SemanticPatches) / sizeof(s_cg140001SemanticPatches[0]),
     s_cg140001LocalFormatPatches,
     sizeof(s_cg140001LocalFormatPatches) / sizeof(s_cg140001LocalFormatPatches[0])},
    {0x450F67D2u, 0x206000u, "1.5.0014", FALSE,
     s_cg150014SemanticPatches,
     sizeof(s_cg150014SemanticPatches) / sizeof(s_cg150014SemanticPatches[0]),
     s_cg150014LocalFormatPatches,
     sizeof(s_cg150014LocalFormatPatches) / sizeof(s_cg150014LocalFormatPatches[0])},
    {0x45DDA886u, 0x20A000u, "1.5.0019", FALSE,
     s_cg150019SemanticPatches,
     sizeof(s_cg150019SemanticPatches) / sizeof(s_cg150019SemanticPatches[0]),
     s_cg150019LocalFormatPatches,
     sizeof(s_cg150019LocalFormatPatches) / sizeof(s_cg150019LocalFormatPatches[0])},
};

static BOOL CALLBACK resolveCgCompilerApi(PINIT_ONCE once, PVOID parameter,
                                          PVOID *context)
{
    MEMORY_BASIC_INFORMATION mbi;
    HMODULE module;
    CgApiProc proc;

    (void)once;
    (void)parameter;
    (void)context;

    if (next_CgCreateProgram == NULL ||
        VirtualQuery((const void *)next_CgCreateProgram, &mbi, sizeof(mbi)) != sizeof(mbi))
        return TRUE;
    module = (HMODULE)mbi.AllocationBase;
    proc.raw = GetProcAddress(module, "cgSetAutoCompile");
    next_CgSetAutoCompile = proc.setAutoCompile;
    proc.raw = GetProcAddress(module, "cgGetAutoCompile");
    next_CgGetAutoCompile = proc.getAutoCompile;
    proc.raw = GetProcAddress(module, "cgGetNamedParameter");
    next_CgGetNamedParameter = proc.getNamedParameter;
    proc.raw = GetProcAddress(module, "cgSetParameterSemantic");
    next_CgSetParameterSemantic = proc.setParameterSemantic;
    proc.raw = GetProcAddress(module, "cgCompileProgram");
    next_CgCompileProgram = proc.compileProgram;
    return TRUE;
}

static BOOL canBindCompilerUniformSemantics(void)
{
    InitOnceExecuteOnce(&g_cg_api_once, resolveCgCompilerApi, NULL, NULL);
    return next_CgSetAutoCompile != NULL && next_CgGetAutoCompile != NULL &&
           next_CgGetNamedParameter != NULL &&
           next_CgSetParameterSemantic != NULL && next_CgCompileProgram != NULL;
}

static unsigned int bindIdasConstantSemantics(void *program)
{
    static const char *names[] = {
        "mtxWorldViewProj",
        "g_mtxWorldViewProj",
        "g_m4WorldViewProj",
        "f2UV_Offset",
    };
    unsigned int bound = 0;
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        void *parameter = next_CgGetNamedParameter(program, names[i]);
        if (parameter == NULL)
            continue;
        next_CgSetParameterSemantic(parameter, "C0");
        ++bound;
    }
    return bound;
}

static BOOL isPortableGpuVendor(uint8_t gpuVendor)
{
    return gpuVendor == AMD_GPU || gpuVendor == ATI_GPU ||
           gpuVendor == INTEL_GPU;
}

static const IMAGE_SECTION_HEADER *cgImageSections(const BYTE *base,
                                                   const IMAGE_NT_HEADERS32 *nt)
{
    const IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(nt);
    const BYTE *end = (const BYTE *)(sections + nt->FileHeader.NumberOfSections);

    if (nt->FileHeader.NumberOfSections == 0u ||
        end < (const BYTE *)sections ||
        end > base + nt->OptionalHeader.SizeOfHeaders)
        return NULL;
    return sections;
}

static BYTE *findUniqueCgImageBytes(BYTE *base, const IMAGE_NT_HEADERS32 *nt,
                                    const void *needle, size_t needleLength,
                                    DWORD requiredCharacteristics,
                                    unsigned int *matchCount)
{
    const IMAGE_SECTION_HEADER *sections = cgImageSections(base, nt);
    BYTE *match = NULL;
    unsigned int count = 0;
    unsigned int sectionIndex;

    if (matchCount != NULL)
        *matchCount = 0;
    if (sections == NULL || needle == NULL || needleLength == 0u)
        return NULL;

    for (sectionIndex = 0; sectionIndex < nt->FileHeader.NumberOfSections;
         ++sectionIndex)
    {
        DWORD sectionRva = sections[sectionIndex].VirtualAddress;
        DWORD sectionSize = sections[sectionIndex].Misc.VirtualSize;
        BYTE *cursor;
        BYTE *end;

        if ((sections[sectionIndex].Characteristics & requiredCharacteristics) !=
            requiredCharacteristics)
            continue;
        if (sectionSize == 0u)
            sectionSize = sections[sectionIndex].SizeOfRawData;
        if (sectionRva >= nt->OptionalHeader.SizeOfImage ||
            sectionSize > nt->OptionalHeader.SizeOfImage - sectionRva ||
            needleLength > sectionSize)
            continue;

        cursor = base + sectionRva;
        end = cursor + sectionSize - needleLength + 1u;
        for (; cursor < end; ++cursor)
        {
            if (memcmp(cursor, needle, needleLength) == 0)
            {
                match = cursor;
                ++count;
            }
        }
    }
    if (matchCount != NULL)
        *matchCount = count;
    return count == 1u ? match : NULL;
}

static DWORD *findUniqueCgImmediateOperand(BYTE *base,
                                           const IMAGE_NT_HEADERS32 *nt,
                                           BYTE opcode, const void *value,
                                           unsigned int *matchCount)
{
    BYTE pattern[sizeof(BYTE) + sizeof(DWORD)];
    DWORD address = (DWORD)(uintptr_t)value;
    BYTE *instruction;

    pattern[0] = opcode;
    memcpy(pattern + 1, &address, sizeof(address));
    instruction = findUniqueCgImageBytes(
        base, nt, pattern, sizeof(pattern), IMAGE_SCN_MEM_EXECUTE, matchCount);
    return instruction != NULL ? (DWORD *)(instruction + 1) : NULL;
}

static BOOL patchCgPointerOperand(DWORD *operand, const void *replacement,
                                  const char *description)
{
    DWORD oldProtect;
    DWORD restoreProtect;

    if (!VirtualProtect(operand, sizeof(*operand), PAGE_EXECUTE_READWRITE,
                        &oldProtect))
    {
        log_error(plfHooks, "[cg] cannot patch %s: %lu", description,
                  GetLastError());
        return FALSE;
    }
    *operand = (DWORD)(uintptr_t)replacement;
    if (!VirtualProtect(operand, sizeof(*operand), oldProtect, &restoreProtect))
        log_warn(plfHooks, "[cg] cannot restore protection for %s: %lu",
                 description, GetLastError());
    return TRUE;
}

static BOOL CALLBACK patchCgCompilerForNonNvidia(PINIT_ONCE once,
                                                  PVOID parameter,
                                                  PVOID *context)
{
    static const char shadowOption[] = "OPTION ARB_fragment_program_shadow;\n";
    static const char shadowSampler[] = "SHADOW2D";
    static const char intelShadowOption[] =
        "#OPTION ARB_fragment_program_shadow;\n";
    static const char intel2DSampler[sizeof(shadowSampler)] = "2D";
    MEMORY_BASIC_INFORMATION mbi;
    BYTE *base;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS32 *nt;
    const CgPatchTarget *target = NULL;
    const char *formatReplacement;
    DWORD *shadowOptionOperand = NULL;
    DWORD *shadowSamplerOperand = NULL;
    uint8_t gpuVendor = getGPUVendorID();
    BOOL patchFormats = gpuVendor == AMD_GPU || gpuVendor == ATI_GPU ||
                        gpuVendor == INTEL_GPU;
    BOOL intel = gpuVendor == INTEL_GPU;
    DWORD oldProtect;
    size_t i;

    (void)once;
    (void)parameter;
    (void)context;

    if (next_CgCreateProgram == NULL ||
        VirtualQuery((const void *)next_CgCreateProgram, &mbi, sizeof(mbi)) != sizeof(mbi))
    {
        log_error(plfHooks, "[cg] cannot locate the loaded Cg module for non-NVIDIA compatibility patch");
        return TRUE;
    }

    base = (BYTE *)mbi.AllocationBase;
    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        log_error(plfHooks, "[cg] loaded Cg module has an invalid DOS header");
        return TRUE;
    }

    nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        log_error(plfHooks, "[cg] loaded Cg module is not a supported PE32 image");
        return TRUE;
    }

    for (i = 0; i < sizeof(s_cgPatchTargets) / sizeof(s_cgPatchTargets[0]); ++i)
    {
        if (nt->FileHeader.TimeDateStamp == s_cgPatchTargets[i].timeDateStamp &&
            nt->OptionalHeader.SizeOfImage == s_cgPatchTargets[i].sizeOfImage)
        {
            target = &s_cgPatchTargets[i];
            break;
        }
    }

    if (target == NULL)
    {
        log_error(plfHooks,
                  "[cg] unsupported Cg.dll for non-NVIDIA compatibility patch (timestamp=0x%08lx image=0x%08lx)",
                  nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
        return TRUE;
    }

    for (i = 0; i < target->semanticPatchCount; ++i)
    {
        const CgSemanticResourcePatch *patch = &target->semanticPatches[i];
        DWORD *resource = (DWORD *)(base + patch->resourceRva);
        const char *semantic;
        size_t semanticLength = strlen(patch->semantic) + 1u;

        if (patch->resourceRva < 0x10u ||
            patch->resourceRva + 4u * sizeof(*resource) > nt->OptionalHeader.SizeOfImage)
        {
            log_error(plfHooks, "[cg] Cg %s %s resource RVA is outside the image",
                      target->version, patch->semantic);
            return TRUE;
        }

        semantic = *(const char **)((BYTE *)resource - 0x10);
        if ((uintptr_t)semantic < (uintptr_t)base ||
            (uintptr_t)semantic + semanticLength >
                (uintptr_t)base + nt->OptionalHeader.SizeOfImage ||
            memcmp(semantic, patch->semantic, semanticLength) != 0 ||
            resource[-3] != 0u || resource[-2] != 8u ||
            resource[-1] != patch->componentCount ||
            resource[0] != patch->originalResource ||
            (target->legacyDescriptor
                 ? (resource[1] != patch->resourceCount || resource[2] != 9u ||
                    (uintptr_t)resource[3] < (uintptr_t)base ||
                    (uintptr_t)resource[3] >=
                        (uintptr_t)base + nt->OptionalHeader.SizeOfImage)
                 : (resource[1] != 0xFFFFFFFFu ||
                    resource[2] != patch->resourceCount || resource[3] != 9u)))
        {
            log_error(plfHooks, "[cg] Cg %s %s descriptor validation failed",
                      target->version, patch->semantic);
            return TRUE;
        }
    }

    for (i = 0; patchFormats && i < target->localFormatPatchCount; ++i)
    {
        const CgFormatPointerPatch *patch = &target->localFormatPatches[i];
        DWORD *operand = (DWORD *)(base + patch->pointerOperandRva);
        const char *original = (const char *)(base + patch->originalStringRva);
        size_t originalLength = strlen(patch->original) + 1u;

        if (patch->pointerOperandRva < 1u ||
            patch->pointerOperandRva + sizeof(*operand) > nt->OptionalHeader.SizeOfImage ||
            patch->originalStringRva + originalLength > nt->OptionalHeader.SizeOfImage ||
            *((BYTE *)operand - 1) != 0x68u ||
            *operand != (DWORD)(uintptr_t)original ||
            memcmp(original, patch->original, originalLength) != 0)
        {
            log_error(plfHooks,
                      "[cg] Cg %s local-format validation failed at RVA 0x%08lx",
                      target->version, patch->pointerOperandRva);
            return TRUE;
        }
    }

    if (intel)
    {
        BYTE *optionString;
        BYTE *samplerString;
        unsigned int optionStringMatches;
        unsigned int samplerStringMatches;
        unsigned int optionOperandMatches = 0;
        unsigned int samplerOperandMatches = 0;

        optionString = findUniqueCgImageBytes(
            base, nt, shadowOption, sizeof(shadowOption), IMAGE_SCN_MEM_READ,
            &optionStringMatches);
        samplerString = findUniqueCgImageBytes(
            base, nt, shadowSampler, sizeof(shadowSampler), IMAGE_SCN_MEM_READ,
            &samplerStringMatches);

        /* Cg 1.4 does not contain the ARB_fragment_program_shadow emitter used
         * by Cg 1.5, so absence of both signatures means there is nothing to
         * downgrade for that compiler. */
        if (optionStringMatches != 0u || samplerStringMatches != 0u)
        {
            if (optionString == NULL || samplerString == NULL)
            {
                log_error(plfHooks,
                          "[cg] Cg %s Intel shadow-emitter signature is ambiguous option=%u sampler=%u",
                          target->version, optionStringMatches,
                          samplerStringMatches);
                return TRUE;
            }
            shadowOptionOperand = findUniqueCgImmediateOperand(
                base, nt, 0x68u, optionString, &optionOperandMatches);
            shadowSamplerOperand = findUniqueCgImmediateOperand(
                base, nt, 0xBEu, samplerString, &samplerOperandMatches);
            if (shadowOptionOperand == NULL || shadowSamplerOperand == NULL)
            {
                log_error(plfHooks,
                          "[cg] Cg %s Intel shadow-emitter reference is ambiguous option=%u sampler=%u",
                          target->version, optionOperandMatches,
                          samplerOperandMatches);
                return TRUE;
            }
        }
    }

    for (i = 0; i < target->semanticPatchCount; ++i)
    {
        const CgSemanticResourcePatch *patch = &target->semanticPatches[i];
        DWORD *resource = (DWORD *)(base + patch->resourceRva);

        if (!VirtualProtect(resource, sizeof(*resource), PAGE_READWRITE, &oldProtect))
        {
            log_error(plfHooks,
                      "[cg] cannot make Cg %s %s descriptor writable: %lu",
                      target->version, patch->semantic, GetLastError());
            return TRUE;
        }
        *resource = patch->replacementResource;
        VirtualProtect(resource, sizeof(*resource), oldProtect, &oldProtect);
    }
    log_debug(plfHooks,
              "[cg] Cg %s non-NVIDIA compatibility: mapped %zu vertex semantics to generic ATTR resources",
              target->version, target->semanticPatchCount);

    for (i = 0; patchFormats && i < target->localFormatPatchCount; ++i)
    {
        const CgFormatPointerPatch *patch = &target->localFormatPatches[i];
        DWORD *operand = (DWORD *)(base + patch->pointerOperandRva);
        formatReplacement = intel ? patch->intelReplacement
                                  : patch->amdReplacement;
        if (!patchCgPointerOperand(operand, formatReplacement,
                                   "ARB parameter-space emitter"))
            return TRUE;
    }

    if (shadowOptionOperand != NULL &&
        (!patchCgPointerOperand(shadowOptionOperand, intelShadowOption,
                                "Intel ARB shadow option emitter") ||
         !patchCgPointerOperand(shadowSamplerOperand, intel2DSampler,
                                "Intel ARB shadow sampler emitter")))
        return TRUE;

    FlushInstructionCache(GetCurrentProcess(), base, nt->OptionalHeader.SizeOfImage);
    log_debug(plfHooks,
              "[cg] Cg %s non-NVIDIA compatibility: emit ARB program.%s bindings%s",
              target->version, intel ? "env" : "local",
              shadowOptionOperand != NULL ? " with Intel 2D shadow downgrade" : "");
    return TRUE;
}

static void shaderCacheLock(void)
{
    if (InterlockedCompareExchange(&g_shader_lock_ready, 1, 0) == 0)
        InitializeCriticalSection(&g_shader_lock);
    EnterCriticalSection(&g_shader_lock);
}

static void dumpCgAssemblyIfRequested(int profile, const char *assembly,
                                      size_t length)
{
    const char *directory = getenv("KITTEN_CG_DUMP_DIR");
    LONG sequence;
    const char *stage;
    char path[MAX_PATH];
    HANDLE file;
    DWORD written;

    if (directory == NULL || directory[0] == '\0' || assembly == NULL || length == 0u)
        return;

    if (profile == CG_PROFILE_ARBVP1)
    {
        stage = "vp";
        sequence = InterlockedIncrement(&s_dumpedVertexPrograms);
    }
    else if (profile == CG_PROFILE_ARBFP1)
    {
        stage = "fp";
        sequence = InterlockedIncrement(&s_dumpedFragmentPrograms);
    }
    else
        return;

    CreateDirectoryA(directory, NULL);
    if (snprintf(path, sizeof(path), "%s\\cg-%s-%06ld.arb", directory,
                 stage, sequence) < 0)
        return;
    file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    if (!WriteFile(file, assembly, (DWORD)length, &written, NULL) ||
        written != (DWORD)length)
        log_warn(plfHooks, "[cg] incomplete compiler-output dump: %s", path);
    CloseHandle(file);
}

int hook_CgGLIsProfileSupported(int profile)
{
    uint8_t gpuVendor = getGPUVendorID();

    if (gpuVendor == NVIDIA_GPU)
    {
        return next_CgGLIsProfileSupported ? next_CgGLIsProfileSupported(profile) : 0;
    }

    switch (profile)
    {
        case CG_PROFILE_ARBVP1:
        case CG_PROFILE_ARBFP1:
        case CG_PROFILE_GLSLV:
        case CG_PROFILE_GLSLF:
        case CG_PROFILE_GLSLG:
            return 1;
        default:
            return 0;
    }
}

const char *hook_CgGetProgramString(char *program, int pname)
{
    const char *value;
    const char *cached;

    if (pname != CG_PROGRAM_STRING)
        return next_CgGetProgramString(program, pname);

    shaderCacheLock();

    cached = (s_lastProfile == CG_PROFILE_ARBVP1) ? s_cachedVP : s_cachedFP;

    if (program == NULL)
    {
        if (cached[0] != '\0')
        {
            LeaveCriticalSection(&g_shader_lock);
            return cached;
        }
        log_warn(plfHooks, "[cg] cgGetProgramString NULL, no cache for profile %d\n", s_lastProfile);
        LeaveCriticalSection(&g_shader_lock);
        return "";
    }

    value = next_CgGetProgramString(program, pname);
    if (value != NULL && value[0] != '\0')
    {
        LeaveCriticalSection(&g_shader_lock);
        return value;
    }

    if (cached[0] != '\0')
    {
        log_warn(plfHooks, "[cg] cgGetProgramString empty, using cache for profile %d\n", s_lastProfile);
        LeaveCriticalSection(&g_shader_lock);
        return cached;
    }
    LeaveCriticalSection(&g_shader_lock);
    return "";
}


char *hook_CgCreateProgram(size_t *context, int program_type, const char *program, int profile, const char *entry, const char **args)
{
    uint8_t gpuVendor = getGPUVendorID();
    BOOL useManualConstantBinding;
    int previousAutoCompile = 0;
    unsigned int boundConstants = 0;

    if (gpuVendor == NVIDIA_GPU)
    {
        return next_CgCreateProgram(context, program_type, program, profile, entry, NULL);
    }

    if (isPortableGpuVendor(gpuVendor))
        InitOnceExecuteOnce(&g_cg_patch_once, patchCgCompilerForNonNvidia,
                            NULL, NULL);

    if (profile == CG_PROFILE_VP20 || profile == CG_PROFILE_VP30 ||
        profile == CG_PROFILE_ARBVP1 || profile == CG_PROFILE_VP40)
        profile = CG_PROFILE_ARBVP1;
    else if (profile == CG_PROFILE_FP20 || profile == CG_PROFILE_FP30 ||
             profile == CG_PROFILE_FP40 || profile == CG_PROFILE_ARBFP1)
        profile = CG_PROFILE_ARBFP1;
    else
        log_debug(plfHooks, "profile %d passed through unnormalized\n", profile);

    const char *op6150[] = {"NumTemps=31", "MaxInstructions=1024", "MaxAddressRegs=1", "MaxLocalParams=256", NULL};

    const char *op7000[] = {"NumTemps=256",
                            "NumInstructionSlots=1447",
                            "MaxLocalParams=256",
                            "NumTexInstructionSlots=1447",
                            "NumMathInstructionSlots=1447",
                            "MaxTexIndirections=128",
                            "MaxDrawBuffers=8",
                            NULL};

    if (profile == CG_PROFILE_ARBVP1)
    {
        args = op6150;
    }
    else if (profile == CG_PROFILE_ARBFP1)
    {
        args = op7000;
    }

    useManualConstantBinding =
        isPortableGpuVendor(gpuVendor) &&
        program_type == CG_PROGRAM_SOURCE && profile == CG_PROFILE_ARBVP1 &&
        canBindCompilerUniformSemantics();
    if (useManualConstantBinding)
    {
        previousAutoCompile = next_CgGetAutoCompile(context);
        next_CgSetAutoCompile(context, CG_COMPILE_MANUAL);
    }

    char *result = next_CgCreateProgram(context, program_type, program, profile, entry, args);

    if (useManualConstantBinding)
    {
        if (result != NULL)
        {
            boundConstants = bindIdasConstantSemantics(result);
            next_CgCompileProgram(result);
        }
        next_CgSetAutoCompile(context, previousAutoCompile);
        if (boundConstants != 0u &&
            InterlockedCompareExchange(&s_loggedConstantBinding, 1, 0) == 0)
            log_debug(plfHooks,
                      "[cg] binding IDAS C0 uniforms through Cg's pre-compile semantic API");
    }

    if (result != NULL)
    {
        const char *str = next_CgGetProgramString((char *)result, CG_PROGRAM_STRING);
        if (str != NULL && str[0] != '\0')
        {
            const char *terminator = (const char *)memchr(str, '\0', SF_SHADER_CAPACITY);
            if (terminator != NULL)
            {
                size_t length = (size_t)(terminator - str);
                dumpCgAssemblyIfRequested(profile, str, length);
                shaderCacheLock();
                if (profile == CG_PROFILE_ARBVP1)
                    memcpy(s_cachedVP, str, length + 1u);
                else if (profile == CG_PROFILE_ARBFP1)
                    memcpy(s_cachedFP, str, length + 1u);
                s_lastProfile = profile;
                LeaveCriticalSection(&g_shader_lock);
            }
        }
    }

    return result;
}

void hook_glProgramParameters4fvNV(GLenum target, GLuint index, GLsizei count, const GLfloat *v)
{
    wrap_glProgramEnvParameters4fvEXT(target, index, count, v);
}

void graphicsBridge_initBridges(void)
{
    log_debug(plfGUI, "Initializing Graphics Bridges (OpenGL/GLX/X11)...");

    symbol_register("glutInit", hook_GlutInit, NULL);
    symbol_register("glutInitDisplayMode", hook_GlutInitDisplayMode, NULL);
    symbol_register("glutInitWindowSize", hook_GlutInitWindowSize, NULL);
    symbol_register("glutInitWindowPosition", hook_GlutInitWindowPosition, NULL);
    symbol_register("glutEnterGameMode", hook_GlutEnterGameMode, NULL);
    symbol_register("glutLeaveGameMode", hook_GlutLeaveGameMode, NULL);
    symbol_register("glutMainLoop", hook_GlutMainLoop, NULL);
    symbol_register("glutDisplayFunc", hook_GlutDisplayFunc, NULL);
    symbol_register("glutIdleFunc", hook_GlutIdleFunc, NULL);
    symbol_register("glutPostRedisplay", hook_GlutPostRedisplay, NULL);
    symbol_register("glutSwapBuffers", hook_GlutSwapBuffers, NULL);
    symbol_register("glutGet", hook_GlutGet, NULL);
    symbol_register("glutSetCursor", hook_GlutSetCursor, NULL);
    symbol_register("glutGameModeString", hook_GlutGameModeString, NULL);
    symbol_register("glutBitmapCharacter", hook_GlutBitmapCharacter, NULL);
    symbol_register("glutBitmapWidth", hook_GlutBitmapWidth, NULL);
    symbol_register("glutKeyboardFunc", hook_GlutKeyboardFunc, NULL);

    static void *glutBitmap9By15 = (void *)1;
    static void *glutBitmap8By13 = (void *)2;
    static void *glutBitmapTimesRoman10 = (void *)3;
    static void *glutBitmapTimesRoman24 = (void *)4;
    static void *glutBitmapHelvetica10 = (void *)5;
    static void *glutBitmapHelvetica12 = (void *)6;
    static void *glutBitmapHelvetica18 = (void *)7;
    symbol_register("glutBitmap9By15", &glutBitmap9By15, NULL);
    symbol_register("glutBitmap8By13", &glutBitmap8By13, NULL);
    symbol_register("glutBitmapTimesRoman10", &glutBitmapTimesRoman10, NULL);
    symbol_register("glutBitmapTimesRoman24", &glutBitmapTimesRoman24, NULL);
    symbol_register("glutBitmapHelvetica10", &glutBitmapHelvetica10, NULL);
    symbol_register("glutBitmapHelvetica12", &glutBitmapHelvetica12, NULL);
    symbol_register("glutBitmapHelvetica18", &glutBitmapHelvetica18, NULL);

    symbol_register("gluPerspective", hook_gluPerspective, NULL);
    symbol_register("gluLookAt", hook_gluLookAt, NULL);

    symbol_register("XF86VidModeGetAllModeLines", hook_XF86VidModeGetAllModeLines, NULL);
    symbol_register("XF86VidModeGetModeLine", hook_XF86VidModeGetModeLine, NULL);
    symbol_register("XF86VidModeSwitchToMode", hook_XF86VidModeSwitchToMode, NULL);
    symbol_register("XF86VidModeSetViewPort", hook_XF86VidModeSetViewPort, NULL);
    symbol_register("XF86VidModeGetViewPort", hook_XF86VidModeGetViewPort, NULL);
    symbol_register("XOpenDisplay", hook_XOpenDisplay, NULL);
    symbol_register("XCloseDisplay", hook_XCloseDisplay, NULL);
    symbol_register("XInitThreads", hook_XInitThreads, NULL);
    symbol_register("XLockDisplay", hook_XLockDisplay, NULL);
    symbol_register("XUnlockDisplay", hook_XUnlockDisplay, NULL);
    symbol_register("XSync", hook_XSync, NULL);
    symbol_register("XFlush", hook_XFlush, NULL);
    symbol_register("XCreateWindow", hook_XCreateWindow, NULL);
    symbol_register("XDestroyWindow", hook_XDestroyWindow, NULL);
    symbol_register("XMapWindow", hook_XMapWindow, NULL);
    symbol_register("XMoveWindow", hook_XMoveWindow, NULL);
    symbol_register("XPending", hook_XPending, NULL);
    symbol_register("XNextEvent", hook_XNextEvent, NULL);
    symbol_register("XTranslateCoordinates", hook_XTranslateCoordinates, NULL);
    symbol_register("XQueryPointer", hook_XQueryPointer, NULL);
    symbol_register("XWarpPointer", hook_XWarpPointer, NULL);
    symbol_register("XGrabPointer", hook_XGrabPointer, NULL);
    symbol_register("XUngrabPointer", hook_XUngrabPointer, NULL);
    symbol_register("XLookupString", hook_XLookupString, NULL);
    symbol_register("XStoreName", hook_XStoreName, NULL);
    symbol_register("XCreateColormap", hook_XCreateColormap, NULL);
    symbol_register("XInternAtom", hook_XInternAtom, NULL);
    symbol_register("XParseColor", hook_XParseColor, NULL);
    symbol_register("XFree", hook_XFree, NULL);
    symbol_register("XSetErrorHandler", hook_XSetErrorHandler, NULL);
    symbol_register("XGetErrorText", hook_XGetErrorText, NULL);
    symbol_register("XCreatePixmapCursor", hook_XCreatePixmapCursor, NULL);
    symbol_register("XCreatePixmapFromBitmapData", hook_XCreatePixmapFromBitmapData, NULL);
    symbol_register("XFreePixmap", hook_XFreePixmap, NULL);
    symbol_register("XFreeCursor", hook_XFreeCursor, NULL);
    symbol_register("XDefineCursor", hook_XDefineCursor, NULL);
    symbol_register("XSetInputFocus", hook_XSetInputFocus, NULL);
    symbol_register("XSetWMProtocols", hook_XSetWMProtocols, NULL);
    symbol_register("XSetWMProperties", hook_XSetWMProperties, NULL);
    symbol_register("XStringListToTextProperty", hook_XStringListToTextProperty, NULL);
    symbol_register("XSetCloseDownMode", hook_XSetCloseDownMode, NULL);
    symbol_register("XAutoRepeatOn", hook_XAutoRepeatOn, NULL);
    symbol_register("XGrabKeyboard", hook_XGrabKeyboard, NULL);
    symbol_register("XUngrabKeyboard", hook_XUngrabKeyboard, NULL);
    symbol_register("glXChooseVisual", hook_GlxChooseVisual, NULL);
    symbol_register("glXCreateContext", hook_GlxCreateContext, NULL);
    symbol_register("glXMakeCurrent", hook_GlxMakeCurrent, NULL);
    symbol_register("glXSwapBuffers", hook_GlxSwapBuffers, NULL);
    symbol_register("glXDestroyContext", hook_GlxDestroyContext, NULL);   
    symbol_register("glXGetCurrentDisplay", hook_GlxGetCurrentDisplay, NULL);
    symbol_register("glXGetCurrentDrawable", hook_GlxGetCurrentDrawable, NULL);
    symbol_register("glXGetCurrentContext", hook_GlxGetCurrentContext, NULL);
    symbol_register("glXGetProcAddressARB", hook_GlxGetProcAddress, NULL);
    symbol_register("glXQueryExtensionsString", hook_GlxQueryExtensionsString, NULL);
    symbol_register("glXQueryServerString", hook_GlxQueryServerString, NULL);
    symbol_register("glXGetClientString", hook_GlxGetClientString, NULL);
    symbol_register("glXSwapIntervalSGI", hook_GlxSwapIntervalSGI, NULL);
    symbol_register("glXChooseFBConfigSGIX", hook_GlxChooseFBConfigSGIX, NULL);
    symbol_register("glXGetFBConfigAttribSGIX", hook_GlxGetFBConfigAttribSGIX, NULL);
    symbol_register("glXCreateContextWithConfigSGIX", hook_GlxCreateContextWithConfigSGIX, NULL);
    symbol_register("glXCreateGLXPbufferSGIX", hook_GlxCreateGLXPbufferSGIX, NULL);
    symbol_register("glXDestroyGLXPbufferSGIX", hook_GlxDestroyGLXPbufferSGIX, NULL);
    symbol_register("glProgramParameters4fvNV", hook_glProgramParameters4fvNV, NULL);
    symbol_register("glVertex3f", wrap_glVertex3f, NULL);
    symbol_register("glNormal3f", wrap_glNormal3f, NULL);
    symbol_register("glNormal3fv", wrap_glNormal3fv, NULL);
    symbol_register("glColor4ub", wrap_glColor4ub, NULL);
    symbol_register("glTexCoord2f", wrap_glTexCoord2f, NULL);
    symbol_register("glMultiTexCoord2fARB", wrap_glMultiTexCoord2fARB, NULL);
    symbol_register("glProgramLocalParameter4fvARB", wrap_glProgramLocalParameter4fvARB, NULL);
    symbol_register("glClientActiveTexture", wrap_glClientActiveTexture, NULL);
    symbol_register("glEnableClientState", wrap_glEnableClientState, NULL);
    symbol_register("glDisableClientState", wrap_glDisableClientState, NULL);
    symbol_register("glVertexPointer", wrap_glVertexPointer, NULL);
    symbol_register("glNormalPointer", wrap_glNormalPointer, NULL);
    symbol_register("glColorPointer", wrap_glColorPointer, NULL);
    symbol_register("glTexCoordPointer", wrap_glTexCoordPointer, NULL);
    symbol_register("cgCreateProgram", hook_CgCreateProgram, (void **)&next_CgCreateProgram);
    symbol_register("cgGetProgramString", hook_CgGetProgramString, (void **)&next_CgGetProgramString);
    symbol_register("cgGLIsProfileSupported", hook_CgGLIsProfileSupported, (void **)&next_CgGLIsProfileSupported);
}
