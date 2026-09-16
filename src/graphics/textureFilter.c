#include "textureFilter.h"
#include "../config/config.h"
#include "../log/log.h"

#include <stdlib.h>

/* ===========================================================================
 * Configuration
 * =========================================================================== */

static int g_configAniso = 0;      /* raw config value cached by tfInit */
static int g_effectiveAniso = 0;   /* final clamped level (0 if disabled/ext missing) */
static int g_ready = 0;            /* deferred-init flag (0 = hw_max not yet queried) */
static GLfloat g_hwMax = 0.0f;     /* hardware max anisotropy */

/* ===========================================================================
 * Open-addressing hash table (linear probing, tombstone deletion)
 * =========================================================================== */

#define TF_EMPTY 0u
#define TF_TOMB  ((GLuint)0xFFFFFFFFu)   /* tombstone; GL object IDs never reach this */

#define TF_TEX_CAP   1024
#define TF_TEX_MASK  (TF_TEX_CAP - 1)

typedef struct
{
    GLuint key;     /* TF_EMPTY / TF_TOMB / texture id (presence == "AF injected") */
} TFTexEntry;

static TFTexEntry g_texTable[TF_TEX_CAP];

static unsigned tfHashKey(GLuint k)
{
    /* Knuth-style multiplicative hash */
    return (unsigned)(k * 2654435761u);
}

static int tfTexFind(GLuint key)
{
    if (key == 0 || key == TF_TOMB)
        return -1;
    unsigned start = tfHashKey(key) & TF_TEX_MASK;
    for (unsigned i = 0; i < TF_TEX_CAP; i++)
    {
        unsigned idx = (start + i) & TF_TEX_MASK;
        GLuint k = g_texTable[idx].key;
        if (k == TF_EMPTY)
            return -1;
        if (k == key)
            return (int)idx;
        /* skip tombstones */
    }
    return -1;
}

static int tfTexUpsert(GLuint key)
{
    if (key == 0 || key == TF_TOMB)
        return -1;
    unsigned start = tfHashKey(key) & TF_TEX_MASK;
    int firstTomb = -1;
    for (unsigned i = 0; i < TF_TEX_CAP; i++)
    {
        unsigned idx = (start + i) & TF_TEX_MASK;
        GLuint k = g_texTable[idx].key;
        if (k == TF_EMPTY)
        {
            unsigned use = (firstTomb >= 0) ? (unsigned)firstTomb : idx;
            return (int)use;
        }
        if (k == key)
            return (int)idx;
        if (k == TF_TOMB && firstTomb < 0)
            firstTomb = (int)idx;
    }
    return (firstTomb >= 0) ? firstTomb : -1;
}

static void tfTexDelete(GLuint key)
{
    int slot = tfTexFind(key);
    if (slot < 0)
        return;
    g_texTable[slot].key = TF_TOMB;
}

/* ===========================================================================
 * Deferred initialisation
 * =========================================================================== */

static void tfEnsureReady(void)
{
    if (g_ready)
        return;
    g_ready = 1;

    if (!GLAD_GL_EXT_texture_filter_anisotropic)
    {
        g_effectiveAniso = 0;
        return;
    }

    if (!glad_glGetFloatv)
        return; /* GLAD not loaded yet — shouldn't happen but guard anyway */

    glad_glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &g_hwMax);

    /* Clamp g_configAniso to [0, (int)g_hwMax] */
    int clamped = g_configAniso;
    if (clamped < 0)
        clamped = 0;
    if (clamped > (int)g_hwMax)
        clamped = (int)g_hwMax;
    g_effectiveAniso = clamped;
}

/* ===========================================================================
 * GL state helpers
 * =========================================================================== */

static GLuint tfCurrentBoundTexture(GLenum target)
{
    if (!glad_glGetIntegerv)
        return 0;

    GLenum query = 0;
    if (target == GL_TEXTURE_2D)
        query = GL_TEXTURE_BINDING_2D;           /* 0x8069 */
    else if (target == GL_TEXTURE_RECTANGLE)
        query = GL_TEXTURE_BINDING_RECTANGLE;    /* 0x84F6 */
    else
        return 0;

    GLint id = 0;
    glad_glGetIntegerv(query, &id);
    return (GLuint)id;
}

static void tfMarkInjected(GLuint tex)
{
    int slot = tfTexUpsert(tex);
    if (slot >= 0)
        g_texTable[slot].key = tex;
}

/* ===========================================================================
 * Public API
 * =========================================================================== */

void tfInit(void)
{
    g_configAniso = Config.display.anisotropic;
    g_ready = 0;
    g_effectiveAniso = 0;
}

int tfAnisotropy(void)
{
    tfEnsureReady();
    return g_effectiveAniso;
}

/* ---- glTexParameter PRE callbacks (override MAX_ANISOTROPY) ---- */

int tfOverrideMaxAniso_i(GLenum target, GLint *param)
{
    if (tfAnisotropy() <= 0)
        return 0;

    *param = (GLint)g_effectiveAniso;

    GLuint tex = tfCurrentBoundTexture(target);
    if (tex)
        tfMarkInjected(tex);

    return 1;
}

int tfOverrideMaxAniso_f(GLenum target, GLfloat *param)
{
    if (tfAnisotropy() <= 0)
        return 0;

    *param = (GLfloat)g_effectiveAniso;

    GLuint tex = tfCurrentBoundTexture(target);
    if (tex)
        tfMarkInjected(tex);

    return 1;
}

/* ---- glTexParameter POST callback (auto-inject AF) ---- */

void tfAutoInjectAfterFilter(GLenum target)
{
    if (tfAnisotropy() <= 0)
        return;

    GLuint tex = tfCurrentBoundTexture(target);
    if (tex == 0)
        return;

    int slot = tfTexFind(tex);
    if (slot >= 0)
        return; /* already injected */

    if (!glad_glTexParameterf)
        return;

    glad_glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                         (GLfloat)g_effectiveAniso);
    tfMarkInjected(tex);

}

/* ---- glDeleteTextures callback ---- */

void tfOnDeleteTextures(GLsizei n, const GLuint *textures)
{
    if (!textures)
        return;
    for (GLsizei i = 0; i < n; i++)
        tfTexDelete(textures[i]);
}
