#pragma once

#include <glad/gl.h>

void irInit(void);

int irRenderScale(void);

int irScale(int v);

void irOnGenFramebuffers(GLsizei n, GLuint *framebuffers);

void irOnDeleteFramebuffers(GLsizei n, const GLuint *framebuffers);

void irOnDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers);

void irOnDeleteTextures(GLsizei n, const GLuint *textures);

void irOnBindFramebuffer(GLenum target, GLuint framebuffer);

void irOnFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                              GLuint texture, GLint level);

void irOnFramebufferRenderbuffer(GLenum target, GLenum attachment,
                                 GLenum renderbuffertarget, GLuint renderbuffer);

void irOnRenderbufferStorage(GLenum target, GLenum internalformat,
                             GLsizei width, GLsizei height,
                             GLsizei *outW, GLsizei *outH);

void irOnTexImage2D(GLenum target, GLint level, GLint internalformat,
                    GLsizei *width, GLsizei *height, GLenum format, GLenum type,
                    const void *pixels);
void irOnBindTexture(GLenum target, GLuint texture);
void irOnActiveTexture(GLenum texture);
int irIsScaledDrawFBO(void);
int irIsScaledReadFBO(void);
void irOnSwap(GLuint sinkFBO);

