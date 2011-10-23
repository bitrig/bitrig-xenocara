/*
 * SGI FREE SOFTWARE LICENSE B (Version 2.0, Sept. 18, 2008)
 * Copyright (C) 1991-2000 Silicon Graphics, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice including the dates of first publication and
 * either this permission notice or a reference to
 * http://oss.sgi.com/projects/FreeB/
 * shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * SILICON GRAPHICS, INC. BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of Silicon Graphics, Inc.
 * shall not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization from
 * Silicon Graphics, Inc.
 */

/**
 * \file glxcurrent.c
 * Client-side GLX interface for current context management.
 */

#ifdef PTHREADS
#include <pthread.h>
#endif

#include "glxclient.h"
#ifdef GLX_USE_APPLEGL
#include <stdlib.h>

#include "apple_glx.h"
#include "apple_glx_context.h"
#else
#include "glapi.h"
#endif

/*
** We setup some dummy structures here so that the API can be used
** even if no context is current.
*/

static GLubyte dummyBuffer[__GLX_BUFFER_LIMIT_SIZE];
static struct glx_context_vtable dummyVtable;
/*
** Dummy context used by small commands when there is no current context.
** All the
** gl and glx entry points are designed to operate as nop's when using
** the dummy context structure.
*/
struct glx_context dummyContext = {
   &dummyBuffer[0],
   &dummyBuffer[0],
   &dummyBuffer[0],
   &dummyBuffer[__GLX_BUFFER_LIMIT_SIZE],
   sizeof(dummyBuffer),
   &dummyVtable
};

/*
 * Current context management and locking
 */

#if defined( PTHREADS )

_X_HIDDEN pthread_mutex_t __glXmutex = PTHREAD_MUTEX_INITIALIZER;

# if defined( GLX_USE_TLS )

/**
 * Per-thread GLX context pointer.
 *
 * \c __glXSetCurrentContext is written is such a way that this pointer can
 * \b never be \c NULL.  This is important!  Because of this
 * \c __glXGetCurrentContext can be implemented as trivial macro.
 */
__thread void *__glX_tls_Context __attribute__ ((tls_model("initial-exec")))
   = &dummyContext;

_X_HIDDEN void
__glXSetCurrentContext(struct glx_context * c)
{
   __glX_tls_Context = (c != NULL) ? c : &dummyContext;
}

# else

static pthread_once_t once_control = PTHREAD_ONCE_INIT;

/**
 * Per-thread data key.
 *
 * Once \c init_thread_data has been called, the per-thread data key will
 * take a value of \c NULL.  As each new thread is created the default
 * value, in that thread, will be \c NULL.
 */
static pthread_key_t ContextTSD;

/**
 * Initialize the per-thread data key.
 *
 * This function is called \b exactly once per-process (not per-thread!) to
 * initialize the per-thread data key.  This is ideally done using the
 * \c pthread_once mechanism.
 */
static void
init_thread_data(void)
{
   if (pthread_key_create(&ContextTSD, NULL) != 0) {
      perror("pthread_key_create");
      exit(-1);
   }
}

_X_HIDDEN void
__glXSetCurrentContext(struct glx_context * c)
{
   pthread_once(&once_control, init_thread_data);
   pthread_setspecific(ContextTSD, c);
}

_X_HIDDEN struct glx_context *
__glXGetCurrentContext(void)
{
   void *v;

   pthread_once(&once_control, init_thread_data);

   v = pthread_getspecific(ContextTSD);
   return (v == NULL) ? &dummyContext : (struct glx_context *) v;
}

# endif /* defined( GLX_USE_TLS ) */

#elif defined( THREADS )

#error Unknown threading method specified.

#else

/* not thread safe */
_X_HIDDEN struct glx_context *__glXcurrentContext = &dummyContext;

#endif


_X_HIDDEN void
__glXSetCurrentContextNull(void)
{
   __glXSetCurrentContext(&dummyContext);
#if defined(GLX_DIRECT_RENDERING) && !defined(GLX_USE_APPLEGL)
   _glapi_set_dispatch(NULL);   /* no-op functions */
   _glapi_set_context(NULL);
#endif
}

_X_EXPORT GLXContext
glXGetCurrentContext(void)
{
   struct glx_context *cx = __glXGetCurrentContext();

   if (cx == &dummyContext) {
      return NULL;
   }
   else {
      return (GLXContext) cx;
   }
}

_X_EXPORT GLXDrawable
glXGetCurrentDrawable(void)
{
   struct glx_context *gc = __glXGetCurrentContext();
   return gc->currentDrawable;
}

static void
__glXGenerateError(Display * dpy, struct glx_context *gc, XID resource,
                   BYTE errorCode, CARD16 minorCode)
{
   xError error;

   error.errorCode = errorCode;
   error.resourceID = resource;
   error.sequenceNumber = dpy->request;
   error.type = X_Error;
   error.majorCode = gc->majorOpcode;
   error.minorCode = minorCode;
   _XError(dpy, &error);
}

/**
 * Make a particular context current.
 *
 * \note This is in this file so that it can access dummyContext.
 */
static Bool
MakeContextCurrent(Display * dpy, GLXDrawable draw,
                   GLXDrawable read, GLXContext gc_user)
{
   struct glx_context *gc = (struct glx_context *) gc_user;
   struct glx_context *oldGC = __glXGetCurrentContext();
   int ret = Success;

   /* Make sure that the new context has a nonzero ID.  In the request,
    * a zero context ID is used only to mean that we bind to no current
    * context.
    */
   if ((gc != NULL) && (gc->xid == None)) {
      return GL_FALSE;
   }

   if (gc == NULL && (draw != None || read != None)) {
      __glXGenerateError(dpy, gc, (draw != None) ? draw : read,
                         BadMatch, X_GLXMakeContextCurrent);
      return False;
   }
   if (gc != NULL && (draw == None || read == None)) {
      __glXGenerateError(dpy, gc, None, BadMatch, X_GLXMakeContextCurrent);
      return False;
   }

#ifndef GLX_USE_APPLEGL
   _glapi_check_multithread();

   if (gc != NULL && gc->thread_id != 0 && gc->thread_id != _glthread_GetID()) {
      __glXGenerateError(dpy, gc, gc->xid,
                         BadAccess, X_GLXMakeContextCurrent);
      return False;
   }
#endif

   if (oldGC == gc &&
       gc->currentDrawable == draw && gc->currentReadable == read)
      return True;

   if (oldGC != &dummyContext) {
      oldGC->vtable->unbind(oldGC, gc);
      oldGC->currentDpy = 0;
      oldGC->currentDrawable = None;
      oldGC->currentReadable = None;
      oldGC->thread_id = 0;
   }

   if (gc) {
      gc->currentDpy = dpy;
      gc->currentDrawable = draw;
      gc->currentReadable = read;
#ifndef GLX_USE_APPLEGL
      gc->thread_id = _glthread_GetID();
#endif
      __glXSetCurrentContext(gc);
      ret = gc->vtable->bind(gc, oldGC, draw, read);
   } else {
      __glXSetCurrentContextNull();
   }

   if (oldGC != &dummyContext && oldGC->xid == None && oldGC != gc) {
      /* We are switching away from a context that was
       * previously destroyed, so we need to free the memory
       * for the old handle. */
      oldGC->vtable->destroy(oldGC);
   }

   if (ret) {
      __glXGenerateError(dpy, gc, None, ret, X_GLXMakeContextCurrent);
      return GL_FALSE;
   }

   return GL_TRUE;
}


_X_EXPORT Bool
glXMakeCurrent(Display * dpy, GLXDrawable draw, GLXContext gc)
{
   return MakeContextCurrent(dpy, draw, draw, gc);
}

_X_EXPORT
GLX_ALIAS(Bool, glXMakeCurrentReadSGI,
          (Display * dpy, GLXDrawable d, GLXDrawable r, GLXContext ctx),
          (dpy, d, r, ctx), MakeContextCurrent)

_X_EXPORT
GLX_ALIAS(Bool, glXMakeContextCurrent,
          (Display * dpy, GLXDrawable d, GLXDrawable r,
           GLXContext ctx), (dpy, d, r, ctx), MakeContextCurrent)
