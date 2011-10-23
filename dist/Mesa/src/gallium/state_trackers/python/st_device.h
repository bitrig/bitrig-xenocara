/**************************************************************************
 * 
 * Copyright 2008 Tungsten Graphics, Inc., Cedar Park, Texas.
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


#ifndef ST_DEVICE_H_
#define ST_DEVICE_H_


#include "pipe/p_state.h"

struct cso_context;
struct pipe_screen;
struct pipe_context;
struct st_winsys; 


struct st_surface
{
   struct pipe_resource *texture;
   unsigned level;
   unsigned layer;
};


struct st_context
{
   struct st_device *st_dev;
   
   struct pipe_context *pipe;
   
   struct cso_context *cso;
   
   void *vs;
   void *fs;
   void *gs;

   struct pipe_resource *default_texture;
   struct pipe_sampler_view *fragment_sampler_views[PIPE_MAX_SAMPLERS];
   struct pipe_sampler_view *vertex_sampler_views[PIPE_MAX_VERTEX_SAMPLERS];
   
   unsigned num_vertex_buffers;
   struct pipe_vertex_buffer vertex_buffers[PIPE_MAX_ATTRIBS];
   
   unsigned num_vertex_elements;
   struct pipe_vertex_element vertex_elements[PIPE_MAX_ATTRIBS];
   
   struct pipe_framebuffer_state framebuffer;
};


struct st_device
{
   /* FIXME: we also need to refcount for textures and surfaces... */
   struct pipe_reference reference;

   struct pipe_screen *screen;
};


static INLINE struct pipe_surface *
st_pipe_surface(struct pipe_context *pipe, struct st_surface *surface, unsigned usage) 
{
   struct pipe_resource *texture = surface->texture;
   struct pipe_surface surf_tmpl;
   memset(&surf_tmpl, 0, sizeof(surf_tmpl));
   surf_tmpl.format = texture->format;
   surf_tmpl.usage = usage;
   surf_tmpl.u.tex.level = surface->level;
   surf_tmpl.u.tex.first_layer = surface->layer;
   surf_tmpl.u.tex.last_layer = surface->layer;
   return pipe->create_surface(pipe, texture, &surf_tmpl);
}

struct st_context *
st_context_create(struct st_device *st_dev);

void
st_context_destroy(struct st_context *st_ctx);

struct st_device *
st_device_create(boolean hardware);

void
st_device_destroy(struct st_device *st_dev);


#endif /* ST_DEVICE_H_ */
