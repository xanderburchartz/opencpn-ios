// Vertaallaag: oude desktop-OpenGL (fixed function) bovenop OpenGL ES 2/3, voor radar_pi.
//
// radar_pi tekent met glBegin/glVertex/glColor/glTexCoord, een matrixstapel
// (glPushMatrix/glTranslate/glRotate/glScale/glOrtho), client-arrays en glPushAttrib.
// OpenGL ES heeft die niet. Deze laag houdt die toestand zelf bij en tekent met één
// eigen GLSL ES-shader. Alleen de functies die radar_pi gebruikt (telling 9 nov 2026,
// docs/logboek.md); de rest bestaat niet.
//
// Alleen voor radar_pi: deze header komt via pi_common.h (patch) binnen, en glcompat.cpp
// zit in het objectbestand van de plugin, waar alles behalve create/destroy lokaal is
// (app/cmake/ios-plugins.cmake). De rest van OpenCPN merkt er niets van.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef OCPNIOS_GLCOMPAT_H
#define OCPNIOS_GLCOMPAT_H

#include <qopengl.h>  // OpenGL ES-headers van Qt (iOS)

#ifdef __cplusplus
extern "C" {
#endif

// ---- Typen en constanten die OpenGL ES niet heeft ----------------------------------
typedef double GLdouble;

#ifndef GL_QUADS
#define GL_QUADS 0x0007
#endif
#ifndef GL_POLYGON
#define GL_POLYGON 0x0009  // als driehoekwaaier getekend (alleen convexe veelhoeken)
#endif
#define GL_POLYGON_BIT 0x00000008
#define GL_LINE_SMOOTH_HINT 0x0C52
#define GL_POLYGON_SMOOTH_HINT 0x0C53
#define GL_MODELVIEW 0x1700
#define GL_MATRIX_MODE 0x0BA0
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_PROJECTION_MATRIX 0x0BA7
#define GL_PROJECTION 0x1701
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_LINE_STIPPLE 0x0B24
#define GL_LINE_SMOOTH 0x0B20
#define GL_POLYGON_SMOOTH 0x0B41
#define GL_COLOR_MATERIAL 0x0B57
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_MODULATE 0x2100
#define GL_LINE_BIT 0x00000004
#define GL_ENABLE_BIT 0x00002000
#define GL_HINT_BIT 0x00008000
#define GL_TEXTURE_BIT 0x00040000
#define GL_ALL_ATTRIB_BITS 0x000FFFFF
#ifndef GL_DOUBLE
#define GL_DOUBLE 0x140A
#endif

// ---- Shaderfunctietypen (radar_pi haalt ze op als functiepointer) -------------------
// Alleen op verzoek: plugins met plugin_dc brengen een eigen glext.h met dezelfde namen.
#ifdef OCPNIOS_GLCOMPAT_WANT_PFN
typedef GLuint (*PFNGLCREATESHADERPROC)(GLenum);
typedef void (*PFNGLDELETESHADERPROC)(GLuint);
typedef void (*PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar *const *, const GLint *);
typedef void (*PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint *);
typedef void (*PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef GLuint (*PFNGLCREATEPROGRAMPROC)(void);
typedef void (*PFNGLDELETEPROGRAMPROC)(GLuint);
typedef void (*PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void (*PFNGLLINKPROGRAMPROC)(GLuint);
typedef void (*PFNGLUSEPROGRAMPROC)(GLuint);
typedef void (*PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint *);
typedef void (*PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (*PFNGLVALIDATEPROGRAMPROC)(GLuint);
typedef void (*PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void (*PFNGLUNIFORM1FVPROC)(GLint, GLsizei, const GLfloat *);
typedef void (*PFNGLUNIFORM2FVPROC)(GLint, GLsizei, const GLfloat *);
typedef void (*PFNGLUNIFORM3FVPROC)(GLint, GLsizei, const GLfloat *);
typedef void (*PFNGLUNIFORM4FVPROC)(GLint, GLsizei, const GLfloat *);
typedef void (*PFNGLUNIFORMMATRIX4FVPROC)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void (*PFNGLGETACTIVEATTRIBPROC)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
typedef GLint (*PFNGLGETATTRIBLOCATIONPROC)(GLuint, const GLchar *);
typedef GLint (*PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar *);
typedef void (*PFNGLGETACTIVEUNIFORMPROC)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
typedef void (*PFNGLCOMPILESHADERPROC)(GLuint);
#endif  // OCPNIOS_GLCOMPAT_WANT_PFN

// ---- Oude functies, door de laag uitgevoerd -----------------------------------------
void glBegin(GLenum mode);
void glEnd(void);
void glVertex2f(GLfloat x, GLfloat y);
void glVertex2i(GLint x, GLint y);
void glVertex2d(GLdouble x, GLdouble y);
void glTexCoord2f(GLfloat s, GLfloat t);
void glTexCoord2i(GLint s, GLint t);
void glTexCoord2d(GLdouble s, GLdouble t);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glVertex3fv(const GLfloat *v);
void glMultMatrixf(const GLfloat *m);
void glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void glPointSize(GLfloat size);  // via gl_PointSize in de shader
void ocpnios_glGetFloatv(GLenum pname, GLfloat *data);  // + GL_MODELVIEW/PROJECTION_MATRIX
void ocpnios_glGetIntegerv(GLenum pname, GLint *data);  // + GL_MATRIX_MODE
void glMultMatrixd(const GLdouble *m);
void glPolygonMode(GLenum face, GLenum mode);      // geen ES-equivalent; doet niets
void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                  GLint border, GLenum format, GLenum type, const void *pixels);  // idem
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void glColor3ub(GLubyte r, GLubyte g, GLubyte b);
void glColor3ubv(const GLubyte *v);
void glColor3f(GLfloat r, GLfloat g, GLfloat b);
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glColor4fv(const GLfloat *v);

void glMatrixMode(GLenum mode);
void glLoadIdentity(void);
void glPushMatrix(void);
void glPopMatrix(void);
void glTranslated(GLdouble x, GLdouble y, GLdouble z);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glScaled(GLdouble x, GLdouble y, GLdouble z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);

void glPushAttrib(GLbitfield mask);
void glPopAttrib(void);
void glEnableClientState(GLenum array);
void glDisableClientState(GLenum array);
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *ptr);
void glColorPointer(GLint size, GLenum type, GLsizei stride, const void *ptr);
void glLineStipple(GLint factor, GLushort pattern);
void glTexEnvi(GLenum target, GLenum pname, GLint param);

// Displaylijsten: alleen getekende vormen (glBegin/glEnd, glDrawArrays) worden bewaard en bij
// glCallList met de dan geldende matrices opnieuw getekend; matrix- en kleurwisselingen
// binnen de lijst niet (WeatherRouting zet die buiten de lijst).
#define GL_COMPILE 0x1300
#define GL_COMPILE_AND_EXECUTE 0x1301
GLuint glGenLists(GLsizei range);
void glNewList(GLuint list, GLenum mode);
void glEndList(void);
void glCallList(GLuint list);
void glDeleteLists(GLuint list, GLsizei range);

// ---- Bestaande ES-functies met een oud-GL-betekenis die de laag moet zien -----------
void ocpnios_glEnable(GLenum cap);
void ocpnios_glDisable(GLenum cap);
void ocpnios_glDrawArrays(GLenum mode, GLint first, GLsizei count);
void ocpnios_glUseProgram(GLuint program);
void ocpnios_glBindTexture(GLenum target, GLuint texture);
void ocpnios_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                          GLsizei height, GLint border, GLenum format, GLenum type,
                          const void *pixels);

/** Begin van een overlay op de kaart: projectie = pixels van de huidige viewport
 *  (zoals OpenCPN op de desktop vóór RenderGLOverlay), modelview = eenheid. */
void ocpnios_glcompat_begin_overlay(void);
/** Voor radar_pi's SET_FUNCTION_POINTER: geeft voor glUseProgram de wrapper van de laag,
 *  anders het echte ES-symbool. */
void *ocpnios_glcompat_getproc(const char *name);

#ifdef __cplusplus
}
#endif

#ifndef OCPNIOS_GLCOMPAT_IMPL
#define glEnable ocpnios_glEnable
#define glDisable ocpnios_glDisable
#define glDrawArrays ocpnios_glDrawArrays
#define glBindTexture ocpnios_glBindTexture
#define glTexImage2D ocpnios_glTexImage2D
#define glGetFloatv ocpnios_glGetFloatv
#define glGetIntegerv ocpnios_glGetIntegerv
#endif

#endif  // OCPNIOS_GLCOMPAT_H
