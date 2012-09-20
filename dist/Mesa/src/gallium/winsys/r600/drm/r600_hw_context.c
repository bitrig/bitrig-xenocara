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
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pipe/p_compiler.h>
#include <util/u_inlines.h>
#include <util/u_memory.h>
#include <pipebuffer/pb_bufmgr.h>
#include "xf86drm.h"
#include "radeon_drm.h"
#include "r600_priv.h"
#include "bof.h"
#include "r600d.h"

#define GROUP_FORCE_NEW_BLOCK	0

/* Get backends mask */
void r600_get_backend_mask(struct r600_context *ctx)
{
	struct r600_bo * buffer;
	u32 * results;
	unsigned num_backends = r600_get_num_backends(ctx->radeon);
	unsigned i, mask = 0;

	/* if backend_map query is supported by the kernel */
	if (ctx->radeon->backend_map_valid) {
		unsigned num_tile_pipes = r600_get_num_tile_pipes(ctx->radeon);
		unsigned backend_map = r600_get_backend_map(ctx->radeon);
		unsigned item_width, item_mask;

		if (ctx->radeon->chip_class >= EVERGREEN) {
			item_width = 4;
			item_mask = 0x7;
		} else {
			item_width = 2;
			item_mask = 0x3;
		}

		while(num_tile_pipes--) {
			i = backend_map & item_mask;
			mask |= (1<<i);
			backend_map >>= item_width;
		}
		if (mask != 0) {
			ctx->backend_mask = mask;
			return;
		}
	}

	/* otherwise backup path for older kernels */

	/* create buffer for event data */
	buffer = r600_bo(ctx->radeon, ctx->max_db*16, 1, 0,
				PIPE_USAGE_STAGING);
	if (!buffer)
		goto err;

	/* initialize buffer with zeroes */
	results = r600_bo_map(ctx->radeon, buffer, PB_USAGE_CPU_WRITE, NULL);
	if (results) {
		memset(results, 0, ctx->max_db * 4 * 4);
		r600_bo_unmap(ctx->radeon, buffer);

		/* emit EVENT_WRITE for ZPASS_DONE */
		ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_EVENT_WRITE, 2, 0);
		ctx->pm4[ctx->pm4_cdwords++] = EVENT_TYPE(EVENT_TYPE_ZPASS_DONE) | EVENT_INDEX(1);
		ctx->pm4[ctx->pm4_cdwords++] = 0;
		ctx->pm4[ctx->pm4_cdwords++] = 0;

		ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_NOP, 0, 0);
		ctx->pm4[ctx->pm4_cdwords++] = 0;
		r600_context_bo_reloc(ctx, &ctx->pm4[ctx->pm4_cdwords - 1], buffer);

		/* execute */
		r600_context_flush(ctx);

		/* analyze results */
		results = r600_bo_map(ctx->radeon, buffer, PB_USAGE_CPU_READ, NULL);
		if (results) {
			for(i = 0; i < ctx->max_db; i++) {
				/* at least highest bit will be set if backend is used */
				if (results[i*4 + 1])
					mask |= (1<<i);
			}
			r600_bo_unmap(ctx->radeon, buffer);
		}
	}

	r600_bo_reference(ctx->radeon, &buffer, NULL);

	if (mask != 0) {
		ctx->backend_mask = mask;
		return;
	}

err:
	/* fallback to old method - set num_backends lower bits to 1 */
	ctx->backend_mask = (~((u32)0))>>(32-num_backends);
	return;
}

static inline void r600_context_ps_partial_flush(struct r600_context *ctx)
{
	if (!(ctx->flags & R600_CONTEXT_DRAW_PENDING))
		return;

	ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_EVENT_WRITE, 0, 0);
	ctx->pm4[ctx->pm4_cdwords++] = EVENT_TYPE(EVENT_TYPE_PS_PARTIAL_FLUSH) | EVENT_INDEX(4);

	ctx->flags &= ~R600_CONTEXT_DRAW_PENDING;
}

void r600_init_cs(struct r600_context *ctx)
{
	/* R6xx requires this packet at the start of each command buffer */
	if (ctx->radeon->family < CHIP_RV770) {
		ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_START_3D_CMDBUF, 0, 0);
		ctx->pm4[ctx->pm4_cdwords++] = 0x00000000;
	}
	/* All asics require this one */
	ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_CONTEXT_CONTROL, 1, 0);
	ctx->pm4[ctx->pm4_cdwords++] = 0x80000000;
	ctx->pm4[ctx->pm4_cdwords++] = 0x80000000;

	ctx->init_dwords = ctx->pm4_cdwords;
}

static void INLINE r600_context_update_fenced_list(struct r600_context *ctx)
{
	for (int i = 0; i < ctx->creloc; i++) {
		if (!LIST_IS_EMPTY(&ctx->bo[i]->fencedlist))
			LIST_DELINIT(&ctx->bo[i]->fencedlist);
		LIST_ADDTAIL(&ctx->bo[i]->fencedlist, &ctx->fenced_bo);
		ctx->bo[i]->fence = ctx->radeon->fence;
		ctx->bo[i]->ctx = ctx;
	}
}

static void INLINE r600_context_fence_wraparound(struct r600_context *ctx, unsigned fence)
{
	struct radeon_bo *bo = NULL;
	struct radeon_bo *tmp;

	LIST_FOR_EACH_ENTRY_SAFE(bo, tmp, &ctx->fenced_bo, fencedlist) {
		if (bo->fence <= *ctx->radeon->cfence) {
			LIST_DELINIT(&bo->fencedlist);
			bo->fence = 0;
		} else {
			bo->fence = fence;
		}
	}
}

static void r600_init_block(struct r600_context *ctx,
			    struct r600_block *block,
			    const struct r600_reg *reg, int index, int nreg,
			    unsigned opcode, unsigned offset_base)
{
	int i = index;
	int j, n = nreg;

	/* initialize block */
	if (opcode == PKT3_SET_RESOURCE) {
		block->flags = BLOCK_FLAG_RESOURCE;
		block->status |= R600_BLOCK_STATUS_RESOURCE_DIRTY; /* dirty all blocks at start */
	} else {
		block->flags = 0;
		block->status |= R600_BLOCK_STATUS_DIRTY; /* dirty all blocks at start */
	}
	block->start_offset = reg[i].offset;
	block->pm4[block->pm4_ndwords++] = PKT3(opcode, n, 0);
	block->pm4[block->pm4_ndwords++] = (block->start_offset - offset_base) >> 2;
	block->reg = &block->pm4[block->pm4_ndwords];
	block->pm4_ndwords += n;
	block->nreg = n;
	block->nreg_dirty = n;
	LIST_INITHEAD(&block->list);
	LIST_INITHEAD(&block->enable_list);

	for (j = 0; j < n; j++) {
		if (reg[i+j].flags & REG_FLAG_DIRTY_ALWAYS) {
			block->flags |= REG_FLAG_DIRTY_ALWAYS;
		}
		if (reg[i+j].flags & REG_FLAG_ENABLE_ALWAYS) {
			if (!(block->status & R600_BLOCK_STATUS_ENABLED)) {
				block->status |= R600_BLOCK_STATUS_ENABLED;
				LIST_ADDTAIL(&block->enable_list, &ctx->enable_list);
				LIST_ADDTAIL(&block->list,&ctx->dirty);
			}
		}
		if (reg[i+j].flags & REG_FLAG_FLUSH_CHANGE) {
			block->flags |= REG_FLAG_FLUSH_CHANGE;
		}

		if (reg[i+j].flags & REG_FLAG_NEED_BO) {
			block->nbo++;
			assert(block->nbo < R600_BLOCK_MAX_BO);
			block->pm4_bo_index[j] = block->nbo;
			block->pm4[block->pm4_ndwords++] = PKT3(PKT3_NOP, 0, 0);
			block->pm4[block->pm4_ndwords++] = 0x00000000;
			if (reg[i+j].flags & REG_FLAG_RV6XX_SBU) {
				block->reloc[block->nbo].flush_flags = 0;
				block->reloc[block->nbo].flush_mask = 0;
			} else {
				block->reloc[block->nbo].flush_flags = reg[i+j].flush_flags;
				block->reloc[block->nbo].flush_mask = reg[i+j].flush_mask;
			}
			block->reloc[block->nbo].bo_pm4_index = block->pm4_ndwords - 1;
		}
		if ((ctx->radeon->family > CHIP_R600) &&
		    (ctx->radeon->family < CHIP_RV770) && reg[i+j].flags & REG_FLAG_RV6XX_SBU) {
			block->pm4[block->pm4_ndwords++] = PKT3(PKT3_SURFACE_BASE_UPDATE, 0, 0);
			block->pm4[block->pm4_ndwords++] = reg[i+j].flush_flags;
		}
	}
	for (j = 0; j < n; j++) {
		if (reg[i+j].flush_flags) {
			block->pm4_flush_ndwords += 7;
		}
	}
	/* check that we stay in limit */
	assert(block->pm4_ndwords < R600_BLOCK_MAX_REG);
}

int r600_context_add_block(struct r600_context *ctx, const struct r600_reg *reg, unsigned nreg,
			   unsigned opcode, unsigned offset_base)
{
	struct r600_block *block;
	struct r600_range *range;
	int offset;

	for (unsigned i = 0, n = 0; i < nreg; i += n) {
		/* ignore new block balise */
		if (reg[i].offset == GROUP_FORCE_NEW_BLOCK) {
			n = 1;
			continue;
		}

		/* ignore regs not on R600 on R600 */
		if ((reg[i].flags & REG_FLAG_NOT_R600) && ctx->radeon->family == CHIP_R600) {
			n = 1;
			continue;
		}

		/* register that need relocation are in their own group */
		/* find number of consecutive registers */
		n = 0;
		offset = reg[i].offset;
		while (reg[i + n].offset == offset) {
			n++;
			offset += 4;
			if ((n + i) >= nreg)
				break;
			if (n >= (R600_BLOCK_MAX_REG - 2))
				break;
		}

		/* allocate new block */
		block = calloc(1, sizeof(struct r600_block));
		if (block == NULL) {
			return -ENOMEM;
		}
		ctx->nblocks++;
		for (int j = 0; j < n; j++) {
			range = &ctx->range[CTX_RANGE_ID(reg[i + j].offset)];
			/* create block table if it doesn't exist */
			if (!range->blocks)
				range->blocks = calloc(1 << HASH_SHIFT, sizeof(void *));
			if (!range->blocks)
				return -1;

			range->blocks[CTX_BLOCK_ID(reg[i + j].offset)] = block;
		}

		r600_init_block(ctx, block, reg, i, n, opcode, offset_base);

	}
	return 0;
}

/* R600/R700 configuration */
static const struct r600_reg r600_config_reg_list[] = {
	{R_008958_VGT_PRIMITIVE_TYPE, 0, 0, 0},
	{R_008C00_SQ_CONFIG, REG_FLAG_ENABLE_ALWAYS | REG_FLAG_FLUSH_CHANGE, 0, 0},
	{R_008C04_SQ_GPR_RESOURCE_MGMT_1, REG_FLAG_ENABLE_ALWAYS | REG_FLAG_FLUSH_CHANGE, 0, 0},
	{R_008C08_SQ_GPR_RESOURCE_MGMT_2, REG_FLAG_ENABLE_ALWAYS | REG_FLAG_FLUSH_CHANGE, 0, 0},
	{R_008C0C_SQ_THREAD_RESOURCE_MGMT, REG_FLAG_ENABLE_ALWAYS | REG_FLAG_FLUSH_CHANGE, 0, 0},
	{R_008C10_SQ_STACK_RESOURCE_MGMT_1, REG_FLAG_ENABLE_ALWAYS | REG_FLAG_FLUSH_CHANGE, 0, 0},
	{R_008C14_SQ_STACK_RESOURCE_MGMT_2, REG_FLAG_ENABLE_ALWAYS | REG_FLAG_FLUSH_CHANGE, 0, 0},
	{R_008D8C_SQ_DYN_GPR_CNTL_PS_FLUSH_REQ, REG_FLAG_ENABLE_ALWAYS | REG_FLAG_FLUSH_CHANGE, 0, 0},
	{R_009508_TA_CNTL_AUX, REG_FLAG_ENABLE_ALWAYS | REG_FLAG_FLUSH_CHANGE, 0, 0},
	{R_009714_VC_ENHANCE, REG_FLAG_ENABLE_ALWAYS | REG_FLAG_FLUSH_CHANGE, 0, 0},
	{R_009830_DB_DEBUG, REG_FLAG_ENABLE_ALWAYS | REG_FLAG_FLUSH_CHANGE, 0, 0},
	{R_009838_DB_WATERMARKS, REG_FLAG_ENABLE_ALWAYS | REG_FLAG_FLUSH_CHANGE, 0, 0},
};

static const struct r600_reg r600_ctl_const_list[] = {
	{R_03CFF0_SQ_VTX_BASE_VTX_LOC, 0, 0, 0},
	{R_03CFF4_SQ_VTX_START_INST_LOC, 0, 0, 0},
};

static const struct r600_reg r600_context_reg_list[] = {
	{R_028350_SX_MISC, 0, 0, 0},
	{R_0286C8_SPI_THREAD_GROUPING, 0, 0, 0},
	{R_0288A8_SQ_ESGS_RING_ITEMSIZE, 0, 0, 0},
	{R_0288AC_SQ_GSVS_RING_ITEMSIZE, 0, 0, 0},
	{R_0288B0_SQ_ESTMP_RING_ITEMSIZE, 0, 0, 0},
	{R_0288B4_SQ_GSTMP_RING_ITEMSIZE, 0, 0, 0},
	{R_0288B8_SQ_VSTMP_RING_ITEMSIZE, 0, 0, 0},
	{R_0288BC_SQ_PSTMP_RING_ITEMSIZE, 0, 0, 0},
	{R_0288C0_SQ_FBUF_RING_ITEMSIZE, 0, 0, 0},
	{R_0288C4_SQ_REDUC_RING_ITEMSIZE, 0, 0, 0},
	{R_0288C8_SQ_GS_VERT_ITEMSIZE, 0, 0, 0},
	{R_028A10_VGT_OUTPUT_PATH_CNTL, 0, 0, 0},
	{R_028A14_VGT_HOS_CNTL, 0, 0, 0},
	{R_028A18_VGT_HOS_MAX_TESS_LEVEL, 0, 0, 0},
	{R_028A1C_VGT_HOS_MIN_TESS_LEVEL, 0, 0, 0},
	{R_028A20_VGT_HOS_REUSE_DEPTH, 0, 0, 0},
	{R_028A24_VGT_GROUP_PRIM_TYPE, 0, 0, 0},
	{R_028A28_VGT_GROUP_FIRST_DECR, 0, 0, 0},
	{R_028A2C_VGT_GROUP_DECR, 0, 0, 0},
	{R_028A30_VGT_GROUP_VECT_0_CNTL, 0, 0, 0},
	{R_028A34_VGT_GROUP_VECT_1_CNTL, 0, 0, 0},
	{R_028A38_VGT_GROUP_VECT_0_FMT_CNTL, 0, 0, 0},
	{R_028A3C_VGT_GROUP_VECT_1_FMT_CNTL, 0, 0, 0},
	{R_028A40_VGT_GS_MODE, 0, 0, 0},
	{R_028A4C_PA_SC_MODE_CNTL, 0, 0, 0},
	{R_028AB0_VGT_STRMOUT_EN, 0, 0, 0},
	{R_028AB4_VGT_REUSE_OFF, 0, 0, 0},
	{R_028AB8_VGT_VTX_CNT_EN, 0, 0, 0},
	{R_028B20_VGT_STRMOUT_BUFFER_EN, 0, 0, 0},
	{R_028028_DB_STENCIL_CLEAR, 0, 0, 0},
	{R_02802C_DB_DEPTH_CLEAR, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_028040_CB_COLOR0_BASE, REG_FLAG_NEED_BO|REG_FLAG_RV6XX_SBU, SURFACE_BASE_UPDATE_COLOR(0), 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280A0_CB_COLOR0_INFO, REG_FLAG_NEED_BO, 0, 0xFFFFFFFF},
	{R_028060_CB_COLOR0_SIZE, 0, 0, 0},
	{R_028080_CB_COLOR0_VIEW, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280E0_CB_COLOR0_FRAG, REG_FLAG_NEED_BO, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280C0_CB_COLOR0_TILE, REG_FLAG_NEED_BO, 0, 0},
	{R_028100_CB_COLOR0_MASK, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_028044_CB_COLOR1_BASE, REG_FLAG_NEED_BO|REG_FLAG_RV6XX_SBU, SURFACE_BASE_UPDATE_COLOR(1), 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280A4_CB_COLOR1_INFO, REG_FLAG_NEED_BO, 0, 0xFFFFFFFF},
	{R_028064_CB_COLOR1_SIZE, 0, 0, 0},
	{R_028084_CB_COLOR1_VIEW, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280E4_CB_COLOR1_FRAG, REG_FLAG_NEED_BO, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280C4_CB_COLOR1_TILE, REG_FLAG_NEED_BO, 0, 0},
	{R_028104_CB_COLOR1_MASK, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_028048_CB_COLOR2_BASE, REG_FLAG_NEED_BO|REG_FLAG_RV6XX_SBU, SURFACE_BASE_UPDATE_COLOR(2), 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280A8_CB_COLOR2_INFO, REG_FLAG_NEED_BO, 0, 0xFFFFFFFF},
	{R_028068_CB_COLOR2_SIZE, 0, 0, 0},
	{R_028088_CB_COLOR2_VIEW, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280E8_CB_COLOR2_FRAG, REG_FLAG_NEED_BO, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280C8_CB_COLOR2_TILE, REG_FLAG_NEED_BO, 0, 0},
	{R_028108_CB_COLOR2_MASK, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_02804C_CB_COLOR3_BASE, REG_FLAG_NEED_BO|REG_FLAG_RV6XX_SBU, SURFACE_BASE_UPDATE_COLOR(3), 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280AC_CB_COLOR3_INFO, REG_FLAG_NEED_BO, 0, 0xFFFFFFFF},
	{R_02806C_CB_COLOR3_SIZE, 0, 0, 0},
	{R_02808C_CB_COLOR3_VIEW, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280EC_CB_COLOR3_FRAG, REG_FLAG_NEED_BO, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280CC_CB_COLOR3_TILE, REG_FLAG_NEED_BO, 0, 0},
	{R_02810C_CB_COLOR3_MASK, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_028050_CB_COLOR4_BASE, REG_FLAG_NEED_BO|REG_FLAG_RV6XX_SBU, SURFACE_BASE_UPDATE_COLOR(4), 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280B0_CB_COLOR4_INFO, REG_FLAG_NEED_BO, 0, 0xFFFFFFFF},
	{R_028070_CB_COLOR4_SIZE, 0, 0, 0},
	{R_028090_CB_COLOR4_VIEW, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280F0_CB_COLOR4_FRAG, REG_FLAG_NEED_BO, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280D0_CB_COLOR4_TILE, REG_FLAG_NEED_BO, 0, 0},
	{R_028110_CB_COLOR4_MASK, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_028054_CB_COLOR5_BASE, REG_FLAG_NEED_BO|REG_FLAG_RV6XX_SBU, SURFACE_BASE_UPDATE_COLOR(5), 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280B4_CB_COLOR5_INFO, REG_FLAG_NEED_BO, 0, 0xFFFFFFFF},
	{R_028074_CB_COLOR5_SIZE, 0, 0, 0},
	{R_028094_CB_COLOR5_VIEW, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280F4_CB_COLOR5_FRAG, REG_FLAG_NEED_BO, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280D4_CB_COLOR5_TILE, REG_FLAG_NEED_BO, 0, 0},
	{R_028114_CB_COLOR5_MASK, 0, 0, 0},
	{R_028058_CB_COLOR6_BASE, REG_FLAG_NEED_BO|REG_FLAG_RV6XX_SBU, SURFACE_BASE_UPDATE_COLOR(6), 0},
	{R_0280B8_CB_COLOR6_INFO, REG_FLAG_NEED_BO, 0, 0xFFFFFFFF},
	{R_028078_CB_COLOR6_SIZE, 0, 0, 0},
	{R_028098_CB_COLOR6_VIEW, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280F8_CB_COLOR6_FRAG, REG_FLAG_NEED_BO, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280D8_CB_COLOR6_TILE, REG_FLAG_NEED_BO, 0, 0},
	{R_028118_CB_COLOR6_MASK, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_02805C_CB_COLOR7_BASE, REG_FLAG_NEED_BO|REG_FLAG_RV6XX_SBU, SURFACE_BASE_UPDATE_COLOR(7), 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0280BC_CB_COLOR7_INFO, REG_FLAG_NEED_BO, 0, 0xFFFFFFFF},
	{R_02807C_CB_COLOR7_SIZE, 0, 0, 0},
	{R_02809C_CB_COLOR7_VIEW, 0, 0, 0},
	{R_0280FC_CB_COLOR7_FRAG, REG_FLAG_NEED_BO, 0, 0},
	{R_0280DC_CB_COLOR7_TILE, REG_FLAG_NEED_BO, 0, 0},
	{R_02811C_CB_COLOR7_MASK, 0, 0, 0},
	{R_028120_CB_CLEAR_RED, 0, 0, 0},
	{R_028124_CB_CLEAR_GREEN, 0, 0, 0},
	{R_028128_CB_CLEAR_BLUE, 0, 0, 0},
	{R_02812C_CB_CLEAR_ALPHA, 0, 0, 0},
	{R_028140_ALU_CONST_BUFFER_SIZE_PS_0, REG_FLAG_DIRTY_ALWAYS, 0, 0},
	{R_028180_ALU_CONST_BUFFER_SIZE_VS_0, REG_FLAG_DIRTY_ALWAYS, 0, 0},
	{R_028940_ALU_CONST_CACHE_PS_0, REG_FLAG_NEED_BO, S_0085F0_SH_ACTION_ENA(1), 0xFFFFFFFF},
	{R_028980_ALU_CONST_CACHE_VS_0, REG_FLAG_NEED_BO, S_0085F0_SH_ACTION_ENA(1), 0xFFFFFFFF},
	{R_02823C_CB_SHADER_MASK, 0, 0, 0},
	{R_028238_CB_TARGET_MASK, 0, 0, 0},
	{R_028410_SX_ALPHA_TEST_CONTROL, 0, 0, 0},
	{R_028414_CB_BLEND_RED, 0, 0, 0},
	{R_028418_CB_BLEND_GREEN, 0, 0, 0},
	{R_02841C_CB_BLEND_BLUE, 0, 0, 0},
	{R_028420_CB_BLEND_ALPHA, 0, 0, 0},
	{R_028424_CB_FOG_RED, 0, 0, 0},
	{R_028428_CB_FOG_GREEN, 0, 0, 0},
	{R_02842C_CB_FOG_BLUE, 0, 0, 0},
	{R_028430_DB_STENCILREFMASK, 0, 0, 0},
	{R_028434_DB_STENCILREFMASK_BF, 0, 0, 0},
	{R_028438_SX_ALPHA_REF, 0, 0, 0},
	{R_0286DC_SPI_FOG_CNTL, 0, 0, 0},
	{R_0286E0_SPI_FOG_FUNC_SCALE, 0, 0, 0},
	{R_0286E4_SPI_FOG_FUNC_BIAS, 0, 0, 0},
	{R_028780_CB_BLEND0_CONTROL, REG_FLAG_NOT_R600, 0, 0},
	{R_028784_CB_BLEND1_CONTROL, REG_FLAG_NOT_R600, 0, 0},
	{R_028788_CB_BLEND2_CONTROL, REG_FLAG_NOT_R600, 0, 0},
	{R_02878C_CB_BLEND3_CONTROL, REG_FLAG_NOT_R600, 0, 0},
	{R_028790_CB_BLEND4_CONTROL, REG_FLAG_NOT_R600, 0, 0},
	{R_028794_CB_BLEND5_CONTROL, REG_FLAG_NOT_R600, 0, 0},
	{R_028798_CB_BLEND6_CONTROL, REG_FLAG_NOT_R600, 0, 0},
	{R_02879C_CB_BLEND7_CONTROL, REG_FLAG_NOT_R600, 0, 0},
	{R_0287A0_CB_SHADER_CONTROL, 0, 0, 0},
	{R_028800_DB_DEPTH_CONTROL, 0, 0, 0},
	{R_028804_CB_BLEND_CONTROL, 0, 0, 0},
	{R_028808_CB_COLOR_CONTROL, 0, 0, 0},
	{R_02880C_DB_SHADER_CONTROL, 0, 0, 0},
	{R_028C04_PA_SC_AA_CONFIG, 0, 0, 0},
	{R_028C1C_PA_SC_AA_SAMPLE_LOCS_MCTX, 0, 0, 0},
	{R_028C20_PA_SC_AA_SAMPLE_LOCS_8S_WD1_MCTX, 0, 0, 0},
	{R_028C30_CB_CLRCMP_CONTROL, 0, 0, 0},
	{R_028C34_CB_CLRCMP_SRC, 0, 0, 0},
	{R_028C38_CB_CLRCMP_DST, 0, 0, 0},
	{R_028C3C_CB_CLRCMP_MSK, 0, 0, 0},
	{R_028C48_PA_SC_AA_MASK, 0, 0, 0},
	{R_028D2C_DB_SRESULTS_COMPARE_STATE1, 0, 0, 0},
	{R_028D44_DB_ALPHA_TO_MASK, 0, 0, 0},
	{R_02800C_DB_DEPTH_BASE, REG_FLAG_NEED_BO|REG_FLAG_RV6XX_SBU, SURFACE_BASE_UPDATE_DEPTH, 0},
	{R_028000_DB_DEPTH_SIZE, 0, 0, 0},
	{R_028004_DB_DEPTH_VIEW, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_028010_DB_DEPTH_INFO, REG_FLAG_NEED_BO, 0, 0},
	{R_028D0C_DB_RENDER_CONTROL, 0, 0, 0},
	{R_028D10_DB_RENDER_OVERRIDE, 0, 0, 0},
	{R_028D24_DB_HTILE_SURFACE, 0, 0, 0},
	{R_028D30_DB_PRELOAD_CONTROL, 0, 0, 0},
	{R_028D34_DB_PREFETCH_LIMIT, 0, 0, 0},
	{R_028030_PA_SC_SCREEN_SCISSOR_TL, 0, 0, 0},
	{R_028034_PA_SC_SCREEN_SCISSOR_BR, 0, 0, 0},
	{R_028200_PA_SC_WINDOW_OFFSET, 0, 0, 0},
	{R_028204_PA_SC_WINDOW_SCISSOR_TL, 0, 0, 0},
	{R_028208_PA_SC_WINDOW_SCISSOR_BR, 0, 0, 0},
	{R_02820C_PA_SC_CLIPRECT_RULE, 0, 0, 0},
	{R_028210_PA_SC_CLIPRECT_0_TL, 0, 0, 0},
	{R_028214_PA_SC_CLIPRECT_0_BR, 0, 0, 0},
	{R_028218_PA_SC_CLIPRECT_1_TL, 0, 0, 0},
	{R_02821C_PA_SC_CLIPRECT_1_BR, 0, 0, 0},
	{R_028220_PA_SC_CLIPRECT_2_TL, 0, 0, 0},
	{R_028224_PA_SC_CLIPRECT_2_BR, 0, 0, 0},
	{R_028228_PA_SC_CLIPRECT_3_TL, 0, 0, 0},
	{R_02822C_PA_SC_CLIPRECT_3_BR, 0, 0, 0},
	{R_028230_PA_SC_EDGERULE, 0, 0, 0},
	{R_028240_PA_SC_GENERIC_SCISSOR_TL, 0, 0, 0},
	{R_028244_PA_SC_GENERIC_SCISSOR_BR, 0, 0, 0},
	{R_028250_PA_SC_VPORT_SCISSOR_0_TL, 0, 0, 0},
	{R_028254_PA_SC_VPORT_SCISSOR_0_BR, 0, 0, 0},
	{R_0282D0_PA_SC_VPORT_ZMIN_0, 0, 0, 0},
	{R_0282D4_PA_SC_VPORT_ZMAX_0, 0, 0, 0},
	{R_02843C_PA_CL_VPORT_XSCALE_0, 0, 0, 0},
	{R_028440_PA_CL_VPORT_XOFFSET_0, 0, 0, 0},
	{R_028444_PA_CL_VPORT_YSCALE_0, 0, 0, 0},
	{R_028448_PA_CL_VPORT_YOFFSET_0, 0, 0, 0},
	{R_02844C_PA_CL_VPORT_ZSCALE_0, 0, 0, 0},
	{R_028450_PA_CL_VPORT_ZOFFSET_0, 0, 0, 0},
	{R_0286D4_SPI_INTERP_CONTROL_0, 0, 0, 0},
	{R_028810_PA_CL_CLIP_CNTL, 0, 0, 0},
	{R_028814_PA_SU_SC_MODE_CNTL, 0, 0, 0},
	{R_028818_PA_CL_VTE_CNTL, 0, 0, 0},
	{R_02881C_PA_CL_VS_OUT_CNTL, 0, 0, 0},
	{R_028820_PA_CL_NANINF_CNTL, 0, 0, 0},
	{R_028A00_PA_SU_POINT_SIZE, 0, 0, 0},
	{R_028A04_PA_SU_POINT_MINMAX, 0, 0, 0},
	{R_028A08_PA_SU_LINE_CNTL, 0, 0, 0},
	{R_028A0C_PA_SC_LINE_STIPPLE, 0, 0, 0},
	{R_028A48_PA_SC_MPASS_PS_CNTL, 0, 0, 0},
	{R_028C00_PA_SC_LINE_CNTL, 0, 0, 0},
	{R_028C08_PA_SU_VTX_CNTL, 0, 0, 0},
	{R_028C0C_PA_CL_GB_VERT_CLIP_ADJ, 0, 0, 0},
	{R_028C10_PA_CL_GB_VERT_DISC_ADJ, 0, 0, 0},
	{R_028C14_PA_CL_GB_HORZ_CLIP_ADJ, 0, 0, 0},
	{R_028C18_PA_CL_GB_HORZ_DISC_ADJ, 0, 0, 0},
	{R_028DF8_PA_SU_POLY_OFFSET_DB_FMT_CNTL, 0, 0, 0},
	{R_028DFC_PA_SU_POLY_OFFSET_CLAMP, 0, 0, 0},
	{R_028E00_PA_SU_POLY_OFFSET_FRONT_SCALE, 0, 0, 0},
	{R_028E04_PA_SU_POLY_OFFSET_FRONT_OFFSET, 0, 0, 0},
	{R_028E08_PA_SU_POLY_OFFSET_BACK_SCALE, 0, 0, 0},
	{R_028E0C_PA_SU_POLY_OFFSET_BACK_OFFSET, 0, 0, 0},
	{R_028E20_PA_CL_UCP0_X, 0, 0, 0},
	{R_028E24_PA_CL_UCP0_Y, 0, 0, 0},
	{R_028E28_PA_CL_UCP0_Z, 0, 0, 0},
	{R_028E2C_PA_CL_UCP0_W, 0, 0, 0},
	{R_028E30_PA_CL_UCP1_X, 0, 0, 0},
	{R_028E34_PA_CL_UCP1_Y, 0, 0, 0},
	{R_028E38_PA_CL_UCP1_Z, 0, 0, 0},
	{R_028E3C_PA_CL_UCP1_W, 0, 0, 0},
	{R_028E40_PA_CL_UCP2_X, 0, 0, 0},
	{R_028E44_PA_CL_UCP2_Y, 0, 0, 0},
	{R_028E48_PA_CL_UCP2_Z, 0, 0, 0},
	{R_028E4C_PA_CL_UCP2_W, 0, 0, 0},
	{R_028E50_PA_CL_UCP3_X, 0, 0, 0},
	{R_028E54_PA_CL_UCP3_Y, 0, 0, 0},
	{R_028E58_PA_CL_UCP3_Z, 0, 0, 0},
	{R_028E5C_PA_CL_UCP3_W, 0, 0, 0},
	{R_028E60_PA_CL_UCP4_X, 0, 0, 0},
	{R_028E64_PA_CL_UCP4_Y, 0, 0, 0},
	{R_028E68_PA_CL_UCP4_Z, 0, 0, 0},
	{R_028E6C_PA_CL_UCP4_W, 0, 0, 0},
	{R_028E70_PA_CL_UCP5_X, 0, 0, 0},
	{R_028E74_PA_CL_UCP5_Y, 0, 0, 0},
	{R_028E78_PA_CL_UCP5_Z, 0, 0, 0},
	{R_028E7C_PA_CL_UCP5_W, 0, 0, 0},
	{R_028380_SQ_VTX_SEMANTIC_0, 0, 0, 0},
	{R_028384_SQ_VTX_SEMANTIC_1, 0, 0, 0},
	{R_028388_SQ_VTX_SEMANTIC_2, 0, 0, 0},
	{R_02838C_SQ_VTX_SEMANTIC_3, 0, 0, 0},
	{R_028390_SQ_VTX_SEMANTIC_4, 0, 0, 0},
	{R_028394_SQ_VTX_SEMANTIC_5, 0, 0, 0},
	{R_028398_SQ_VTX_SEMANTIC_6, 0, 0, 0},
	{R_02839C_SQ_VTX_SEMANTIC_7, 0, 0, 0},
	{R_0283A0_SQ_VTX_SEMANTIC_8, 0, 0, 0},
	{R_0283A4_SQ_VTX_SEMANTIC_9, 0, 0, 0},
	{R_0283A8_SQ_VTX_SEMANTIC_10, 0, 0, 0},
	{R_0283AC_SQ_VTX_SEMANTIC_11, 0, 0, 0},
	{R_0283B0_SQ_VTX_SEMANTIC_12, 0, 0, 0},
	{R_0283B4_SQ_VTX_SEMANTIC_13, 0, 0, 0},
	{R_0283B8_SQ_VTX_SEMANTIC_14, 0, 0, 0},
	{R_0283BC_SQ_VTX_SEMANTIC_15, 0, 0, 0},
	{R_0283C0_SQ_VTX_SEMANTIC_16, 0, 0, 0},
	{R_0283C4_SQ_VTX_SEMANTIC_17, 0, 0, 0},
	{R_0283C8_SQ_VTX_SEMANTIC_18, 0, 0, 0},
	{R_0283CC_SQ_VTX_SEMANTIC_19, 0, 0, 0},
	{R_0283D0_SQ_VTX_SEMANTIC_20, 0, 0, 0},
	{R_0283D4_SQ_VTX_SEMANTIC_21, 0, 0, 0},
	{R_0283D8_SQ_VTX_SEMANTIC_22, 0, 0, 0},
	{R_0283DC_SQ_VTX_SEMANTIC_23, 0, 0, 0},
	{R_0283E0_SQ_VTX_SEMANTIC_24, 0, 0, 0},
	{R_0283E4_SQ_VTX_SEMANTIC_25, 0, 0, 0},
	{R_0283E8_SQ_VTX_SEMANTIC_26, 0, 0, 0},
	{R_0283EC_SQ_VTX_SEMANTIC_27, 0, 0, 0},
	{R_0283F0_SQ_VTX_SEMANTIC_28, 0, 0, 0},
	{R_0283F4_SQ_VTX_SEMANTIC_29, 0, 0, 0},
	{R_0283F8_SQ_VTX_SEMANTIC_30, 0, 0, 0},
	{R_0283FC_SQ_VTX_SEMANTIC_31, 0, 0, 0},
	{R_028614_SPI_VS_OUT_ID_0, 0, 0, 0},
	{R_028618_SPI_VS_OUT_ID_1, 0, 0, 0},
	{R_02861C_SPI_VS_OUT_ID_2, 0, 0, 0},
	{R_028620_SPI_VS_OUT_ID_3, 0, 0, 0},
	{R_028624_SPI_VS_OUT_ID_4, 0, 0, 0},
	{R_028628_SPI_VS_OUT_ID_5, 0, 0, 0},
	{R_02862C_SPI_VS_OUT_ID_6, 0, 0, 0},
	{R_028630_SPI_VS_OUT_ID_7, 0, 0, 0},
	{R_028634_SPI_VS_OUT_ID_8, 0, 0, 0},
	{R_028638_SPI_VS_OUT_ID_9, 0, 0, 0},
	{R_0286C4_SPI_VS_OUT_CONFIG, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_028858_SQ_PGM_START_VS, REG_FLAG_NEED_BO, S_0085F0_SH_ACTION_ENA(1), 0xFFFFFFFF},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_028868_SQ_PGM_RESOURCES_VS, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_028894_SQ_PGM_START_FS, REG_FLAG_NEED_BO, S_0085F0_SH_ACTION_ENA(1), 0xFFFFFFFF},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_0288A4_SQ_PGM_RESOURCES_FS, 0, 0, 0},
	{R_0288D0_SQ_PGM_CF_OFFSET_VS, 0, 0, 0},
	{R_0288DC_SQ_PGM_CF_OFFSET_FS, 0, 0, 0},
	{R_028644_SPI_PS_INPUT_CNTL_0, 0, 0, 0},
	{R_028648_SPI_PS_INPUT_CNTL_1, 0, 0, 0},
	{R_02864C_SPI_PS_INPUT_CNTL_2, 0, 0, 0},
	{R_028650_SPI_PS_INPUT_CNTL_3, 0, 0, 0},
	{R_028654_SPI_PS_INPUT_CNTL_4, 0, 0, 0},
	{R_028658_SPI_PS_INPUT_CNTL_5, 0, 0, 0},
	{R_02865C_SPI_PS_INPUT_CNTL_6, 0, 0, 0},
	{R_028660_SPI_PS_INPUT_CNTL_7, 0, 0, 0},
	{R_028664_SPI_PS_INPUT_CNTL_8, 0, 0, 0},
	{R_028668_SPI_PS_INPUT_CNTL_9, 0, 0, 0},
	{R_02866C_SPI_PS_INPUT_CNTL_10, 0, 0, 0},
	{R_028670_SPI_PS_INPUT_CNTL_11, 0, 0, 0},
	{R_028674_SPI_PS_INPUT_CNTL_12, 0, 0, 0},
	{R_028678_SPI_PS_INPUT_CNTL_13, 0, 0, 0},
	{R_02867C_SPI_PS_INPUT_CNTL_14, 0, 0, 0},
	{R_028680_SPI_PS_INPUT_CNTL_15, 0, 0, 0},
	{R_028684_SPI_PS_INPUT_CNTL_16, 0, 0, 0},
	{R_028688_SPI_PS_INPUT_CNTL_17, 0, 0, 0},
	{R_02868C_SPI_PS_INPUT_CNTL_18, 0, 0, 0},
	{R_028690_SPI_PS_INPUT_CNTL_19, 0, 0, 0},
	{R_028694_SPI_PS_INPUT_CNTL_20, 0, 0, 0},
	{R_028698_SPI_PS_INPUT_CNTL_21, 0, 0, 0},
	{R_02869C_SPI_PS_INPUT_CNTL_22, 0, 0, 0},
	{R_0286A0_SPI_PS_INPUT_CNTL_23, 0, 0, 0},
	{R_0286A4_SPI_PS_INPUT_CNTL_24, 0, 0, 0},
	{R_0286A8_SPI_PS_INPUT_CNTL_25, 0, 0, 0},
	{R_0286AC_SPI_PS_INPUT_CNTL_26, 0, 0, 0},
	{R_0286B0_SPI_PS_INPUT_CNTL_27, 0, 0, 0},
	{R_0286B4_SPI_PS_INPUT_CNTL_28, 0, 0, 0},
	{R_0286B8_SPI_PS_INPUT_CNTL_29, 0, 0, 0},
	{R_0286BC_SPI_PS_INPUT_CNTL_30, 0, 0, 0},
	{R_0286C0_SPI_PS_INPUT_CNTL_31, 0, 0, 0},
	{R_0286CC_SPI_PS_IN_CONTROL_0, 0, 0, 0},
	{R_0286D0_SPI_PS_IN_CONTROL_1, 0, 0, 0},
	{R_0286D8_SPI_INPUT_Z, 0, 0, 0},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_028840_SQ_PGM_START_PS, REG_FLAG_NEED_BO, S_0085F0_SH_ACTION_ENA(1), 0xFFFFFFFF},
	{GROUP_FORCE_NEW_BLOCK, 0, 0, 0},
	{R_028850_SQ_PGM_RESOURCES_PS, 0, 0, 0},
	{R_028854_SQ_PGM_EXPORTS_PS, 0, 0, 0},
	{R_0288CC_SQ_PGM_CF_OFFSET_PS, 0, 0, 0},
	{R_028400_VGT_MAX_VTX_INDX, 0, 0, 0},
	{R_028404_VGT_MIN_VTX_INDX, 0, 0, 0},
	{R_028408_VGT_INDX_OFFSET, 0, 0, 0},
	{R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX, 0, 0, 0},
	{R_028A84_VGT_PRIMITIVEID_EN, 0, 0, 0},
	{R_028A94_VGT_MULTI_PRIM_IB_RESET_EN, 0, 0, 0},
	{R_028AA0_VGT_INSTANCE_STEP_RATE_0, 0, 0, 0},
	{R_028AA4_VGT_INSTANCE_STEP_RATE_1, 0, 0, 0},
};

/* SHADER RESOURCE R600/R700 */
int r600_resource_init(struct r600_context *ctx, struct r600_range *range, unsigned offset, unsigned nblocks, unsigned stride, struct r600_reg *reg, int nreg, unsigned offset_base)
{
	int i;
	struct r600_block *block;
	range->blocks = calloc(nblocks, sizeof(struct r600_block *));
	if (range->blocks == NULL)
		return -ENOMEM;

	reg[0].offset += offset;
	for (i = 0; i < nblocks; i++) {
		block = calloc(1, sizeof(struct r600_block));
		if (block == NULL) {
			return -ENOMEM;
		}
		ctx->nblocks++;
		range->blocks[i] = block;
		r600_init_block(ctx, block, reg, 0, nreg, PKT3_SET_RESOURCE, offset_base);

		reg[0].offset += stride;
	}
	return 0;
}

      
static int r600_resource_range_init(struct r600_context *ctx, struct r600_range *range, unsigned offset, unsigned nblocks, unsigned stride)
{
	struct r600_reg r600_shader_resource[] = {
		{R_038000_RESOURCE0_WORD0, REG_FLAG_NEED_BO, S_0085F0_TC_ACTION_ENA(1) | S_0085F0_VC_ACTION_ENA(1), 0xFFFFFFFF},
		{R_038004_RESOURCE0_WORD1, REG_FLAG_NEED_BO, S_0085F0_TC_ACTION_ENA(1) | S_0085F0_VC_ACTION_ENA(1), 0xFFFFFFFF},
		{R_038008_RESOURCE0_WORD2, 0, 0, 0},
		{R_03800C_RESOURCE0_WORD3, 0, 0, 0},
		{R_038010_RESOURCE0_WORD4, 0, 0, 0},
		{R_038014_RESOURCE0_WORD5, 0, 0, 0},
		{R_038018_RESOURCE0_WORD6, 0, 0, 0},
	};
	unsigned nreg = Elements(r600_shader_resource);

	return r600_resource_init(ctx, range, offset, nblocks, stride, r600_shader_resource, nreg, R600_RESOURCE_OFFSET);
}

/* SHADER SAMPLER R600/R700 */
static int r600_state_sampler_init(struct r600_context *ctx, u32 offset)
{
	struct r600_reg r600_shader_sampler[] = {
		{R_03C000_SQ_TEX_SAMPLER_WORD0_0, 0, 0, 0},
		{R_03C004_SQ_TEX_SAMPLER_WORD1_0, 0, 0, 0},
		{R_03C008_SQ_TEX_SAMPLER_WORD2_0, 0, 0, 0},
	};
	unsigned nreg = Elements(r600_shader_sampler);

	for (int i = 0; i < nreg; i++) {
		r600_shader_sampler[i].offset += offset;
	}
	return r600_context_add_block(ctx, r600_shader_sampler, nreg, PKT3_SET_SAMPLER, R600_SAMPLER_OFFSET);
}

/* SHADER SAMPLER BORDER R600/R700 */
static int r600_state_sampler_border_init(struct r600_context *ctx, u32 offset)
{
	struct r600_reg r600_shader_sampler_border[] = {
		{R_00A400_TD_PS_SAMPLER0_BORDER_RED, 0, 0, 0},
		{R_00A404_TD_PS_SAMPLER0_BORDER_GREEN, 0, 0, 0},
		{R_00A408_TD_PS_SAMPLER0_BORDER_BLUE, 0, 0, 0},
		{R_00A40C_TD_PS_SAMPLER0_BORDER_ALPHA, 0, 0, 0},
	};
	unsigned nreg = Elements(r600_shader_sampler_border);

	for (int i = 0; i < nreg; i++) {
		r600_shader_sampler_border[i].offset += offset;
	}
	return r600_context_add_block(ctx, r600_shader_sampler_border, nreg, PKT3_SET_CONFIG_REG, R600_CONFIG_REG_OFFSET);
}

static int r600_loop_const_init(struct r600_context *ctx, u32 offset)
{
	unsigned nreg = 32;
	struct r600_reg r600_loop_consts[32];
	int i;

	for (i = 0; i < nreg; i++) {
		r600_loop_consts[i].offset = R600_LOOP_CONST_OFFSET + ((offset + i) * 4);
		r600_loop_consts[i].flags = REG_FLAG_DIRTY_ALWAYS;
		r600_loop_consts[i].flush_flags = 0;
		r600_loop_consts[i].flush_mask = 0;
	}
	return r600_context_add_block(ctx, r600_loop_consts, nreg, PKT3_SET_LOOP_CONST, R600_LOOP_CONST_OFFSET);
}

static void r600_context_clear_fenced_bo(struct r600_context *ctx)
{
	struct radeon_bo *bo, *tmp;

	LIST_FOR_EACH_ENTRY_SAFE(bo, tmp, &ctx->fenced_bo, fencedlist) {
		LIST_DELINIT(&bo->fencedlist);
		bo->fence = 0;
		bo->ctx = NULL;
	}
}

static void r600_free_resource_range(struct r600_context *ctx, struct r600_range *range, int nblocks)
{
	struct r600_block *block;
	int i;
	for (i = 0; i < nblocks; i++) {
		block = range->blocks[i];
		if (block) {
			for (int k = 1; k <= block->nbo; k++)
				r600_bo_reference(ctx->radeon, &block->reloc[k].bo, NULL);
			free(block);
		}
	}
	free(range->blocks);

}

/* initialize */
void r600_context_fini(struct r600_context *ctx)
{
	struct r600_block *block;
	struct r600_range *range;

	for (int i = 0; i < NUM_RANGES; i++) {
		if (!ctx->range[i].blocks)
			continue;
		for (int j = 0; j < (1 << HASH_SHIFT); j++) {
			block = ctx->range[i].blocks[j];
			if (block) {
				for (int k = 0, offset = block->start_offset; k < block->nreg; k++, offset += 4) {
					range = &ctx->range[CTX_RANGE_ID(offset)];
					range->blocks[CTX_BLOCK_ID(offset)] = NULL;
				}
				for (int k = 1; k <= block->nbo; k++) {
					r600_bo_reference(ctx->radeon, &block->reloc[k].bo, NULL);
				}
				free(block);
			}
		}
		free(ctx->range[i].blocks);
	}
	r600_free_resource_range(ctx, &ctx->ps_resources, ctx->num_ps_resources);
	r600_free_resource_range(ctx, &ctx->vs_resources, ctx->num_vs_resources);
	r600_free_resource_range(ctx, &ctx->fs_resources, ctx->num_fs_resources);
	free(ctx->range);
	free(ctx->blocks);
	free(ctx->reloc);
	free(ctx->bo);
	free(ctx->pm4);

	r600_context_clear_fenced_bo(ctx);
	memset(ctx, 0, sizeof(struct r600_context));
}

static void r600_add_resource_block(struct r600_context *ctx, struct r600_range *range, int num_blocks, int *index)
{
	int c = *index;
	for (int j = 0; j < num_blocks; j++) {
		if (!range->blocks[j])
			continue;

		ctx->blocks[c++] = range->blocks[j];
	}
	*index = c;
}

int r600_setup_block_table(struct r600_context *ctx)
{
	/* setup block table */
	int c = 0;
	ctx->blocks = calloc(ctx->nblocks, sizeof(void*));
	if (!ctx->blocks)
		return -ENOMEM;
	for (int i = 0; i < NUM_RANGES; i++) {
		if (!ctx->range[i].blocks)
			continue;
		for (int j = 0, add; j < (1 << HASH_SHIFT); j++) {
			if (!ctx->range[i].blocks[j])
				continue;

			add = 1;
			for (int k = 0; k < c; k++) {
				if (ctx->blocks[k] == ctx->range[i].blocks[j]) {
					add = 0;
					break;
				}
			}
			if (add) {
				assert(c < ctx->nblocks);
				ctx->blocks[c++] = ctx->range[i].blocks[j];
				j += (ctx->range[i].blocks[j]->nreg) - 1;
			}
		}
	}

	r600_add_resource_block(ctx, &ctx->ps_resources, ctx->num_ps_resources, &c);
	r600_add_resource_block(ctx, &ctx->vs_resources, ctx->num_vs_resources, &c);
	r600_add_resource_block(ctx, &ctx->fs_resources, ctx->num_fs_resources, &c);
	return 0;
}

int r600_context_init(struct r600_context *ctx, struct radeon *radeon)
{
	int r;

	memset(ctx, 0, sizeof(struct r600_context));
	ctx->radeon = radeon;
	LIST_INITHEAD(&ctx->query_list);

	/* init dirty list */
	LIST_INITHEAD(&ctx->dirty);
	LIST_INITHEAD(&ctx->resource_dirty);
	LIST_INITHEAD(&ctx->enable_list);

	ctx->range = calloc(NUM_RANGES, sizeof(struct r600_range));
	if (!ctx->range) {
		r = -ENOMEM;
		goto out_err;
	}

	/* add blocks */
	r = r600_context_add_block(ctx, r600_config_reg_list,
				   Elements(r600_config_reg_list), PKT3_SET_CONFIG_REG, R600_CONFIG_REG_OFFSET);
	if (r)
		goto out_err;
	r = r600_context_add_block(ctx, r600_context_reg_list,
				   Elements(r600_context_reg_list), PKT3_SET_CONTEXT_REG, R600_CONTEXT_REG_OFFSET);
	if (r)
		goto out_err;
	r = r600_context_add_block(ctx, r600_ctl_const_list,
				   Elements(r600_ctl_const_list), PKT3_SET_CTL_CONST, R600_CTL_CONST_OFFSET);
	if (r)
		goto out_err;

	/* PS SAMPLER BORDER */
	for (int j = 0, offset = 0; j < 18; j++, offset += 0x10) {
		r = r600_state_sampler_border_init(ctx, offset);
		if (r)
			goto out_err;
	}

	/* VS SAMPLER BORDER */
	for (int j = 0, offset = 0x200; j < 18; j++, offset += 0x10) {
		r = r600_state_sampler_border_init(ctx, offset);
		if (r)
			goto out_err;
	}
	/* PS SAMPLER */
	for (int j = 0, offset = 0; j < 18; j++, offset += 0xC) {
		r = r600_state_sampler_init(ctx, offset);
		if (r)
			goto out_err;
	}
	/* VS SAMPLER */
	for (int j = 0, offset = 0xD8; j < 18; j++, offset += 0xC) {
		r = r600_state_sampler_init(ctx, offset);
		if (r)
			goto out_err;
	}

	ctx->num_ps_resources = 160;
	ctx->num_vs_resources = 160;
	ctx->num_fs_resources = 16;
	r = r600_resource_range_init(ctx, &ctx->ps_resources, 0, 160, 0x1c);
	if (r)
		goto out_err;
	r = r600_resource_range_init(ctx, &ctx->vs_resources, 0x1180, 160, 0x1c);
	if (r)
		goto out_err;
	r = r600_resource_range_init(ctx, &ctx->fs_resources, 0x2300, 16, 0x1c);
	if (r)
		goto out_err;

	/* PS loop const */
	r600_loop_const_init(ctx, 0);
	/* VS loop const */
	r600_loop_const_init(ctx, 32);

	r = r600_setup_block_table(ctx);
	if (r)
		goto out_err;

	/* allocate cs variables */
	ctx->nreloc = RADEON_CTX_MAX_PM4;
	ctx->reloc = calloc(ctx->nreloc, sizeof(struct r600_reloc));
	if (ctx->reloc == NULL) {
		r = -ENOMEM;
		goto out_err;
	}
	ctx->bo = calloc(ctx->nreloc, sizeof(void *));
	if (ctx->bo == NULL) {
		r = -ENOMEM;
		goto out_err;
	}
	ctx->pm4_ndwords = RADEON_CTX_MAX_PM4;
	ctx->pm4 = calloc(ctx->pm4_ndwords, 4);
	if (ctx->pm4 == NULL) {
		r = -ENOMEM;
		goto out_err;
	}

	r600_init_cs(ctx);
	/* save 16dwords space for fence mecanism */
	ctx->pm4_ndwords -= 16;

	LIST_INITHEAD(&ctx->fenced_bo);

	ctx->max_db = 4;

	r600_get_backend_mask(ctx);

	return 0;
out_err:
	r600_context_fini(ctx);
	return r;
}

/* Flushes all surfaces */
void r600_context_flush_all(struct r600_context *ctx, unsigned flush_flags)
{
	unsigned ndwords = 5;

	if ((ctx->pm4_dirty_cdwords + ndwords + ctx->pm4_cdwords) > ctx->pm4_ndwords) {
		/* need to flush */
		r600_context_flush(ctx);
	}

	ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_SURFACE_SYNC, 3, ctx->predicate_drawing);
	ctx->pm4[ctx->pm4_cdwords++] = flush_flags;     /* CP_COHER_CNTL */
	ctx->pm4[ctx->pm4_cdwords++] = 0xffffffff;      /* CP_COHER_SIZE */
	ctx->pm4[ctx->pm4_cdwords++] = 0;               /* CP_COHER_BASE */
	ctx->pm4[ctx->pm4_cdwords++] = 0x0000000A;      /* POLL_INTERVAL */
}

void r600_context_bo_flush(struct r600_context *ctx, unsigned flush_flags,
				unsigned flush_mask, struct r600_bo *rbo)
{
	struct radeon_bo *bo;

	bo = rbo->bo;
	/* if bo has already been flushed */
	if (!(~bo->last_flush & flush_flags)) {
		bo->last_flush &= flush_mask;
		return;
	}

	if ((ctx->radeon->family < CHIP_RV770) &&
	    (G_0085F0_CB_ACTION_ENA(flush_flags) ||
	     G_0085F0_DB_ACTION_ENA(flush_flags))) {
		if (ctx->flags & R600_CONTEXT_CHECK_EVENT_FLUSH) {
			/* the rv670 seems to fail fbo-generatemipmap unless we flush the CB1 dest base ena */
			if ((bo->binding & BO_BOUND_TEXTURE) &&
			    (flush_flags & S_0085F0_CB_ACTION_ENA(1))) {
				if ((ctx->radeon->family == CHIP_RV670) ||
				    (ctx->radeon->family == CHIP_RS780) ||
				    (ctx->radeon->family == CHIP_RS880)) {
					ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_SURFACE_SYNC, 3, ctx->predicate_drawing);
					ctx->pm4[ctx->pm4_cdwords++] = S_0085F0_CB1_DEST_BASE_ENA(1);     /* CP_COHER_CNTL */
					ctx->pm4[ctx->pm4_cdwords++] = 0xffffffff;      /* CP_COHER_SIZE */
					ctx->pm4[ctx->pm4_cdwords++] = 0;               /* CP_COHER_BASE */
					ctx->pm4[ctx->pm4_cdwords++] = 0x0000000A;      /* POLL_INTERVAL */
				}
			}

			ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_EVENT_WRITE, 0, ctx->predicate_drawing);
			ctx->pm4[ctx->pm4_cdwords++] = EVENT_TYPE(EVENT_TYPE_CACHE_FLUSH_AND_INV_EVENT) | EVENT_INDEX(0);
			ctx->flags &= ~R600_CONTEXT_CHECK_EVENT_FLUSH;
		}
	} else {
		ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_SURFACE_SYNC, 3, ctx->predicate_drawing);
		ctx->pm4[ctx->pm4_cdwords++] = flush_flags;
		ctx->pm4[ctx->pm4_cdwords++] = (bo->size + 255) >> 8;
		ctx->pm4[ctx->pm4_cdwords++] = 0x00000000;
		ctx->pm4[ctx->pm4_cdwords++] = 0x0000000A;
		ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_NOP, 0, ctx->predicate_drawing);
		ctx->pm4[ctx->pm4_cdwords++] = bo->reloc_id;
	}
	bo->last_flush = (bo->last_flush | flush_flags) & flush_mask;
}

void r600_context_get_reloc(struct r600_context *ctx, struct r600_bo *rbo)
{
	struct radeon_bo *bo = rbo->bo;
	bo->reloc = &ctx->reloc[ctx->creloc];
	bo->reloc_id = ctx->creloc * sizeof(struct r600_reloc) / 4;
	ctx->reloc[ctx->creloc].handle = bo->handle;
	ctx->reloc[ctx->creloc].read_domain = rbo->domains & (RADEON_GEM_DOMAIN_GTT | RADEON_GEM_DOMAIN_VRAM);
	ctx->reloc[ctx->creloc].write_domain = rbo->domains & (RADEON_GEM_DOMAIN_GTT | RADEON_GEM_DOMAIN_VRAM);
	ctx->reloc[ctx->creloc].flags = 0;
	radeon_bo_reference(ctx->radeon, &ctx->bo[ctx->creloc], bo);
	rbo->fence = ctx->radeon->fence;
	ctx->creloc++;
}

void r600_context_reg(struct r600_context *ctx,
		      unsigned offset, unsigned value,
		      unsigned mask)
{
	struct r600_range *range;
	struct r600_block *block;
	unsigned id;
	unsigned new_val;
	int dirty;

	range = &ctx->range[CTX_RANGE_ID(offset)];
	block = range->blocks[CTX_BLOCK_ID(offset)];
	id = (offset - block->start_offset) >> 2;

	dirty = block->status & R600_BLOCK_STATUS_DIRTY;

	new_val = block->reg[id];
	new_val &= ~mask;
	new_val |= value;
	if (new_val != block->reg[id]) {
		dirty |= R600_BLOCK_STATUS_DIRTY;
		block->reg[id] = new_val;
	}
	if (dirty)
		r600_context_dirty_block(ctx, block, dirty, id);
}

void r600_context_dirty_block(struct r600_context *ctx,
			      struct r600_block *block,
			      int dirty, int index)
{
	if ((index + 1) > block->nreg_dirty)
		block->nreg_dirty = index + 1;

	if ((dirty != (block->status & R600_BLOCK_STATUS_DIRTY)) || !(block->status & R600_BLOCK_STATUS_ENABLED)) {
		block->status |= R600_BLOCK_STATUS_DIRTY;
		ctx->pm4_dirty_cdwords += block->pm4_ndwords + block->pm4_flush_ndwords;
		if (!(block->status & R600_BLOCK_STATUS_ENABLED)) {
			block->status |= R600_BLOCK_STATUS_ENABLED;
			LIST_ADDTAIL(&block->enable_list, &ctx->enable_list);
		}
		LIST_ADDTAIL(&block->list,&ctx->dirty);

		if (block->flags & REG_FLAG_FLUSH_CHANGE) {
			r600_context_ps_partial_flush(ctx);
		}
	}
}

void r600_context_pipe_state_set(struct r600_context *ctx, struct r600_pipe_state *state)
{
	struct r600_block *block;
	unsigned new_val;
	int dirty;
	for (int i = 0; i < state->nregs; i++) {
		unsigned id, reloc_id;
		struct r600_pipe_reg *reg = &state->regs[i];

		block = reg->block;
		id = reg->id;

		dirty = block->status & R600_BLOCK_STATUS_DIRTY;

		new_val = block->reg[id];
		new_val &= ~reg->mask;
		new_val |= reg->value;
		if (new_val != block->reg[id]) {
			block->reg[id] = new_val;
			dirty |= R600_BLOCK_STATUS_DIRTY;
		}
		if (block->flags & REG_FLAG_DIRTY_ALWAYS)
			dirty |= R600_BLOCK_STATUS_DIRTY;
		if (block->pm4_bo_index[id]) {
			/* find relocation */
			reloc_id = block->pm4_bo_index[id];
			r600_bo_reference(ctx->radeon, &block->reloc[reloc_id].bo, reg->bo);
			reg->bo->fence = ctx->radeon->fence;
			/* always force dirty for relocs for now */
			dirty |= R600_BLOCK_STATUS_DIRTY;
		}

		if (dirty)
			r600_context_dirty_block(ctx, block, dirty, id);
	}
}

static void r600_context_dirty_resource_block(struct r600_context *ctx,
					      struct r600_block *block,
					      int dirty, int index)
{
	block->nreg_dirty = index + 1;

	if ((dirty != (block->status & R600_BLOCK_STATUS_RESOURCE_DIRTY)) || !(block->status & R600_BLOCK_STATUS_ENABLED)) {
		block->status |= R600_BLOCK_STATUS_RESOURCE_DIRTY;
		ctx->pm4_dirty_cdwords += block->pm4_ndwords + block->pm4_flush_ndwords;
		if (!(block->status & R600_BLOCK_STATUS_ENABLED)) {
			block->status |= R600_BLOCK_STATUS_ENABLED;
			LIST_ADDTAIL(&block->enable_list, &ctx->enable_list);
		}
		LIST_ADDTAIL(&block->list,&ctx->resource_dirty);
	}
}

void r600_context_pipe_state_set_resource(struct r600_context *ctx, struct r600_pipe_resource_state *state, struct r600_block *block)
{
	int dirty;
	int num_regs = ctx->radeon->chip_class >= EVERGREEN ? 8 : 7;
	boolean is_vertex;

	if (state == NULL) {
		block->status &= ~(R600_BLOCK_STATUS_ENABLED | R600_BLOCK_STATUS_RESOURCE_DIRTY);
		if (block->reloc[1].bo)
			block->reloc[1].bo->bo->binding &= ~BO_BOUND_TEXTURE;

		r600_bo_reference(ctx->radeon, &block->reloc[1].bo, NULL);
		r600_bo_reference(ctx->radeon, &block->reloc[2].bo, NULL);
		LIST_DELINIT(&block->list);
		LIST_DELINIT(&block->enable_list);
		return;
	}

	is_vertex = ((state->val[num_regs-1] & 0xc0000000) == 0xc0000000);
	dirty = block->status & R600_BLOCK_STATUS_RESOURCE_DIRTY;

	if (memcmp(block->reg, state->val, num_regs*4)) {
		memcpy(block->reg, state->val, num_regs * 4);
		dirty |= R600_BLOCK_STATUS_RESOURCE_DIRTY;
	}

	/* if no BOs on block, force dirty */
	if (!block->reloc[1].bo || !block->reloc[2].bo)
		dirty |= R600_BLOCK_STATUS_RESOURCE_DIRTY;

	if (!dirty) {
		if (is_vertex) {
			if (block->reloc[1].bo->bo->handle != state->bo[0]->bo->handle)
				dirty |= R600_BLOCK_STATUS_RESOURCE_DIRTY;
		} else {
			if ((block->reloc[1].bo->bo->handle != state->bo[0]->bo->handle) ||
			    (block->reloc[2].bo->bo->handle != state->bo[1]->bo->handle))
				dirty |= R600_BLOCK_STATUS_RESOURCE_DIRTY;
		}
	}
	if (!dirty) {
		if (is_vertex)
			state->bo[0]->fence = ctx->radeon->fence;
		else {
			state->bo[0]->fence = ctx->radeon->fence;
			state->bo[1]->fence = ctx->radeon->fence;
		}
	} else {
		if (is_vertex) {
			/* VERTEX RESOURCE, we preted there is 2 bo to relocate so
			 * we have single case btw VERTEX & TEXTURE resource
			 */
			r600_bo_reference(ctx->radeon, &block->reloc[1].bo, state->bo[0]);
			r600_bo_reference(ctx->radeon, &block->reloc[2].bo, NULL);
			state->bo[0]->fence = ctx->radeon->fence;
		} else {
			/* TEXTURE RESOURCE */
			r600_bo_reference(ctx->radeon, &block->reloc[1].bo, state->bo[0]);
			r600_bo_reference(ctx->radeon, &block->reloc[2].bo, state->bo[1]);
			state->bo[0]->fence = ctx->radeon->fence;
			state->bo[1]->fence = ctx->radeon->fence;
			state->bo[0]->bo->binding |= BO_BOUND_TEXTURE;
		}
	}
	if (dirty) {
		if (is_vertex)
			block->status |= R600_BLOCK_STATUS_RESOURCE_VERTEX;
		else
			block->status &= ~R600_BLOCK_STATUS_RESOURCE_VERTEX;
	
		r600_context_dirty_resource_block(ctx, block, dirty, num_regs - 1);
	}
}

void r600_context_pipe_state_set_ps_resource(struct r600_context *ctx, struct r600_pipe_resource_state *state, unsigned rid)
{
	struct r600_block *block = ctx->ps_resources.blocks[rid];

	r600_context_pipe_state_set_resource(ctx, state, block);
}

void r600_context_pipe_state_set_vs_resource(struct r600_context *ctx, struct r600_pipe_resource_state *state, unsigned rid)
{
	struct r600_block *block = ctx->vs_resources.blocks[rid];

	r600_context_pipe_state_set_resource(ctx, state, block);
}

void r600_context_pipe_state_set_fs_resource(struct r600_context *ctx, struct r600_pipe_resource_state *state, unsigned rid)
{
	struct r600_block *block = ctx->fs_resources.blocks[rid];

	r600_context_pipe_state_set_resource(ctx, state, block);
}

static inline void r600_context_pipe_state_set_sampler(struct r600_context *ctx, struct r600_pipe_state *state, unsigned offset)
{
	struct r600_range *range;
	struct r600_block *block;
	int i;
	int dirty;

	range = &ctx->range[CTX_RANGE_ID(offset)];
	block = range->blocks[CTX_BLOCK_ID(offset)];
	if (state == NULL) {
		block->status &= ~(R600_BLOCK_STATUS_ENABLED | R600_BLOCK_STATUS_DIRTY);
		LIST_DELINIT(&block->list);
		LIST_DELINIT(&block->enable_list);
		return;
	}
	dirty = block->status & R600_BLOCK_STATUS_DIRTY;
	for (i = 0; i < 3; i++) {
		if (block->reg[i] != state->regs[i].value) {
			block->reg[i] = state->regs[i].value;
			dirty |= R600_BLOCK_STATUS_DIRTY;
		}
	}

	if (dirty)
		r600_context_dirty_block(ctx, block, dirty, 2);
}


static inline void r600_context_pipe_state_set_sampler_border(struct r600_context *ctx, struct r600_pipe_state *state, unsigned offset)
{
	struct r600_range *range;
	struct r600_block *block;
	int i;
	int dirty;

	range = &ctx->range[CTX_RANGE_ID(offset)];
	block = range->blocks[CTX_BLOCK_ID(offset)];
	if (state == NULL) {
		block->status &= ~(R600_BLOCK_STATUS_ENABLED | R600_BLOCK_STATUS_DIRTY);
		LIST_DELINIT(&block->list);
		LIST_DELINIT(&block->enable_list);
		return;
	}
	if (state->nregs <= 3) {
		return;
	}
	dirty = block->status & R600_BLOCK_STATUS_DIRTY;
	for (i = 0; i < 4; i++) {
		if (block->reg[i] != state->regs[i + 3].value) {
			block->reg[i] = state->regs[i + 3].value;
			dirty |= R600_BLOCK_STATUS_DIRTY;
		}
	}

	/* We have to flush the shaders before we change the border color
	 * registers, or previous draw commands that haven't completed yet
	 * will end up using the new border color. */
	if (dirty & R600_BLOCK_STATUS_DIRTY)
		r600_context_ps_partial_flush(ctx);
	if (dirty)
		r600_context_dirty_block(ctx, block, dirty, 3);
}

void r600_context_pipe_state_set_ps_sampler(struct r600_context *ctx, struct r600_pipe_state *state, unsigned id)
{
	unsigned offset;

	offset = 0x0003C000 + id * 0xc;
	r600_context_pipe_state_set_sampler(ctx, state, offset);
	offset = 0x0000A400 + id * 0x10;
	r600_context_pipe_state_set_sampler_border(ctx, state, offset);
}

void r600_context_pipe_state_set_vs_sampler(struct r600_context *ctx, struct r600_pipe_state *state, unsigned id)
{
	unsigned offset;

	offset = 0x0003C0D8 + id * 0xc;
	r600_context_pipe_state_set_sampler(ctx, state, offset);
	offset = 0x0000A600 + id * 0x10;
	r600_context_pipe_state_set_sampler_border(ctx, state, offset);
}

struct r600_bo *r600_context_reg_bo(struct r600_context *ctx, unsigned offset)
{
	struct r600_range *range;
	struct r600_block *block;
	unsigned id;

	range = &ctx->range[CTX_RANGE_ID(offset)];
	block = range->blocks[CTX_BLOCK_ID(offset)];
	offset -= block->start_offset;
	id = block->pm4_bo_index[offset >> 2];
	if (block->reloc[id].bo) {
		return block->reloc[id].bo;
	}
	return NULL;
}

void r600_context_block_emit_dirty(struct r600_context *ctx, struct r600_block *block)
{
	int id;
	int optional = block->nbo == 0 && !(block->flags & REG_FLAG_DIRTY_ALWAYS);
	int cp_dwords = block->pm4_ndwords, start_dword = 0;
	int new_dwords = 0;
	int nbo = block->nbo;

	if (block->nreg_dirty == 0 && optional) {
		goto out;
	}

	if (nbo) {
		ctx->flags |= R600_CONTEXT_CHECK_EVENT_FLUSH;

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
				nbo--;
				if (nbo == 0)
					break;
			}
		}
		ctx->flags &= ~R600_CONTEXT_CHECK_EVENT_FLUSH;
	}

	optional &= (block->nreg_dirty != block->nreg);
	if (optional) {
		new_dwords = block->nreg_dirty;
		start_dword = ctx->pm4_cdwords;
		cp_dwords = new_dwords + 2;
	}
	memcpy(&ctx->pm4[ctx->pm4_cdwords], block->pm4, cp_dwords * 4);
	ctx->pm4_cdwords += cp_dwords;

	if (optional) {
		uint32_t newword;

		newword = ctx->pm4[start_dword];
		newword &= PKT_COUNT_C;
		newword |= PKT_COUNT_S(new_dwords);
		ctx->pm4[start_dword] = newword;
	}
out:
	block->status ^= R600_BLOCK_STATUS_DIRTY;
	block->nreg_dirty = 0;
	LIST_DELINIT(&block->list);
}

void r600_context_block_resource_emit_dirty(struct r600_context *ctx, struct r600_block *block)
{
	int id;
	int cp_dwords = block->pm4_ndwords;
	int nbo = block->nbo;

	ctx->flags |= R600_CONTEXT_CHECK_EVENT_FLUSH;

	if (block->status & R600_BLOCK_STATUS_RESOURCE_VERTEX) {
		nbo = 1;
		cp_dwords -= 2; /* don't copy the second NOP */
	}

	for (int j = 0; j < nbo; j++) {
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
	ctx->flags &= ~R600_CONTEXT_CHECK_EVENT_FLUSH;

	memcpy(&ctx->pm4[ctx->pm4_cdwords], block->pm4, cp_dwords * 4);
	ctx->pm4_cdwords += cp_dwords;

	block->status ^= R600_BLOCK_STATUS_RESOURCE_DIRTY;
	block->nreg_dirty = 0;
	LIST_DELINIT(&block->list);
}

void r600_context_flush_dest_caches(struct r600_context *ctx)
{
	struct r600_bo *cb[8];
	struct r600_bo *db;
	int i;

	if (!(ctx->flags & R600_CONTEXT_DST_CACHES_DIRTY))
		return;

	db = r600_context_reg_bo(ctx, R_02800C_DB_DEPTH_BASE);
	cb[0] = r600_context_reg_bo(ctx, R_028040_CB_COLOR0_BASE);
	cb[1] = r600_context_reg_bo(ctx, R_028044_CB_COLOR1_BASE);
	cb[2] = r600_context_reg_bo(ctx, R_028048_CB_COLOR2_BASE);
	cb[3] = r600_context_reg_bo(ctx, R_02804C_CB_COLOR3_BASE);
	cb[4] = r600_context_reg_bo(ctx, R_028050_CB_COLOR4_BASE);
	cb[5] = r600_context_reg_bo(ctx, R_028054_CB_COLOR5_BASE);
	cb[6] = r600_context_reg_bo(ctx, R_028058_CB_COLOR6_BASE);
	cb[7] = r600_context_reg_bo(ctx, R_02805C_CB_COLOR7_BASE);

	ctx->flags |= R600_CONTEXT_CHECK_EVENT_FLUSH;
	/* flush the color buffers */
	for (i = 0; i < 8; i++) {
		if (!cb[i])
			continue;

		r600_context_bo_flush(ctx,
					(S_0085F0_CB0_DEST_BASE_ENA(1) << i) |
					S_0085F0_CB_ACTION_ENA(1),
					0, cb[i]);
	}
	if (db) {
		r600_context_bo_flush(ctx, S_0085F0_DB_ACTION_ENA(1) | S_0085F0_DB_DEST_BASE_ENA(1), 0, db);
	}
	ctx->flags &= ~R600_CONTEXT_CHECK_EVENT_FLUSH;
	ctx->flags &= ~R600_CONTEXT_DST_CACHES_DIRTY;
}

void r600_context_draw(struct r600_context *ctx, const struct r600_draw *draw)
{
	unsigned ndwords = 7;
	struct r600_block *dirty_block = NULL;
	struct r600_block *next_block;
	uint32_t *pm4;

	if (draw->indices) {
		ndwords = 11;
		/* make sure there is enough relocation space before scheduling draw */
		if (ctx->creloc >= (ctx->nreloc - 1)) {
			r600_context_flush(ctx);
		}
	}

	/* queries need some special values */
	if (ctx->num_query_running) {
		if (ctx->radeon->family >= CHIP_RV770) {
			r600_context_reg(ctx,
					R_028D0C_DB_RENDER_CONTROL,
					S_028D0C_R700_PERFECT_ZPASS_COUNTS(1),
					S_028D0C_R700_PERFECT_ZPASS_COUNTS(1));
		}
		r600_context_reg(ctx,
				R_028D10_DB_RENDER_OVERRIDE,
				S_028D10_NOOP_CULL_DISABLE(1),
				S_028D10_NOOP_CULL_DISABLE(1));
	}

	/* update the max dword count to make sure we have enough space
	 * reserved for flushing the destination caches */
	ctx->pm4_ndwords = RADEON_CTX_MAX_PM4 - ctx->num_dest_buffers * 7 - 16;

	if ((ctx->pm4_dirty_cdwords + ndwords + ctx->pm4_cdwords) > ctx->pm4_ndwords) {
		/* need to flush */
		r600_context_flush(ctx);
	}
	/* at that point everythings is flushed and ctx->pm4_cdwords = 0 */
	if ((ctx->pm4_dirty_cdwords + ndwords) > ctx->pm4_ndwords) {
		R600_ERR("context is too big to be scheduled\n");
		return;
	}
	/* enough room to copy packet */
	LIST_FOR_EACH_ENTRY_SAFE(dirty_block, next_block, &ctx->dirty, list) {
		r600_context_block_emit_dirty(ctx, dirty_block);
	}

	LIST_FOR_EACH_ENTRY_SAFE(dirty_block, next_block, &ctx->resource_dirty, list) {
		r600_context_block_resource_emit_dirty(ctx, dirty_block);
	}

	/* draw packet */
	pm4 = &ctx->pm4[ctx->pm4_cdwords];

	pm4[0] = PKT3(PKT3_INDEX_TYPE, 0, ctx->predicate_drawing);
	pm4[1] = draw->vgt_index_type;
	pm4[2] = PKT3(PKT3_NUM_INSTANCES, 0, ctx->predicate_drawing);
	pm4[3] = draw->vgt_num_instances;
	if (draw->indices) {
		pm4[4] = PKT3(PKT3_DRAW_INDEX, 3, ctx->predicate_drawing);
		pm4[5] = draw->indices_bo_offset + r600_bo_offset(draw->indices);
		pm4[6] = 0;
		pm4[7] = draw->vgt_num_indices;
		pm4[8] = draw->vgt_draw_initiator;
		pm4[9] = PKT3(PKT3_NOP, 0, ctx->predicate_drawing);
		pm4[10] = 0;
		r600_context_bo_reloc(ctx, &pm4[10], draw->indices);
	} else {
		pm4[4] = PKT3(PKT3_DRAW_INDEX_AUTO, 1, ctx->predicate_drawing);
		pm4[5] = draw->vgt_num_indices;
		pm4[6] = draw->vgt_draw_initiator;
	}
	ctx->pm4_cdwords += ndwords;

	ctx->flags |= (R600_CONTEXT_DST_CACHES_DIRTY | R600_CONTEXT_DRAW_PENDING);

	/* all dirty state have been scheduled in current cs */
	ctx->pm4_dirty_cdwords = 0;
}

void r600_context_flush(struct r600_context *ctx)
{
	struct drm_radeon_cs drmib = {};
	struct drm_radeon_cs_chunk chunks[2];
	uint64_t chunk_array[2];
	unsigned fence;
	int r;
	struct r600_block *enable_block = NULL;

	if (ctx->pm4_cdwords == ctx->init_dwords)
		return;

	/* suspend queries */
	r600_context_queries_suspend(ctx);

	if (ctx->radeon->family >= CHIP_CEDAR)
		evergreen_context_flush_dest_caches(ctx);
	else
		r600_context_flush_dest_caches(ctx);

	/* partial flush is needed to avoid lockups on some chips with user fences */
	ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_EVENT_WRITE, 0, 0);
	ctx->pm4[ctx->pm4_cdwords++] = EVENT_TYPE(EVENT_TYPE_PS_PARTIAL_FLUSH) | EVENT_INDEX(4);
	/* emit fence */
	ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_EVENT_WRITE_EOP, 4, 0);
	ctx->pm4[ctx->pm4_cdwords++] = EVENT_TYPE(EVENT_TYPE_CACHE_FLUSH_AND_INV_TS_EVENT) | EVENT_INDEX(5);
	ctx->pm4[ctx->pm4_cdwords++] = 0;
	ctx->pm4[ctx->pm4_cdwords++] = (1 << 29) | (0 << 24);
	ctx->pm4[ctx->pm4_cdwords++] = ctx->radeon->fence;
	ctx->pm4[ctx->pm4_cdwords++] = 0;
	ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_NOP, 0, 0);
	ctx->pm4[ctx->pm4_cdwords++] = 0;
	r600_context_bo_reloc(ctx, &ctx->pm4[ctx->pm4_cdwords - 1], ctx->radeon->fence_bo);

#if 1
	/* emit cs */
	drmib.num_chunks = 2;
	drmib.chunks = (uint64_t)(uintptr_t)chunk_array;
	chunks[0].chunk_id = RADEON_CHUNK_ID_IB;
	chunks[0].length_dw = ctx->pm4_cdwords;
	chunks[0].chunk_data = (uint64_t)(uintptr_t)ctx->pm4;
	chunks[1].chunk_id = RADEON_CHUNK_ID_RELOCS;
	chunks[1].length_dw = ctx->creloc * sizeof(struct r600_reloc) / 4;
	chunks[1].chunk_data = (uint64_t)(uintptr_t)ctx->reloc;
	chunk_array[0] = (uint64_t)(uintptr_t)&chunks[0];
	chunk_array[1] = (uint64_t)(uintptr_t)&chunks[1];
	r = drmCommandWriteRead(ctx->radeon->fd, DRM_RADEON_CS, &drmib,
				sizeof(struct drm_radeon_cs));
#else
	*ctx->radeon->cfence = ctx->radeon->fence;
#endif

	r600_context_update_fenced_list(ctx);

	fence = ctx->radeon->fence + 1;
	if (fence < ctx->radeon->fence) {
		/* wrap around */
		fence = 1;
		r600_context_fence_wraparound(ctx, fence);
	}
	ctx->radeon->fence = fence;

	/* restart */
	for (int i = 0; i < ctx->creloc; i++) {
		ctx->bo[i]->reloc = NULL;
		ctx->bo[i]->last_flush = 0;
		radeon_bo_reference(ctx->radeon, &ctx->bo[i], NULL);
	}
	ctx->creloc = 0;
	ctx->pm4_dirty_cdwords = 0;
	ctx->pm4_cdwords = 0;
	ctx->flags = 0;

	r600_init_cs(ctx);

	/* resume queries */
	r600_context_queries_resume(ctx, TRUE);

	/* set all valid group as dirty so they get reemited on
	 * next draw command
	 */
	LIST_FOR_EACH_ENTRY(enable_block, &ctx->enable_list, enable_list) {
		if (!(enable_block->flags & BLOCK_FLAG_RESOURCE)) {
			if(!(enable_block->status & R600_BLOCK_STATUS_DIRTY)) {
				LIST_ADDTAIL(&enable_block->list,&ctx->dirty);
				enable_block->status |= R600_BLOCK_STATUS_DIRTY;
			}
		} else {
			if(!(enable_block->status & R600_BLOCK_STATUS_RESOURCE_DIRTY)) {
				LIST_ADDTAIL(&enable_block->list,&ctx->resource_dirty);
				enable_block->status |= R600_BLOCK_STATUS_RESOURCE_DIRTY;
			}
		}
		ctx->pm4_dirty_cdwords += enable_block->pm4_ndwords + 
			enable_block->pm4_flush_ndwords;
		enable_block->nreg_dirty = enable_block->nreg;
	}
}

void r600_context_emit_fence(struct r600_context *ctx, struct r600_bo *fence_bo, unsigned offset, unsigned value)
{
	unsigned ndwords = 10;

	if (((ctx->pm4_dirty_cdwords + ndwords + ctx->pm4_cdwords) > ctx->pm4_ndwords) ||
	    (ctx->creloc >= (ctx->nreloc - 1))) {
		/* need to flush */
		r600_context_flush(ctx);
	}

	ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_EVENT_WRITE, 0, 0);
	ctx->pm4[ctx->pm4_cdwords++] = EVENT_TYPE(EVENT_TYPE_PS_PARTIAL_FLUSH) | EVENT_INDEX(4);
	ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_EVENT_WRITE_EOP, 4, 0);
	ctx->pm4[ctx->pm4_cdwords++] = EVENT_TYPE(EVENT_TYPE_CACHE_FLUSH_AND_INV_TS_EVENT) | EVENT_INDEX(5);
	ctx->pm4[ctx->pm4_cdwords++] = offset << 2;             /* ADDRESS_LO */
	ctx->pm4[ctx->pm4_cdwords++] = (1 << 29) | (0 << 24);   /* DATA_SEL | INT_EN | ADDRESS_HI */
	ctx->pm4[ctx->pm4_cdwords++] = value;                   /* DATA_LO */
	ctx->pm4[ctx->pm4_cdwords++] = 0;                       /* DATA_HI */
	ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_NOP, 0, 0);
	ctx->pm4[ctx->pm4_cdwords++] = 0;
	r600_context_bo_reloc(ctx, &ctx->pm4[ctx->pm4_cdwords - 1], fence_bo);
}

void r600_context_dump_bof(struct r600_context *ctx, const char *file)
{
	bof_t *bcs, *blob, *array, *bo, *size, *handle, *device_id, *root;
	unsigned i;

	root = device_id = bcs = blob = array = bo = size = handle = NULL;
	root = bof_object();
	if (root == NULL)
		goto out_err;
	device_id = bof_int32(ctx->radeon->device);
	if (device_id == NULL)
		goto out_err;
	if (bof_object_set(root, "device_id", device_id))
		goto out_err;
	bof_decref(device_id);
	device_id = NULL;
	/* dump relocs */
	blob = bof_blob(ctx->creloc * 16, ctx->reloc);
	if (blob == NULL)
		goto out_err;
	if (bof_object_set(root, "reloc", blob))
		goto out_err;
	bof_decref(blob);
	blob = NULL;
	/* dump cs */
	blob = bof_blob(ctx->pm4_cdwords * 4, ctx->pm4);
	if (blob == NULL)
		goto out_err;
	if (bof_object_set(root, "pm4", blob))
		goto out_err;
	bof_decref(blob);
	blob = NULL;
	/* dump bo */
	array = bof_array();
	if (array == NULL)
		goto out_err;
	for (i = 0; i < ctx->creloc; i++) {
		struct radeon_bo *rbo = ctx->bo[i];
		bo = bof_object();
		if (bo == NULL)
			goto out_err;
		size = bof_int32(rbo->size);
		if (size == NULL)
			goto out_err;
		if (bof_object_set(bo, "size", size))
			goto out_err;
		bof_decref(size);
		size = NULL;
		handle = bof_int32(rbo->handle);
		if (handle == NULL)
			goto out_err;
		if (bof_object_set(bo, "handle", handle))
			goto out_err;
		bof_decref(handle);
		handle = NULL;
		radeon_bo_map(ctx->radeon, rbo);
		blob = bof_blob(rbo->size, rbo->data);
		radeon_bo_unmap(ctx->radeon, rbo);
		if (blob == NULL)
			goto out_err;
		if (bof_object_set(bo, "data", blob))
			goto out_err;
		bof_decref(blob);
		blob = NULL;
		if (bof_array_append(array, bo))
			goto out_err;
		bof_decref(bo);
		bo = NULL;
	}
	if (bof_object_set(root, "bo", array))
		goto out_err;
	bof_dump_file(root, file);
out_err:
	bof_decref(blob);
	bof_decref(array);
	bof_decref(bo);
	bof_decref(size);
	bof_decref(handle);
	bof_decref(device_id);
	bof_decref(root);
}

static boolean r600_query_result(struct r600_context *ctx, struct r600_query *query, boolean wait)
{
	unsigned results_base = query->results_start;
	u64 start, end;
	u32 *results, *current_result;

	if (wait)
		results = r600_bo_map(ctx->radeon, query->buffer, PB_USAGE_CPU_READ, NULL);
	else
		results = r600_bo_map(ctx->radeon, query->buffer, PB_USAGE_DONTBLOCK | PB_USAGE_CPU_READ, NULL);
	if (!results)
		return FALSE;


	/* count all results across all data blocks */
	while (results_base != query->results_end) {
		current_result = (u32*)((char*)results + results_base);

		start = (u64)current_result[0] | (u64)current_result[1] << 32;
		end = (u64)current_result[2] | (u64)current_result[3] << 32;
		if (((start & 0x8000000000000000UL) && (end & 0x8000000000000000UL))
                    || query->type == PIPE_QUERY_TIME_ELAPSED) {
			query->result += end - start;
		}

		results_base += 4 * 4;
		if (results_base >= query->buffer_size)
			results_base = 0;
	}

	query->results_start = query->results_end;
	r600_bo_unmap(ctx->radeon, query->buffer);
	return TRUE;
}

void r600_query_begin(struct r600_context *ctx, struct r600_query *query)
{
	unsigned required_space, new_results_end;

	/* query request needs 6/8 dwords for begin + 6/8 dwords for end */
	if (query->type == PIPE_QUERY_TIME_ELAPSED)
		required_space = 16;
	else
		required_space = 12;

	if ((required_space + ctx->pm4_cdwords) > ctx->pm4_ndwords) {
		/* need to flush */
		r600_context_flush(ctx);
	}

	if (query->type == PIPE_QUERY_OCCLUSION_COUNTER) {
		/* Count queries emitted without flushes, and flush if more than
		 * half of buffer used, to avoid overwriting results which may be
		 * still in use. */
		if (query->state & R600_QUERY_STATE_FLUSHED) {
			query->queries_emitted = 1;
		} else {
			if (++query->queries_emitted > query->buffer_size / query->result_size / 2)
				r600_context_flush(ctx);
		}
	}

	new_results_end = query->results_end + query->result_size;
	if (new_results_end >= query->buffer_size)
		new_results_end = 0;

	/* collect current results if query buffer is full */
	if (new_results_end == query->results_start) {
		if (!(query->state & R600_QUERY_STATE_FLUSHED))
			r600_context_flush(ctx);
		r600_query_result(ctx, query, TRUE);
	}

	if (query->type == PIPE_QUERY_OCCLUSION_COUNTER) {
		u32 *results;
		int i;

		results = r600_bo_map(ctx->radeon, query->buffer, PB_USAGE_CPU_WRITE, NULL);
		if (results) {
			results = (u32*)((char*)results + query->results_end);
			memset(results, 0, query->result_size);

			/* Set top bits for unused backends */
			for (i = 0; i < ctx->max_db; i++) {
				if (!(ctx->backend_mask & (1<<i))) {
					results[(i * 4)+1] = 0x80000000;
					results[(i * 4)+3] = 0x80000000;
				}
			}
			r600_bo_unmap(ctx->radeon, query->buffer);
		}
	}

	/* emit begin query */
	if (query->type == PIPE_QUERY_TIME_ELAPSED) {
		ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_EVENT_WRITE_EOP, 4, 0);
		ctx->pm4[ctx->pm4_cdwords++] = EVENT_TYPE(EVENT_TYPE_CACHE_FLUSH_AND_INV_TS_EVENT) | EVENT_INDEX(5);
		ctx->pm4[ctx->pm4_cdwords++] = query->results_end + r600_bo_offset(query->buffer);
		ctx->pm4[ctx->pm4_cdwords++] = (3 << 29);
		ctx->pm4[ctx->pm4_cdwords++] = 0;
		ctx->pm4[ctx->pm4_cdwords++] = 0;
	} else {
		ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_EVENT_WRITE, 2, 0);
		ctx->pm4[ctx->pm4_cdwords++] = EVENT_TYPE(EVENT_TYPE_ZPASS_DONE) | EVENT_INDEX(1);
		ctx->pm4[ctx->pm4_cdwords++] = query->results_end + r600_bo_offset(query->buffer);
		ctx->pm4[ctx->pm4_cdwords++] = 0;
	}
	ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_NOP, 0, 0);
	ctx->pm4[ctx->pm4_cdwords++] = 0;
	r600_context_bo_reloc(ctx, &ctx->pm4[ctx->pm4_cdwords - 1], query->buffer);

	query->state |= R600_QUERY_STATE_STARTED;
	query->state ^= R600_QUERY_STATE_ENDED;
	ctx->num_query_running++;
}

void r600_query_end(struct r600_context *ctx, struct r600_query *query)
{
	/* emit end query */
	if (query->type == PIPE_QUERY_TIME_ELAPSED) {
		ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_EVENT_WRITE_EOP, 4, 0);
		ctx->pm4[ctx->pm4_cdwords++] = EVENT_TYPE(EVENT_TYPE_CACHE_FLUSH_AND_INV_TS_EVENT) | EVENT_INDEX(5);
		ctx->pm4[ctx->pm4_cdwords++] = query->results_end + 8 + r600_bo_offset(query->buffer);
		ctx->pm4[ctx->pm4_cdwords++] = (3 << 29);
		ctx->pm4[ctx->pm4_cdwords++] = 0;
		ctx->pm4[ctx->pm4_cdwords++] = 0;
	} else {
		ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_EVENT_WRITE, 2, 0);
		ctx->pm4[ctx->pm4_cdwords++] = EVENT_TYPE(EVENT_TYPE_ZPASS_DONE) | EVENT_INDEX(1);
		ctx->pm4[ctx->pm4_cdwords++] = query->results_end + 8 + r600_bo_offset(query->buffer);
		ctx->pm4[ctx->pm4_cdwords++] = 0;
	}
	ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_NOP, 0, 0);
	ctx->pm4[ctx->pm4_cdwords++] = 0;
	r600_context_bo_reloc(ctx, &ctx->pm4[ctx->pm4_cdwords - 1], query->buffer);

	query->results_end += query->result_size;
	if (query->results_end >= query->buffer_size)
		query->results_end = 0;

	query->state ^= R600_QUERY_STATE_STARTED;
	query->state |= R600_QUERY_STATE_ENDED;
	query->state &= ~R600_QUERY_STATE_FLUSHED;

	ctx->num_query_running--;
}

void r600_query_predication(struct r600_context *ctx, struct r600_query *query, int operation,
			    int flag_wait)
{
	if (operation == PREDICATION_OP_CLEAR) {
		if (ctx->pm4_cdwords + 3 > ctx->pm4_ndwords)
			r600_context_flush(ctx);

		ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_SET_PREDICATION, 1, 0);
		ctx->pm4[ctx->pm4_cdwords++] = 0;
		ctx->pm4[ctx->pm4_cdwords++] = PRED_OP(PREDICATION_OP_CLEAR);
	} else {
		unsigned results_base = query->results_start;
		unsigned count;
		u32 op;

		/* find count of the query data blocks */
		count = query->buffer_size + query->results_end - query->results_start;
		if (count >= query->buffer_size) count-=query->buffer_size;
		count /= query->result_size;

		if (ctx->pm4_cdwords + 5 * count > ctx->pm4_ndwords)
			r600_context_flush(ctx);

		op = PRED_OP(operation) | PREDICATION_DRAW_VISIBLE |
				(flag_wait ? PREDICATION_HINT_WAIT : PREDICATION_HINT_NOWAIT_DRAW);

		/* emit predicate packets for all data blocks */
		while (results_base != query->results_end) {
			ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_SET_PREDICATION, 1, 0);
			ctx->pm4[ctx->pm4_cdwords++] = results_base + r600_bo_offset(query->buffer);
			ctx->pm4[ctx->pm4_cdwords++] = op;
			ctx->pm4[ctx->pm4_cdwords++] = PKT3(PKT3_NOP, 0, 0);
			ctx->pm4[ctx->pm4_cdwords++] = 0;
			r600_context_bo_reloc(ctx, &ctx->pm4[ctx->pm4_cdwords - 1], query->buffer);
			results_base += query->result_size;
			if (results_base >= query->buffer_size)
				results_base = 0;
			/* set CONTINUE bit for all packets except the first */
			op |= PREDICATION_CONTINUE;
		}
	}
}

struct r600_query *r600_context_query_create(struct r600_context *ctx, unsigned query_type)
{
	struct r600_query *query;

	if (query_type != PIPE_QUERY_OCCLUSION_COUNTER && query_type != PIPE_QUERY_TIME_ELAPSED)
		return NULL;

	query = calloc(1, sizeof(struct r600_query));
	if (query == NULL)
		return NULL;

	query->type = query_type;
	query->buffer_size = 4096;

	if (query_type == PIPE_QUERY_OCCLUSION_COUNTER)
		query->result_size = 4 * 4 * ctx->max_db;
	else
		query->result_size = 4 * 4;

	/* adjust buffer size to simplify offsets wrapping math */
	query->buffer_size -= query->buffer_size % query->result_size;

	/* As of GL4, query buffers are normally read by the CPU after
	 * being written by the gpu, hence staging is probably a good
	 * usage pattern.
	 */
	query->buffer = r600_bo(ctx->radeon, query->buffer_size, 1, 0,
				PIPE_USAGE_STAGING);
	if (!query->buffer) {
		free(query);
		return NULL;
	}

	LIST_ADDTAIL(&query->list, &ctx->query_list);

	return query;
}

void r600_context_query_destroy(struct r600_context *ctx, struct r600_query *query)
{
	r600_bo_reference(ctx->radeon, &query->buffer, NULL);
	LIST_DELINIT(&query->list);
	free(query);
}

boolean r600_context_query_result(struct r600_context *ctx,
				struct r600_query *query,
				boolean wait, void *vresult)
{
	uint64_t *result = (uint64_t*)vresult;

	if (!(query->state & R600_QUERY_STATE_FLUSHED)) {
		r600_context_flush(ctx);
	}
	if (!r600_query_result(ctx, query, wait))
		return FALSE;
	if (query->type == PIPE_QUERY_TIME_ELAPSED)
		*result = (1000000*query->result)/r600_get_clock_crystal_freq(ctx->radeon);
	else
		*result = query->result;
	query->result = 0;
	return TRUE;
}

void r600_context_queries_suspend(struct r600_context *ctx)
{
	struct r600_query *query;

	LIST_FOR_EACH_ENTRY(query, &ctx->query_list, list) {
		if (query->state & R600_QUERY_STATE_STARTED) {
			r600_query_end(ctx, query);
			query->state |= R600_QUERY_STATE_SUSPENDED;
		}
	}
}

void r600_context_queries_resume(struct r600_context *ctx, boolean flushed)
{
	struct r600_query *query;

	LIST_FOR_EACH_ENTRY(query, &ctx->query_list, list) {
		if (flushed)
			query->state |= R600_QUERY_STATE_FLUSHED;

		if (query->state & R600_QUERY_STATE_SUSPENDED) {
			r600_query_begin(ctx, query);
			query->state ^= R600_QUERY_STATE_SUSPENDED;
		}
	}
}
