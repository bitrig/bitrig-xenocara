/*
 * Copyright (C) 2009 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "nouveau_driver.h"
#include "nouveau_context.h"
#include "nouveau_gldefs.h"
#include "nouveau_texture.h"
#include "nouveau_class.h"
#include "nouveau_util.h"
#include "nv10_driver.h"

void
nv10_emit_tex_gen(GLcontext *ctx, int emit)
{
}

static uint32_t
get_tex_format_pot(struct gl_texture_image *ti)
{
	switch (ti->TexFormat) {
	case MESA_FORMAT_ARGB8888:
		return NV10TCL_TX_FORMAT_FORMAT_A8R8G8B8;

	case MESA_FORMAT_XRGB8888:
		return NV10TCL_TX_FORMAT_FORMAT_X8R8G8B8;

	case MESA_FORMAT_ARGB1555:
		return NV10TCL_TX_FORMAT_FORMAT_A1R5G5B5;

	case MESA_FORMAT_ARGB4444:
		return NV10TCL_TX_FORMAT_FORMAT_A4R4G4B4;

	case MESA_FORMAT_RGB565:
		return NV10TCL_TX_FORMAT_FORMAT_R5G6B5;

	case MESA_FORMAT_A8:
	case MESA_FORMAT_I8:
		return NV10TCL_TX_FORMAT_FORMAT_A8;

	case MESA_FORMAT_L8:
		return NV10TCL_TX_FORMAT_FORMAT_L8;

	case MESA_FORMAT_CI8:
		return NV10TCL_TX_FORMAT_FORMAT_INDEX8;

	default:
		assert(0);
	}
}

static uint32_t
get_tex_format_rect(struct gl_texture_image *ti)
{
	switch (ti->TexFormat) {
	case MESA_FORMAT_ARGB1555:
		return NV10TCL_TX_FORMAT_FORMAT_A1R5G5B5_RECT;

	case MESA_FORMAT_RGB565:
		return NV10TCL_TX_FORMAT_FORMAT_R5G6B5_RECT;

	case MESA_FORMAT_ARGB8888:
	case MESA_FORMAT_XRGB8888:
		return NV10TCL_TX_FORMAT_FORMAT_A8R8G8B8_RECT;

	case MESA_FORMAT_A8:
	case MESA_FORMAT_L8:
	case MESA_FORMAT_I8:
		return NV10TCL_TX_FORMAT_FORMAT_A8_RECT;

	default:
		assert(0);
	}
}

void
nv10_emit_tex_obj(GLcontext *ctx, int emit)
{
	const int i = emit - NOUVEAU_STATE_TEX_OBJ0;
	struct nouveau_channel *chan = context_chan(ctx);
	struct nouveau_grobj *celsius = context_eng3d(ctx);
	struct nouveau_bo_context *bctx = context_bctx_i(ctx, TEXTURE, i);
	const int bo_flags = NOUVEAU_BO_RD | NOUVEAU_BO_GART | NOUVEAU_BO_VRAM;
	struct gl_texture_object *t;
	struct nouveau_surface *s;
	struct gl_texture_image *ti;
	uint32_t tx_format, tx_filter, tx_enable;

	if (!ctx->Texture.Unit[i]._ReallyEnabled) {
		BEGIN_RING(chan, celsius, NV10TCL_TX_ENABLE(i), 1);
		OUT_RING(chan, 0);
		return;
	}

	t = ctx->Texture.Unit[i]._Current;
	s = &to_nouveau_texture(t)->surfaces[t->BaseLevel];
	ti = t->Image[0][t->BaseLevel];

	if (!nouveau_texture_validate(ctx, t))
		return;

	/* Recompute the texturing registers. */
	tx_format = nvgl_wrap_mode(t->WrapT) << 28
		| nvgl_wrap_mode(t->WrapS) << 24
		| ti->HeightLog2 << 20
		| ti->WidthLog2 << 16
		| 5 << 4 | 1 << 12;

	tx_filter = nvgl_filter_mode(t->MagFilter) << 28
		| nvgl_filter_mode(t->MinFilter) << 24;

	tx_enable = NV10TCL_TX_ENABLE_ENABLE
		| log2i(t->MaxAnisotropy) << 4;

	if (t->Target == GL_TEXTURE_RECTANGLE) {
		BEGIN_RING(chan, celsius, NV10TCL_TX_NPOT_PITCH(i), 1);
		OUT_RING(chan, s->pitch << 16);
		BEGIN_RING(chan, celsius, NV10TCL_TX_NPOT_SIZE(i), 1);
		OUT_RING(chan, align(s->width, 2) << 16 | s->height);

		tx_format |= get_tex_format_rect(ti);
	} else {
		tx_format |= get_tex_format_pot(ti);
	}

	if (t->MinFilter != GL_NEAREST &&
	    t->MinFilter != GL_LINEAR) {
		int lod_min = t->MinLod;
		int lod_max = MIN2(t->MaxLod, t->_MaxLambda);
		int lod_bias = t->LodBias
			+ ctx->Texture.Unit[i].LodBias;

		lod_max = CLAMP(lod_max, 0, 15);
		lod_min = CLAMP(lod_min, 0, 15);
		lod_bias = CLAMP(lod_bias, 0, 15);

		tx_format |= NV10TCL_TX_FORMAT_MIPMAP;
		tx_filter |= lod_bias << 8;
		tx_enable |= lod_min << 26
			| lod_max << 14;
	}

	/* Write it to the hardware. */
	nouveau_bo_mark(bctx, celsius, NV10TCL_TX_FORMAT(i),
			s->bo, tx_format, 0,
			NV10TCL_TX_FORMAT_DMA0,
			NV10TCL_TX_FORMAT_DMA1,
			bo_flags | NOUVEAU_BO_OR);

	nouveau_bo_markl(bctx, celsius, NV10TCL_TX_OFFSET(i),
			 s->bo, 0, bo_flags);

	BEGIN_RING(chan, celsius, NV10TCL_TX_FILTER(i), 1);
	OUT_RING(chan, tx_filter);

	BEGIN_RING(chan, celsius, NV10TCL_TX_ENABLE(i), 1);
	OUT_RING(chan, tx_enable);
}

