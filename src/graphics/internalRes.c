#include "internalRes.h"
#include "../config/config.h"
#include "../log/log.h"

#include <stdlib.h>
#include <string.h>

/* ===========================================================================
 * Configuration
 * =========================================================================== */

static int g_renderScale = 1;   /* effective scale, clamped to {1,2,4,8} */

/* ===========================================================================
 * Open-addressing hash tables (linear probing, tombstone deletion)
 * =========================================================================== */

#define IR_EMPTY 0u
#define IR_TOMB  ((GLuint)0xFFFFFFFFu)   /* tombstone; GL object IDs never reach this */

/* ---- FBO table ---- */
#define IR_FBO_CAP   256
#define IR_FBO_MASK  (IR_FBO_CAP - 1)

typedef struct
{
    GLuint key;     
    int    scaled;  
    int    unscaleable; 
    int    hasNativeColor; 
    GLuint rbAttachments[4];
    GLuint colorTex;        
    GLsizei colorW, colorH; 
} IRFboEntry;

static IRFboEntry g_fboTable[IR_FBO_CAP];

/* ---- Texture allocation cache ---- */
#define IR_TEX_CAP   512
#define IR_TEX_MASK  (IR_TEX_CAP - 1)

typedef struct
{
    GLuint  key;  
    GLsizei w, h; 
    GLuint  scaled;
    GLenum  target; 
    GLenum  internalformat; 
    GLenum  format;
    GLenum  type;
} IRTexEntry;

static IRTexEntry g_texTable[IR_TEX_CAP];

/* ---- Renderbuffer allocation cache ---- */
#define IR_RB_CAP   128
#define IR_RB_MASK  (IR_RB_CAP - 1)

typedef struct
{
    GLuint  key;             
    GLenum  internalformat;
    GLsizei width;           
    GLsizei height;
} IRRbEntry;

static IRRbEntry g_rbTable[IR_RB_CAP];

static unsigned irHashKey(GLuint k)
{
    /* Knuth-style multiplicative hash */
    return (unsigned)(k * 2654435761u);
}

/* ---- FBO table ops ---- */

static int irFboFind(GLuint key)
{
    if (key == 0 || key == IR_TOMB)
        return -1;
    unsigned start = irHashKey(key) & IR_FBO_MASK;
    for (unsigned i = 0; i < IR_FBO_CAP; i++)
    {
        unsigned idx = (start + i) & IR_FBO_MASK;
        GLuint k = g_fboTable[idx].key;
        if (k == IR_EMPTY)
            return -1;
        if (k == key)
            return (int)idx;
        /* skip tombstones */
    }
    return -1;
}

static int irFboUpsert(GLuint key)
{
    if (key == 0 || key == IR_TOMB)
        return -1;
    unsigned start = irHashKey(key) & IR_FBO_MASK;
    int firstTomb = -1;
    for (unsigned i = 0; i < IR_FBO_CAP; i++)
    {
        unsigned idx = (start + i) & IR_FBO_MASK;
        GLuint k = g_fboTable[idx].key;
        if (k == IR_EMPTY)
        {
            unsigned use = (firstTomb >= 0) ? (unsigned)firstTomb : idx;
            g_fboTable[use].key = key;
            g_fboTable[use].scaled = 0;
            g_fboTable[use].unscaleable = 0;
            g_fboTable[use].hasNativeColor = 0;
            g_fboTable[use].rbAttachments[0] = 0;
            g_fboTable[use].rbAttachments[1] = 0;
            g_fboTable[use].rbAttachments[2] = 0;
            g_fboTable[use].rbAttachments[3] = 0;
            g_fboTable[use].colorTex = 0;
            g_fboTable[use].colorW = 0;
            g_fboTable[use].colorH = 0;
            return (int)use;
        }
        if (k == key)
            return (int)idx;
        if (k == IR_TOMB && firstTomb < 0)
            firstTomb = (int)idx;
    }
    if (firstTomb >= 0)
    {
        g_fboTable[firstTomb].key = key;
        g_fboTable[firstTomb].scaled = 0;
        g_fboTable[firstTomb].unscaleable = 0;
        g_fboTable[firstTomb].hasNativeColor = 0;
        g_fboTable[firstTomb].rbAttachments[0] = 0;
        g_fboTable[firstTomb].rbAttachments[1] = 0;
        g_fboTable[firstTomb].rbAttachments[2] = 0;
        g_fboTable[firstTomb].rbAttachments[3] = 0;
        g_fboTable[firstTomb].colorTex = 0;
        g_fboTable[firstTomb].colorW = 0;
        g_fboTable[firstTomb].colorH = 0;
        return firstTomb;
    }
    return -1; /* table full */
}

static void irFboDelete(GLuint key)
{
    int slot = irFboFind(key);
    if (slot < 0)
        return;
    g_fboTable[slot].key = IR_TOMB;
    g_fboTable[slot].scaled = 0;
    g_fboTable[slot].unscaleable = 0;
    g_fboTable[slot].hasNativeColor = 0;
    g_fboTable[slot].rbAttachments[0] = 0;
    g_fboTable[slot].rbAttachments[1] = 0;
    g_fboTable[slot].rbAttachments[2] = 0;
    g_fboTable[slot].rbAttachments[3] = 0;
    g_fboTable[slot].colorTex = 0;
    g_fboTable[slot].colorW = 0;
    g_fboTable[slot].colorH = 0;
}

static void irFboMarkScaled(GLuint key)
{
    int slot = irFboUpsert(key);
    if (slot >= 0)
        g_fboTable[slot].scaled = 1;
}

static void irFboClearUnscaleable(GLuint key)
{
    int slot = irFboFind(key);
    if (slot >= 0)
        g_fboTable[slot].unscaleable = 0;
}

static int irRbFind(GLuint key);  
static int irTexFind(GLuint key); 


static void irFboMakeNative(GLuint fbo)
{
    int fslot = irFboFind(fbo);
    if (fslot < 0 || g_renderScale <= 1)
        return;

    GLsizei colorW = g_fboTable[fslot].colorW;
    GLsizei colorH = g_fboTable[fslot].colorH;
    if (colorW <= 0 || colorH <= 0)
        return; 

    for (int i = 1; i < 4; i++)   
    {
        GLuint rb = g_fboTable[fslot].rbAttachments[i];
        if (rb == 0)
            continue;
        int rslot = irRbFind(rb);
        if (rslot < 0)
            continue;

        if (g_rbTable[rslot].width  == colorW &&
            g_rbTable[rslot].height == colorH)
            continue;

        GLint prevRb = 0;
        glad_glGetIntegerv(0x8CA7, &prevRb);
        glad_glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
        if (glad_glRenderbufferStorageEXT)
            glad_glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,
                                          g_rbTable[rslot].internalformat,
                                          colorW, colorH);
        glad_glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, (GLuint)prevRb);

        log_debug(plfBridges,
                 "[ir] reconciled FBO %u: rb %u %dx%d -> %dx%d (match color)",
                 fbo, rb,
                 g_rbTable[rslot].width, g_rbTable[rslot].height,
                 colorW, colorH);

        g_rbTable[rslot].width  = colorW;
        g_rbTable[rslot].height = colorH;
    }
    g_fboTable[fslot].scaled = 0;
    g_fboTable[fslot].unscaleable = 1;  /* keep viewport native -> complete */
}

static int irFormatTypeForInternal(GLenum internalformat,
                                   GLenum *outFormat, GLenum *outType)
{
    GLenum f = 0, t = 0;
    switch (internalformat)
    {
        case 0x8058: /* GL_RGBA8 */   f = GL_RGBA; t = GL_UNSIGNED_BYTE; break;
        case GL_RGBA:                 f = GL_RGBA; t = GL_UNSIGNED_BYTE; break;
        case 0x8057: /* GL_RGB8 */    f = GL_RGB;  t = GL_UNSIGNED_BYTE; break;
        case GL_RGB:                  f = GL_RGB;  t = GL_UNSIGNED_BYTE; break;
        default: break;
    }
    if (f == 0 || t == 0)
        return 0;
    *outFormat = f;
    *outType   = t;
    return 1;
}

static int irFboMakeScaled(GLuint fbo)
{
    int fslot = irFboFind(fbo);
    if (fslot < 0 || g_renderScale <= 1)
        return 0;

    GLsizei colorW = g_fboTable[fslot].colorW;
    GLsizei colorH = g_fboTable[fslot].colorH;
    GLuint  tex    = g_fboTable[fslot].colorTex;
    if (colorW <= 0 || colorH <= 0 || tex == 0)
        return 0;

    int sharedSlot = irTexFind(tex);
    if (sharedSlot >= 0 && g_texTable[sharedSlot].scaled)
    {
        g_fboTable[fslot].scaled = 1;
        g_fboTable[fslot].unscaleable = 0;
        return 1;
    }

    int tslot = irTexFind(tex);
    GLenum target        = GL_TEXTURE_2D;
    GLenum internalformat = 0;
    GLenum format        = 0;
    GLenum type          = 0;
    if (tslot >= 0)
    {
        target        = g_texTable[tslot].target;
        internalformat = g_texTable[tslot].internalformat;
        format        = g_texTable[tslot].format;
        type          = g_texTable[tslot].type;
    }
    else
    {
        GLint prevA = 0, prevB = 0;
        int gotParams = 0;
        if (glad_glGetIntegerv && glad_glGetTexLevelParameteriv &&
            glad_glBindTexture && glad_glActiveTexture)
        {
            glad_glGetIntegerv(GL_ACTIVE_TEXTURE, &prevA);
            glad_glActiveTexture(GL_TEXTURE0);
            glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevB);
            glad_glBindTexture(GL_TEXTURE_2D, tex);
            GLint q = 0, qf = 0, qt = 0;
            glad_glGetTexLevelParameteriv(GL_TEXTURE_2D, 0,
                                          GL_TEXTURE_INTERNAL_FORMAT, &q);
            glad_glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, 0x1909, &qf);
            glad_glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, 0x190A, &qt);
            glad_glBindTexture(GL_TEXTURE_2D, (GLuint)prevB);
            glad_glActiveTexture((GLenum)prevA);
            internalformat = (GLenum)q;
            if (qf != 0 && qt != 0)
            {
                format = (GLenum)qf;
                type   = (GLenum)qt;
                gotParams = 1;
            }
        }
        if (!gotParams)
        {
            if (internalformat == 0 ||
                !irFormatTypeForInternal(internalformat, &format, &type))
                return 0; 
        }
    }
    if (internalformat == 0 || format == 0 || type == 0)
        return 0; 

    GLsizei scW = colorW * g_renderScale;
    GLsizei scH = colorH * g_renderScale;
    if (scW <= 0 || scH <= 0)
        return 0;

    if (glad_glTexImage2D)
    {
        if (target == 0x8513u) 
        {
            for (GLenum face = 0x8515u; face <= 0x851Au; face++)
                glad_glTexImage2D(face, 0, internalformat, scW, scH, 0,
                                  format, type, NULL);
        }
        else
        {
            glad_glTexImage2D(target, 0, internalformat, scW, scH, 0,
                              format, type, NULL);
        }
    }

    g_texTable[tslot].w      = scW;
    g_texTable[tslot].h      = scH;
    g_texTable[tslot].scaled = 1;

    g_fboTable[fslot].colorW = scW;
    g_fboTable[fslot].colorH = scH;
    g_fboTable[fslot].scaled = 1;
    g_fboTable[fslot].unscaleable = 0;

    log_debug(plfBridges,
             "[ir] promote-color FBO %u: tex=%u %dx%d -> %dx%d (scaled)%s",
             fbo, tex, colorW, colorH, scW, scH,
             (tslot >= 0 ? "" : " [gl-recovered]"));
    return 1;
}

/* ---- Texture cache ops ---- */

static int irTexFind(GLuint key)
{
    if (key == 0 || key == IR_TOMB)
        return -1;
    unsigned start = irHashKey(key) & IR_TEX_MASK;
    for (unsigned i = 0; i < IR_TEX_CAP; i++)
    {
        unsigned idx = (start + i) & IR_TEX_MASK;
        GLuint k = g_texTable[idx].key;
        if (k == IR_EMPTY)
            return -1;
        if (k == key)
            return (int)idx;
    }
    return -1;
}

static int irTexUpsert(GLuint key)
{
    if (key == 0 || key == IR_TOMB)
        return -1;
    unsigned start = irHashKey(key) & IR_TEX_MASK;
    int firstTomb = -1;
    for (unsigned i = 0; i < IR_TEX_CAP; i++)
    {
        unsigned idx = (start + i) & IR_TEX_MASK;
        GLuint k = g_texTable[idx].key;
        if (k == IR_EMPTY)
        {
            unsigned use = (firstTomb >= 0) ? (unsigned)firstTomb : idx;
            return (int)use;
        }
        if (k == key)
            return (int)idx;
        if (k == IR_TOMB && firstTomb < 0)
            firstTomb = (int)idx;
    }
    return (firstTomb >= 0) ? firstTomb : -1;
}

static void irTexDelete(GLuint key)
{
    int slot = irTexFind(key);
    if (slot < 0)
        return;
    g_texTable[slot].key = IR_TOMB;
}

/* ---- Renderbuffer cache ops ---- */

static int irRbFind(GLuint key)
{
    if (key == 0 || key == IR_TOMB)
        return -1;
    unsigned start = irHashKey(key) & IR_RB_MASK;
    for (unsigned i = 0; i < IR_RB_CAP; i++)
    {
        unsigned idx = (start + i) & IR_RB_MASK;
        GLuint k = g_rbTable[idx].key;
        if (k == IR_EMPTY)
            return -1;
        if (k == key)
            return (int)idx;
        /* skip tombstones */
    }
    return -1;
}

static int irRbUpsert(GLuint key)
{
    if (key == 0 || key == IR_TOMB)
        return -1;
    unsigned start = irHashKey(key) & IR_RB_MASK;
    int firstTomb = -1;
    for (unsigned i = 0; i < IR_RB_CAP; i++)
    {
        unsigned idx = (start + i) & IR_RB_MASK;
        GLuint k = g_rbTable[idx].key;
        if (k == IR_EMPTY)
        {
            unsigned use = (firstTomb >= 0) ? (unsigned)firstTomb : idx;
            g_rbTable[use].key = key;
            g_rbTable[use].internalformat = 0;
            g_rbTable[use].width = 0;
            g_rbTable[use].height = 0;
            return (int)use;
        }
        if (k == key)
            return (int)idx;
        if (k == IR_TOMB && firstTomb < 0)
            firstTomb = (int)idx;
    }
    if (firstTomb >= 0)
    {
        g_rbTable[firstTomb].key = key;
        g_rbTable[firstTomb].internalformat = 0;
        g_rbTable[firstTomb].width = 0;
        g_rbTable[firstTomb].height = 0;
        return firstTomb;
    }
    return -1; /* table full */
}

static void irRbDelete(GLuint key)
{
    int slot = irRbFind(key);
    if (slot < 0)
        return;
    g_rbTable[slot].key = IR_TOMB;
    g_rbTable[slot].internalformat = 0;
    g_rbTable[slot].width = 0;
    g_rbTable[slot].height = 0;
}

/* ---- FBO attachment index helper ---- */

static int irRbAttachIndex(GLenum attachment)
{
    switch (attachment)
    {
        case GL_COLOR_ATTACHMENT0: return 0; /* GL_COLOR_ATTACHMENT0  */
        case GL_DEPTH_ATTACHMENT: return 1; /* GL_DEPTH_ATTACHMENT   */
        case GL_STENCIL_ATTACHMENT: return 2; /* GL_STENCIL_ATTACHMENT */
        case GL_DEPTH_STENCIL_ATTACHMENT: return 3; /* GL_DEPTH_STENCIL_ATTACHMENT */
        default: return -1;
    }
}

/* ===========================================================================
 * Current GL state tracking
 * =========================================================================== */

static GLuint g_drawFBO = 0;
static GLuint g_readFBO = 0;

#define IR_MAX_UNITS 32
/* GL_TEXTURE_CUBE_MAP (0x8513) bindings are tracked so cube-map render targets
 * can be scaled like 2D ones. */
typedef struct { GLuint tex2D; GLuint texRect; GLuint texCube; } IRUnitBindings;
static IRUnitBindings g_units[IR_MAX_UNITS];
static int g_activeUnitIdx = 0;   /* index 0..31 (GL_TEXTURE0 + idx) */

static GLuint irFboFromTarget(GLenum target)
{
    if (target == GL_READ_FRAMEBUFFER)
        return g_readFBO;
    /* GL_FRAMEBUFFER and GL_DRAW_FRAMEBUFFER both affect the draw target */
    return g_drawFBO;
}


/* ===========================================================================
 * Public API
 * =========================================================================== */

void irInit(void)
{
    int rs = Config.display.render_scale;
    /* Clamp to the nearest supported tier {1,2,4,8}. */
    if (rs <= 1)       rs = 1;
    else if (rs <= 2)  rs = 2;
    else if (rs <= 4)  rs = 4;
    else               rs = 8;
    g_renderScale = rs;
}


int irRenderScale(void) { return g_renderScale; }

int irScale(int v) { return v * g_renderScale; }





/* ---- FBO events ---- */

void irOnGenFramebuffers(GLsizei n, GLuint *framebuffers)
{
    /* FBOs are registered lazily on bind (irOnBindFramebuffer upserts). */
    (void)n;
    (void)framebuffers;
}

void irOnDeleteFramebuffers(GLsizei n, const GLuint *framebuffers)
{
    if (!framebuffers)
        return;
    for (GLsizei i = 0; i < n; i++)
    {
        irFboDelete(framebuffers[i]);
        if (g_drawFBO == framebuffers[i]) g_drawFBO = 0;
        if (g_readFBO == framebuffers[i]) g_readFBO = 0;
    }
}

void irOnDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers)
{
    if (!renderbuffers)
        return;
    for (GLsizei i = 0; i < n; i++)
    {
        GLuint rb = renderbuffers[i];
        irRbDelete(rb);

        /* Detach from any FBO's attachment tracking to avoid dangling refs. */
        for (unsigned k = 0; k < IR_FBO_CAP; k++)
        {
            if (g_fboTable[k].key == IR_EMPTY || g_fboTable[k].key == IR_TOMB)
                continue;
            for (int m = 0; m < 4; m++)
            {
                if (g_fboTable[k].rbAttachments[m] == rb)
                    g_fboTable[k].rbAttachments[m] = 0;
            }
        }
    }
}

void irOnDeleteTextures(GLsizei n, const GLuint *textures)
{
    if (!textures)
        return;
    for (GLsizei i = 0; i < n; i++)
    {
        GLuint tex = textures[i];
        if (tex == 0 || tex == IR_TOMB)
            continue;

        irTexDelete(tex);

        /* Detach from any FBO's color-tex tracking to avoid dangling refs. */
        for (unsigned k = 0; k < IR_FBO_CAP; k++)
        {
            if (g_fboTable[k].key == IR_EMPTY || g_fboTable[k].key == IR_TOMB)
                continue;
            if (g_fboTable[k].colorTex == tex)
            {
                g_fboTable[k].colorTex = 0;
                g_fboTable[k].hasNativeColor = 0;
                g_fboTable[k].colorW = 0;
                g_fboTable[k].colorH = 0;
            }
        }
    }
}

void irOnBindFramebuffer(GLenum target, GLuint framebuffer)
{
    if (target == GL_FRAMEBUFFER)
    {
        g_drawFBO = framebuffer;
        g_readFBO = framebuffer;
    }
    else if (target == GL_DRAW_FRAMEBUFFER)
    {
        g_drawFBO = framebuffer;
    }
    else if (target == GL_READ_FRAMEBUFFER)
    {
        g_readFBO = framebuffer;
    }

}

void irOnFramebufferTexture2D(GLenum target, GLenum attachment,
                              GLenum textarget, GLuint texture, GLint level)
{
    (void)textarget;

    if (g_renderScale <= 1)
        return;

    if (texture == 0 || level != 0)
        return;

    GLuint fbo = irFboFromTarget(target);
    if (fbo == 0)
        return;

    int isColor = (attachment >= GL_COLOR_ATTACHMENT0 &&
                   attachment <  GL_COLOR_ATTACHMENT0 + 8);

    int slot = irTexFind(texture);

    if (slot >= 0)
    {
        if (isColor && texture != 0)
        {
            int fslot = irFboUpsert(fbo);
            if (fslot >= 0)
            {
                g_fboTable[fslot].hasNativeColor = 1;
                g_fboTable[fslot].colorTex = texture;
                g_fboTable[fslot].colorW   = g_texTable[slot].w;
                g_fboTable[fslot].colorH   = g_texTable[slot].h;
            }
            if (g_texTable[slot].scaled)
                irFboMarkScaled(fbo);
            else if (fslot >= 0 && g_fboTable[fslot].scaled)
            {
                irFboMakeNative(fbo);
            }
        }
        else if (g_texTable[slot].scaled)
            irFboMarkScaled(fbo); 
    }
    else if (isColor && texture != 0)
    {
        int fslot = irFboUpsert(fbo);
        if (fslot >= 0)
        {
            g_fboTable[fslot].hasNativeColor = 1;

            GLint prevActive = 0, prevBind = 0;
            GLsizei realW = 0, realH = 0;
            if (glad_glGetTexLevelParameteriv)
            {
                glad_glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
                glad_glActiveTexture(GL_TEXTURE0);
                glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevBind);
                glad_glBindTexture(GL_TEXTURE_2D, texture);
                glad_glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,  &realW);
                glad_glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &realH);
                glad_glBindTexture(GL_TEXTURE_2D, (GLuint)prevBind);
                glad_glActiveTexture((GLenum)prevActive);
            }
            g_fboTable[fslot].colorTex = texture;
            g_fboTable[fslot].colorW   = realW;
            g_fboTable[fslot].colorH   = realH;

            if (g_fboTable[fslot].scaled)
            {
                if (!irFboMakeScaled(fbo))
                    irFboMakeNative(fbo);
            }
        }
    }
}

void irOnFramebufferRenderbuffer(GLenum target, GLenum attachment,
                                 GLenum renderbuffertarget, GLuint renderbuffer)
{
    (void)renderbuffertarget;

    GLuint fbo = irFboFromTarget(target);
    if (fbo != 0 && renderbuffer != 0)
    {
        int idx = irRbAttachIndex(attachment);
        if (idx >= 0)
        {
            int s = irFboUpsert(fbo);
            if (s >= 0)
                g_fboTable[s].rbAttachments[idx] = renderbuffer;
        }
    }

    if (g_renderScale <= 1)
        return;
    if (renderbuffer == 0)
        return;

    if (g_renderScale > 1)
    {
        GLuint fbo = irFboFromTarget(target);
        if (fbo != 0)
        {
            int fslot = irFboFind(fbo);
            if (fslot >= 0 && g_fboTable[fslot].hasNativeColor)
            {
                if (!irFboMakeScaled(fbo))
                    irFboMakeNative(fbo);
            }
            else
            {
                irFboMarkScaled(fbo);
                irFboClearUnscaleable(fbo);
            }
        }
    }
}

void irOnRenderbufferStorage(GLenum target, GLenum internalformat,
                             GLsizei width, GLsizei height,
                             GLsizei *outW, GLsizei *outH)
{
    (void)target;

    if (g_renderScale <= 1)
    {
        if (outW) *outW = width;
        if (outH) *outH = height;
        return;
    }

    if (width <= 0 || height <= 0)
    {
        if (outW) *outW = width;
        if (outH) *outH = height;
        return;
    }

    GLsizei scW = width  * g_renderScale;
    GLsizei scH = height * g_renderScale;

    if (outW) *outW = scW;
    if (outH) *outH = scH;

    GLint rb = 0;
    glad_glGetIntegerv(0x8CA7, &rb);
    if (rb != 0)
    {
        int slot = irRbUpsert((GLuint)rb);
        if (slot >= 0)
        {
            g_rbTable[slot].key = (GLuint)rb;
            g_rbTable[slot].internalformat = internalformat;
            g_rbTable[slot].width = scW;
            g_rbTable[slot].height = scH;
        }
        else
        {
            log_warn(plfBridges,
                     "[ir] rb cache full; cannot track renderbuffer %u", (GLuint)rb);
        }
    }
}

/* ---- Texture events ---- */

void irOnTexImage2D(GLenum target, GLint level, GLint internalformat,
                    GLsizei *width, GLsizei *height, GLenum format, GLenum type,
                    const void *pixels)
{
    (void)internalformat;
    (void)format;
    (void)type;

    if (g_renderScale <= 1)
        return;

    if (level != 0)
        return;

    int isCubeFace = (target >= 0x8515u && target <= 0x851Au);
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_RECTANGLE && !isCubeFace)
        return;

    GLuint tex;
    if (isCubeFace)
        tex = g_units[g_activeUnitIdx].texCube;
    else
        tex = (target == GL_TEXTURE_2D)
                  ? g_units[g_activeUnitIdx].tex2D
                  : g_units[g_activeUnitIdx].texRect;
    if (tex == 0 || tex == IR_TOMB)
        return;

    GLenum cacheTarget = isCubeFace ? 0x8513u : target;

    if (pixels != NULL)
    {
        if (tex != 0 && tex != IR_TOMB)
        {
            if (irTexFind(tex) >= 0)
            {
                irTexDelete(tex);
                log_debug(plfBridges,
                          "[ir] texture %u re-specified with data; cache invalidated",
                          tex);
            }
        }
        return; /* data textures are not scaled */
    }

    if (!width || !height || *width <= 0 || *height <= 0)
        return;

    int slot = irTexUpsert(tex);
    if (slot < 0)
    {
        log_warn(plfBridges, "[ir] texture cache full; cannot track texture %u", tex);
        return;
    }
    g_texTable[slot].key    = tex;
    g_texTable[slot].w      = *width;  
    g_texTable[slot].h      = *height;
    g_texTable[slot].scaled = 1;
    g_texTable[slot].target = cacheTarget;
    g_texTable[slot].internalformat = internalformat;
    g_texTable[slot].format = format;
    g_texTable[slot].type   = type;

    if (*width == Config.display.width && *height == Config.display.height)
    {
        g_texTable[slot].scaled = 0;
        log_debug(plfBridges,
                 "[ir] glTexImage2D tex=%u %dx%d skipped (display-sized)",
                 tex, *width, *height);
        return;
    }

    *width  *= g_renderScale;
    *height *= g_renderScale;
}

void irOnBindTexture(GLenum target, GLuint texture)
{
    if (target == GL_TEXTURE_2D)
        g_units[g_activeUnitIdx].tex2D = texture;
    else if (target == GL_TEXTURE_RECTANGLE)
        g_units[g_activeUnitIdx].texRect = texture;
    else if (target == GL_TEXTURE_CUBE_MAP)
        g_units[g_activeUnitIdx].texCube = texture;
}

void irOnActiveTexture(GLenum texture)
{
    int idx = (int)(texture - GL_TEXTURE0);
    if (idx < 0 || idx >= IR_MAX_UNITS)
        idx = 0;
    g_activeUnitIdx = idx;
}

/* ---- Queries ---- */

int irIsScaledDrawFBO(void)
{
    if (g_renderScale <= 1 || g_drawFBO == 0)
        return 0;
    int slot = irFboFind(g_drawFBO);
    return (slot >= 0 && g_fboTable[slot].scaled && !g_fboTable[slot].unscaleable);
}

int irIsScaledReadFBO(void)
{
    if (g_renderScale <= 1 || g_readFBO == 0)
        return 0;
    int slot = irFboFind(g_readFBO);
    return (slot >= 0 && g_fboTable[slot].scaled && !g_fboTable[slot].unscaleable);
}


void irOnSwap(GLuint sinkFBO)
{
    if (g_renderScale <= 1 || sinkFBO == 0)
        return;
    int fslot = irFboFind(sinkFBO);
    if (fslot < 0)
        return; 

    if (g_fboTable[fslot].scaled)
        return; 

    if (irFboMakeScaled(sinkFBO))
        log_debug(plfBridges,
                 "[ir] swap: promoted sink FBO %u to scaled (render_scale=%d)",
                 sinkFBO, g_renderScale);
}

