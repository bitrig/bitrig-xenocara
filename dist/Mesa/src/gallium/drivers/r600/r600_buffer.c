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
 *      Corbin Simpson <MostAwesomeDude@gmail.com>
 */
#include <pipe/p_screen.h>
#include <util/u_format.h>
#include <util/u_math.h>
#include <util/u_inlines.h>
#include <util/u_memory.h>
#include "state_tracker/drm_driver.h"
#include <xf86drm.h>
#include "radeon_drm.h"
#include "r600.h"
#include "r600_pipe.h"

extern struct u_resource_vtbl r600_buffer_vtbl;


struct pipe_resource *r600_buffer_create(struct pipe_screen *screen,
					 const struct pipe_resource *templ)
{
	struct r600_resource_buffer *rbuffer;
	struct r600_bo *bo;
	/* XXX We probably want a different alignment for buffers and textures. */
	unsigned alignment = 4096;

	rbuffer = CALLOC_STRUCT(r600_resource_buffer);
	if (rbuffer == NULL)
		return NULL;

	rbuffer->magic = R600_BUFFER_MAGIC;
	rbuffer->user_buffer = NULL;
	rbuffer->r.base.b = *templ;
	pipe_reference_init(&rbuffer->r.base.b.reference, 1);
	rbuffer->r.base.b.screen = screen;
	rbuffer->r.base.vtbl = &r600_buffer_vtbl;
	rbuffer->r.size = rbuffer->r.base.b.width0;
	rbuffer->r.bo_size = rbuffer->r.size;
	rbuffer->uploaded = FALSE;
	bo = r600_bo((struct radeon*)screen->winsys, rbuffer->r.base.b.width0, alignment, rbuffer->r.base.b.bind, rbuffer->r.base.b.usage);
	if (bo == NULL) {
		FREE(rbuffer);
		return NULL;
	}
	rbuffer->r.bo = bo;
	return &rbuffer->r.base.b;
}

struct pipe_resource *r600_user_buffer_create(struct pipe_screen *screen,
					      void *ptr, unsigned bytes,
					      unsigned bind)
{
	struct r600_resource_buffer *rbuffer;

	rbuffer = CALLOC_STRUCT(r600_resource_buffer);
	if (rbuffer == NULL)
		return NULL;

	rbuffer->magic = R600_BUFFER_MAGIC;
	pipe_reference_init(&rbuffer->r.base.b.reference, 1);
	rbuffer->r.base.vtbl = &r600_buffer_vtbl;
	rbuffer->r.base.b.screen = screen;
	rbuffer->r.base.b.target = PIPE_BUFFER;
	rbuffer->r.base.b.format = PIPE_FORMAT_R8_UNORM;
	rbuffer->r.base.b.usage = PIPE_USAGE_IMMUTABLE;
	rbuffer->r.base.b.bind = bind;
	rbuffer->r.base.b.width0 = bytes;
	rbuffer->r.base.b.height0 = 1;
	rbuffer->r.base.b.depth0 = 1;
	rbuffer->r.base.b.array_size = 1;
	rbuffer->r.base.b.flags = 0;
	rbuffer->r.bo = NULL;
	rbuffer->r.bo_size = 0;
	rbuffer->user_buffer = ptr;
	rbuffer->uploaded = FALSE;
	return &rbuffer->r.base.b;
}

static void r600_buffer_destroy(struct pipe_screen *screen,
				struct pipe_resource *buf)
{
	struct r600_resource_buffer *rbuffer = r600_buffer(buf);

	if (rbuffer->r.bo) {
		r600_bo_reference((struct radeon*)screen->winsys, &rbuffer->r.bo, NULL);
	}
	rbuffer->r.bo = NULL;
	FREE(rbuffer);
}

static void *r600_buffer_transfer_map(struct pipe_context *pipe,
				      struct pipe_transfer *transfer)
{
	struct r600_resource_buffer *rbuffer = r600_buffer(transfer->resource);
	int write = 0;
	uint8_t *data;

	if (rbuffer->user_buffer)
		return (uint8_t*)rbuffer->user_buffer + transfer->box.x;

	if (transfer->usage & PIPE_TRANSFER_DONTBLOCK) {
		/* FIXME */
	}
	if (transfer->usage & PIPE_TRANSFER_WRITE) {
		write = 1;
	}
	data = r600_bo_map((struct radeon*)pipe->winsys, rbuffer->r.bo, transfer->usage, pipe);
	if (!data)
		return NULL;

	return (uint8_t*)data + transfer->box.x;
}

static void r600_buffer_transfer_unmap(struct pipe_context *pipe,
					struct pipe_transfer *transfer)
{
	struct r600_resource_buffer *rbuffer = r600_buffer(transfer->resource);

	if (rbuffer->user_buffer)
		return;

	if (rbuffer->r.bo)
		r600_bo_unmap((struct radeon*)pipe->winsys, rbuffer->r.bo);
}

static void r600_buffer_transfer_flush_region(struct pipe_context *pipe,
						struct pipe_transfer *transfer,
						const struct pipe_box *box)
{
}

unsigned r600_buffer_is_referenced_by_cs(struct pipe_context *context,
					 struct pipe_resource *buf,
					 unsigned level, int layer)
{
	/* FIXME */
	return PIPE_REFERENCED_FOR_READ | PIPE_REFERENCED_FOR_WRITE;
}

struct pipe_resource *r600_buffer_from_handle(struct pipe_screen *screen,
					      struct winsys_handle *whandle)
{
	struct radeon *rw = (struct radeon*)screen->winsys;
	struct r600_resource *rbuffer;
	struct r600_bo *bo = NULL;

	bo = r600_bo_handle(rw, whandle->handle, NULL);
	if (bo == NULL) {
		return NULL;
	}

	rbuffer = CALLOC_STRUCT(r600_resource);
	if (rbuffer == NULL) {
		r600_bo_reference(rw, &bo, NULL);
		return NULL;
	}

	pipe_reference_init(&rbuffer->base.b.reference, 1);
	rbuffer->base.b.target = PIPE_BUFFER;
	rbuffer->base.b.screen = screen;
	rbuffer->base.vtbl = &r600_buffer_vtbl;
	rbuffer->bo = bo;
	return &rbuffer->base.b;
}

struct u_resource_vtbl r600_buffer_vtbl =
{
	u_default_resource_get_handle,		/* get_handle */
	r600_buffer_destroy,			/* resource_destroy */
	r600_buffer_is_referenced_by_cs,	/* is_buffer_referenced */
	u_default_get_transfer,			/* get_transfer */
	u_default_transfer_destroy,		/* transfer_destroy */
	r600_buffer_transfer_map,		/* transfer_map */
	r600_buffer_transfer_flush_region,	/* transfer_flush_region */
	r600_buffer_transfer_unmap,		/* transfer_unmap */
	u_default_transfer_inline_write		/* transfer_inline_write */
};

int r600_upload_index_buffer(struct r600_pipe_context *rctx, struct r600_drawl *draw)
{
	if (r600_buffer_is_user_buffer(draw->index_buffer)) {
		struct r600_resource_buffer *rbuffer = r600_buffer(draw->index_buffer);
		unsigned upload_offset;
		int ret = 0;

		ret = r600_upload_buffer(rctx->rupload_vb,
					draw->index_buffer_offset,
					draw->count * draw->index_size,
					rbuffer,
					&upload_offset,
					&rbuffer->r.bo_size,
					&rbuffer->r.bo);
		if (ret)
			return ret;
		rbuffer->uploaded = TRUE;
		draw->index_buffer_offset = upload_offset;
	}

	return 0;
}

int r600_upload_user_buffers(struct r600_pipe_context *rctx)
{
	enum pipe_error ret = PIPE_OK;
	int i, nr;

	nr = rctx->vertex_elements->count;
	nr = rctx->nvertex_buffer;

	for (i = 0; i < nr; i++) {
		struct pipe_vertex_buffer *vb = &rctx->vertex_buffer[i];

		if (r600_buffer_is_user_buffer(vb->buffer)) {
			struct r600_resource_buffer *rbuffer = r600_buffer(vb->buffer);
			unsigned upload_offset;

			ret = r600_upload_buffer(rctx->rupload_vb,
						0, vb->buffer->width0,
						rbuffer,
						&upload_offset,
						&rbuffer->r.bo_size,
						&rbuffer->r.bo);
			if (ret)
				return ret;
			rbuffer->uploaded = TRUE;
			vb->buffer_offset = upload_offset;
		}
	}
	return ret;
}
