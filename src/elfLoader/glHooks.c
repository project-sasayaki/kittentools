#include <glad/gl.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "glHooks.h"
#include "collections.h"
#include "../config/config.h"
#include "../graphics/gpuVendor.h"
#include "../graphics/internalRes.h"
#include "../log/log.h"
#include "../graphics/msaa.h"
#include "../graphics/textureFilter.h"

#include "glHooks_list.h"

#define X(name, ret, params, args) \
    ret __attribute__((cdecl)) wrap_##name params \
    { \
        if (glad_##name) \
            return glad_##name args; \
        return (ret)0; \
    }
GL_HOOKS
#undef X

#define ARB_PARAMETER_CACHE_SIZE 256u
#define ARB_PARAMETER_CACHE_CONTEXTS 8u

typedef struct
{
    HGLRC context;
    GLfloat values[2][ARB_PARAMETER_CACHE_SIZE][4];
    BYTE valid[2][ARB_PARAMETER_CACHE_SIZE];
} ArbParameterCache;

static SRWLOCK s_arbParameterCacheLock = SRWLOCK_INIT;
static ArbParameterCache s_arbParameterCaches[ARB_PARAMETER_CACHE_CONTEXTS];
static LONG s_loggedEnvBulkFallback;

static int arbParameterTargetIndex(GLenum target)
{
    if (target == GL_VERTEX_PROGRAM_ARB)
        return 0;
    if (target == GL_FRAGMENT_PROGRAM_ARB)
        return 1;
    return -1;
}

static ArbParameterCache *getArbParameterCache(BOOL create)
{
    HGLRC context = wglGetCurrentContext();
    ArbParameterCache *empty = NULL;
    unsigned int i;

    if (context == NULL)
        return NULL;

    AcquireSRWLockExclusive(&s_arbParameterCacheLock);
    for (i = 0; i < ARB_PARAMETER_CACHE_CONTEXTS; ++i)
    {
        if (s_arbParameterCaches[i].context == context)
        {
            ReleaseSRWLockExclusive(&s_arbParameterCacheLock);
            return &s_arbParameterCaches[i];
        }
        if (empty == NULL && s_arbParameterCaches[i].context == NULL)
            empty = &s_arbParameterCaches[i];
    }
    if (create && empty != NULL)
        empty->context = context;
    ReleaseSRWLockExclusive(&s_arbParameterCacheLock);
    return create ? empty : NULL;
}

static void cacheArbEnvParameters(GLenum target, GLuint index, GLsizei count,
                                  const GLfloat *params)
{
    ArbParameterCache *cache = getArbParameterCache(TRUE);
    int targetIndex = arbParameterTargetIndex(target);
    GLsizei parameterOffset;

    if (cache == NULL || targetIndex < 0 || params == NULL || count <= 0)
        return;
    AcquireSRWLockExclusive(&s_arbParameterCacheLock);
    for (parameterOffset = 0; parameterOffset < count; ++parameterOffset)
    {
        GLuint parameterIndex = index + (GLuint)parameterOffset;
        if (parameterIndex >= ARB_PARAMETER_CACHE_SIZE)
            break;
        memcpy(cache->values[targetIndex][parameterIndex],
               params + (size_t)parameterOffset * 4u,
               sizeof(cache->values[targetIndex][parameterIndex]));
        cache->valid[targetIndex][parameterIndex] = 1;
    }
    ReleaseSRWLockExclusive(&s_arbParameterCacheLock);
}

void __attribute__((cdecl)) wrap_glProgramStringARB(GLenum target, GLenum format, GLsizei len, const GLvoid *string)
{
    if (!glad_glProgramStringARB)
        return;

    if (string != NULL && len >= 10)
    {
        const char *s = (const char *)string;
        if (target == GL_VERTEX_PROGRAM_ARB && memcmp(s, "!!ARBfp1.0", 10) == 0)
            target = GL_FRAGMENT_PROGRAM_ARB;
        else if (target == GL_FRAGMENT_PROGRAM_ARB && memcmp(s, "!!ARBvp1.0", 10) == 0)
            target = GL_VERTEX_PROGRAM_ARB;
    }

    glad_glProgramStringARB(target, format, len, string);
}

void __attribute__((cdecl)) wrap_glProgramEnvParameter4fvARB(GLenum target, GLuint index,
                                                              const GLfloat *params)
{
    uint8_t gpuVendor = getGPUVendorID();
    BOOL mirrorToLocal = gpuVendor == AMD_GPU || gpuVendor == ATI_GPU;

    /* ID5 normally rewrites Cg's program.local declarations to program.env and
     * consequently uploads constants through the env API.  The AMD compiler
     * patch deliberately preserves local declarations, so mirror those writes
     * into the local bank while retaining env state for hand-authored shaders. */
    if (glad_glProgramEnvParameter4fvARB)
        glad_glProgramEnvParameter4fvARB(target, index, params);

    if (mirrorToLocal)
    {
        cacheArbEnvParameters(target, index, 1, params);
        if (glad_glProgramLocalParameter4fvARB)
            glad_glProgramLocalParameter4fvARB(target, index, params);
    }
}

void __attribute__((cdecl)) wrap_glProgramEnvParameters4fvEXT(GLenum target, GLuint index,
                                                               GLsizei count,
                                                               const GLfloat *params)
{
    uint8_t gpuVendor = getGPUVendorID();
    BOOL mirrorToLocal = gpuVendor == AMD_GPU || gpuVendor == ATI_GPU;
    GLsizei i;

    if (glad_glProgramEnvParameters4fvEXT)
        glad_glProgramEnvParameters4fvEXT(target, index, count, params);
    else if (glad_glProgramEnvParameter4fvARB && params != NULL && count > 0)
    {
        /* GL_EXT_gpu_program_parameters is optional and is not exposed by
         * every Intel driver.  ID5 supplies its bulk constants through the NV
         * entry point, so preserve that operation with the ARB singular API
         * instead of silently dropping every matrix and per-frame value. */
        if (InterlockedCompareExchange(&s_loggedEnvBulkFallback, 1, 0) == 0)
            log_debug(plfHooks,
                      "[gl] glProgramEnvParameters4fvEXT unavailable; using ARB singular env uploads");
        for (i = 0; i < count; ++i)
            glad_glProgramEnvParameter4fvARB(
                target, index + (GLuint)i, params + (size_t)i * 4u);
    }
    if (mirrorToLocal)
    {
        cacheArbEnvParameters(target, index, count, params);
        if (glad_glProgramLocalParameters4fvEXT)
            glad_glProgramLocalParameters4fvEXT(target, index, count, params);
        else if (glad_glProgramLocalParameter4fvARB && params != NULL)
            for (i = 0; i < count; ++i)
                glad_glProgramLocalParameter4fvARB(
                    target, index + (GLuint)i, params + (size_t)i * 4u);
    }
}

void __attribute__((cdecl)) wrap_glProgramLocalParameter4fvARB(GLenum target, GLuint index,
                                                                const GLfloat *params)
{
    uint8_t gpuVendor = getGPUVendorID();

    /* shaderfix-idas keeps Intel's ARB constants in program.env.  IDAS also
     * sends one per-object value through the singular local entry point, so
     * redirect that call into the same bank. */
    if (gpuVendor == INTEL_GPU)
    {
        if (glad_glProgramEnvParameter4fvARB)
            glad_glProgramEnvParameter4fvARB(target, index, params);
        return;
    }

    if (glad_glProgramLocalParameter4fvARB)
        glad_glProgramLocalParameter4fvARB(target, index, params);
}

void __attribute__((cdecl)) wrap_glBindProgramARB(GLenum target, GLuint program)
{
    uint8_t gpuVendor;
    int targetIndex;
    ArbParameterCache *cache;
    unsigned int i;

    if (!glad_glBindProgramARB)
        return;
    glad_glBindProgramARB(target, program);

    gpuVendor = getGPUVendorID();
    targetIndex = arbParameterTargetIndex(target);
    if ((gpuVendor != AMD_GPU && gpuVendor != ATI_GPU) || targetIndex < 0 ||
        !glad_glProgramLocalParameter4fvARB)
        return;

    cache = getArbParameterCache(FALSE);
    if (cache == NULL)
        return;
    AcquireSRWLockShared(&s_arbParameterCacheLock);
    for (i = 0; i < ARB_PARAMETER_CACHE_SIZE; ++i)
    {
        if (!cache->valid[targetIndex][i])
            continue;
        glad_glProgramLocalParameter4fvARB(target, i,
                                           cache->values[targetIndex][i]);
    }
    ReleaseSRWLockShared(&s_arbParameterCacheLock);
}

static BOOL useGenericImmediateAttributes(void)
{
    uint8_t gpuVendor = getGPUVendorID();
    return gpuVendor == AMD_GPU || gpuVendor == ATI_GPU ||
           gpuVendor == INTEL_GPU;
}

static __thread GLenum s_clientActiveTexture = GL_TEXTURE0_ARB;

static GLuint genericTexCoordAttribute(void)
{
    return (GLuint)s_clientActiveTexture - 33976u;
}

void __attribute__((cdecl)) wrap_glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (useGenericImmediateAttributes())
    {
        if (glad_glVertexAttrib3fARB)
            glad_glVertexAttrib3fARB(0u, x, y, z);
        return;
    }
    if (glad_glVertex3f)
        glad_glVertex3f(x, y, z);
}

void __attribute__((cdecl)) wrap_glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
    if (useGenericImmediateAttributes())
    {
        if (glad_glVertexAttrib3fARB)
            glad_glVertexAttrib3fARB(2u, nx, ny, nz);
        return;
    }
    if (glad_glNormal3f)
        glad_glNormal3f(nx, ny, nz);
}

void __attribute__((cdecl)) wrap_glNormal3fv(const GLfloat *normal)
{
    if (useGenericImmediateAttributes())
    {
        /* Match shaderfix-idas' smokeL2 path exactly.  That shader stores UV
         * in Normal.xy, and glVertexAttrib3fARB is the entry point already
         * required by the other immediate-mode remaps. */
        if (glad_glVertexAttrib3fARB && normal != NULL)
            glad_glVertexAttrib3fARB(2u, normal[0], normal[1], normal[2]);
        return;
    }
    if (glad_glNormal3fv)
        glad_glNormal3fv(normal);
}

void __attribute__((cdecl)) wrap_glColor4ub(GLubyte red, GLubyte green,
                                             GLubyte blue, GLubyte alpha)
{
    if (useGenericImmediateAttributes())
    {
        if (glad_glVertexAttrib4NubARB)
            glad_glVertexAttrib4NubARB(3u, red, green, blue, alpha);
        return;
    }
    if (glad_glColor4ub)
        glad_glColor4ub(red, green, blue, alpha);
}

void __attribute__((cdecl)) wrap_glTexCoord2f(GLfloat s, GLfloat t)
{
    if (useGenericImmediateAttributes())
    {
        if (glad_glVertexAttrib2fARB)
            glad_glVertexAttrib2fARB(8u, s, t);
        return;
    }
    if (glad_glTexCoord2f)
        glad_glTexCoord2f(s, t);
}

void __attribute__((cdecl)) wrap_glMultiTexCoord2fARB(GLenum target, GLfloat s,
                                                       GLfloat t)
{
    if (useGenericImmediateAttributes())
    {
        if (glad_glVertexAttrib2fARB)
            glad_glVertexAttrib2fARB((GLuint)target - 33976u, s, t);
        return;
    }
    if (glad_glMultiTexCoord2fARB)
        glad_glMultiTexCoord2fARB(target, s, t);
}

void __attribute__((cdecl)) wrap_glClientActiveTexture(GLenum texture)
{
    if (useGenericImmediateAttributes())
    {
        s_clientActiveTexture = texture;
        return;
    }
    if (glad_glClientActiveTexture)
        glad_glClientActiveTexture(texture);
}

static int clientArrayAttribute(GLenum array, GLuint *attribute)
{
    switch (array)
    {
        case GL_VERTEX_ARRAY:
            *attribute = 0u;
            return 1;
        case GL_NORMAL_ARRAY:
            *attribute = 2u;
            return 1;
        case GL_COLOR_ARRAY:
            *attribute = 3u;
            return 1;
        case GL_TEXTURE_COORD_ARRAY:
            *attribute = genericTexCoordAttribute();
            return 1;
        default:
            return 0;
    }
}

void __attribute__((cdecl)) wrap_glEnableClientState(GLenum array)
{
    GLuint attribute;
    if (useGenericImmediateAttributes() && clientArrayAttribute(array, &attribute))
    {
        if (glad_glEnableVertexAttribArrayARB)
            glad_glEnableVertexAttribArrayARB(attribute);
        return;
    }
    if (glad_glEnableClientState)
        glad_glEnableClientState(array);
}

void __attribute__((cdecl)) wrap_glDisableClientState(GLenum array)
{
    GLuint attribute;
    if (useGenericImmediateAttributes() && clientArrayAttribute(array, &attribute))
    {
        if (glad_glDisableVertexAttribArrayARB)
            glad_glDisableVertexAttribArrayARB(attribute);
        return;
    }
    if (glad_glDisableClientState)
        glad_glDisableClientState(array);
}

void __attribute__((cdecl)) wrap_glVertexPointer(GLint size, GLenum type,
                                                  GLsizei stride,
                                                  const GLvoid *pointer)
{
    if (useGenericImmediateAttributes())
    {
        if (glad_glVertexAttribPointerARB)
            glad_glVertexAttribPointerARB(0u, size, type, GL_FALSE, stride, pointer);
        return;
    }
    if (glad_glVertexPointer)
        glad_glVertexPointer(size, type, stride, pointer);
}

void __attribute__((cdecl)) wrap_glNormalPointer(GLenum type, GLsizei stride,
                                                  const GLvoid *pointer)
{
    if (useGenericImmediateAttributes())
    {
        if (glad_glVertexAttribPointerARB)
            glad_glVertexAttribPointerARB(2u, 3, type, GL_TRUE, stride, pointer);
        return;
    }
    if (glad_glNormalPointer)
        glad_glNormalPointer(type, stride, pointer);
}

void __attribute__((cdecl)) wrap_glColorPointer(GLint size, GLenum type,
                                                 GLsizei stride,
                                                 const GLvoid *pointer)
{
    if (useGenericImmediateAttributes())
    {
        if (glad_glVertexAttribPointerARB)
            glad_glVertexAttribPointerARB(3u, size, type, GL_TRUE, stride, pointer);
        return;
    }
    if (glad_glColorPointer)
        glad_glColorPointer(size, type, stride, pointer);
}

void __attribute__((cdecl)) wrap_glTexCoordPointer(GLint size, GLenum type,
                                                    GLsizei stride,
                                                    const GLvoid *pointer)
{
    if (useGenericImmediateAttributes())
    {
        if (glad_glVertexAttribPointerARB)
            glad_glVertexAttribPointerARB(genericTexCoordAttribute(), size, type,
                                          GL_FALSE, stride, pointer);
        return;
    }
    if (glad_glTexCoordPointer)
        glad_glTexCoordPointer(size, type, stride, pointer);
}

void __attribute__((cdecl)) wrap_glGenFramebuffersEXT(GLsizei n, GLuint *framebuffers)
{
    if (glad_glGenFramebuffersEXT)
        glad_glGenFramebuffersEXT(n, framebuffers);
    irOnGenFramebuffers(n, framebuffers);
}

void __attribute__((cdecl)) wrap_glBindFramebufferEXT(GLenum target, GLuint framebuffer)
{
    msOnBindFramebuffer(target, framebuffer);
    irOnBindFramebuffer(target, framebuffer);

    if (glad_glBindFramebuffer)
        glad_glBindFramebuffer(target, framebuffer);
    else if (glad_glBindFramebufferEXT)
        glad_glBindFramebufferEXT(target, framebuffer);
}

void __attribute__((cdecl)) wrap_glFramebufferTexture2DEXT(GLenum target, GLenum attachment, GLenum textarget, GLuint texture,
                                                                      GLint level)
{
    irOnFramebufferTexture2D(target, attachment, textarget, texture, level);

    if (msOnFramebufferTexture2D(target, attachment, textarget, texture, level))
        return;

    if (glad_glFramebufferTexture2DEXT)
        glad_glFramebufferTexture2DEXT(target, attachment, textarget, texture, level);
}

void __attribute__((cdecl)) wrap_glDeleteFramebuffersEXT(GLsizei n, const GLuint *framebuffers)
{
    if (framebuffers)
    {
        for (GLsizei i = 0; i < n; i++)
            msInvalidateFBO(framebuffers[i]);
        irOnDeleteFramebuffers(n, framebuffers);
    }
    if (glad_glDeleteFramebuffersEXT)
        glad_glDeleteFramebuffersEXT(n, framebuffers);
}

GLenum __attribute__((cdecl)) wrap_glCheckFramebufferStatusEXT(GLenum target)
{
    if (glad_glCheckFramebufferStatusEXT)
        return glad_glCheckFramebufferStatusEXT(target);
    return 0;
}

void __attribute__((cdecl)) wrap_glGenRenderbuffersEXT(GLsizei n, GLuint *renderbuffers)
{
    if (glad_glGenRenderbuffersEXT)
        glad_glGenRenderbuffersEXT(n, renderbuffers);
}

void __attribute__((cdecl)) wrap_glBindRenderbufferEXT(GLenum target, GLuint renderbuffer)
{
    if (glad_glBindRenderbufferEXT)
        glad_glBindRenderbufferEXT(target, renderbuffer);
}

void __attribute__((cdecl)) wrap_glDeleteRenderbuffersEXT(GLsizei n, const GLuint *renderbuffers)
{
    if (renderbuffers)
        irOnDeleteRenderbuffers(n, renderbuffers);
    if (glad_glDeleteRenderbuffersEXT)
        glad_glDeleteRenderbuffersEXT(n, renderbuffers);
}

void __attribute__((cdecl)) wrap_glRenderbufferStorageEXT(GLenum target, GLenum internalformat, GLsizei width, GLsizei height)
{
    GLsizei sw = width, sh = height;
    irOnRenderbufferStorage(target, internalformat, width, height, &sw, &sh);
    if (glad_glRenderbufferStorageEXT)
        glad_glRenderbufferStorageEXT(target, internalformat, sw, sh);
}

void __attribute__((cdecl)) wrap_glFramebufferRenderbufferEXT(GLenum target, GLenum attachment, GLenum renderbuffertarget,
                                                                         GLuint renderbuffer)
{
    irOnFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
    if (glad_glFramebufferRenderbufferEXT)
        glad_glFramebufferRenderbufferEXT(target, attachment, renderbuffertarget, renderbuffer);
}

void hook_glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (irIsScaledDrawFBO())
    {
        x = irScale(x); y = irScale(y);
        width = irScale(width); height = irScale(height);
    }

    return glad_glViewport(x, y, width, height);
}

void hook_glTexImage2D(unsigned int target, int level, int internalformat, int width, int height, int border, unsigned int format,
                        unsigned int type, const void *pixels)
{
    GLsizei w = width, h = height;
    irOnTexImage2D(target, level, internalformat, &w, &h, format, type, pixels);
    return glad_glTexImage2D(target, level, internalformat, w, h, border, format, type, pixels);
}

void hook_glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    if (!glad_glTexParameteri)
        return;

    if (pname == GL_TEXTURE_MAX_ANISOTROPY_EXT)
    {
        tfOverrideMaxAniso_i(target, &param);
        glad_glTexParameteri(target, pname, param);
    }
    else
    {
        glad_glTexParameteri(target, pname, param);
        if (pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_MAG_FILTER)
            tfAutoInjectAfterFilter(target);
    }
}

void hook_glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{ 
    if (!glad_glTexParameterf)
        return;

    if (pname == GL_TEXTURE_MAX_ANISOTROPY_EXT)
    {
        tfOverrideMaxAniso_f(target, &param);
        glad_glTexParameterf(target, pname, param);
    }
    else
    {
        glad_glTexParameterf(target, pname, param);
        if (pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_MAG_FILTER)
            tfAutoInjectAfterFilter(target);
    }
}

void __attribute__((cdecl)) wrap_glDeleteTextures(GLsizei n, const GLuint *textures)
{
    if (textures)
        for (GLsizei i = 0; i < n; i++)
            msInvalidateTex(textures[i]);
    irOnDeleteTextures(n, textures);
    tfOnDeleteTextures(n, textures);
    if (glad_glDeleteTextures)
        glad_glDeleteTextures(n, textures);
}

void hook_glBindTexture(GLenum target, GLuint texture)
{
    irOnBindTexture(target, texture);
    msResolveTex(texture);
    if(!glad_glBindTexture)
        return;

    glad_glBindTexture(target, texture);
}

void __attribute__((cdecl)) hook_glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (irIsScaledDrawFBO())
    {
        x = irScale(x); y = irScale(y);
        width = irScale(width); height = irScale(height);
    }
    if (glad_glScissor)
        glad_glScissor(x, y, width, height);
}


/* ---------------------------------------------------------------------------
 * The interceptor router mapping string names to our safe __cdecl wrappers.
 * ------------------------------------------------------------------------- */

typedef struct
{
    const char *name;
    void *proc;
} GLHookEntry;

static const GLHookEntry s_hookTable[] = {
    /* Hand-written hooks. */
    { "glViewport", (void *)&hook_glViewport },
    { "glTexImage2D", (void *)&hook_glTexImage2D },
    { "glTexParameteri", (void *)&hook_glTexParameteri },
    { "glTexParameterf", (void *)&hook_glTexParameterf },
    { "glDeleteTextures", (void *)&wrap_glDeleteTextures },
    { "glBindTexture", (void *)&hook_glBindTexture },
    { "glProgramStringARB", (void *)&wrap_glProgramStringARB },
    { "glProgramEnvParameter4fvARB", (void *)&wrap_glProgramEnvParameter4fvARB },
    { "glProgramEnvParameters4fvEXT", (void *)&wrap_glProgramEnvParameters4fvEXT },
    { "glProgramLocalParameter4fvARB", (void *)&wrap_glProgramLocalParameter4fvARB },
    { "glBindProgramARB", (void *)&wrap_glBindProgramARB },
    { "glVertex3f", (void *)&wrap_glVertex3f },
    { "glNormal3f", (void *)&wrap_glNormal3f },
    { "glNormal3fv", (void *)&wrap_glNormal3fv },
    { "glColor4ub", (void *)&wrap_glColor4ub },
    { "glTexCoord2f", (void *)&wrap_glTexCoord2f },
    { "glMultiTexCoord2fARB", (void *)&wrap_glMultiTexCoord2fARB },
    { "glClientActiveTexture", (void *)&wrap_glClientActiveTexture },
    { "glEnableClientState", (void *)&wrap_glEnableClientState },
    { "glDisableClientState", (void *)&wrap_glDisableClientState },
    { "glVertexPointer", (void *)&wrap_glVertexPointer },
    { "glNormalPointer", (void *)&wrap_glNormalPointer },
    { "glColorPointer", (void *)&wrap_glColorPointer },
    { "glTexCoordPointer", (void *)&wrap_glTexCoordPointer },
    { "glGenFramebuffersEXT", (void *)&wrap_glGenFramebuffersEXT },
    { "glBindFramebufferEXT", (void *)&wrap_glBindFramebufferEXT },
    { "glFramebufferTexture2DEXT", (void *)&wrap_glFramebufferTexture2DEXT },
    { "glDeleteFramebuffersEXT", (void *)&wrap_glDeleteFramebuffersEXT },
    { "glCheckFramebufferStatusEXT", (void *)&wrap_glCheckFramebufferStatusEXT },
    { "glGenRenderbuffersEXT", (void *)&wrap_glGenRenderbuffersEXT },
    { "glBindRenderbufferEXT", (void *)&wrap_glBindRenderbufferEXT },
    { "glDeleteRenderbuffersEXT", (void *)&wrap_glDeleteRenderbuffersEXT },
    { "glRenderbufferStorageEXT", (void *)&wrap_glRenderbufferStorageEXT },
    { "glFramebufferRenderbufferEXT", (void *)&wrap_glFramebufferRenderbufferEXT },
    { "glScissor", (void *)&hook_glScissor },

    /* Generated plain forwarders. */
#define X(name, ret, params, args) { #name, (void *)&wrap_##name },
    GL_HOOKS
#undef X
};

static StrMap g_glHooksCache;
static INIT_ONCE g_glHooksInitOnce = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK glHooks_BuildCache(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context)
{
    (void)InitOnce; (void)Parameter; (void)Context;
    strMap_Init(&g_glHooksCache);
    for (size_t i = 0; i < sizeof(s_hookTable) / sizeof(s_hookTable[0]); ++i)
        strMap_Set(&g_glHooksCache, s_hookTable[i].name, s_hookTable[i].proc);
    return TRUE;
}

void *GLHooks_GetProcAddress(const char *procName)
{
    InitOnceExecuteOnce(&g_glHooksInitOnce, glHooks_BuildCache, NULL, NULL);

    void *proc = strMap_Get(&g_glHooksCache, procName);
    if (proc)
        return proc;

    return (void *)wglGetProcAddress(procName);
}
