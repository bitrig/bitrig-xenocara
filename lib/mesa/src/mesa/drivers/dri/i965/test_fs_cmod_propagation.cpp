/*
 * Copyright © 2015 Intel Corporation
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

#include <gtest/gtest.h>
#include "brw_fs.h"
#include "brw_cfg.h"
#include "program/program.h"

using namespace brw;

class cmod_propagation_test : public ::testing::Test {
   virtual void SetUp();

public:
   struct brw_compiler *compiler;
   struct brw_device_info *devinfo;
   struct gl_context *ctx;
   struct brw_wm_prog_data *prog_data;
   struct gl_shader_program *shader_prog;
   struct brw_fragment_program *fp;
   fs_visitor *v;
};

class cmod_propagation_fs_visitor : public fs_visitor
{
public:
   cmod_propagation_fs_visitor(struct brw_compiler *compiler,
                               struct brw_wm_prog_data *prog_data,
                               struct gl_shader_program *shader_prog)
      : fs_visitor(compiler, NULL, NULL, MESA_SHADER_FRAGMENT, NULL,
                   &prog_data->base, shader_prog,
                   (struct gl_program *) NULL, 8, -1) {}
};


void cmod_propagation_test::SetUp()
{
   ctx = (struct gl_context *)calloc(1, sizeof(*ctx));
   compiler = (struct brw_compiler *)calloc(1, sizeof(*compiler));
   devinfo = (struct brw_device_info *)calloc(1, sizeof(*devinfo));
   compiler->devinfo = devinfo;

   fp = ralloc(NULL, struct brw_fragment_program);
   prog_data = ralloc(NULL, struct brw_wm_prog_data);
   shader_prog = ralloc(NULL, struct gl_shader_program);

   v = new cmod_propagation_fs_visitor(compiler, prog_data, shader_prog);

   _mesa_init_fragment_program(ctx, &fp->program, GL_FRAGMENT_SHADER, 0);

   devinfo->gen = 4;
}

static fs_inst *
instruction(bblock_t *block, int num)
{
   fs_inst *inst = (fs_inst *)block->start();
   for (int i = 0; i < num; i++) {
      inst = (fs_inst *)inst->next;
   }
   return inst;
}

static bool
cmod_propagation(fs_visitor *v)
{
   const bool print = false;

   if (print) {
      fprintf(stderr, "= Before =\n");
      v->cfg->dump(v);
   }

   bool ret = v->opt_cmod_propagation();

   if (print) {
      fprintf(stderr, "\n= After =\n");
      v->cfg->dump(v);
   }

   return ret;
}

TEST_F(cmod_propagation_test, basic)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg zero(0.0f);
   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), dest, zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)        dest  src0  src1
    * 1: cmp.ge.f0(8)  null  dest  0.0f
    *
    * = After =
    * 0: add.ge.f0(8)  dest  src0  src1
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, cmp_nonzero)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg nonzero(1.0f);
   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), dest, nonzero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)        dest  src0  src1
    * 1: cmp.ge.f0(8)  null  dest  1.0f
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, non_cmod_instruction)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::uint_type);
   fs_reg src0 = v->vgrf(glsl_type::uint_type);
   fs_reg zero(0u);
   bld.FBL(dest, src0);
   bld.CMP(bld.null_reg_ud(), dest, zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: fbl(8)        dest  src0
    * 1: cmp.ge.f0(8)  null  dest  0u
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_FBL, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, intervening_flag_write)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);
   fs_reg zero(0.0f);
   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), src2, zero, BRW_CONDITIONAL_GE);
   bld.CMP(bld.null_reg_f(), dest, zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)        dest  src0  src1
    * 1: cmp.ge.f0(8)  null  src2  0.0f
    * 2: cmp.ge.f0(8)  null  dest  0.0f
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 1)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 2)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 2)->conditional_mod);
}

TEST_F(cmod_propagation_test, intervening_flag_read)
{
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::float_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);
   fs_reg zero(0.0f);
   bld.ADD(dest0, src0, src1);
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero));
   bld.CMP(bld.null_reg_f(), dest0, zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)        dest0 src0  src1
    * 1: (+f0) sel(8)  dest1 src2  0.0f
    * 2: cmp.ge.f0(8)  null  dest0 0.0f
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_OPCODE_SEL, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_PREDICATE_NORMAL, instruction(block0, 1)->predicate);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 2)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 2)->conditional_mod);
}

TEST_F(cmod_propagation_test, intervening_dest_write)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::vec4_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg src2 = v->vgrf(glsl_type::vec2_type);
   fs_reg zero(0.0f);
   bld.ADD(offset(dest, bld, 2), src0, src1);
   bld.emit(SHADER_OPCODE_TEX, dest, src2)
      ->regs_written = 4;
   bld.CMP(bld.null_reg_f(), offset(dest, bld, 2), zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)        dest+2  src0    src1
    * 1: tex(8) rlen 4 dest+0  src2
    * 2: cmp.ge.f0(8)  null    dest+2  0.0f
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(SHADER_OPCODE_TEX, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 2)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 2)->conditional_mod);
}

TEST_F(cmod_propagation_test, intervening_flag_read_same_value)
{
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::float_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);
   fs_reg zero(0.0f);
   set_condmod(BRW_CONDITIONAL_GE, bld.ADD(dest0, src0, src1));
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero));
   bld.CMP(bld.null_reg_f(), dest0, zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add.ge.f0(8)  dest0 src0  src1
    * 1: (+f0) sel(8)  dest1 src2  0.0f
    * 2: cmp.ge.f0(8)  null  dest0 0.0f
    *
    * = After =
    * 0: add.ge.f0(8)  dest0 src0  src1
    * 1: (+f0) sel(8)  dest1 src2  0.0f
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_SEL, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_PREDICATE_NORMAL, instruction(block0, 1)->predicate);
}

TEST_F(cmod_propagation_test, negate)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg zero(0.0f);
   bld.ADD(dest, src0, src1);
   dest.negate = true;
   bld.CMP(bld.null_reg_f(), dest, zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)        dest  src0  src1
    * 1: cmp.ge.f0(8)  null  -dest 0.0f
    *
    * = After =
    * 0: add.le.f0(8)  dest  src0  src1
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_LE, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, movnz)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   bld.CMP(dest, src0, src1, BRW_CONDITIONAL_GE);
   set_condmod(BRW_CONDITIONAL_NZ,
               bld.MOV(bld.null_reg_f(), dest));

   /* = Before =
    *
    * 0: cmp.ge.f0(8)  dest  src0  src1
    * 1: mov.nz.f0(8)  null  dest
    *
    * = After =
    * 0: cmp.ge.f0(8)  dest  src0  src1
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, different_types_cmod_with_zero)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::int_type);
   fs_reg src0 = v->vgrf(glsl_type::int_type);
   fs_reg src1 = v->vgrf(glsl_type::int_type);
   fs_reg zero(0.0f);
   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), retype(dest, BRW_REGISTER_TYPE_F), zero,
           BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)        dest:D  src0:D  src1:D
    * 1: cmp.ge.f0(8)  null:F  dest:F  0.0f
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, andnz_one)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::int_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg zero(0.0f);
   fs_reg one(1);

   bld.CMP(retype(dest, BRW_REGISTER_TYPE_F), src0, zero, BRW_CONDITIONAL_L);
   set_condmod(BRW_CONDITIONAL_NZ,
               bld.AND(bld.null_reg_d(), dest, one));

   /* = Before =
    * 0: cmp.l.f0(8)     dest:F  src0:F  0F
    * 1: and.nz.f0(8)    null:D  dest:D  1D
    *
    * = After =
    * 0: cmp.l.f0(8)     dest:F  src0:F  0F
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
   EXPECT_TRUE(retype(dest, BRW_REGISTER_TYPE_F)
               .equals(instruction(block0, 0)->dst));
}

TEST_F(cmod_propagation_test, andnz_non_one)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::int_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg zero(0.0f);
   fs_reg nonone(38);

   bld.CMP(retype(dest, BRW_REGISTER_TYPE_F), src0, zero, BRW_CONDITIONAL_L);
   set_condmod(BRW_CONDITIONAL_NZ,
               bld.AND(bld.null_reg_d(), dest, nonone));

   /* = Before =
    * 0: cmp.l.f0(8)     dest:F  src0:F  0F
    * 1: and.nz.f0(8)    null:D  dest:D  38D
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_AND, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, andz_one)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::int_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg zero(0.0f);
   fs_reg one(1);

   bld.CMP(retype(dest, BRW_REGISTER_TYPE_F), src0, zero, BRW_CONDITIONAL_L);
   set_condmod(BRW_CONDITIONAL_Z,
               bld.AND(bld.null_reg_d(), dest, one));

   /* = Before =
    * 0: cmp.l.f0(8)     dest:F  src0:F  0F
    * 1: and.z.f0(8)     null:D  dest:D  1D
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_AND, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_EQ, instruction(block0, 1)->conditional_mod);
}
