// o-charts_pi op iOS: in-app vervanging van de FIFO's naar oexserverd (zie
// ocharts_ios_pipe.h). Hoort bij de plugin (statisch gelinkt in de app).
// SPDX-License-Identifier: GPL-2.0-or-later

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <map>
#include <mutex>
#include <string>

#include "ocharts_ios_backend.h"
#include "ocharts_ios_glue.h"

int OcIosPipeOpen(const char* path, int flags, ...);
int OcIosPipeMkfifo(const char* path, mode_t mode);
ssize_t OcIosPipeWrite(int fd, const void* buf, size_t n);
ssize_t OcIosPipeRead(int fd, void* buf, size_t n);
int OcIosPipeClose(int fd);
int OcIosPipeUnlink(const char* path);

namespace {

// Zelfde waarde als PUBLIC in Osenc.h en oernc_inStream.h.
const char kPublic[] = "/tmp/OCPN_PIPEX";

// Nep-descriptors ver boven wat het systeem uitdeelt, zodat ze nooit samenvallen.
const int kFdBase = 0x3C000000;

struct Fd {
  bool is_public = false;
  ocios_stream* stream = nullptr;  // alleen private
};

std::mutex g_m;
int g_next = kFdBase;
std::map<int, Fd> g_fds;
// Geregistreerde private namen (mkfifo) en het antwoord dat erop wacht.
struct Pending {
  bool answered = false;
  ocios_stream* stream = nullptr;
};
std::map<std::string, Pending> g_names;

bool IsOurs(int fd) { return fd >= kFdBase; }

}  // namespace

int OcIosPipeOpen(const char* path, int flags, ...) {
  if (path && strcmp(path, kPublic) == 0) {
    // De "server" is bereikbaar als de ontsleuteling draait.
    if (!OcIosBackendUsable()) {
      errno = ENXIO;
      return -1;
    }
    std::lock_guard<std::mutex> l(g_m);
    int fd = g_next++;
    g_fds[fd].is_public = true;
    return fd;
  }
  {
    std::lock_guard<std::mutex> l(g_m);
    auto it = path ? g_names.find(path) : g_names.end();
    if (it != g_names.end()) {
      if (!it->second.answered || !it->second.stream) {
        errno = ENOENT;  // geen (geslaagd) verzoek op deze naam
        return -1;
      }
      int fd = g_next++;
      g_fds[fd].stream = it->second.stream;
      it->second.stream = nullptr;
      it->second.answered = false;
      return fd;
    }
  }
  int mode = 0;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
  }
  return ::open(path, flags, mode);
}

int OcIosPipeMkfifo(const char* path, mode_t) {
  if (!path) {
    errno = EINVAL;
    return -1;
  }
  std::lock_guard<std::mutex> l(g_m);
  g_names[path] = Pending{};
  return 0;
}

ssize_t OcIosPipeWrite(int fd, const void* buf, size_t n) {
  if (!IsOurs(fd)) return ::write(fd, buf, n);
  {
    std::lock_guard<std::mutex> l(g_m);
    auto it = g_fds.find(fd);
    if (it == g_fds.end() || !it->second.is_public) {
      errno = EBADF;
      return -1;
    }
  }
  // fifo_msg en rnc_fifo_msg: 1 byte opdracht, dan fifo_name[256].
  if (n < 2) {
    errno = EINVAL;
    return -1;
  }
  const char* name = static_cast<const char*>(buf) + 1;
  std::string fifo(name, strnlen(name, 256));
  ocios_stream* s = ocios_request(buf, n);
  std::lock_guard<std::mutex> l(g_m);
  Pending& p = g_names[fifo];
  if (p.stream) ocios_stream_close(p.stream);
  p.stream = s;
  p.answered = true;
  if (!s) {
    errno = EPIPE;
    return -1;
  }
  return static_cast<ssize_t>(n);
}

ssize_t OcIosPipeRead(int fd, void* buf, size_t n) {
  if (!IsOurs(fd)) return ::read(fd, buf, n);
  ocios_stream* s = nullptr;
  {
    std::lock_guard<std::mutex> l(g_m);
    auto it = g_fds.find(fd);
    if (it == g_fds.end() || it->second.is_public) {
      errno = EBADF;
      return -1;
    }
    s = it->second.stream;
  }
  long r = ocios_stream_read(s, buf, n);
  // De plugin telt de uitkomst op in een size_t; een fout als einde van de stroom
  // doorgeven (0), zoals een FIFO waarvan de schrijver weg is.
  return r > 0 ? static_cast<ssize_t>(r) : 0;
}

int OcIosPipeClose(int fd) {
  if (!IsOurs(fd)) return ::close(fd);
  ocios_stream* s = nullptr;
  {
    std::lock_guard<std::mutex> l(g_m);
    auto it = g_fds.find(fd);
    if (it == g_fds.end()) {
      errno = EBADF;
      return -1;
    }
    s = it->second.stream;
    g_fds.erase(it);
  }
  if (s) ocios_stream_close(s);
  return 0;
}

int OcIosPipeUnlink(const char* path) {
  if (path) {
    ocios_stream* s = nullptr;
    bool ours = false;
    {
      std::lock_guard<std::mutex> l(g_m);
      auto it = g_names.find(path);
      if (it != g_names.end()) {
        ours = true;
        s = it->second.stream;
        g_names.erase(it);
      }
    }
    if (s) ocios_stream_close(s);
    // CreateTempFileName maakte ook een echt (leeg) bestand met deze naam; dat mag weg.
    if (ours) {
      ::unlink(path);
      return 0;
    }
  }
  return ::unlink(path);
}
