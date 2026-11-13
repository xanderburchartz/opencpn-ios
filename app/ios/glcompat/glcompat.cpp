// Vertaallaag oude OpenGL → OpenGL ES voor radar_pi. Zie glcompat.h.
// SPDX-License-Identifier: GPL-2.0-or-later

#define OCPNIOS_GLCOMPAT_IMPL
#include "glcompat.h"
// Bij plugins met GLCOMPAT is glcompat.h al geforceerd ingesloten vóór OCPNIOS_GLCOMPAT_IMPL;
// dan staan de macro's aan en zou ocpnios_glEnable zichzelf aanroepen.
#undef glEnable
#undef glDisable
#undef glDrawArrays
#undef glBindTexture
#undef glTexImage2D
#undef glGetFloatv

#include <QOpenGLContext>
#include <dlfcn.h>

#include <cmath>
#include <cstring>
#include <map>
#include <vector>

namespace {

// ---------------------------------------------------------------- matrices (kolomorde)
struct Mat4 {
  float m[16];
};

Mat4 Identity() {
  Mat4 r{};
  r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
  return r;
}

Mat4 Mul(const Mat4 &a, const Mat4 &b) {  // a * b
  Mat4 r{};
  for (int c = 0; c < 4; ++c)
    for (int rr = 0; rr < 4; ++rr) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
      r.m[c * 4 + rr] = s;
    }
  return r;
}

struct State {
  GLenum matrix_mode = GL_MODELVIEW;
  std::vector<Mat4> modelview{Identity()};
  std::vector<Mat4> projection{Identity()};

  float color[4] = {1, 1, 1, 1};
  float texcoord[2] = {0, 0};
  bool texture_enabled = false;
  bool texenv_replace = false;
  float point_size = 1.f;

  // immediate mode
  bool in_begin = false;
  GLenum begin_mode = 0;
  std::vector<float> verts;  // per hoekpunt: x y z s t r g b a (K = 9)

  // client-arrays
  bool va_enabled = false, ca_enabled = false;
  GLint va_size = 2, ca_size = 4;
  GLenum va_type = GL_FLOAT, ca_type = GL_UNSIGNED_BYTE;
  GLsizei va_stride = 0, ca_stride = 0;
  const void *va_ptr = nullptr, *ca_ptr = nullptr;

  GLuint user_program = 0;  // via ocpnios_glUseProgram (radar-shader)

  // Displaylijsten
  struct ListCmd {
    GLenum mode;
    std::vector<float> verts;
  };
  std::map<GLuint, std::vector<ListCmd>> lists;
  GLuint next_list = 1;
  GLuint recording = 0;
  bool record_execute = false;

  // Formaat per textuur: GL_ALPHA gedraagt zich in desktop-GL (GL_MODULATE) anders dan
  // in een ES-shader (texture2D geeft dan rgb = 0).
  std::map<GLuint, GLenum> tex_format;
  GLuint bound_texture = 0;

  struct Attrib {
    GLbitfield mask;
    float color[4];
    bool texture_enabled, texenv_replace, blend;
    GLint blend_src, blend_dst;
    float line_width;
  };
  std::vector<Attrib> attrib_stack;
};

State &S() {
  static State s;
  return s;
}

Mat4 &Top() {
  State &s = S();
  return s.matrix_mode == GL_PROJECTION ? s.projection.back() : s.modelview.back();
}

void MultTop(const Mat4 &m) {
  Mat4 &t = Top();
  t = Mul(t, m);
}

// ---------------------------------------------------------------- eigen shader
struct Program {
  GLuint prog = 0;
  GLint a_pos = -1, a_tex = -1, a_col = -1;
  GLint u_mvp = -1, u_mode = -1, u_tex = -1, u_psize = -1;
};

const char *kVS =
    "attribute vec4 a_pos;\n"
    "attribute vec2 a_tex;\n"
    "attribute vec4 a_col;\n"
    "uniform mat4 u_mvp;\n"
    "uniform float u_psize;\n"
    "varying vec2 v_tex;\n"
    "varying vec4 v_col;\n"
    "void main() { v_tex = a_tex; v_col = a_col; gl_Position = u_mvp * a_pos;\n"
    "  gl_PointSize = u_psize; }\n";

// u_mode: 0 = kleur, 1 = textuur x kleur (GL_MODULATE), 2 = textuur (GL_REPLACE),
//         3 = alfatextuur: kleur van glColor, alpha = kleur.a x textuur.a
const char *kFS =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform int u_mode;\n"
    "varying vec2 v_tex;\n"
    "varying vec4 v_col;\n"
    "void main() {\n"
    "  if (u_mode == 0) gl_FragColor = v_col;\n"
    "  else if (u_mode == 1) gl_FragColor = texture2D(u_tex, v_tex) * v_col;\n"
    "  else if (u_mode == 3) gl_FragColor = vec4(v_col.rgb, v_col.a * texture2D(u_tex, v_tex).a);\n"
    "  else gl_FragColor = texture2D(u_tex, v_tex);\n"
    "}\n";

GLuint Compile(GLenum type, const char *src) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512] = {0};
    glGetShaderInfoLog(sh, sizeof(log) - 1, nullptr, log);
    qWarning("glcompat: shader compileert niet: %s", log);
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

// Per GL-context een eigen programma (de radar heeft ook een eigen venster/context).
Program &CompatProgram() {
  static std::map<const void *, Program> programs;
  Program &p = programs[QOpenGLContext::currentContext()];
  if (p.prog) return p;
  GLuint vs = Compile(GL_VERTEX_SHADER, kVS);
  GLuint fs = Compile(GL_FRAGMENT_SHADER, kFS);
  if (!vs || !fs) return p;
  p.prog = glCreateProgram();
  glAttachShader(p.prog, vs);
  glAttachShader(p.prog, fs);
  glLinkProgram(p.prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  p.a_pos = glGetAttribLocation(p.prog, "a_pos");
  p.a_tex = glGetAttribLocation(p.prog, "a_tex");
  p.a_col = glGetAttribLocation(p.prog, "a_col");
  p.u_mvp = glGetUniformLocation(p.prog, "u_mvp");
  p.u_mode = glGetUniformLocation(p.prog, "u_mode");
  p.u_tex = glGetUniformLocation(p.prog, "u_tex");
  p.u_psize = glGetUniformLocation(p.prog, "u_psize");
  return p;
}

// ---------------------------------------------------------------- tekenen
// verts: x y z s t r g b a per hoekpunt (K floats).
constexpr size_t K = 9;
void DrawNow(GLenum mode, const std::vector<float> &in);

void Draw(GLenum mode, const std::vector<float> &in) {
  State &s = S();
  if (in.empty()) return;
  if (s.recording) {
    s.lists[s.recording].push_back({mode, in});
    if (!s.record_execute) return;
  }
  DrawNow(mode, in);
}

void DrawNow(GLenum mode, const std::vector<float> &in) {
  State &s = S();
  const std::vector<float> *v = &in;
  std::vector<float> tri;
  if (mode == GL_QUADS) {  // vierhoeken → twee driehoeken (0 1 2, 0 2 3)
    size_t n = in.size() / K;
    tri.reserve(n / 4 * 6 * K);
    for (size_t q = 0; q + 3 < n; q += 4) {
      const size_t idx[6] = {q, q + 1, q + 2, q, q + 2, q + 3};
      for (size_t i : idx) tri.insert(tri.end(), &in[i * K], &in[i * K] + K);
    }
    v = &tri;
    mode = GL_TRIANGLES;
  }
  if (mode == GL_POLYGON) mode = GL_TRIANGLE_FAN;
  const GLsizei count = (GLsizei)(v->size() / K);
  if (!count) return;

  Mat4 mvp = Mul(s.projection.back(), s.modelview.back());

  GLint a_pos, a_tex, a_col, u_mvp;
  GLuint prog;
  bool own = (s.user_program == 0);
  if (own) {
    Program &p = CompatProgram();
    if (!p.prog) return;
    prog = p.prog;
    a_pos = p.a_pos;
    a_tex = p.a_tex;
    a_col = p.a_col;
    u_mvp = p.u_mvp;
    glUseProgram(prog);
    int mode = 0;
    if (s.texture_enabled) {
      auto f = s.tex_format.find(s.bound_texture);
      bool alpha_only = f != s.tex_format.end() && f->second == GL_ALPHA;
      mode = alpha_only ? 3 : (s.texenv_replace ? 2 : 1);
    }
    glUniform1i(p.u_mode, mode);
    glUniform1i(p.u_tex, 0);
    glUniform1f(p.u_psize, s.point_size);
  } else {
    // Programma van radar_pi zelf (iOS-variant van de radarshader, zelfde namen).
    prog = s.user_program;
    a_pos = glGetAttribLocation(prog, "a_pos");
    a_tex = glGetAttribLocation(prog, "a_tex");
    a_col = glGetAttribLocation(prog, "a_col");
    u_mvp = glGetUniformLocation(prog, "u_mvp");
    glUseProgram(prog);
  }
  glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp.m);

  // Client-geheugen: geen VBO gebonden; OpenCPN's eigen binding daarna terugzetten.
  GLint old_buf = 0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_buf);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  const GLsizei stride = K * sizeof(float);
  const float *base = v->data();
  if (a_pos >= 0) {
    glEnableVertexAttribArray(a_pos);
    glVertexAttribPointer(a_pos, 3, GL_FLOAT, GL_FALSE, stride, base);
  }
  if (a_tex >= 0) {
    glEnableVertexAttribArray(a_tex);
    glVertexAttribPointer(a_tex, 2, GL_FLOAT, GL_FALSE, stride, base + 3);
  }
  if (a_col >= 0) {
    glEnableVertexAttribArray(a_col);
    glVertexAttribPointer(a_col, 4, GL_FLOAT, GL_FALSE, stride, base + 5);
  }
  glDrawArrays(mode, 0, count);
  if (a_pos >= 0) glDisableVertexAttribArray(a_pos);
  if (a_tex >= 0) glDisableVertexAttribArray(a_tex);
  if (a_col >= 0) glDisableVertexAttribArray(a_col);
  glBindBuffer(GL_ARRAY_BUFFER, old_buf);
  glUseProgram(s.user_program);
}

void PushVertex(float x, float y, float z = 0.f) {
  State &s = S();
  const float v[K] = {x, y, z, s.texcoord[0], s.texcoord[1],
                      s.color[0], s.color[1], s.color[2], s.color[3]};
  s.verts.insert(s.verts.end(), v, v + K);
}

float ReadComp(const void *base, GLenum type, GLsizei stride, GLint size, GLint i,
               GLint c) {
  size_t elem = (type == GL_FLOAT) ? sizeof(float)
                : (type == GL_DOUBLE) ? sizeof(double)
                : (type == GL_INT) ? sizeof(GLint)
                : (type == GL_SHORT) ? sizeof(GLshort)
                                     : sizeof(GLubyte);
  size_t step = stride ? (size_t)stride : elem * size;
  const char *p = (const char *)base + step * i + elem * c;
  switch (type) {
    case GL_FLOAT:
      return *(const float *)p;
    case GL_DOUBLE:
      return (float)*(const double *)p;
    case GL_INT:
      return (float)*(const GLint *)p;
    case GL_SHORT:
      return (float)*(const GLshort *)p;
    default:
      return *(const GLubyte *)p / 255.f;  // GL_UNSIGNED_BYTE (kleuren)
  }
}

}  // namespace

extern "C" {

// ---------------------------------------------------------------- immediate mode
void glBegin(GLenum mode) {
  State &s = S();
  s.in_begin = true;
  s.begin_mode = mode;
  s.verts.clear();
}

void glEnd(void) {
  State &s = S();
  if (!s.in_begin) return;
  s.in_begin = false;
  Draw(s.begin_mode, s.verts);
  s.verts.clear();
}

void glVertex2f(GLfloat x, GLfloat y) { PushVertex(x, y); }
void glVertex2i(GLint x, GLint y) { PushVertex((float)x, (float)y); }
void glVertex2d(GLdouble x, GLdouble y) { PushVertex((float)x, (float)y); }
void glTexCoord2f(GLfloat s_, GLfloat t) {
  S().texcoord[0] = s_;
  S().texcoord[1] = t;
}
void glTexCoord2i(GLint s_, GLint t) { glTexCoord2f((float)s_, (float)t); }
void glTexCoord2d(GLdouble s_, GLdouble t) { glTexCoord2f((float)s_, (float)t); }
void glVertex3f(GLfloat x, GLfloat y, GLfloat z) { PushVertex(x, y, z); }
void glVertex3fv(const GLfloat *v) { PushVertex(v[0], v[1], v[2]); }
void glMultMatrixf(const GLfloat *m) {
  Mat4 t;
  for (int i = 0; i < 16; ++i) t.m[i] = m[i];
  MultTop(t);
}
void glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
  Mat4 m{};
  m.m[0] = (float)(2 * n / (r - l));
  m.m[5] = (float)(2 * n / (t - b));
  m.m[8] = (float)((r + l) / (r - l));
  m.m[9] = (float)((t + b) / (t - b));
  m.m[10] = (float)(-(f + n) / (f - n));
  m.m[11] = -1.f;
  m.m[14] = (float)(-2 * f * n / (f - n));
  MultTop(m);
}
void glPointSize(GLfloat size) { S().point_size = size; }
void ocpnios_glGetFloatv(GLenum pname, GLfloat *data) {
  State &s = S();
  if (pname == GL_MODELVIEW_MATRIX) {
    std::memcpy(data, s.modelview.back().m, sizeof(float) * 16);
  } else if (pname == GL_PROJECTION_MATRIX) {
    std::memcpy(data, s.projection.back().m, sizeof(float) * 16);
  } else {
    glGetFloatv(pname, data);
  }
}
void glMultMatrixd(const GLdouble *m) {
  Mat4 t;
  for (int i = 0; i < 16; ++i) t.m[i] = (float)m[i];
  MultTop(t);
}
void glPolygonMode(GLenum, GLenum) {}
void glTexImage1D(GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const void *) {}
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
  float *c = S().color;
  c[0] = r;
  c[1] = g;
  c[2] = b;
  c[3] = a;
}
void glColor3f(GLfloat r, GLfloat g, GLfloat b) { glColor4f(r, g, b, 1.f); }
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
  glColor4f(r / 255.f, g / 255.f, b / 255.f, a / 255.f);
}
void glColor3ub(GLubyte r, GLubyte g, GLubyte b) { glColor4ub(r, g, b, 255); }
void glColor3ubv(const GLubyte *v) { glColor4ub(v[0], v[1], v[2], 255); }

// ---------------------------------------------------------------- matrices
void glMatrixMode(GLenum mode) { S().matrix_mode = mode; }
void glLoadIdentity(void) { Top() = Identity(); }
void glPushMatrix(void) {
  State &s = S();
  auto &st = s.matrix_mode == GL_PROJECTION ? s.projection : s.modelview;
  st.push_back(st.back());
}
void glPopMatrix(void) {
  State &s = S();
  auto &st = s.matrix_mode == GL_PROJECTION ? s.projection : s.modelview;
  if (st.size() > 1) st.pop_back();
}
void glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
  Mat4 t = Identity();
  t.m[12] = x;
  t.m[13] = y;
  t.m[14] = z;
  MultTop(t);
}
void glTranslated(GLdouble x, GLdouble y, GLdouble z) {
  glTranslatef((float)x, (float)y, (float)z);
}
void glScalef(GLfloat x, GLfloat y, GLfloat z) {
  Mat4 t = Identity();
  t.m[0] = x;
  t.m[5] = y;
  t.m[10] = z;
  MultTop(t);
}
void glScaled(GLdouble x, GLdouble y, GLdouble z) {
  glScalef((float)x, (float)y, (float)z);
}
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
  float len = std::sqrt(x * x + y * y + z * z);
  if (len == 0) return;
  x /= len;
  y /= len;
  z /= len;
  float a = angle * (float)M_PI / 180.f;
  float c = std::cos(a), s = std::sin(a), ic = 1 - c;
  Mat4 r = Identity();
  r.m[0] = x * x * ic + c;
  r.m[1] = y * x * ic + z * s;
  r.m[2] = x * z * ic - y * s;
  r.m[4] = x * y * ic - z * s;
  r.m[5] = y * y * ic + c;
  r.m[6] = y * z * ic + x * s;
  r.m[8] = x * z * ic + y * s;
  r.m[9] = y * z * ic - x * s;
  r.m[10] = z * z * ic + c;
  MultTop(r);
}
void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z) {
  glRotatef((float)angle, (float)x, (float)y, (float)z);
}
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n,
             GLdouble f) {
  Mat4 o = Identity();
  o.m[0] = (float)(2 / (r - l));
  o.m[5] = (float)(2 / (t - b));
  o.m[10] = (float)(-2 / (f - n));
  o.m[12] = (float)(-(r + l) / (r - l));
  o.m[13] = (float)(-(t + b) / (t - b));
  o.m[14] = (float)(-(f + n) / (f - n));
  MultTop(o);
}

// ---------------------------------------------------------------- attributen
void glPushAttrib(GLbitfield mask) {
  State &s = S();
  State::Attrib a{};
  a.mask = mask;
  std::memcpy(a.color, s.color, sizeof(a.color));
  a.texture_enabled = s.texture_enabled;
  a.texenv_replace = s.texenv_replace;
  a.blend = glIsEnabled(GL_BLEND);
  glGetIntegerv(GL_BLEND_SRC_RGB, &a.blend_src);
  glGetIntegerv(GL_BLEND_DST_RGB, &a.blend_dst);
  glGetFloatv(GL_LINE_WIDTH, &a.line_width);
  s.attrib_stack.push_back(a);
}

void glPopAttrib(void) {
  State &s = S();
  if (s.attrib_stack.empty()) return;
  State::Attrib a = s.attrib_stack.back();
  s.attrib_stack.pop_back();
  std::memcpy(s.color, a.color, sizeof(a.color));
  s.texture_enabled = a.texture_enabled;
  s.texenv_replace = a.texenv_replace;
  if (a.blend)
    glEnable(GL_BLEND);
  else
    glDisable(GL_BLEND);
  glBlendFunc(a.blend_src, a.blend_dst);
  glLineWidth(a.line_width);
}

void glEnableClientState(GLenum array) {
  if (array == GL_VERTEX_ARRAY) S().va_enabled = true;
  if (array == GL_COLOR_ARRAY) S().ca_enabled = true;
}
void glDisableClientState(GLenum array) {
  if (array == GL_VERTEX_ARRAY) S().va_enabled = false;
  if (array == GL_COLOR_ARRAY) S().ca_enabled = false;
}
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) {
  State &s = S();
  s.va_size = size;
  s.va_type = type;
  s.va_stride = stride;
  s.va_ptr = ptr;
}
void glColorPointer(GLint size, GLenum type, GLsizei stride, const void *ptr) {
  State &s = S();
  s.ca_size = size;
  s.ca_type = type;
  s.ca_stride = stride;
  s.ca_ptr = ptr;
}
void glLineStipple(GLint, GLushort) {}  // stippellijnen worden doorgetrokken lijnen
void glTexEnvi(GLenum target, GLenum pname, GLint param) {
  if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_MODE)
    S().texenv_replace = (param == GL_REPLACE);
}

// ---------------------------------------------------------------- ES-functies met wrapper
void ocpnios_glEnable(GLenum cap) {
  switch (cap) {
    case GL_TEXTURE_2D:
      S().texture_enabled = true;
      return;
    case GL_LINE_STIPPLE:
    case GL_LINE_SMOOTH:
    case GL_POLYGON_SMOOTH:
    case GL_COLOR_MATERIAL:
      return;  // bestaat niet in ES; geen gevolg voor het beeld van belang
    default:
      glEnable(cap);
  }
}

void ocpnios_glDisable(GLenum cap) {
  switch (cap) {
    case GL_TEXTURE_2D:
      S().texture_enabled = false;
      return;
    case GL_LINE_STIPPLE:
    case GL_LINE_SMOOTH:
    case GL_POLYGON_SMOOTH:
    case GL_COLOR_MATERIAL:
      return;
    default:
      glDisable(cap);
  }
}

void ocpnios_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
  State &s = S();
  if (!s.va_enabled || !s.va_ptr) {
    glDrawArrays(mode, first, count);
    return;
  }
  std::vector<float> v;
  v.reserve((size_t)count * K);
  for (GLint i = first; i < first + count; ++i) {
    float x = ReadComp(s.va_ptr, s.va_type, s.va_stride, s.va_size, i, 0);
    float y = ReadComp(s.va_ptr, s.va_type, s.va_stride, s.va_size, i, 1);
    float z = s.va_size > 2 ? ReadComp(s.va_ptr, s.va_type, s.va_stride, s.va_size, i, 2) : 0.f;
    float c[4] = {s.color[0], s.color[1], s.color[2], s.color[3]};
    if (s.ca_enabled && s.ca_ptr)
      for (int k = 0; k < s.ca_size && k < 4; ++k)
        c[k] = ReadComp(s.ca_ptr, s.ca_type, s.ca_stride, s.ca_size, i, k);
    const float vv[K] = {x, y, z, s.texcoord[0], s.texcoord[1], c[0], c[1], c[2], c[3]};
    v.insert(v.end(), vv, vv + K);
  }
  Draw(mode, v);
}

GLuint glGenLists(GLsizei range) {
  State &s = S();
  GLuint first = s.next_list;
  s.next_list += (GLuint)(range > 0 ? range : 1);
  return first;
}
void glNewList(GLuint list, GLenum mode) {
  State &s = S();
  s.lists[list].clear();
  s.recording = list;
  s.record_execute = (mode == GL_COMPILE_AND_EXECUTE);
}
void glEndList(void) { S().recording = 0; }
void glCallList(GLuint list) {
  State &s = S();
  auto it = s.lists.find(list);
  if (it == s.lists.end()) return;
  for (const auto &cmd : it->second) DrawNow(cmd.mode, cmd.verts);
}
void glDeleteLists(GLuint list, GLsizei range) {
  for (GLsizei i = 0; i < range; ++i) S().lists.erase(list + (GLuint)i);
}

void ocpnios_glBindTexture(GLenum target, GLuint texture) {
  if (target == GL_TEXTURE_2D) S().bound_texture = texture;
  glBindTexture(target, texture);
}

void ocpnios_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                          GLsizei height, GLint border, GLenum format, GLenum type,
                          const void *pixels) {
  if (target == GL_TEXTURE_2D && level == 0) S().tex_format[S().bound_texture] = format;
  glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
}

void ocpnios_glUseProgram(GLuint program) {
  S().user_program = program;
  glUseProgram(program);
}

void ocpnios_glcompat_begin_overlay(void) {
  State &s = S();
  GLint vp[4] = {0, 0, 1, 1};
  glGetIntegerv(GL_VIEWPORT, vp);
  s.projection.assign(1, Identity());
  s.modelview.assign(1, Identity());
  s.matrix_mode = GL_PROJECTION;
  glOrtho(0, vp[2], vp[3], 0, -1, 1);
  s.matrix_mode = GL_MODELVIEW;
  s.texture_enabled = false;
  s.texenv_replace = false;
  s.user_program = 0;
  s.attrib_stack.clear();
  s.color[0] = s.color[1] = s.color[2] = s.color[3] = 1.f;
}

void *ocpnios_glcompat_getproc(const char *name) {
  if (name && std::strcmp(name, "glUseProgram") == 0)
    return (void *)&ocpnios_glUseProgram;
  return dlsym(RTLD_DEFAULT, name);
}

}  // extern "C"
