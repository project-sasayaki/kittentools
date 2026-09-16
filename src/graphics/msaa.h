#include <glad/gl.h>

void msInit(void);
void msPostGLInit(void);
GLuint msOnBindFramebuffer(GLenum target, GLuint framebuffer);
int msOnFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                             GLuint texture, GLint level);
void msInvalidateFBO(GLuint fbo);
void msInvalidateTex(GLuint tex);
void msResolveTex(GLuint tex);
void msOnBlit(void);
int msPrepareReadResolve(void);
void msRestoreReadFBO(void);
GLuint msGetSinkFBO(void);
void msOnSwap(void);
