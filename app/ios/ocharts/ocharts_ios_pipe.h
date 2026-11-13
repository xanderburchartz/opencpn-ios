// o-charts_pi op iOS: de FIFO-aanroepen van de plugin binnen de app afhandelen.
//
// Alleen ingesloten in het Linux/macOS-deel van Osenc.cpp en oernc_inStream.cpp (zie
// patches/o-charts_pi/0001-ios.patch), en aan het eind van dat deel weer opgeheven met
// ocharts_ios_pipe_end.h. Daartussen gaan open/mkfifo/write/read/close/unlink naar
// ocharts_ios_pipe.cpp:
//   - open(PUBLIC) lukt alleen als de ontsleuteling draait (anders ENXIO, zoals wanneer
//     oexserverd niet loopt);
//   - write() op de publieke "FIFO" geeft het verzoek ongewijzigd aan ocios_request();
//   - open/read/close op de private "FIFO" lezen de antwoordstroom (ocios_stream_read).
// Echte bestandsdescriptors en paden gaan ongewijzigd naar het systeem.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int OcIosPipeOpen(const char* path, int flags, ...);
int OcIosPipeMkfifo(const char* path, mode_t mode);
ssize_t OcIosPipeWrite(int fd, const void* buf, size_t n);
ssize_t OcIosPipeRead(int fd, void* buf, size_t n);
int OcIosPipeClose(int fd);
int OcIosPipeUnlink(const char* path);
// oernc_inStream.cpp roept unlink() aan met een wxString.
template <class S, class = decltype(static_cast<const char*>(
                                  ((const S*)nullptr)->mb_str()))>
int OcIosPipeUnlink(const S& path) {
  return OcIosPipeUnlink(static_cast<const char*>(path.mb_str()));
}

#define open(...) OcIosPipeOpen(__VA_ARGS__)
#define mkfifo(...) OcIosPipeMkfifo(__VA_ARGS__)
#define write(...) OcIosPipeWrite(__VA_ARGS__)
#define read(...) OcIosPipeRead(__VA_ARGS__)
#define close(...) OcIosPipeClose(__VA_ARGS__)
#define unlink(...) OcIosPipeUnlink(__VA_ARGS__)
