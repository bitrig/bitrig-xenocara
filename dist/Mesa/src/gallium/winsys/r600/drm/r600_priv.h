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
 */
#ifndef R600_PRIV_H
#define R600_PRIV_H

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <util/u_double_list.h>
#include <util/u_inlines.h>
#include <os/os_thread.h>
#include "r600.h"

struct r600_bomgr;
struct r600_bo;

struct radeon {
	int				fd;
	int				refcount;
	unsigned			device;
	unsigned			family;
	enum chip_class			chip_class;
	struct r600_tiling_info		tiling_info;
	struct r600_bomgr		*bomgr;
	unsigned			fence;
	unsigned			*cfence;
	struct r600_bo			*fence_bo;
};

struct r600_reg {
	unsigned			opcode;
	unsigned			offset_base;
	unsigned			offset;
	unsigned			need_bo;
	unsigned			flush_flags;
	unsigned			flush_mask;
};

struct radeon_bo {
	struct pipe_reference		reference;
	unsigned			handle;
	unsigned			size;
	unsigned			alignment;
	int				map_count;
	void				*data;
	struct list_head		fencedlist;
	unsigned			fence;
	struct r600_context		*ctx;
	boolean				shared;
	struct r600_reloc		*reloc;
	unsigned			reloc_id;
	unsigned			last_flush;
};

struct r600_bo {
	struct pipe_reference		reference;
	unsigned			size;
	unsigned			tiling_flags;
	unsigned			kernel_pitch;
	unsigned			domains;
	struct radeon_bo		*bo;
	unsigned			fence;
	/* manager data */
	struct list_head		list;
	unsigned			manager_id;
	unsigned			alignment;
	unsigned			offset;
	int64_t				start;
	int64_t				end;
};

struct r600_bomgr {
	struct radeon			*radeon;
	unsigned			usecs;
	pipe_mutex			mutex;
	struct list_head		delayed;
	unsigned			num_delayed;
};

/*
 * r600_drm.c
 */
struct radeon *r600_new(int fd, unsigned device);
void r600_delete(struct radeon *r600);

/*
 * radeon_pciid.c
 */
unsigned radeon_family_from_device(unsigned device);

/*
 * radeon_bo.c
 */
struct radeon_bo *radeon_bo(struct radeon *radeon, unsigned handle,
			    unsigned size, unsigned alignment);
void radeon_bo_reference(struct radeon *radeon, struct radeon_bo **dst,
			 struct radeon_bo *src);
int radeon_bo_wait(struct radeon *radeon, struct radeon_bo *bo);
int radeon_bo_busy(struct radeon *radeon, struct radeon_bo *bo, uint32_t *domain);
int radeon_bo_fencelist(struct radeon *radeon, struct radeon_bo **bolist, uint32_t num_bo);
int radeon_bo_get_tiling_flags(struct radeon *radeon,
			       struct radeon_bo *bo,
			       uint32_t *tiling_flags,
			       uint32_t *pitch);
int radeon_bo_get_name(struct radeon *radeon,
		       struct radeon_bo *bo,
		       uint32_t *name);

/*
 * r600_hw_context.c
 */
int r600_context_init_fence(struct r600_context *ctx);
void r600_context_bo_reloc(struct r600_context *ctx, u32 *pm4, struct r600_bo *rbo);
void r600_context_bo_flush(struct r600_context *ctx, unsigned flush_flags,
				unsigned flush_mask, struct r600_bo *rbo);
struct r600_bo *r600_context_reg_bo(struct r600_context *ctx, unsigned offset);
int r600_context_add_block(struct r600_context *ctx, const struct r600_reg *reg, unsigned nreg);

/*
 * r600_bo.c
 */
void r600_bo_destroy(struct radeon *radeon, struct r600_bo *bo);

/*
 * r600_bomgr.c
 */
struct r600_bomgr *r600_bomgr_create(struct radeon *radeon, unsigned usecs);
void r600_bomgr_destroy(struct r600_bomgr *mgr);
bool r600_bomgr_bo_destroy(struct r600_bomgr *mgr, struct r600_bo *bo);
void r600_bomgr_bo_init(struct r600_bomgr *mgr, struct r600_bo *bo);
struct r600_bo *r600_bomgr_bo_create(struct r600_bomgr *mgr,
					unsigned size,
					unsigned alignment,
					unsigned cfence);


/*
 * helpers
 */
#define CTX_RANGE_ID(ctx, offset) (((offset) >> (ctx)->hash_shift) & 255)
#define CTX_BLOCK_ID(ctx, offset) ((offset) & ((1 << (ctx)->hash_shift) - 1))

static void inline r600_context_reg(struct r600_context *ctx,
					unsigned offset, unsigned value,
					unsigned mask)
{
	struct r600_range *range;
	struct r600_block *block;
	unsigned id;

	range = &ctx->range[CTX_RANGE_ID(ctx, offset)];
	block = range->blocks[CTX_BLOCK_ID(ctx, offset)];
	id = (offset - block->start_offset) >> 2;
	block->reg[id] &= ~mask;
	block->reg[id] |= value;
	if (!(block->status & R600_BLOCK_STATUS_DIRTY)) {
		ctx->pm4_dirty_cdwords += block->pm4_ndwords;
		block->status |= R600_BLOCK_STATUS_ENABLED;
		block->status |= R600_BLOCK_STATUS_DIRTY;
		LIST_ADDTAIL(&block->list,&ctx->dirty);
	}
}

static inline void r600_context_block_emit_dirty(struct r600_context *ctx, struct r600_block *block)
{
	int id;

	for (int j = 0; j < block->nreg; j++) {
		if (block->pm4_bo_index[j]) {
			/* find relocation */
			id = block->pm4_bo_index[j];
			r600_context_bo_reloc(ctx,
					&block->pm4[block->reloc[id].bo_pm4_index],
					block->reloc[id].bo);
			r600_context_bo_flush(ctx,
					block->reloc[id].flush_flags,
					block->reloc[id].flush_mask,
					block->reloc[id].bo);
		}
	}
	memcpy(&ctx->pm4[ctx->pm4_cdwords], block->pm4, block->pm4_ndwords * 4);
	ctx->pm4_cdwords += block->pm4_ndwords;
	block->status ^= R600_BLOCK_STATUS_DIRTY;
	LIST_DELINIT(&block->list);
}

/*
 * radeon_bo.c
 */
static inline int radeon_bo_map(struct radeon *radeon, struct radeon_bo *bo)
{
	bo->map_count++;
	return 0;
}

static inline void radeon_bo_unmap(struct radeon *radeon, struct radeon_bo *bo)
{
	bo->map_count--;
	assert(bo->map_count >= 0);
}

/*
 * r600_bo
 */
static inline struct radeon_bo *r600_bo_get_bo(struct r600_bo *bo)
{
	return bo->bo;
}

static unsigned inline r600_bo_get_handle(struct r600_bo *bo)
{
	return bo->bo->handle;
}

static unsigned inline r600_bo_get_size(struct r600_bo *bo)
{
	return bo->size;
}

/*
 * fence
 */
static inline bool fence_is_after(unsigned fence, unsigned ofence)
{
	/* handle wrap around */
	if (fence < 0x80000000 && ofence > 0x80000000)
		return TRUE;
	if (fence > ofence)
		return TRUE;
	return FALSE;
}

#endif
