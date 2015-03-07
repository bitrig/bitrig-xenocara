/*
 * Copyright © 2010 Intel Corporation
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

/** @file brw_fs_visitor.cpp
 *
 * This file supports generating the FS LIR from the GLSL IR.  The LIR
 * makes it easier to do backend-specific optimizations than doing so
 * in the GLSL IR or in the native code.
 */
extern "C" {

#include <sys/types.h>

#include "main/macros.h"
#include "main/shaderobj.h"
#include "program/prog_parameter.h"
#include "program/prog_print.h"
#include "program/prog_optimize.h"
#include "program/register_allocate.h"
#include "program/sampler.h"
#include "program/hash_table.h"
#include "brw_context.h"
#include "brw_eu.h"
#include "brw_wm.h"
}
#include "brw_fs.h"
#include "main/uniforms.h"
#include "glsl/glsl_types.h"
#include "glsl/ir_optimization.h"

void
fs_visitor::visit(ir_variable *ir)
{
   fs_reg *reg = NULL;

   if (variable_storage(ir))
      return;

   if (ir->data.mode == ir_var_shader_in) {
      if (!strcmp(ir->name, "gl_FragCoord")) {
	 reg = emit_fragcoord_interpolation(ir);
      } else if (!strcmp(ir->name, "gl_FrontFacing")) {
	 reg = emit_frontfacing_interpolation(ir);
      } else {
	 reg = emit_general_interpolation(ir);
      }
      assert(reg);
      hash_table_insert(this->variable_ht, reg, ir);
      return;
   } else if (ir->data.mode == ir_var_shader_out) {
      reg = new(this->mem_ctx) fs_reg(this, ir->type);

      if (ir->data.index > 0) {
	 assert(ir->data.location == FRAG_RESULT_DATA0);
	 assert(ir->data.index == 1);
	 this->dual_src_output = *reg;
         this->do_dual_src = true;
      } else if (ir->data.location == FRAG_RESULT_COLOR) {
	 /* Writing gl_FragColor outputs to all color regions. */
	 for (unsigned int i = 0; i < MAX2(c->key.nr_color_regions, 1); i++) {
	    this->outputs[i] = *reg;
	    this->output_components[i] = 4;
	 }
      } else if (ir->data.location == FRAG_RESULT_DEPTH) {
	 this->frag_depth = *reg;
      } else if (ir->data.location == FRAG_RESULT_SAMPLE_MASK) {
         this->sample_mask = *reg;
      } else {
	 /* gl_FragData or a user-defined FS output */
	 assert(ir->data.location >= FRAG_RESULT_DATA0 &&
		ir->data.location < FRAG_RESULT_DATA0 + BRW_MAX_DRAW_BUFFERS);

	 int vector_elements =
	    ir->type->is_array() ? ir->type->fields.array->vector_elements
				 : ir->type->vector_elements;

	 /* General color output. */
	 for (unsigned int i = 0; i < MAX2(1, ir->type->length); i++) {
	    int output = ir->data.location - FRAG_RESULT_DATA0 + i;
	    this->outputs[output] = *reg;
	    this->outputs[output].reg_offset += vector_elements * i;
	    this->output_components[output] = vector_elements;
	 }
      }
   } else if (ir->data.mode == ir_var_uniform) {
      int param_index = uniforms;

      /* Thanks to the lower_ubo_reference pass, we will see only
       * ir_binop_ubo_load expressions and not ir_dereference_variable for UBO
       * variables, so no need for them to be in variable_ht.
       *
       * Atomic counters take no uniform storage, no need to do
       * anything here.
       */
      if (ir->is_in_uniform_block() || ir->type->contains_atomic())
         return;

      if (dispatch_width == 16) {
	 if (!variable_storage(ir)) {
	    fail("Failed to find uniform '%s' in SIMD16\n", ir->name);
	 }
	 return;
      }

      param_size[param_index] = type_size(ir->type);
      if (!strncmp(ir->name, "gl_", 3)) {
	 setup_builtin_uniform_values(ir);
      } else {
	 setup_uniform_values(ir);
      }

      reg = new(this->mem_ctx) fs_reg(UNIFORM, param_index);
      reg->type = brw_type_for_base_type(ir->type);

   } else if (ir->data.mode == ir_var_system_value) {
      if (ir->data.location == SYSTEM_VALUE_SAMPLE_POS) {
	 reg = emit_samplepos_setup(ir);
      } else if (ir->data.location == SYSTEM_VALUE_SAMPLE_ID) {
	 reg = emit_sampleid_setup(ir);
      } else if (ir->data.location == SYSTEM_VALUE_SAMPLE_MASK_IN) {
         reg = emit_samplemaskin_setup(ir);
      }
   }

   if (!reg)
      reg = new(this->mem_ctx) fs_reg(this, ir->type);

   hash_table_insert(this->variable_ht, reg, ir);
}

void
fs_visitor::visit(ir_dereference_variable *ir)
{
   fs_reg *reg = variable_storage(ir->var);
   this->result = *reg;
}

void
fs_visitor::visit(ir_dereference_record *ir)
{
   const glsl_type *struct_type = ir->record->type;

   ir->record->accept(this);

   unsigned int offset = 0;
   for (unsigned int i = 0; i < struct_type->length; i++) {
      if (strcmp(struct_type->fields.structure[i].name, ir->field) == 0)
	 break;
      offset += type_size(struct_type->fields.structure[i].type);
   }
   this->result.reg_offset += offset;
   this->result.type = brw_type_for_base_type(ir->type);
}

void
fs_visitor::visit(ir_dereference_array *ir)
{
   ir_constant *constant_index;
   fs_reg src;
   int element_size = type_size(ir->type);

   constant_index = ir->array_index->as_constant();

   ir->array->accept(this);
   src = this->result;
   src.type = brw_type_for_base_type(ir->type);

   if (constant_index) {
      assert(src.file == UNIFORM || src.file == GRF);
      src.reg_offset += constant_index->value.i[0] * element_size;
   } else {
      /* Variable index array dereference.  We attach the variable index
       * component to the reg as a pointer to a register containing the
       * offset.  Currently only uniform arrays are supported in this patch,
       * and that reladdr pointer is resolved by
       * move_uniform_array_access_to_pull_constants().  All other array types
       * are lowered by lower_variable_index_to_cond_assign().
       */
      ir->array_index->accept(this);

      fs_reg index_reg;
      index_reg = fs_reg(this, glsl_type::int_type);
      emit(BRW_OPCODE_MUL, index_reg, this->result, fs_reg(element_size));

      if (src.reladdr) {
         emit(BRW_OPCODE_ADD, index_reg, *src.reladdr, index_reg);
      }

      src.reladdr = ralloc(mem_ctx, fs_reg);
      memcpy(src.reladdr, &index_reg, sizeof(index_reg));
   }
   this->result = src;
}

void
fs_visitor::emit_lrp(const fs_reg &dst, const fs_reg &x, const fs_reg &y,
                     const fs_reg &a)
{
   if (brw->gen < 6 ||
       !x.is_valid_3src() ||
       !y.is_valid_3src() ||
       !a.is_valid_3src()) {
      /* We can't use the LRP instruction.  Emit x*(1-a) + y*a. */
      fs_reg y_times_a           = fs_reg(this, glsl_type::float_type);
      fs_reg one_minus_a         = fs_reg(this, glsl_type::float_type);
      fs_reg x_times_one_minus_a = fs_reg(this, glsl_type::float_type);

      emit(MUL(y_times_a, y, a));

      fs_reg negative_a = a;
      negative_a.negate = !a.negate;
      emit(ADD(one_minus_a, negative_a, fs_reg(1.0f)));
      emit(MUL(x_times_one_minus_a, x, one_minus_a));

      emit(ADD(dst, x_times_one_minus_a, y_times_a));
   } else {
      /* The LRP instruction actually does op1 * op0 + op2 * (1 - op0), so
       * we need to reorder the operands.
       */
      emit(LRP(dst, a, y, x));
   }
}

void
fs_visitor::emit_minmax(uint32_t conditionalmod, const fs_reg &dst,
                        const fs_reg &src0, const fs_reg &src1)
{
   fs_inst *inst;

   if (brw->gen >= 6) {
      inst = emit(BRW_OPCODE_SEL, dst, src0, src1);
      inst->conditional_mod = conditionalmod;
   } else {
      emit(CMP(reg_null_d, src0, src1, conditionalmod));

      inst = emit(BRW_OPCODE_SEL, dst, src0, src1);
      inst->predicate = BRW_PREDICATE_NORMAL;
   }
}

/* Instruction selection: Produce a MOV.sat instead of
 * MIN(MAX(val, 0), 1) when possible.
 */
bool
fs_visitor::try_emit_saturate(ir_expression *ir)
{
   ir_rvalue *sat_val = ir->as_rvalue_to_saturate();

   if (!sat_val)
      return false;

   fs_inst *pre_inst = (fs_inst *) this->instructions.get_tail();

   sat_val->accept(this);
   fs_reg src = this->result;

   fs_inst *last_inst = (fs_inst *) this->instructions.get_tail();

   /* If the last instruction from our accept() didn't generate our
    * src, generate a saturated MOV
    */
   fs_inst *modify = get_instruction_generating_reg(pre_inst, last_inst, src);
   if (!modify || modify->regs_written != 1) {
      this->result = fs_reg(this, ir->type);
      fs_inst *inst = emit(MOV(this->result, src));
      inst->saturate = true;
   } else {
      modify->saturate = true;
      this->result = src;
   }


   return true;
}

bool
fs_visitor::try_emit_mad(ir_expression *ir)
{
   /* 3-src instructions were introduced in gen6. */
   if (brw->gen < 6)
      return false;

   /* MAD can only handle floating-point data. */
   if (ir->type != glsl_type::float_type)
      return false;

   ir_rvalue *nonmul = ir->operands[1];
   ir_expression *mul = ir->operands[0]->as_expression();

   if (!mul || mul->operation != ir_binop_mul) {
      nonmul = ir->operands[0];
      mul = ir->operands[1]->as_expression();

      if (!mul || mul->operation != ir_binop_mul)
         return false;
   }

   if (nonmul->as_constant() ||
       mul->operands[0]->as_constant() ||
       mul->operands[1]->as_constant())
      return false;

   nonmul->accept(this);
   fs_reg src0 = this->result;

   mul->operands[0]->accept(this);
   fs_reg src1 = this->result;

   mul->operands[1]->accept(this);
   fs_reg src2 = this->result;

   this->result = fs_reg(this, ir->type);
   emit(BRW_OPCODE_MAD, this->result, src0, src1, src2);

   return true;
}

void
fs_visitor::visit(ir_expression *ir)
{
   unsigned int operand;
   fs_reg op[3], temp;
   fs_inst *inst;

   assert(ir->get_num_operands() <= 3);

   if (try_emit_saturate(ir))
      return;
   if (ir->operation == ir_binop_add) {
      if (try_emit_mad(ir))
	 return;
   }

   for (operand = 0; operand < ir->get_num_operands(); operand++) {
      ir->operands[operand]->accept(this);
      if (this->result.file == BAD_FILE) {
	 fail("Failed to get tree for expression operand:\n");
	 ir->operands[operand]->fprint(stderr);
         fprintf(stderr, "\n");
      }
      assert(this->result.is_valid_3src());
      op[operand] = this->result;

      /* Matrix expression operands should have been broken down to vector
       * operations already.
       */
      assert(!ir->operands[operand]->type->is_matrix());
      /* And then those vector operands should have been broken down to scalar.
       */
      assert(!ir->operands[operand]->type->is_vector());
   }

   /* Storage for our result.  If our result goes into an assignment, it will
    * just get copy-propagated out, so no worries.
    */
   this->result = fs_reg(this, ir->type);

   switch (ir->operation) {
   case ir_unop_logic_not:
      /* Note that BRW_OPCODE_NOT is not appropriate here, since it is
       * ones complement of the whole register, not just bit 0.
       */
      emit(XOR(this->result, op[0], fs_reg(1)));
      break;
   case ir_unop_neg:
      op[0].negate = !op[0].negate;
      emit(MOV(this->result, op[0]));
      break;
   case ir_unop_abs:
      op[0].abs = true;
      op[0].negate = false;
      emit(MOV(this->result, op[0]));
      break;
   case ir_unop_sign:
      if (ir->type->is_float()) {
         /* AND(val, 0x80000000) gives the sign bit.
          *
          * Predicated OR ORs 1.0 (0x3f800000) with the sign bit if val is not
          * zero.
          */
         emit(CMP(reg_null_f, op[0], fs_reg(0.0f), BRW_CONDITIONAL_NZ));

         op[0].type = BRW_REGISTER_TYPE_UD;
         this->result.type = BRW_REGISTER_TYPE_UD;
         emit(AND(this->result, op[0], fs_reg(0x80000000u)));

         inst = emit(OR(this->result, this->result, fs_reg(0x3f800000u)));
         inst->predicate = BRW_PREDICATE_NORMAL;

         this->result.type = BRW_REGISTER_TYPE_F;
      } else {
         /*  ASR(val, 31) -> negative val generates 0xffffffff (signed -1).
          *               -> non-negative val generates 0x00000000.
          *  Predicated OR sets 1 if val is positive.
          */
         emit(CMP(reg_null_d, op[0], fs_reg(0), BRW_CONDITIONAL_G));

         emit(ASR(this->result, op[0], fs_reg(31)));

         inst = emit(OR(this->result, this->result, fs_reg(1)));
         inst->predicate = BRW_PREDICATE_NORMAL;
      }
      break;
   case ir_unop_rcp:
      emit_math(SHADER_OPCODE_RCP, this->result, op[0]);
      break;

   case ir_unop_exp2:
      emit_math(SHADER_OPCODE_EXP2, this->result, op[0]);
      break;
   case ir_unop_log2:
      emit_math(SHADER_OPCODE_LOG2, this->result, op[0]);
      break;
   case ir_unop_exp:
   case ir_unop_log:
      assert(!"not reached: should be handled by ir_explog_to_explog2");
      break;
   case ir_unop_sin:
   case ir_unop_sin_reduced:
      emit_math(SHADER_OPCODE_SIN, this->result, op[0]);
      break;
   case ir_unop_cos:
   case ir_unop_cos_reduced:
      emit_math(SHADER_OPCODE_COS, this->result, op[0]);
      break;

   case ir_unop_dFdx:
      emit(FS_OPCODE_DDX, this->result, op[0]);
      break;
   case ir_unop_dFdy:
      emit(FS_OPCODE_DDY, this->result, op[0]);
      break;

   case ir_binop_add:
      emit(ADD(this->result, op[0], op[1]));
      break;
   case ir_binop_sub:
      assert(!"not reached: should be handled by ir_sub_to_add_neg");
      break;

   case ir_binop_mul:
      if (brw->gen < 8 && ir->type->is_integer()) {
	 /* For integer multiplication, the MUL uses the low 16 bits
	  * of one of the operands (src0 on gen6, src1 on gen7).  The
	  * MACH accumulates in the contribution of the upper 16 bits
	  * of that operand.
          */
         if (ir->operands[0]->is_uint16_constant()) {
            if (brw->gen < 7)
               emit(MUL(this->result, op[0], op[1]));
            else
               emit(MUL(this->result, op[1], op[0]));
         } else if (ir->operands[1]->is_uint16_constant()) {
            if (brw->gen < 7)
               emit(MUL(this->result, op[1], op[0]));
            else
               emit(MUL(this->result, op[0], op[1]));
         } else {
            if (brw->gen >= 7)
               no16("SIMD16 explicit accumulator operands unsupported\n");

            struct brw_reg acc = retype(brw_acc_reg(), this->result.type);

            emit(MUL(acc, op[0], op[1]));
            emit(MACH(reg_null_d, op[0], op[1]));
            emit(MOV(this->result, fs_reg(acc)));
         }
      } else {
	 emit(MUL(this->result, op[0], op[1]));
      }
      break;
   case ir_binop_imul_high: {
      if (brw->gen >= 7)
         no16("SIMD16 explicit accumulator operands unsupported\n");

      struct brw_reg acc = retype(brw_acc_reg(), this->result.type);

      emit(MUL(acc, op[0], op[1]));
      emit(MACH(this->result, op[0], op[1]));
      break;
   }
   case ir_binop_div:
      /* Floating point should be lowered by DIV_TO_MUL_RCP in the compiler. */
      assert(ir->type->is_integer());
      emit_math(SHADER_OPCODE_INT_QUOTIENT, this->result, op[0], op[1]);
      break;
   case ir_binop_carry: {
      if (brw->gen >= 7)
         no16("SIMD16 explicit accumulator operands unsupported\n");

      struct brw_reg acc = retype(brw_acc_reg(), BRW_REGISTER_TYPE_UD);

      emit(ADDC(reg_null_ud, op[0], op[1]));
      emit(MOV(this->result, fs_reg(acc)));
      break;
   }
   case ir_binop_borrow: {
      if (brw->gen >= 7)
         no16("SIMD16 explicit accumulator operands unsupported\n");

      struct brw_reg acc = retype(brw_acc_reg(), BRW_REGISTER_TYPE_UD);

      emit(SUBB(reg_null_ud, op[0], op[1]));
      emit(MOV(this->result, fs_reg(acc)));
      break;
   }
   case ir_binop_mod:
      /* Floating point should be lowered by MOD_TO_FRACT in the compiler. */
      assert(ir->type->is_integer());
      emit_math(SHADER_OPCODE_INT_REMAINDER, this->result, op[0], op[1]);
      break;

   case ir_binop_less:
   case ir_binop_greater:
   case ir_binop_lequal:
   case ir_binop_gequal:
   case ir_binop_equal:
   case ir_binop_all_equal:
   case ir_binop_nequal:
   case ir_binop_any_nequal:
      resolve_bool_comparison(ir->operands[0], &op[0]);
      resolve_bool_comparison(ir->operands[1], &op[1]);

      emit(CMP(this->result, op[0], op[1],
               brw_conditional_for_comparison(ir->operation)));
      break;

   case ir_binop_logic_xor:
      emit(XOR(this->result, op[0], op[1]));
      break;

   case ir_binop_logic_or:
      emit(OR(this->result, op[0], op[1]));
      break;

   case ir_binop_logic_and:
      emit(AND(this->result, op[0], op[1]));
      break;

   case ir_binop_dot:
   case ir_unop_any:
      assert(!"not reached: should be handled by brw_fs_channel_expressions");
      break;

   case ir_unop_noise:
      assert(!"not reached: should be handled by lower_noise");
      break;

   case ir_quadop_vector:
      assert(!"not reached: should be handled by lower_quadop_vector");
      break;

   case ir_binop_vector_extract:
      assert(!"not reached: should be handled by lower_vec_index_to_cond_assign()");
      break;

   case ir_triop_vector_insert:
      assert(!"not reached: should be handled by lower_vector_insert()");
      break;

   case ir_binop_ldexp:
      assert(!"not reached: should be handled by ldexp_to_arith()");
      break;

   case ir_unop_sqrt:
      emit_math(SHADER_OPCODE_SQRT, this->result, op[0]);
      break;

   case ir_unop_rsq:
      emit_math(SHADER_OPCODE_RSQ, this->result, op[0]);
      break;

   case ir_unop_bitcast_i2f:
   case ir_unop_bitcast_u2f:
      op[0].type = BRW_REGISTER_TYPE_F;
      this->result = op[0];
      break;
   case ir_unop_i2u:
   case ir_unop_bitcast_f2u:
      op[0].type = BRW_REGISTER_TYPE_UD;
      this->result = op[0];
      break;
   case ir_unop_u2i:
   case ir_unop_bitcast_f2i:
      op[0].type = BRW_REGISTER_TYPE_D;
      this->result = op[0];
      break;
   case ir_unop_i2f:
   case ir_unop_u2f:
   case ir_unop_f2i:
   case ir_unop_f2u:
      emit(MOV(this->result, op[0]));
      break;

   case ir_unop_b2i:
      emit(AND(this->result, op[0], fs_reg(1)));
      break;
   case ir_unop_b2f:
      temp = fs_reg(this, glsl_type::int_type);
      emit(AND(temp, op[0], fs_reg(1)));
      emit(MOV(this->result, temp));
      break;

   case ir_unop_f2b:
      emit(CMP(this->result, op[0], fs_reg(0.0f), BRW_CONDITIONAL_NZ));
      break;
   case ir_unop_i2b:
      emit(CMP(this->result, op[0], fs_reg(0), BRW_CONDITIONAL_NZ));
      break;

   case ir_unop_trunc:
      emit(RNDZ(this->result, op[0]));
      break;
   case ir_unop_ceil:
      op[0].negate = !op[0].negate;
      emit(RNDD(this->result, op[0]));
      this->result.negate = true;
      break;
   case ir_unop_floor:
      emit(RNDD(this->result, op[0]));
      break;
   case ir_unop_fract:
      emit(FRC(this->result, op[0]));
      break;
   case ir_unop_round_even:
      emit(RNDE(this->result, op[0]));
      break;

   case ir_binop_min:
   case ir_binop_max:
      resolve_ud_negate(&op[0]);
      resolve_ud_negate(&op[1]);
      emit_minmax(ir->operation == ir_binop_min ?
                  BRW_CONDITIONAL_L : BRW_CONDITIONAL_GE,
                  this->result, op[0], op[1]);
      break;
   case ir_unop_pack_snorm_2x16:
   case ir_unop_pack_snorm_4x8:
   case ir_unop_pack_unorm_2x16:
   case ir_unop_pack_unorm_4x8:
   case ir_unop_unpack_snorm_2x16:
   case ir_unop_unpack_snorm_4x8:
   case ir_unop_unpack_unorm_2x16:
   case ir_unop_unpack_unorm_4x8:
   case ir_unop_unpack_half_2x16:
   case ir_unop_pack_half_2x16:
      assert(!"not reached: should be handled by lower_packing_builtins");
      break;
   case ir_unop_unpack_half_2x16_split_x:
      emit(FS_OPCODE_UNPACK_HALF_2x16_SPLIT_X, this->result, op[0]);
      break;
   case ir_unop_unpack_half_2x16_split_y:
      emit(FS_OPCODE_UNPACK_HALF_2x16_SPLIT_Y, this->result, op[0]);
      break;
   case ir_binop_pow:
      emit_math(SHADER_OPCODE_POW, this->result, op[0], op[1]);
      break;

   case ir_unop_bitfield_reverse:
      emit(BFREV(this->result, op[0]));
      break;
   case ir_unop_bit_count:
      emit(CBIT(this->result, op[0]));
      break;
   case ir_unop_find_msb:
      temp = fs_reg(this, glsl_type::uint_type);
      emit(FBH(temp, op[0]));

      /* FBH counts from the MSB side, while GLSL's findMSB() wants the count
       * from the LSB side. If FBH didn't return an error (0xFFFFFFFF), then
       * subtract the result from 31 to convert the MSB count into an LSB count.
       */

      /* FBH only supports UD type for dst, so use a MOV to convert UD to D. */
      emit(MOV(this->result, temp));
      emit(CMP(reg_null_d, this->result, fs_reg(-1), BRW_CONDITIONAL_NZ));

      temp.negate = true;
      inst = emit(ADD(this->result, temp, fs_reg(31)));
      inst->predicate = BRW_PREDICATE_NORMAL;
      break;
   case ir_unop_find_lsb:
      emit(FBL(this->result, op[0]));
      break;
   case ir_triop_bitfield_extract:
      /* Note that the instruction's argument order is reversed from GLSL
       * and the IR.
       */
      emit(BFE(this->result, op[2], op[1], op[0]));
      break;
   case ir_binop_bfm:
      emit(BFI1(this->result, op[0], op[1]));
      break;
   case ir_triop_bfi:
      emit(BFI2(this->result, op[0], op[1], op[2]));
      break;
   case ir_quadop_bitfield_insert:
      assert(!"not reached: should be handled by "
              "lower_instructions::bitfield_insert_to_bfm_bfi");
      break;

   case ir_unop_bit_not:
      emit(NOT(this->result, op[0]));
      break;
   case ir_binop_bit_and:
      emit(AND(this->result, op[0], op[1]));
      break;
   case ir_binop_bit_xor:
      emit(XOR(this->result, op[0], op[1]));
      break;
   case ir_binop_bit_or:
      emit(OR(this->result, op[0], op[1]));
      break;

   case ir_binop_lshift:
      emit(SHL(this->result, op[0], op[1]));
      break;

   case ir_binop_rshift:
      if (ir->type->base_type == GLSL_TYPE_INT)
	 emit(ASR(this->result, op[0], op[1]));
      else
	 emit(SHR(this->result, op[0], op[1]));
      break;
   case ir_binop_pack_half_2x16_split:
      emit(FS_OPCODE_PACK_HALF_2x16_SPLIT, this->result, op[0], op[1]);
      break;
   case ir_binop_ubo_load: {
      /* This IR node takes a constant uniform block and a constant or
       * variable byte offset within the block and loads a vector from that.
       */
      ir_constant *uniform_block = ir->operands[0]->as_constant();
      ir_constant *const_offset = ir->operands[1]->as_constant();
      fs_reg surf_index = fs_reg(c->prog_data.base.binding_table.ubo_start +
                                 uniform_block->value.u[0]);
      if (const_offset) {
         fs_reg packed_consts = fs_reg(this, glsl_type::float_type);
         packed_consts.type = result.type;

         fs_reg const_offset_reg = fs_reg(const_offset->value.u[0] & ~15);
         emit(new(mem_ctx) fs_inst(FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD,
                                   packed_consts, surf_index, const_offset_reg));

         for (int i = 0; i < ir->type->vector_elements; i++) {
            packed_consts.set_smear(const_offset->value.u[0] % 16 / 4 + i);

            /* The std140 packing rules don't allow vectors to cross 16-byte
             * boundaries, and a reg is 32 bytes.
             */
            assert(packed_consts.subreg_offset < 32);

            /* UBO bools are any nonzero value.  We consider bools to be
             * values with the low bit set to 1.  Convert them using CMP.
             */
            if (ir->type->base_type == GLSL_TYPE_BOOL) {
               emit(CMP(result, packed_consts, fs_reg(0u), BRW_CONDITIONAL_NZ));
            } else {
               emit(MOV(result, packed_consts));
            }

            result.reg_offset++;
         }
      } else {
         /* Turn the byte offset into a dword offset. */
         fs_reg base_offset = fs_reg(this, glsl_type::int_type);
         emit(SHR(base_offset, op[1], fs_reg(2)));

         for (int i = 0; i < ir->type->vector_elements; i++) {
            emit(VARYING_PULL_CONSTANT_LOAD(result, surf_index,
                                            base_offset, i));

            if (ir->type->base_type == GLSL_TYPE_BOOL)
               emit(CMP(result, result, fs_reg(0), BRW_CONDITIONAL_NZ));

            result.reg_offset++;
         }
      }

      result.reg_offset = 0;
      break;
   }

   case ir_triop_fma:
      /* Note that the instruction's argument order is reversed from GLSL
       * and the IR.
       */
      emit(MAD(this->result, op[2], op[1], op[0]));
      break;

   case ir_triop_lrp:
      emit_lrp(this->result, op[0], op[1], op[2]);
      break;

   case ir_triop_csel:
      emit(CMP(reg_null_d, op[0], fs_reg(0), BRW_CONDITIONAL_NZ));
      inst = emit(BRW_OPCODE_SEL, this->result, op[1], op[2]);
      inst->predicate = BRW_PREDICATE_NORMAL;
      break;
   }
}

void
fs_visitor::emit_assignment_writes(fs_reg &l, fs_reg &r,
				   const glsl_type *type, bool predicated)
{
   switch (type->base_type) {
   case GLSL_TYPE_FLOAT:
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_INT:
   case GLSL_TYPE_BOOL:
      for (unsigned int i = 0; i < type->components(); i++) {
	 l.type = brw_type_for_base_type(type);
	 r.type = brw_type_for_base_type(type);

	 if (predicated || !l.equals(r)) {
	    fs_inst *inst = emit(MOV(l, r));
	    inst->predicate = predicated ? BRW_PREDICATE_NORMAL : BRW_PREDICATE_NONE;
	 }

	 l.reg_offset++;
	 r.reg_offset++;
      }
      break;
   case GLSL_TYPE_ARRAY:
      for (unsigned int i = 0; i < type->length; i++) {
	 emit_assignment_writes(l, r, type->fields.array, predicated);
      }
      break;

   case GLSL_TYPE_STRUCT:
      for (unsigned int i = 0; i < type->length; i++) {
	 emit_assignment_writes(l, r, type->fields.structure[i].type,
				predicated);
      }
      break;

   case GLSL_TYPE_SAMPLER:
   case GLSL_TYPE_IMAGE:
   case GLSL_TYPE_ATOMIC_UINT:
      break;

   case GLSL_TYPE_VOID:
   case GLSL_TYPE_ERROR:
   case GLSL_TYPE_INTERFACE:
      assert(!"not reached");
      break;
   }
}

/* If the RHS processing resulted in an instruction generating a
 * temporary value, and it would be easy to rewrite the instruction to
 * generate its result right into the LHS instead, do so.  This ends
 * up reliably removing instructions where it can be tricky to do so
 * later without real UD chain information.
 */
bool
fs_visitor::try_rewrite_rhs_to_dst(ir_assignment *ir,
                                   fs_reg dst,
                                   fs_reg src,
                                   fs_inst *pre_rhs_inst,
                                   fs_inst *last_rhs_inst)
{
   /* Only attempt if we're doing a direct assignment. */
   if (ir->condition ||
       !(ir->lhs->type->is_scalar() ||
        (ir->lhs->type->is_vector() &&
         ir->write_mask == (1 << ir->lhs->type->vector_elements) - 1)))
      return false;

   /* Make sure the last instruction generated our source reg. */
   fs_inst *modify = get_instruction_generating_reg(pre_rhs_inst,
						    last_rhs_inst,
						    src);
   if (!modify)
      return false;

   /* If last_rhs_inst wrote a different number of components than our LHS,
    * we can't safely rewrite it.
    */
   if (virtual_grf_sizes[dst.reg] != modify->regs_written)
      return false;

   /* Success!  Rewrite the instruction. */
   modify->dst = dst;

   return true;
}

void
fs_visitor::visit(ir_assignment *ir)
{
   fs_reg l, r;
   fs_inst *inst;

   /* FINISHME: arrays on the lhs */
   ir->lhs->accept(this);
   l = this->result;

   fs_inst *pre_rhs_inst = (fs_inst *) this->instructions.get_tail();

   ir->rhs->accept(this);
   r = this->result;

   fs_inst *last_rhs_inst = (fs_inst *) this->instructions.get_tail();

   assert(l.file != BAD_FILE);
   assert(r.file != BAD_FILE);

   if (try_rewrite_rhs_to_dst(ir, l, r, pre_rhs_inst, last_rhs_inst))
      return;

   if (ir->condition) {
      emit_bool_to_cond_code(ir->condition);
   }

   if (ir->lhs->type->is_scalar() ||
       ir->lhs->type->is_vector()) {
      for (int i = 0; i < ir->lhs->type->vector_elements; i++) {
	 if (ir->write_mask & (1 << i)) {
	    inst = emit(MOV(l, r));
	    if (ir->condition)
	       inst->predicate = BRW_PREDICATE_NORMAL;
	    r.reg_offset++;
	 }
	 l.reg_offset++;
      }
   } else {
      emit_assignment_writes(l, r, ir->lhs->type, ir->condition != NULL);
   }
}

fs_inst *
fs_visitor::emit_texture_gen4(ir_texture *ir, fs_reg dst, fs_reg coordinate,
			      fs_reg shadow_c, fs_reg lod, fs_reg dPdy)
{
   int mlen;
   int base_mrf = 1;
   bool simd16 = false;
   fs_reg orig_dst;

   /* g0 header. */
   mlen = 1;

   if (ir->shadow_comparitor) {
      for (int i = 0; i < ir->coordinate->type->vector_elements; i++) {
	 emit(MOV(fs_reg(MRF, base_mrf + mlen + i), coordinate));
	 coordinate.reg_offset++;
      }

      /* gen4's SIMD8 sampler always has the slots for u,v,r present.
       * the unused slots must be zeroed.
       */
      for (int i = ir->coordinate->type->vector_elements; i < 3; i++) {
         emit(MOV(fs_reg(MRF, base_mrf + mlen + i), fs_reg(0.0f)));
      }
      mlen += 3;

      if (ir->op == ir_tex) {
	 /* There's no plain shadow compare message, so we use shadow
	  * compare with a bias of 0.0.
	  */
	 emit(MOV(fs_reg(MRF, base_mrf + mlen), fs_reg(0.0f)));
	 mlen++;
      } else if (ir->op == ir_txb || ir->op == ir_txl) {
	 emit(MOV(fs_reg(MRF, base_mrf + mlen), lod));
	 mlen++;
      } else {
         assert(!"Should not get here.");
      }

      emit(MOV(fs_reg(MRF, base_mrf + mlen), shadow_c));
      mlen++;
   } else if (ir->op == ir_tex) {
      for (int i = 0; i < ir->coordinate->type->vector_elements; i++) {
	 emit(MOV(fs_reg(MRF, base_mrf + mlen + i), coordinate));
	 coordinate.reg_offset++;
      }
      /* zero the others. */
      for (int i = ir->coordinate->type->vector_elements; i<3; i++) {
         emit(MOV(fs_reg(MRF, base_mrf + mlen + i), fs_reg(0.0f)));
      }
      /* gen4's SIMD8 sampler always has the slots for u,v,r present. */
      mlen += 3;
   } else if (ir->op == ir_txd) {
      fs_reg &dPdx = lod;

      for (int i = 0; i < ir->coordinate->type->vector_elements; i++) {
	 emit(MOV(fs_reg(MRF, base_mrf + mlen + i), coordinate));
	 coordinate.reg_offset++;
      }
      /* the slots for u and v are always present, but r is optional */
      mlen += MAX2(ir->coordinate->type->vector_elements, 2);

      /*  P   = u, v, r
       * dPdx = dudx, dvdx, drdx
       * dPdy = dudy, dvdy, drdy
       *
       * 1-arg: Does not exist.
       *
       * 2-arg: dudx   dvdx   dudy   dvdy
       *        dPdx.x dPdx.y dPdy.x dPdy.y
       *        m4     m5     m6     m7
       *
       * 3-arg: dudx   dvdx   drdx   dudy   dvdy   drdy
       *        dPdx.x dPdx.y dPdx.z dPdy.x dPdy.y dPdy.z
       *        m5     m6     m7     m8     m9     m10
       */
      for (int i = 0; i < ir->lod_info.grad.dPdx->type->vector_elements; i++) {
	 emit(MOV(fs_reg(MRF, base_mrf + mlen), dPdx));
	 dPdx.reg_offset++;
      }
      mlen += MAX2(ir->lod_info.grad.dPdx->type->vector_elements, 2);

      for (int i = 0; i < ir->lod_info.grad.dPdy->type->vector_elements; i++) {
	 emit(MOV(fs_reg(MRF, base_mrf + mlen), dPdy));
	 dPdy.reg_offset++;
      }
      mlen += MAX2(ir->lod_info.grad.dPdy->type->vector_elements, 2);
   } else if (ir->op == ir_txs) {
      /* There's no SIMD8 resinfo message on Gen4.  Use SIMD16 instead. */
      simd16 = true;
      emit(MOV(fs_reg(MRF, base_mrf + mlen, BRW_REGISTER_TYPE_UD), lod));
      mlen += 2;
   } else {
      /* Oh joy.  gen4 doesn't have SIMD8 non-shadow-compare bias/lod
       * instructions.  We'll need to do SIMD16 here.
       */
      simd16 = true;
      assert(ir->op == ir_txb || ir->op == ir_txl || ir->op == ir_txf);

      for (int i = 0; i < ir->coordinate->type->vector_elements; i++) {
	 emit(MOV(fs_reg(MRF, base_mrf + mlen + i * 2, coordinate.type),
                  coordinate));
	 coordinate.reg_offset++;
      }

      /* Initialize the rest of u/v/r with 0.0.  Empirically, this seems to
       * be necessary for TXF (ld), but seems wise to do for all messages.
       */
      for (int i = ir->coordinate->type->vector_elements; i < 3; i++) {
	 emit(MOV(fs_reg(MRF, base_mrf + mlen + i * 2), fs_reg(0.0f)));
      }

      /* lod/bias appears after u/v/r. */
      mlen += 6;

      emit(MOV(fs_reg(MRF, base_mrf + mlen, lod.type), lod));
      mlen++;

      /* The unused upper half. */
      mlen++;
   }

   if (simd16) {
      /* Now, since we're doing simd16, the return is 2 interleaved
       * vec4s where the odd-indexed ones are junk. We'll need to move
       * this weirdness around to the expected layout.
       */
      orig_dst = dst;
      dst = fs_reg(GRF, virtual_grf_alloc(8),
                   (brw->is_g4x ?
                    brw_type_for_base_type(ir->type) :
                    BRW_REGISTER_TYPE_F));
   }

   fs_inst *inst = NULL;
   switch (ir->op) {
   case ir_tex:
      inst = emit(SHADER_OPCODE_TEX, dst);
      break;
   case ir_txb:
      inst = emit(FS_OPCODE_TXB, dst);
      break;
   case ir_txl:
      inst = emit(SHADER_OPCODE_TXL, dst);
      break;
   case ir_txd:
      inst = emit(SHADER_OPCODE_TXD, dst);
      break;
   case ir_txs:
      inst = emit(SHADER_OPCODE_TXS, dst);
      break;
   case ir_txf:
      inst = emit(SHADER_OPCODE_TXF, dst);
      break;
   default:
      fail("unrecognized texture opcode");
   }
   inst->base_mrf = base_mrf;
   inst->mlen = mlen;
   inst->header_present = true;
   inst->regs_written = simd16 ? 8 : 4;

   if (simd16) {
      for (int i = 0; i < 4; i++) {
	 emit(MOV(orig_dst, dst));
	 orig_dst.reg_offset++;
	 dst.reg_offset += 2;
      }
   }

   return inst;
}

/* gen5's sampler has slots for u, v, r, array index, then optional
 * parameters like shadow comparitor or LOD bias.  If optional
 * parameters aren't present, those base slots are optional and don't
 * need to be included in the message.
 *
 * We don't fill in the unnecessary slots regardless, which may look
 * surprising in the disassembly.
 */
fs_inst *
fs_visitor::emit_texture_gen5(ir_texture *ir, fs_reg dst, fs_reg coordinate,
                              fs_reg shadow_c, fs_reg lod, fs_reg lod2,
                              fs_reg sample_index)
{
   int mlen = 0;
   int base_mrf = 2;
   int reg_width = dispatch_width / 8;
   bool header_present = false;
   const int vector_elements =
      ir->coordinate ? ir->coordinate->type->vector_elements : 0;

   if (ir->offset) {
      /* The offsets set up by the ir_texture visitor are in the
       * m1 header, so we can't go headerless.
       */
      header_present = true;
      mlen++;
      base_mrf--;
   }

   for (int i = 0; i < vector_elements; i++) {
      emit(MOV(fs_reg(MRF, base_mrf + mlen + i * reg_width, coordinate.type),
               coordinate));
      coordinate.reg_offset++;
   }
   mlen += vector_elements * reg_width;

   if (ir->shadow_comparitor) {
      mlen = MAX2(mlen, header_present + 4 * reg_width);

      emit(MOV(fs_reg(MRF, base_mrf + mlen), shadow_c));
      mlen += reg_width;
   }

   fs_inst *inst = NULL;
   switch (ir->op) {
   case ir_tex:
      inst = emit(SHADER_OPCODE_TEX, dst);
      break;
   case ir_txb:
      mlen = MAX2(mlen, header_present + 4 * reg_width);
      emit(MOV(fs_reg(MRF, base_mrf + mlen), lod));
      mlen += reg_width;

      inst = emit(FS_OPCODE_TXB, dst);
      break;
   case ir_txl:
      mlen = MAX2(mlen, header_present + 4 * reg_width);
      emit(MOV(fs_reg(MRF, base_mrf + mlen), lod));
      mlen += reg_width;

      inst = emit(SHADER_OPCODE_TXL, dst);
      break;
   case ir_txd: {
      mlen = MAX2(mlen, header_present + 4 * reg_width); /* skip over 'ai' */

      /**
       *  P   =  u,    v,    r
       * dPdx = dudx, dvdx, drdx
       * dPdy = dudy, dvdy, drdy
       *
       * Load up these values:
       * - dudx   dudy   dvdx   dvdy   drdx   drdy
       * - dPdx.x dPdy.x dPdx.y dPdy.y dPdx.z dPdy.z
       */
      for (int i = 0; i < ir->lod_info.grad.dPdx->type->vector_elements; i++) {
	 emit(MOV(fs_reg(MRF, base_mrf + mlen), lod));
	 lod.reg_offset++;
	 mlen += reg_width;

	 emit(MOV(fs_reg(MRF, base_mrf + mlen), lod2));
	 lod2.reg_offset++;
	 mlen += reg_width;
      }

      inst = emit(SHADER_OPCODE_TXD, dst);
      break;
   }
   case ir_txs:
      emit(MOV(fs_reg(MRF, base_mrf + mlen, BRW_REGISTER_TYPE_UD), lod));
      mlen += reg_width;
      inst = emit(SHADER_OPCODE_TXS, dst);
      break;
   case ir_query_levels:
      emit(MOV(fs_reg(MRF, base_mrf + mlen, BRW_REGISTER_TYPE_UD), fs_reg(0u)));
      mlen += reg_width;
      inst = emit(SHADER_OPCODE_TXS, dst);
      break;
   case ir_txf:
      mlen = header_present + 4 * reg_width;
      emit(MOV(fs_reg(MRF, base_mrf + mlen - reg_width, BRW_REGISTER_TYPE_UD), lod));
      inst = emit(SHADER_OPCODE_TXF, dst);
      break;
   case ir_txf_ms:
      mlen = header_present + 4 * reg_width;

      /* lod */
      emit(MOV(fs_reg(MRF, base_mrf + mlen - reg_width, BRW_REGISTER_TYPE_UD), fs_reg(0)));
      /* sample index */
      emit(MOV(fs_reg(MRF, base_mrf + mlen, BRW_REGISTER_TYPE_UD), sample_index));
      mlen += reg_width;
      inst = emit(SHADER_OPCODE_TXF_CMS, dst);
      break;
   case ir_lod:
      inst = emit(SHADER_OPCODE_LOD, dst);
      break;
   case ir_tg4:
      inst = emit(SHADER_OPCODE_TG4, dst);
      break;
   default:
      fail("unrecognized texture opcode");
      break;
   }
   inst->base_mrf = base_mrf;
   inst->mlen = mlen;
   inst->header_present = header_present;
   inst->regs_written = 4;

   if (mlen > MAX_SAMPLER_MESSAGE_SIZE) {
      fail("Message length >" STRINGIFY(MAX_SAMPLER_MESSAGE_SIZE)
           " disallowed by hardware\n");
   }

   return inst;
}

fs_inst *
fs_visitor::emit_texture_gen7(ir_texture *ir, fs_reg dst, fs_reg coordinate,
                              fs_reg shadow_c, fs_reg lod, fs_reg lod2,
                              fs_reg sample_index, fs_reg mcs, int sampler)
{
   int reg_width = dispatch_width / 8;
   bool header_present = false;

   fs_reg payload = fs_reg(this, glsl_type::float_type);
   fs_reg next = payload;

   if (ir->op == ir_tg4 || (ir->offset && ir->op != ir_txf) || sampler >= 16) {
      /* For general texture offsets (no txf workaround), we need a header to
       * put them in.  Note that for SIMD16 we're making space for two actual
       * hardware registers here, so the emit will have to fix up for this.
       *
       * * ir4_tg4 needs to place its channel select in the header,
       * for interaction with ARB_texture_swizzle
       *
       * The sampler index is only 4-bits, so for larger sampler numbers we
       * need to offset the Sampler State Pointer in the header.
       */
      header_present = true;
      next.reg_offset++;
   }

   if (ir->shadow_comparitor) {
      emit(MOV(next, shadow_c));
      next.reg_offset++;
   }

   bool has_nonconstant_offset = ir->offset && !ir->offset->as_constant();
   bool coordinate_done = false;

   /* Set up the LOD info */
   switch (ir->op) {
   case ir_tex:
   case ir_lod:
      break;
   case ir_txb:
      emit(MOV(next, lod));
      next.reg_offset++;
      break;
   case ir_txl:
      emit(MOV(next, lod));
      next.reg_offset++;
      break;
   case ir_txd: {
      no16("Gen7 does not support sample_d/sample_d_c in SIMD16 mode.");

      /* Load dPdx and the coordinate together:
       * [hdr], [ref], x, dPdx.x, dPdy.x, y, dPdx.y, dPdy.y, z, dPdx.z, dPdy.z
       */
      for (int i = 0; i < ir->coordinate->type->vector_elements; i++) {
	 emit(MOV(next, coordinate));
	 coordinate.reg_offset++;
	 next.reg_offset++;

         /* For cube map array, the coordinate is (u,v,r,ai) but there are
          * only derivatives for (u, v, r).
          */
         if (i < ir->lod_info.grad.dPdx->type->vector_elements) {
            emit(MOV(next, lod));
            lod.reg_offset++;
            next.reg_offset++;

            emit(MOV(next, lod2));
            lod2.reg_offset++;
            next.reg_offset++;
         }
      }

      coordinate_done = true;
      break;
   }
   case ir_txs:
      emit(MOV(retype(next, BRW_REGISTER_TYPE_UD), lod));
      next.reg_offset++;
      break;
   case ir_query_levels:
      emit(MOV(retype(next, BRW_REGISTER_TYPE_UD), fs_reg(0u)));
      next.reg_offset++;
      break;
   case ir_txf:
      /* Unfortunately, the parameters for LD are intermixed: u, lod, v, r. */
      emit(MOV(retype(next, BRW_REGISTER_TYPE_D), coordinate));
      coordinate.reg_offset++;
      next.reg_offset++;

      emit(MOV(retype(next, BRW_REGISTER_TYPE_D), lod));
      next.reg_offset++;

      for (int i = 1; i < ir->coordinate->type->vector_elements; i++) {
	 emit(MOV(retype(next, BRW_REGISTER_TYPE_D), coordinate));
	 coordinate.reg_offset++;
	 next.reg_offset++;
      }

      coordinate_done = true;
      break;
   case ir_txf_ms:
      emit(MOV(retype(next, BRW_REGISTER_TYPE_UD), sample_index));
      next.reg_offset++;

      /* data from the multisample control surface */
      emit(MOV(retype(next, BRW_REGISTER_TYPE_UD), mcs));
      next.reg_offset++;

      /* there is no offsetting for this message; just copy in the integer
       * texture coordinates
       */
      for (int i = 0; i < ir->coordinate->type->vector_elements; i++) {
         emit(MOV(retype(next, BRW_REGISTER_TYPE_D), coordinate));
         coordinate.reg_offset++;
         next.reg_offset++;
      }

      coordinate_done = true;
      break;
   case ir_tg4:
      if (has_nonconstant_offset) {
         if (ir->shadow_comparitor)
            no16("Gen7 does not support gather4_po_c in SIMD16 mode.");

         /* More crazy intermixing */
         ir->offset->accept(this);
         fs_reg offset_value = this->result;

         for (int i = 0; i < 2; i++) { /* u, v */
            emit(MOV(next, coordinate));
            coordinate.reg_offset++;
            next.reg_offset++;
         }

         for (int i = 0; i < 2; i++) { /* offu, offv */
            emit(MOV(retype(next, BRW_REGISTER_TYPE_D), offset_value));
            offset_value.reg_offset++;
            next.reg_offset++;
         }

         if (ir->coordinate->type->vector_elements == 3) { /* r if present */
            emit(MOV(next, coordinate));
            coordinate.reg_offset++;
            next.reg_offset++;
         }

         coordinate_done = true;
      }
      break;
   }

   /* Set up the coordinate (except for cases where it was done above) */
   if (ir->coordinate && !coordinate_done) {
      for (int i = 0; i < ir->coordinate->type->vector_elements; i++) {
         emit(MOV(next, coordinate));
         coordinate.reg_offset++;
         next.reg_offset++;
      }
   }

   /* Generate the SEND */
   fs_inst *inst = NULL;
   switch (ir->op) {
   case ir_tex: inst = emit(SHADER_OPCODE_TEX, dst, payload); break;
   case ir_txb: inst = emit(FS_OPCODE_TXB, dst, payload); break;
   case ir_txl: inst = emit(SHADER_OPCODE_TXL, dst, payload); break;
   case ir_txd: inst = emit(SHADER_OPCODE_TXD, dst, payload); break;
   case ir_txf: inst = emit(SHADER_OPCODE_TXF, dst, payload); break;
   case ir_txf_ms: inst = emit(SHADER_OPCODE_TXF_CMS, dst, payload); break;
   case ir_txs: inst = emit(SHADER_OPCODE_TXS, dst, payload); break;
   case ir_query_levels: inst = emit(SHADER_OPCODE_TXS, dst, payload); break;
   case ir_lod: inst = emit(SHADER_OPCODE_LOD, dst, payload); break;
   case ir_tg4:
      if (has_nonconstant_offset)
         inst = emit(SHADER_OPCODE_TG4_OFFSET, dst, payload);
      else
         inst = emit(SHADER_OPCODE_TG4, dst, payload);
      break;
   }
   inst->base_mrf = -1;
   if (reg_width == 2)
      inst->mlen = next.reg_offset * reg_width - header_present;
   else
      inst->mlen = next.reg_offset * reg_width;
   inst->header_present = header_present;
   inst->regs_written = 4;

   virtual_grf_sizes[payload.reg] = next.reg_offset;
   if (inst->mlen > MAX_SAMPLER_MESSAGE_SIZE) {
      fail("Message length >" STRINGIFY(MAX_SAMPLER_MESSAGE_SIZE)
           " disallowed by hardware\n");
   }

   return inst;
}

fs_reg
fs_visitor::rescale_texcoord(ir_texture *ir, fs_reg coordinate,
                             bool is_rect, int sampler, int texunit)
{
   fs_inst *inst = NULL;
   bool needs_gl_clamp = true;
   fs_reg scale_x, scale_y;

   /* The 965 requires the EU to do the normalization of GL rectangle
    * texture coordinates.  We use the program parameter state
    * tracking to get the scaling factor.
    */
   if (is_rect &&
       (brw->gen < 6 ||
	(brw->gen >= 6 && (c->key.tex.gl_clamp_mask[0] & (1 << sampler) ||
			     c->key.tex.gl_clamp_mask[1] & (1 << sampler))))) {
      struct gl_program_parameter_list *params = prog->Parameters;
      int tokens[STATE_LENGTH] = {
	 STATE_INTERNAL,
	 STATE_TEXRECT_SCALE,
	 texunit,
	 0,
	 0
      };

      no16("rectangle scale uniform setup not supported on SIMD16\n");
      if (dispatch_width == 16) {
	 return coordinate;
      }

      GLuint index = _mesa_add_state_reference(params,
					       (gl_state_index *)tokens);
      /* Try to find existing copies of the texrect scale uniforms. */
      for (unsigned i = 0; i < uniforms; i++) {
         if (stage_prog_data->param[i] ==
             &prog->Parameters->ParameterValues[index][0].f) {
            scale_x = fs_reg(UNIFORM, i);
            scale_y = fs_reg(UNIFORM, i + 1);
            break;
         }
      }

      /* If we didn't already set them up, do so now. */
      if (scale_x.file == BAD_FILE) {
         scale_x = fs_reg(UNIFORM, uniforms);
         scale_y = fs_reg(UNIFORM, uniforms + 1);

         stage_prog_data->param[uniforms++] =
            &prog->Parameters->ParameterValues[index][0].f;
         stage_prog_data->param[uniforms++] =
            &prog->Parameters->ParameterValues[index][1].f;
      }
   }

   /* The 965 requires the EU to do the normalization of GL rectangle
    * texture coordinates.  We use the program parameter state
    * tracking to get the scaling factor.
    */
   if (brw->gen < 6 && is_rect) {
      fs_reg dst = fs_reg(this, ir->coordinate->type);
      fs_reg src = coordinate;
      coordinate = dst;

      emit(MUL(dst, src, scale_x));
      dst.reg_offset++;
      src.reg_offset++;
      emit(MUL(dst, src, scale_y));
   } else if (is_rect) {
      /* On gen6+, the sampler handles the rectangle coordinates
       * natively, without needing rescaling.  But that means we have
       * to do GL_CLAMP clamping at the [0, width], [0, height] scale,
       * not [0, 1] like the default case below.
       */
      needs_gl_clamp = false;

      for (int i = 0; i < 2; i++) {
	 if (c->key.tex.gl_clamp_mask[i] & (1 << sampler)) {
	    fs_reg chan = coordinate;
	    chan.reg_offset += i;

	    inst = emit(BRW_OPCODE_SEL, chan, chan, fs_reg(0.0f));
	    inst->conditional_mod = BRW_CONDITIONAL_G;

	    /* Our parameter comes in as 1.0/width or 1.0/height,
	     * because that's what people normally want for doing
	     * texture rectangle handling.  We need width or height
	     * for clamping, but we don't care enough to make a new
	     * parameter type, so just invert back.
	     */
	    fs_reg limit = fs_reg(this, glsl_type::float_type);
	    emit(MOV(limit, i == 0 ? scale_x : scale_y));
	    emit(SHADER_OPCODE_RCP, limit, limit);

	    inst = emit(BRW_OPCODE_SEL, chan, chan, limit);
	    inst->conditional_mod = BRW_CONDITIONAL_L;
	 }
      }
   }

   if (ir->coordinate && needs_gl_clamp) {
      for (unsigned int i = 0;
	   i < MIN2(ir->coordinate->type->vector_elements, 3); i++) {
	 if (c->key.tex.gl_clamp_mask[i] & (1 << sampler)) {
	    fs_reg chan = coordinate;
	    chan.reg_offset += i;

	    fs_inst *inst = emit(MOV(chan, chan));
	    inst->saturate = true;
	 }
      }
   }
   return coordinate;
}

/* Sample from the MCS surface attached to this multisample texture. */
fs_reg
fs_visitor::emit_mcs_fetch(ir_texture *ir, fs_reg coordinate, int sampler)
{
   int reg_width = dispatch_width / 8;
   fs_reg payload = fs_reg(this, glsl_type::float_type);
   fs_reg dest = fs_reg(this, glsl_type::uvec4_type);
   fs_reg next = payload;

   /* parameters are: u, v, r, lod; missing parameters are treated as zero */
   for (int i = 0; i < ir->coordinate->type->vector_elements; i++) {
      emit(MOV(retype(next, BRW_REGISTER_TYPE_D), coordinate));
      coordinate.reg_offset++;
      next.reg_offset++;
   }

   fs_inst *inst = emit(SHADER_OPCODE_TXF_MCS, dest, payload);
   virtual_grf_sizes[payload.reg] = next.reg_offset;
   inst->base_mrf = -1;
   inst->mlen = next.reg_offset * reg_width;
   inst->header_present = false;
   inst->regs_written = 4; /* we only care about one reg of response,
                            * but the sampler always writes 4/8
                            */
   inst->sampler = sampler;

   return dest;
}

void
fs_visitor::visit(ir_texture *ir)
{
   fs_inst *inst = NULL;

   int sampler =
      _mesa_get_sampler_uniform_value(ir->sampler, shader_prog, prog);
   /* FINISHME: We're failing to recompile our programs when the sampler is
    * updated.  This only matters for the texture rectangle scale parameters
    * (pre-gen6, or gen6+ with GL_CLAMP).
    */
   int texunit = prog->SamplerUnits[sampler];

   if (ir->op == ir_tg4) {
      /* When tg4 is used with the degenerate ZERO/ONE swizzles, don't bother
       * emitting anything other than setting up the constant result.
       */
      ir_constant *chan = ir->lod_info.component->as_constant();
      int swiz = GET_SWZ(c->key.tex.swizzles[sampler], chan->value.i[0]);
      if (swiz == SWIZZLE_ZERO || swiz == SWIZZLE_ONE) {

         fs_reg res = fs_reg(this, glsl_type::vec4_type);
         this->result = res;

         for (int i=0; i<4; i++) {
            emit(MOV(res, fs_reg(swiz == SWIZZLE_ZERO ? 0.0f : 1.0f)));
            res.reg_offset++;
         }
         return;
      }
   }

   /* Should be lowered by do_lower_texture_projection */
   assert(!ir->projector);

   /* Should be lowered */
   assert(!ir->offset || !ir->offset->type->is_array());

   /* Generate code to compute all the subexpression trees.  This has to be
    * done before loading any values into MRFs for the sampler message since
    * generating these values may involve SEND messages that need the MRFs.
    */
   fs_reg coordinate;
   if (ir->coordinate) {
      ir->coordinate->accept(this);

      coordinate = rescale_texcoord(ir, this->result,
                                    ir->sampler->type->sampler_dimensionality ==
                                    GLSL_SAMPLER_DIM_RECT,
                                    sampler, texunit);
   }

   fs_reg shadow_comparitor;
   if (ir->shadow_comparitor) {
      ir->shadow_comparitor->accept(this);
      shadow_comparitor = this->result;
   }

   fs_reg lod, lod2, sample_index, mcs;
   switch (ir->op) {
   case ir_tex:
   case ir_lod:
   case ir_tg4:
   case ir_query_levels:
      break;
   case ir_txb:
      ir->lod_info.bias->accept(this);
      lod = this->result;
      break;
   case ir_txd:
      ir->lod_info.grad.dPdx->accept(this);
      lod = this->result;

      ir->lod_info.grad.dPdy->accept(this);
      lod2 = this->result;
      break;
   case ir_txf:
   case ir_txl:
   case ir_txs:
      ir->lod_info.lod->accept(this);
      lod = this->result;
      break;
   case ir_txf_ms:
      ir->lod_info.sample_index->accept(this);
      sample_index = this->result;

      if (brw->gen >= 7 && c->key.tex.compressed_multisample_layout_mask & (1<<sampler))
         mcs = emit_mcs_fetch(ir, coordinate, sampler);
      else
         mcs = fs_reg(0u);
      break;
   default:
      assert(!"Unrecognized texture opcode");
   };

   /* Writemasking doesn't eliminate channels on SIMD8 texture
    * samples, so don't worry about them.
    */
   fs_reg dst = fs_reg(this, glsl_type::get_instance(ir->type->base_type, 4, 1));

   if (brw->gen >= 7) {
      inst = emit_texture_gen7(ir, dst, coordinate, shadow_comparitor,
                               lod, lod2, sample_index, mcs, sampler);
   } else if (brw->gen >= 5) {
      inst = emit_texture_gen5(ir, dst, coordinate, shadow_comparitor,
                               lod, lod2, sample_index);
   } else {
      inst = emit_texture_gen4(ir, dst, coordinate, shadow_comparitor,
                               lod, lod2);
   }

   if (ir->offset != NULL && ir->op != ir_txf)
      inst->texture_offset = brw_texture_offset(ctx, ir->offset->as_constant());

   if (ir->op == ir_tg4)
      inst->texture_offset |= gather_channel(ir, sampler) << 16; // M0.2:16-17

   inst->sampler = sampler;

   if (ir->shadow_comparitor)
      inst->shadow_compare = true;

   /* fixup #layers for cube map arrays */
   if (ir->op == ir_txs) {
      glsl_type const *type = ir->sampler->type;
      if (type->sampler_dimensionality == GLSL_SAMPLER_DIM_CUBE &&
          type->sampler_array) {
         fs_reg depth = dst;
         depth.reg_offset = 2;
         emit_math(SHADER_OPCODE_INT_QUOTIENT, depth, depth, fs_reg(6));
      }
   }

   if (brw->gen == 6 && ir->op == ir_tg4) {
      emit_gen6_gather_wa(c->key.tex.gen6_gather_wa[sampler], dst);
   }

   swizzle_result(ir, dst, sampler);
}

/**
 * Apply workarounds for Gen6 gather with UINT/SINT
 */
void
fs_visitor::emit_gen6_gather_wa(uint8_t wa, fs_reg dst)
{
   if (!wa)
      return;

   int width = (wa & WA_8BIT) ? 8 : 16;

   for (int i = 0; i < 4; i++) {
      fs_reg dst_f = retype(dst, BRW_REGISTER_TYPE_F);
      /* Convert from UNORM to UINT */
      emit(MUL(dst_f, dst_f, fs_reg((float)((1 << width) - 1))));
      emit(MOV(dst, dst_f));

      if (wa & WA_SIGN) {
         /* Reinterpret the UINT value as a signed INT value by
          * shifting the sign bit into place, then shifting back
          * preserving sign.
          */
         emit(SHL(dst, dst, fs_reg(32 - width)));
         emit(ASR(dst, dst, fs_reg(32 - width)));
      }

      dst.reg_offset++;
   }
}

/**
 * Set up the gather channel based on the swizzle, for gather4.
 */
uint32_t
fs_visitor::gather_channel(ir_texture *ir, int sampler)
{
   ir_constant *chan = ir->lod_info.component->as_constant();
   int swiz = GET_SWZ(c->key.tex.swizzles[sampler], chan->value.i[0]);
   switch (swiz) {
      case SWIZZLE_X: return 0;
      case SWIZZLE_Y:
         /* gather4 sampler is broken for green channel on RG32F --
          * we must ask for blue instead.
          */
         if (c->key.tex.gather_channel_quirk_mask & (1<<sampler))
            return 2;
         return 1;
      case SWIZZLE_Z: return 2;
      case SWIZZLE_W: return 3;
      default:
         assert(!"Not reached"); /* zero, one swizzles handled already */
         return 0;
   }
}

/**
 * Swizzle the result of a texture result.  This is necessary for
 * EXT_texture_swizzle as well as DEPTH_TEXTURE_MODE for shadow comparisons.
 */
void
fs_visitor::swizzle_result(ir_texture *ir, fs_reg orig_val, int sampler)
{
   if (ir->op == ir_query_levels) {
      /* # levels is in .w */
      orig_val.reg_offset += 3;
      this->result = orig_val;
      return;
   }

   this->result = orig_val;

   /* txs,lod don't actually sample the texture, so swizzling the result
    * makes no sense.
    */
   if (ir->op == ir_txs || ir->op == ir_lod || ir->op == ir_tg4)
      return;

   if (ir->type == glsl_type::float_type) {
      /* Ignore DEPTH_TEXTURE_MODE swizzling. */
      assert(ir->sampler->type->sampler_shadow);
   } else if (c->key.tex.swizzles[sampler] != SWIZZLE_NOOP) {
      fs_reg swizzled_result = fs_reg(this, glsl_type::vec4_type);

      for (int i = 0; i < 4; i++) {
	 int swiz = GET_SWZ(c->key.tex.swizzles[sampler], i);
	 fs_reg l = swizzled_result;
	 l.reg_offset += i;

	 if (swiz == SWIZZLE_ZERO) {
	    emit(MOV(l, fs_reg(0.0f)));
	 } else if (swiz == SWIZZLE_ONE) {
	    emit(MOV(l, fs_reg(1.0f)));
	 } else {
	    fs_reg r = orig_val;
	    r.reg_offset += GET_SWZ(c->key.tex.swizzles[sampler], i);
	    emit(MOV(l, r));
	 }
      }
      this->result = swizzled_result;
   }
}

void
fs_visitor::visit(ir_swizzle *ir)
{
   ir->val->accept(this);
   fs_reg val = this->result;

   if (ir->type->vector_elements == 1) {
      this->result.reg_offset += ir->mask.x;
      return;
   }

   fs_reg result = fs_reg(this, ir->type);
   this->result = result;

   for (unsigned int i = 0; i < ir->type->vector_elements; i++) {
      fs_reg channel = val;
      int swiz = 0;

      switch (i) {
      case 0:
	 swiz = ir->mask.x;
	 break;
      case 1:
	 swiz = ir->mask.y;
	 break;
      case 2:
	 swiz = ir->mask.z;
	 break;
      case 3:
	 swiz = ir->mask.w;
	 break;
      }

      channel.reg_offset += swiz;
      emit(MOV(result, channel));
      result.reg_offset++;
   }
}

void
fs_visitor::visit(ir_discard *ir)
{
   assert(ir->condition == NULL); /* FINISHME */

   /* We track our discarded pixels in f0.1.  By predicating on it, we can
    * update just the flag bits that aren't yet discarded.  By emitting a
    * CMP of g0 != g0, all our currently executing channels will get turned
    * off.
    */
   fs_reg some_reg = fs_reg(retype(brw_vec8_grf(0, 0),
                                   BRW_REGISTER_TYPE_UW));
   fs_inst *cmp = emit(CMP(reg_null_f, some_reg, some_reg,
                           BRW_CONDITIONAL_NZ));
   cmp->predicate = BRW_PREDICATE_NORMAL;
   cmp->flag_subreg = 1;

   if (brw->gen >= 6) {
      /* For performance, after a discard, jump to the end of the shader.
       * However, many people will do foliage by discarding based on a
       * texture's alpha mask, and then continue on to texture with the
       * remaining pixels.  To avoid trashing the derivatives for those
       * texture samples, we'll only jump if all of the pixels in the subspan
       * have been discarded.
       */
      fs_inst *discard_jump = emit(FS_OPCODE_DISCARD_JUMP);
      discard_jump->flag_subreg = 1;
      discard_jump->predicate = BRW_PREDICATE_ALIGN1_ANY4H;
      discard_jump->predicate_inverse = true;
   }
}

void
fs_visitor::visit(ir_constant *ir)
{
   /* Set this->result to reg at the bottom of the function because some code
    * paths will cause this visitor to be applied to other fields.  This will
    * cause the value stored in this->result to be modified.
    *
    * Make reg constant so that it doesn't get accidentally modified along the
    * way.  Yes, I actually had this problem. :(
    */
   const fs_reg reg(this, ir->type);
   fs_reg dst_reg = reg;

   if (ir->type->is_array()) {
      const unsigned size = type_size(ir->type->fields.array);

      for (unsigned i = 0; i < ir->type->length; i++) {
	 ir->array_elements[i]->accept(this);
	 fs_reg src_reg = this->result;

	 dst_reg.type = src_reg.type;
	 for (unsigned j = 0; j < size; j++) {
	    emit(MOV(dst_reg, src_reg));
	    src_reg.reg_offset++;
	    dst_reg.reg_offset++;
	 }
      }
   } else if (ir->type->is_record()) {
      foreach_list(node, &ir->components) {
	 ir_constant *const field = (ir_constant *) node;
	 const unsigned size = type_size(field->type);

	 field->accept(this);
	 fs_reg src_reg = this->result;

	 dst_reg.type = src_reg.type;
	 for (unsigned j = 0; j < size; j++) {
	    emit(MOV(dst_reg, src_reg));
	    src_reg.reg_offset++;
	    dst_reg.reg_offset++;
	 }
      }
   } else {
      const unsigned size = type_size(ir->type);

      for (unsigned i = 0; i < size; i++) {
	 switch (ir->type->base_type) {
	 case GLSL_TYPE_FLOAT:
	    emit(MOV(dst_reg, fs_reg(ir->value.f[i])));
	    break;
	 case GLSL_TYPE_UINT:
	    emit(MOV(dst_reg, fs_reg(ir->value.u[i])));
	    break;
	 case GLSL_TYPE_INT:
	    emit(MOV(dst_reg, fs_reg(ir->value.i[i])));
	    break;
	 case GLSL_TYPE_BOOL:
	    emit(MOV(dst_reg, fs_reg((int)ir->value.b[i])));
	    break;
	 default:
	    assert(!"Non-float/uint/int/bool constant");
	 }
	 dst_reg.reg_offset++;
      }
   }

   this->result = reg;
}

void
fs_visitor::emit_bool_to_cond_code(ir_rvalue *ir)
{
   ir_expression *expr = ir->as_expression();

   if (expr &&
       expr->operation != ir_binop_logic_and &&
       expr->operation != ir_binop_logic_or &&
       expr->operation != ir_binop_logic_xor) {
      fs_reg op[2];
      fs_inst *inst;

      assert(expr->get_num_operands() <= 2);
      for (unsigned int i = 0; i < expr->get_num_operands(); i++) {
	 assert(expr->operands[i]->type->is_scalar());

	 expr->operands[i]->accept(this);
	 op[i] = this->result;

	 resolve_ud_negate(&op[i]);
      }

      switch (expr->operation) {
      case ir_unop_logic_not:
	 inst = emit(AND(reg_null_d, op[0], fs_reg(1)));
	 inst->conditional_mod = BRW_CONDITIONAL_Z;
	 break;

      case ir_unop_f2b:
	 if (brw->gen >= 6) {
	    emit(CMP(reg_null_d, op[0], fs_reg(0.0f), BRW_CONDITIONAL_NZ));
	 } else {
	    inst = emit(MOV(reg_null_f, op[0]));
            inst->conditional_mod = BRW_CONDITIONAL_NZ;
	 }
	 break;

      case ir_unop_i2b:
	 if (brw->gen >= 6) {
	    emit(CMP(reg_null_d, op[0], fs_reg(0), BRW_CONDITIONAL_NZ));
	 } else {
	    inst = emit(MOV(reg_null_d, op[0]));
            inst->conditional_mod = BRW_CONDITIONAL_NZ;
	 }
	 break;

      case ir_binop_greater:
      case ir_binop_gequal:
      case ir_binop_less:
      case ir_binop_lequal:
      case ir_binop_equal:
      case ir_binop_all_equal:
      case ir_binop_nequal:
      case ir_binop_any_nequal:
	 resolve_bool_comparison(expr->operands[0], &op[0]);
	 resolve_bool_comparison(expr->operands[1], &op[1]);

	 emit(CMP(reg_null_d, op[0], op[1],
                  brw_conditional_for_comparison(expr->operation)));
	 break;

      default:
	 assert(!"not reached");
	 fail("bad cond code\n");
	 break;
      }
      return;
   }

   ir->accept(this);

   fs_inst *inst = emit(AND(reg_null_d, this->result, fs_reg(1)));
   inst->conditional_mod = BRW_CONDITIONAL_NZ;
}

/**
 * Emit a gen6 IF statement with the comparison folded into the IF
 * instruction.
 */
void
fs_visitor::emit_if_gen6(ir_if *ir)
{
   ir_expression *expr = ir->condition->as_expression();

   if (expr) {
      fs_reg op[2];
      fs_inst *inst;
      fs_reg temp;

      assert(expr->get_num_operands() <= 2);
      for (unsigned int i = 0; i < expr->get_num_operands(); i++) {
	 assert(expr->operands[i]->type->is_scalar());

	 expr->operands[i]->accept(this);
	 op[i] = this->result;
      }

      switch (expr->operation) {
      case ir_unop_logic_not:
      case ir_binop_logic_xor:
      case ir_binop_logic_or:
      case ir_binop_logic_and:
         /* For operations on bool arguments, only the low bit of the bool is
          * valid, and the others are undefined.  Fall back to the condition
          * code path.
          */
         break;

      case ir_unop_f2b:
	 inst = emit(BRW_OPCODE_IF, reg_null_f, op[0], fs_reg(0));
	 inst->conditional_mod = BRW_CONDITIONAL_NZ;
	 return;

      case ir_unop_i2b:
	 emit(IF(op[0], fs_reg(0), BRW_CONDITIONAL_NZ));
	 return;

      case ir_binop_greater:
      case ir_binop_gequal:
      case ir_binop_less:
      case ir_binop_lequal:
      case ir_binop_equal:
      case ir_binop_all_equal:
      case ir_binop_nequal:
      case ir_binop_any_nequal:
	 resolve_bool_comparison(expr->operands[0], &op[0]);
	 resolve_bool_comparison(expr->operands[1], &op[1]);

	 emit(IF(op[0], op[1],
                 brw_conditional_for_comparison(expr->operation)));
	 return;
      default:
	 assert(!"not reached");
	 emit(IF(op[0], fs_reg(0), BRW_CONDITIONAL_NZ));
	 fail("bad condition\n");
	 return;
      }
   }

   emit_bool_to_cond_code(ir->condition);
   fs_inst *inst = emit(BRW_OPCODE_IF);
   inst->predicate = BRW_PREDICATE_NORMAL;
}

/**
 * Try to replace IF/MOV/ELSE/MOV/ENDIF with SEL.
 *
 * Many GLSL shaders contain the following pattern:
 *
 *    x = condition ? foo : bar
 *
 * The compiler emits an ir_if tree for this, since each subexpression might be
 * a complex tree that could have side-effects or short-circuit logic.
 *
 * However, the common case is to simply select one of two constants or
 * variable values---which is exactly what SEL is for.  In this case, the
 * assembly looks like:
 *
 *    (+f0) IF
 *    MOV dst src0
 *    ELSE
 *    MOV dst src1
 *    ENDIF
 *
 * which can be easily translated into:
 *
 *    (+f0) SEL dst src0 src1
 *
 * If src0 is an immediate value, we promote it to a temporary GRF.
 */
void
fs_visitor::try_replace_with_sel()
{
   fs_inst *endif_inst = (fs_inst *) instructions.get_tail();
   assert(endif_inst->opcode == BRW_OPCODE_ENDIF);

   /* Pattern match in reverse: IF, MOV, ELSE, MOV, ENDIF. */
   int opcodes[] = {
      BRW_OPCODE_IF, BRW_OPCODE_MOV, BRW_OPCODE_ELSE, BRW_OPCODE_MOV,
   };

   fs_inst *match = (fs_inst *) endif_inst->prev;
   for (int i = 0; i < 4; i++) {
      if (match->is_head_sentinel() || match->opcode != opcodes[4-i-1])
         return;
      match = (fs_inst *) match->prev;
   }

   /* The opcodes match; it looks like the right sequence of instructions. */
   fs_inst *else_mov = (fs_inst *) endif_inst->prev;
   fs_inst *then_mov = (fs_inst *) else_mov->prev->prev;
   fs_inst *if_inst = (fs_inst *) then_mov->prev;

   /* Check that the MOVs are the right form. */
   if (then_mov->dst.equals(else_mov->dst) &&
       !then_mov->is_partial_write() &&
       !else_mov->is_partial_write()) {

      /* Remove the matched instructions; we'll emit a SEL to replace them. */
      while (!if_inst->next->is_tail_sentinel())
         if_inst->next->remove();
      if_inst->remove();

      /* Only the last source register can be a constant, so if the MOV in
       * the "then" clause uses a constant, we need to put it in a temporary.
       */
      fs_reg src0(then_mov->src[0]);
      if (src0.file == IMM) {
         src0 = fs_reg(this, glsl_type::float_type);
         src0.type = then_mov->src[0].type;
         emit(MOV(src0, then_mov->src[0]));
      }

      fs_inst *sel;
      if (if_inst->conditional_mod) {
         /* Sandybridge-specific IF with embedded comparison */
         emit(CMP(reg_null_d, if_inst->src[0], if_inst->src[1],
                  if_inst->conditional_mod));
         sel = emit(BRW_OPCODE_SEL, then_mov->dst, src0, else_mov->src[0]);
         sel->predicate = BRW_PREDICATE_NORMAL;
      } else {
         /* Separate CMP and IF instructions */
         sel = emit(BRW_OPCODE_SEL, then_mov->dst, src0, else_mov->src[0]);
         sel->predicate = if_inst->predicate;
         sel->predicate_inverse = if_inst->predicate_inverse;
      }
   }
}

void
fs_visitor::visit(ir_if *ir)
{
   if (brw->gen < 6) {
      no16("Can't support (non-uniform) control flow on SIMD16\n");
   }

   /* Don't point the annotation at the if statement, because then it plus
    * the then and else blocks get printed.
    */
   this->base_ir = ir->condition;

   if (brw->gen == 6) {
      emit_if_gen6(ir);
   } else {
      emit_bool_to_cond_code(ir->condition);

      emit(IF(BRW_PREDICATE_NORMAL));
   }

   foreach_list(node, &ir->then_instructions) {
      ir_instruction *ir = (ir_instruction *)node;
      this->base_ir = ir;

      ir->accept(this);
   }

   if (!ir->else_instructions.is_empty()) {
      emit(BRW_OPCODE_ELSE);

      foreach_list(node, &ir->else_instructions) {
	 ir_instruction *ir = (ir_instruction *)node;
	 this->base_ir = ir;

	 ir->accept(this);
      }
   }

   emit(BRW_OPCODE_ENDIF);

   try_replace_with_sel();
}

void
fs_visitor::visit(ir_loop *ir)
{
   if (brw->gen < 6) {
      no16("Can't support (non-uniform) control flow on SIMD16\n");
   }

   this->base_ir = NULL;
   emit(BRW_OPCODE_DO);

   foreach_list(node, &ir->body_instructions) {
      ir_instruction *ir = (ir_instruction *)node;

      this->base_ir = ir;
      ir->accept(this);
   }

   this->base_ir = NULL;
   emit(BRW_OPCODE_WHILE);
}

void
fs_visitor::visit(ir_loop_jump *ir)
{
   switch (ir->mode) {
   case ir_loop_jump::jump_break:
      emit(BRW_OPCODE_BREAK);
      break;
   case ir_loop_jump::jump_continue:
      emit(BRW_OPCODE_CONTINUE);
      break;
   }
}

void
fs_visitor::visit_atomic_counter_intrinsic(ir_call *ir)
{
   ir_dereference *deref = static_cast<ir_dereference *>(
      ir->actual_parameters.get_head());
   ir_variable *location = deref->variable_referenced();
   unsigned surf_index = (c->prog_data.base.binding_table.abo_start +
                          location->data.atomic.buffer_index);

   /* Calculate the surface offset */
   fs_reg offset(this, glsl_type::uint_type);
   ir_dereference_array *deref_array = deref->as_dereference_array();

   if (deref_array) {
      deref_array->array_index->accept(this);

      fs_reg tmp(this, glsl_type::uint_type);
      emit(MUL(tmp, this->result, ATOMIC_COUNTER_SIZE));
      emit(ADD(offset, tmp, location->data.atomic.offset));
   } else {
      offset = location->data.atomic.offset;
   }

   /* Emit the appropriate machine instruction */
   const char *callee = ir->callee->function_name();
   ir->return_deref->accept(this);
   fs_reg dst = this->result;

   if (!strcmp("__intrinsic_atomic_read", callee)) {
      emit_untyped_surface_read(surf_index, dst, offset);

   } else if (!strcmp("__intrinsic_atomic_increment", callee)) {
      emit_untyped_atomic(BRW_AOP_INC, surf_index, dst, offset,
                          fs_reg(), fs_reg());

   } else if (!strcmp("__intrinsic_atomic_predecrement", callee)) {
      emit_untyped_atomic(BRW_AOP_PREDEC, surf_index, dst, offset,
                          fs_reg(), fs_reg());
   }
}

void
fs_visitor::visit(ir_call *ir)
{
   const char *callee = ir->callee->function_name();

   if (!strcmp("__intrinsic_atomic_read", callee) ||
       !strcmp("__intrinsic_atomic_increment", callee) ||
       !strcmp("__intrinsic_atomic_predecrement", callee)) {
      visit_atomic_counter_intrinsic(ir);
   } else {
      assert(!"Unsupported intrinsic.");
   }
}

void
fs_visitor::visit(ir_return *ir)
{
   assert(!"FINISHME");
}

void
fs_visitor::visit(ir_function *ir)
{
   /* Ignore function bodies other than main() -- we shouldn't see calls to
    * them since they should all be inlined before we get to ir_to_mesa.
    */
   if (strcmp(ir->name, "main") == 0) {
      const ir_function_signature *sig;
      exec_list empty;

      sig = ir->matching_signature(NULL, &empty);

      assert(sig);

      foreach_list(node, &sig->body) {
	 ir_instruction *ir = (ir_instruction *)node;
	 this->base_ir = ir;

	 ir->accept(this);
      }
   }
}

void
fs_visitor::visit(ir_function_signature *ir)
{
   assert(!"not reached");
   (void)ir;
}

void
fs_visitor::visit(ir_emit_vertex *)
{
   assert(!"not reached");
}

void
fs_visitor::visit(ir_end_primitive *)
{
   assert(!"not reached");
}

void
fs_visitor::emit_untyped_atomic(unsigned atomic_op, unsigned surf_index,
                                fs_reg dst, fs_reg offset, fs_reg src0,
                                fs_reg src1)
{
   const unsigned operand_len = dispatch_width / 8;
   unsigned mlen = 0;

   /* Initialize the sample mask in the message header. */
   emit(MOV(brw_uvec_mrf(8, mlen, 0), fs_reg(0u)))
      ->force_writemask_all = true;

   if (fp->UsesKill) {
      emit(MOV(brw_uvec_mrf(1, mlen, 7), brw_flag_reg(0, 1)))
         ->force_writemask_all = true;
   } else {
      emit(MOV(brw_uvec_mrf(1, mlen, 7),
               retype(brw_vec1_grf(1, 7), BRW_REGISTER_TYPE_UD)))
         ->force_writemask_all = true;
   }

   mlen++;

   /* Set the atomic operation offset. */
   emit(MOV(brw_uvec_mrf(dispatch_width, mlen, 0), offset));
   mlen += operand_len;

   /* Set the atomic operation arguments. */
   if (src0.file != BAD_FILE) {
      emit(MOV(brw_uvec_mrf(dispatch_width, mlen, 0), src0));
      mlen += operand_len;
   }

   if (src1.file != BAD_FILE) {
      emit(MOV(brw_uvec_mrf(dispatch_width, mlen, 0), src1));
      mlen += operand_len;
   }

   /* Emit the instruction. */
   fs_inst *inst = new(mem_ctx) fs_inst(SHADER_OPCODE_UNTYPED_ATOMIC, dst,
                                        atomic_op, surf_index);
   inst->base_mrf = 0;
   inst->mlen = mlen;
   inst->header_present = true;
   emit(inst);
}

void
fs_visitor::emit_untyped_surface_read(unsigned surf_index, fs_reg dst,
                                      fs_reg offset)
{
   const unsigned operand_len = dispatch_width / 8;
   unsigned mlen = 0;

   /* Initialize the sample mask in the message header. */
   emit(MOV(brw_uvec_mrf(8, mlen, 0), fs_reg(0u)))
      ->force_writemask_all = true;

   if (fp->UsesKill) {
      emit(MOV(brw_uvec_mrf(1, mlen, 7), brw_flag_reg(0, 1)))
         ->force_writemask_all = true;
   } else {
      emit(MOV(brw_uvec_mrf(1, mlen, 7),
               retype(brw_vec1_grf(1, 7), BRW_REGISTER_TYPE_UD)))
         ->force_writemask_all = true;
   }

   mlen++;

   /* Set the surface read offset. */
   emit(MOV(brw_uvec_mrf(dispatch_width, mlen, 0), offset));
   mlen += operand_len;

   /* Emit the instruction. */
   fs_inst *inst = new(mem_ctx)
      fs_inst(SHADER_OPCODE_UNTYPED_SURFACE_READ, dst, surf_index);
   inst->base_mrf = 0;
   inst->mlen = mlen;
   inst->header_present = true;
   emit(inst);
}

fs_inst *
fs_visitor::emit(fs_inst *inst)
{
   if (force_uncompressed_stack > 0)
      inst->force_uncompressed = true;

   inst->annotation = this->current_annotation;
   inst->ir = this->base_ir;

   this->instructions.push_tail(inst);

   return inst;
}

void
fs_visitor::emit(exec_list list)
{
   foreach_list_safe(node, &list) {
      fs_inst *inst = (fs_inst *)node;
      inst->remove();
      emit(inst);
   }
}

/** Emits a dummy fragment shader consisting of magenta for bringup purposes. */
void
fs_visitor::emit_dummy_fs()
{
   int reg_width = dispatch_width / 8;

   /* Everyone's favorite color. */
   emit(MOV(fs_reg(MRF, 2 + 0 * reg_width), fs_reg(1.0f)));
   emit(MOV(fs_reg(MRF, 2 + 1 * reg_width), fs_reg(0.0f)));
   emit(MOV(fs_reg(MRF, 2 + 2 * reg_width), fs_reg(1.0f)));
   emit(MOV(fs_reg(MRF, 2 + 3 * reg_width), fs_reg(0.0f)));

   fs_inst *write;
   write = emit(FS_OPCODE_FB_WRITE, fs_reg(0), fs_reg(0));
   write->base_mrf = 2;
   write->mlen = 4 * reg_width;
   write->eot = true;
}

/* The register location here is relative to the start of the URB
 * data.  It will get adjusted to be a real location before
 * generate_code() time.
 */
struct brw_reg
fs_visitor::interp_reg(int location, int channel)
{
   int regnr = c->prog_data.urb_setup[location] * 2 + channel / 2;
   int stride = (channel & 1) * 4;

   assert(c->prog_data.urb_setup[location] != -1);

   return brw_vec1_grf(regnr, stride);
}

/** Emits the interpolation for the varying inputs. */
void
fs_visitor::emit_interpolation_setup_gen4()
{
   this->current_annotation = "compute pixel centers";
   this->pixel_x = fs_reg(this, glsl_type::uint_type);
   this->pixel_y = fs_reg(this, glsl_type::uint_type);
   this->pixel_x.type = BRW_REGISTER_TYPE_UW;
   this->pixel_y.type = BRW_REGISTER_TYPE_UW;

   emit(FS_OPCODE_PIXEL_X, this->pixel_x);
   emit(FS_OPCODE_PIXEL_Y, this->pixel_y);

   this->current_annotation = "compute pixel deltas from v0";
   if (brw->has_pln) {
      this->delta_x[BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC] =
         fs_reg(this, glsl_type::vec2_type);
      this->delta_y[BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC] =
         this->delta_x[BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC];
      this->delta_y[BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC].reg_offset++;
   } else {
      this->delta_x[BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC] =
         fs_reg(this, glsl_type::float_type);
      this->delta_y[BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC] =
         fs_reg(this, glsl_type::float_type);
   }
   emit(ADD(this->delta_x[BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC],
            this->pixel_x, fs_reg(negate(brw_vec1_grf(1, 0)))));
   emit(ADD(this->delta_y[BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC],
            this->pixel_y, fs_reg(negate(brw_vec1_grf(1, 1)))));

   this->current_annotation = "compute pos.w and 1/pos.w";
   /* Compute wpos.w.  It's always in our setup, since it's needed to
    * interpolate the other attributes.
    */
   this->wpos_w = fs_reg(this, glsl_type::float_type);
   emit(FS_OPCODE_LINTERP, wpos_w,
        this->delta_x[BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC],
        this->delta_y[BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC],
	interp_reg(VARYING_SLOT_POS, 3));
   /* Compute the pixel 1/W value from wpos.w. */
   this->pixel_w = fs_reg(this, glsl_type::float_type);
   emit_math(SHADER_OPCODE_RCP, this->pixel_w, wpos_w);
   this->current_annotation = NULL;
}

/** Emits the interpolation for the varying inputs. */
void
fs_visitor::emit_interpolation_setup_gen6()
{
   struct brw_reg g1_uw = retype(brw_vec1_grf(1, 0), BRW_REGISTER_TYPE_UW);

   /* If the pixel centers end up used, the setup is the same as for gen4. */
   this->current_annotation = "compute pixel centers";
   fs_reg int_pixel_x = fs_reg(this, glsl_type::uint_type);
   fs_reg int_pixel_y = fs_reg(this, glsl_type::uint_type);
   int_pixel_x.type = BRW_REGISTER_TYPE_UW;
   int_pixel_y.type = BRW_REGISTER_TYPE_UW;
   emit(ADD(int_pixel_x,
            fs_reg(stride(suboffset(g1_uw, 4), 2, 4, 0)),
            fs_reg(brw_imm_v(0x10101010))));
   emit(ADD(int_pixel_y,
            fs_reg(stride(suboffset(g1_uw, 5), 2, 4, 0)),
            fs_reg(brw_imm_v(0x11001100))));

   /* As of gen6, we can no longer mix float and int sources.  We have
    * to turn the integer pixel centers into floats for their actual
    * use.
    */
   this->pixel_x = fs_reg(this, glsl_type::float_type);
   this->pixel_y = fs_reg(this, glsl_type::float_type);
   emit(MOV(this->pixel_x, int_pixel_x));
   emit(MOV(this->pixel_y, int_pixel_y));

   this->current_annotation = "compute pos.w";
   this->pixel_w = fs_reg(brw_vec8_grf(c->source_w_reg, 0));
   this->wpos_w = fs_reg(this, glsl_type::float_type);
   emit_math(SHADER_OPCODE_RCP, this->wpos_w, this->pixel_w);

   for (int i = 0; i < BRW_WM_BARYCENTRIC_INTERP_MODE_COUNT; ++i) {
      uint8_t reg = c->barycentric_coord_reg[i];
      this->delta_x[i] = fs_reg(brw_vec8_grf(reg, 0));
      this->delta_y[i] = fs_reg(brw_vec8_grf(reg + 1, 0));
   }

   this->current_annotation = NULL;
}

void
fs_visitor::emit_color_write(int target, int index, int first_color_mrf)
{
   int reg_width = dispatch_width / 8;
   fs_inst *inst;
   fs_reg color = outputs[target];
   fs_reg mrf;

   /* If there's no color data to be written, skip it. */
   if (color.file == BAD_FILE)
      return;

   color.reg_offset += index;

   if (dispatch_width == 8 || brw->gen >= 6) {
      /* SIMD8 write looks like:
       * m + 0: r0
       * m + 1: r1
       * m + 2: g0
       * m + 3: g1
       *
       * gen6 SIMD16 DP write looks like:
       * m + 0: r0
       * m + 1: r1
       * m + 2: g0
       * m + 3: g1
       * m + 4: b0
       * m + 5: b1
       * m + 6: a0
       * m + 7: a1
       */
      inst = emit(MOV(fs_reg(MRF, first_color_mrf + index * reg_width,
                             color.type),
                      color));
      inst->saturate = c->key.clamp_fragment_color;
   } else {
      /* pre-gen6 SIMD16 single source DP write looks like:
       * m + 0: r0
       * m + 1: g0
       * m + 2: b0
       * m + 3: a0
       * m + 4: r1
       * m + 5: g1
       * m + 6: b1
       * m + 7: a1
       */
      if (brw->has_compr4) {
	 /* By setting the high bit of the MRF register number, we
	  * indicate that we want COMPR4 mode - instead of doing the
	  * usual destination + 1 for the second half we get
	  * destination + 4.
	  */
	 inst = emit(MOV(fs_reg(MRF, BRW_MRF_COMPR4 + first_color_mrf + index,
                                color.type),
                         color));
	 inst->saturate = c->key.clamp_fragment_color;
      } else {
	 push_force_uncompressed();
	 inst = emit(MOV(fs_reg(MRF, first_color_mrf + index, color.type),
                         color));
	 inst->saturate = c->key.clamp_fragment_color;
	 pop_force_uncompressed();

	 inst = emit(MOV(fs_reg(MRF, first_color_mrf + index + 4, color.type),
                         half(color, 1)));
	 inst->force_sechalf = true;
	 inst->saturate = c->key.clamp_fragment_color;
      }
   }
}

static int
cond_for_alpha_func(GLenum func)
{
   switch(func) {
      case GL_GREATER:
         return BRW_CONDITIONAL_G;
      case GL_GEQUAL:
         return BRW_CONDITIONAL_GE;
      case GL_LESS:
         return BRW_CONDITIONAL_L;
      case GL_LEQUAL:
         return BRW_CONDITIONAL_LE;
      case GL_EQUAL:
         return BRW_CONDITIONAL_EQ;
      case GL_NOTEQUAL:
         return BRW_CONDITIONAL_NEQ;
      default:
         assert(!"Not reached");
         return 0;
   }
}

/**
 * Alpha test support for when we compile it into the shader instead
 * of using the normal fixed-function alpha test.
 */
void
fs_visitor::emit_alpha_test()
{
   this->current_annotation = "Alpha test";

   fs_inst *cmp;
   if (c->key.alpha_test_func == GL_ALWAYS)
      return;

   if (c->key.alpha_test_func == GL_NEVER) {
      /* f0.1 = 0 */
      fs_reg some_reg = fs_reg(retype(brw_vec8_grf(0, 0),
                                      BRW_REGISTER_TYPE_UW));
      cmp = emit(CMP(reg_null_f, some_reg, some_reg,
                     BRW_CONDITIONAL_NEQ));
   } else {
      /* RT0 alpha */
      fs_reg color = outputs[0];
      color.reg_offset += 3;

      /* f0.1 &= func(color, ref) */
      cmp = emit(CMP(reg_null_f, color, fs_reg(c->key.alpha_test_ref),
                     cond_for_alpha_func(c->key.alpha_test_func)));
   }
   cmp->predicate = BRW_PREDICATE_NORMAL;
   cmp->flag_subreg = 1;
}

void
fs_visitor::emit_fb_writes()
{
   this->current_annotation = "FB write header";
   bool header_present = true;
   /* We can potentially have a message length of up to 15, so we have to set
    * base_mrf to either 0 or 1 in order to fit in m0..m15.
    */
   int base_mrf = 1;
   int nr = base_mrf;
   int reg_width = dispatch_width / 8;
   bool src0_alpha_to_render_target = false;

   if (do_dual_src) {
      no16("GL_ARB_blend_func_extended not yet supported in SIMD16.");
      if (dispatch_width == 16)
         do_dual_src = false;
   }

   /* From the Sandy Bridge PRM, volume 4, page 198:
    *
    *     "Dispatched Pixel Enables. One bit per pixel indicating
    *      which pixels were originally enabled when the thread was
    *      dispatched. This field is only required for the end-of-
    *      thread message and on all dual-source messages."
    */
   if (brw->gen >= 6 &&
       (brw->is_haswell || brw->gen >= 8 || !this->fp->UsesKill) &&
       !do_dual_src &&
       c->key.nr_color_regions == 1) {
      header_present = false;
   }

   if (header_present) {
      src0_alpha_to_render_target = brw->gen >= 6 &&
				    !do_dual_src &&
                                    c->key.replicate_alpha;
      /* m2, m3 header */
      nr += 2;
   }

   if (c->aa_dest_stencil_reg) {
      push_force_uncompressed();
      emit(MOV(fs_reg(MRF, nr++),
               fs_reg(brw_vec8_grf(c->aa_dest_stencil_reg, 0))));
      pop_force_uncompressed();
   }

   c->prog_data.uses_omask =
      fp->Base.OutputsWritten & BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK);
   if(c->prog_data.uses_omask) {
      this->current_annotation = "FB write oMask";
      assert(this->sample_mask.file != BAD_FILE);
      /* Hand over gl_SampleMask. Only lower 16 bits are relevant. */
      emit(FS_OPCODE_SET_OMASK, fs_reg(MRF, nr, BRW_REGISTER_TYPE_UW), this->sample_mask);
      nr += 1;
   }

   /* Reserve space for color. It'll be filled in per MRT below. */
   int color_mrf = nr;
   nr += 4 * reg_width;
   if (do_dual_src)
      nr += 4;
   if (src0_alpha_to_render_target)
      nr += reg_width;

   if (c->source_depth_to_render_target) {
      if (brw->gen == 6) {
	 /* For outputting oDepth on gen6, SIMD8 writes have to be
	  * used.  This would require SIMD8 moves of each half to
	  * message regs, kind of like pre-gen5 SIMD16 FB writes.
	  * Just bail on doing so for now.
	  */
	 no16("Missing support for simd16 depth writes on gen6\n");
      }

      if (prog->OutputsWritten & BITFIELD64_BIT(FRAG_RESULT_DEPTH)) {
	 /* Hand over gl_FragDepth. */
	 assert(this->frag_depth.file != BAD_FILE);
	 emit(MOV(fs_reg(MRF, nr), this->frag_depth));
      } else {
	 /* Pass through the payload depth. */
	 emit(MOV(fs_reg(MRF, nr),
                  fs_reg(brw_vec8_grf(c->source_depth_reg, 0))));
      }
      nr += reg_width;
   }

   if (c->dest_depth_reg) {
      emit(MOV(fs_reg(MRF, nr),
               fs_reg(brw_vec8_grf(c->dest_depth_reg, 0))));
      nr += reg_width;
   }

   if (do_dual_src) {
      fs_reg src0 = this->outputs[0];
      fs_reg src1 = this->dual_src_output;

      this->current_annotation = ralloc_asprintf(this->mem_ctx,
						 "FB write src0");
      for (int i = 0; i < 4; i++) {
	 fs_inst *inst = emit(MOV(fs_reg(MRF, color_mrf + i, src0.type), src0));
	 src0.reg_offset++;
	 inst->saturate = c->key.clamp_fragment_color;
      }

      this->current_annotation = ralloc_asprintf(this->mem_ctx,
						 "FB write src1");
      for (int i = 0; i < 4; i++) {
	 fs_inst *inst = emit(MOV(fs_reg(MRF, color_mrf + 4 + i, src1.type),
                                  src1));
	 src1.reg_offset++;
	 inst->saturate = c->key.clamp_fragment_color;
      }

      if (INTEL_DEBUG & DEBUG_SHADER_TIME)
         emit_shader_time_end();

      fs_inst *inst = emit(FS_OPCODE_FB_WRITE);
      inst->target = 0;
      inst->base_mrf = base_mrf;
      inst->mlen = nr - base_mrf;
      inst->eot = true;
      inst->header_present = header_present;
      if ((brw->gen >= 8 || brw->is_haswell) && fp->UsesKill) {
         inst->predicate = BRW_PREDICATE_NORMAL;
         inst->flag_subreg = 1;
      }

      c->prog_data.dual_src_blend = true;
      this->current_annotation = NULL;
      return;
   }

   for (int target = 0; target < c->key.nr_color_regions; target++) {
      this->current_annotation = ralloc_asprintf(this->mem_ctx,
						 "FB write target %d",
						 target);
      /* If src0_alpha_to_render_target is true, include source zero alpha
       * data in RenderTargetWrite message for targets > 0.
       */
      int write_color_mrf = color_mrf;
      if (src0_alpha_to_render_target && target != 0) {
         fs_inst *inst;
         fs_reg color = outputs[0];
         color.reg_offset += 3;

         inst = emit(MOV(fs_reg(MRF, write_color_mrf, color.type),
                         color));
         inst->saturate = c->key.clamp_fragment_color;
         write_color_mrf = color_mrf + reg_width;
      }

      for (unsigned i = 0; i < this->output_components[target]; i++)
         emit_color_write(target, i, write_color_mrf);

      bool eot = false;
      if (target == c->key.nr_color_regions - 1) {
         eot = true;

         if (INTEL_DEBUG & DEBUG_SHADER_TIME)
            emit_shader_time_end();
      }

      fs_inst *inst = emit(FS_OPCODE_FB_WRITE);
      inst->target = target;
      inst->base_mrf = base_mrf;
      if (src0_alpha_to_render_target && target == 0)
         inst->mlen = nr - base_mrf - reg_width;
      else
         inst->mlen = nr - base_mrf;
      inst->eot = eot;
      inst->header_present = header_present;
      if ((brw->gen >= 8 || brw->is_haswell) && fp->UsesKill) {
         inst->predicate = BRW_PREDICATE_NORMAL;
         inst->flag_subreg = 1;
      }
   }

   if (c->key.nr_color_regions == 0) {
      /* Even if there's no color buffers enabled, we still need to send
       * alpha out the pipeline to our null renderbuffer to support
       * alpha-testing, alpha-to-coverage, and so on.
       */
      emit_color_write(0, 3, color_mrf);

      if (INTEL_DEBUG & DEBUG_SHADER_TIME)
         emit_shader_time_end();

      fs_inst *inst = emit(FS_OPCODE_FB_WRITE);
      inst->base_mrf = base_mrf;
      inst->mlen = nr - base_mrf;
      inst->eot = true;
      inst->header_present = header_present;
      if ((brw->gen >= 8 || brw->is_haswell) && fp->UsesKill) {
         inst->predicate = BRW_PREDICATE_NORMAL;
         inst->flag_subreg = 1;
      }
   }

   this->current_annotation = NULL;
}

void
fs_visitor::resolve_ud_negate(fs_reg *reg)
{
   if (reg->type != BRW_REGISTER_TYPE_UD ||
       !reg->negate)
      return;

   fs_reg temp = fs_reg(this, glsl_type::uint_type);
   emit(MOV(temp, *reg));
   *reg = temp;
}

void
fs_visitor::resolve_bool_comparison(ir_rvalue *rvalue, fs_reg *reg)
{
   if (rvalue->type != glsl_type::bool_type)
      return;

   fs_reg temp = fs_reg(this, glsl_type::bool_type);
   emit(AND(temp, *reg, fs_reg(1)));
   *reg = temp;
}

fs_visitor::fs_visitor(struct brw_context *brw,
                       struct brw_wm_compile *c,
                       struct gl_shader_program *shader_prog,
                       struct gl_fragment_program *fp,
                       unsigned dispatch_width)
   : backend_visitor(brw, shader_prog, &fp->Base, &c->prog_data.base,
                     MESA_SHADER_FRAGMENT),
     dispatch_width(dispatch_width)
{
   this->c = c;
   this->fp = fp;
   this->mem_ctx = ralloc_context(NULL);
   this->failed = false;
   this->simd16_unsupported = false;
   this->no16_msg = NULL;
   this->variable_ht = hash_table_ctor(0,
                                       hash_table_pointer_hash,
                                       hash_table_pointer_compare);

   memset(this->outputs, 0, sizeof(this->outputs));
   memset(this->output_components, 0, sizeof(this->output_components));
   this->first_non_payload_grf = 0;
   this->max_grf = brw->gen >= 7 ? GEN7_MRF_HACK_START : BRW_MAX_GRF;

   this->current_annotation = NULL;
   this->base_ir = NULL;

   this->virtual_grf_sizes = NULL;
   this->virtual_grf_count = 0;
   this->virtual_grf_array_size = 0;
   this->virtual_grf_start = NULL;
   this->virtual_grf_end = NULL;
   this->live_intervals = NULL;
   this->regs_live_at_ip = NULL;

   this->uniforms = 0;
   this->pull_constant_loc = NULL;
   this->push_constant_loc = NULL;

   this->force_uncompressed_stack = 0;

   this->spilled_any_registers = false;
   this->do_dual_src = false;

   if (dispatch_width == 8)
      this->param_size = rzalloc_array(mem_ctx, int, stage_prog_data->nr_params);
}

fs_visitor::~fs_visitor()
{
   ralloc_free(this->mem_ctx);
   hash_table_dtor(this->variable_ht);
}
