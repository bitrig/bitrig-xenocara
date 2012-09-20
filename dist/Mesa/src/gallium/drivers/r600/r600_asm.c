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
 */
#include <stdio.h>
#include <errno.h>
#include <byteswap.h>
#include "util/u_format.h"
#include "util/u_memory.h"
#include "pipe/p_shader_tokens.h"
#include "r600_pipe.h"
#include "r600_sq.h"
#include "r600_opcodes.h"
#include "r600_asm.h"
#include "r600_formats.h"
#include "r600d.h"

#define NUM_OF_CYCLES 3
#define NUM_OF_COMPONENTS 4

static inline unsigned int r600_bc_get_num_operands(struct r600_bc *bc, struct r600_bc_alu *alu)
{
	if(alu->is_op3)
		return 3;

	switch (bc->chiprev) {
	case CHIPREV_R600:
	case CHIPREV_R700:
		switch (alu->inst) {
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_NOP:
			return 0;
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_ADD:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_ADD_INT:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLE:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGT:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGE:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLNE:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MUL:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MULHI_UINT:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MAX:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MIN:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_SETE:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_SETNE:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_SETGT:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_SETGE:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETE:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGT:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGE:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETNE:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_DOT4:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_DOT4_IEEE:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_CUBE:
			return 2;

		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MOV:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MOVA:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MOVA_FLOOR:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MOVA_INT:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_FRACT:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_FLOOR:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_TRUNC:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_EXP_IEEE:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_LOG_CLAMPED:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_LOG_IEEE:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIP_CLAMPED:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIP_IEEE:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIPSQRT_CLAMPED:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIPSQRT_IEEE:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_FLT_TO_INT:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_INT_TO_FLT:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_SIN:
		case V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_COS:
			return 1;
		default: R600_ERR(
			"Need instruction operand number for 0x%x.\n", alu->inst);
		}
		break;
	case CHIPREV_EVERGREEN:
	case CHIPREV_CAYMAN:
		switch (alu->inst) {
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_NOP:
			return 0;
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_ADD:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_ADD_INT:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGT:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLNE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MUL:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MULHI_UINT:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MAX:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MIN:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_SETE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_SETNE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_SETGT:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_SETGE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGT:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETNE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_DOT4:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_DOT4_IEEE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_CUBE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INTERP_XY:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INTERP_ZW:
			return 2;

		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MOV:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MOVA_INT:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_FRACT:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_FLOOR:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_TRUNC:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_EXP_IEEE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_LOG_CLAMPED:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_LOG_IEEE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIP_CLAMPED:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIP_IEEE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIPSQRT_CLAMPED:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIPSQRT_IEEE:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_FLT_TO_INT:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_FLT_TO_INT_FLOOR:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_INT_TO_FLT:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_SIN:
		case EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_COS:
			return 1;
		default: R600_ERR(
			"Need instruction operand number for 0x%x.\n", alu->inst);
		}
		break;
	}

	return 3;
}

int r700_bc_alu_build(struct r600_bc *bc, struct r600_bc_alu *alu, unsigned id);

static struct r600_bc_cf *r600_bc_cf(void)
{
	struct r600_bc_cf *cf = CALLOC_STRUCT(r600_bc_cf);

	if (cf == NULL)
		return NULL;
	LIST_INITHEAD(&cf->list);
	LIST_INITHEAD(&cf->alu);
	LIST_INITHEAD(&cf->vtx);
	LIST_INITHEAD(&cf->tex);
	return cf;
}

static struct r600_bc_alu *r600_bc_alu(void)
{
	struct r600_bc_alu *alu = CALLOC_STRUCT(r600_bc_alu);

	if (alu == NULL)
		return NULL;
	LIST_INITHEAD(&alu->list);
	return alu;
}

static struct r600_bc_vtx *r600_bc_vtx(void)
{
	struct r600_bc_vtx *vtx = CALLOC_STRUCT(r600_bc_vtx);

	if (vtx == NULL)
		return NULL;
	LIST_INITHEAD(&vtx->list);
	return vtx;
}

static struct r600_bc_tex *r600_bc_tex(void)
{
	struct r600_bc_tex *tex = CALLOC_STRUCT(r600_bc_tex);

	if (tex == NULL)
		return NULL;
	LIST_INITHEAD(&tex->list);
	return tex;
}

int r600_bc_init(struct r600_bc *bc, enum radeon_family family)
{
	LIST_INITHEAD(&bc->cf);
	bc->family = family;
	switch (bc->family) {
	case CHIP_R600:
	case CHIP_RV610:
	case CHIP_RV630:
	case CHIP_RV670:
	case CHIP_RV620:
	case CHIP_RV635:
	case CHIP_RS780:
	case CHIP_RS880:
		bc->chiprev = CHIPREV_R600;
		break;
	case CHIP_RV770:
	case CHIP_RV730:
	case CHIP_RV710:
	case CHIP_RV740:
		bc->chiprev = CHIPREV_R700;
		break;
	case CHIP_CEDAR:
	case CHIP_REDWOOD:
	case CHIP_JUNIPER:
	case CHIP_CYPRESS:
	case CHIP_HEMLOCK:
	case CHIP_PALM:
	case CHIP_SUMO:
	case CHIP_SUMO2:
	case CHIP_BARTS:
	case CHIP_TURKS:
	case CHIP_CAICOS:
		bc->chiprev = CHIPREV_EVERGREEN;
		break;
	case CHIP_CAYMAN:
		bc->chiprev = CHIPREV_CAYMAN;
		break;
	default:
		R600_ERR("unknown family %d\n", bc->family);
		return -EINVAL;
	}
	return 0;
}

static int r600_bc_add_cf(struct r600_bc *bc)
{
	struct r600_bc_cf *cf = r600_bc_cf();

	if (cf == NULL)
		return -ENOMEM;
	LIST_ADDTAIL(&cf->list, &bc->cf);
	if (bc->cf_last)
		cf->id = bc->cf_last->id + 2;
	bc->cf_last = cf;
	bc->ncf++;
	bc->ndw += 2;
	bc->force_add_cf = 0;
	return 0;
}

int r600_bc_add_output(struct r600_bc *bc, const struct r600_bc_output *output)
{
	int r;

	if (bc->cf_last && (bc->cf_last->inst == output->inst ||
		(bc->cf_last->inst == BC_INST(bc, V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT) &&
		output->inst == BC_INST(bc, V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT_DONE))) &&
		output->type == bc->cf_last->output.type &&
		output->elem_size == bc->cf_last->output.elem_size &&
		output->swizzle_x == bc->cf_last->output.swizzle_x &&
		output->swizzle_y == bc->cf_last->output.swizzle_y &&
		output->swizzle_z == bc->cf_last->output.swizzle_z &&
		output->swizzle_w == bc->cf_last->output.swizzle_w &&
		(output->burst_count + bc->cf_last->output.burst_count) <= 16) {

		if ((output->gpr + output->burst_count) == bc->cf_last->output.gpr &&
			(output->array_base + output->burst_count) == bc->cf_last->output.array_base) {

			bc->cf_last->output.end_of_program |= output->end_of_program;
			bc->cf_last->output.inst = output->inst;
			bc->cf_last->output.gpr = output->gpr;
			bc->cf_last->output.array_base = output->array_base;
			bc->cf_last->output.burst_count += output->burst_count;
			return 0;

		} else if (output->gpr == (bc->cf_last->output.gpr + bc->cf_last->output.burst_count) &&
			output->array_base == (bc->cf_last->output.array_base + bc->cf_last->output.burst_count)) {

			bc->cf_last->output.end_of_program |= output->end_of_program;
			bc->cf_last->output.inst = output->inst;
			bc->cf_last->output.burst_count += output->burst_count;
			return 0;
		}
	}

	r = r600_bc_add_cf(bc);
	if (r)
		return r;
	bc->cf_last->inst = output->inst;
	memcpy(&bc->cf_last->output, output, sizeof(struct r600_bc_output));
	return 0;
}

/* alu instructions that can ony exits once per group */
static int is_alu_once_inst(struct r600_bc *bc, struct r600_bc_alu *alu)
{
	switch (bc->chiprev) {
	case CHIPREV_R600:
	case CHIPREV_R700:
		return !alu->is_op3 && (
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLE ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGE ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLNE ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGT_UINT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGE_UINT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLE_INT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGT_INT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGE_INT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLNE_INT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGT_UINT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGE_UINT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETE ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGE ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETNE ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SET_INV ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SET_POP ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SET_CLR ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SET_RESTORE ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETE_PUSH ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGT_PUSH ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGE_PUSH ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETNE_PUSH ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETE_INT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGT_INT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGE_INT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETNE_INT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETE_PUSH_INT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGT_PUSH_INT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGE_PUSH_INT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETNE_PUSH_INT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETLT_PUSH_INT ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETLE_PUSH_INT);
	case CHIPREV_EVERGREEN:
	case CHIPREV_CAYMAN:
	default:
		return !alu->is_op3 && (
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLE ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGE ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLNE ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGT_UINT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGE_UINT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLE_INT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGT_INT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLGE_INT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_KILLNE_INT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGT_UINT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGE_UINT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETE ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGE ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETNE ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SET_INV ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SET_POP ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SET_CLR ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SET_RESTORE ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETE_PUSH ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGT_PUSH ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGE_PUSH ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETNE_PUSH ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETE_INT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGT_INT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGE_INT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETNE_INT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETE_PUSH_INT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGT_PUSH_INT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETGE_PUSH_INT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETNE_PUSH_INT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETLT_PUSH_INT ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_PRED_SETLE_PUSH_INT);
	}
}

static int is_alu_reduction_inst(struct r600_bc *bc, struct r600_bc_alu *alu)
{
	switch (bc->chiprev) {
	case CHIPREV_R600:
	case CHIPREV_R700:
		return !alu->is_op3 && (
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_CUBE ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_DOT4 ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_DOT4_IEEE ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MAX4);
	case CHIPREV_EVERGREEN:
	case CHIPREV_CAYMAN:
	default:
		return !alu->is_op3 && (
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_CUBE ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_DOT4 ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_DOT4_IEEE ||
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MAX4);
	}
}

static int is_alu_cube_inst(struct r600_bc *bc, struct r600_bc_alu *alu)
{
	switch (bc->chiprev) {
	case CHIPREV_R600:
	case CHIPREV_R700:
		return !alu->is_op3 &&
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_CUBE;
	case CHIPREV_EVERGREEN:
	case CHIPREV_CAYMAN:
	default:
		return !alu->is_op3 &&
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_CUBE;
	}
}

static int is_alu_mova_inst(struct r600_bc *bc, struct r600_bc_alu *alu)
{
	switch (bc->chiprev) {
	case CHIPREV_R600:
	case CHIPREV_R700:
		return !alu->is_op3 && (
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MOVA ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MOVA_FLOOR ||
			alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MOVA_INT);
	case CHIPREV_EVERGREEN:
	case CHIPREV_CAYMAN:
	default:
		return !alu->is_op3 && (
			alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MOVA_INT);
	}
}

/* alu instructions that can only execute on the vector unit */
static int is_alu_vec_unit_inst(struct r600_bc *bc, struct r600_bc_alu *alu)
{
	return is_alu_reduction_inst(bc, alu) ||
		is_alu_mova_inst(bc, alu) ||
		(bc->chiprev == CHIPREV_EVERGREEN &&
		alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_FLT_TO_INT_FLOOR);
}

/* alu instructions that can only execute on the trans unit */
static int is_alu_trans_unit_inst(struct r600_bc *bc, struct r600_bc_alu *alu)
{
	switch (bc->chiprev) {
	case CHIPREV_R600:
	case CHIPREV_R700:
		if (!alu->is_op3)
			return alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_ASHR_INT ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_FLT_TO_INT ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_INT_TO_FLT ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_LSHL_INT ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_LSHR_INT ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MULHI_INT ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MULHI_UINT ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MULLO_INT ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MULLO_UINT ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIP_INT ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIP_UINT ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_UINT_TO_FLT ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_COS ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_EXP_IEEE ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_LOG_CLAMPED ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_LOG_IEEE ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIP_CLAMPED ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIP_FF ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIP_IEEE ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIPSQRT_CLAMPED ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIPSQRT_FF ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIPSQRT_IEEE ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_SIN ||
				alu->inst == V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_SQRT_IEEE;
		else
			return alu->inst == V_SQ_ALU_WORD1_OP3_SQ_OP3_INST_MUL_LIT ||
				alu->inst == V_SQ_ALU_WORD1_OP3_SQ_OP3_INST_MUL_LIT_D2 ||
				alu->inst == V_SQ_ALU_WORD1_OP3_SQ_OP3_INST_MUL_LIT_M2 ||
				alu->inst == V_SQ_ALU_WORD1_OP3_SQ_OP3_INST_MUL_LIT_M4;
	case CHIPREV_EVERGREEN:
	case CHIPREV_CAYMAN:
	default:
		if (!alu->is_op3)
			/* Note that FLT_TO_INT_* instructions are vector-only instructions
			 * on Evergreen, despite what the documentation says. FLT_TO_INT
			 * can do both vector and scalar. */
			return alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_ASHR_INT ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_INT_TO_FLT ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_LSHL_INT ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_LSHR_INT ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MULHI_INT ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MULHI_UINT ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MULLO_INT ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MULLO_UINT ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIP_INT ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIP_UINT ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_UINT_TO_FLT ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_COS ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_EXP_IEEE ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_LOG_CLAMPED ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_LOG_IEEE ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIP_CLAMPED ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIP_FF ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIP_IEEE ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIPSQRT_CLAMPED ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIPSQRT_FF ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_RECIPSQRT_IEEE ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_SIN ||
				alu->inst == EG_V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_SQRT_IEEE;
		else
			return alu->inst == EG_V_SQ_ALU_WORD1_OP3_SQ_OP3_INST_MUL_LIT;
	}
}

/* alu instructions that can execute on any unit */
static int is_alu_any_unit_inst(struct r600_bc *bc, struct r600_bc_alu *alu)
{
	return !is_alu_vec_unit_inst(bc, alu) &&
		!is_alu_trans_unit_inst(bc, alu);
}

static int assign_alu_units(struct r600_bc *bc, struct r600_bc_alu *alu_first,
			    struct r600_bc_alu *assignment[5])
{
	struct r600_bc_alu *alu;
	unsigned i, chan, trans;
	int max_slots = bc->chiprev == CHIPREV_CAYMAN ? 4 : 5;

	for (i = 0; i < max_slots; i++)
		assignment[i] = NULL;

	for (alu = alu_first; alu; alu = LIST_ENTRY(struct r600_bc_alu, alu->list.next, list)) {
		chan = alu->dst.chan;
		if (max_slots == 4)
			trans = 0;
		else if (is_alu_trans_unit_inst(bc, alu))
			trans = 1;
		else if (is_alu_vec_unit_inst(bc, alu))
			trans = 0;
		else if (assignment[chan])
			trans = 1; /* Assume ALU_INST_PREFER_VECTOR. */
		else
			trans = 0;

		if (trans) {
			if (assignment[4]) {
				assert(0); /* ALU.Trans has already been allocated. */
				return -1;
			}
			assignment[4] = alu;
		} else {
			if (assignment[chan]) {
				assert(0); /* ALU.chan has already been allocated. */
				return -1;
			}
			assignment[chan] = alu;
		}

		if (alu->last)
			break;
	}
	return 0;
}

struct alu_bank_swizzle {
	int	hw_gpr[NUM_OF_CYCLES][NUM_OF_COMPONENTS];
	int	hw_cfile_addr[4];
	int	hw_cfile_elem[4];
};

static const unsigned cycle_for_bank_swizzle_vec[][3] = {
	[SQ_ALU_VEC_012] = { 0, 1, 2 },
	[SQ_ALU_VEC_021] = { 0, 2, 1 },
	[SQ_ALU_VEC_120] = { 1, 2, 0 },
	[SQ_ALU_VEC_102] = { 1, 0, 2 },
	[SQ_ALU_VEC_201] = { 2, 0, 1 },
	[SQ_ALU_VEC_210] = { 2, 1, 0 }
};

static const unsigned cycle_for_bank_swizzle_scl[][3] = {
	[SQ_ALU_SCL_210] = { 2, 1, 0 },
	[SQ_ALU_SCL_122] = { 1, 2, 2 },
	[SQ_ALU_SCL_212] = { 2, 1, 2 },
	[SQ_ALU_SCL_221] = { 2, 2, 1 }
};

static void init_bank_swizzle(struct alu_bank_swizzle *bs)
{
	int i, cycle, component;
	/* set up gpr use */
	for (cycle = 0; cycle < NUM_OF_CYCLES; cycle++)
		for (component = 0; component < NUM_OF_COMPONENTS; component++)
			 bs->hw_gpr[cycle][component] = -1;
	for (i = 0; i < 4; i++)
		bs->hw_cfile_addr[i] = -1;
	for (i = 0; i < 4; i++)
		bs->hw_cfile_elem[i] = -1;
}

static int reserve_gpr(struct alu_bank_swizzle *bs, unsigned sel, unsigned chan, unsigned cycle)
{
	if (bs->hw_gpr[cycle][chan] == -1)
		bs->hw_gpr[cycle][chan] = sel;
	else if (bs->hw_gpr[cycle][chan] != (int)sel) {
		/* Another scalar operation has already used the GPR read port for the channel. */
		return -1;
	}
	return 0;
}

static int reserve_cfile(struct r600_bc *bc, struct alu_bank_swizzle *bs, unsigned sel, unsigned chan)
{
	int res, num_res = 4;
	if (bc->chiprev >= CHIPREV_R700) {
		num_res = 2;
		chan /= 2;
	}
	for (res = 0; res < num_res; ++res) {
		if (bs->hw_cfile_addr[res] == -1) {
			bs->hw_cfile_addr[res] = sel;
			bs->hw_cfile_elem[res] = chan;
			return 0;
		} else if (bs->hw_cfile_addr[res] == sel &&
			bs->hw_cfile_elem[res] == chan)
			return 0; /* Read for this scalar element already reserved, nothing to do here. */
	}
	/* All cfile read ports are used, cannot reference vector element. */
	return -1;
}

static int is_gpr(unsigned sel)
{
	return (sel >= 0 && sel <= 127);
}

/* CB constants start at 512, and get translated to a kcache index when ALU
 * clauses are constructed. Note that we handle kcache constants the same way
 * as (the now gone) cfile constants, is that really required? */
static int is_cfile(unsigned sel)
{
	return (sel > 255 && sel < 512) ||
		(sel > 511 && sel < 4607) || /* Kcache before translation. */
		(sel > 127 && sel < 192); /* Kcache after translation. */
}

static int is_const(int sel)
{
	return is_cfile(sel) ||
		(sel >= V_SQ_ALU_SRC_0 &&
		sel <= V_SQ_ALU_SRC_LITERAL);
}

static int check_vector(struct r600_bc *bc, struct r600_bc_alu *alu,
			struct alu_bank_swizzle *bs, int bank_swizzle)
{
	int r, src, num_src, sel, elem, cycle;

	num_src = r600_bc_get_num_operands(bc, alu);
	for (src = 0; src < num_src; src++) {
		sel = alu->src[src].sel;
		elem = alu->src[src].chan;
		if (is_gpr(sel)) {
			cycle = cycle_for_bank_swizzle_vec[bank_swizzle][src];
			if (src == 1 && sel == alu->src[0].sel && elem == alu->src[0].chan)
				/* Nothing to do; special-case optimization,
				 * second source uses first source’s reservation. */
				continue;
			else {
				r = reserve_gpr(bs, sel, elem, cycle);
				if (r)
					return r;
			}
		} else if (is_cfile(sel)) {
			r = reserve_cfile(bc, bs, sel, elem);
			if (r)
				return r;
		}
		/* No restrictions on PV, PS, literal or special constants. */
	}
	return 0;
}

static int check_scalar(struct r600_bc *bc, struct r600_bc_alu *alu,
			struct alu_bank_swizzle *bs, int bank_swizzle)
{
	int r, src, num_src, const_count, sel, elem, cycle;

	num_src = r600_bc_get_num_operands(bc, alu);
	for (const_count = 0, src = 0; src < num_src; ++src) {
		sel = alu->src[src].sel;
		elem = alu->src[src].chan;
		if (is_const(sel)) { /* Any constant, including literal and inline constants. */
			if (const_count >= 2)
				/* More than two references to a constant in
				 * transcendental operation. */
				return -1;
			else
				const_count++;
		}
		if (is_cfile(sel)) {
			r = reserve_cfile(bc, bs, sel, elem);
			if (r)
				return r;
		}
	}
	for (src = 0; src < num_src; ++src) {
		sel = alu->src[src].sel;
		elem = alu->src[src].chan;
		if (is_gpr(sel)) {
			cycle = cycle_for_bank_swizzle_scl[bank_swizzle][src];
			if (cycle < const_count)
				/* Cycle for GPR load conflicts with
				 * constant load in transcendental operation. */
				return -1;
			r = reserve_gpr(bs, sel, elem, cycle);
			if (r)
				return r;
		}
		/* PV PS restrictions */
		if (const_count && (sel == 254 || sel == 255)) {
			cycle = cycle_for_bank_swizzle_scl[bank_swizzle][src];
			if (cycle < const_count)
				return -1;
		}
	}
	return 0;
}

static int check_and_set_bank_swizzle(struct r600_bc *bc,
				      struct r600_bc_alu *slots[5])
{
	struct alu_bank_swizzle bs;
	int bank_swizzle[5];
	int i, r = 0, forced = 1;
	boolean scalar_only = bc->chiprev == CHIPREV_CAYMAN ? false : true;
	int max_slots = bc->chiprev == CHIPREV_CAYMAN ? 4 : 5;

	for (i = 0; i < max_slots; i++) {
		if (slots[i]) {
			if (slots[i]->bank_swizzle_force) {
				slots[i]->bank_swizzle = slots[i]->bank_swizzle_force;
			} else {
				forced = 0;
			}
		}

		if (i < 4 && slots[i])
			scalar_only = false;
	}
	if (forced)
		return 0;

	/* Just check every possible combination of bank swizzle.
	 * Not very efficent, but works on the first try in most of the cases. */
	for (i = 0; i < 4; i++)
		if (!slots[i] || !slots[i]->bank_swizzle_force)
			bank_swizzle[i] = SQ_ALU_VEC_012;
		else
			bank_swizzle[i] = slots[i]->bank_swizzle;

	bank_swizzle[4] = SQ_ALU_SCL_210;
	while(bank_swizzle[4] <= SQ_ALU_SCL_221) {

		if (max_slots == 4) {
			for (i = 0; i < max_slots; i++) {
				if (bank_swizzle[i] == SQ_ALU_VEC_210)
				  return -1;
			}
		}
		init_bank_swizzle(&bs);
		if (scalar_only == false) {
			for (i = 0; i < 4; i++) {
				if (slots[i]) {
					r = check_vector(bc, slots[i], &bs, bank_swizzle[i]);
					if (r)
						break;
				}
			}
		} else
			r = 0;

		if (!r && slots[4] && max_slots == 5) {
			r = check_scalar(bc, slots[4], &bs, bank_swizzle[4]);
		}
		if (!r) {
			for (i = 0; i < max_slots; i++) {
				if (slots[i])
					slots[i]->bank_swizzle = bank_swizzle[i];
			}
			return 0;
		}

		if (scalar_only) {
			bank_swizzle[4]++;
		} else {
			for (i = 0; i < max_slots; i++) {
				if (!slots[i] || !slots[i]->bank_swizzle_force) {
					bank_swizzle[i]++;
					if (bank_swizzle[i] <= SQ_ALU_VEC_210)
						break;
					else
						bank_swizzle[i] = SQ_ALU_VEC_012;
				}
			}
		}
	}

	/* Couldn't find a working swizzle. */
	return -1;
}

static int replace_gpr_with_pv_ps(struct r600_bc *bc,
				  struct r600_bc_alu *slots[5], struct r600_bc_alu *alu_prev)
{
	struct r600_bc_alu *prev[5];
	int gpr[5], chan[5];
	int i, j, r, src, num_src;
	int max_slots = bc->chiprev == CHIPREV_CAYMAN ? 4 : 5;

	r = assign_alu_units(bc, alu_prev, prev);
	if (r)
		return r;

	for (i = 0; i < max_slots; ++i) {
		if (prev[i] && (prev[i]->dst.write || prev[i]->is_op3) && !prev[i]->dst.rel) {
			gpr[i] = prev[i]->dst.sel;
			/* cube writes more than PV.X */
			if (!is_alu_cube_inst(bc, prev[i]) && is_alu_reduction_inst(bc, prev[i]))
				chan[i] = 0;
			else
				chan[i] = prev[i]->dst.chan;
		} else
			gpr[i] = -1;
	}

	for (i = 0; i < max_slots; ++i) {
		struct r600_bc_alu *alu = slots[i];
		if(!alu)
			continue;

		num_src = r600_bc_get_num_operands(bc, alu);
		for (src = 0; src < num_src; ++src) {
			if (!is_gpr(alu->src[src].sel) || alu->src[src].rel)
				continue;

			if (bc->chiprev < CHIPREV_CAYMAN) {
				if (alu->src[src].sel == gpr[4] &&
				    alu->src[src].chan == chan[4]) {
					alu->src[src].sel = V_SQ_ALU_SRC_PS;
					alu->src[src].chan = 0;
					continue;
				}
			}

			for (j = 0; j < 4; ++j) {
				if (alu->src[src].sel == gpr[j] &&
					alu->src[src].chan == j) {
					alu->src[src].sel = V_SQ_ALU_SRC_PV;
					alu->src[src].chan = chan[j];
					break;
				}
			}
		}
	}

	return 0;
}

void r600_bc_special_constants(u32 value, unsigned *sel, unsigned *neg)
{
	switch(value) {
	case 0:
		*sel = V_SQ_ALU_SRC_0;
		break;
	case 1:
		*sel = V_SQ_ALU_SRC_1_INT;
		break;
	case -1:
		*sel = V_SQ_ALU_SRC_M_1_INT;
		break;
	case 0x3F800000: /* 1.0f */
		*sel = V_SQ_ALU_SRC_1;
		break;
	case 0x3F000000: /* 0.5f */
		*sel = V_SQ_ALU_SRC_0_5;
		break;
	case 0xBF800000: /* -1.0f */
		*sel = V_SQ_ALU_SRC_1;
		*neg ^= 1;
		break;
	case 0xBF000000: /* -0.5f */
		*sel = V_SQ_ALU_SRC_0_5;
		*neg ^= 1;
		break;
	default:
		*sel = V_SQ_ALU_SRC_LITERAL;
		break;
	}
}

/* compute how many literal are needed */
static int r600_bc_alu_nliterals(struct r600_bc *bc, struct r600_bc_alu *alu,
				 uint32_t literal[4], unsigned *nliteral)
{
	unsigned num_src = r600_bc_get_num_operands(bc, alu);
	unsigned i, j;

	for (i = 0; i < num_src; ++i) {
		if (alu->src[i].sel == V_SQ_ALU_SRC_LITERAL) {
			uint32_t value = alu->src[i].value;
			unsigned found = 0;
			for (j = 0; j < *nliteral; ++j) {
				if (literal[j] == value) {
					found = 1;
					break;
				}
			}
			if (!found) {
				if (*nliteral >= 4)
					return -EINVAL;
				literal[(*nliteral)++] = value;
			}
		}
	}
	return 0;
}

static void r600_bc_alu_adjust_literals(struct r600_bc *bc,
					struct r600_bc_alu *alu,
					uint32_t literal[4], unsigned nliteral)
{
	unsigned num_src = r600_bc_get_num_operands(bc, alu);
	unsigned i, j;

	for (i = 0; i < num_src; ++i) {
		if (alu->src[i].sel == V_SQ_ALU_SRC_LITERAL) {
			uint32_t value = alu->src[i].value;
			for (j = 0; j < nliteral; ++j) {
				if (literal[j] == value) {
					alu->src[i].chan = j;
					break;
				}
			}
		}
	}
}

static int merge_inst_groups(struct r600_bc *bc, struct r600_bc_alu *slots[5],
			     struct r600_bc_alu *alu_prev)
{
	struct r600_bc_alu *prev[5];
	struct r600_bc_alu *result[5] = { NULL };

	uint32_t literal[4], prev_literal[4];
	unsigned nliteral = 0, prev_nliteral = 0;

	int i, j, r, src, num_src;
	int num_once_inst = 0;
	int have_mova = 0, have_rel = 0;
	int max_slots = bc->chiprev == CHIPREV_CAYMAN ? 4 : 5;

	r = assign_alu_units(bc, alu_prev, prev);
	if (r)
		return r;

	for (i = 0; i < max_slots; ++i) {
		struct r600_bc_alu *alu;

		/* check number of literals */
		if (prev[i]) {
			if (r600_bc_alu_nliterals(bc, prev[i], literal, &nliteral))
				return 0;
			if (r600_bc_alu_nliterals(bc, prev[i], prev_literal, &prev_nliteral))
				return 0;
			if (is_alu_mova_inst(bc, prev[i])) {
				if (have_rel)
					return 0;
				have_mova = 1;
			}
			num_once_inst += is_alu_once_inst(bc, prev[i]);
		}
		if (slots[i] && r600_bc_alu_nliterals(bc, slots[i], literal, &nliteral))
			return 0;

		/* Let's check used slots. */
		if (prev[i] && !slots[i]) {
			result[i] = prev[i];
			continue;
		} else if (prev[i] && slots[i]) {
			if (max_slots == 5 && result[4] == NULL && prev[4] == NULL && slots[4] == NULL) {
				/* Trans unit is still free try to use it. */
				if (is_alu_any_unit_inst(bc, slots[i])) {
					result[i] = prev[i];
					result[4] = slots[i];
				} else if (is_alu_any_unit_inst(bc, prev[i])) {
					result[i] = slots[i];
					result[4] = prev[i];
				} else
					return 0;
			} else
				return 0;
		} else if(!slots[i]) {
			continue;
		} else
			result[i] = slots[i];

		alu = slots[i];
		num_once_inst += is_alu_once_inst(bc, alu);

		/* Let's check dst gpr. */
		if (alu->dst.rel) {
			if (have_mova)
				return 0;
			have_rel = 1;
		}

		/* Let's check source gprs */
		num_src = r600_bc_get_num_operands(bc, alu);
		for (src = 0; src < num_src; ++src) {
			if (alu->src[src].rel) {
				if (have_mova)
					return 0;
				have_rel = 1;
			}

			/* Constants don't matter. */
			if (!is_gpr(alu->src[src].sel))
				continue;

			for (j = 0; j < max_slots; ++j) {
				if (!prev[j] || !prev[j]->dst.write)
					continue;

				/* If it's relative then we can't determin which gpr is really used. */
				if (prev[j]->dst.chan == alu->src[src].chan &&
					(prev[j]->dst.sel == alu->src[src].sel ||
					prev[j]->dst.rel || alu->src[src].rel))
					return 0;
			}
		}
	}

	/* more than one PRED_ or KILL_ ? */
	if (num_once_inst > 1)
		return 0;

	/* check if the result can still be swizzlet */
	r = check_and_set_bank_swizzle(bc, result);
	if (r)
		return 0;

	/* looks like everything worked out right, apply the changes */

	/* undo adding previus literals */
	bc->cf_last->ndw -= align(prev_nliteral, 2);

	/* sort instructions */
	for (i = 0; i < max_slots; ++i) {
		slots[i] = result[i];
		if (result[i]) {
			LIST_DEL(&result[i]->list);
			result[i]->last = 0;
			LIST_ADDTAIL(&result[i]->list, &bc->cf_last->alu);
		}
	}

	/* determine new last instruction */
	LIST_ENTRY(struct r600_bc_alu, bc->cf_last->alu.prev, list)->last = 1;

	/* determine new first instruction */
	for (i = 0; i < max_slots; ++i) {
		if (result[i]) {
			bc->cf_last->curr_bs_head = result[i];
			break;
		}
	}

	bc->cf_last->prev_bs_head = bc->cf_last->prev2_bs_head;
	bc->cf_last->prev2_bs_head = NULL;

	return 0;
}

/* This code handles kcache lines as single blocks of 32 constants. We could
 * probably do slightly better by recognizing that we actually have two
 * consecutive lines of 16 constants, but the resulting code would also be
 * somewhat more complicated. */
static int r600_bc_alloc_kcache_lines(struct r600_bc *bc, struct r600_bc_alu *alu, int type)
{
	struct r600_bc_kcache *kcache = bc->cf_last->kcache;
	unsigned int required_lines;
	unsigned int free_lines = 0;
	unsigned int cache_line[3];
	unsigned int count = 0;
	unsigned int i, j;
	int r;

	/* Collect required cache lines. */
	for (i = 0; i < 3; ++i) {
		boolean found = false;
		unsigned int line;

		if (alu->src[i].sel < 512)
			continue;

		line = ((alu->src[i].sel - 512) / 32) * 2;

		for (j = 0; j < count; ++j) {
			if (cache_line[j] == line) {
				found = true;
				break;
			}
		}

		if (!found)
			cache_line[count++] = line;
	}

	/* This should never actually happen. */
	if (count >= 3) return -ENOMEM;

	for (i = 0; i < 2; ++i) {
		if (kcache[i].mode == V_SQ_CF_KCACHE_NOP) {
			++free_lines;
		}
	}

	/* Filter lines pulled in by previous intructions. Note that this is
	 * only for the required_lines count, we can't remove these from the
	 * cache_line array since we may have to start a new ALU clause. */
	for (i = 0, required_lines = count; i < count; ++i) {
		for (j = 0; j < 2; ++j) {
			if (kcache[j].mode == V_SQ_CF_KCACHE_LOCK_2 &&
			    kcache[j].addr == cache_line[i]) {
				--required_lines;
				break;
			}
		}
	}

	/* Start a new ALU clause if needed. */
	if (required_lines > free_lines) {
		if ((r = r600_bc_add_cf(bc))) {
			return r;
		}
		bc->cf_last->inst = (type << 3);
		kcache = bc->cf_last->kcache;
	}

	/* Setup the kcache lines. */
	for (i = 0; i < count; ++i) {
		boolean found = false;

		for (j = 0; j < 2; ++j) {
			if (kcache[j].mode == V_SQ_CF_KCACHE_LOCK_2 &&
			    kcache[j].addr == cache_line[i]) {
				found = true;
				break;
			}
		}

		if (found) continue;

		for (j = 0; j < 2; ++j) {
			if (kcache[j].mode == V_SQ_CF_KCACHE_NOP) {
				kcache[j].bank = 0;
				kcache[j].addr = cache_line[i];
				kcache[j].mode = V_SQ_CF_KCACHE_LOCK_2;
				break;
			}
		}
	}

	/* Alter the src operands to refer to the kcache. */
	for (i = 0; i < 3; ++i) {
		static const unsigned int base[] = {128, 160, 256, 288};
		unsigned int line;

		if (alu->src[i].sel < 512)
			continue;

		alu->src[i].sel -= 512;
		line = (alu->src[i].sel / 32) * 2;

		for (j = 0; j < 2; ++j) {
			if (kcache[j].mode == V_SQ_CF_KCACHE_LOCK_2 &&
			    kcache[j].addr == line) {
				alu->src[i].sel &= 0x1f;
				alu->src[i].sel += base[j];
				break;
			}
		}
	}

	return 0;
}

int r600_bc_add_alu_type(struct r600_bc *bc, const struct r600_bc_alu *alu, int type)
{
	struct r600_bc_alu *nalu = r600_bc_alu();
	struct r600_bc_alu *lalu;
	int i, r;

	if (nalu == NULL)
		return -ENOMEM;
	memcpy(nalu, alu, sizeof(struct r600_bc_alu));

	if (bc->cf_last != NULL && bc->cf_last->inst != (type << 3)) {
		/* check if we could add it anyway */
		if (bc->cf_last->inst == (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU << 3) &&
			type == V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU_PUSH_BEFORE) {
			LIST_FOR_EACH_ENTRY(lalu, &bc->cf_last->alu, list) {
				if (lalu->predicate) {
					bc->force_add_cf = 1;
					break;
				}
			}
		} else
			bc->force_add_cf = 1;
	}

	/* cf can contains only alu or only vtx or only tex */
	if (bc->cf_last == NULL || bc->force_add_cf) {
		r = r600_bc_add_cf(bc);
		if (r) {
			free(nalu);
			return r;
		}
	}
	bc->cf_last->inst = (type << 3);

	/* Setup the kcache for this ALU instruction. This will start a new
	 * ALU clause if needed. */
	if ((r = r600_bc_alloc_kcache_lines(bc, nalu, type))) {
		free(nalu);
		return r;
	}

	if (!bc->cf_last->curr_bs_head) {
		bc->cf_last->curr_bs_head = nalu;
	}
	/* number of gpr == the last gpr used in any alu */
	for (i = 0; i < 3; i++) {
		if (nalu->src[i].sel >= bc->ngpr && nalu->src[i].sel < 128) {
			bc->ngpr = nalu->src[i].sel + 1;
		}
		if (nalu->src[i].sel == V_SQ_ALU_SRC_LITERAL)
			r600_bc_special_constants(nalu->src[i].value,
				&nalu->src[i].sel, &nalu->src[i].neg);
	}
	if (nalu->dst.sel >= bc->ngpr) {
		bc->ngpr = nalu->dst.sel + 1;
	}
	LIST_ADDTAIL(&nalu->list, &bc->cf_last->alu);
	/* each alu use 2 dwords */
	bc->cf_last->ndw += 2;
	bc->ndw += 2;

	/* process cur ALU instructions for bank swizzle */
	if (nalu->last) {
		uint32_t literal[4];
		unsigned nliteral;
		struct r600_bc_alu *slots[5];
		int max_slots = bc->chiprev == CHIPREV_CAYMAN ? 4 : 5;
		r = assign_alu_units(bc, bc->cf_last->curr_bs_head, slots);
		if (r)
			return r;

		if (bc->cf_last->prev_bs_head) {
			r = merge_inst_groups(bc, slots, bc->cf_last->prev_bs_head);
			if (r)
				return r;
		}

		if (bc->cf_last->prev_bs_head) {
			r = replace_gpr_with_pv_ps(bc, slots, bc->cf_last->prev_bs_head);
			if (r)
				return r;
		}

		r = check_and_set_bank_swizzle(bc, slots);
		if (r)
			return r;

		for (i = 0, nliteral = 0; i < max_slots; i++) {
			if (slots[i]) {
				r = r600_bc_alu_nliterals(bc, slots[i], literal, &nliteral);
				if (r)
					return r;
			}
		}
		bc->cf_last->ndw += align(nliteral, 2);

		/* at most 128 slots, one add alu can add 5 slots + 4 constants(2 slots)
		 * worst case */
		if ((bc->cf_last->ndw >> 1) >= 120) {
			bc->force_add_cf = 1;
		}

		bc->cf_last->prev2_bs_head = bc->cf_last->prev_bs_head;
		bc->cf_last->prev_bs_head = bc->cf_last->curr_bs_head;
		bc->cf_last->curr_bs_head = NULL;
	}
	return 0;
}

int r600_bc_add_alu(struct r600_bc *bc, const struct r600_bc_alu *alu)
{
	return r600_bc_add_alu_type(bc, alu, BC_INST(bc, V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU));
}

static unsigned r600_bc_num_tex_and_vtx_instructions(const struct r600_bc *bc)
{
	switch (bc->chiprev) {
	case CHIPREV_R600:
		return 8;

	case CHIPREV_R700:
		return 16;

	case CHIPREV_EVERGREEN:
	case CHIPREV_CAYMAN:
		return 64;

	default:
		R600_ERR("Unknown chiprev %d.\n", bc->chiprev);
		return 8;
	}
}

static inline boolean last_inst_was_vtx_fetch(struct r600_bc *bc)
{
	if (bc->chiprev == CHIPREV_CAYMAN) {
		if (bc->cf_last->inst != CM_V_SQ_CF_WORD1_SQ_CF_INST_TC)
			return TRUE;
	} else {
		if (bc->cf_last->inst != V_SQ_CF_WORD1_SQ_CF_INST_VTX &&
		    bc->cf_last->inst != V_SQ_CF_WORD1_SQ_CF_INST_VTX_TC)
			return TRUE;
	}
	return FALSE;
}

int r600_bc_add_vtx(struct r600_bc *bc, const struct r600_bc_vtx *vtx)
{
	struct r600_bc_vtx *nvtx = r600_bc_vtx();
	int r;

	if (nvtx == NULL)
		return -ENOMEM;
	memcpy(nvtx, vtx, sizeof(struct r600_bc_vtx));

	/* cf can contains only alu or only vtx or only tex */
	if (bc->cf_last == NULL ||
	    last_inst_was_vtx_fetch(bc) ||
	    bc->force_add_cf) {
		r = r600_bc_add_cf(bc);
		if (r) {
			free(nvtx);
			return r;
		}
		if (bc->chiprev == CHIPREV_CAYMAN)
			bc->cf_last->inst = CM_V_SQ_CF_WORD1_SQ_CF_INST_TC;
		else
			bc->cf_last->inst = V_SQ_CF_WORD1_SQ_CF_INST_VTX;
	}
	LIST_ADDTAIL(&nvtx->list, &bc->cf_last->vtx);
	/* each fetch use 4 dwords */
	bc->cf_last->ndw += 4;
	bc->ndw += 4;
	if ((bc->cf_last->ndw / 4) >= r600_bc_num_tex_and_vtx_instructions(bc))
		bc->force_add_cf = 1;
	return 0;
}

int r600_bc_add_tex(struct r600_bc *bc, const struct r600_bc_tex *tex)
{
	struct r600_bc_tex *ntex = r600_bc_tex();
	int r;

	if (ntex == NULL)
		return -ENOMEM;
	memcpy(ntex, tex, sizeof(struct r600_bc_tex));

	/* we can't fetch data und use it as texture lookup address in the same TEX clause */
	if (bc->cf_last != NULL &&
		bc->cf_last->inst == V_SQ_CF_WORD1_SQ_CF_INST_TEX) {
		struct r600_bc_tex *ttex;
		LIST_FOR_EACH_ENTRY(ttex, &bc->cf_last->tex, list) {
			if (ttex->dst_gpr == ntex->src_gpr) {
				bc->force_add_cf = 1;
				break;
			}
		}
		/* slight hack to make gradients always go into same cf */
		if (ntex->inst == SQ_TEX_INST_SET_GRADIENTS_H)
			bc->force_add_cf = 1;
	}

	/* cf can contains only alu or only vtx or only tex */
	if (bc->cf_last == NULL ||
		bc->cf_last->inst != V_SQ_CF_WORD1_SQ_CF_INST_TEX ||
	        bc->force_add_cf) {
		r = r600_bc_add_cf(bc);
		if (r) {
			free(ntex);
			return r;
		}
		bc->cf_last->inst = V_SQ_CF_WORD1_SQ_CF_INST_TEX;
	}
	if (ntex->src_gpr >= bc->ngpr) {
		bc->ngpr = ntex->src_gpr + 1;
	}
	if (ntex->dst_gpr >= bc->ngpr) {
		bc->ngpr = ntex->dst_gpr + 1;
	}
	LIST_ADDTAIL(&ntex->list, &bc->cf_last->tex);
	/* each texture fetch use 4 dwords */
	bc->cf_last->ndw += 4;
	bc->ndw += 4;
	if ((bc->cf_last->ndw / 4) >= r600_bc_num_tex_and_vtx_instructions(bc))
		bc->force_add_cf = 1;
	return 0;
}

int r600_bc_add_cfinst(struct r600_bc *bc, int inst)
{
	int r;
	r = r600_bc_add_cf(bc);
	if (r)
		return r;

	bc->cf_last->cond = V_SQ_CF_COND_ACTIVE;
	bc->cf_last->inst = inst;
	return 0;
}

int cm_bc_add_cf_end(struct r600_bc *bc)
{
	return r600_bc_add_cfinst(bc, CM_V_SQ_CF_WORD1_SQ_CF_INST_END);
}

/* common to all 3 families */
static int r600_bc_vtx_build(struct r600_bc *bc, struct r600_bc_vtx *vtx, unsigned id)
{
	bc->bytecode[id] = S_SQ_VTX_WORD0_BUFFER_ID(vtx->buffer_id) |
			S_SQ_VTX_WORD0_FETCH_TYPE(vtx->fetch_type) |
			S_SQ_VTX_WORD0_SRC_GPR(vtx->src_gpr) |
			S_SQ_VTX_WORD0_SRC_SEL_X(vtx->src_sel_x);
	if (bc->chiprev < CHIPREV_CAYMAN)
		bc->bytecode[id] |= S_SQ_VTX_WORD0_MEGA_FETCH_COUNT(vtx->mega_fetch_count);
	id++;
	bc->bytecode[id++] = S_SQ_VTX_WORD1_DST_SEL_X(vtx->dst_sel_x) |
				S_SQ_VTX_WORD1_DST_SEL_Y(vtx->dst_sel_y) |
				S_SQ_VTX_WORD1_DST_SEL_Z(vtx->dst_sel_z) |
				S_SQ_VTX_WORD1_DST_SEL_W(vtx->dst_sel_w) |
				S_SQ_VTX_WORD1_USE_CONST_FIELDS(vtx->use_const_fields) |
				S_SQ_VTX_WORD1_DATA_FORMAT(vtx->data_format) |
				S_SQ_VTX_WORD1_NUM_FORMAT_ALL(vtx->num_format_all) |
				S_SQ_VTX_WORD1_FORMAT_COMP_ALL(vtx->format_comp_all) |
				S_SQ_VTX_WORD1_SRF_MODE_ALL(vtx->srf_mode_all) |
				S_SQ_VTX_WORD1_GPR_DST_GPR(vtx->dst_gpr);
	bc->bytecode[id] = S_SQ_VTX_WORD2_OFFSET(vtx->offset)|
				S_SQ_VTX_WORD2_ENDIAN_SWAP(vtx->endian);
	if (bc->chiprev < CHIPREV_CAYMAN)
		bc->bytecode[id] |= S_SQ_VTX_WORD2_MEGA_FETCH(1);
	id++;
	bc->bytecode[id++] = 0;
	return 0;
}

/* common to all 3 families */
static int r600_bc_tex_build(struct r600_bc *bc, struct r600_bc_tex *tex, unsigned id)
{
	bc->bytecode[id++] = S_SQ_TEX_WORD0_TEX_INST(tex->inst) |
				S_SQ_TEX_WORD0_RESOURCE_ID(tex->resource_id) |
				S_SQ_TEX_WORD0_SRC_GPR(tex->src_gpr) |
				S_SQ_TEX_WORD0_SRC_REL(tex->src_rel);
	bc->bytecode[id++] = S_SQ_TEX_WORD1_DST_GPR(tex->dst_gpr) |
				S_SQ_TEX_WORD1_DST_REL(tex->dst_rel) |
				S_SQ_TEX_WORD1_DST_SEL_X(tex->dst_sel_x) |
				S_SQ_TEX_WORD1_DST_SEL_Y(tex->dst_sel_y) |
				S_SQ_TEX_WORD1_DST_SEL_Z(tex->dst_sel_z) |
				S_SQ_TEX_WORD1_DST_SEL_W(tex->dst_sel_w) |
				S_SQ_TEX_WORD1_LOD_BIAS(tex->lod_bias) |
				S_SQ_TEX_WORD1_COORD_TYPE_X(tex->coord_type_x) |
				S_SQ_TEX_WORD1_COORD_TYPE_Y(tex->coord_type_y) |
				S_SQ_TEX_WORD1_COORD_TYPE_Z(tex->coord_type_z) |
				S_SQ_TEX_WORD1_COORD_TYPE_W(tex->coord_type_w);
	bc->bytecode[id++] = S_SQ_TEX_WORD2_OFFSET_X(tex->offset_x) |
				S_SQ_TEX_WORD2_OFFSET_Y(tex->offset_y) |
				S_SQ_TEX_WORD2_OFFSET_Z(tex->offset_z) |
				S_SQ_TEX_WORD2_SAMPLER_ID(tex->sampler_id) |
				S_SQ_TEX_WORD2_SRC_SEL_X(tex->src_sel_x) |
				S_SQ_TEX_WORD2_SRC_SEL_Y(tex->src_sel_y) |
				S_SQ_TEX_WORD2_SRC_SEL_Z(tex->src_sel_z) |
				S_SQ_TEX_WORD2_SRC_SEL_W(tex->src_sel_w);
	bc->bytecode[id++] = 0;
	return 0;
}

/* r600 only, r700/eg bits in r700_asm.c */
static int r600_bc_alu_build(struct r600_bc *bc, struct r600_bc_alu *alu, unsigned id)
{
	/* don't replace gpr by pv or ps for destination register */
	bc->bytecode[id++] = S_SQ_ALU_WORD0_SRC0_SEL(alu->src[0].sel) |
				S_SQ_ALU_WORD0_SRC0_REL(alu->src[0].rel) |
				S_SQ_ALU_WORD0_SRC0_CHAN(alu->src[0].chan) |
				S_SQ_ALU_WORD0_SRC0_NEG(alu->src[0].neg) |
				S_SQ_ALU_WORD0_SRC1_SEL(alu->src[1].sel) |
				S_SQ_ALU_WORD0_SRC1_REL(alu->src[1].rel) |
				S_SQ_ALU_WORD0_SRC1_CHAN(alu->src[1].chan) |
				S_SQ_ALU_WORD0_SRC1_NEG(alu->src[1].neg) |
				S_SQ_ALU_WORD0_LAST(alu->last);

	if (alu->is_op3) {
		bc->bytecode[id++] = S_SQ_ALU_WORD1_DST_GPR(alu->dst.sel) |
					S_SQ_ALU_WORD1_DST_CHAN(alu->dst.chan) |
					S_SQ_ALU_WORD1_DST_REL(alu->dst.rel) |
					S_SQ_ALU_WORD1_CLAMP(alu->dst.clamp) |
					S_SQ_ALU_WORD1_OP3_SRC2_SEL(alu->src[2].sel) |
					S_SQ_ALU_WORD1_OP3_SRC2_REL(alu->src[2].rel) |
					S_SQ_ALU_WORD1_OP3_SRC2_CHAN(alu->src[2].chan) |
					S_SQ_ALU_WORD1_OP3_SRC2_NEG(alu->src[2].neg) |
					S_SQ_ALU_WORD1_OP3_ALU_INST(alu->inst) |
					S_SQ_ALU_WORD1_BANK_SWIZZLE(alu->bank_swizzle);
	} else {
		bc->bytecode[id++] = S_SQ_ALU_WORD1_DST_GPR(alu->dst.sel) |
					S_SQ_ALU_WORD1_DST_CHAN(alu->dst.chan) |
					S_SQ_ALU_WORD1_DST_REL(alu->dst.rel) |
					S_SQ_ALU_WORD1_CLAMP(alu->dst.clamp) |
					S_SQ_ALU_WORD1_OP2_SRC0_ABS(alu->src[0].abs) |
					S_SQ_ALU_WORD1_OP2_SRC1_ABS(alu->src[1].abs) |
					S_SQ_ALU_WORD1_OP2_WRITE_MASK(alu->dst.write) |
					S_SQ_ALU_WORD1_OP2_OMOD(alu->omod) |
					S_SQ_ALU_WORD1_OP2_ALU_INST(alu->inst) |
					S_SQ_ALU_WORD1_BANK_SWIZZLE(alu->bank_swizzle) |
					S_SQ_ALU_WORD1_OP2_UPDATE_EXECUTE_MASK(alu->predicate) |
					S_SQ_ALU_WORD1_OP2_UPDATE_PRED(alu->predicate);
	}
	return 0;
}

static void r600_bc_cf_vtx_build(uint32_t *bytecode, const struct r600_bc_cf *cf)
{
	*bytecode++ = S_SQ_CF_WORD0_ADDR(cf->addr >> 1);
	*bytecode++ = S_SQ_CF_WORD1_CF_INST(cf->inst) |
			S_SQ_CF_WORD1_BARRIER(1) |
			S_SQ_CF_WORD1_COUNT((cf->ndw / 4) - 1);
}

/* common for r600/r700 - eg in eg_asm.c */
static int r600_bc_cf_build(struct r600_bc *bc, struct r600_bc_cf *cf)
{
	unsigned id = cf->id;

	switch (cf->inst) {
	case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU << 3):
	case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU_PUSH_BEFORE << 3):
	case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU_POP_AFTER << 3):
	case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU_POP2_AFTER << 3):
		bc->bytecode[id++] = S_SQ_CF_ALU_WORD0_ADDR(cf->addr >> 1) |
			S_SQ_CF_ALU_WORD0_KCACHE_MODE0(cf->kcache[0].mode) |
			S_SQ_CF_ALU_WORD0_KCACHE_BANK0(cf->kcache[0].bank) |
			S_SQ_CF_ALU_WORD0_KCACHE_BANK1(cf->kcache[1].bank);

		bc->bytecode[id++] = S_SQ_CF_ALU_WORD1_CF_INST(cf->inst >> 3) |
			S_SQ_CF_ALU_WORD1_KCACHE_MODE1(cf->kcache[1].mode) |
			S_SQ_CF_ALU_WORD1_KCACHE_ADDR0(cf->kcache[0].addr) |
			S_SQ_CF_ALU_WORD1_KCACHE_ADDR1(cf->kcache[1].addr) |
					S_SQ_CF_ALU_WORD1_BARRIER(1) |
					S_SQ_CF_ALU_WORD1_USES_WATERFALL(bc->chiprev == CHIPREV_R600 ? cf->r6xx_uses_waterfall : 0) |
					S_SQ_CF_ALU_WORD1_COUNT((cf->ndw / 2) - 1);
		break;
	case V_SQ_CF_WORD1_SQ_CF_INST_TEX:
	case V_SQ_CF_WORD1_SQ_CF_INST_VTX:
	case V_SQ_CF_WORD1_SQ_CF_INST_VTX_TC:
		if (bc->chiprev == CHIPREV_R700)
			r700_bc_cf_vtx_build(&bc->bytecode[id], cf);
		else
			r600_bc_cf_vtx_build(&bc->bytecode[id], cf);
		break;
	case V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT:
	case V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT_DONE:
		bc->bytecode[id++] = S_SQ_CF_ALLOC_EXPORT_WORD0_RW_GPR(cf->output.gpr) |
			S_SQ_CF_ALLOC_EXPORT_WORD0_ELEM_SIZE(cf->output.elem_size) |
			S_SQ_CF_ALLOC_EXPORT_WORD0_ARRAY_BASE(cf->output.array_base) |
			S_SQ_CF_ALLOC_EXPORT_WORD0_TYPE(cf->output.type);
		bc->bytecode[id++] = S_SQ_CF_ALLOC_EXPORT_WORD1_BURST_COUNT(cf->output.burst_count - 1) |
			S_SQ_CF_ALLOC_EXPORT_WORD1_SWIZ_SEL_X(cf->output.swizzle_x) |
			S_SQ_CF_ALLOC_EXPORT_WORD1_SWIZ_SEL_Y(cf->output.swizzle_y) |
			S_SQ_CF_ALLOC_EXPORT_WORD1_SWIZ_SEL_Z(cf->output.swizzle_z) |
			S_SQ_CF_ALLOC_EXPORT_WORD1_SWIZ_SEL_W(cf->output.swizzle_w) |
			S_SQ_CF_ALLOC_EXPORT_WORD1_BARRIER(cf->output.barrier) |
			S_SQ_CF_ALLOC_EXPORT_WORD1_CF_INST(cf->output.inst) |
			S_SQ_CF_ALLOC_EXPORT_WORD1_END_OF_PROGRAM(cf->output.end_of_program);
		break;
	case V_SQ_CF_WORD1_SQ_CF_INST_JUMP:
	case V_SQ_CF_WORD1_SQ_CF_INST_ELSE:
	case V_SQ_CF_WORD1_SQ_CF_INST_POP:
	case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_START_NO_AL:
	case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_END:
	case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_CONTINUE:
	case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_BREAK:
	case V_SQ_CF_WORD1_SQ_CF_INST_CALL_FS:
	case V_SQ_CF_WORD1_SQ_CF_INST_RETURN:
		bc->bytecode[id++] = S_SQ_CF_WORD0_ADDR(cf->cf_addr >> 1);
		bc->bytecode[id++] = S_SQ_CF_WORD1_CF_INST(cf->inst) |
					S_SQ_CF_WORD1_BARRIER(1) |
			                S_SQ_CF_WORD1_COND(cf->cond) |
			                S_SQ_CF_WORD1_POP_COUNT(cf->pop_count);

		break;
	default:
		R600_ERR("unsupported CF instruction (0x%X)\n", cf->inst);
		return -EINVAL;
	}
	return 0;
}

int r600_bc_build(struct r600_bc *bc)
{
	struct r600_bc_cf *cf;
	struct r600_bc_alu *alu;
	struct r600_bc_vtx *vtx;
	struct r600_bc_tex *tex;
	uint32_t literal[4];
	unsigned nliteral;
	unsigned addr;
	int i, r;

	if (bc->callstack[0].max > 0)
		bc->nstack = ((bc->callstack[0].max + 3) >> 2) + 2;
	if (bc->type == TGSI_PROCESSOR_VERTEX && !bc->nstack) {
		bc->nstack = 1;
	}

	/* first path compute addr of each CF block */
	/* addr start after all the CF instructions */
	addr = bc->cf_last->id + 2;
	LIST_FOR_EACH_ENTRY(cf, &bc->cf, list) {
		switch (cf->inst) {
		case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU << 3):
		case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU_POP_AFTER << 3):
		case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU_POP2_AFTER << 3):
		case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU_PUSH_BEFORE << 3):
			break;
		case V_SQ_CF_WORD1_SQ_CF_INST_TEX:
		case V_SQ_CF_WORD1_SQ_CF_INST_VTX:
		case V_SQ_CF_WORD1_SQ_CF_INST_VTX_TC:
			/* fetch node need to be 16 bytes aligned*/
			addr += 3;
			addr &= 0xFFFFFFFCUL;
			break;
		case V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT:
		case V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT_DONE:
		case EG_V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT:
		case EG_V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT_DONE:
			break;
		case V_SQ_CF_WORD1_SQ_CF_INST_JUMP:
		case V_SQ_CF_WORD1_SQ_CF_INST_ELSE:
		case V_SQ_CF_WORD1_SQ_CF_INST_POP:
		case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_START_NO_AL:
		case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_END:
		case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_CONTINUE:
		case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_BREAK:
		case V_SQ_CF_WORD1_SQ_CF_INST_CALL_FS:
		case V_SQ_CF_WORD1_SQ_CF_INST_RETURN:
		case CM_V_SQ_CF_WORD1_SQ_CF_INST_END:
			break;
		default:
			R600_ERR("unsupported CF instruction (0x%X)\n", cf->inst);
			return -EINVAL;
		}
		cf->addr = addr;
		addr += cf->ndw;
		bc->ndw = cf->addr + cf->ndw;
	}
	free(bc->bytecode);
	bc->bytecode = calloc(1, bc->ndw * 4);
	if (bc->bytecode == NULL)
		return -ENOMEM;
	LIST_FOR_EACH_ENTRY(cf, &bc->cf, list) {
		addr = cf->addr;
		if (bc->chiprev >= CHIPREV_EVERGREEN)
			r = eg_bc_cf_build(bc, cf);
		else
			r = r600_bc_cf_build(bc, cf);
		if (r)
			return r;
		switch (cf->inst) {
		case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU << 3):
		case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU_POP_AFTER << 3):
		case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU_POP2_AFTER << 3):
		case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU_PUSH_BEFORE << 3):
			nliteral = 0;
			memset(literal, 0, sizeof(literal));
			LIST_FOR_EACH_ENTRY(alu, &cf->alu, list) {
				r = r600_bc_alu_nliterals(bc, alu, literal, &nliteral);
				if (r)
					return r;
				r600_bc_alu_adjust_literals(bc, alu, literal, nliteral);
				switch(bc->chiprev) {
				case CHIPREV_R600:
					r = r600_bc_alu_build(bc, alu, addr);
					break;
				case CHIPREV_R700:
				case CHIPREV_EVERGREEN: /* eg alu is same encoding as r700 */
				case CHIPREV_CAYMAN: /* eg alu is same encoding as r700 */
					r = r700_bc_alu_build(bc, alu, addr);
					break;
				default:
					R600_ERR("unknown family %d\n", bc->family);
					return -EINVAL;
				}
				if (r)
					return r;
				addr += 2;
				if (alu->last) {
					for (i = 0; i < align(nliteral, 2); ++i) {
						bc->bytecode[addr++] = literal[i];
					}
					nliteral = 0;
					memset(literal, 0, sizeof(literal));
				}
			}
			break;
		case V_SQ_CF_WORD1_SQ_CF_INST_VTX:
		case V_SQ_CF_WORD1_SQ_CF_INST_VTX_TC:
			LIST_FOR_EACH_ENTRY(vtx, &cf->vtx, list) {
				r = r600_bc_vtx_build(bc, vtx, addr);
				if (r)
					return r;
				addr += 4;
			}
			break;
		case V_SQ_CF_WORD1_SQ_CF_INST_TEX:
			if (bc->chiprev == CHIPREV_CAYMAN) {
				LIST_FOR_EACH_ENTRY(vtx, &cf->vtx, list) {
					r = r600_bc_vtx_build(bc, vtx, addr);
					if (r)
						return r;
					addr += 4;
				}
			}
			LIST_FOR_EACH_ENTRY(tex, &cf->tex, list) {
				r = r600_bc_tex_build(bc, tex, addr);
				if (r)
					return r;
				addr += 4;
			}
			break;
		case V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT:
		case V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT_DONE:
		case EG_V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT:
		case EG_V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT_DONE:
		case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_START_NO_AL:
		case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_END:
		case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_CONTINUE:
		case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_BREAK:
		case V_SQ_CF_WORD1_SQ_CF_INST_JUMP:
		case V_SQ_CF_WORD1_SQ_CF_INST_ELSE:
		case V_SQ_CF_WORD1_SQ_CF_INST_POP:
		case V_SQ_CF_WORD1_SQ_CF_INST_CALL_FS:
		case V_SQ_CF_WORD1_SQ_CF_INST_RETURN:
		case CM_V_SQ_CF_WORD1_SQ_CF_INST_END:
			break;
		default:
			R600_ERR("unsupported CF instruction (0x%X)\n", cf->inst);
			return -EINVAL;
		}
	}
	return 0;
}

void r600_bc_clear(struct r600_bc *bc)
{
	struct r600_bc_cf *cf = NULL, *next_cf;

	free(bc->bytecode);
	bc->bytecode = NULL;

	LIST_FOR_EACH_ENTRY_SAFE(cf, next_cf, &bc->cf, list) {
		struct r600_bc_alu *alu = NULL, *next_alu;
		struct r600_bc_tex *tex = NULL, *next_tex;
		struct r600_bc_tex *vtx = NULL, *next_vtx;

		LIST_FOR_EACH_ENTRY_SAFE(alu, next_alu, &cf->alu, list) {
			free(alu);
		}

		LIST_INITHEAD(&cf->alu);

		LIST_FOR_EACH_ENTRY_SAFE(tex, next_tex, &cf->tex, list) {
			free(tex);
		}

		LIST_INITHEAD(&cf->tex);

		LIST_FOR_EACH_ENTRY_SAFE(vtx, next_vtx, &cf->vtx, list) {
			free(vtx);
		}

		LIST_INITHEAD(&cf->vtx);

		free(cf);
	}

	LIST_INITHEAD(&cf->list);
}

void r600_bc_dump(struct r600_bc *bc)
{
	struct r600_bc_cf *cf = NULL;
	struct r600_bc_alu *alu = NULL;
	struct r600_bc_vtx *vtx = NULL;
	struct r600_bc_tex *tex = NULL;

	unsigned i, id;
	uint32_t literal[4];
	unsigned nliteral;
	char chip = '6';

	switch (bc->chiprev) {
	case 1:
		chip = '7';
		break;
	case 2:
		chip = 'E';
		break;
	case 3:
		chip = 'C';
		break;
	case 0:
	default:
		chip = '6';
		break;
	}
	fprintf(stderr, "bytecode %d dw -- %d gprs ---------------------\n", bc->ndw, bc->ngpr);
	fprintf(stderr, "     %c\n", chip);

	LIST_FOR_EACH_ENTRY(cf, &bc->cf, list) {
		id = cf->id;

		switch (cf->inst) {
		case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU << 3):
		case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU_POP_AFTER << 3):
		case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU_POP2_AFTER << 3):
		case (V_SQ_CF_ALU_WORD1_SQ_CF_INST_ALU_PUSH_BEFORE << 3):
			fprintf(stderr, "%04d %08X ALU ", id, bc->bytecode[id]);
			fprintf(stderr, "ADDR:%d ", cf->addr);
			fprintf(stderr, "KCACHE_MODE0:%X ", cf->kcache[0].mode);
			fprintf(stderr, "KCACHE_BANK0:%X ", cf->kcache[0].bank);
			fprintf(stderr, "KCACHE_BANK1:%X\n", cf->kcache[1].bank);
			id++;
			fprintf(stderr, "%04d %08X ALU ", id, bc->bytecode[id]);
			fprintf(stderr, "INST:%d ", cf->inst);
			fprintf(stderr, "KCACHE_MODE1:%X ", cf->kcache[1].mode);
			fprintf(stderr, "KCACHE_ADDR0:%X ", cf->kcache[0].addr);
			fprintf(stderr, "KCACHE_ADDR1:%X ", cf->kcache[1].addr);
			fprintf(stderr, "COUNT:%d\n", cf->ndw / 2);
			break;
		case V_SQ_CF_WORD1_SQ_CF_INST_TEX:
		case V_SQ_CF_WORD1_SQ_CF_INST_VTX:
		case V_SQ_CF_WORD1_SQ_CF_INST_VTX_TC:
			fprintf(stderr, "%04d %08X TEX/VTX ", id, bc->bytecode[id]);
			fprintf(stderr, "ADDR:%d\n", cf->addr);
			id++;
			fprintf(stderr, "%04d %08X TEX/VTX ", id, bc->bytecode[id]);
			fprintf(stderr, "INST:%d ", cf->inst);
			fprintf(stderr, "COUNT:%d\n", cf->ndw / 4);
			break;
		case V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT:
		case V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT_DONE:
		case EG_V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT:
		case EG_V_SQ_CF_ALLOC_EXPORT_WORD1_SQ_CF_INST_EXPORT_DONE:
			fprintf(stderr, "%04d %08X EXPORT ", id, bc->bytecode[id]);
			fprintf(stderr, "GPR:%X ", cf->output.gpr);
			fprintf(stderr, "ELEM_SIZE:%X ", cf->output.elem_size);
			fprintf(stderr, "ARRAY_BASE:%X ", cf->output.array_base);
			fprintf(stderr, "TYPE:%X\n", cf->output.type);
			id++;
			fprintf(stderr, "%04d %08X EXPORT ", id, bc->bytecode[id]);
			fprintf(stderr, "SWIZ_X:%X ", cf->output.swizzle_x);
			fprintf(stderr, "SWIZ_Y:%X ", cf->output.swizzle_y);
			fprintf(stderr, "SWIZ_Z:%X ", cf->output.swizzle_z);
			fprintf(stderr, "SWIZ_W:%X ", cf->output.swizzle_w);
			fprintf(stderr, "BARRIER:%X ", cf->output.barrier);
			fprintf(stderr, "INST:%d ", cf->output.inst);
			fprintf(stderr, "BURST_COUNT:%d ", cf->output.burst_count);
			fprintf(stderr, "EOP:%X\n", cf->output.end_of_program);
			break;
		case V_SQ_CF_WORD1_SQ_CF_INST_JUMP:
		case V_SQ_CF_WORD1_SQ_CF_INST_ELSE:
		case V_SQ_CF_WORD1_SQ_CF_INST_POP:
		case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_START_NO_AL:
		case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_END:
		case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_CONTINUE:
		case V_SQ_CF_WORD1_SQ_CF_INST_LOOP_BREAK:
		case V_SQ_CF_WORD1_SQ_CF_INST_CALL_FS:
		case V_SQ_CF_WORD1_SQ_CF_INST_RETURN:
		case CM_V_SQ_CF_WORD1_SQ_CF_INST_END:
			fprintf(stderr, "%04d %08X CF ", id, bc->bytecode[id]);
			fprintf(stderr, "ADDR:%d\n", cf->cf_addr);
			id++;
			fprintf(stderr, "%04d %08X CF ", id, bc->bytecode[id]);
			fprintf(stderr, "INST:%d ", cf->inst);
			fprintf(stderr, "COND:%X ", cf->cond);
			fprintf(stderr, "POP_COUNT:%X\n", cf->pop_count);
			break;
		}

		id = cf->addr;
		nliteral = 0;
		LIST_FOR_EACH_ENTRY(alu, &cf->alu, list) {
			r600_bc_alu_nliterals(bc, alu, literal, &nliteral);

			fprintf(stderr, "%04d %08X   ", id, bc->bytecode[id]);
			fprintf(stderr, "SRC0(SEL:%d ", alu->src[0].sel);
			fprintf(stderr, "REL:%d ", alu->src[0].rel);
			fprintf(stderr, "CHAN:%d ", alu->src[0].chan);
			fprintf(stderr, "NEG:%d) ", alu->src[0].neg);
			fprintf(stderr, "SRC1(SEL:%d ", alu->src[1].sel);
			fprintf(stderr, "REL:%d ", alu->src[1].rel);
			fprintf(stderr, "CHAN:%d ", alu->src[1].chan);
			fprintf(stderr, "NEG:%d) ", alu->src[1].neg);
			fprintf(stderr, "LAST:%d)\n", alu->last);
			id++;
			fprintf(stderr, "%04d %08X %c ", id, bc->bytecode[id], alu->last ? '*' : ' ');
			fprintf(stderr, "INST:%d ", alu->inst);
			fprintf(stderr, "DST(SEL:%d ", alu->dst.sel);
			fprintf(stderr, "CHAN:%d ", alu->dst.chan);
			fprintf(stderr, "REL:%d ", alu->dst.rel);
			fprintf(stderr, "CLAMP:%d) ", alu->dst.clamp);
			fprintf(stderr, "BANK_SWIZZLE:%d ", alu->bank_swizzle);
			if (alu->is_op3) {
				fprintf(stderr, "SRC2(SEL:%d ", alu->src[2].sel);
				fprintf(stderr, "REL:%d ", alu->src[2].rel);
				fprintf(stderr, "CHAN:%d ", alu->src[2].chan);
				fprintf(stderr, "NEG:%d)\n", alu->src[2].neg);
			} else {
				fprintf(stderr, "SRC0_ABS:%d ", alu->src[0].abs);
				fprintf(stderr, "SRC1_ABS:%d ", alu->src[1].abs);
				fprintf(stderr, "WRITE_MASK:%d ", alu->dst.write);
				fprintf(stderr, "OMOD:%d ", alu->omod);
				fprintf(stderr, "EXECUTE_MASK:%d ", alu->predicate);
				fprintf(stderr, "UPDATE_PRED:%d\n", alu->predicate);
			}

			id++;
			if (alu->last) {
				for (i = 0; i < nliteral; i++, id++) {
					float *f = (float*)(bc->bytecode + id);
					fprintf(stderr, "%04d %08X\t%f\n", id, bc->bytecode[id], *f);
				}
				id += nliteral & 1;
				nliteral = 0;
			}
		}

		LIST_FOR_EACH_ENTRY(tex, &cf->tex, list) {
			fprintf(stderr, "%04d %08X   ", id, bc->bytecode[id]);
			fprintf(stderr, "INST:%d ", tex->inst);
			fprintf(stderr, "RESOURCE_ID:%d ", tex->resource_id);
			fprintf(stderr, "SRC(GPR:%d ", tex->src_gpr);
			fprintf(stderr, "REL:%d)\n", tex->src_rel);
			id++;
			fprintf(stderr, "%04d %08X   ", id, bc->bytecode[id]);
			fprintf(stderr, "DST(GPR:%d ", tex->dst_gpr);
			fprintf(stderr, "REL:%d ", tex->dst_rel);
			fprintf(stderr, "SEL_X:%d ", tex->dst_sel_x);
			fprintf(stderr, "SEL_Y:%d ", tex->dst_sel_y);
			fprintf(stderr, "SEL_Z:%d ", tex->dst_sel_z);
			fprintf(stderr, "SEL_W:%d) ", tex->dst_sel_w);
			fprintf(stderr, "LOD_BIAS:%d ", tex->lod_bias);
			fprintf(stderr, "COORD_TYPE_X:%d ", tex->coord_type_x);
			fprintf(stderr, "COORD_TYPE_Y:%d ", tex->coord_type_y);
			fprintf(stderr, "COORD_TYPE_Z:%d ", tex->coord_type_z);
			fprintf(stderr, "COORD_TYPE_W:%d\n", tex->coord_type_w);
			id++;
			fprintf(stderr, "%04d %08X   ", id, bc->bytecode[id]);
			fprintf(stderr, "OFFSET_X:%d ", tex->offset_x);
			fprintf(stderr, "OFFSET_Y:%d ", tex->offset_y);
			fprintf(stderr, "OFFSET_Z:%d ", tex->offset_z);
			fprintf(stderr, "SAMPLER_ID:%d ", tex->sampler_id);
			fprintf(stderr, "SRC(SEL_X:%d ", tex->src_sel_x);
			fprintf(stderr, "SEL_Y:%d ", tex->src_sel_y);
			fprintf(stderr, "SEL_Z:%d ", tex->src_sel_z);
			fprintf(stderr, "SEL_W:%d)\n", tex->src_sel_w);
			id++;
			fprintf(stderr, "%04d %08X   \n", id, bc->bytecode[id]);
			id++;
		}

		LIST_FOR_EACH_ENTRY(vtx, &cf->vtx, list) {
			fprintf(stderr, "%04d %08X   ", id, bc->bytecode[id]);
			fprintf(stderr, "INST:%d ", vtx->inst);
			fprintf(stderr, "FETCH_TYPE:%d ", vtx->fetch_type);
			fprintf(stderr, "BUFFER_ID:%d\n", vtx->buffer_id);
			id++;
			/* This assumes that no semantic fetches exist */
			fprintf(stderr, "%04d %08X   ", id, bc->bytecode[id]);
			fprintf(stderr, "SRC(GPR:%d ", vtx->src_gpr);
			fprintf(stderr, "SEL_X:%d) ", vtx->src_sel_x);
			if (bc->chiprev < CHIPREV_CAYMAN)
				fprintf(stderr, "MEGA_FETCH_COUNT:%d ", vtx->mega_fetch_count);
			else
				fprintf(stderr, "SEL_Y:%d) ", 0);
			fprintf(stderr, "DST(GPR:%d ", vtx->dst_gpr);
			fprintf(stderr, "SEL_X:%d ", vtx->dst_sel_x);
			fprintf(stderr, "SEL_Y:%d ", vtx->dst_sel_y);
			fprintf(stderr, "SEL_Z:%d ", vtx->dst_sel_z);
			fprintf(stderr, "SEL_W:%d) ", vtx->dst_sel_w);
			fprintf(stderr, "USE_CONST_FIELDS:%d ", vtx->use_const_fields);
			fprintf(stderr, "FORMAT(DATA:%d ", vtx->data_format);
			fprintf(stderr, "NUM:%d ", vtx->num_format_all);
			fprintf(stderr, "COMP:%d ", vtx->format_comp_all);
			fprintf(stderr, "MODE:%d)\n", vtx->srf_mode_all);
			id++;
			fprintf(stderr, "%04d %08X   ", id, bc->bytecode[id]);
			fprintf(stderr, "ENDIAN:%d ", vtx->endian);
			fprintf(stderr, "OFFSET:%d\n", vtx->offset);
			/* TODO */
			id++;
			fprintf(stderr, "%04d %08X   \n", id, bc->bytecode[id]);
			id++;
		}
	}

	fprintf(stderr, "--------------------------------------\n");
}

static void r600_vertex_data_type(enum pipe_format pformat, unsigned *format,
				unsigned *num_format, unsigned *format_comp, unsigned *endian)
{
	const struct util_format_description *desc;
	unsigned i;

	*format = 0;
	*num_format = 0;
	*format_comp = 0;
	*endian = ENDIAN_NONE;

	desc = util_format_description(pformat);
	if (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN) {
		goto out_unknown;
	}

	/* Find the first non-VOID channel. */
	for (i = 0; i < 4; i++) {
		if (desc->channel[i].type != UTIL_FORMAT_TYPE_VOID) {
			break;
		}
	}

	*endian = r600_endian_swap(desc->channel[i].size);

	switch (desc->channel[i].type) {
	/* Half-floats, floats, ints */
	case UTIL_FORMAT_TYPE_FLOAT:
		switch (desc->channel[i].size) {
		case 16:
			switch (desc->nr_channels) {
			case 1:
				*format = FMT_16_FLOAT;
				break;
			case 2:
				*format = FMT_16_16_FLOAT;
				break;
			case 3:
			case 4:
				*format = FMT_16_16_16_16_FLOAT;
				break;
			}
			break;
		case 32:
			switch (desc->nr_channels) {
			case 1:
				*format = FMT_32_FLOAT;
				break;
			case 2:
				*format = FMT_32_32_FLOAT;
				break;
			case 3:
				*format = FMT_32_32_32_FLOAT;
				break;
			case 4:
				*format = FMT_32_32_32_32_FLOAT;
				break;
			}
			break;
		default:
			goto out_unknown;
		}
		break;
		/* Unsigned ints */
	case UTIL_FORMAT_TYPE_UNSIGNED:
		/* Signed ints */
	case UTIL_FORMAT_TYPE_SIGNED:
		switch (desc->channel[i].size) {
		case 8:
			switch (desc->nr_channels) {
			case 1:
				*format = FMT_8;
				break;
			case 2:
				*format = FMT_8_8;
				break;
			case 3:
			case 4:
				*format = FMT_8_8_8_8;
				break;
			}
			break;
		case 16:
			switch (desc->nr_channels) {
			case 1:
				*format = FMT_16;
				break;
			case 2:
				*format = FMT_16_16;
				break;
			case 3:
			case 4:
				*format = FMT_16_16_16_16;
				break;
			}
			break;
		case 32:
			switch (desc->nr_channels) {
			case 1:
				*format = FMT_32;
				break;
			case 2:
				*format = FMT_32_32;
				break;
			case 3:
				*format = FMT_32_32_32;
				break;
			case 4:
				*format = FMT_32_32_32_32;
				break;
			}
			break;
		default:
			goto out_unknown;
		}
		break;
	default:
		goto out_unknown;
	}

	if (desc->channel[i].type == UTIL_FORMAT_TYPE_SIGNED) {
		*format_comp = 1;
	}
	if (desc->channel[i].normalized) {
		*num_format = 0;
	} else {
		*num_format = 2;
	}
	return;
out_unknown:
	R600_ERR("unsupported vertex format %s\n", util_format_name(pformat));
}

int r600_vertex_elements_build_fetch_shader(struct r600_pipe_context *rctx, struct r600_vertex_element *ve)
{
	static int dump_shaders = -1;

	struct r600_bc bc;
	struct r600_bc_vtx vtx;
	struct pipe_vertex_element *elements = ve->elements;
	const struct util_format_description *desc;
	unsigned fetch_resource_start = rctx->family >= CHIP_CEDAR ? 0 : 160;
	unsigned format, num_format, format_comp, endian;
	u32 *bytecode;
	int i, r;

	/* Vertex element offsets need special handling. If the offset is
	 * bigger than what we can put in the fetch instruction we need to
	 * alter the vertex resource offset. In order to simplify code we
	 * will bind one resource per element in such cases. It's a worst
	 * case scenario. */
	for (i = 0; i < ve->count; i++) {
		ve->vbuffer_offset[i] = C_SQ_VTX_WORD2_OFFSET & elements[i].src_offset;
		if (ve->vbuffer_offset[i]) {
			ve->vbuffer_need_offset = 1;
		}
	}

	memset(&bc, 0, sizeof(bc));
	r = r600_bc_init(&bc, r600_get_family(rctx->radeon));
	if (r)
		return r;

	for (i = 0; i < ve->count; i++) {
		if (elements[i].instance_divisor > 1) {
			struct r600_bc_alu alu;

			memset(&alu, 0, sizeof(alu));
			alu.inst = BC_INST(&bc, V_SQ_ALU_WORD1_OP2_SQ_OP2_INST_MULHI_UINT);
			alu.src[0].sel = 0;
			alu.src[0].chan = 3;

			alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
			alu.src[1].value = (1ll << 32) / elements[i].instance_divisor + 1;

			alu.dst.sel = i + 1;
			alu.dst.chan = 3;
			alu.dst.write = 1;
			alu.last = 1;

			if ((r = r600_bc_add_alu(&bc, &alu))) {
				r600_bc_clear(&bc);
				return r;
			}
		}
	}

	for (i = 0; i < ve->count; i++) {
		unsigned vbuffer_index;
		r600_vertex_data_type(ve->elements[i].src_format, &format, &num_format, &format_comp, &endian);
		desc = util_format_description(ve->elements[i].src_format);
		if (desc == NULL) {
			r600_bc_clear(&bc);
			R600_ERR("unknown format %d\n", ve->elements[i].src_format);
			return -EINVAL;
		}

		/* see above for vbuffer_need_offset explanation */
		vbuffer_index = elements[i].vertex_buffer_index;
		memset(&vtx, 0, sizeof(vtx));
		vtx.buffer_id = (ve->vbuffer_need_offset ? i : vbuffer_index) + fetch_resource_start;
		vtx.fetch_type = elements[i].instance_divisor ? 1 : 0;
		vtx.src_gpr = elements[i].instance_divisor > 1 ? i + 1 : 0;
		vtx.src_sel_x = elements[i].instance_divisor ? 3 : 0;
		vtx.mega_fetch_count = 0x1F;
		vtx.dst_gpr = i + 1;
		vtx.dst_sel_x = desc->swizzle[0];
		vtx.dst_sel_y = desc->swizzle[1];
		vtx.dst_sel_z = desc->swizzle[2];
		vtx.dst_sel_w = desc->swizzle[3];
		vtx.data_format = format;
		vtx.num_format_all = num_format;
		vtx.format_comp_all = format_comp;
		vtx.srf_mode_all = 1;
		vtx.offset = elements[i].src_offset;
		vtx.endian = endian;

		if ((r = r600_bc_add_vtx(&bc, &vtx))) {
			r600_bc_clear(&bc);
			return r;
		}
	}

	r600_bc_add_cfinst(&bc, BC_INST(&bc, V_SQ_CF_WORD1_SQ_CF_INST_RETURN));

	if ((r = r600_bc_build(&bc))) {
		r600_bc_clear(&bc);
		return r;
	}

	if (dump_shaders == -1)
		dump_shaders = debug_get_bool_option("R600_DUMP_SHADERS", FALSE);

	if (dump_shaders) {
		fprintf(stderr, "--------------------------------------------------------------\n");
		r600_bc_dump(&bc);
		fprintf(stderr, "______________________________________________________________\n");
	}

	ve->fs_size = bc.ndw*4;

	/* use PIPE_BIND_VERTEX_BUFFER so we use the cache buffer manager */
	ve->fetch_shader = r600_bo(rctx->radeon, ve->fs_size, 256, PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_IMMUTABLE);
	if (ve->fetch_shader == NULL) {
		r600_bc_clear(&bc);
		return -ENOMEM;
	}

	bytecode = r600_bo_map(rctx->radeon, ve->fetch_shader, 0, NULL);
	if (bytecode == NULL) {
		r600_bc_clear(&bc);
		r600_bo_reference(rctx->radeon, &ve->fetch_shader, NULL);
		return -ENOMEM;
	}

	if (R600_BIG_ENDIAN) {
		for (i = 0; i < ve->fs_size / 4; ++i) {
			bytecode[i] = bswap_32(bc.bytecode[i]);
		}
	} else {
		memcpy(bytecode, bc.bytecode, ve->fs_size);
	}

	r600_bo_unmap(rctx->radeon, ve->fetch_shader);
	r600_bc_clear(&bc);

	if (rctx->family >= CHIP_CEDAR)
		evergreen_fetch_shader(&rctx->context, ve);
	else
		r600_fetch_shader(&rctx->context, ve);

	return 0;
}
