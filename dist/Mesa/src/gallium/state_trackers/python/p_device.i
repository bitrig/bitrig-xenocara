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

/**
 * @file
 * SWIG interface definion for Gallium types.
 *
 * @author Jose Fonseca <jrfonseca@tungstengraphics.com>
 */


%nodefaultctor st_device;
%nodefaultdtor st_device;


struct st_device {
};

%newobject st_device::texture_create;
%newobject st_device::context_create;
%newobject st_device::buffer_create;

%extend st_device {
   
   st_device(int hardware = 1) {
      return st_device_create(hardware ? TRUE : FALSE);
   }

   ~st_device() {
      st_device_destroy($self);
   }
   
   const char * get_name( void ) {
      return $self->screen->get_name($self->screen);
   }

   const char * get_vendor( void ) {
      return $self->screen->get_vendor($self->screen);
   }

   /**
    * Query an integer-valued capability/parameter/limit
    * \param param  one of PIPE_CAP_x
    */
   int get_param( int param ) {
      return $self->screen->get_param($self->screen, param);
   }

   /**
    * Query a float-valued capability/parameter/limit
    * \param param  one of PIPE_CAP_x
    */
   float get_paramf( int param ) {
      return $self->screen->get_paramf($self->screen, param);
   }

   /**
    * Check if the given pipe_format is supported as a texture or
    * drawing surface.
    * \param bind bitmask of PIPE_BIND flags
    */
   int is_format_supported( enum pipe_format format, 
                            enum pipe_texture_target target,
                            unsigned sample_count,
                            unsigned bind, 
                            unsigned geom_flags ) {
      /* We can't really display surfaces with the python statetracker so mask
       * out that usage */
      bind &= ~PIPE_BIND_DISPLAY_TARGET;

      return $self->screen->is_format_supported( $self->screen,
                                                 format,
                                                 target,
                                                 sample_count,
                                                 bind,
                                                 geom_flags );
   }

   struct st_context *
   context_create(void) {
      return st_context_create($self);
   }

   struct pipe_resource * 
   resource_create(
         enum pipe_format format,
         unsigned width,
         unsigned height,
         unsigned depth = 1,
         unsigned last_level = 0,
         enum pipe_texture_target target = PIPE_TEXTURE_2D,
         unsigned bind = 0
      ) {
      struct pipe_resource templat;

      /* We can't really display surfaces with the python statetracker so mask
       * out that usage */
      bind &= ~PIPE_BIND_DISPLAY_TARGET;

      memset(&templat, 0, sizeof(templat));
      templat.format = format;
      templat.width0 = width;
      templat.height0 = height;
      templat.depth0 = depth;
      templat.last_level = last_level;
      templat.target = target;
      templat.bind = bind;

      return $self->screen->resource_create($self->screen, &templat);
   }

   struct pipe_resource *
   buffer_create(unsigned size, unsigned bind = 0) {
      return pipe_buffer_create($self->screen, bind, size);
   }
};
