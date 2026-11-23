#pragma once

#include <stdbool.h>

/* Workarounds for running a game's NVIDIA-only GL/Cg path on other GPUs.
 *
 * Nothing is on by default: each GameModule lists the ones its renderer needs
 * in .gpuCompat, and they only ever take effect on a non-NVIDIA GPU.  The GPU
 * vendor is read at each query, since it is refined from the GL context once
 * that exists. */
typedef enum
{
    /* cgGLIsProfileSupported reports only the ARB/GLSL profiles, and
     * cgCreateProgram compiles every vp/fp profile as arbvp1/arbfp1. */
    GPU_COMPAT_CG_ARB_PROFILES = 1u << 0,

    /* AMD/ATI/Intel: patch Cg.dll to bind vertex semantics to generic ATTRn,
     * and feed glVertex*, glNormal*, gl*Pointer etc. to those attributes. */
    GPU_COMPAT_GENERIC_ATTRIBS = 1u << 1,

    /* AMD/ATI/Intel: patch Cg.dll's program.local/env emitter and route the
     * constant uploads to match: AMD mirrors env into local, Intel redirects
     * local into env. */
    GPU_COMPAT_CONSTANT_BANKS = 1u << 2,

    /* AMD/ATI/Intel: pin the game's world-view-projection uniforms to C0 via
     * cgSetParameterSemantic before Cg compiles the vertex program. */
    GPU_COMPAT_PINNED_UNIFORMS = 1u << 3,

    /* Intel: patch Cg.dll to emit plain 2D samplers instead of
     * ARB_fragment_program_shadow. */
    GPU_COMPAT_INTEL_SHADOW = 1u << 4,

    /* vp40/fp40 programs (OPTION NV_*_program) passed to glProgramStringARB
     * are rebuilt for arbvp1/arbfp1 from the Cg source named in their header. */
    GPU_COMPAT_NV_ASM_RECOMPILE = 1u << 5,

    /* Mirror generic attribute arrays onto the conventional ones they alias
     * on NVIDIA (0 position, 2 normal, 3 colour, 8+n texcoord n, ...). */
    GPU_COMPAT_NV_ATTRIB_ALIASING = 1u << 6,

    /* Refuse the game's cached NVIDIA assembly (asm_gl / asm_lbg) so it asks
     * Cg to compile the source instead. */
    GPU_COMPAT_SKIP_ASM_CACHE = 1u << 7,
} GpuCompatFeature;

/* Everything that needs the cgCreateProgram hook to leave its NVIDIA path. */
#define GPU_COMPAT_CG_COMPILER                                                                   \
    (GPU_COMPAT_CG_ARB_PROFILES | GPU_COMPAT_GENERIC_ATTRIBS | GPU_COMPAT_CONSTANT_BANKS |        \
     GPU_COMPAT_PINNED_UNIFORMS | GPU_COMPAT_INTEL_SHADOW)

/* The running game's features (a mask of GpuCompatFeature). */
void gpuCompat_setFeatures(unsigned features);

/* Any of `features` is wanted by the game and the GPU is not NVIDIA. */
bool gpuCompat_on(unsigned features);
/* ... and the GPU is AMD or ATI. */
bool gpuCompat_onAmd(unsigned features);
/* ... and the GPU is Intel. */
bool gpuCompat_onIntel(unsigned features);
/* ... and the GPU is AMD, ATI or Intel. */
bool gpuCompat_onKnownVendor(unsigned features);
