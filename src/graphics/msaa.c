#include <glad/gl.h>
#include <string.h>
#include "msaa.h"
#include "log/log.h"
#include "../config/config.h"

/* Maximum number of simultaneously proxied FBOs. */
#define MS_CAP 64

typedef struct
{
    int     active;
    GLuint  fbo;        /* engine FBO id (key) */
    GLuint  tex;        /* its colour texture name */
    GLuint  msColorRB;  /* multisampled colour renderbuffer (the proxy) */
    GLuint  msDepthRB;  /* multisampled depth renderbuffer (or 0) */
    GLuint  tmpFBO;     /* scratch FBO that holds `tex` as colour, for resolve */
    GLuint  tmpDepthFBO;/* scratch FBO holding engine depth tex, for depth resolve */
    GLuint  engineDepthTex; /* engine depth TEXTURE (0 if depth was a renderbuffer) */
    GLsizei w, h;
    GLenum  colorFmt;   /* sized internal format of the colour tex */
    int     hasDepth;
    int     depthIsTex; /* 1 if engine depth attachment is a TEXTURE (needs writeback) */
    int     proxied;    /* 1 once we swapped the colour attachment for msRB */
    int     skip;       /* 1 = engine resolve destination / shared-with-skip tex */
    int     dirty;      /* geometry drawn since last resolve */
    unsigned seq;       /* monotonic draw sequence, for shared-texture ordering */
} MSProxy;

static MSProxy g_proxy[MS_CAP];
static int     g_requested;   /* requested MSAA samples (from config) */
static int     g_maxSamples;  /* GL_MAX_SAMPLES */
static unsigned g_seq;
static GLuint  g_readFBO;     /* mirror of READ binding */
static GLuint  g_drawFBO;     /* mirror of DRAW binding */
static GLuint  g_lastSceneFBO; /* last non-default DRAW FBO bound (final composite target) */
static GLuint  g_savedReadFBO; /* READ binding parked on tmpFBO by msPrepareReadResolve */

/* ------------------------------------------------------------------ helpers */

static MSProxy *msFind(GLuint fbo)
{
    for (int i = 0; i < MS_CAP; i++)
        if (g_proxy[i].active && g_proxy[i].fbo == fbo)
            return &g_proxy[i];
    return NULL;
}

static MSProxy *msAlloc(void)
{
    for (int i = 0; i < MS_CAP; i++)
        if (!g_proxy[i].active)
            return &g_proxy[i];
    return NULL;
}

static GLenum msColorStorageFmt(GLenum raw)
{
    switch (raw)
    {
    case GL_RGBA:
    case GL_BGRA:       return GL_RGBA8;
    case GL_RGB:
    case GL_BGR:        return GL_RGB8;
    case GL_RGBA8:
    case GL_RGB8:
    case GL_RGBA16F:
    case GL_RGB16F:
    case GL_RGBA32F:
    case GL_RGB32F:
    case GL_R8:
    case GL_RG8:
    case GL_SRGB8_ALPHA8:
    case GL_SRGB8:      return raw;
    default:            return GL_RGBA8;
    }
}

static int msIsEngineMSAASource(GLuint fbo)
{
    if (fbo == 0)
        return 0;
    GLint prevRead = 0;
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);

    glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, fbo);
    GLint type = GL_NONE;
    glad_glGetFramebufferAttachmentParameterivEXT(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                  GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    int isMS = 0;
    if (type == GL_RENDERBUFFER)
    {
        GLint name = 0;
        glad_glGetFramebufferAttachmentParameterivEXT(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                      GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
        if (name)
        {
            GLint samples = 0;
            glad_glBindRenderbufferEXT(GL_RENDERBUFFER, (GLuint)name);
            glad_glGetRenderbufferParameterivEXT(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &samples);
            isMS = (samples > 0);
        }
    }
    glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    return isMS;
}

/* ------------------------------------------------------------------ lifecycle */

void msInit(void)
{
    int s = Config.display.msaa_samples;
    /* MSAA runs whenever configured, regardless of render_scale. */
    g_requested = (s > 0) ? s : 0;
    g_maxSamples = 0;
    g_seq = 0;
    g_readFBO = 0;
    g_drawFBO = 0;
    g_lastSceneFBO = 0;
    g_savedReadFBO = 0;
    memset(g_proxy, 0, sizeof(g_proxy));
}

void msPostGLInit(void)
{
    if (g_requested <= 0)
        return;

    GLint maxS = 0;
    glad_glGetIntegerv(GL_MAX_SAMPLES, &maxS);
    g_maxSamples = (maxS > 0) ? maxS : 4;
    if (g_requested > g_maxSamples)
        g_requested = g_maxSamples;
}

static int msFinalizeColor(MSProxy *e)
{
    GLint prevTex = 0, prevDraw = 0, prevRead = 0;
    glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);

    /* Size/format from the colour texture. */
    glad_glBindTexture(GL_TEXTURE_2D, e->tex);
    GLint w = 0, h = 0, ifmt = 0;
    glad_glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glad_glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    glad_glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &ifmt);
    glad_glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    if (w <= 0 || h <= 0)
    {
        glad_glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
        glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
        return 0; /* texture storage not ready yet; caller retries on next bind */
    }

    e->w = (GLsizei)w;
    e->h = (GLsizei)h;
    e->colorFmt = msColorStorageFmt((GLenum)ifmt);

    /* Multisampled colour renderbuffer. */
    glad_glGenRenderbuffersEXT(1, &e->msColorRB);
    glad_glBindRenderbufferEXT(GL_RENDERBUFFER, e->msColorRB);
    glad_glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, g_requested, e->colorFmt, e->w, e->h);

    /* Scratch FBO holding the colour texture, used as the resolve DRAW target. */
    glad_glGenFramebuffersEXT(1, &e->tmpFBO);
    glad_glBindFramebufferEXT(GL_FRAMEBUFFER, e->tmpFBO);
    glad_glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, e->tex, 0);
    GLenum status = glad_glCheckFramebufferStatusEXT(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        log_warn(plfBridges, "[msaa] FBO %u: scratch resolve FBO incomplete (0x%X); skipping",
                 e->fbo, status);
        glad_glBindFramebufferEXT(GL_FRAMEBUFFER, (GLuint)prevDraw);
        glad_glBindRenderbufferEXT(GL_RENDERBUFFER, 0);
        if (e->msColorRB) { glad_glDeleteRenderbuffersEXT(1, &e->msColorRB); e->msColorRB = 0; }
        if (e->tmpFBO)    { glad_glDeleteFramebuffersEXT(1, &e->tmpFBO);       e->tmpFBO = 0; }
        e->skip = 1;
        glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
        return 0;
    }

    /* Swap the engine FBO's colour attachment for the multisampled renderbuffer. */
    glad_glBindFramebufferEXT(GL_FRAMEBUFFER, e->fbo);
    glad_glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_RENDERBUFFER, e->msColorRB);

    glad_glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
    glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    glad_glBindRenderbufferEXT(GL_RENDERBUFFER, 0);

    e->proxied = 1;
    e->dirty   = 0;

    log_debug(plfBridges, "[msaa] FBO %u proxied: tex %u %dx%d %s @%dx",
              e->fbo, e->tex, e->w, e->h, e->colorFmt == GL_RGBA16F ? "RGBA16F" : "RGBA8",
              g_requested);
    return 1;
}

static void msEnsureDepth(MSProxy *e)
{
    if (!e->proxied || e->skip)
        return;

    GLint prevDraw = 0, prevRead = 0;
    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);

    glad_glBindFramebufferEXT(GL_FRAMEBUFFER, e->fbo);
    GLint dtype = GL_NONE;
    glad_glGetFramebufferAttachmentParameterivEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                                  GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &dtype);
    if (dtype == GL_NONE)
    {
        /* Engine has no depth (or detached it). Drop our MS depth state. */
        if (e->msDepthRB)  { glad_glDeleteRenderbuffersEXT(1, &e->msDepthRB);  e->msDepthRB = 0; }
        if (e->tmpDepthFBO){ glad_glDeleteFramebuffersEXT(1, &e->tmpDepthFBO); e->tmpDepthFBO = 0; }
        e->hasDepth = 0;
        e->depthIsTex = 0;
        e->engineDepthTex = 0;
        glad_glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
        glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
        return;
    }

    GLint dName = 0;
    glad_glGetFramebufferAttachmentParameterivEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                                  GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &dName);
    GLint stype = GL_NONE;
    glad_glGetFramebufferAttachmentParameterivEXT(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                                  GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &stype);

    if (dtype == GL_RENDERBUFFER && dName != 0 && (GLuint)dName == e->msDepthRB)
    {
        /* Already ours; nothing to do. */
        glad_glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
        glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
        return;
    }

    e->depthIsTex = (dtype == GL_TEXTURE);
    e->engineDepthTex = e->depthIsTex ? (GLuint)dName : 0;

    if (!e->msDepthRB)
        glad_glGenRenderbuffersEXT(1, &e->msDepthRB);
    glad_glBindRenderbufferEXT(GL_RENDERBUFFER, e->msDepthRB);
    glad_glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, g_requested,
                                             GL_DEPTH24_STENCIL8, e->w, e->h);
    glad_glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                      GL_RENDERBUFFER, e->msDepthRB);
    /* If the engine keeps a stencil attachment, point it at our MS RB too
     * (DEPTH24_STENCIL8 provides both) so the FBO stays single-format. */
    if (stype != GL_NONE)
        glad_glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER, e->msDepthRB);

    if (e->depthIsTex && e->engineDepthTex)
    {
        if (!e->tmpDepthFBO)
            glad_glGenFramebuffersEXT(1, &e->tmpDepthFBO);
        glad_glBindFramebufferEXT(GL_FRAMEBUFFER, e->tmpDepthFBO);
        glad_glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                       GL_TEXTURE_2D, e->engineDepthTex, 0);

        glad_glDrawBuffer(GL_NONE);
        glad_glReadBuffer(GL_NONE);
    }
    else if (e->tmpDepthFBO)
    {
        glad_glDeleteFramebuffersEXT(1, &e->tmpDepthFBO);
        e->tmpDepthFBO = 0;
    }

    e->hasDepth = 1;

    log_debug(plfBridges, "[msaa] FBO %u depth -> MS RB (engine %s %u)",
              e->fbo, e->depthIsTex ? "texture" : "renderbuffer", e->engineDepthTex);
    glad_glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
    glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    glad_glBindRenderbufferEXT(GL_RENDERBUFFER, 0);
}

static void msRebuild(MSProxy *e)
{
    GLint prevTex = 0, prevDraw = 0, prevRead = 0;
    glad_glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);

    glad_glBindTexture(GL_TEXTURE_2D, e->tex);
    GLint w = 0, h = 0, ifmt = 0;
    glad_glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glad_glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    glad_glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &ifmt);
    glad_glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    if (w <= 0 || h <= 0)
        return;
    if (w == e->w && h == e->h && (GLenum)ifmt == e->colorFmt)
        return; /* unchanged */

    e->w = (GLsizei)w;
    e->h = (GLsizei)h;
    e->colorFmt = msColorStorageFmt((GLenum)ifmt);

    if (e->msColorRB) { glad_glDeleteRenderbuffersEXT(1, &e->msColorRB); e->msColorRB = 0; }
    glad_glGenRenderbuffersEXT(1, &e->msColorRB);
    glad_glBindRenderbufferEXT(GL_RENDERBUFFER, e->msColorRB);
    glad_glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, g_requested, e->colorFmt, e->w, e->h);

    glad_glBindFramebufferEXT(GL_FRAMEBUFFER, e->fbo);
    glad_glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_RENDERBUFFER, e->msColorRB);
    if (e->hasDepth)
    {
        if (e->msDepthRB) { glad_glDeleteRenderbuffersEXT(1, &e->msDepthRB); e->msDepthRB = 0; }
        glad_glGenRenderbuffersEXT(1, &e->msDepthRB);
        glad_glBindRenderbufferEXT(GL_RENDERBUFFER, e->msDepthRB);
        glad_glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, g_requested,
                                                 GL_DEPTH24_STENCIL8, e->w, e->h);
        glad_glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          GL_RENDERBUFFER, e->msDepthRB);
        glad_glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER, e->msDepthRB);
    }
    /* Reattach the (new-size) texture to the scratch resolve FBO. */
    if (e->tmpFBO)
    {
        glad_glBindFramebufferEXT(GL_FRAMEBUFFER, e->tmpFBO);
        glad_glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_2D, e->tex, 0);
    }
    if (e->tmpDepthFBO && e->engineDepthTex)
    {
        glad_glBindFramebufferEXT(GL_FRAMEBUFFER, e->tmpDepthFBO);
        glad_glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                       GL_TEXTURE_2D, e->engineDepthTex, 0);
    }
    glad_glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
    glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    glad_glBindRenderbufferEXT(GL_RENDERBUFFER, 0);

    log_debug(plfBridges, "[msaa] FBO %u rebuilt at %dx%d (tex re-specified)", e->fbo, e->w, e->h);
}

static void msUnproxy(MSProxy *e)
{
    GLint prevDraw = 0, prevRead = 0;
    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);

    if (e->proxied)
    {
        glad_glBindFramebufferEXT(GL_FRAMEBUFFER, e->fbo);
        glad_glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_2D, e->tex, 0);

        if (e->hasDepth && e->engineDepthTex)
            glad_glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                           GL_TEXTURE_2D, e->engineDepthTex, 0);
        else if (e->hasDepth && e->msDepthRB)
            glad_glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                              GL_RENDERBUFFER, 0);
    }
    if (e->msColorRB) { glad_glDeleteRenderbuffersEXT(1, &e->msColorRB); e->msColorRB = 0; }
    if (e->msDepthRB) { glad_glDeleteRenderbuffersEXT(1, &e->msDepthRB); e->msDepthRB = 0; }
    if (e->tmpFBO)     { glad_glDeleteFramebuffersEXT(1, &e->tmpFBO);     e->tmpFBO = 0; }
    if (e->tmpDepthFBO){ glad_glDeleteFramebuffersEXT(1, &e->tmpDepthFBO);e->tmpDepthFBO = 0; }

    glad_glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
    glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, (GLuint)prevRead);

    e->proxied = 0;
}

static void msMarkSkip(GLuint fbo)
{
    MSProxy *e = msFind(fbo);
    if (!e)
        return;
    if (e->proxied)
        msUnproxy(e);
    e->skip = 1;
    GLuint tex = e->tex;
    for (int i = 0; i < MS_CAP; i++)
    {
        MSProxy *o = &g_proxy[i];
        if (o->active && o->tex == tex)
        {
            if (o->proxied)
                msUnproxy(o);
            o->skip = 1;
        }
    }
}

static int msCheckComplete(MSProxy *e)
{
    GLint prevDraw = 0, prevRead = 0;
    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);

    glad_glBindFramebufferEXT(GL_FRAMEBUFFER, e->fbo);
    GLenum status = glad_glCheckFramebufferStatusEXT(GL_FRAMEBUFFER);
    glad_glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
    glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, (GLuint)prevRead);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        log_warn(plfBridges,
                 "[msaa] FBO %u incomplete after proxying (0x%X); MSAA disabled for it",
                 e->fbo, status);
        msMarkSkip(e->fbo);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ resolve */

static void msResolveFBO(MSProxy *e)
{
    GLint prevRead = 0, prevDraw = 0, prevRB = 0, prevRBuf = 0, prevDBuf = 0;
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    glad_glGetIntegerv(GL_RENDERBUFFER_BINDING, &prevRB);
    glad_glGetIntegerv(GL_READ_BUFFER, &prevRBuf);
    glad_glGetIntegerv(GL_DRAW_BUFFER, &prevDBuf);

    /* Colour: MS colour RB -> engine colour texture. */
    glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, e->fbo);
    glad_glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, e->tmpFBO);
    glad_glReadBuffer(GL_COLOR_ATTACHMENT0);
    glad_glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glad_glBlitFramebufferEXT(0, 0, e->w, e->h, 0, 0, e->w, e->h,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);

    if (e->depthIsTex && e->engineDepthTex && e->tmpDepthFBO)
    {
        glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, e->fbo);
        glad_glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, e->tmpDepthFBO);
        glad_glDrawBuffer(GL_NONE);
        glad_glReadBuffer(GL_NONE);
        glad_glBlitFramebufferEXT(0, 0, e->w, e->h, 0, 0, e->w, e->h,
                                  GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);
    }

    glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    glad_glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
    glad_glBindRenderbufferEXT(GL_RENDERBUFFER, (GLuint)prevRB);
    glad_glReadBuffer((GLenum)prevRBuf);
    glad_glDrawBuffer((GLenum)prevDBuf);

    e->dirty = 0;
}

void msResolveTex(GLuint tex)
{
    if (g_requested <= 0 || tex == 0)
        return;

    MSProxy *best = NULL;
    for (int i = 0; i < MS_CAP; i++)
    {
        MSProxy *e = &g_proxy[i];
        if (e->active && e->proxied && !e->skip && e->dirty && e->tex == tex)
        {
            if (!best || e->seq > best->seq)
                best = e;
        }
    }
    if (best)
        msResolveFBO(best);
}

/* ------------------------------------------------------------------ blit hook */

void msOnBlit(void)
{
    if (g_requested <= 0)
        return;
    GLint rd = 0, dw = 0;
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &rd);
    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &dw);

    if (rd == 0 || dw == 0 || rd == dw)
        return;

    int srcIsOurs = (msFind((GLuint)rd) != NULL);
    if (msIsEngineMSAASource((GLuint)rd) && !srcIsOurs)
    {
        MSProxy *e = msFind((GLuint)dw);
        if (e && !e->skip)
            msMarkSkip((GLuint)dw);
    }
}

int msPrepareReadResolve(void)
{
    if (g_requested <= 0)
        return 0;
    GLint rd = 0;
    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &rd);
    MSProxy *e = msFind((GLuint)rd);
    if (!e || !e->proxied || e->skip)
        return 0;

    if (e->dirty)
        msResolveFBO(e);
    g_savedReadFBO = (GLuint)rd;
    glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, e->tmpFBO);
    return 1;
}

void msRestoreReadFBO(void)
{
    if (g_savedReadFBO)
    {
        glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, g_savedReadFBO);
        g_savedReadFBO = 0;
    }
}

GLuint msGetSinkFBO(void)
{
    return g_lastSceneFBO;
}

void msOnSwap(void)
{
    if (g_requested <= 0 || g_lastSceneFBO == 0)
        return;
    MSProxy *e = msFind(g_lastSceneFBO);
    if (e && e->proxied && !e->skip && e->dirty)
        msResolveFBO(e);
}

GLuint msOnBindFramebuffer(GLenum target, GLuint framebuffer)
{
    if (framebuffer != 0 &&
        (target == GL_DRAW_FRAMEBUFFER || target == GL_FRAMEBUFFER))
        g_lastSceneFBO = framebuffer;

    if (g_requested <= 0)
        return framebuffer;

    GLuint prevDraw = g_drawFBO;

    /* Update read/draw mirrors. */
    if (target == GL_READ_FRAMEBUFFER)
        g_readFBO = framebuffer;
    else if (target == GL_DRAW_FRAMEBUFFER)
        g_drawFBO = framebuffer;
    else
    {
        g_readFBO = framebuffer;
        g_drawFBO = framebuffer;
    }

    if (framebuffer != 0 && (target == GL_DRAW_FRAMEBUFFER || target == GL_FRAMEBUFFER))
    {
        MSProxy *e = msFind(framebuffer);
        if (e)
        {
            if (!e->skip && !e->proxied)
                msFinalizeColor(e);
            if (e->proxied && !e->skip)
                msEnsureDepth(e);

            if (e->proxied && !e->skip)
            {
                if (msCheckComplete(e))
                {
                    e->dirty = 1;
                    e->seq = ++g_seq;
                }
            }
        }
    }

    if (prevDraw != 0 && prevDraw != framebuffer)
    {
        MSProxy *p = msFind(prevDraw);
        if (p)
        {
            if (p->proxied && !p->skip && p->dirty)
                msResolveFBO(p);
        }
    }

    return framebuffer; /* never redirect */
}

int msOnFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                             GLuint texture, GLint level)
{
    if (g_requested <= 0)
        return 0;
    if (textarget != GL_TEXTURE_2D)
        return 0;
    if (level != 0)
        return 0;

    GLint b = 0;
    glad_glGetIntegerv((target == GL_READ_FRAMEBUFFER) ? GL_READ_FRAMEBUFFER_BINDING
                                                        : GL_DRAW_FRAMEBUFFER_BINDING,
                       &b);
    GLuint fbo = (GLuint)b;
    if (fbo == 0)
        return 0; /* never proxy the default framebuffer */

    MSProxy *e = msFind(fbo);
    if (!e && attachment == GL_COLOR_ATTACHMENT0 && texture != 0)
    {
        for (int i = 0; i < MS_CAP; i++)
        {
            if (g_proxy[i].active && g_proxy[i].tex == texture)
                return 0; 
        }

        e = msAlloc();
        if (!e)
            return 0;
        memset(e, 0, sizeof(*e));
        e->active = 1;
        e->fbo = fbo;
        e->tex = texture;
    }

    if (attachment == GL_DEPTH_ATTACHMENT || attachment == GL_STENCIL_ATTACHMENT)
    {
        if (!e || e->skip || !e->proxied)
            return 0; /* engine keeps its attachment until we proxy */

        if (texture == 0)
        {
            if (e->msDepthRB)  { glad_glDeleteRenderbuffersEXT(1, &e->msDepthRB);  e->msDepthRB = 0; }
            if (e->tmpDepthFBO){ glad_glDeleteFramebuffersEXT(1, &e->tmpDepthFBO); e->tmpDepthFBO = 0; }
            e->hasDepth = 0;
            e->depthIsTex = 0;
            e->engineDepthTex = 0;
            return 0;
        }

        e->engineDepthTex = texture;
        e->depthIsTex = 1;
        e->hasDepth = 1;

        GLint prevDraw = 0, prevRead = 0;
        glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
        glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);

        if (!e->tmpDepthFBO)
            glad_glGenFramebuffersEXT(1, &e->tmpDepthFBO);
        glad_glBindFramebufferEXT(GL_FRAMEBUFFER, e->tmpDepthFBO);
        glad_glFramebufferTexture2DEXT(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture, 0);
        glad_glDrawBuffer(GL_NONE);
        glad_glReadBuffer(GL_NONE);

        glad_glBindFramebufferEXT(GL_FRAMEBUFFER, e->fbo);
        if (attachment == GL_DEPTH_ATTACHMENT || attachment == GL_STENCIL_ATTACHMENT)
        {
            if (!e->msDepthRB)
            {
                glad_glGenRenderbuffersEXT(1, &e->msDepthRB);
                glad_glBindRenderbufferEXT(GL_RENDERBUFFER, e->msDepthRB);
                glad_glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, g_requested,
                                                         GL_DEPTH24_STENCIL8, e->w, e->h);
            }
            glad_glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, attachment,
                                              GL_RENDERBUFFER, e->msDepthRB);
        }
        glad_glBindRenderbufferEXT(GL_RENDERBUFFER, 0);
        glad_glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
        glad_glBindFramebufferEXT(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
        return 1;
    }

    if (attachment != GL_COLOR_ATTACHMENT0)
        return 0;

    if (texture == 0)
    {
        if (e)
        {
            msUnproxy(e);
            e->active = 0;
        }
        return 0;
    }

    e->tex = texture;

    if (e->proxied && !e->skip)
    {
        msRebuild(e);
        return 1;
    }
    return 0; 
}

void msInvalidateFBO(GLuint fbo)
{
    if (fbo == 0)
        return;
    for (int i = 0; i < MS_CAP; i++)
    {
        MSProxy *e = &g_proxy[i];
        if (e->active && e->fbo == fbo)
        {
            msUnproxy(e);
            e->active = 0;
        }
    }
}

void msInvalidateTex(GLuint tex)
{
    if (tex == 0)
        return;
    for (int i = 0; i < MS_CAP; i++)
    {
        MSProxy *e = &g_proxy[i];
        if (e->active && e->tex == tex)
        {
            msUnproxy(e);
            e->active = 0;
        }
    }
}
