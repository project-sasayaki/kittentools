#pragma once
#include <glad/gl.h>

void *GLHooks_GetProcAddress(const char *procName);
void __attribute__((cdecl)) wrap_glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void __attribute__((cdecl)) wrap_glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);
void __attribute__((cdecl)) wrap_glNormal3fv(const GLfloat *normal);
void __attribute__((cdecl)) wrap_glColor4ub(GLubyte red, GLubyte green,
                                             GLubyte blue, GLubyte alpha);
void __attribute__((cdecl)) wrap_glTexCoord2f(GLfloat s, GLfloat t);
void __attribute__((cdecl)) wrap_glMultiTexCoord2fARB(GLenum target, GLfloat s,
                                                       GLfloat t);
void __attribute__((cdecl)) wrap_glClientActiveTexture(GLenum texture);
void __attribute__((cdecl)) wrap_glEnableClientState(GLenum array);
void __attribute__((cdecl)) wrap_glDisableClientState(GLenum array);
void __attribute__((cdecl)) wrap_glVertexPointer(GLint size, GLenum type,
                                                  GLsizei stride,
                                                  const GLvoid *pointer);
void __attribute__((cdecl)) wrap_glNormalPointer(GLenum type, GLsizei stride,
                                                  const GLvoid *pointer);
void __attribute__((cdecl)) wrap_glColorPointer(GLint size, GLenum type,
                                                 GLsizei stride,
                                                 const GLvoid *pointer);
void __attribute__((cdecl)) wrap_glTexCoordPointer(GLint size, GLenum type,
                                                    GLsizei stride,
                                                    const GLvoid *pointer);
void __attribute__((cdecl)) wrap_glProgramEnvParameters4fvEXT(GLenum target, GLuint index,
                                                              GLsizei count,
                                                              const GLfloat *params);
void __attribute__((cdecl)) wrap_glProgramLocalParameter4fvARB(GLenum target, GLuint index,
                                                               const GLfloat *params);
