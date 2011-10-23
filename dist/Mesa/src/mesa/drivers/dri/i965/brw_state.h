/*
 Copyright (C) Intel Corp.  2006.  All Rights Reserved.
 Intel funded Tungsten Graphics (http://www.tungstengraphics.com) to
 develop this 3D driver.
 
 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:
 
 The above copyright notice and this permission notice (including the
 next paragraph) shall be included in all copies or substantial
 portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 
 **********************************************************************/
 /*
  * Authors:
  *   Keith Whitwell <keith@tungstengraphics.com>
  */
    

#ifndef BRW_STATE_H
#define BRW_STATE_H

#include "brw_context.h"

static INLINE void
brw_add_validated_bo(struct brw_context *brw, drm_intel_bo *bo)
{
   assert(brw->state.validated_bo_count < ARRAY_SIZE(brw->state.validated_bos));

   if (bo != NULL) {
      drm_intel_bo_reference(bo);
      brw->state.validated_bos[brw->state.validated_bo_count++] = bo;
   }
};

extern const struct brw_tracked_state brw_blend_constant_color;
extern const struct brw_tracked_state brw_cc_unit;
extern const struct brw_tracked_state brw_check_fallback;
extern const struct brw_tracked_state brw_clip_prog;
extern const struct brw_tracked_state brw_clip_unit;
extern const struct brw_tracked_state brw_vs_constants;
extern const struct brw_tracked_state brw_wm_constants;
extern const struct brw_tracked_state brw_constant_buffer;
extern const struct brw_tracked_state brw_curbe_offsets;
extern const struct brw_tracked_state brw_invarient_state;
extern const struct brw_tracked_state brw_gs_prog;
extern const struct brw_tracked_state brw_gs_unit;
extern const struct brw_tracked_state brw_line_stipple;
extern const struct brw_tracked_state brw_aa_line_parameters;
extern const struct brw_tracked_state brw_pipelined_state_pointers;
extern const struct brw_tracked_state brw_binding_table_pointers;
extern const struct brw_tracked_state brw_depthbuffer;
extern const struct brw_tracked_state brw_polygon_stipple_offset;
extern const struct brw_tracked_state brw_polygon_stipple;
extern const struct brw_tracked_state brw_program_parameters;
extern const struct brw_tracked_state brw_recalculate_urb_fence;
extern const struct brw_tracked_state brw_sf_prog;
extern const struct brw_tracked_state brw_sf_unit;
extern const struct brw_tracked_state brw_sf_vp;
extern const struct brw_tracked_state brw_state_base_address;
extern const struct brw_tracked_state brw_urb_fence;
extern const struct brw_tracked_state brw_vertex_state;
extern const struct brw_tracked_state brw_vs_surfaces;
extern const struct brw_tracked_state brw_vs_prog;
extern const struct brw_tracked_state brw_vs_unit;
extern const struct brw_tracked_state brw_wm_input_sizes;
extern const struct brw_tracked_state brw_wm_prog;
extern const struct brw_tracked_state brw_wm_samplers;
extern const struct brw_tracked_state brw_wm_constant_surface;
extern const struct brw_tracked_state brw_wm_surfaces;
extern const struct brw_tracked_state brw_wm_binding_table;
extern const struct brw_tracked_state brw_wm_unit;

extern const struct brw_tracked_state brw_psp_urb_cbs;

extern const struct brw_tracked_state brw_pipe_control;

extern const struct brw_tracked_state brw_drawing_rect;
extern const struct brw_tracked_state brw_indices;
extern const struct brw_tracked_state brw_vertices;
extern const struct brw_tracked_state brw_index_buffer;
extern const struct brw_tracked_state gen6_binding_table_pointers;
extern const struct brw_tracked_state gen6_blend_state;
extern const struct brw_tracked_state gen6_cc_state_pointers;
extern const struct brw_tracked_state gen6_clip_state;
extern const struct brw_tracked_state gen6_clip_vp;
extern const struct brw_tracked_state gen6_color_calc_state;
extern const struct brw_tracked_state gen6_depth_stencil_state;
extern const struct brw_tracked_state gen6_gs_state;
extern const struct brw_tracked_state gen6_sampler_state;
extern const struct brw_tracked_state gen6_scissor_state;
extern const struct brw_tracked_state gen6_scissor_state_pointers;
extern const struct brw_tracked_state gen6_sf_state;
extern const struct brw_tracked_state gen6_sf_vp;
extern const struct brw_tracked_state gen6_urb;
extern const struct brw_tracked_state gen6_viewport_state;
extern const struct brw_tracked_state gen6_vs_state;
extern const struct brw_tracked_state gen6_wm_constants;
extern const struct brw_tracked_state gen6_wm_state;

/***********************************************************************
 * brw_state.c
 */
void brw_validate_state(struct brw_context *brw);
void brw_upload_state(struct brw_context *brw);
void brw_init_state(struct brw_context *brw);
void brw_destroy_state(struct brw_context *brw);
void brw_clear_validated_bos(struct brw_context *brw);

/***********************************************************************
 * brw_state_cache.c
 */
drm_intel_bo *brw_cache_data(struct brw_cache *cache,
		       enum brw_cache_id cache_id,
		       const void *data,
		       GLuint size);

drm_intel_bo *brw_upload_cache(struct brw_cache *cache,
			       enum brw_cache_id cache_id,
			       const void *key,
			       GLuint key_sz,
			       drm_intel_bo **reloc_bufs,
			       GLuint nr_reloc_bufs,
			       const void *data,
			       GLuint data_sz);

drm_intel_bo *brw_upload_cache_with_auxdata(struct brw_cache *cache,
					    enum brw_cache_id cache_id,
					    const void *key,
					    GLuint key_sz,
					    drm_intel_bo **reloc_bufs,
					    GLuint nr_reloc_bufs,
					    const void *data,
					    GLuint data_sz,
					    const void *aux,
					    GLuint aux_sz,
					    void *aux_return);

drm_intel_bo *brw_search_cache( struct brw_cache *cache,
			  enum brw_cache_id cache_id,
			  const void *key,
			  GLuint key_size,
			  drm_intel_bo **reloc_bufs,
			  GLuint nr_reloc_bufs,
			  void *aux_return);
void brw_state_cache_check_size( struct brw_context *brw );

void brw_init_caches( struct brw_context *brw );
void brw_destroy_caches( struct brw_context *brw );

/***********************************************************************
 * brw_state_batch.c
 */
#define BRW_BATCH_STRUCT(brw, s) intel_batchbuffer_data(brw->intel.batch, (s), \
							sizeof(*(s)), false)
#define BRW_CACHED_BATCH_STRUCT(brw, s) brw_cached_batch_struct( brw, (s), sizeof(*(s)) )

GLboolean brw_cached_batch_struct( struct brw_context *brw,
				   const void *data,
				   GLuint sz );
void brw_destroy_batch_cache( struct brw_context *brw );
void brw_clear_batch_cache( struct brw_context *brw );
void *brw_state_batch(struct brw_context *brw,
		      int size,
		      int alignment,
		      drm_intel_bo **out_bo,
		      uint32_t *out_offset);

/* brw_wm_surface_state.c */
void brw_create_constant_surface(struct brw_context *brw,
				 drm_intel_bo *bo,
				 int width,
				 drm_intel_bo **out_bo,
				 uint32_t *out_offset);

#endif
