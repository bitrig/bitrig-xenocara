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


#include "util/u_memory.h"
#include "util/u_math.h"
#include "util/u_cpu_detect.h"
#include "util/u_format.h"
#include "util/u_format_s3tc.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "draw/draw_context.h"

#include "lp_texture.h"
#include "lp_fence.h"
#include "lp_jit.h"
#include "lp_screen.h"
#include "lp_context.h"
#include "lp_debug.h"
#include "lp_public.h"
#include "lp_limits.h"
#include "lp_rast.h"

#include "state_tracker/sw_winsys.h"

#ifdef DEBUG
int LP_DEBUG = 0;

static const struct debug_named_value lp_debug_flags[] = {
   { "pipe",   DEBUG_PIPE, NULL },
   { "tgsi",   DEBUG_TGSI, NULL },
   { "tex",    DEBUG_TEX, NULL },
   { "setup",  DEBUG_SETUP, NULL },
   { "rast",   DEBUG_RAST, NULL },
   { "query",  DEBUG_QUERY, NULL },
   { "screen", DEBUG_SCREEN, NULL },
   { "show_tiles",    DEBUG_SHOW_TILES, NULL },
   { "show_subtiles", DEBUG_SHOW_SUBTILES, NULL },
   { "counters", DEBUG_COUNTERS, NULL },
   { "scene", DEBUG_SCENE, NULL },
   { "fence", DEBUG_FENCE, NULL },
   { "mem", DEBUG_MEM, NULL },
   { "fs", DEBUG_FS, NULL },
   DEBUG_NAMED_VALUE_END
};
#endif

int LP_PERF = 0;
static const struct debug_named_value lp_perf_flags[] = {
   { "texmem",         PERF_TEX_MEM, NULL },
   { "no_mipmap",      PERF_NO_MIPMAPS, NULL },
   { "no_linear",      PERF_NO_LINEAR, NULL },
   { "no_mip_linear",  PERF_NO_MIP_LINEAR, NULL },
   { "no_tex",         PERF_NO_TEX, NULL },
   { "no_blend",       PERF_NO_BLEND, NULL },
   { "no_depth",       PERF_NO_DEPTH, NULL },
   { "no_alphatest",   PERF_NO_ALPHATEST, NULL },
   DEBUG_NAMED_VALUE_END
};


static const char *
llvmpipe_get_vendor(struct pipe_screen *screen)
{
   return "VMware, Inc.";
}


static const char *
llvmpipe_get_name(struct pipe_screen *screen)
{
   return "llvmpipe";
}


static int
llvmpipe_get_param(struct pipe_screen *screen, enum pipe_cap param)
{
   switch (param) {
   case PIPE_CAP_MAX_TEXTURE_IMAGE_UNITS:
      return PIPE_MAX_SAMPLERS;
   case PIPE_CAP_MAX_VERTEX_TEXTURE_UNITS:
      /* At this time, the draw module and llvmpipe driver only
       * support vertex shader texture lookups when LLVM is enabled in
       * the draw module.
       */
      if (debug_get_bool_option("DRAW_USE_LLVM", TRUE))
         return PIPE_MAX_VERTEX_SAMPLERS;
      else
         return 0;
   case PIPE_CAP_MAX_COMBINED_SAMPLERS:
      return PIPE_MAX_SAMPLERS + PIPE_MAX_VERTEX_SAMPLERS;
   case PIPE_CAP_NPOT_TEXTURES:
      return 1;
   case PIPE_CAP_TWO_SIDED_STENCIL:
      return 1;
   case PIPE_CAP_GLSL:
      return 1;
   case PIPE_CAP_SM3:
      return 1;
   case PIPE_CAP_ANISOTROPIC_FILTER:
      return 0;
   case PIPE_CAP_POINT_SPRITE:
      return 1;
   case PIPE_CAP_MAX_RENDER_TARGETS:
      return PIPE_MAX_COLOR_BUFS;
   case PIPE_CAP_OCCLUSION_QUERY:
      return 1;
   case PIPE_CAP_TIMER_QUERY:
      return 0;
   case PIPE_CAP_TEXTURE_MIRROR_CLAMP:
      return 1;
   case PIPE_CAP_TEXTURE_MIRROR_REPEAT:
      return 1;
   case PIPE_CAP_TEXTURE_SHADOW_MAP:
      return 1;
   case PIPE_CAP_TEXTURE_SWIZZLE:
      return 1;
   case PIPE_CAP_MAX_TEXTURE_2D_LEVELS:
      return LP_MAX_TEXTURE_2D_LEVELS;
   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
      return LP_MAX_TEXTURE_3D_LEVELS;
   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      return LP_MAX_TEXTURE_2D_LEVELS;
   case PIPE_CAP_BLEND_EQUATION_SEPARATE:
      return 1;
   case PIPE_CAP_INDEP_BLEND_ENABLE:
      return 1;
   case PIPE_CAP_INDEP_BLEND_FUNC:
      return 0;
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
      return 1;
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
      return 0;
   case PIPE_CAP_PRIMITIVE_RESTART:
      return 1;
   case PIPE_CAP_DEPTHSTENCIL_CLEAR_SEPARATE:
      return 1;
   case PIPE_CAP_DEPTH_CLAMP:
      return 0;
   default:
      return 0;
   }
}

static int
llvmpipe_get_shader_param(struct pipe_screen *screen, unsigned shader, enum pipe_shader_cap param)
{
   switch(shader)
   {
   case PIPE_SHADER_FRAGMENT:
      return tgsi_exec_get_shader_param(param);
   case PIPE_SHADER_VERTEX:
   case PIPE_SHADER_GEOMETRY:
      return draw_get_shader_param(shader, param);
   default:
      return 0;
   }
}

static float
llvmpipe_get_paramf(struct pipe_screen *screen, enum pipe_cap param)
{
   switch (param) {
   case PIPE_CAP_MAX_LINE_WIDTH:
      /* fall-through */
   case PIPE_CAP_MAX_LINE_WIDTH_AA:
      return 255.0; /* arbitrary */
   case PIPE_CAP_MAX_POINT_WIDTH:
      /* fall-through */
   case PIPE_CAP_MAX_POINT_WIDTH_AA:
      return 255.0; /* arbitrary */
   case PIPE_CAP_MAX_TEXTURE_ANISOTROPY:
      return 16.0; /* not actually signficant at this time */
   case PIPE_CAP_MAX_TEXTURE_LOD_BIAS:
      return 16.0; /* arbitrary */
   case PIPE_CAP_GUARD_BAND_LEFT:
   case PIPE_CAP_GUARD_BAND_TOP:
   case PIPE_CAP_GUARD_BAND_RIGHT:
   case PIPE_CAP_GUARD_BAND_BOTTOM:
      return 0.0;
   default:
      assert(0);
      return 0;
   }
}


/**
 * Query format support for creating a texture, drawing surface, etc.
 * \param format  the format to test
 * \param type  one of PIPE_TEXTURE, PIPE_SURFACE
 */
static boolean
llvmpipe_is_format_supported( struct pipe_screen *_screen,
                              enum pipe_format format,
                              enum pipe_texture_target target,
                              unsigned sample_count,
                              unsigned bind,
                              unsigned geom_flags )
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);
   struct sw_winsys *winsys = screen->winsys;
   const struct util_format_description *format_desc;

   format_desc = util_format_description(format);
   if (!format_desc)
      return FALSE;

   assert(target == PIPE_BUFFER ||
          target == PIPE_TEXTURE_1D ||
          target == PIPE_TEXTURE_2D ||
          target == PIPE_TEXTURE_RECT ||
          target == PIPE_TEXTURE_3D ||
          target == PIPE_TEXTURE_CUBE);

   if (sample_count > 1)
      return FALSE;

   if (bind & PIPE_BIND_RENDER_TARGET) {
      if (format_desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS)
         return FALSE;

      if (format_desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
         return FALSE;

      if (format_desc->block.width != 1 ||
          format_desc->block.height != 1)
         return FALSE;
   }

   if (bind & PIPE_BIND_DISPLAY_TARGET) {
      if(!winsys->is_displaytarget_format_supported(winsys, bind, format))
         return FALSE;
   }

   if (bind & PIPE_BIND_DEPTH_STENCIL) {
      if (format_desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
         return FALSE;

      if (format_desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
         return FALSE;

      /* FIXME: Temporary restriction. See lp_state_fs.c. */
      if (format_desc->block.bits != 32)
         return FALSE;
   }

   if (format_desc->layout == UTIL_FORMAT_LAYOUT_S3TC) {
      return util_format_s3tc_enabled;
   }

   /*
    * Everything else should be supported by u_format.
    */
   return TRUE;
}




static void
llvmpipe_flush_frontbuffer(struct pipe_screen *_screen,
                           struct pipe_resource *resource,
                           unsigned level, unsigned layer,
                           void *context_private)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);
   struct sw_winsys *winsys = screen->winsys;
   struct llvmpipe_resource *texture = llvmpipe_resource(resource);

   assert(texture->dt);
   if (texture->dt)
      winsys->displaytarget_display(winsys, texture->dt, context_private);
}


static void
llvmpipe_destroy_screen( struct pipe_screen *_screen )
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);
   struct sw_winsys *winsys = screen->winsys;

   if (screen->rast)
      lp_rast_destroy(screen->rast);

   lp_jit_screen_cleanup(screen);

   if(winsys->destroy)
      winsys->destroy(winsys);

   pipe_mutex_destroy(screen->rast_mutex);

   FREE(screen);
}




/**
 * Fence reference counting.
 */
static void
llvmpipe_fence_reference(struct pipe_screen *screen,
                         struct pipe_fence_handle **ptr,
                         struct pipe_fence_handle *fence)
{
   struct lp_fence **old = (struct lp_fence **) ptr;
   struct lp_fence *f = (struct lp_fence *) fence;

   lp_fence_reference(old, f);
}


/**
 * Has the fence been executed/finished?
 */
static int
llvmpipe_fence_signalled(struct pipe_screen *screen,
                         struct pipe_fence_handle *fence,
                         unsigned flag)
{
   struct lp_fence *f = (struct lp_fence *) fence;
   return lp_fence_signalled(f);
}


/**
 * Wait for the fence to finish.
 */
static int
llvmpipe_fence_finish(struct pipe_screen *screen,
                      struct pipe_fence_handle *fence_handle,
                      unsigned flag)
{
   struct lp_fence *f = (struct lp_fence *) fence_handle;

   lp_fence_wait(f);
   return 0;
}



/**
 * Create a new pipe_screen object
 * Note: we're not presently subclassing pipe_screen (no llvmpipe_screen).
 */
struct pipe_screen *
llvmpipe_create_screen(struct sw_winsys *winsys)
{
   struct llvmpipe_screen *screen;

#ifdef PIPE_ARCH_X86
   /* require SSE2 due to LLVM PR6960. */
   util_cpu_detect();
   if (!util_cpu_caps.has_sse2)
       return NULL;
#endif

   screen = CALLOC_STRUCT(llvmpipe_screen);

#ifdef DEBUG
   LP_DEBUG = debug_get_flags_option("LP_DEBUG", lp_debug_flags, 0 );
#endif

   LP_PERF = debug_get_flags_option("LP_PERF", lp_perf_flags, 0 );

   if (!screen)
      return NULL;

   screen->winsys = winsys;

   screen->base.destroy = llvmpipe_destroy_screen;

   screen->base.get_name = llvmpipe_get_name;
   screen->base.get_vendor = llvmpipe_get_vendor;
   screen->base.get_param = llvmpipe_get_param;
   screen->base.get_shader_param = llvmpipe_get_shader_param;
   screen->base.get_paramf = llvmpipe_get_paramf;
   screen->base.is_format_supported = llvmpipe_is_format_supported;

   screen->base.context_create = llvmpipe_create_context;
   screen->base.flush_frontbuffer = llvmpipe_flush_frontbuffer;
   screen->base.fence_reference = llvmpipe_fence_reference;
   screen->base.fence_signalled = llvmpipe_fence_signalled;
   screen->base.fence_finish = llvmpipe_fence_finish;

   llvmpipe_init_screen_resource_funcs(&screen->base);

   lp_jit_screen_init(screen);

   screen->num_threads = util_cpu_caps.nr_cpus > 1 ? util_cpu_caps.nr_cpus : 0;
#ifdef PIPE_OS_EMBEDDED
   screen->num_threads = 0;
#endif
   screen->num_threads = debug_get_num_option("LP_NUM_THREADS", screen->num_threads);
   screen->num_threads = MIN2(screen->num_threads, LP_MAX_THREADS);

   screen->rast = lp_rast_create(screen->num_threads);
   if (!screen->rast) {
      lp_jit_screen_cleanup(screen);
      FREE(screen);
      return NULL;
   }
   pipe_mutex_init(screen->rast_mutex);

   util_format_s3tc_init();

   return &screen->base;
}
