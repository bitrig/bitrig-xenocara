/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *      Christian König <christian.koenig@amd.com>
 */

#ifndef SI_STATE_H
#define SI_STATE_H

#include "si_pm4.h"
#include "radeon/r600_pipe_common.h"

#define SI_NUM_SHADERS (PIPE_SHADER_TESS_EVAL+1)

struct si_screen;
struct si_shader;

struct si_state_blend {
	struct si_pm4_state	pm4;
	uint32_t		cb_target_mask;
	bool			alpha_to_one;
	bool			dual_src_blend;
};

struct si_state_sample_mask {
	struct si_pm4_state	pm4;
	uint16_t		sample_mask;
};

struct si_state_scissor {
	struct si_pm4_state		pm4;
	struct pipe_scissor_state	scissor;
};

struct si_state_viewport {
	struct si_pm4_state		pm4;
	struct pipe_viewport_state	viewport;
};

struct si_state_rasterizer {
	struct si_pm4_state	pm4;
	bool			flatshade;
	bool			two_side;
	bool			multisample_enable;
	bool			line_stipple_enable;
	unsigned		sprite_coord_enable;
	unsigned		pa_sc_line_stipple;
	unsigned		pa_cl_clip_cntl;
	unsigned		clip_plane_enable;
	float			offset_units;
	float			offset_scale;
	bool			poly_stipple_enable;
	bool			line_smooth;
	bool			poly_smooth;
};

struct si_state_dsa {
	struct si_pm4_state	pm4;
	unsigned		alpha_func;
	uint8_t			valuemask[2];
	uint8_t			writemask[2];
};

struct si_vertex_element
{
	unsigned			count;
	uint32_t			rsrc_word3[PIPE_MAX_ATTRIBS];
	uint32_t			format_size[PIPE_MAX_ATTRIBS];
	struct pipe_vertex_element	elements[PIPE_MAX_ATTRIBS];
};

union si_state {
	struct {
		struct si_state_blend		*blend;
		struct si_pm4_state		*blend_color;
		struct si_pm4_state		*clip;
		struct si_state_sample_mask	*sample_mask;
		struct si_state_scissor		*scissor[16];
		struct si_state_viewport	*viewport[16];
		struct si_state_rasterizer	*rasterizer;
		struct si_state_dsa		*dsa;
		struct si_pm4_state		*fb_rs;
		struct si_pm4_state		*fb_blend;
		struct si_pm4_state		*dsa_stencil_ref;
		struct si_pm4_state		*ta_bordercolor_base;
		struct si_pm4_state		*ls;
		struct si_pm4_state		*hs;
		struct si_pm4_state		*es;
		struct si_pm4_state		*gs;
		struct si_pm4_state		*gs_rings;
		struct si_pm4_state		*tf_ring;
		struct si_pm4_state		*vgt_shader_config;
		struct si_pm4_state		*vs;
		struct si_pm4_state		*ps;
		struct si_pm4_state		*spi;
	} named;
	struct si_pm4_state	*array[0];
};

struct si_shader_data {
	struct r600_atom	atom;
	uint32_t		sh_base[SI_NUM_SHADERS];
};

#define SI_NUM_USER_SAMPLERS            16 /* AKA OpenGL textures units per shader */
#define SI_POLY_STIPPLE_SAMPLER         SI_NUM_USER_SAMPLERS
#define SI_NUM_SAMPLERS                 (SI_POLY_STIPPLE_SAMPLER + 1)

/* User sampler views:   0..15
 * Polygon stipple tex:  16
 * FMASK sampler views:  17..33 (no sampler states)
 */
#define SI_FMASK_TEX_OFFSET		SI_NUM_SAMPLERS
#define SI_NUM_SAMPLER_VIEWS		(SI_FMASK_TEX_OFFSET + SI_NUM_SAMPLERS)
#define SI_NUM_SAMPLER_STATES		SI_NUM_SAMPLERS

/* User constant buffers:   0..15
 * Driver state constants:  16
 */
#define SI_NUM_USER_CONST_BUFFERS	16
#define SI_DRIVER_STATE_CONST_BUF	SI_NUM_USER_CONST_BUFFERS
#define SI_NUM_CONST_BUFFERS		(SI_DRIVER_STATE_CONST_BUF + 1)

/* Read-write buffer slots.
 *
 * Ring buffers:        0..1
 * Streamout buffers:   2..5
 */
#define SI_RING_TESS_FACTOR	0 /* for HS (TCS)  */
#define SI_RING_ESGS		0 /* for ES, GS */
#define SI_RING_GSVS		1 /* for GS, VS */
#define SI_RING_GSVS_1		2 /* 1, 2, 3 for GS */
#define SI_RING_GSVS_2		3
#define SI_RING_GSVS_3		4
#define SI_NUM_RING_BUFFERS	5
#define SI_SO_BUF_OFFSET	SI_NUM_RING_BUFFERS
#define SI_NUM_RW_BUFFERS	(SI_SO_BUF_OFFSET + 4)

#define SI_NUM_VERTEX_BUFFERS	16


/* This represents descriptors in memory, such as buffer resources,
 * image resources, and sampler states.
 */
struct si_descriptors {
	/* The list of descriptors in malloc'd memory. */
	uint32_t *list;
	/* The size of one descriptor. */
	unsigned element_dw_size;
	/* The maximum number of descriptors. */
	unsigned num_elements;
	/* Whether the list has been changed and should be re-uploaded. */
	bool list_dirty;

	/* The buffer where the descriptors have been uploaded. */
	struct r600_resource *buffer;
	unsigned buffer_offset;

	/* The i-th bit is set if that element is enabled (non-NULL resource). */
	uint64_t enabled_mask;

	/* The shader userdata offset within a shader where the 64-bit pointer to the descriptor
	 * array will be stored. */
	unsigned shader_userdata_offset;
	/* Whether the pointer should be re-emitted. */
	bool pointer_dirty;
};

struct si_sampler_views {
	struct si_descriptors		desc;
	struct pipe_sampler_view	*views[SI_NUM_SAMPLER_VIEWS];
};

struct si_sampler_states {
	struct si_descriptors		desc;
	void				*saved_states[2]; /* saved for u_blitter */
};

struct si_buffer_resources {
	struct si_descriptors		desc;
	enum radeon_bo_usage		shader_usage; /* READ, WRITE, or READWRITE */
	enum radeon_bo_priority		priority;
	struct pipe_resource		**buffers; /* this has num_buffers elements */
};

#define si_pm4_block_idx(member) \
	(offsetof(union si_state, named.member) / sizeof(struct si_pm4_state *))

#define si_pm4_state_changed(sctx, member) \
	((sctx)->queued.named.member != (sctx)->emitted.named.member)

#define si_pm4_bind_state(sctx, member, value) \
	do { \
		(sctx)->queued.named.member = (value); \
	} while(0)

#define si_pm4_delete_state(sctx, member, value) \
	do { \
		if ((sctx)->queued.named.member == (value)) { \
			(sctx)->queued.named.member = NULL; \
		} \
		si_pm4_free_state(sctx, (struct si_pm4_state *)(value), \
				  si_pm4_block_idx(member)); \
	} while(0)

#define si_pm4_set_state(sctx, member, value) \
	do { \
		if ((sctx)->queued.named.member != (value)) { \
			si_pm4_free_state(sctx, \
				(struct si_pm4_state *)(sctx)->queued.named.member, \
				si_pm4_block_idx(member)); \
			(sctx)->queued.named.member = (value); \
		} \
	} while(0)

/* si_descriptors.c */
void si_set_sampler_descriptors(struct si_context *sctx, unsigned shader,
				unsigned start, unsigned count, void **states);
void si_set_ring_buffer(struct pipe_context *ctx, uint shader, uint slot,
			struct pipe_resource *buffer,
			unsigned stride, unsigned num_records,
			bool add_tid, bool swizzle,
			unsigned element_size, unsigned index_stride, uint64_t offset);
void si_init_all_descriptors(struct si_context *sctx);
bool si_upload_shader_descriptors(struct si_context *sctx);
void si_release_all_descriptors(struct si_context *sctx);
void si_all_descriptors_begin_new_cs(struct si_context *sctx);
void si_upload_const_buffer(struct si_context *sctx, struct r600_resource **rbuffer,
			    const uint8_t *ptr, unsigned size, uint32_t *const_offset);
void si_shader_change_notify(struct si_context *sctx);

/* si_state.c */
struct si_shader_selector;

void si_update_fb_blend_state(struct si_context *sctx);
boolean si_is_format_supported(struct pipe_screen *screen,
                               enum pipe_format format,
                               enum pipe_texture_target target,
                               unsigned sample_count,
                               unsigned usage);
void si_init_state_functions(struct si_context *sctx);
unsigned cik_bank_wh(unsigned bankwh);
unsigned cik_db_pipe_config(struct si_screen *sscreen, unsigned tile_mode);
unsigned cik_macro_tile_aspect(unsigned macro_tile_aspect);
unsigned cik_tile_split(unsigned tile_split);
unsigned si_array_mode(unsigned mode);
uint32_t si_num_banks(struct si_screen *sscreen, struct r600_texture *tex);
unsigned si_tile_mode_index(struct r600_texture *rtex, unsigned level, bool stencil);
struct pipe_sampler_view *
si_create_sampler_view_custom(struct pipe_context *ctx,
			      struct pipe_resource *texture,
			      const struct pipe_sampler_view *state,
			      unsigned width0, unsigned height0,
			      unsigned force_level);

/* si_state_shader.c */
bool si_update_shaders(struct si_context *sctx);
void si_init_shader_functions(struct si_context *sctx);

/* si_state_draw.c */
extern const struct r600_atom si_atom_cache_flush;
extern const struct r600_atom si_atom_msaa_sample_locs;
extern const struct r600_atom si_atom_msaa_config;
void si_emit_cache_flush(struct r600_common_context *sctx, struct r600_atom *atom);
void si_draw_vbo(struct pipe_context *ctx, const struct pipe_draw_info *dinfo);

/* si_commands.c */
void si_cmd_context_control(struct si_pm4_state *pm4);

#endif
