/*
 * Mesa 3-D graphics library
 * Version:  6.5
 *
 * Copyright (C) 1999-2005  Brian Paul   All Rights Reserved.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#include "glxheader.h"
#include "GL/xmesa.h"
#include "xmesaP.h"
#include "imports.h"
#include "renderbuffer.h"


static void
xmesa_delete_renderbuffer(struct gl_renderbuffer *rb)
{
   /* XXX Note: the ximage or Pixmap attached to this renderbuffer
    * should probably get freed here, but that's currently done in
    * XMesaDestroyBuffer().
    */
   _mesa_free(rb);
}


/**
 * Reallocate renderbuffer storage for front color buffer.
 */
static GLboolean
xmesa_alloc_front_storage(GLcontext *ctx, struct gl_renderbuffer *rb,
                          GLenum internalFormat, GLuint width, GLuint height)
{
   struct xmesa_renderbuffer *xrb = xmesa_renderbuffer(rb);

   /* just clear these to be sure we don't accidentally use them */
   xrb->origin1 = NULL;
   xrb->origin2 = NULL;
   xrb->origin4 = NULL;

   /* for the FLIP macro: */
   xrb->bottom = height - 1;

   rb->Width = width;
   rb->Height = height;
   rb->InternalFormat = internalFormat;

   return GL_TRUE;
}


/**
 * Reallocate renderbuffer storage for back color buffer.
 * XXX we should resize the back pixmap/ximage here.
 */
static GLboolean
xmesa_alloc_back_storage(GLcontext *ctx, struct gl_renderbuffer *rb,
                         GLenum internalFormat, GLuint width, GLuint height)
{
   struct xmesa_renderbuffer *xrb = xmesa_renderbuffer(rb);

   /* same as front buffer */
   (void) xmesa_alloc_front_storage(ctx, rb, internalFormat, width, height);

   /* plus... */
   if (xrb->ximage) {
      /* Needed by PIXELADDR1 macro */
      xrb->width1 = xrb->ximage->bytes_per_line;
      xrb->origin1 = (GLubyte *) xrb->ximage->data + xrb->width1 * (height - 1);

      /* Needed by PIXELADDR2 macro */
      xrb->width2 = xrb->ximage->bytes_per_line / 2;
      xrb->origin2 = (GLushort *) xrb->ximage->data + xrb->width2 * (height - 1);

      /* Needed by PIXELADDR3 macro */
      xrb->width3 = xrb->ximage->bytes_per_line;
      xrb->origin3 = (GLubyte *) xrb->ximage->data + xrb->width3 * (height - 1);

      /* Needed by PIXELADDR4 macro */
      xrb->width4 = xrb->ximage->width;
      xrb->origin4 = (GLuint *) xrb->ximage->data + xrb->width4 * (height - 1);
   }
   else {
      /* this assertion will fail if we happend to run out of memory */
      /*assert(xrb->pixmap);*/
   }

   return GL_TRUE;
}


struct xmesa_renderbuffer *
xmesa_new_renderbuffer(GLcontext *ctx, GLuint name, const GLvisual *visual,
                       GLboolean backBuffer)
{
   struct xmesa_renderbuffer *xrb = CALLOC_STRUCT(xmesa_renderbuffer);
   if (xrb) {
      GLuint name = 0;
      _mesa_init_renderbuffer(&xrb->Base, name);

      xrb->Base.Delete = xmesa_delete_renderbuffer;
      if (backBuffer)
         xrb->Base.AllocStorage = xmesa_alloc_back_storage;
      else
         xrb->Base.AllocStorage = xmesa_alloc_front_storage;

      if (visual->rgbMode) {
         xrb->Base.InternalFormat = GL_RGBA;
         xrb->Base._BaseFormat = GL_RGBA;
         xrb->Base.DataType = GL_UNSIGNED_BYTE;
         xrb->Base.RedBits = visual->redBits;
         xrb->Base.GreenBits = visual->greenBits;
         xrb->Base.BlueBits = visual->blueBits;
         xrb->Base.AlphaBits = visual->alphaBits;
      }
      else {
         xrb->Base.InternalFormat = GL_COLOR_INDEX;
         xrb->Base._BaseFormat = GL_COLOR_INDEX;
         xrb->Base.DataType = GL_UNSIGNED_INT;
         xrb->Base.IndexBits = visual->indexBits;
      }
      /* only need to set Red/Green/EtcBits fields for user-created RBs */
   }
   return xrb;
}




