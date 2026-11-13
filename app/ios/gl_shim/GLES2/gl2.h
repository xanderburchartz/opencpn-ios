// opencpn-ios: plugins vragen desktop-GL- of GLES2-headers die iOS niet heeft.
// Zelfde keuze als de OpenCPN-kern op iOS (en Android): Qt's OpenGL ES-header plus de
// uitgeklede gl_private.h uit libs/gl_headers/android.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <qopengl.h>
#include <GL/gl_private.h>
