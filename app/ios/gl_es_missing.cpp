// Desktop-OpenGL-functies die de OpenCPN-kern aanroept maar OpenGL ES niet heeft.
//
// glGetTexLevelParameteriv (gui/src/gl_texture_mgr.cpp, CompressUsingGPU): zonder eigen
// definitie linkte de app tegen een niet-openbaar symbool in OpenGLES.framework, en App Store
// Connect weigert dat (fout 90338, 24 nov 2026). Hij vraagt hiermee of de GPU een textuur
// gecomprimeerd heeft: 0 = nee, dan gebruikt OpenCPN de ongecomprimeerde weg.
// De plugins hebben hun eigen vertaallaag (app/ios/glcompat).
// SPDX-License-Identifier: GPL-2.0-or-later

typedef unsigned int GLenum;
typedef int GLint;

extern "C" void glGetTexLevelParameteriv(GLenum, GLint, GLenum, GLint* params) {
  if (params) *params = 0;
}
