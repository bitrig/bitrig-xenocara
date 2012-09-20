/**************************************************************************
 * 
 * Copyright 2006 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

#ifndef INTEL_FBO_H
#define INTEL_FBO_H

#include <stdbool.h>
#include "main/formats.h"
#include "intel_screen.h"

struct intel_context;
struct intel_texture_image;

/**
 * Intel renderbuffer, derived from gl_renderbuffer.
 */
struct intel_renderbuffer
{
   struct gl_renderbuffer Base;
   struct intel_region *region;

   /** Only used by depth renderbuffers for which HiZ is enabled. */
   struct intel_region *hiz_region;

   /**
    * \name Packed depth/stencil unwrappers
    *
    * If the intel_context is using separate stencil and this renderbuffer has
    * a a packed depth/stencil format, then wrapped_depth and wrapped_stencil
    * are the real renderbuffers.
    */
   struct gl_renderbuffer *wrapped_depth;
   struct gl_renderbuffer *wrapped_stencil;

   /** \} */

   GLuint draw_x, draw_y; /**< Offset of drawing within the region */
};


/**
 * gl_renderbuffer is a base class which we subclass.  The Class field
 * is used for simple run-time type checking.
 */
#define INTEL_RB_CLASS 0x12345678


/**
 * Return a gl_renderbuffer ptr casted to intel_renderbuffer.
 * NULL will be returned if the rb isn't really an intel_renderbuffer.
 * This is determined by checking the ClassID.
 */
static INLINE struct intel_renderbuffer *
intel_renderbuffer(struct gl_renderbuffer *rb)
{
   struct intel_renderbuffer *irb = (struct intel_renderbuffer *) rb;
   if (irb && irb->Base.ClassID == INTEL_RB_CLASS) {
      /*_mesa_warning(NULL, "Returning non-intel Rb\n");*/
      return irb;
   }
   else
      return NULL;
}


/**
 * \brief Return the framebuffer attachment specified by attIndex.
 *
 * If the framebuffer lacks the specified attachment, then return null.
 *
 * If the attached renderbuffer is a wrapper, then return wrapped
 * renderbuffer.
 */
static INLINE struct intel_renderbuffer *
intel_get_renderbuffer(struct gl_framebuffer *fb, gl_buffer_index attIndex)
{
   struct gl_renderbuffer *rb;
   struct intel_renderbuffer *irb;

   /* XXX: Who passes -1 to intel_get_renderbuffer? */
   if (attIndex < 0)
      return NULL;

   rb = fb->Attachment[attIndex].Renderbuffer;
   if (!rb)
      return NULL;

   irb = intel_renderbuffer(rb);
   if (!irb)
      return NULL;

   switch (attIndex) {
   case BUFFER_DEPTH:
      if (irb->wrapped_depth) {
	 irb = intel_renderbuffer(irb->wrapped_depth);
      }
      break;
   case BUFFER_STENCIL:
      if (irb->wrapped_stencil) {
	 irb = intel_renderbuffer(irb->wrapped_stencil);
      }
      break;
   default:
      break;
   }

   return irb;
}

/**
 * If the framebuffer has a depth buffer attached, then return its HiZ region.
 * The HiZ region may be null.
 */
static INLINE struct intel_region*
intel_framebuffer_get_hiz_region(struct gl_framebuffer *fb)
{
   struct intel_renderbuffer *rb = NULL;
   if (fb)
      rb = intel_get_renderbuffer(fb, BUFFER_DEPTH);

   if (rb)
      return rb->hiz_region;
   else
      return NULL;
}

static INLINE bool
intel_framebuffer_has_hiz(struct gl_framebuffer *fb)
{
   return intel_framebuffer_get_hiz_region(fb) != NULL;
}


extern void
intel_renderbuffer_set_region(struct intel_context *intel,
			      struct intel_renderbuffer *irb,
			      struct intel_region *region);

extern void
intel_renderbuffer_set_hiz_region(struct intel_context *intel,
				  struct intel_renderbuffer *rb,
				  struct intel_region *region);


extern struct intel_renderbuffer *
intel_create_renderbuffer(gl_format format);

struct gl_renderbuffer*
intel_create_wrapped_renderbuffer(struct gl_context * ctx,
				  int width, int height,
				  gl_format format);

GLboolean
intel_alloc_renderbuffer_storage(struct gl_context * ctx,
				 struct gl_renderbuffer *rb,
                                 GLenum internalFormat,
                                 GLuint width, GLuint height);

extern void
intel_fbo_init(struct intel_context *intel);


extern void
intel_flip_renderbuffers(struct gl_framebuffer *fb);

void
intel_renderbuffer_set_draw_offset(struct intel_renderbuffer *irb,
				   struct intel_texture_image *intel_image,
				   int zoffset);

uint32_t
intel_renderbuffer_tile_offsets(struct intel_renderbuffer *irb,
				uint32_t *tile_x,
				uint32_t *tile_y);

static INLINE struct intel_region *
intel_get_rb_region(struct gl_framebuffer *fb, GLuint attIndex)
{
   struct intel_renderbuffer *irb = intel_get_renderbuffer(fb, attIndex);
   if (irb)
      return irb->region;
   else
      return NULL;
}

#endif /* INTEL_FBO_H */
