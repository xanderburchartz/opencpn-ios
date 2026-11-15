// Calls GLShaderProgram::SetAttributePointerf with a VBO still bound (as after
// radar_pi drew with a VBO) and reads back which buffer the attribute uses.
#include <OpenGL/OpenGL.h>
#include <cstdio>
#include "shaders.h"
const GLchar *preamble = "#version 120\n";

int main() {
  CGLPixelFormatAttribute attrs[] = {kCGLPFAAccelerated, (CGLPixelFormatAttribute)0};
  CGLPixelFormatObj pf; GLint n;
  if (CGLChoosePixelFormat(attrs, &pf, &n) || !pf) { puts("geen pixelformaat"); return 2; }
  CGLContextObj ctx; CGLCreateContext(pf, nullptr, &ctx); CGLSetCurrentContext(ctx);
  printf("GL_RENDERER %s, GL_VERSION %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

  GLShaderProgram prog;
  prog.addShaderFromSource("attribute vec2 position; void main(){ gl_Position = vec4(position,0.0,1.0); }", GL_VERTEX_SHADER);
  prog.addShaderFromSource("void main(){ gl_FragColor = vec4(1.0); }", GL_FRAGMENT_SHADER);
  prog.linkProgram();
  printf("programma gelinkt: %d\n", prog.isOK());
  prog.Bind();

  GLuint vbo; glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  float big[4096] = {0}; glBufferData(GL_ARRAY_BUFFER, sizeof big, big, GL_STATIC_DRAW);
  printf("VBO %u gebonden voor de aanroep\n", vbo);

  static float coords[8] = {-1, -1, 1, -1, 1, 1, -1, 1};
  prog.SetAttributePointerf("position", coords);

  GLint aloc = glGetAttribLocation(prog.programId(), "position");
  GLint binding = -1; void *ptr = nullptr;
  glGetVertexAttribiv(aloc, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &binding);
  glGetVertexAttribPointerv(aloc, GL_VERTEX_ATTRIB_ARRAY_POINTER, &ptr);
  printf("attribuut leest uit buffer %d, pointer %p (coords op %p)\n", binding, ptr, (void *)coords);
  printf("%s\n", binding == 0 ? "GOED: client-geheugen" : "FOUT: pointer is een offset in de VBO");
  return binding == 0 ? 0 : 1;
}
