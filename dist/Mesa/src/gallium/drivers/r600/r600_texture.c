/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
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
 *      Jerome Glisse
 *      Corbin Simpson
 */
#include <errno.h>
#include <pipe/p_screen.h>
#include <util/u_format.h>
#include <util/u_format_s3tc.h>
#include <util/u_math.h>
#include <util/u_inlines.h>
#include <util/u_memory.h>
#include "state_tracker/drm_driver.h"
#include "pipebuffer/pb_buffer.h"
#include "r600_pipe.h"
#include "r600_resource.h"
#include "r600_state_inlines.h"
#include "r600d.h"
#include "r600_formats.h"

/* Copy from a full GPU texture to a transfer's staging one. */
static void r600_copy_to_staging_texture(struct pipe_context *ctx, struct r600_transfer *rtransfer)
{
	struct pipe_transfer *transfer = (struct pipe_transfer*)rtransfer;
	struct pipe_resource *texture = transfer->resource;

	ctx->resource_copy_region(ctx, rtransfer->staging_texture,
				0, 0, 0, 0, texture, transfer->level,
				&transfer->box);
}


/* Copy from a transfer's staging texture to a full GPU one. */
static void r600_copy_from_staging_texture(struct pipe_context *ctx, struct r600_transfer *rtransfer)
{
	struct pipe_transfer *transfer = (struct pipe_transfer*)rtransfer;
	struct pipe_resource *texture = transfer->resource;
	struct pipe_box sbox;

	sbox.x = sbox.y = sbox.z = 0;
	sbox.width = transfer->box.width;
	sbox.height = transfer->box.height;
	/* XXX that might be wrong */
	sbox.depth = 1;
	ctx->resource_copy_region(ctx, texture, transfer->level,
				  transfer->box.x, transfer->box.y, transfer->box.z,
				  rtransfer->staging_texture,
				  0, &sbox);

        ctx->flush(ctx, NULL);
}

unsigned r600_texture_get_offset(struct r600_resource_texture *rtex,
					unsigned level, unsigned layer)
{
	unsigned offset = rtex->offset[level];

	switch (rtex->resource.b.b.b.target) {
	case PIPE_TEXTURE_3D:
	case PIPE_TEXTURE_CUBE:
	default:
		return offset + layer * rtex->layer_size[level];
	}
}

static unsigned r600_get_block_alignment(struct pipe_screen *screen,
					 enum pipe_format format,
					 unsigned array_mode)
{
	struct r600_screen* rscreen = (struct r600_screen *)screen;
	unsigned pixsize = util_format_get_blocksize(format);
	int p_align;

	switch(array_mode) {
	case V_038000_ARRAY_1D_TILED_THIN1:
		p_align = MAX2(8,
			       ((rscreen->tiling_info->group_bytes / 8 / pixsize)));
		break;
	case V_038000_ARRAY_2D_TILED_THIN1:
		p_align = MAX2(rscreen->tiling_info->num_banks,
			       (((rscreen->tiling_info->group_bytes / 8 / pixsize)) *
				rscreen->tiling_info->num_banks)) * 8;
		break;
	case V_038000_ARRAY_LINEAR_ALIGNED:
		p_align = MAX2(64, rscreen->tiling_info->group_bytes / pixsize);
		break;
	case V_038000_ARRAY_LINEAR_GENERAL:
	default:
		p_align = rscreen->tiling_info->group_bytes / pixsize;
		break;
	}
	return p_align;
}

static unsigned r600_get_height_alignment(struct pipe_screen *screen,
					  unsigned array_mode)
{
	struct r600_screen* rscreen = (struct r600_screen *)screen;
	int h_align;

	switch (array_mode) {
	case V_038000_ARRAY_2D_TILED_THIN1:
		h_align = rscreen->tiling_info->num_channels * 8;
		break;
	case V_038000_ARRAY_1D_TILED_THIN1:
	case V_038000_ARRAY_LINEAR_ALIGNED:
		h_align = 8;
		break;
	case V_038000_ARRAY_LINEAR_GENERAL:
	default:
		h_align = 1;
		break;
	}
	return h_align;
}

static unsigned r600_get_base_alignment(struct pipe_screen *screen,
					enum pipe_format format,
					unsigned array_mode)
{
	struct r600_screen* rscreen = (struct r600_screen *)screen;
	unsigned pixsize = util_format_get_blocksize(format);
	int p_align = r600_get_block_alignment(screen, format, array_mode);
	int h_align = r600_get_height_alignment(screen, array_mode);
	int b_align;

	switch (array_mode) {
	case V_038000_ARRAY_2D_TILED_THIN1:
		b_align = MAX2(rscreen->tiling_info->num_banks * rscreen->tiling_info->num_channels * 8 * 8 * pixsize,
			       p_align * pixsize * h_align);
		break;
	case V_038000_ARRAY_1D_TILED_THIN1:
	case V_038000_ARRAY_LINEAR_ALIGNED:
	case V_038000_ARRAY_LINEAR_GENERAL:
	default:
		b_align = rscreen->tiling_info->group_bytes;
		break;
	}
	return b_align;
}

static unsigned mip_minify(unsigned size, unsigned level)
{
	unsigned val;
	val = u_minify(size, level);
	if (level > 0)
		val = util_next_power_of_two(val);
	return val;
}

static unsigned r600_texture_get_nblocksx(struct pipe_screen *screen,
					  struct r600_resource_texture *rtex,
					  unsigned level)
{
	struct pipe_resource *ptex = &rtex->resource.b.b.b;
	unsigned nblocksx, block_align, width;
	unsigned blocksize = util_format_get_blocksize(ptex->format);

	if (rtex->pitch_override)
		return rtex->pitch_override / blocksize;

	width = mip_minify(ptex->width0, level);
	nblocksx = util_format_get_nblocksx(ptex->format, width);

	block_align = r600_get_block_alignment(screen, ptex->format,
					      rtex->array_mode[level]);
	nblocksx = align(nblocksx, block_align);
	return nblocksx;
}

static unsigned r600_texture_get_nblocksy(struct pipe_screen *screen,
					  struct r600_resource_texture *rtex,
					  unsigned level)
{
	struct pipe_resource *ptex = &rtex->resource.b.b.b;
	unsigned height, tile_height;

	height = mip_minify(ptex->height0, level);
	height = util_format_get_nblocksy(ptex->format, height);
	tile_height = r600_get_height_alignment(screen,
						rtex->array_mode[level]);
	height = align(height, tile_height);
	return height;
}

static void r600_texture_set_array_mode(struct pipe_screen *screen,
					struct r600_resource_texture *rtex,
					unsigned level, unsigned array_mode)
{
	struct pipe_resource *ptex = &rtex->resource.b.b.b;

	switch (array_mode) {
	case V_0280A0_ARRAY_LINEAR_GENERAL:
	case V_0280A0_ARRAY_LINEAR_ALIGNED:
	case V_0280A0_ARRAY_1D_TILED_THIN1:
	default:
		rtex->array_mode[level] = array_mode;
		break;
	case V_0280A0_ARRAY_2D_TILED_THIN1:
	{
		unsigned w, h, tile_height, tile_width;

		tile_height = r600_get_height_alignment(screen, array_mode);
		tile_width = r600_get_block_alignment(screen, ptex->format, array_mode);

		w = mip_minify(ptex->width0, level);
		h = mip_minify(ptex->height0, level);
		if (w <= tile_width || h <= tile_height)
			rtex->array_mode[level] = V_0280A0_ARRAY_1D_TILED_THIN1;
		else
			rtex->array_mode[level] = array_mode;
	}
	break;
	}
}

static void r600_setup_miptree(struct pipe_screen *screen,
			       struct r600_resource_texture *rtex,
			       unsigned array_mode)
{
	struct pipe_resource *ptex = &rtex->resource.b.b.b;
	struct radeon *radeon = (struct radeon *)screen->winsys;
	enum chip_class chipc = r600_get_family_class(radeon);
	unsigned size, layer_size, i, offset;
	unsigned nblocksx, nblocksy, extra_size = 0;

	for (i = 0, offset = 0; i <= ptex->last_level; i++) {
		unsigned blocksize = util_format_get_blocksize(ptex->format);
		unsigned base_align = r600_get_base_alignment(screen, ptex->format, array_mode);

		r600_texture_set_array_mode(screen, rtex, i, array_mode);

		nblocksx = r600_texture_get_nblocksx(screen, rtex, i);
		nblocksy = r600_texture_get_nblocksy(screen, rtex, i);

		layer_size = nblocksx * nblocksy * blocksize;
		if (ptex->target == PIPE_TEXTURE_CUBE) {
			if (chipc >= R700)
				size = layer_size * 8;
			else
				size = layer_size * 6;
		}
		else if (ptex->target == PIPE_TEXTURE_3D)
			size = layer_size * u_minify(ptex->depth0, i);
		else
			size = layer_size * ptex->array_size;

		/* evergreen stores depth and stencil separately */
		if ((chipc >= EVERGREEN) && util_format_is_depth_or_stencil(ptex->format))
			extra_size = align(extra_size + (nblocksx * nblocksy * 1), base_align);

		/* align base image and start of miptree */
		if ((i == 0) || (i == 1))
			offset = align(offset, base_align);
		rtex->offset[i] = offset;
		rtex->layer_size[i] = layer_size;
		rtex->pitch_in_blocks[i] = nblocksx; /* CB talks in elements */
		rtex->pitch_in_bytes[i] = nblocksx * blocksize;

		offset += size;
	}
	rtex->size = offset + extra_size;
}

/* Figure out whether u_blitter will fallback to a transfer operation.
 * If so, don't use a staging resource.
 */
static boolean permit_hardware_blit(struct pipe_screen *screen,
					const struct pipe_resource *res)
{
	unsigned bind;

	if (util_format_is_depth_or_stencil(res->format))
		bind = PIPE_BIND_DEPTH_STENCIL;
	else
		bind = PIPE_BIND_RENDER_TARGET;

	/* hackaround for S3TC */
	if (util_format_is_compressed(res->format))
		return TRUE;
	    
	if (!screen->is_format_supported(screen,
				res->format,
				res->target,
				res->nr_samples,
                                bind))
		return FALSE;

	if (!screen->is_format_supported(screen,
				res->format,
				res->target,
				res->nr_samples,
                                PIPE_BIND_SAMPLER_VIEW))
		return FALSE;

	return TRUE;
}

static boolean r600_texture_get_handle(struct pipe_screen* screen,
					struct pipe_resource *ptex,
					struct winsys_handle *whandle)
{
	struct r600_resource_texture *rtex = (struct r600_resource_texture*)ptex;
	struct r600_resource *resource = &rtex->resource;
	struct radeon *radeon = (struct radeon *)screen->winsys;

	return r600_bo_get_winsys_handle(radeon, resource->bo,
			rtex->pitch_in_bytes[0], whandle);
}

static void r600_texture_destroy(struct pipe_screen *screen,
				 struct pipe_resource *ptex)
{
	struct r600_resource_texture *rtex = (struct r600_resource_texture*)ptex;
	struct r600_resource *resource = &rtex->resource;
	struct radeon *radeon = (struct radeon *)screen->winsys;

	if (rtex->flushed_depth_texture)
		pipe_resource_reference((struct pipe_resource **)&rtex->flushed_depth_texture, NULL);

	if (resource->bo) {
		r600_bo_reference(radeon, &resource->bo, NULL);
	}
	FREE(rtex);
}

static const struct u_resource_vtbl r600_texture_vtbl =
{
	r600_texture_get_handle,	/* get_handle */
	r600_texture_destroy,		/* resource_destroy */
	r600_texture_get_transfer,	/* get_transfer */
	r600_texture_transfer_destroy,	/* transfer_destroy */
	r600_texture_transfer_map,	/* transfer_map */
	u_default_transfer_flush_region,/* transfer_flush_region */
	r600_texture_transfer_unmap,	/* transfer_unmap */
	u_default_transfer_inline_write	/* transfer_inline_write */
};

static struct r600_resource_texture *
r600_texture_create_object(struct pipe_screen *screen,
			   const struct pipe_resource *base,
			   unsigned array_mode,
			   unsigned pitch_in_bytes_override,
			   unsigned max_buffer_size,
			   struct r600_bo *bo)
{
	struct r600_resource_texture *rtex;
	struct r600_resource *resource;
	struct radeon *radeon = (struct radeon *)screen->winsys;

	rtex = CALLOC_STRUCT(r600_resource_texture);
	if (rtex == NULL)
		return NULL;

	resource = &rtex->resource;
	resource->b.b.b = *base;
	resource->b.b.vtbl = &r600_texture_vtbl;
	pipe_reference_init(&resource->b.b.b.reference, 1);
	resource->b.b.b.screen = screen;
	resource->bo = bo;
	rtex->pitch_override = pitch_in_bytes_override;
	/* only mark depth textures the HW can hit as depth textures */
	if (util_format_is_depth_or_stencil(base->format) && permit_hardware_blit(screen, base))
		rtex->depth = 1;

	r600_setup_miptree(screen, rtex, array_mode);

	resource->size = rtex->size;

	if (!resource->bo) {
		struct pipe_resource *ptex = &rtex->resource.b.b.b;
		int base_align = r600_get_base_alignment(screen, ptex->format, array_mode);

		resource->bo = r600_bo(radeon, rtex->size, base_align, base->bind, base->usage);
		if (!resource->bo) {
			FREE(rtex);
			return NULL;
		}
	}
	return rtex;
}

struct pipe_resource *r600_texture_create(struct pipe_screen *screen,
						const struct pipe_resource *templ)
{
	unsigned array_mode = 0;
	static int force_tiling = -1;

	/* Would like some magic "get_bool_option_once" routine.
	 */
	if (force_tiling == -1) {
#if 0
		/* reenable when 2D tiling is fixed better */
		struct r600_screen *rscreen = (struct r600_screen *)screen;
		if (r600_get_minor_version(rscreen->radeon) >= 9)
			force_tiling = debug_get_bool_option("R600_TILING", TRUE);
#endif
		force_tiling = debug_get_bool_option("R600_TILING", FALSE);
	}

	if (force_tiling && permit_hardware_blit(screen, templ)) {
		if (!(templ->flags & R600_RESOURCE_FLAG_TRANSFER) &&
		    !(templ->bind & PIPE_BIND_SCANOUT)) {
			array_mode = V_038000_ARRAY_2D_TILED_THIN1;
		}
	}

	if (!(templ->flags & R600_RESOURCE_FLAG_TRANSFER) &&
	    util_format_is_compressed(templ->format))
		array_mode = V_038000_ARRAY_1D_TILED_THIN1;

	return (struct pipe_resource *)r600_texture_create_object(screen, templ, array_mode,
								  0, 0, NULL);

}

static struct pipe_surface *r600_create_surface(struct pipe_context *pipe,
						struct pipe_resource *texture,
						const struct pipe_surface *surf_tmpl)
{
	struct r600_resource_texture *rtex = (struct r600_resource_texture*)texture;
	struct r600_surface *surface = CALLOC_STRUCT(r600_surface);
	unsigned level = surf_tmpl->u.tex.level;

	assert(surf_tmpl->u.tex.first_layer == surf_tmpl->u.tex.last_layer);
	if (surface == NULL)
		return NULL;
	/* XXX no offset */
/*	offset = r600_texture_get_offset(rtex, level, surf_tmpl->u.tex.first_layer);*/
	pipe_reference_init(&surface->base.reference, 1);
	pipe_resource_reference(&surface->base.texture, texture);
	surface->base.context = pipe;
	surface->base.format = surf_tmpl->format;
	surface->base.width = mip_minify(texture->width0, level);
	surface->base.height = mip_minify(texture->height0, level);
	surface->base.usage = surf_tmpl->usage;
	surface->base.texture = texture;
	surface->base.u.tex.first_layer = surf_tmpl->u.tex.first_layer;
	surface->base.u.tex.last_layer = surf_tmpl->u.tex.last_layer;
	surface->base.u.tex.level = level;

	surface->aligned_height = r600_texture_get_nblocksy(pipe->screen,
							    rtex, level);
	return &surface->base;
}

static void r600_surface_destroy(struct pipe_context *pipe,
				 struct pipe_surface *surface)
{
	pipe_resource_reference(&surface->texture, NULL);
	FREE(surface);
}


struct pipe_resource *r600_texture_from_handle(struct pipe_screen *screen,
					       const struct pipe_resource *templ,
					       struct winsys_handle *whandle)
{
	struct radeon *rw = (struct radeon*)screen->winsys;
	struct r600_bo *bo = NULL;
	unsigned array_mode = 0;

	/* Support only 2D textures without mipmaps */
	if ((templ->target != PIPE_TEXTURE_2D && templ->target != PIPE_TEXTURE_RECT) ||
	      templ->depth0 != 1 || templ->last_level != 0)
		return NULL;

	bo = r600_bo_handle(rw, whandle->handle, &array_mode);
	if (bo == NULL) {
		return NULL;
	}

	return (struct pipe_resource *)r600_texture_create_object(screen, templ, array_mode,
								  whandle->stride,
								  0,
								  bo);
}

int r600_texture_depth_flush(struct pipe_context *ctx,
			     struct pipe_resource *texture, boolean just_create)
{
	struct r600_resource_texture *rtex = (struct r600_resource_texture*)texture;
	struct pipe_resource resource;

	if (rtex->flushed_depth_texture)
		goto out;

	resource.target = PIPE_TEXTURE_2D;
	resource.format = texture->format;
	resource.width0 = texture->width0;
	resource.height0 = texture->height0;
	resource.depth0 = 1;
	resource.array_size = 1;
	resource.last_level = texture->last_level;
	resource.nr_samples = 0;
	resource.usage = PIPE_USAGE_DYNAMIC;
	resource.bind = 0;
	resource.flags = R600_RESOURCE_FLAG_TRANSFER;

	resource.bind |= PIPE_BIND_DEPTH_STENCIL;

	rtex->flushed_depth_texture = (struct r600_resource_texture *)ctx->screen->resource_create(ctx->screen, &resource);
	if (rtex->flushed_depth_texture == NULL) {
		R600_ERR("failed to create temporary texture to hold untiled copy\n");
		return -ENOMEM;
	}

	((struct r600_resource_texture *)rtex->flushed_depth_texture)->is_flushing_texture = TRUE;
out:
	if (just_create)
		return 0;

	/* XXX: only do this if the depth texture has actually changed:
	 */
	r600_blit_uncompress_depth(ctx, rtex);
	return 0;
}

/* Needs adjustment for pixelformat:
 */
static INLINE unsigned u_box_volume( const struct pipe_box *box )
{
	return box->width * box->depth * box->height;
};

struct pipe_transfer* r600_texture_get_transfer(struct pipe_context *ctx,
						struct pipe_resource *texture,
						unsigned level,
						unsigned usage,
						const struct pipe_box *box)
{
	struct r600_resource_texture *rtex = (struct r600_resource_texture*)texture;
	struct pipe_resource resource;
	struct r600_transfer *trans;
	int r;
	boolean use_staging_texture = FALSE;

	/* We cannot map a tiled texture directly because the data is
	 * in a different order, therefore we do detiling using a blit.
	 *
	 * Also, use a temporary in GTT memory for read transfers, as
	 * the CPU is much happier reading out of cached system memory
	 * than uncached VRAM.
	 */
	if (R600_TEX_IS_TILED(rtex, level))
		use_staging_texture = TRUE;

	if ((usage & PIPE_TRANSFER_READ) && u_box_volume(box) > 1024)
		use_staging_texture = TRUE;

	/* XXX: Use a staging texture for uploads if the underlying BO
	 * is busy.  No interface for checking that currently? so do
	 * it eagerly whenever the transfer doesn't require a readback
	 * and might block.
	 */
	if ((usage & PIPE_TRANSFER_WRITE) &&
			!(usage & (PIPE_TRANSFER_READ |
					PIPE_TRANSFER_DONTBLOCK |
					PIPE_TRANSFER_UNSYNCHRONIZED)))
		use_staging_texture = TRUE;

	if (!permit_hardware_blit(ctx->screen, texture) ||
		(texture->flags & R600_RESOURCE_FLAG_TRANSFER))
		use_staging_texture = FALSE;

	trans = CALLOC_STRUCT(r600_transfer);
	if (trans == NULL)
		return NULL;
	pipe_resource_reference(&trans->transfer.resource, texture);
	trans->transfer.level = level;
	trans->transfer.usage = usage;
	trans->transfer.box = *box;
	if (rtex->depth) {
		/* XXX: only readback the rectangle which is being mapped?
		*/
		/* XXX: when discard is true, no need to read back from depth texture
		*/
		r = r600_texture_depth_flush(ctx, texture, FALSE);
		if (r < 0) {
			R600_ERR("failed to create temporary texture to hold untiled copy\n");
			pipe_resource_reference(&trans->transfer.resource, NULL);
			FREE(trans);
			return NULL;
		}
		trans->transfer.stride = rtex->flushed_depth_texture->pitch_in_bytes[level];
		trans->offset = r600_texture_get_offset(rtex->flushed_depth_texture, level, box->z);
		return &trans->transfer;
	} else if (use_staging_texture) {
		resource.target = PIPE_TEXTURE_2D;
		resource.format = texture->format;
		resource.width0 = box->width;
		resource.height0 = box->height;
		resource.depth0 = 1;
		resource.array_size = 1;
		resource.last_level = 0;
		resource.nr_samples = 0;
		resource.usage = PIPE_USAGE_STAGING;
		resource.bind = 0;
		resource.flags = R600_RESOURCE_FLAG_TRANSFER;
		/* For texture reading, the temporary (detiled) texture is used as
		 * a render target when blitting from a tiled texture. */
		if (usage & PIPE_TRANSFER_READ) {
			resource.bind |= PIPE_BIND_RENDER_TARGET;
		}
		/* For texture writing, the temporary texture is used as a sampler
		 * when blitting into a tiled texture. */
		if (usage & PIPE_TRANSFER_WRITE) {
			resource.bind |= PIPE_BIND_SAMPLER_VIEW;
		}
		/* Create the temporary texture. */
		trans->staging_texture = ctx->screen->resource_create(ctx->screen, &resource);
		if (trans->staging_texture == NULL) {
			R600_ERR("failed to create temporary texture to hold untiled copy\n");
			pipe_resource_reference(&trans->transfer.resource, NULL);
			FREE(trans);
			return NULL;
		}

		trans->transfer.stride =
			((struct r600_resource_texture *)trans->staging_texture)->pitch_in_bytes[0];
		if (usage & PIPE_TRANSFER_READ) {
			r600_copy_to_staging_texture(ctx, trans);
			/* Always referenced in the blit. */
                        ctx->flush(ctx, NULL);
		}
		return &trans->transfer;
	}
	trans->transfer.stride = rtex->pitch_in_bytes[level];
	trans->transfer.layer_stride = rtex->layer_size[level];
	trans->offset = r600_texture_get_offset(rtex, level, box->z);
	return &trans->transfer;
}

void r600_texture_transfer_destroy(struct pipe_context *ctx,
				   struct pipe_transfer *transfer)
{
	struct r600_transfer *rtransfer = (struct r600_transfer*)transfer;
	struct pipe_resource *texture = transfer->resource;
	struct r600_resource_texture *rtex = (struct r600_resource_texture*)texture;

	if (rtransfer->staging_texture) {
		if (transfer->usage & PIPE_TRANSFER_WRITE) {
			r600_copy_from_staging_texture(ctx, rtransfer);
		}
		pipe_resource_reference(&rtransfer->staging_texture, NULL);
	}

	if (rtex->depth && !rtex->is_flushing_texture) {
		if ((transfer->usage & PIPE_TRANSFER_WRITE) && rtex->flushed_depth_texture)
			r600_blit_push_depth(ctx, rtex);
	}

	pipe_resource_reference(&transfer->resource, NULL);
	FREE(transfer);
}

void* r600_texture_transfer_map(struct pipe_context *ctx,
				struct pipe_transfer* transfer)
{
	struct r600_transfer *rtransfer = (struct r600_transfer*)transfer;
	struct r600_bo *bo;
	enum pipe_format format = transfer->resource->format;
	struct radeon *radeon = (struct radeon *)ctx->screen->winsys;
	unsigned offset = 0;
	unsigned usage = 0;
	char *map;

	if (rtransfer->staging_texture) {
		bo = ((struct r600_resource *)rtransfer->staging_texture)->bo;
	} else {
		struct r600_resource_texture *rtex = (struct r600_resource_texture*)transfer->resource;

		if (rtex->flushed_depth_texture)
			bo = ((struct r600_resource *)rtex->flushed_depth_texture)->bo;
		else
			bo = ((struct r600_resource *)transfer->resource)->bo;

		offset = rtransfer->offset +
			transfer->box.y / util_format_get_blockheight(format) * transfer->stride +
			transfer->box.x / util_format_get_blockwidth(format) * util_format_get_blocksize(format);
	}

	if (transfer->usage & PIPE_TRANSFER_WRITE) {
		usage |= PB_USAGE_CPU_WRITE;

		if (transfer->usage & PIPE_TRANSFER_DISCARD) {
		}

		if (transfer->usage & PIPE_TRANSFER_FLUSH_EXPLICIT) {
		}
	}

	if (transfer->usage & PIPE_TRANSFER_READ) {
		usage |= PB_USAGE_CPU_READ;
	}

	if (transfer->usage & PIPE_TRANSFER_DONTBLOCK) {
		usage |= PB_USAGE_DONTBLOCK;
	}

	if (transfer->usage & PIPE_TRANSFER_UNSYNCHRONIZED) {
		usage |= PB_USAGE_UNSYNCHRONIZED;
	}

	map = r600_bo_map(radeon, bo, usage, ctx);
	if (!map) {
		return NULL;
	}

	return map + offset;
}

void r600_texture_transfer_unmap(struct pipe_context *ctx,
				 struct pipe_transfer* transfer)
{
	struct r600_transfer *rtransfer = (struct r600_transfer*)transfer;
	struct radeon *radeon = (struct radeon *)ctx->screen->winsys;
	struct r600_bo *bo;

	if (rtransfer->staging_texture) {
		bo = ((struct r600_resource *)rtransfer->staging_texture)->bo;
	} else {
		struct r600_resource_texture *rtex = (struct r600_resource_texture*)transfer->resource;

		if (rtex->flushed_depth_texture) {
			bo = ((struct r600_resource *)rtex->flushed_depth_texture)->bo;
		} else {
			bo = ((struct r600_resource *)transfer->resource)->bo;
		}
	}
	r600_bo_unmap(radeon, bo);
}

void r600_init_surface_functions(struct r600_pipe_context *r600)
{
	r600->context.create_surface = r600_create_surface;
	r600->context.surface_destroy = r600_surface_destroy;
}

static unsigned r600_get_swizzle_combined(const unsigned char *swizzle_format,
		const unsigned char *swizzle_view)
{
	unsigned i;
	unsigned char swizzle[4];
	unsigned result = 0;
	const uint32_t swizzle_shift[4] = {
		16, 19, 22, 25,
	};
	const uint32_t swizzle_bit[4] = {
		0, 1, 2, 3,
	};

	if (swizzle_view) {
		/* Combine two sets of swizzles. */
		for (i = 0; i < 4; i++) {
			swizzle[i] = swizzle_view[i] <= UTIL_FORMAT_SWIZZLE_W ?
				swizzle_format[swizzle_view[i]] : swizzle_view[i];
		}
	} else {
		memcpy(swizzle, swizzle_format, 4);
	}

	/* Get swizzle. */
	for (i = 0; i < 4; i++) {
		switch (swizzle[i]) {
		case UTIL_FORMAT_SWIZZLE_Y:
			result |= swizzle_bit[1] << swizzle_shift[i];
			break;
		case UTIL_FORMAT_SWIZZLE_Z:
			result |= swizzle_bit[2] << swizzle_shift[i];
			break;
		case UTIL_FORMAT_SWIZZLE_W:
			result |= swizzle_bit[3] << swizzle_shift[i];
			break;
		case UTIL_FORMAT_SWIZZLE_0:
			result |= V_038010_SQ_SEL_0 << swizzle_shift[i];
			break;
		case UTIL_FORMAT_SWIZZLE_1:
			result |= V_038010_SQ_SEL_1 << swizzle_shift[i];
			break;
		default: /* UTIL_FORMAT_SWIZZLE_X */
			result |= swizzle_bit[0] << swizzle_shift[i];
		}
	}
	return result;
}

/* texture format translate */
uint32_t r600_translate_texformat(struct pipe_screen *screen,
				  enum pipe_format format,
				  const unsigned char *swizzle_view,
				  uint32_t *word4_p, uint32_t *yuv_format_p)
{
	uint32_t result = 0, word4 = 0, yuv_format = 0;
	const struct util_format_description *desc;
	boolean uniform = TRUE;
	static int r600_enable_s3tc = -1;

	int i;
	const uint32_t sign_bit[4] = {
		S_038010_FORMAT_COMP_X(V_038010_SQ_FORMAT_COMP_SIGNED),
		S_038010_FORMAT_COMP_Y(V_038010_SQ_FORMAT_COMP_SIGNED),
		S_038010_FORMAT_COMP_Z(V_038010_SQ_FORMAT_COMP_SIGNED),
		S_038010_FORMAT_COMP_W(V_038010_SQ_FORMAT_COMP_SIGNED)
	};
	desc = util_format_description(format);

	word4 |= r600_get_swizzle_combined(desc->swizzle, swizzle_view);

	/* Colorspace (return non-RGB formats directly). */
	switch (desc->colorspace) {
		/* Depth stencil formats */
	case UTIL_FORMAT_COLORSPACE_ZS:
		switch (format) {
		case PIPE_FORMAT_Z16_UNORM:
			result = FMT_16;
			goto out_word4;
		case PIPE_FORMAT_X24S8_USCALED:
			word4 |= S_038010_NUM_FORMAT_ALL(V_038010_SQ_NUM_FORMAT_INT);
		case PIPE_FORMAT_Z24X8_UNORM:
		case PIPE_FORMAT_Z24_UNORM_S8_USCALED:
			result = FMT_8_24;
			goto out_word4;
		case PIPE_FORMAT_S8X24_USCALED:
			word4 |= S_038010_NUM_FORMAT_ALL(V_038010_SQ_NUM_FORMAT_INT);
		case PIPE_FORMAT_X8Z24_UNORM:
		case PIPE_FORMAT_S8_USCALED_Z24_UNORM:
			result = FMT_24_8;
			goto out_word4;
		case PIPE_FORMAT_S8_USCALED:
			result = FMT_8;
			word4 |= S_038010_NUM_FORMAT_ALL(V_038010_SQ_NUM_FORMAT_INT);
			goto out_word4;
		default:
			goto out_unknown;
		}

	case UTIL_FORMAT_COLORSPACE_YUV:
		yuv_format |= (1 << 30);
		switch (format) {
		case PIPE_FORMAT_UYVY:
		case PIPE_FORMAT_YUYV:
		default:
			break;
		}
		goto out_unknown; /* TODO */

	case UTIL_FORMAT_COLORSPACE_SRGB:
		word4 |= S_038010_FORCE_DEGAMMA(1);
		break;

	default:
		break;
	}

	if (r600_enable_s3tc == -1) {
		struct r600_screen *rscreen = (struct r600_screen *)screen;
		if (r600_get_minor_version(rscreen->radeon) >= 9)
			r600_enable_s3tc = 1;
		else
			r600_enable_s3tc = debug_get_bool_option("R600_ENABLE_S3TC", FALSE);
	}

	if (desc->layout == UTIL_FORMAT_LAYOUT_RGTC) {
		if (!r600_enable_s3tc)
			goto out_unknown;

		switch (format) {
		case PIPE_FORMAT_RGTC1_SNORM:
		case PIPE_FORMAT_LATC1_SNORM:
			word4 |= sign_bit[0];
		case PIPE_FORMAT_RGTC1_UNORM:
		case PIPE_FORMAT_LATC1_UNORM:
			result = FMT_BC4;
			goto out_word4;
		case PIPE_FORMAT_RGTC2_SNORM:
		case PIPE_FORMAT_LATC2_SNORM:
			word4 |= sign_bit[0] | sign_bit[1];
		case PIPE_FORMAT_RGTC2_UNORM:
		case PIPE_FORMAT_LATC2_UNORM:
			result = FMT_BC5;
			goto out_word4;
		default:
			goto out_unknown;
		}
	}

	if (desc->layout == UTIL_FORMAT_LAYOUT_S3TC) {

		if (!r600_enable_s3tc)
			goto out_unknown;

		if (!util_format_s3tc_enabled) {
			goto out_unknown;
		}

		switch (format) {
		case PIPE_FORMAT_DXT1_RGB:
		case PIPE_FORMAT_DXT1_RGBA:
		case PIPE_FORMAT_DXT1_SRGB:
		case PIPE_FORMAT_DXT1_SRGBA:
			result = FMT_BC1;
			goto out_word4;
		case PIPE_FORMAT_DXT3_RGBA:
		case PIPE_FORMAT_DXT3_SRGBA:
			result = FMT_BC2;
			goto out_word4;
		case PIPE_FORMAT_DXT5_RGBA:
		case PIPE_FORMAT_DXT5_SRGBA:
			result = FMT_BC3;
			goto out_word4;
		default:
			goto out_unknown;
		}
	}

	if (format == PIPE_FORMAT_R9G9B9E5_FLOAT) {
		result = FMT_5_9_9_9_SHAREDEXP;
		goto out_word4;
	} else if (format == PIPE_FORMAT_R11G11B10_FLOAT) {
		result = FMT_10_11_11_FLOAT;
		goto out_word4;
	}


	for (i = 0; i < desc->nr_channels; i++) {
		if (desc->channel[i].type == UTIL_FORMAT_TYPE_SIGNED) {
			word4 |= sign_bit[i];
		}
	}

	/* R8G8Bx_SNORM - TODO CxV8U8 */

	/* See whether the components are of the same size. */
	for (i = 1; i < desc->nr_channels; i++) {
		uniform = uniform && desc->channel[0].size == desc->channel[i].size;
	}

	/* Non-uniform formats. */
	if (!uniform) {
		switch(desc->nr_channels) {
		case 3:
			if (desc->channel[0].size == 5 &&
			    desc->channel[1].size == 6 &&
			    desc->channel[2].size == 5) {
				result = FMT_5_6_5;
				goto out_word4;
			}
			goto out_unknown;
		case 4:
			if (desc->channel[0].size == 5 &&
			    desc->channel[1].size == 5 &&
			    desc->channel[2].size == 5 &&
			    desc->channel[3].size == 1) {
				result = FMT_1_5_5_5;
				goto out_word4;
			}
			if (desc->channel[0].size == 10 &&
			    desc->channel[1].size == 10 &&
			    desc->channel[2].size == 10 &&
			    desc->channel[3].size == 2) {
				result = FMT_2_10_10_10;
				goto out_word4;
			}
			goto out_unknown;
		}
		goto out_unknown;
	}

	/* Find the first non-VOID channel. */
	for (i = 0; i < 4; i++) {
		if (desc->channel[i].type != UTIL_FORMAT_TYPE_VOID) {
			break;
		}
	}

	if (i == 4)
		goto out_unknown;

	/* uniform formats */
	switch (desc->channel[i].type) {
	case UTIL_FORMAT_TYPE_UNSIGNED:
	case UTIL_FORMAT_TYPE_SIGNED:
		if (!desc->channel[i].normalized &&
		    desc->colorspace != UTIL_FORMAT_COLORSPACE_SRGB) {
			goto out_unknown;
		}

		switch (desc->channel[i].size) {
		case 4:
			switch (desc->nr_channels) {
			case 2:
				result = FMT_4_4;
				goto out_word4;
			case 4:
				result = FMT_4_4_4_4;
				goto out_word4;
			}
			goto out_unknown;
		case 8:
			switch (desc->nr_channels) {
			case 1:
				result = FMT_8;
				goto out_word4;
			case 2:
				result = FMT_8_8;
				goto out_word4;
			case 4:
				result = FMT_8_8_8_8;
				goto out_word4;
			}
			goto out_unknown;
		case 16:
			switch (desc->nr_channels) {
			case 1:
				result = FMT_16;
				goto out_word4;
			case 2:
				result = FMT_16_16;
				goto out_word4;
			case 4:
				result = FMT_16_16_16_16;
				goto out_word4;
			}
			goto out_unknown;
		case 32:
			switch (desc->nr_channels) {
			case 1:
				result = FMT_32;
				goto out_word4;
			case 2:
				result = FMT_32_32;
				goto out_word4;
			case 4:
				result = FMT_32_32_32_32;
				goto out_word4;
			}
		}
		goto out_unknown;

	case UTIL_FORMAT_TYPE_FLOAT:
		switch (desc->channel[i].size) {
		case 16:
			switch (desc->nr_channels) {
			case 1:
				result = FMT_16_FLOAT;
				goto out_word4;
			case 2:
				result = FMT_16_16_FLOAT;
				goto out_word4;
			case 4:
				result = FMT_16_16_16_16_FLOAT;
				goto out_word4;
			}
			goto out_unknown;
		case 32:
			switch (desc->nr_channels) {
			case 1:
				result = FMT_32_FLOAT;
				goto out_word4;
			case 2:
				result = FMT_32_32_FLOAT;
				goto out_word4;
			case 4:
				result = FMT_32_32_32_32_FLOAT;
				goto out_word4;
			}
		}
		goto out_unknown;
	}

out_word4:
	if (word4_p)
		*word4_p = word4;
	if (yuv_format_p)
		*yuv_format_p = yuv_format;
	return result;
out_unknown:
	/* R600_ERR("Unable to handle texformat %d %s\n", format, util_format_name(format)); */
	return ~0;
}
