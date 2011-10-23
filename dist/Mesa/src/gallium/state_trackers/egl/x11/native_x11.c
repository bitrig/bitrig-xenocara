/*
 * Mesa 3-D graphics library
 * Version:  7.8
 *
 * Copyright (C) 2009-2010 Chia-I Wu <olv@0xlab.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/u_string.h"
#include "egllog.h"

#include "native_x11.h"

static struct native_display *
native_create_display(void *dpy, struct native_event_handler *event_handler,
                      void *user_data)
{
   struct native_display *ndpy = NULL;
   boolean force_sw;

   force_sw = debug_get_bool_option("EGL_SOFTWARE", FALSE);
   if (!force_sw) {
      ndpy = x11_create_dri2_display((Display *) dpy,
            event_handler, user_data);
   }

   if (!ndpy) {
      EGLint level = (force_sw) ? _EGL_INFO : _EGL_WARNING;

      _eglLog(level, "use software fallback");
      ndpy = x11_create_ximage_display((Display *) dpy,
            event_handler, user_data);
   }

   return ndpy;
}

static const struct native_platform x11_platform = {
   "X11", /* name */
   native_create_display
};

const struct native_platform *
native_get_x11_platform(void)
{
   return &x11_platform;
}
