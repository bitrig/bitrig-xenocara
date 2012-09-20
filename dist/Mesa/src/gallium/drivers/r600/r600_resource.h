/*
 * Copyright 2010 Marek Olšák <maraeo@gmail.com
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
 */
#ifndef R600_RESOURCE_H
#define R600_RESOURCE_H

#include "util/u_transfer.h"
#include "util/u_vbuf_mgr.h"

/* flag to indicate a resource is to be used as a transfer so should not be tiled */
#define R600_RESOURCE_FLAG_TRANSFER     PIPE_RESOURCE_FLAG_DRV_PRIV

/* Texture transfer. */
struct r600_transfer {
	/* Base class. */
	struct pipe_transfer		transfer;
	/* Buffer transfer. */
	struct pipe_transfer		*buffer_transfer;
	unsigned			offset;
	struct pipe_resource		*staging_texture;
};

/* This gets further specialized into either buffer or texture
 * structures. Use the vtbl struct to choose between the two
 * underlying implementations.
 */
struct r600_resource {
	struct u_vbuf_resource		b;
	struct r600_bo			*bo;
	u32				size;
	unsigned			bo_size;
};

struct r600_resource_texture {
	struct r600_resource		resource;
	unsigned			offset[PIPE_MAX_TEXTURE_LEVELS];
	unsigned			pitch_in_bytes[PIPE_MAX_TEXTURE_LEVELS];  /* transfer */
	unsigned			pitch_in_blocks[PIPE_MAX_TEXTURE_LEVELS]; /* texture resource */
	unsigned			layer_size[PIPE_MAX_TEXTURE_LEVELS];
	unsigned			array_mode[PIPE_MAX_TEXTURE_LEVELS];
	unsigned			pitch_override;
	unsigned			size;
	unsigned			tile_type;
	unsigned			depth;
	unsigned			dirty_db;
	struct r600_resource_texture	*flushed_depth_texture;
	boolean				is_flushing_texture;

	/* on some cards we have to use integer 64/128-bit types
	   for s3tc blits, do this until gallium grows int formats */
	boolean force_int_type;
};

#define R600_TEX_IS_TILED(tex, level) ((tex)->array_mode[level] != V_038000_ARRAY_LINEAR_GENERAL && (tex)->array_mode[level] != V_038000_ARRAY_LINEAR_ALIGNED)

#define R600_BUFFER_MAGIC 0xabcd1600

/* XXX this could be removed */
struct r600_resource_buffer {
	struct r600_resource		r;
	uint32_t			magic;
};

struct r600_surface {
	struct pipe_surface		base;
	unsigned			aligned_height;
};

void r600_init_screen_resource_functions(struct pipe_screen *screen);

/* r600_texture */
struct pipe_resource *r600_texture_create(struct pipe_screen *screen,
					const struct pipe_resource *templ);
struct pipe_resource *r600_texture_from_handle(struct pipe_screen *screen,
						const struct pipe_resource *base,
						struct winsys_handle *whandle);

/* r600_buffer */
static INLINE struct r600_resource_buffer *r600_buffer(struct pipe_resource *buffer)
{
	if (buffer) {
		assert(((struct r600_resource_buffer *)buffer)->magic == R600_BUFFER_MAGIC);
		return (struct r600_resource_buffer *)buffer;
	}
	return NULL;
}

int r600_texture_depth_flush(struct pipe_context *ctx, struct pipe_resource *texture, boolean just_create);

/* r600_texture.c texture transfer functions. */
struct pipe_transfer* r600_texture_get_transfer(struct pipe_context *ctx,
						struct pipe_resource *texture,
						unsigned level,
						unsigned usage,
						const struct pipe_box *box);
void r600_texture_transfer_destroy(struct pipe_context *ctx,
				   struct pipe_transfer *trans);
void* r600_texture_transfer_map(struct pipe_context *ctx,
				struct pipe_transfer* transfer);
void r600_texture_transfer_unmap(struct pipe_context *ctx,
				 struct pipe_transfer* transfer);

struct r600_pipe_context;

void r600_upload_const_buffer(struct r600_pipe_context *rctx, struct r600_resource_buffer **rbuffer, uint32_t *offset);

#endif
