/*
 * Copyright (c) 2014 Scott Mansell
 * Copyright © 2014 Broadcom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <inttypes.h>
#include "pipe/p_state.h"
#include "util/u_format.h"
#include "util/u_hash_table.h"
#include "util/u_hash.h"
#include "util/u_memory.h"
#include "util/u_pack_color.h"
#include "util/format_srgb.h"
#include "util/ralloc.h"
#include "util/hash_table.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_info.h"
#include "tgsi/tgsi_lowering.h"

#include "vc4_context.h"
#include "vc4_qpu.h"
#include "vc4_qir.h"
#ifdef USE_VC4_SIMULATOR
#include "simpenrose/simpenrose.h"
#endif

struct vc4_key {
        struct vc4_uncompiled_shader *shader_state;
        struct {
                enum pipe_format format;
                unsigned compare_mode:1;
                unsigned compare_func:3;
                unsigned wrap_s:3;
                unsigned wrap_t:3;
                uint8_t swizzle[4];
        } tex[VC4_MAX_TEXTURE_SAMPLERS];
        uint8_t ucp_enables;
};

struct vc4_fs_key {
        struct vc4_key base;
        enum pipe_format color_format;
        bool depth_enabled;
        bool stencil_enabled;
        bool stencil_twoside;
        bool stencil_full_writemasks;
        bool is_points;
        bool is_lines;
        bool alpha_test;
        bool point_coord_upper_left;
        bool light_twoside;
        uint8_t alpha_test_func;
        uint32_t point_sprite_mask;

        struct pipe_rt_blend_state blend;
};

struct vc4_vs_key {
        struct vc4_key base;

        /**
         * This is a proxy for the array of FS input semantics, which is
         * larger than we would want to put in the key.
         */
        uint64_t compiled_fs_id;

        enum pipe_format attr_formats[8];
        bool is_coord;
        bool per_vertex_point_size;
};

static void
resize_qreg_array(struct vc4_compile *c,
                  struct qreg **regs,
                  uint32_t *size,
                  uint32_t decl_size)
{
        if (*size >= decl_size)
                return;

        uint32_t old_size = *size;
        *size = MAX2(*size * 2, decl_size);
        *regs = reralloc(c, *regs, struct qreg, *size);
        if (!*regs) {
                fprintf(stderr, "Malloc failure\n");
                abort();
        }

        for (uint32_t i = old_size; i < *size; i++)
                (*regs)[i] = c->undef;
}

static struct qreg
add_uniform(struct vc4_compile *c,
            enum quniform_contents contents,
            uint32_t data)
{
        for (int i = 0; i < c->num_uniforms; i++) {
                if (c->uniform_contents[i] == contents &&
                    c->uniform_data[i] == data) {
                        return (struct qreg) { QFILE_UNIF, i };
                }
        }

        uint32_t uniform = c->num_uniforms++;
        struct qreg u = { QFILE_UNIF, uniform };

        if (uniform >= c->uniform_array_size) {
                c->uniform_array_size = MAX2(MAX2(16, uniform + 1),
                                             c->uniform_array_size * 2);

                c->uniform_data = reralloc(c, c->uniform_data,
                                           uint32_t,
                                           c->uniform_array_size);
                c->uniform_contents = reralloc(c, c->uniform_contents,
                                               enum quniform_contents,
                                               c->uniform_array_size);
        }

        c->uniform_contents[uniform] = contents;
        c->uniform_data[uniform] = data;

        return u;
}

static struct qreg
get_temp_for_uniform(struct vc4_compile *c, enum quniform_contents contents,
                     uint32_t data)
{
        struct qreg u = add_uniform(c, contents, data);
        struct qreg t = qir_MOV(c, u);
        return t;
}

static struct qreg
qir_uniform_ui(struct vc4_compile *c, uint32_t ui)
{
        return get_temp_for_uniform(c, QUNIFORM_CONSTANT, ui);
}

static struct qreg
qir_uniform_f(struct vc4_compile *c, float f)
{
        return qir_uniform_ui(c, fui(f));
}

static struct qreg
indirect_uniform_load(struct vc4_compile *c,
                      struct tgsi_full_src_register *src, int swiz)
{
        struct tgsi_ind_register *indirect = &src->Indirect;
        struct vc4_compiler_ubo_range *range = &c->ubo_ranges[indirect->ArrayID];
        if (!range->used) {
                range->used = true;
                range->dst_offset = c->next_ubo_dst_offset;
                c->next_ubo_dst_offset += range->size;
                c->num_ubo_ranges++;
        };

        assert(src->Register.Indirect);
        assert(indirect->File == TGSI_FILE_ADDRESS);

        struct qreg addr_val = c->addr[indirect->Swizzle];
        struct qreg indirect_offset =
                qir_ADD(c, addr_val, qir_uniform_ui(c,
                                                    range->dst_offset +
                                                    (src->Register.Index * 16)+
                                                    swiz * 4));
        indirect_offset = qir_MIN(c, indirect_offset, qir_uniform_ui(c, (range->dst_offset +
                                                                         range->size - 4)));

        qir_TEX_DIRECT(c, indirect_offset, add_uniform(c, QUNIFORM_UBO_ADDR, 0));
        struct qreg r4 = qir_TEX_RESULT(c);
        c->num_texture_samples++;
        return qir_MOV(c, r4);
}

static struct qreg
get_src(struct vc4_compile *c, unsigned tgsi_op,
        struct tgsi_full_src_register *full_src, int i)
{
        struct tgsi_src_register *src = &full_src->Register;
        struct qreg r = c->undef;

        uint32_t s = i;
        switch (i) {
        case TGSI_SWIZZLE_X:
                s = src->SwizzleX;
                break;
        case TGSI_SWIZZLE_Y:
                s = src->SwizzleY;
                break;
        case TGSI_SWIZZLE_Z:
                s = src->SwizzleZ;
                break;
        case TGSI_SWIZZLE_W:
                s = src->SwizzleW;
                break;
        default:
                abort();
        }

        switch (src->File) {
        case TGSI_FILE_NULL:
                return r;
        case TGSI_FILE_TEMPORARY:
                r = c->temps[src->Index * 4 + s];
                break;
        case TGSI_FILE_IMMEDIATE:
                r = c->consts[src->Index * 4 + s];
                break;
        case TGSI_FILE_CONSTANT:
                if (src->Indirect) {
                        r = indirect_uniform_load(c, full_src, s);
                } else {
                        r = get_temp_for_uniform(c, QUNIFORM_UNIFORM,
                                                 src->Index * 4 + s);
                }
                break;
        case TGSI_FILE_INPUT:
                r = c->inputs[src->Index * 4 + s];
                break;
        case TGSI_FILE_SAMPLER:
        case TGSI_FILE_SAMPLER_VIEW:
                r = c->undef;
                break;
        default:
                fprintf(stderr, "unknown src file %d\n", src->File);
                abort();
        }

        if (src->Absolute)
                r = qir_FMAXABS(c, r, r);

        if (src->Negate) {
                switch (tgsi_opcode_infer_src_type(tgsi_op)) {
                case TGSI_TYPE_SIGNED:
                case TGSI_TYPE_UNSIGNED:
                        r = qir_SUB(c, qir_uniform_ui(c, 0), r);
                        break;
                default:
                        r = qir_FSUB(c, qir_uniform_f(c, 0.0), r);
                        break;
                }
        }

        return r;
};


static void
update_dst(struct vc4_compile *c, struct tgsi_full_instruction *tgsi_inst,
           int i, struct qreg val)
{
        struct tgsi_dst_register *tgsi_dst = &tgsi_inst->Dst[0].Register;

        assert(!tgsi_dst->Indirect);

        switch (tgsi_dst->File) {
        case TGSI_FILE_TEMPORARY:
                c->temps[tgsi_dst->Index * 4 + i] = val;
                break;
        case TGSI_FILE_OUTPUT:
                c->outputs[tgsi_dst->Index * 4 + i] = val;
                c->num_outputs = MAX2(c->num_outputs,
                                      tgsi_dst->Index * 4 + i + 1);
                break;
        case TGSI_FILE_ADDRESS:
                assert(tgsi_dst->Index == 0);
                c->addr[i] = val;
                break;
        default:
                fprintf(stderr, "unknown dst file %d\n", tgsi_dst->File);
                abort();
        }
};

static struct qreg
get_swizzled_channel(struct vc4_compile *c,
                     struct qreg *srcs, int swiz)
{
        switch (swiz) {
        default:
        case UTIL_FORMAT_SWIZZLE_NONE:
                fprintf(stderr, "warning: unknown swizzle\n");
                /* FALLTHROUGH */
        case UTIL_FORMAT_SWIZZLE_0:
                return qir_uniform_f(c, 0.0);
        case UTIL_FORMAT_SWIZZLE_1:
                return qir_uniform_f(c, 1.0);
        case UTIL_FORMAT_SWIZZLE_X:
        case UTIL_FORMAT_SWIZZLE_Y:
        case UTIL_FORMAT_SWIZZLE_Z:
        case UTIL_FORMAT_SWIZZLE_W:
                return srcs[swiz];
        }
}

static struct qreg
tgsi_to_qir_alu(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        struct qreg dst = qir_get_temp(c);
        qir_emit(c, qir_inst4(op, dst,
                              src[0 * 4 + i],
                              src[1 * 4 + i],
                              src[2 * 4 + i],
                              c->undef));
        return dst;
}

static struct qreg
tgsi_to_qir_scalar(struct vc4_compile *c,
                   struct tgsi_full_instruction *tgsi_inst,
                   enum qop op, struct qreg *src, int i)
{
        struct qreg dst = qir_get_temp(c);
        qir_emit(c, qir_inst(op, dst,
                             src[0 * 4 + 0],
                             c->undef));
        return dst;
}

static struct qreg
tgsi_to_qir_rcp(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        struct qreg x = src[0 * 4 + 0];
        struct qreg r = qir_RCP(c, x);

        /* Apply a Newton-Raphson step to improve the accuracy. */
        r = qir_FMUL(c, r, qir_FSUB(c,
                                    qir_uniform_f(c, 2.0),
                                    qir_FMUL(c, x, r)));

        return r;
}

static struct qreg
tgsi_to_qir_rsq(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        struct qreg x = src[0 * 4 + 0];
        struct qreg r = qir_RSQ(c, x);

        /* Apply a Newton-Raphson step to improve the accuracy. */
        r = qir_FMUL(c, r, qir_FSUB(c,
                                    qir_uniform_f(c, 1.5),
                                    qir_FMUL(c,
                                             qir_uniform_f(c, 0.5),
                                             qir_FMUL(c, x,
                                                      qir_FMUL(c, r, r)))));

        return r;
}

static struct qreg
qir_srgb_decode(struct vc4_compile *c, struct qreg srgb)
{
        struct qreg low = qir_FMUL(c, srgb, qir_uniform_f(c, 1.0 / 12.92));
        struct qreg high = qir_POW(c,
                                   qir_FMUL(c,
                                            qir_FADD(c,
                                                     srgb,
                                                     qir_uniform_f(c, 0.055)),
                                            qir_uniform_f(c, 1.0 / 1.055)),
                                   qir_uniform_f(c, 2.4));

        qir_SF(c, qir_FSUB(c, srgb, qir_uniform_f(c, 0.04045)));
        return qir_SEL_X_Y_NS(c, low, high);
}

static struct qreg
qir_srgb_encode(struct vc4_compile *c, struct qreg linear)
{
        struct qreg low = qir_FMUL(c, linear, qir_uniform_f(c, 12.92));
        struct qreg high = qir_FSUB(c,
                                    qir_FMUL(c,
                                             qir_uniform_f(c, 1.055),
                                             qir_POW(c,
                                                     linear,
                                                     qir_uniform_f(c, 0.41666))),
                                    qir_uniform_f(c, 0.055));

        qir_SF(c, qir_FSUB(c, linear, qir_uniform_f(c, 0.0031308)));
        return qir_SEL_X_Y_NS(c, low, high);
}

static struct qreg
tgsi_to_qir_umul(struct vc4_compile *c,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg *src, int i)
{
        struct qreg src0_hi = qir_SHR(c, src[0 * 4 + i],
                                      qir_uniform_ui(c, 16));
        struct qreg src0_lo = qir_AND(c, src[0 * 4 + i],
                                      qir_uniform_ui(c, 0xffff));
        struct qreg src1_hi = qir_SHR(c, src[1 * 4 + i],
                                      qir_uniform_ui(c, 16));
        struct qreg src1_lo = qir_AND(c, src[1 * 4 + i],
                                      qir_uniform_ui(c, 0xffff));

        struct qreg hilo = qir_MUL24(c, src0_hi, src1_lo);
        struct qreg lohi = qir_MUL24(c, src0_lo, src1_hi);
        struct qreg lolo = qir_MUL24(c, src0_lo, src1_lo);

        return qir_ADD(c, lolo, qir_SHL(c,
                                        qir_ADD(c, hilo, lohi),
                                        qir_uniform_ui(c, 16)));
}

static struct qreg
tgsi_to_qir_idiv(struct vc4_compile *c,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg *src, int i)
{
        return qir_FTOI(c, qir_FMUL(c,
                                    qir_ITOF(c, src[0 * 4 + i]),
                                    qir_RCP(c, qir_ITOF(c, src[1 * 4 + i]))));
}

static struct qreg
tgsi_to_qir_ineg(struct vc4_compile *c,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg *src, int i)
{
        return qir_SUB(c, qir_uniform_ui(c, 0), src[0 * 4 + i]);
}

static struct qreg
tgsi_to_qir_seq(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        qir_SF(c, qir_FSUB(c, src[0 * 4 + i], src[1 * 4 + i]));
        return qir_SEL_X_0_ZS(c, qir_uniform_f(c, 1.0));
}

static struct qreg
tgsi_to_qir_sne(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        qir_SF(c, qir_FSUB(c, src[0 * 4 + i], src[1 * 4 + i]));
        return qir_SEL_X_0_ZC(c, qir_uniform_f(c, 1.0));
}

static struct qreg
tgsi_to_qir_slt(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        qir_SF(c, qir_FSUB(c, src[0 * 4 + i], src[1 * 4 + i]));
        return qir_SEL_X_0_NS(c, qir_uniform_f(c, 1.0));
}

static struct qreg
tgsi_to_qir_sge(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        qir_SF(c, qir_FSUB(c, src[0 * 4 + i], src[1 * 4 + i]));
        return qir_SEL_X_0_NC(c, qir_uniform_f(c, 1.0));
}

static struct qreg
tgsi_to_qir_fseq(struct vc4_compile *c,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg *src, int i)
{
        qir_SF(c, qir_FSUB(c, src[0 * 4 + i], src[1 * 4 + i]));
        return qir_SEL_X_0_ZS(c, qir_uniform_ui(c, ~0));
}

static struct qreg
tgsi_to_qir_fsne(struct vc4_compile *c,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg *src, int i)
{
        qir_SF(c, qir_FSUB(c, src[0 * 4 + i], src[1 * 4 + i]));
        return qir_SEL_X_0_ZC(c, qir_uniform_ui(c, ~0));
}

static struct qreg
tgsi_to_qir_fslt(struct vc4_compile *c,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg *src, int i)
{
        qir_SF(c, qir_FSUB(c, src[0 * 4 + i], src[1 * 4 + i]));
        return qir_SEL_X_0_NS(c, qir_uniform_ui(c, ~0));
}

static struct qreg
tgsi_to_qir_fsge(struct vc4_compile *c,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg *src, int i)
{
        qir_SF(c, qir_FSUB(c, src[0 * 4 + i], src[1 * 4 + i]));
        return qir_SEL_X_0_NC(c, qir_uniform_ui(c, ~0));
}

static struct qreg
tgsi_to_qir_useq(struct vc4_compile *c,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg *src, int i)
{
        qir_SF(c, qir_SUB(c, src[0 * 4 + i], src[1 * 4 + i]));
        return qir_SEL_X_0_ZS(c, qir_uniform_ui(c, ~0));
}

static struct qreg
tgsi_to_qir_usne(struct vc4_compile *c,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg *src, int i)
{
        qir_SF(c, qir_SUB(c, src[0 * 4 + i], src[1 * 4 + i]));
        return qir_SEL_X_0_ZC(c, qir_uniform_ui(c, ~0));
}

static struct qreg
tgsi_to_qir_islt(struct vc4_compile *c,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg *src, int i)
{
        qir_SF(c, qir_SUB(c, src[0 * 4 + i], src[1 * 4 + i]));
        return qir_SEL_X_0_NS(c, qir_uniform_ui(c, ~0));
}

static struct qreg
tgsi_to_qir_isge(struct vc4_compile *c,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg *src, int i)
{
        qir_SF(c, qir_SUB(c, src[0 * 4 + i], src[1 * 4 + i]));
        return qir_SEL_X_0_NC(c, qir_uniform_ui(c, ~0));
}

static struct qreg
tgsi_to_qir_cmp(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        qir_SF(c, src[0 * 4 + i]);
        return qir_SEL_X_Y_NS(c,
                              src[1 * 4 + i],
                              src[2 * 4 + i]);
}

static struct qreg
tgsi_to_qir_mad(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        return qir_FADD(c,
                        qir_FMUL(c,
                                 src[0 * 4 + i],
                                 src[1 * 4 + i]),
                        src[2 * 4 + i]);
}

static struct qreg
tgsi_to_qir_lrp(struct vc4_compile *c,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg *src, int i)
{
        struct qreg src0 = src[0 * 4 + i];
        struct qreg src1 = src[1 * 4 + i];
        struct qreg src2 = src[2 * 4 + i];

        /* LRP is:
         *    src0 * src1 + (1 - src0) * src2.
         * -> src0 * src1 + src2 - src0 * src2
         * -> src2 + src0 * (src1 - src2)
         */
        return qir_FADD(c, src2, qir_FMUL(c, src0, qir_FSUB(c, src1, src2)));

}

static void
tgsi_to_qir_tex(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src)
{
        assert(!tgsi_inst->Instruction.Saturate);

        struct qreg s = src[0 * 4 + 0];
        struct qreg t = src[0 * 4 + 1];
        struct qreg r = src[0 * 4 + 2];
        uint32_t unit = tgsi_inst->Src[1].Register.Index;
        bool is_txl = tgsi_inst->Instruction.Opcode == TGSI_OPCODE_TXL;

        struct qreg proj = c->undef;
        if (tgsi_inst->Instruction.Opcode == TGSI_OPCODE_TXP) {
                proj = qir_RCP(c, src[0 * 4 + 3]);
                s = qir_FMUL(c, s, proj);
                t = qir_FMUL(c, t, proj);
        }

        struct qreg texture_u[] = {
                add_uniform(c, QUNIFORM_TEXTURE_CONFIG_P0, unit),
                add_uniform(c, QUNIFORM_TEXTURE_CONFIG_P1, unit),
                add_uniform(c, QUNIFORM_CONSTANT, 0),
                add_uniform(c, QUNIFORM_CONSTANT, 0),
        };
        uint32_t next_texture_u = 0;

        /* There is no native support for GL texture rectangle coordinates, so
         * we have to rescale from ([0, width], [0, height]) to ([0, 1], [0,
         * 1]).
         */
        if (tgsi_inst->Texture.Texture == TGSI_TEXTURE_RECT ||
            tgsi_inst->Texture.Texture == TGSI_TEXTURE_SHADOWRECT) {
                s = qir_FMUL(c, s,
                             get_temp_for_uniform(c,
                                                  QUNIFORM_TEXRECT_SCALE_X,
                                                  unit));
                t = qir_FMUL(c, t,
                             get_temp_for_uniform(c,
                                                  QUNIFORM_TEXRECT_SCALE_Y,
                                                  unit));
        }

        if (tgsi_inst->Texture.Texture == TGSI_TEXTURE_CUBE ||
            tgsi_inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE ||
            is_txl) {
                texture_u[2] = add_uniform(c, QUNIFORM_TEXTURE_CONFIG_P2,
                                           unit | (is_txl << 16));
        }

        if (tgsi_inst->Texture.Texture == TGSI_TEXTURE_CUBE ||
                   tgsi_inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE) {
                struct qreg ma = qir_FMAXABS(c, qir_FMAXABS(c, s, t), r);
                struct qreg rcp_ma = qir_RCP(c, ma);
                s = qir_FMUL(c, s, rcp_ma);
                t = qir_FMUL(c, t, rcp_ma);
                r = qir_FMUL(c, r, rcp_ma);

                qir_TEX_R(c, r, texture_u[next_texture_u++]);
        } else if (c->key->tex[unit].wrap_s == PIPE_TEX_WRAP_CLAMP_TO_BORDER ||
                   c->key->tex[unit].wrap_s == PIPE_TEX_WRAP_CLAMP ||
                   c->key->tex[unit].wrap_t == PIPE_TEX_WRAP_CLAMP_TO_BORDER ||
                   c->key->tex[unit].wrap_t == PIPE_TEX_WRAP_CLAMP) {
                qir_TEX_R(c, get_temp_for_uniform(c, QUNIFORM_TEXTURE_BORDER_COLOR, unit),
                          texture_u[next_texture_u++]);
        }

        if (c->key->tex[unit].wrap_s == PIPE_TEX_WRAP_CLAMP) {
                s = qir_FMIN(c, qir_FMAX(c, s, qir_uniform_f(c, 0.0)),
                             qir_uniform_f(c, 1.0));
        }

        if (c->key->tex[unit].wrap_t == PIPE_TEX_WRAP_CLAMP) {
                t = qir_FMIN(c, qir_FMAX(c, t, qir_uniform_f(c, 0.0)),
                             qir_uniform_f(c, 1.0));
        }

        qir_TEX_T(c, t, texture_u[next_texture_u++]);

        if (tgsi_inst->Instruction.Opcode == TGSI_OPCODE_TXB ||
            tgsi_inst->Instruction.Opcode == TGSI_OPCODE_TXL)
                qir_TEX_B(c, src[0 * 4 + 3], texture_u[next_texture_u++]);

        qir_TEX_S(c, s, texture_u[next_texture_u++]);

        c->num_texture_samples++;
        struct qreg r4 = qir_TEX_RESULT(c);

        enum pipe_format format = c->key->tex[unit].format;

        struct qreg unpacked[4];
        if (util_format_is_depth_or_stencil(format)) {
                struct qreg depthf = qir_ITOF(c, qir_SHR(c, r4,
                                                         qir_uniform_ui(c, 8)));
                struct qreg normalized = qir_FMUL(c, depthf,
                                                  qir_uniform_f(c, 1.0f/0xffffff));

                struct qreg depth_output;

                struct qreg one = qir_uniform_f(c, 1.0f);
                if (c->key->tex[unit].compare_mode) {
                        struct qreg compare = src[0 * 4 + 2];

                        if (tgsi_inst->Instruction.Opcode == TGSI_OPCODE_TXP)
                                compare = qir_FMUL(c, compare, proj);

                        switch (c->key->tex[unit].compare_func) {
                        case PIPE_FUNC_NEVER:
                                depth_output = qir_uniform_f(c, 0.0f);
                                break;
                        case PIPE_FUNC_ALWAYS:
                                depth_output = one;
                                break;
                        case PIPE_FUNC_EQUAL:
                                qir_SF(c, qir_FSUB(c, compare, normalized));
                                depth_output = qir_SEL_X_0_ZS(c, one);
                                break;
                        case PIPE_FUNC_NOTEQUAL:
                                qir_SF(c, qir_FSUB(c, compare, normalized));
                                depth_output = qir_SEL_X_0_ZC(c, one);
                                break;
                        case PIPE_FUNC_GREATER:
                                qir_SF(c, qir_FSUB(c, compare, normalized));
                                depth_output = qir_SEL_X_0_NC(c, one);
                                break;
                        case PIPE_FUNC_GEQUAL:
                                qir_SF(c, qir_FSUB(c, normalized, compare));
                                depth_output = qir_SEL_X_0_NS(c, one);
                                break;
                        case PIPE_FUNC_LESS:
                                qir_SF(c, qir_FSUB(c, compare, normalized));
                                depth_output = qir_SEL_X_0_NS(c, one);
                                break;
                        case PIPE_FUNC_LEQUAL:
                                qir_SF(c, qir_FSUB(c, normalized, compare));
                                depth_output = qir_SEL_X_0_NC(c, one);
                                break;
                        }
                } else {
                        depth_output = normalized;
                }

                for (int i = 0; i < 4; i++)
                        unpacked[i] = depth_output;
        } else {
                for (int i = 0; i < 4; i++)
                        unpacked[i] = qir_R4_UNPACK(c, r4, i);
        }

        const uint8_t *format_swiz = vc4_get_format_swizzle(format);
        struct qreg texture_output[4];
        for (int i = 0; i < 4; i++) {
                texture_output[i] = get_swizzled_channel(c, unpacked,
                                                         format_swiz[i]);
        }

        if (util_format_is_srgb(format)) {
                for (int i = 0; i < 3; i++)
                        texture_output[i] = qir_srgb_decode(c,
                                                            texture_output[i]);
        }

        for (int i = 0; i < 4; i++) {
                if (!(tgsi_inst->Dst[0].Register.WriteMask & (1 << i)))
                        continue;

                update_dst(c, tgsi_inst, i,
                           get_swizzled_channel(c, texture_output,
                                                c->key->tex[unit].swizzle[i]));
        }
}

static struct qreg
tgsi_to_qir_trunc(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        return qir_ITOF(c, qir_FTOI(c, src[0 * 4 + i]));
}

/**
 * Computes x - floor(x), which is tricky because our FTOI truncates (rounds
 * to zero).
 */
static struct qreg
tgsi_to_qir_frc(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        struct qreg trunc = qir_ITOF(c, qir_FTOI(c, src[0 * 4 + i]));
        struct qreg diff = qir_FSUB(c, src[0 * 4 + i], trunc);
        qir_SF(c, diff);
        return qir_SEL_X_Y_NS(c,
                              qir_FADD(c, diff, qir_uniform_f(c, 1.0)),
                              diff);
}

/**
 * Computes floor(x), which is tricky because our FTOI truncates (rounds to
 * zero).
 */
static struct qreg
tgsi_to_qir_flr(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        struct qreg trunc = qir_ITOF(c, qir_FTOI(c, src[0 * 4 + i]));

        /* This will be < 0 if we truncated and the truncation was of a value
         * that was < 0 in the first place.
         */
        qir_SF(c, qir_FSUB(c, src[0 * 4 + i], trunc));

        return qir_SEL_X_Y_NS(c,
                              qir_FSUB(c, trunc, qir_uniform_f(c, 1.0)),
                              trunc);
}

/**
 * Computes ceil(x), which is tricky because our FTOI truncates (rounds to
 * zero).
 */
static struct qreg
tgsi_to_qir_ceil(struct vc4_compile *c,
                 struct tgsi_full_instruction *tgsi_inst,
                 enum qop op, struct qreg *src, int i)
{
        struct qreg trunc = qir_ITOF(c, qir_FTOI(c, src[0 * 4 + i]));

        /* This will be < 0 if we truncated and the truncation was of a value
         * that was > 0 in the first place.
         */
        qir_SF(c, qir_FSUB(c, trunc, src[0 * 4 + i]));

        return qir_SEL_X_Y_NS(c,
                              qir_FADD(c, trunc, qir_uniform_f(c, 1.0)),
                              trunc);
}

static struct qreg
tgsi_to_qir_abs(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        struct qreg arg = src[0 * 4 + i];
        return qir_FMAXABS(c, arg, arg);
}

/* Note that this instruction replicates its result from the x channel */
static struct qreg
tgsi_to_qir_sin(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        float coeff[] = {
                -2.0 * M_PI,
                pow(2.0 * M_PI, 3) / (3 * 2 * 1),
                -pow(2.0 * M_PI, 5) / (5 * 4 * 3 * 2 * 1),
                pow(2.0 * M_PI, 7) / (7 * 6 * 5 * 4 * 3 * 2 * 1),
                -pow(2.0 * M_PI, 9) / (9 * 8 * 7 * 6 * 5 * 4 * 3 * 2 * 1),
        };

        struct qreg scaled_x =
                qir_FMUL(c,
                         src[0 * 4 + 0],
                         qir_uniform_f(c, 1.0f / (M_PI * 2.0f)));

        struct qreg x = qir_FADD(c,
                                 tgsi_to_qir_frc(c, NULL, 0, &scaled_x, 0),
                                 qir_uniform_f(c, -0.5));
        struct qreg x2 = qir_FMUL(c, x, x);
        struct qreg sum = qir_FMUL(c, x, qir_uniform_f(c, coeff[0]));
        for (int i = 1; i < ARRAY_SIZE(coeff); i++) {
                x = qir_FMUL(c, x, x2);
                sum = qir_FADD(c,
                               sum,
                               qir_FMUL(c,
                                        x,
                                        qir_uniform_f(c, coeff[i])));
        }
        return sum;
}

/* Note that this instruction replicates its result from the x channel */
static struct qreg
tgsi_to_qir_cos(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        float coeff[] = {
                -1.0f,
                pow(2.0 * M_PI, 2) / (2 * 1),
                -pow(2.0 * M_PI, 4) / (4 * 3 * 2 * 1),
                pow(2.0 * M_PI, 6) / (6 * 5 * 4 * 3 * 2 * 1),
                -pow(2.0 * M_PI, 8) / (8 * 7 * 6 * 5 * 4 * 3 * 2 * 1),
                pow(2.0 * M_PI, 10) / (10 * 9 * 8 * 7 * 6 * 5 * 4 * 3 * 2 * 1),
        };

        struct qreg scaled_x =
                qir_FMUL(c, src[0 * 4 + 0],
                         qir_uniform_f(c, 1.0f / (M_PI * 2.0f)));
        struct qreg x_frac = qir_FADD(c,
                                      tgsi_to_qir_frc(c, NULL, 0, &scaled_x, 0),
                                      qir_uniform_f(c, -0.5));

        struct qreg sum = qir_uniform_f(c, coeff[0]);
        struct qreg x2 = qir_FMUL(c, x_frac, x_frac);
        struct qreg x = x2; /* Current x^2, x^4, or x^6 */
        for (int i = 1; i < ARRAY_SIZE(coeff); i++) {
                if (i != 1)
                        x = qir_FMUL(c, x, x2);

                struct qreg mul = qir_FMUL(c,
                                           x,
                                           qir_uniform_f(c, coeff[i]));
                if (i == 0)
                        sum = mul;
                else
                        sum = qir_FADD(c, sum, mul);
        }
        return sum;
}

static struct qreg
tgsi_to_qir_clamp(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        return qir_FMAX(c, qir_FMIN(c,
                                    src[0 * 4 + i],
                                    src[2 * 4 + i]),
                        src[1 * 4 + i]);
}

static struct qreg
tgsi_to_qir_ssg(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        qir_SF(c, src[0 * 4 + i]);
        return qir_SEL_X_Y_NC(c,
                              qir_SEL_X_0_ZC(c, qir_uniform_f(c, 1.0)),
                              qir_uniform_f(c, -1.0));
}

/* Compare to tgsi_to_qir_flr() for the floor logic. */
static struct qreg
tgsi_to_qir_arl(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        struct qreg trunc = qir_FTOI(c, src[0 * 4 + i]);
        struct qreg scaled = qir_SHL(c, trunc, qir_uniform_ui(c, 4));

        qir_SF(c, qir_FSUB(c, src[0 * 4 + i], qir_ITOF(c, trunc)));

        return qir_SEL_X_Y_NS(c, qir_SUB(c, scaled, qir_uniform_ui(c, 4)),
                              scaled);
}

static struct qreg
tgsi_to_qir_uarl(struct vc4_compile *c,
                struct tgsi_full_instruction *tgsi_inst,
                enum qop op, struct qreg *src, int i)
{
        return qir_SHL(c, src[0 * 4 + i], qir_uniform_ui(c, 4));
}

static void
emit_vertex_input(struct vc4_compile *c, int attr)
{
        enum pipe_format format = c->vs_key->attr_formats[attr];
        struct qreg vpm_reads[4];

        /* Right now, we're setting the VPM offsets to be 16 bytes wide every
         * time, so we always read 4 32-bit VPM entries.
         */
        for (int i = 0; i < 4; i++) {
                vpm_reads[i] = qir_get_temp(c);
                qir_emit(c, qir_inst(QOP_VPM_READ,
                                     vpm_reads[i],
                                     c->undef,
                                     c->undef));
                c->num_inputs++;
        }

        bool format_warned = false;
        const struct util_format_description *desc =
                util_format_description(format);

        for (int i = 0; i < 4; i++) {
                uint8_t swiz = desc->swizzle[i];
                struct qreg result;

                if (swiz > UTIL_FORMAT_SWIZZLE_W)
                        result = get_swizzled_channel(c, vpm_reads, swiz);
                else if (desc->channel[swiz].size == 32 &&
                         desc->channel[swiz].type == UTIL_FORMAT_TYPE_FLOAT) {
                        result = get_swizzled_channel(c, vpm_reads, swiz);
                } else if (desc->channel[swiz].size == 8 &&
                           (desc->channel[swiz].type == UTIL_FORMAT_TYPE_UNSIGNED ||
                            desc->channel[swiz].type == UTIL_FORMAT_TYPE_SIGNED) &&
                           desc->channel[swiz].normalized) {
                        struct qreg vpm = vpm_reads[0];
                        if (desc->channel[swiz].type == UTIL_FORMAT_TYPE_SIGNED)
                                vpm = qir_XOR(c, vpm, qir_uniform_ui(c, 0x80808080));
                        result = qir_UNPACK_8(c, vpm, swiz);
                } else {
                        if (!format_warned) {
                                fprintf(stderr,
                                        "vtx element %d unsupported type: %s\n",
                                        attr, util_format_name(format));
                                format_warned = true;
                        }
                        result = qir_uniform_f(c, 0.0);
                }

                if (desc->channel[swiz].normalized &&
                    desc->channel[swiz].type == UTIL_FORMAT_TYPE_SIGNED) {
                        result = qir_FSUB(c,
                                          qir_FMUL(c,
                                                   result,
                                                   qir_uniform_f(c, 2.0)),
                                          qir_uniform_f(c, 1.0));
                }

                c->inputs[attr * 4 + i] = result;
        }
}

static void
tgsi_to_qir_kill_if(struct vc4_compile *c, struct qreg *src, int i)
{
        if (c->discard.file == QFILE_NULL)
                c->discard = qir_uniform_f(c, 0.0);
        qir_SF(c, src[0 * 4 + i]);
        c->discard = qir_SEL_X_Y_NS(c, qir_uniform_f(c, 1.0),
                                    c->discard);
}

static void
emit_fragcoord_input(struct vc4_compile *c, int attr)
{
        c->inputs[attr * 4 + 0] = qir_FRAG_X(c);
        c->inputs[attr * 4 + 1] = qir_FRAG_Y(c);
        c->inputs[attr * 4 + 2] =
                qir_FMUL(c,
                         qir_ITOF(c, qir_FRAG_Z(c)),
                         qir_uniform_f(c, 1.0 / 0xffffff));
        c->inputs[attr * 4 + 3] = qir_RCP(c, qir_FRAG_W(c));
}

static void
emit_point_coord_input(struct vc4_compile *c, int attr)
{
        if (c->point_x.file == QFILE_NULL) {
                c->point_x = qir_uniform_f(c, 0.0);
                c->point_y = qir_uniform_f(c, 0.0);
        }

        c->inputs[attr * 4 + 0] = c->point_x;
        if (c->fs_key->point_coord_upper_left) {
                c->inputs[attr * 4 + 1] = qir_FSUB(c,
                                                   qir_uniform_f(c, 1.0),
                                                   c->point_y);
        } else {
                c->inputs[attr * 4 + 1] = c->point_y;
        }
        c->inputs[attr * 4 + 2] = qir_uniform_f(c, 0.0);
        c->inputs[attr * 4 + 3] = qir_uniform_f(c, 1.0);
}

static struct qreg
emit_fragment_varying(struct vc4_compile *c, uint8_t semantic,
                      uint8_t index, uint8_t swizzle)
{
        uint32_t i = c->num_input_semantics++;
        struct qreg vary = {
                QFILE_VARY,
                i
        };

        if (c->num_input_semantics >= c->input_semantics_array_size) {
                c->input_semantics_array_size =
                        MAX2(4, c->input_semantics_array_size * 2);

                c->input_semantics = reralloc(c, c->input_semantics,
                                              struct vc4_varying_semantic,
                                              c->input_semantics_array_size);
        }

        c->input_semantics[i].semantic = semantic;
        c->input_semantics[i].index = index;
        c->input_semantics[i].swizzle = swizzle;

        return qir_VARY_ADD_C(c, qir_FMUL(c, vary, qir_FRAG_W(c)));
}

static void
emit_fragment_input(struct vc4_compile *c, int attr,
                    struct tgsi_full_declaration *decl)
{
        for (int i = 0; i < 4; i++) {
                c->inputs[attr * 4 + i] =
                        emit_fragment_varying(c,
                                              decl->Semantic.Name,
                                              decl->Semantic.Index,
                                              i);
                c->num_inputs++;
        }
}

static void
emit_face_input(struct vc4_compile *c, int attr)
{
        c->inputs[attr * 4 + 0] = qir_FSUB(c,
                                           qir_uniform_f(c, 1.0),
                                           qir_FMUL(c,
                                                    qir_ITOF(c, qir_FRAG_REV_FLAG(c)),
                                                    qir_uniform_f(c, 2.0)));
        c->inputs[attr * 4 + 1] = qir_uniform_f(c, 0.0);
        c->inputs[attr * 4 + 2] = qir_uniform_f(c, 0.0);
        c->inputs[attr * 4 + 3] = qir_uniform_f(c, 1.0);
}

static void
add_output(struct vc4_compile *c,
           uint32_t decl_offset,
           uint8_t semantic_name,
           uint8_t semantic_index,
           uint8_t semantic_swizzle)
{
        uint32_t old_array_size = c->outputs_array_size;
        resize_qreg_array(c, &c->outputs, &c->outputs_array_size,
                          decl_offset + 1);

        if (old_array_size != c->outputs_array_size) {
                c->output_semantics = reralloc(c,
                                               c->output_semantics,
                                               struct vc4_varying_semantic,
                                               c->outputs_array_size);
        }

        c->output_semantics[decl_offset].semantic = semantic_name;
        c->output_semantics[decl_offset].index = semantic_index;
        c->output_semantics[decl_offset].swizzle = semantic_swizzle;
}

static void
add_array_info(struct vc4_compile *c, uint32_t array_id,
               uint32_t start, uint32_t size)
{
        if (array_id >= c->ubo_ranges_array_size) {
                c->ubo_ranges_array_size = MAX2(c->ubo_ranges_array_size * 2,
                                                array_id + 1);
                c->ubo_ranges = reralloc(c, c->ubo_ranges,
                                         struct vc4_compiler_ubo_range,
                                         c->ubo_ranges_array_size);
        }

        c->ubo_ranges[array_id].dst_offset = 0;
        c->ubo_ranges[array_id].src_offset = start;
        c->ubo_ranges[array_id].size = size;
        c->ubo_ranges[array_id].used = false;
}

static void
emit_tgsi_declaration(struct vc4_compile *c,
                      struct tgsi_full_declaration *decl)
{
        switch (decl->Declaration.File) {
        case TGSI_FILE_TEMPORARY: {
                uint32_t old_size = c->temps_array_size;
                resize_qreg_array(c, &c->temps, &c->temps_array_size,
                                  (decl->Range.Last + 1) * 4);

                for (int i = old_size; i < c->temps_array_size; i++)
                        c->temps[i] = qir_uniform_ui(c, 0);
                break;
        }

        case TGSI_FILE_INPUT:
                resize_qreg_array(c, &c->inputs, &c->inputs_array_size,
                                  (decl->Range.Last + 1) * 4);

                for (int i = decl->Range.First;
                     i <= decl->Range.Last;
                     i++) {
                        if (c->stage == QSTAGE_FRAG) {
                                if (decl->Semantic.Name ==
                                    TGSI_SEMANTIC_POSITION) {
                                        emit_fragcoord_input(c, i);
                                } else if (decl->Semantic.Name == TGSI_SEMANTIC_FACE) {
                                        emit_face_input(c, i);
                                } else if (decl->Semantic.Name == TGSI_SEMANTIC_GENERIC &&
                                           (c->fs_key->point_sprite_mask &
                                            (1 << decl->Semantic.Index))) {
                                        emit_point_coord_input(c, i);
                                } else {
                                        emit_fragment_input(c, i, decl);
                                }
                        } else {
                                emit_vertex_input(c, i);
                        }
                }
                break;

        case TGSI_FILE_OUTPUT: {
                for (int i = 0; i < 4; i++) {
                        add_output(c,
                                   decl->Range.First * 4 + i,
                                   decl->Semantic.Name,
                                   decl->Semantic.Index,
                                   i);
                }

                switch (decl->Semantic.Name) {
                case TGSI_SEMANTIC_POSITION:
                        c->output_position_index = decl->Range.First * 4;
                        break;
                case TGSI_SEMANTIC_CLIPVERTEX:
                        c->output_clipvertex_index = decl->Range.First * 4;
                        break;
                case TGSI_SEMANTIC_COLOR:
                        c->output_color_index = decl->Range.First * 4;
                        break;
                case TGSI_SEMANTIC_PSIZE:
                        c->output_point_size_index = decl->Range.First * 4;
                        break;
                }

                break;

        case TGSI_FILE_CONSTANT:
                add_array_info(c,
                               decl->Array.ArrayID,
                               decl->Range.First * 16,
                               (decl->Range.Last -
                                decl->Range.First + 1) * 16);
                break;
        }
        }
}

static void
emit_tgsi_instruction(struct vc4_compile *c,
                      struct tgsi_full_instruction *tgsi_inst)
{
        struct {
                enum qop op;
                struct qreg (*func)(struct vc4_compile *c,
                                    struct tgsi_full_instruction *tgsi_inst,
                                    enum qop op,
                                    struct qreg *src, int i);
        } op_trans[] = {
                [TGSI_OPCODE_MOV] = { QOP_MOV, tgsi_to_qir_alu },
                [TGSI_OPCODE_ABS] = { 0, tgsi_to_qir_abs },
                [TGSI_OPCODE_MUL] = { QOP_FMUL, tgsi_to_qir_alu },
                [TGSI_OPCODE_ADD] = { QOP_FADD, tgsi_to_qir_alu },
                [TGSI_OPCODE_SUB] = { QOP_FSUB, tgsi_to_qir_alu },
                [TGSI_OPCODE_MIN] = { QOP_FMIN, tgsi_to_qir_alu },
                [TGSI_OPCODE_MAX] = { QOP_FMAX, tgsi_to_qir_alu },
                [TGSI_OPCODE_F2I] = { QOP_FTOI, tgsi_to_qir_alu },
                [TGSI_OPCODE_I2F] = { QOP_ITOF, tgsi_to_qir_alu },
                [TGSI_OPCODE_UADD] = { QOP_ADD, tgsi_to_qir_alu },
                [TGSI_OPCODE_USHR] = { QOP_SHR, tgsi_to_qir_alu },
                [TGSI_OPCODE_ISHR] = { QOP_ASR, tgsi_to_qir_alu },
                [TGSI_OPCODE_SHL] = { QOP_SHL, tgsi_to_qir_alu },
                [TGSI_OPCODE_IMIN] = { QOP_MIN, tgsi_to_qir_alu },
                [TGSI_OPCODE_IMAX] = { QOP_MAX, tgsi_to_qir_alu },
                [TGSI_OPCODE_AND] = { QOP_AND, tgsi_to_qir_alu },
                [TGSI_OPCODE_OR] = { QOP_OR, tgsi_to_qir_alu },
                [TGSI_OPCODE_XOR] = { QOP_XOR, tgsi_to_qir_alu },
                [TGSI_OPCODE_NOT] = { QOP_NOT, tgsi_to_qir_alu },

                [TGSI_OPCODE_UMUL] = { 0, tgsi_to_qir_umul },
                [TGSI_OPCODE_IDIV] = { 0, tgsi_to_qir_idiv },
                [TGSI_OPCODE_INEG] = { 0, tgsi_to_qir_ineg },

                [TGSI_OPCODE_SEQ] = { 0, tgsi_to_qir_seq },
                [TGSI_OPCODE_SNE] = { 0, tgsi_to_qir_sne },
                [TGSI_OPCODE_SGE] = { 0, tgsi_to_qir_sge },
                [TGSI_OPCODE_SLT] = { 0, tgsi_to_qir_slt },
                [TGSI_OPCODE_FSEQ] = { 0, tgsi_to_qir_fseq },
                [TGSI_OPCODE_FSNE] = { 0, tgsi_to_qir_fsne },
                [TGSI_OPCODE_FSGE] = { 0, tgsi_to_qir_fsge },
                [TGSI_OPCODE_FSLT] = { 0, tgsi_to_qir_fslt },
                [TGSI_OPCODE_USEQ] = { 0, tgsi_to_qir_useq },
                [TGSI_OPCODE_USNE] = { 0, tgsi_to_qir_usne },
                [TGSI_OPCODE_ISGE] = { 0, tgsi_to_qir_isge },
                [TGSI_OPCODE_ISLT] = { 0, tgsi_to_qir_islt },

                [TGSI_OPCODE_CMP] = { 0, tgsi_to_qir_cmp },
                [TGSI_OPCODE_MAD] = { 0, tgsi_to_qir_mad },
                [TGSI_OPCODE_RCP] = { QOP_RCP, tgsi_to_qir_rcp },
                [TGSI_OPCODE_RSQ] = { QOP_RSQ, tgsi_to_qir_rsq },
                [TGSI_OPCODE_EX2] = { QOP_EXP2, tgsi_to_qir_scalar },
                [TGSI_OPCODE_LG2] = { QOP_LOG2, tgsi_to_qir_scalar },
                [TGSI_OPCODE_LRP] = { 0, tgsi_to_qir_lrp },
                [TGSI_OPCODE_TRUNC] = { 0, tgsi_to_qir_trunc },
                [TGSI_OPCODE_CEIL] = { 0, tgsi_to_qir_ceil },
                [TGSI_OPCODE_FRC] = { 0, tgsi_to_qir_frc },
                [TGSI_OPCODE_FLR] = { 0, tgsi_to_qir_flr },
                [TGSI_OPCODE_SIN] = { 0, tgsi_to_qir_sin },
                [TGSI_OPCODE_COS] = { 0, tgsi_to_qir_cos },
                [TGSI_OPCODE_CLAMP] = { 0, tgsi_to_qir_clamp },
                [TGSI_OPCODE_SSG] = { 0, tgsi_to_qir_ssg },
                [TGSI_OPCODE_ARL] = { 0, tgsi_to_qir_arl },
                [TGSI_OPCODE_UARL] = { 0, tgsi_to_qir_uarl },
        };
        static int asdf = 0;
        uint32_t tgsi_op = tgsi_inst->Instruction.Opcode;

        if (tgsi_op == TGSI_OPCODE_END)
                return;

        struct qreg src_regs[12];
        for (int s = 0; s < 3; s++) {
                for (int i = 0; i < 4; i++) {
                        src_regs[4 * s + i] =
                                get_src(c, tgsi_inst->Instruction.Opcode,
                                        &tgsi_inst->Src[s], i);
                }
        }

        switch (tgsi_op) {
        case TGSI_OPCODE_TEX:
        case TGSI_OPCODE_TXP:
        case TGSI_OPCODE_TXB:
        case TGSI_OPCODE_TXL:
                tgsi_to_qir_tex(c, tgsi_inst,
                                op_trans[tgsi_op].op, src_regs);
                return;
        case TGSI_OPCODE_KILL:
                c->discard = qir_uniform_f(c, 1.0);
                return;
        case TGSI_OPCODE_KILL_IF:
                for (int i = 0; i < 4; i++)
                        tgsi_to_qir_kill_if(c, src_regs, i);
                return;
        default:
                break;
        }

        if (tgsi_op > ARRAY_SIZE(op_trans) || !(op_trans[tgsi_op].func)) {
                fprintf(stderr, "unknown tgsi inst: ");
                tgsi_dump_instruction(tgsi_inst, asdf++);
                fprintf(stderr, "\n");
                abort();
        }

        for (int i = 0; i < 4; i++) {
                if (!(tgsi_inst->Dst[0].Register.WriteMask & (1 << i)))
                        continue;

                struct qreg result;

                result = op_trans[tgsi_op].func(c, tgsi_inst,
                                                op_trans[tgsi_op].op,
                                                src_regs, i);

                if (tgsi_inst->Instruction.Saturate) {
                        float low = (tgsi_inst->Instruction.Saturate ==
                                     TGSI_SAT_MINUS_PLUS_ONE ? -1.0 : 0.0);
                        result = qir_FMAX(c,
                                          qir_FMIN(c,
                                                   result,
                                                   qir_uniform_f(c, 1.0)),
                                          qir_uniform_f(c, low));
                }

                update_dst(c, tgsi_inst, i, result);
        }
}

static void
parse_tgsi_immediate(struct vc4_compile *c, struct tgsi_full_immediate *imm)
{
        for (int i = 0; i < 4; i++) {
                unsigned n = c->num_consts++;
                resize_qreg_array(c, &c->consts, &c->consts_array_size, n + 1);
                c->consts[n] = qir_uniform_ui(c, imm->u[i].Uint);
        }
}

static struct qreg
vc4_blend_channel(struct vc4_compile *c,
                  struct qreg *dst,
                  struct qreg *src,
                  struct qreg val,
                  unsigned factor,
                  int channel)
{
        switch(factor) {
        case PIPE_BLENDFACTOR_ONE:
                return val;
        case PIPE_BLENDFACTOR_SRC_COLOR:
                return qir_FMUL(c, val, src[channel]);
        case PIPE_BLENDFACTOR_SRC_ALPHA:
                return qir_FMUL(c, val, src[3]);
        case PIPE_BLENDFACTOR_DST_ALPHA:
                return qir_FMUL(c, val, dst[3]);
        case PIPE_BLENDFACTOR_DST_COLOR:
                return qir_FMUL(c, val, dst[channel]);
        case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
                if (channel != 3) {
                        return qir_FMUL(c,
                                        val,
                                        qir_FMIN(c,
                                                 src[3],
                                                 qir_FSUB(c,
                                                          qir_uniform_f(c, 1.0),
                                                          dst[3])));
                } else {
                        return val;
                }
        case PIPE_BLENDFACTOR_CONST_COLOR:
                return qir_FMUL(c, val,
                                get_temp_for_uniform(c,
                                                     QUNIFORM_BLEND_CONST_COLOR,
                                                     channel));
        case PIPE_BLENDFACTOR_CONST_ALPHA:
                return qir_FMUL(c, val,
                                get_temp_for_uniform(c,
                                                     QUNIFORM_BLEND_CONST_COLOR,
                                                     3));
        case PIPE_BLENDFACTOR_ZERO:
                return qir_uniform_f(c, 0.0);
        case PIPE_BLENDFACTOR_INV_SRC_COLOR:
                return qir_FMUL(c, val, qir_FSUB(c, qir_uniform_f(c, 1.0),
                                                 src[channel]));
        case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
                return qir_FMUL(c, val, qir_FSUB(c, qir_uniform_f(c, 1.0),
                                                 src[3]));
        case PIPE_BLENDFACTOR_INV_DST_ALPHA:
                return qir_FMUL(c, val, qir_FSUB(c, qir_uniform_f(c, 1.0),
                                                 dst[3]));
        case PIPE_BLENDFACTOR_INV_DST_COLOR:
                return qir_FMUL(c, val, qir_FSUB(c, qir_uniform_f(c, 1.0),
                                                 dst[channel]));
        case PIPE_BLENDFACTOR_INV_CONST_COLOR:
                return qir_FMUL(c, val,
                                qir_FSUB(c, qir_uniform_f(c, 1.0),
                                         get_temp_for_uniform(c,
                                                              QUNIFORM_BLEND_CONST_COLOR,
                                                              channel)));
        case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
                return qir_FMUL(c, val,
                                qir_FSUB(c, qir_uniform_f(c, 1.0),
                                         get_temp_for_uniform(c,
                                                              QUNIFORM_BLEND_CONST_COLOR,
                                                              3)));

        default:
        case PIPE_BLENDFACTOR_SRC1_COLOR:
        case PIPE_BLENDFACTOR_SRC1_ALPHA:
        case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
        case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:
                /* Unsupported. */
                fprintf(stderr, "Unknown blend factor %d\n", factor);
                return val;
        }
}

static struct qreg
vc4_blend_func(struct vc4_compile *c,
               struct qreg src, struct qreg dst,
               unsigned func)
{
        switch (func) {
        case PIPE_BLEND_ADD:
                return qir_FADD(c, src, dst);
        case PIPE_BLEND_SUBTRACT:
                return qir_FSUB(c, src, dst);
        case PIPE_BLEND_REVERSE_SUBTRACT:
                return qir_FSUB(c, dst, src);
        case PIPE_BLEND_MIN:
                return qir_FMIN(c, src, dst);
        case PIPE_BLEND_MAX:
                return qir_FMAX(c, src, dst);

        default:
                /* Unsupported. */
                fprintf(stderr, "Unknown blend func %d\n", func);
                return src;

        }
}

/**
 * Implements fixed function blending in shader code.
 *
 * VC4 doesn't have any hardware support for blending.  Instead, you read the
 * current contents of the destination from the tile buffer after having
 * waited for the scoreboard (which is handled by vc4_qpu_emit.c), then do
 * math using your output color and that destination value, and update the
 * output color appropriately.
 */
static void
vc4_blend(struct vc4_compile *c, struct qreg *result,
          struct qreg *dst_color, struct qreg *src_color)
{
        struct pipe_rt_blend_state *blend = &c->fs_key->blend;

        if (!blend->blend_enable) {
                for (int i = 0; i < 4; i++)
                        result[i] = src_color[i];
                return;
        }

        struct qreg src_blend[4], dst_blend[4];
        for (int i = 0; i < 3; i++) {
                src_blend[i] = vc4_blend_channel(c,
                                                 dst_color, src_color,
                                                 src_color[i],
                                                 blend->rgb_src_factor, i);
                dst_blend[i] = vc4_blend_channel(c,
                                                 dst_color, src_color,
                                                 dst_color[i],
                                                 blend->rgb_dst_factor, i);
        }
        src_blend[3] = vc4_blend_channel(c,
                                         dst_color, src_color,
                                         src_color[3],
                                         blend->alpha_src_factor, 3);
        dst_blend[3] = vc4_blend_channel(c,
                                         dst_color, src_color,
                                         dst_color[3],
                                         blend->alpha_dst_factor, 3);

        for (int i = 0; i < 3; i++) {
                result[i] = vc4_blend_func(c,
                                           src_blend[i], dst_blend[i],
                                           blend->rgb_func);
        }
        result[3] = vc4_blend_func(c,
                                   src_blend[3], dst_blend[3],
                                   blend->alpha_func);
}

static void
clip_distance_discard(struct vc4_compile *c)
{
        for (int i = 0; i < PIPE_MAX_CLIP_PLANES; i++) {
                if (!(c->key->ucp_enables & (1 << i)))
                        continue;

                struct qreg dist = emit_fragment_varying(c,
                                                         TGSI_SEMANTIC_CLIPDIST,
                                                         i,
                                                         TGSI_SWIZZLE_X);

                qir_SF(c, dist);

                if (c->discard.file == QFILE_NULL)
                        c->discard = qir_uniform_f(c, 0.0);

                c->discard = qir_SEL_X_Y_NS(c, qir_uniform_f(c, 1.0),
                                            c->discard);
        }
}

static void
alpha_test_discard(struct vc4_compile *c)
{
        struct qreg src_alpha;
        struct qreg alpha_ref = get_temp_for_uniform(c, QUNIFORM_ALPHA_REF, 0);

        if (!c->fs_key->alpha_test)
                return;

        if (c->output_color_index != -1)
                src_alpha = c->outputs[c->output_color_index + 3];
        else
                src_alpha = qir_uniform_f(c, 1.0);

        if (c->discard.file == QFILE_NULL)
                c->discard = qir_uniform_f(c, 0.0);

        switch (c->fs_key->alpha_test_func) {
        case PIPE_FUNC_NEVER:
                c->discard = qir_uniform_f(c, 1.0);
                break;
        case PIPE_FUNC_ALWAYS:
                break;
        case PIPE_FUNC_EQUAL:
                qir_SF(c, qir_FSUB(c, src_alpha, alpha_ref));
                c->discard = qir_SEL_X_Y_ZS(c, c->discard,
                                            qir_uniform_f(c, 1.0));
                break;
        case PIPE_FUNC_NOTEQUAL:
                qir_SF(c, qir_FSUB(c, src_alpha, alpha_ref));
                c->discard = qir_SEL_X_Y_ZC(c, c->discard,
                                            qir_uniform_f(c, 1.0));
                break;
        case PIPE_FUNC_GREATER:
                qir_SF(c, qir_FSUB(c, src_alpha, alpha_ref));
                c->discard = qir_SEL_X_Y_NC(c, c->discard,
                                            qir_uniform_f(c, 1.0));
                break;
        case PIPE_FUNC_GEQUAL:
                qir_SF(c, qir_FSUB(c, alpha_ref, src_alpha));
                c->discard = qir_SEL_X_Y_NS(c, c->discard,
                                            qir_uniform_f(c, 1.0));
                break;
        case PIPE_FUNC_LESS:
                qir_SF(c, qir_FSUB(c, src_alpha, alpha_ref));
                c->discard = qir_SEL_X_Y_NS(c, c->discard,
                                            qir_uniform_f(c, 1.0));
                break;
        case PIPE_FUNC_LEQUAL:
                qir_SF(c, qir_FSUB(c, alpha_ref, src_alpha));
                c->discard = qir_SEL_X_Y_NC(c, c->discard,
                                            qir_uniform_f(c, 1.0));
                break;
        }
}

static void
emit_frag_end(struct vc4_compile *c)
{
        clip_distance_discard(c);
        alpha_test_discard(c);

        enum pipe_format color_format = c->fs_key->color_format;
        const uint8_t *format_swiz = vc4_get_format_swizzle(color_format);
        struct qreg tlb_read_color[4] = { c->undef, c->undef, c->undef, c->undef };
        struct qreg dst_color[4] = { c->undef, c->undef, c->undef, c->undef };
        struct qreg linear_dst_color[4] = { c->undef, c->undef, c->undef, c->undef };
        if (c->fs_key->blend.blend_enable ||
            c->fs_key->blend.colormask != 0xf) {
                struct qreg r4 = qir_TLB_COLOR_READ(c);
                for (int i = 0; i < 4; i++)
                        tlb_read_color[i] = qir_R4_UNPACK(c, r4, i);
                for (int i = 0; i < 4; i++) {
                        dst_color[i] = get_swizzled_channel(c,
                                                            tlb_read_color,
                                                            format_swiz[i]);
                        if (util_format_is_srgb(color_format) && i != 3) {
                                linear_dst_color[i] =
                                        qir_srgb_decode(c, dst_color[i]);
                        } else {
                                linear_dst_color[i] = dst_color[i];
                        }
                }
        }

        struct qreg blend_color[4];
        struct qreg undef_array[4] = {
                c->undef, c->undef, c->undef, c->undef
        };
        vc4_blend(c, blend_color, linear_dst_color,
                  (c->output_color_index != -1 ?
                   c->outputs + c->output_color_index :
                   undef_array));

        if (util_format_is_srgb(color_format)) {
                for (int i = 0; i < 3; i++)
                        blend_color[i] = qir_srgb_encode(c, blend_color[i]);
        }

        /* If the bit isn't set in the color mask, then just return the
         * original dst color, instead.
         */
        for (int i = 0; i < 4; i++) {
                if (!(c->fs_key->blend.colormask & (1 << i))) {
                        blend_color[i] = dst_color[i];
                }
        }

        /* Debug: Sometimes you're getting a black output and just want to see
         * if the FS is getting executed at all.  Spam magenta into the color
         * output.
         */
        if (0) {
                blend_color[0] = qir_uniform_f(c, 1.0);
                blend_color[1] = qir_uniform_f(c, 0.0);
                blend_color[2] = qir_uniform_f(c, 1.0);
                blend_color[3] = qir_uniform_f(c, 0.5);
        }

        struct qreg swizzled_outputs[4];
        for (int i = 0; i < 4; i++) {
                swizzled_outputs[i] = get_swizzled_channel(c, blend_color,
                                                           format_swiz[i]);
        }

        if (c->discard.file != QFILE_NULL)
                qir_TLB_DISCARD_SETUP(c, c->discard);

        if (c->fs_key->stencil_enabled) {
                qir_TLB_STENCIL_SETUP(c, add_uniform(c, QUNIFORM_STENCIL, 0));
                if (c->fs_key->stencil_twoside) {
                        qir_TLB_STENCIL_SETUP(c, add_uniform(c, QUNIFORM_STENCIL, 1));
                }
                if (c->fs_key->stencil_full_writemasks) {
                        qir_TLB_STENCIL_SETUP(c, add_uniform(c, QUNIFORM_STENCIL, 2));
                }
        }

        if (c->fs_key->depth_enabled) {
                struct qreg z;
                if (c->output_position_index != -1) {
                        z = qir_FTOI(c, qir_FMUL(c, c->outputs[c->output_position_index + 2],
                                                 qir_uniform_f(c, 0xffffff)));
                } else {
                        z = qir_FRAG_Z(c);
                }
                qir_TLB_Z_WRITE(c, z);
        }

        bool color_written = false;
        for (int i = 0; i < 4; i++) {
                if (swizzled_outputs[i].file != QFILE_NULL)
                        color_written = true;
        }

        struct qreg packed_color;
        if (color_written) {
                /* Fill in any undefined colors.  The simulator will assertion
                 * fail if we read something that wasn't written, and I don't
                 * know what hardware does.
                 */
                for (int i = 0; i < 4; i++) {
                        if (swizzled_outputs[i].file == QFILE_NULL)
                                swizzled_outputs[i] = qir_uniform_f(c, 0.0);
                }
                packed_color = qir_get_temp(c);
                qir_emit(c, qir_inst4(QOP_PACK_COLORS, packed_color,
                                      swizzled_outputs[0],
                                      swizzled_outputs[1],
                                      swizzled_outputs[2],
                                      swizzled_outputs[3]));
        } else {
                packed_color = qir_uniform_ui(c, 0);
        }

        qir_emit(c, qir_inst(QOP_TLB_COLOR_WRITE, c->undef,
                             packed_color, c->undef));
}

static void
emit_scaled_viewport_write(struct vc4_compile *c, struct qreg rcp_w)
{
        struct qreg xyi[2];

        for (int i = 0; i < 2; i++) {
                struct qreg scale =
                        add_uniform(c, QUNIFORM_VIEWPORT_X_SCALE + i, 0);

                xyi[i] = qir_FTOI(c, qir_FMUL(c,
                                              qir_FMUL(c,
                                                       c->outputs[c->output_position_index + i],
                                                       scale),
                                              rcp_w));
        }

        qir_VPM_WRITE(c, qir_PACK_SCALED(c, xyi[0], xyi[1]));
}

static void
emit_zs_write(struct vc4_compile *c, struct qreg rcp_w)
{
        struct qreg zscale = add_uniform(c, QUNIFORM_VIEWPORT_Z_SCALE, 0);
        struct qreg zoffset = add_uniform(c, QUNIFORM_VIEWPORT_Z_OFFSET, 0);

        qir_VPM_WRITE(c, qir_FMUL(c, qir_FADD(c, qir_FMUL(c,
                                                          c->outputs[c->output_position_index + 2],
                                                          zscale),
                                              zoffset),
                                  rcp_w));
}

static void
emit_rcp_wc_write(struct vc4_compile *c, struct qreg rcp_w)
{
        qir_VPM_WRITE(c, rcp_w);
}

static void
emit_point_size_write(struct vc4_compile *c)
{
        struct qreg point_size;

        if (c->output_point_size_index)
                point_size = c->outputs[c->output_point_size_index + 3];
        else
                point_size = qir_uniform_f(c, 1.0);

        /* Workaround: HW-2726 PTB does not handle zero-size points (BCM2835,
         * BCM21553).
         */
        point_size = qir_FMAX(c, point_size, qir_uniform_f(c, .125));

        qir_VPM_WRITE(c, point_size);
}

/**
 * Emits a VPM read of the stub vertex attribute set up by vc4_draw.c.
 *
 * The simulator insists that there be at least one vertex attribute, so
 * vc4_draw.c will emit one if it wouldn't have otherwise.  The simulator also
 * insists that all vertex attributes loaded get read by the VS/CS, so we have
 * to consume it here.
 */
static void
emit_stub_vpm_read(struct vc4_compile *c)
{
        if (c->num_inputs)
                return;

        for (int i = 0; i < 4; i++) {
                qir_emit(c, qir_inst(QOP_VPM_READ,
                                     qir_get_temp(c),
                                     c->undef,
                                     c->undef));
                c->num_inputs++;
        }
}

static void
emit_ucp_clipdistance(struct vc4_compile *c)
{
        unsigned cv;
        if (c->output_clipvertex_index != -1)
                cv = c->output_clipvertex_index;
        else if (c->output_position_index != -1)
                cv = c->output_position_index;
        else
                return;

        for (int plane = 0; plane < PIPE_MAX_CLIP_PLANES; plane++) {
                if (!(c->key->ucp_enables & (1 << plane)))
                        continue;

                /* Pick the next outputs[] that hasn't been written to, since
                 * there are no other program writes left to be processed at
                 * this point.  If something had been declared but not written
                 * (like a w component), we'll just smash over the top of it.
                 */
                uint32_t output_index = c->num_outputs++;
                add_output(c, output_index,
                           TGSI_SEMANTIC_CLIPDIST,
                           plane,
                           TGSI_SWIZZLE_X);


                struct qreg dist = qir_uniform_f(c, 0.0);
                for (int i = 0; i < 4; i++) {
                        struct qreg pos_chan = c->outputs[cv + i];
                        struct qreg ucp =
                                add_uniform(c, QUNIFORM_USER_CLIP_PLANE,
                                            plane * 4 + i);
                        dist = qir_FADD(c, dist, qir_FMUL(c, pos_chan, ucp));
                }

                c->outputs[output_index] = dist;
        }
}

static void
emit_vert_end(struct vc4_compile *c,
              struct vc4_varying_semantic *fs_inputs,
              uint32_t num_fs_inputs)
{
        struct qreg rcp_w = qir_RCP(c, c->outputs[c->output_position_index + 3]);

        emit_stub_vpm_read(c);
        emit_ucp_clipdistance(c);

        emit_scaled_viewport_write(c, rcp_w);
        emit_zs_write(c, rcp_w);
        emit_rcp_wc_write(c, rcp_w);
        if (c->vs_key->per_vertex_point_size)
                emit_point_size_write(c);

        for (int i = 0; i < num_fs_inputs; i++) {
                struct vc4_varying_semantic *input = &fs_inputs[i];
                int j;

                for (j = 0; j < c->num_outputs; j++) {
                        struct vc4_varying_semantic *output =
                                &c->output_semantics[j];

                        if (input->semantic == output->semantic &&
                            input->index == output->index &&
                            input->swizzle == output->swizzle) {
                                qir_VPM_WRITE(c, c->outputs[j]);
                                break;
                        }
                }
                /* Emit padding if we didn't find a declared VS output for
                 * this FS input.
                 */
                if (j == c->num_outputs)
                        qir_VPM_WRITE(c, qir_uniform_f(c, 0.0));
        }
}

static void
emit_coord_end(struct vc4_compile *c)
{
        struct qreg rcp_w = qir_RCP(c, c->outputs[c->output_position_index + 3]);

        emit_stub_vpm_read(c);

        for (int i = 0; i < 4; i++)
                qir_VPM_WRITE(c, c->outputs[c->output_position_index + i]);

        emit_scaled_viewport_write(c, rcp_w);
        emit_zs_write(c, rcp_w);
        emit_rcp_wc_write(c, rcp_w);
        if (c->vs_key->per_vertex_point_size)
                emit_point_size_write(c);
}

static struct vc4_compile *
vc4_shader_tgsi_to_qir(struct vc4_context *vc4, enum qstage stage,
                       struct vc4_key *key)
{
        struct vc4_compile *c = qir_compile_init();
        int ret;

        c->stage = stage;
        for (int i = 0; i < 4; i++)
                c->addr[i] = qir_uniform_f(c, 0.0);

        c->shader_state = &key->shader_state->base;
        c->program_id = key->shader_state->program_id;
        c->variant_id = key->shader_state->compiled_variant_count++;

        c->key = key;
        switch (stage) {
        case QSTAGE_FRAG:
                c->fs_key = (struct vc4_fs_key *)key;
                if (c->fs_key->is_points) {
                        c->point_x = emit_fragment_varying(c, ~0, ~0, 0);
                        c->point_y = emit_fragment_varying(c, ~0, ~0, 0);
                } else if (c->fs_key->is_lines) {
                        c->line_x = emit_fragment_varying(c, ~0, ~0, 0);
                }
                break;
        case QSTAGE_VERT:
                c->vs_key = (struct vc4_vs_key *)key;
                break;
        case QSTAGE_COORD:
                c->vs_key = (struct vc4_vs_key *)key;
                break;
        }

        const struct tgsi_token *tokens = key->shader_state->base.tokens;
        if (c->fs_key && c->fs_key->light_twoside) {
                if (!key->shader_state->twoside_tokens) {
                        const struct tgsi_lowering_config lowering_config = {
                                .color_two_side = true,
                        };
                        struct tgsi_shader_info info;
                        key->shader_state->twoside_tokens =
                                tgsi_transform_lowering(&lowering_config,
                                                        key->shader_state->base.tokens,
                                                        &info);

                        /* If no transformation occurred, then NULL is
                         * returned and we just use our original tokens.
                         */
                        if (!key->shader_state->twoside_tokens) {
                                key->shader_state->twoside_tokens =
                                        key->shader_state->base.tokens;
                        }
                }
                tokens = key->shader_state->twoside_tokens;
        }

        ret = tgsi_parse_init(&c->parser, tokens);
        assert(ret == TGSI_PARSE_OK);

        if (vc4_debug & VC4_DEBUG_TGSI) {
                fprintf(stderr, "%s prog %d/%d TGSI:\n",
                        qir_get_stage_name(c->stage),
                        c->program_id, c->variant_id);
                tgsi_dump(tokens, 0);
        }

        while (!tgsi_parse_end_of_tokens(&c->parser)) {
                tgsi_parse_token(&c->parser);

                switch (c->parser.FullToken.Token.Type) {
                case TGSI_TOKEN_TYPE_DECLARATION:
                        emit_tgsi_declaration(c,
                                              &c->parser.FullToken.FullDeclaration);
                        break;

                case TGSI_TOKEN_TYPE_INSTRUCTION:
                        emit_tgsi_instruction(c,
                                              &c->parser.FullToken.FullInstruction);
                        break;

                case TGSI_TOKEN_TYPE_IMMEDIATE:
                        parse_tgsi_immediate(c,
                                             &c->parser.FullToken.FullImmediate);
                        break;
                }
        }

        switch (stage) {
        case QSTAGE_FRAG:
                emit_frag_end(c);
                break;
        case QSTAGE_VERT:
                emit_vert_end(c,
                              vc4->prog.fs->input_semantics,
                              vc4->prog.fs->num_inputs);
                break;
        case QSTAGE_COORD:
                emit_coord_end(c);
                break;
        }

        tgsi_parse_free(&c->parser);

        qir_optimize(c);

        if (vc4_debug & VC4_DEBUG_QIR) {
                fprintf(stderr, "%s prog %d/%d QIR:\n",
                        qir_get_stage_name(c->stage),
                        c->program_id, c->variant_id);
                qir_dump(c);
        }
        qir_reorder_uniforms(c);
        vc4_generate_code(vc4, c);

        if (vc4_debug & VC4_DEBUG_SHADERDB) {
                fprintf(stderr, "SHADER-DB: %s prog %d/%d: %d instructions\n",
                        qir_get_stage_name(c->stage),
                        c->program_id, c->variant_id,
                        c->qpu_inst_count);
                fprintf(stderr, "SHADER-DB: %s prog %d/%d: %d uniforms\n",
                        qir_get_stage_name(c->stage),
                        c->program_id, c->variant_id,
                        c->num_uniforms);
        }

        return c;
}

static void *
vc4_shader_state_create(struct pipe_context *pctx,
                        const struct pipe_shader_state *cso)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        struct vc4_uncompiled_shader *so = CALLOC_STRUCT(vc4_uncompiled_shader);
        if (!so)
                return NULL;

        const struct tgsi_lowering_config lowering_config = {
                .lower_DST = true,
                .lower_XPD = true,
                .lower_SCS = true,
                .lower_POW = true,
                .lower_LIT = true,
                .lower_EXP = true,
                .lower_LOG = true,
                .lower_DP4 = true,
                .lower_DP3 = true,
                .lower_DPH = true,
                .lower_DP2 = true,
                .lower_DP2A = true,
        };

        struct tgsi_shader_info info;
        so->base.tokens = tgsi_transform_lowering(&lowering_config, cso->tokens, &info);
        if (!so->base.tokens)
                so->base.tokens = tgsi_dup_tokens(cso->tokens);
        so->program_id = vc4->next_uncompiled_program_id++;

        return so;
}

static void
copy_uniform_state_to_shader(struct vc4_compiled_shader *shader,
                             struct vc4_compile *c)
{
        int count = c->num_uniforms;
        struct vc4_shader_uniform_info *uinfo = &shader->uniforms;

        uinfo->count = count;
        uinfo->data = ralloc_array(shader, uint32_t, count);
        memcpy(uinfo->data, c->uniform_data,
               count * sizeof(*uinfo->data));
        uinfo->contents = ralloc_array(shader, enum quniform_contents, count);
        memcpy(uinfo->contents, c->uniform_contents,
               count * sizeof(*uinfo->contents));
        uinfo->num_texture_samples = c->num_texture_samples;
}

static struct vc4_compiled_shader *
vc4_get_compiled_shader(struct vc4_context *vc4, enum qstage stage,
                        struct vc4_key *key)
{
        struct util_hash_table *ht;
        uint32_t key_size;
        if (stage == QSTAGE_FRAG) {
                ht = vc4->fs_cache;
                key_size = sizeof(struct vc4_fs_key);
        } else {
                ht = vc4->vs_cache;
                key_size = sizeof(struct vc4_vs_key);
        }

        struct vc4_compiled_shader *shader;
        shader = util_hash_table_get(ht, key);
        if (shader)
                return shader;

        struct vc4_compile *c = vc4_shader_tgsi_to_qir(vc4, stage, key);
        shader = rzalloc(NULL, struct vc4_compiled_shader);

        shader->program_id = vc4->next_compiled_program_id++;
        if (stage == QSTAGE_FRAG) {
                bool input_live[c->num_input_semantics];
                struct simple_node *node;

                memset(input_live, 0, sizeof(input_live));
                foreach(node, &c->instructions) {
                        struct qinst *inst = (struct qinst *)node;
                        for (int i = 0; i < qir_get_op_nsrc(inst->op); i++) {
                                if (inst->src[i].file == QFILE_VARY)
                                        input_live[inst->src[i].index] = true;
                        }
                }

                shader->input_semantics = ralloc_array(shader,
                                                       struct vc4_varying_semantic,
                                                       c->num_input_semantics);

                for (int i = 0; i < c->num_input_semantics; i++) {
                        struct vc4_varying_semantic *sem = &c->input_semantics[i];

                        if (!input_live[i])
                                continue;

                        /* Skip non-VS-output inputs. */
                        if (sem->semantic == (uint8_t)~0)
                                continue;

                        if (sem->semantic == TGSI_SEMANTIC_COLOR)
                                shader->color_inputs |= (1 << shader->num_inputs);
                        shader->input_semantics[shader->num_inputs] = *sem;
                        shader->num_inputs++;
                }
        } else {
                shader->num_inputs = c->num_inputs;
        }

        copy_uniform_state_to_shader(shader, c);
        shader->bo = vc4_bo_alloc_mem(vc4->screen, c->qpu_insts,
                                      c->qpu_inst_count * sizeof(uint64_t),
                                      "code");

        /* Copy the compiler UBO range state to the compiled shader, dropping
         * out arrays that were never referenced by an indirect load.
         *
         * (Note that QIR dead code elimination of an array access still
         * leaves that array alive, though)
         */
        if (c->num_ubo_ranges) {
                shader->num_ubo_ranges = c->num_ubo_ranges;
                shader->ubo_ranges = ralloc_array(shader, struct vc4_ubo_range,
                                                  c->num_ubo_ranges);
                uint32_t j = 0;
                for (int i = 0; i < c->ubo_ranges_array_size; i++) {
                        struct vc4_compiler_ubo_range *range =
                                &c->ubo_ranges[i];
                        if (!range->used)
                                continue;

                        shader->ubo_ranges[j].dst_offset = range->dst_offset;
                        shader->ubo_ranges[j].src_offset = range->src_offset;
                        shader->ubo_ranges[j].size = range->size;
                        shader->ubo_size += c->ubo_ranges[i].size;
                        j++;
                }
        }

        qir_compile_destroy(c);

        struct vc4_key *dup_key;
        dup_key = malloc(key_size);
        memcpy(dup_key, key, key_size);
        util_hash_table_set(ht, dup_key, shader);

        return shader;
}

static void
vc4_setup_shared_key(struct vc4_context *vc4, struct vc4_key *key,
                     struct vc4_texture_stateobj *texstate)
{
        for (int i = 0; i < texstate->num_textures; i++) {
                struct pipe_sampler_view *sampler = texstate->textures[i];
                struct pipe_sampler_state *sampler_state =
                        texstate->samplers[i];

                if (sampler) {
                        key->tex[i].format = sampler->format;
                        key->tex[i].swizzle[0] = sampler->swizzle_r;
                        key->tex[i].swizzle[1] = sampler->swizzle_g;
                        key->tex[i].swizzle[2] = sampler->swizzle_b;
                        key->tex[i].swizzle[3] = sampler->swizzle_a;
                        key->tex[i].compare_mode = sampler_state->compare_mode;
                        key->tex[i].compare_func = sampler_state->compare_func;
                        key->tex[i].wrap_s = sampler_state->wrap_s;
                        key->tex[i].wrap_t = sampler_state->wrap_t;
                }
        }

        key->ucp_enables = vc4->rasterizer->base.clip_plane_enable;
}

static void
vc4_update_compiled_fs(struct vc4_context *vc4, uint8_t prim_mode)
{
        struct vc4_fs_key local_key;
        struct vc4_fs_key *key = &local_key;

        if (!(vc4->dirty & (VC4_DIRTY_PRIM_MODE |
                            VC4_DIRTY_BLEND |
                            VC4_DIRTY_FRAMEBUFFER |
                            VC4_DIRTY_ZSA |
                            VC4_DIRTY_RASTERIZER |
                            VC4_DIRTY_FRAGTEX |
                            VC4_DIRTY_TEXSTATE |
                            VC4_DIRTY_PROG))) {
                return;
        }

        memset(key, 0, sizeof(*key));
        vc4_setup_shared_key(vc4, &key->base, &vc4->fragtex);
        key->base.shader_state = vc4->prog.bind_fs;
        key->is_points = (prim_mode == PIPE_PRIM_POINTS);
        key->is_lines = (prim_mode >= PIPE_PRIM_LINES &&
                         prim_mode <= PIPE_PRIM_LINE_STRIP);
        key->blend = vc4->blend->rt[0];

        if (vc4->framebuffer.cbufs[0])
                key->color_format = vc4->framebuffer.cbufs[0]->format;

        key->stencil_enabled = vc4->zsa->stencil_uniforms[0] != 0;
        key->stencil_twoside = vc4->zsa->stencil_uniforms[1] != 0;
        key->stencil_full_writemasks = vc4->zsa->stencil_uniforms[2] != 0;
        key->depth_enabled = (vc4->zsa->base.depth.enabled ||
                              key->stencil_enabled);
        if (vc4->zsa->base.alpha.enabled) {
                key->alpha_test = true;
                key->alpha_test_func = vc4->zsa->base.alpha.func;
        }

        if (key->is_points) {
                key->point_sprite_mask =
                        vc4->rasterizer->base.sprite_coord_enable;
                key->point_coord_upper_left =
                        (vc4->rasterizer->base.sprite_coord_mode ==
                         PIPE_SPRITE_COORD_UPPER_LEFT);
        }

        key->light_twoside = vc4->rasterizer->base.light_twoside;

        struct vc4_compiled_shader *old_fs = vc4->prog.fs;
        vc4->prog.fs = vc4_get_compiled_shader(vc4, QSTAGE_FRAG, &key->base);
        if (vc4->prog.fs == old_fs)
                return;

        if (vc4->rasterizer->base.flatshade &&
            old_fs && vc4->prog.fs->color_inputs != old_fs->color_inputs) {
                vc4->dirty |= VC4_DIRTY_FLAT_SHADE_FLAGS;
        }
}

static void
vc4_update_compiled_vs(struct vc4_context *vc4, uint8_t prim_mode)
{
        struct vc4_vs_key local_key;
        struct vc4_vs_key *key = &local_key;

        if (!(vc4->dirty & (VC4_DIRTY_PRIM_MODE |
                            VC4_DIRTY_RASTERIZER |
                            VC4_DIRTY_VERTTEX |
                            VC4_DIRTY_TEXSTATE |
                            VC4_DIRTY_VTXSTATE |
                            VC4_DIRTY_PROG))) {
                return;
        }

        memset(key, 0, sizeof(*key));
        vc4_setup_shared_key(vc4, &key->base, &vc4->verttex);
        key->base.shader_state = vc4->prog.bind_vs;
        key->compiled_fs_id = vc4->prog.fs->program_id;

        for (int i = 0; i < ARRAY_SIZE(key->attr_formats); i++)
                key->attr_formats[i] = vc4->vtx->pipe[i].src_format;

        key->per_vertex_point_size =
                (prim_mode == PIPE_PRIM_POINTS &&
                 vc4->rasterizer->base.point_size_per_vertex);

        vc4->prog.vs = vc4_get_compiled_shader(vc4, QSTAGE_VERT, &key->base);
        key->is_coord = true;
        vc4->prog.cs = vc4_get_compiled_shader(vc4, QSTAGE_COORD, &key->base);
}

void
vc4_update_compiled_shaders(struct vc4_context *vc4, uint8_t prim_mode)
{
        vc4_update_compiled_fs(vc4, prim_mode);
        vc4_update_compiled_vs(vc4, prim_mode);
}

static unsigned
fs_cache_hash(void *key)
{
        return _mesa_hash_data(key, sizeof(struct vc4_fs_key));
}

static unsigned
vs_cache_hash(void *key)
{
        return _mesa_hash_data(key, sizeof(struct vc4_vs_key));
}

static int
fs_cache_compare(void *key1, void *key2)
{
        return memcmp(key1, key2, sizeof(struct vc4_fs_key));
}

static int
vs_cache_compare(void *key1, void *key2)
{
        return memcmp(key1, key2, sizeof(struct vc4_vs_key));
}

struct delete_state {
        struct vc4_context *vc4;
        struct vc4_uncompiled_shader *shader_state;
};

static enum pipe_error
fs_delete_from_cache(void *in_key, void *in_value, void *data)
{
        struct delete_state *del = data;
        struct vc4_fs_key *key = in_key;
        struct vc4_compiled_shader *shader = in_value;

        if (key->base.shader_state == data) {
                util_hash_table_remove(del->vc4->fs_cache, key);
                vc4_bo_unreference(&shader->bo);
                ralloc_free(shader);
        }

        return 0;
}

static enum pipe_error
vs_delete_from_cache(void *in_key, void *in_value, void *data)
{
        struct delete_state *del = data;
        struct vc4_vs_key *key = in_key;
        struct vc4_compiled_shader *shader = in_value;

        if (key->base.shader_state == data) {
                util_hash_table_remove(del->vc4->vs_cache, key);
                vc4_bo_unreference(&shader->bo);
                ralloc_free(shader);
        }

        return 0;
}

static void
vc4_shader_state_delete(struct pipe_context *pctx, void *hwcso)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        struct vc4_uncompiled_shader *so = hwcso;
        struct delete_state del;

        del.vc4 = vc4;
        del.shader_state = so;
        util_hash_table_foreach(vc4->fs_cache, fs_delete_from_cache, &del);
        util_hash_table_foreach(vc4->vs_cache, vs_delete_from_cache, &del);

        if (so->twoside_tokens != so->base.tokens)
                free((void *)so->twoside_tokens);
        free((void *)so->base.tokens);
        free(so);
}

static uint32_t translate_wrap(uint32_t p_wrap, bool using_nearest)
{
        switch (p_wrap) {
        case PIPE_TEX_WRAP_REPEAT:
                return 0;
        case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
                return 1;
        case PIPE_TEX_WRAP_MIRROR_REPEAT:
                return 2;
        case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
                return 3;
        case PIPE_TEX_WRAP_CLAMP:
                return (using_nearest ? 1 : 3);
        default:
                fprintf(stderr, "Unknown wrap mode %d\n", p_wrap);
                assert(!"not reached");
                return 0;
        }
}

static void
write_texture_p0(struct vc4_context *vc4,
                 struct vc4_texture_stateobj *texstate,
                 uint32_t unit)
{
        struct pipe_sampler_view *texture = texstate->textures[unit];
        struct vc4_resource *rsc = vc4_resource(texture->texture);

        cl_reloc(vc4, &vc4->uniforms, rsc->bo,
                 VC4_SET_FIELD(rsc->slices[0].offset >> 12, VC4_TEX_P0_OFFSET) |
                 VC4_SET_FIELD(texture->u.tex.last_level -
                               texture->u.tex.first_level, VC4_TEX_P0_MIPLVLS) |
                 VC4_SET_FIELD(texture->target == PIPE_TEXTURE_CUBE,
                               VC4_TEX_P0_CMMODE) |
                 VC4_SET_FIELD(rsc->vc4_format & 7, VC4_TEX_P0_TYPE));
}

static void
write_texture_p1(struct vc4_context *vc4,
                 struct vc4_texture_stateobj *texstate,
                 uint32_t unit)
{
        struct pipe_sampler_view *texture = texstate->textures[unit];
        struct vc4_resource *rsc = vc4_resource(texture->texture);
        struct pipe_sampler_state *sampler = texstate->samplers[unit];
        static const uint8_t minfilter_map[6] = {
                VC4_TEX_P1_MINFILT_NEAR_MIP_NEAR,
                VC4_TEX_P1_MINFILT_LIN_MIP_NEAR,
                VC4_TEX_P1_MINFILT_NEAR_MIP_LIN,
                VC4_TEX_P1_MINFILT_LIN_MIP_LIN,
                VC4_TEX_P1_MINFILT_NEAREST,
                VC4_TEX_P1_MINFILT_LINEAR,
        };
        static const uint32_t magfilter_map[] = {
                [PIPE_TEX_FILTER_NEAREST] = VC4_TEX_P1_MAGFILT_NEAREST,
                [PIPE_TEX_FILTER_LINEAR] = VC4_TEX_P1_MAGFILT_LINEAR,
        };

        bool either_nearest =
                (sampler->mag_img_filter == PIPE_TEX_MIPFILTER_NEAREST ||
                 sampler->min_img_filter == PIPE_TEX_MIPFILTER_NEAREST);

        cl_u32(&vc4->uniforms,
               VC4_SET_FIELD(rsc->vc4_format >> 4, VC4_TEX_P1_TYPE4) |
               VC4_SET_FIELD(texture->texture->height0 & 2047,
                             VC4_TEX_P1_HEIGHT) |
               VC4_SET_FIELD(texture->texture->width0 & 2047,
                             VC4_TEX_P1_WIDTH) |
               VC4_SET_FIELD(magfilter_map[sampler->mag_img_filter],
                             VC4_TEX_P1_MAGFILT) |
               VC4_SET_FIELD(minfilter_map[sampler->min_mip_filter * 2 +
                                           sampler->min_img_filter],
                             VC4_TEX_P1_MINFILT) |
               VC4_SET_FIELD(translate_wrap(sampler->wrap_s, either_nearest),
                             VC4_TEX_P1_WRAP_S) |
               VC4_SET_FIELD(translate_wrap(sampler->wrap_t, either_nearest),
                             VC4_TEX_P1_WRAP_T));
}

static void
write_texture_p2(struct vc4_context *vc4,
                 struct vc4_texture_stateobj *texstate,
                 uint32_t data)
{
        uint32_t unit = data & 0xffff;
        struct pipe_sampler_view *texture = texstate->textures[unit];
        struct vc4_resource *rsc = vc4_resource(texture->texture);

        cl_u32(&vc4->uniforms,
               VC4_SET_FIELD(VC4_TEX_P2_PTYPE_CUBE_MAP_STRIDE,
                             VC4_TEX_P2_PTYPE) |
               VC4_SET_FIELD(rsc->cube_map_stride >> 12, VC4_TEX_P2_CMST) |
               VC4_SET_FIELD((data >> 16) & 1, VC4_TEX_P2_BSLOD));
}


#define SWIZ(x,y,z,w) {          \
        UTIL_FORMAT_SWIZZLE_##x, \
        UTIL_FORMAT_SWIZZLE_##y, \
        UTIL_FORMAT_SWIZZLE_##z, \
        UTIL_FORMAT_SWIZZLE_##w  \
}

static void
write_texture_border_color(struct vc4_context *vc4,
                           struct vc4_texture_stateobj *texstate,
                           uint32_t unit)
{
        struct pipe_sampler_state *sampler = texstate->samplers[unit];
        struct pipe_sampler_view *texture = texstate->textures[unit];
        struct vc4_resource *rsc = vc4_resource(texture->texture);
        union util_color uc;

        const struct util_format_description *tex_format_desc =
                util_format_description(texture->format);

        float border_color[4];
        for (int i = 0; i < 4; i++)
                border_color[i] = sampler->border_color.f[i];
        if (util_format_is_srgb(texture->format)) {
                for (int i = 0; i < 3; i++)
                        border_color[i] =
                                util_format_linear_to_srgb_float(border_color[i]);
        }

        /* Turn the border color into the layout of channels that it would
         * have when stored as texture contents.
         */
        float storage_color[4];
        util_format_unswizzle_4f(storage_color,
                                 border_color,
                                 tex_format_desc->swizzle);

        /* Now, pack so that when the vc4_format-sampled texture contents are
         * replaced with our border color, the vc4_get_format_swizzle()
         * swizzling will get the right channels.
         */
        if (util_format_is_depth_or_stencil(texture->format)) {
                uc.ui[0] = util_pack_z(PIPE_FORMAT_Z24X8_UNORM,
                                       sampler->border_color.f[0]) << 8;
        } else {
                switch (rsc->vc4_format) {
                default:
                case VC4_TEXTURE_TYPE_RGBA8888:
                        util_pack_color(storage_color,
                                        PIPE_FORMAT_R8G8B8A8_UNORM, &uc);
                        break;
                case VC4_TEXTURE_TYPE_RGBA4444:
                        util_pack_color(storage_color,
                                        PIPE_FORMAT_A8B8G8R8_UNORM, &uc);
                        break;
                case VC4_TEXTURE_TYPE_RGB565:
                        util_pack_color(storage_color,
                                        PIPE_FORMAT_B8G8R8A8_UNORM, &uc);
                        break;
                case VC4_TEXTURE_TYPE_ALPHA:
                        uc.ui[0] = float_to_ubyte(storage_color[0]) << 24;
                        break;
                case VC4_TEXTURE_TYPE_LUMALPHA:
                        uc.ui[0] = ((float_to_ubyte(storage_color[1]) << 24) |
                                    (float_to_ubyte(storage_color[0]) << 0));
                        break;
                }
        }

        cl_u32(&vc4->uniforms, uc.ui[0]);
}

static uint32_t
get_texrect_scale(struct vc4_texture_stateobj *texstate,
                  enum quniform_contents contents,
                  uint32_t data)
{
        struct pipe_sampler_view *texture = texstate->textures[data];
        uint32_t dim;

        if (contents == QUNIFORM_TEXRECT_SCALE_X)
                dim = texture->texture->width0;
        else
                dim = texture->texture->height0;

        return fui(1.0f / dim);
}

static struct vc4_bo *
vc4_upload_ubo(struct vc4_context *vc4, struct vc4_compiled_shader *shader,
               const uint32_t *gallium_uniforms)
{
        if (!shader->ubo_size)
                return NULL;

        struct vc4_bo *ubo = vc4_bo_alloc(vc4->screen, shader->ubo_size, "ubo");
        uint32_t *data = vc4_bo_map(ubo);
        for (uint32_t i = 0; i < shader->num_ubo_ranges; i++) {
                memcpy(data + shader->ubo_ranges[i].dst_offset,
                       gallium_uniforms + shader->ubo_ranges[i].src_offset,
                       shader->ubo_ranges[i].size);
        }

        return ubo;
}

void
vc4_write_uniforms(struct vc4_context *vc4, struct vc4_compiled_shader *shader,
                   struct vc4_constbuf_stateobj *cb,
                   struct vc4_texture_stateobj *texstate)
{
        struct vc4_shader_uniform_info *uinfo = &shader->uniforms;
        const uint32_t *gallium_uniforms = cb->cb[0].user_buffer;
        struct vc4_bo *ubo = vc4_upload_ubo(vc4, shader, gallium_uniforms);

        cl_start_shader_reloc(&vc4->uniforms, uinfo->num_texture_samples);

        for (int i = 0; i < uinfo->count; i++) {

                switch (uinfo->contents[i]) {
                case QUNIFORM_CONSTANT:
                        cl_u32(&vc4->uniforms, uinfo->data[i]);
                        break;
                case QUNIFORM_UNIFORM:
                        cl_u32(&vc4->uniforms,
                               gallium_uniforms[uinfo->data[i]]);
                        break;
                case QUNIFORM_VIEWPORT_X_SCALE:
                        cl_f(&vc4->uniforms, vc4->viewport.scale[0] * 16.0f);
                        break;
                case QUNIFORM_VIEWPORT_Y_SCALE:
                        cl_f(&vc4->uniforms, vc4->viewport.scale[1] * 16.0f);
                        break;

                case QUNIFORM_VIEWPORT_Z_OFFSET:
                        cl_f(&vc4->uniforms, vc4->viewport.translate[2]);
                        break;
                case QUNIFORM_VIEWPORT_Z_SCALE:
                        cl_f(&vc4->uniforms, vc4->viewport.scale[2]);
                        break;

                case QUNIFORM_USER_CLIP_PLANE:
                        cl_f(&vc4->uniforms,
                             vc4->clip.ucp[uinfo->data[i] / 4][uinfo->data[i] % 4]);
                        break;

                case QUNIFORM_TEXTURE_CONFIG_P0:
                        write_texture_p0(vc4, texstate, uinfo->data[i]);
                        break;

                case QUNIFORM_TEXTURE_CONFIG_P1:
                        write_texture_p1(vc4, texstate, uinfo->data[i]);
                        break;

                case QUNIFORM_TEXTURE_CONFIG_P2:
                        write_texture_p2(vc4, texstate, uinfo->data[i]);
                        break;

                case QUNIFORM_UBO_ADDR:
                        cl_reloc(vc4, &vc4->uniforms, ubo, 0);
                        break;

                case QUNIFORM_TEXTURE_BORDER_COLOR:
                        write_texture_border_color(vc4, texstate, uinfo->data[i]);
                        break;

                case QUNIFORM_TEXRECT_SCALE_X:
                case QUNIFORM_TEXRECT_SCALE_Y:
                        cl_u32(&vc4->uniforms,
                               get_texrect_scale(texstate,
                                                 uinfo->contents[i],
                                                 uinfo->data[i]));
                        break;

                case QUNIFORM_BLEND_CONST_COLOR:
                        cl_f(&vc4->uniforms,
                             vc4->blend_color.color[uinfo->data[i]]);
                        break;

                case QUNIFORM_STENCIL:
                        cl_u32(&vc4->uniforms,
                               vc4->zsa->stencil_uniforms[uinfo->data[i]] |
                               (uinfo->data[i] <= 1 ?
                                (vc4->stencil_ref.ref_value[uinfo->data[i]] << 8) :
                                0));
                        break;

                case QUNIFORM_ALPHA_REF:
                        cl_f(&vc4->uniforms, vc4->zsa->base.alpha.ref_value);
                        break;
                }
#if 0
                uint32_t written_val = *(uint32_t *)(vc4->uniforms.next - 4);
                fprintf(stderr, "%p: %d / 0x%08x (%f)\n",
                        shader, i, written_val, uif(written_val));
#endif
        }
}

static void
vc4_fp_state_bind(struct pipe_context *pctx, void *hwcso)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        vc4->prog.bind_fs = hwcso;
        vc4->prog.dirty |= VC4_SHADER_DIRTY_FP;
        vc4->dirty |= VC4_DIRTY_PROG;
}

static void
vc4_vp_state_bind(struct pipe_context *pctx, void *hwcso)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        vc4->prog.bind_vs = hwcso;
        vc4->prog.dirty |= VC4_SHADER_DIRTY_VP;
        vc4->dirty |= VC4_DIRTY_PROG;
}

void
vc4_program_init(struct pipe_context *pctx)
{
        struct vc4_context *vc4 = vc4_context(pctx);

        pctx->create_vs_state = vc4_shader_state_create;
        pctx->delete_vs_state = vc4_shader_state_delete;

        pctx->create_fs_state = vc4_shader_state_create;
        pctx->delete_fs_state = vc4_shader_state_delete;

        pctx->bind_fs_state = vc4_fp_state_bind;
        pctx->bind_vs_state = vc4_vp_state_bind;

        vc4->fs_cache = util_hash_table_create(fs_cache_hash, fs_cache_compare);
        vc4->vs_cache = util_hash_table_create(vs_cache_hash, vs_cache_compare);
}
