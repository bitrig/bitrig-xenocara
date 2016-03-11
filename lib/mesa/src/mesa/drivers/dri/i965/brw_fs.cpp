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

/** @file brw_fs.cpp
 *
 * This file drives the GLSL IR -> LIR translation, contains the
 * optimizations on the LIR, and drives the generation of native code
 * from the LIR.
 */

#include <sys/types.h>

#include "util/hash_table.h"
#include "main/macros.h"
#include "main/shaderobj.h"
#include "main/fbobject.h"
#include "program/prog_parameter.h"
#include "program/prog_print.h"
#include "util/register_allocate.h"
#include "program/hash_table.h"
#include "brw_context.h"
#include "brw_eu.h"
#include "brw_wm.h"
#include "brw_fs.h"
#include "brw_cfg.h"
#include "brw_dead_control_flow.h"
#include "main/uniforms.h"
#include "brw_fs_live_variables.h"
#include "glsl/glsl_types.h"
#include "program/sampler.h"

using namespace brw;

void
fs_inst::init(enum opcode opcode, uint8_t exec_size, const fs_reg &dst,
              const fs_reg *src, unsigned sources)
{
   memset(this, 0, sizeof(*this));

   this->src = new fs_reg[MAX2(sources, 3)];
   for (unsigned i = 0; i < sources; i++)
      this->src[i] = src[i];

   this->opcode = opcode;
   this->dst = dst;
   this->sources = sources;
   this->exec_size = exec_size;

   assert(dst.file != IMM && dst.file != UNIFORM);

   assert(this->exec_size != 0);

   this->conditional_mod = BRW_CONDITIONAL_NONE;

   /* This will be the case for almost all instructions. */
   switch (dst.file) {
   case GRF:
   case HW_REG:
   case MRF:
   case ATTR:
      this->regs_written = DIV_ROUND_UP(dst.component_size(exec_size),
                                        REG_SIZE);
      break;
   case BAD_FILE:
      this->regs_written = 0;
      break;
   case IMM:
   case UNIFORM:
      unreachable("Invalid destination register file");
   default:
      unreachable("Invalid register file");
   }

   this->writes_accumulator = false;
}

fs_inst::fs_inst()
{
   init(BRW_OPCODE_NOP, 8, dst, NULL, 0);
}

fs_inst::fs_inst(enum opcode opcode, uint8_t exec_size)
{
   init(opcode, exec_size, reg_undef, NULL, 0);
}

fs_inst::fs_inst(enum opcode opcode, uint8_t exec_size, const fs_reg &dst)
{
   init(opcode, exec_size, dst, NULL, 0);
}

fs_inst::fs_inst(enum opcode opcode, uint8_t exec_size, const fs_reg &dst,
                 const fs_reg &src0)
{
   const fs_reg src[1] = { src0 };
   init(opcode, exec_size, dst, src, 1);
}

fs_inst::fs_inst(enum opcode opcode, uint8_t exec_size, const fs_reg &dst,
                 const fs_reg &src0, const fs_reg &src1)
{
   const fs_reg src[2] = { src0, src1 };
   init(opcode, exec_size, dst, src, 2);
}

fs_inst::fs_inst(enum opcode opcode, uint8_t exec_size, const fs_reg &dst,
                 const fs_reg &src0, const fs_reg &src1, const fs_reg &src2)
{
   const fs_reg src[3] = { src0, src1, src2 };
   init(opcode, exec_size, dst, src, 3);
}

fs_inst::fs_inst(enum opcode opcode, uint8_t exec_width, const fs_reg &dst,
                 const fs_reg src[], unsigned sources)
{
   init(opcode, exec_width, dst, src, sources);
}

fs_inst::fs_inst(const fs_inst &that)
{
   memcpy(this, &that, sizeof(that));

   this->src = new fs_reg[MAX2(that.sources, 3)];

   for (unsigned i = 0; i < that.sources; i++)
      this->src[i] = that.src[i];
}

fs_inst::~fs_inst()
{
   delete[] this->src;
}

void
fs_inst::resize_sources(uint8_t num_sources)
{
   if (this->sources != num_sources) {
      fs_reg *src = new fs_reg[MAX2(num_sources, 3)];

      for (unsigned i = 0; i < MIN2(this->sources, num_sources); ++i)
         src[i] = this->src[i];

      delete[] this->src;
      this->src = src;
      this->sources = num_sources;
   }
}

void
fs_visitor::VARYING_PULL_CONSTANT_LOAD(const fs_builder &bld,
                                       const fs_reg &dst,
                                       const fs_reg &surf_index,
                                       const fs_reg &varying_offset,
                                       uint32_t const_offset)
{
   /* We have our constant surface use a pitch of 4 bytes, so our index can
    * be any component of a vector, and then we load 4 contiguous
    * components starting from that.
    *
    * We break down the const_offset to a portion added to the variable
    * offset and a portion done using reg_offset, which means that if you
    * have GLSL using something like "uniform vec4 a[20]; gl_FragColor =
    * a[i]", we'll temporarily generate 4 vec4 loads from offset i * 4, and
    * CSE can later notice that those loads are all the same and eliminate
    * the redundant ones.
    */
   fs_reg vec4_offset = vgrf(glsl_type::int_type);
   bld.ADD(vec4_offset, varying_offset, fs_reg(const_offset & ~3));

   int scale = 1;
   if (devinfo->gen == 4 && bld.dispatch_width() == 8) {
      /* Pre-gen5, we can either use a SIMD8 message that requires (header,
       * u, v, r) as parameters, or we can just use the SIMD16 message
       * consisting of (header, u).  We choose the second, at the cost of a
       * longer return length.
       */
      scale = 2;
   }

   enum opcode op;
   if (devinfo->gen >= 7)
      op = FS_OPCODE_VARYING_PULL_CONSTANT_LOAD_GEN7;
   else
      op = FS_OPCODE_VARYING_PULL_CONSTANT_LOAD;

   int regs_written = 4 * (bld.dispatch_width() / 8) * scale;
   fs_reg vec4_result = fs_reg(GRF, alloc.allocate(regs_written), dst.type);
   fs_inst *inst = bld.emit(op, vec4_result, surf_index, vec4_offset);
   inst->regs_written = regs_written;

   if (devinfo->gen < 7) {
      inst->base_mrf = 13;
      inst->header_size = 1;
      if (devinfo->gen == 4)
         inst->mlen = 3;
      else
         inst->mlen = 1 + bld.dispatch_width() / 8;
   }

   bld.MOV(dst, offset(vec4_result, bld, (const_offset & 3) * scale));
}

/**
 * A helper for MOV generation for fixing up broken hardware SEND dependency
 * handling.
 */
void
fs_visitor::DEP_RESOLVE_MOV(const fs_builder &bld, int grf)
{
   /* The caller always wants uncompressed to emit the minimal extra
    * dependencies, and to avoid having to deal with aligning its regs to 2.
    */
   const fs_builder ubld = bld.annotate("send dependency resolve")
                              .half(0);

   ubld.MOV(ubld.null_reg_f(), fs_reg(GRF, grf, BRW_REGISTER_TYPE_F));
}

bool
fs_inst::equals(fs_inst *inst) const
{
   return (opcode == inst->opcode &&
           dst.equals(inst->dst) &&
           src[0].equals(inst->src[0]) &&
           src[1].equals(inst->src[1]) &&
           src[2].equals(inst->src[2]) &&
           saturate == inst->saturate &&
           predicate == inst->predicate &&
           conditional_mod == inst->conditional_mod &&
           mlen == inst->mlen &&
           base_mrf == inst->base_mrf &&
           target == inst->target &&
           eot == inst->eot &&
           header_size == inst->header_size &&
           shadow_compare == inst->shadow_compare &&
           exec_size == inst->exec_size &&
           offset == inst->offset);
}

bool
fs_inst::overwrites_reg(const fs_reg &reg) const
{
   return reg.in_range(dst, regs_written);
}

bool
fs_inst::is_send_from_grf() const
{
   switch (opcode) {
   case FS_OPCODE_VARYING_PULL_CONSTANT_LOAD_GEN7:
   case SHADER_OPCODE_SHADER_TIME_ADD:
   case FS_OPCODE_INTERPOLATE_AT_CENTROID:
   case FS_OPCODE_INTERPOLATE_AT_SAMPLE:
   case FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET:
   case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET:
   case SHADER_OPCODE_UNTYPED_ATOMIC:
   case SHADER_OPCODE_UNTYPED_SURFACE_READ:
   case SHADER_OPCODE_UNTYPED_SURFACE_WRITE:
   case SHADER_OPCODE_TYPED_ATOMIC:
   case SHADER_OPCODE_TYPED_SURFACE_READ:
   case SHADER_OPCODE_TYPED_SURFACE_WRITE:
   case SHADER_OPCODE_URB_WRITE_SIMD8:
      return true;
   case FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD:
      return src[1].file == GRF;
   case FS_OPCODE_FB_WRITE:
      return src[0].file == GRF;
   default:
      if (is_tex())
         return src[0].file == GRF;

      return false;
   }
}

bool
fs_inst::is_copy_payload(const brw::simple_allocator &grf_alloc) const
{
   if (this->opcode != SHADER_OPCODE_LOAD_PAYLOAD)
      return false;

   fs_reg reg = this->src[0];
   if (reg.file != GRF || reg.reg_offset != 0 || reg.stride == 0)
      return false;

   if (grf_alloc.sizes[reg.reg] != this->regs_written)
      return false;

   for (int i = 0; i < this->sources; i++) {
      reg.type = this->src[i].type;
      if (!this->src[i].equals(reg))
         return false;

      if (i < this->header_size) {
         reg.reg_offset += 1;
      } else {
         reg.reg_offset += this->exec_size / 8;
      }
   }

   return true;
}

bool
fs_inst::can_do_source_mods(const struct brw_device_info *devinfo)
{
   if (devinfo->gen == 6 && is_math())
      return false;

   if (is_send_from_grf())
      return false;

   if (!backend_instruction::can_do_source_mods())
      return false;

   return true;
}

bool
fs_inst::has_side_effects() const
{
   return this->eot || backend_instruction::has_side_effects();
}

void
fs_reg::init()
{
   memset(this, 0, sizeof(*this));
   stride = 1;
}

/** Generic unset register constructor. */
fs_reg::fs_reg()
{
   init();
   this->file = BAD_FILE;
}

/** Immediate value constructor. */
fs_reg::fs_reg(float f)
{
   init();
   this->file = IMM;
   this->type = BRW_REGISTER_TYPE_F;
   this->stride = 0;
   this->fixed_hw_reg.dw1.f = f;
}

/** Immediate value constructor. */
fs_reg::fs_reg(int32_t i)
{
   init();
   this->file = IMM;
   this->type = BRW_REGISTER_TYPE_D;
   this->stride = 0;
   this->fixed_hw_reg.dw1.d = i;
}

/** Immediate value constructor. */
fs_reg::fs_reg(uint32_t u)
{
   init();
   this->file = IMM;
   this->type = BRW_REGISTER_TYPE_UD;
   this->stride = 0;
   this->fixed_hw_reg.dw1.ud = u;
}

/** Vector float immediate value constructor. */
fs_reg::fs_reg(uint8_t vf[4])
{
   init();
   this->file = IMM;
   this->type = BRW_REGISTER_TYPE_VF;
   memcpy(&this->fixed_hw_reg.dw1.ud, vf, sizeof(unsigned));
}

/** Vector float immediate value constructor. */
fs_reg::fs_reg(uint8_t vf0, uint8_t vf1, uint8_t vf2, uint8_t vf3)
{
   init();
   this->file = IMM;
   this->type = BRW_REGISTER_TYPE_VF;
   this->fixed_hw_reg.dw1.ud = (vf0 <<  0) |
                               (vf1 <<  8) |
                               (vf2 << 16) |
                               (vf3 << 24);
}

/** Fixed brw_reg. */
fs_reg::fs_reg(struct brw_reg fixed_hw_reg)
{
   init();
   this->file = HW_REG;
   this->fixed_hw_reg = fixed_hw_reg;
   this->type = fixed_hw_reg.type;
}

bool
fs_reg::equals(const fs_reg &r) const
{
   return (file == r.file &&
           reg == r.reg &&
           reg_offset == r.reg_offset &&
           subreg_offset == r.subreg_offset &&
           type == r.type &&
           negate == r.negate &&
           abs == r.abs &&
           !reladdr && !r.reladdr &&
           memcmp(&fixed_hw_reg, &r.fixed_hw_reg, sizeof(fixed_hw_reg)) == 0 &&
           stride == r.stride);
}

fs_reg &
fs_reg::set_smear(unsigned subreg)
{
   assert(file != HW_REG && file != IMM);
   subreg_offset = subreg * type_sz(type);
   stride = 0;
   return *this;
}

bool
fs_reg::is_contiguous() const
{
   return stride == 1;
}

unsigned
fs_reg::component_size(unsigned width) const
{
   const unsigned stride = (file != HW_REG ? this->stride :
                            fixed_hw_reg.hstride == 0 ? 0 :
                            1 << (fixed_hw_reg.hstride - 1));
   return MAX2(width * stride, 1) * type_sz(type);
}

int
fs_visitor::type_size(const struct glsl_type *type)
{
   unsigned int size, i;

   switch (type->base_type) {
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_INT:
   case GLSL_TYPE_FLOAT:
   case GLSL_TYPE_BOOL:
      return type->components();
   case GLSL_TYPE_ARRAY:
      return type_size(type->fields.array) * type->length;
   case GLSL_TYPE_STRUCT:
      size = 0;
      for (i = 0; i < type->length; i++) {
	 size += type_size(type->fields.structure[i].type);
      }
      return size;
   case GLSL_TYPE_SAMPLER:
      /* Samplers take up no register space, since they're baked in at
       * link time.
       */
      return 0;
   case GLSL_TYPE_ATOMIC_UINT:
      return 0;
   case GLSL_TYPE_SUBROUTINE:
      return 1;
   case GLSL_TYPE_IMAGE:
      return BRW_IMAGE_PARAM_SIZE;
   case GLSL_TYPE_VOID:
   case GLSL_TYPE_ERROR:
   case GLSL_TYPE_INTERFACE:
   case GLSL_TYPE_DOUBLE:
      unreachable("not reached");
   }

   return 0;
}

/**
 * Create a MOV to read the timestamp register.
 *
 * The caller is responsible for emitting the MOV.  The return value is
 * the destination of the MOV, with extra parameters set.
 */
fs_reg
fs_visitor::get_timestamp(const fs_builder &bld)
{
   assert(devinfo->gen >= 7);

   fs_reg ts = fs_reg(retype(brw_vec4_reg(BRW_ARCHITECTURE_REGISTER_FILE,
                                          BRW_ARF_TIMESTAMP,
                                          0),
                             BRW_REGISTER_TYPE_UD));

   fs_reg dst = fs_reg(GRF, alloc.allocate(1), BRW_REGISTER_TYPE_UD);

   /* We want to read the 3 fields we care about even if it's not enabled in
    * the dispatch.
    */
   bld.group(4, 0).exec_all().MOV(dst, ts);

   /* The caller wants the low 32 bits of the timestamp.  Since it's running
    * at the GPU clock rate of ~1.2ghz, it will roll over every ~3 seconds,
    * which is plenty of time for our purposes.  It is identical across the
    * EUs, but since it's tracking GPU core speed it will increment at a
    * varying rate as render P-states change.
    *
    * The caller could also check if render P-states have changed (or anything
    * else that might disrupt timing) by setting smear to 2 and checking if
    * that field is != 0.
    */
   dst.set_smear(0);

   return dst;
}

void
fs_visitor::emit_shader_time_begin()
{
   shader_start_time = get_timestamp(bld.annotate("shader time start"));
}

void
fs_visitor::emit_shader_time_end()
{
   /* Insert our code just before the final SEND with EOT. */
   exec_node *end = this->instructions.get_tail();
   assert(end && ((fs_inst *) end)->eot);
   const fs_builder ibld = bld.annotate("shader time end")
                              .exec_all().at(NULL, end);

   fs_reg shader_end_time = get_timestamp(ibld);

   /* Check that there weren't any timestamp reset events (assuming these
    * were the only two timestamp reads that happened).
    */
   fs_reg reset = shader_end_time;
   reset.set_smear(2);
   set_condmod(BRW_CONDITIONAL_Z,
               ibld.AND(ibld.null_reg_ud(), reset, fs_reg(1u)));
   ibld.IF(BRW_PREDICATE_NORMAL);

   fs_reg start = shader_start_time;
   start.negate = true;
   fs_reg diff = fs_reg(GRF, alloc.allocate(1), BRW_REGISTER_TYPE_UD);
   diff.set_smear(0);

   const fs_builder cbld = ibld.group(1, 0);
   cbld.group(1, 0).ADD(diff, start, shader_end_time);

   /* If there were no instructions between the two timestamp gets, the diff
    * is 2 cycles.  Remove that overhead, so I can forget about that when
    * trying to determine the time taken for single instructions.
    */
   cbld.ADD(diff, diff, fs_reg(-2u));
   SHADER_TIME_ADD(cbld, 0, diff);
   SHADER_TIME_ADD(cbld, 1, fs_reg(1u));
   ibld.emit(BRW_OPCODE_ELSE);
   SHADER_TIME_ADD(cbld, 2, fs_reg(1u));
   ibld.emit(BRW_OPCODE_ENDIF);
}

void
fs_visitor::SHADER_TIME_ADD(const fs_builder &bld,
                            int shader_time_subindex,
                            fs_reg value)
{
   int index = shader_time_index * 3 + shader_time_subindex;
   fs_reg offset = fs_reg(index * SHADER_TIME_STRIDE);

   fs_reg payload;
   if (dispatch_width == 8)
      payload = vgrf(glsl_type::uvec2_type);
   else
      payload = vgrf(glsl_type::uint_type);

   bld.emit(SHADER_OPCODE_SHADER_TIME_ADD, fs_reg(), payload, offset, value);
}

void
fs_visitor::vfail(const char *format, va_list va)
{
   char *msg;

   if (failed)
      return;

   failed = true;

   msg = ralloc_vasprintf(mem_ctx, format, va);
   msg = ralloc_asprintf(mem_ctx, "%s compile failed: %s\n", stage_abbrev, msg);

   this->fail_msg = msg;

   if (debug_enabled) {
      fprintf(stderr, "%s",  msg);
   }
}

void
fs_visitor::fail(const char *format, ...)
{
   va_list va;

   va_start(va, format);
   vfail(format, va);
   va_end(va);
}

/**
 * Mark this program as impossible to compile in SIMD16 mode.
 *
 * During the SIMD8 compile (which happens first), we can detect and flag
 * things that are unsupported in SIMD16 mode, so the compiler can skip
 * the SIMD16 compile altogether.
 *
 * During a SIMD16 compile (if one happens anyway), this just calls fail().
 */
void
fs_visitor::no16(const char *msg)
{
   if (dispatch_width == 16) {
      fail("%s", msg);
   } else {
      simd16_unsupported = true;

      compiler->shader_perf_log(log_data,
                                "SIMD16 shader failed to compile: %s", msg);
   }
}

/**
 * Returns true if the instruction has a flag that means it won't
 * update an entire destination register.
 *
 * For example, dead code elimination and live variable analysis want to know
 * when a write to a variable screens off any preceding values that were in
 * it.
 */
bool
fs_inst::is_partial_write() const
{
   return ((this->predicate && this->opcode != BRW_OPCODE_SEL) ||
           (this->exec_size * type_sz(this->dst.type)) < 32 ||
           !this->dst.is_contiguous());
}

unsigned
fs_inst::components_read(unsigned i) const
{
   switch (opcode) {
   case FS_OPCODE_LINTERP:
      if (i == 0)
         return 2;
      else
         return 1;

   case FS_OPCODE_PIXEL_X:
   case FS_OPCODE_PIXEL_Y:
      assert(i == 0);
      return 2;

   case FS_OPCODE_FB_WRITE_LOGICAL:
      assert(src[6].file == IMM);
      /* First/second FB write color. */
      if (i < 2)
         return src[6].fixed_hw_reg.dw1.ud;
      else
         return 1;

   case SHADER_OPCODE_TEX_LOGICAL:
   case SHADER_OPCODE_TXD_LOGICAL:
   case SHADER_OPCODE_TXF_LOGICAL:
   case SHADER_OPCODE_TXL_LOGICAL:
   case SHADER_OPCODE_TXS_LOGICAL:
   case FS_OPCODE_TXB_LOGICAL:
   case SHADER_OPCODE_TXF_CMS_LOGICAL:
   case SHADER_OPCODE_TXF_UMS_LOGICAL:
   case SHADER_OPCODE_TXF_MCS_LOGICAL:
   case SHADER_OPCODE_LOD_LOGICAL:
   case SHADER_OPCODE_TG4_LOGICAL:
   case SHADER_OPCODE_TG4_OFFSET_LOGICAL:
      assert(src[8].file == IMM && src[9].file == IMM);
      /* Texture coordinates. */
      if (i == 0)
         return src[8].fixed_hw_reg.dw1.ud;
      /* Texture derivatives. */
      else if ((i == 2 || i == 3) && opcode == SHADER_OPCODE_TXD_LOGICAL)
         return src[9].fixed_hw_reg.dw1.ud;
      /* Texture offset. */
      else if (i == 7)
         return 2;
      else
         return 1;

   case SHADER_OPCODE_UNTYPED_SURFACE_READ_LOGICAL:
   case SHADER_OPCODE_TYPED_SURFACE_READ_LOGICAL:
      assert(src[3].file == IMM);
      /* Surface coordinates. */
      if (i == 0)
         return src[3].fixed_hw_reg.dw1.ud;
      /* Surface operation source (ignored for reads). */
      else if (i == 1)
         return 0;
      else
         return 1;

   case SHADER_OPCODE_UNTYPED_SURFACE_WRITE_LOGICAL:
   case SHADER_OPCODE_TYPED_SURFACE_WRITE_LOGICAL:
      assert(src[3].file == IMM &&
             src[4].file == IMM);
      /* Surface coordinates. */
      if (i == 0)
         return src[3].fixed_hw_reg.dw1.ud;
      /* Surface operation source. */
      else if (i == 1)
         return src[4].fixed_hw_reg.dw1.ud;
      else
         return 1;

   case SHADER_OPCODE_UNTYPED_ATOMIC_LOGICAL:
   case SHADER_OPCODE_TYPED_ATOMIC_LOGICAL: {
      assert(src[3].file == IMM &&
             src[4].file == IMM);
      const unsigned op = src[4].fixed_hw_reg.dw1.ud;
      /* Surface coordinates. */
      if (i == 0)
         return src[3].fixed_hw_reg.dw1.ud;
      /* Surface operation source. */
      else if (i == 1 && op == BRW_AOP_CMPWR)
         return 2;
      else if (i == 1 && (op == BRW_AOP_INC || op == BRW_AOP_DEC ||
                          op == BRW_AOP_PREDEC))
         return 0;
      else
         return 1;
   }

   default:
      return 1;
   }
}

int
fs_inst::regs_read(int arg) const
{
   switch (opcode) {
   case FS_OPCODE_FB_WRITE:
   case SHADER_OPCODE_URB_WRITE_SIMD8:
   case SHADER_OPCODE_UNTYPED_ATOMIC:
   case SHADER_OPCODE_UNTYPED_SURFACE_READ:
   case SHADER_OPCODE_UNTYPED_SURFACE_WRITE:
   case SHADER_OPCODE_TYPED_ATOMIC:
   case SHADER_OPCODE_TYPED_SURFACE_READ:
   case SHADER_OPCODE_TYPED_SURFACE_WRITE:
   case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET:
      if (arg == 0)
         return mlen;
      break;

   case FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD_GEN7:
      /* The payload is actually stored in src1 */
      if (arg == 1)
         return mlen;
      break;

   case FS_OPCODE_LINTERP:
      if (arg == 1)
         return 1;
      break;

   case SHADER_OPCODE_LOAD_PAYLOAD:
      if (arg < this->header_size)
         return 1;
      break;

   case CS_OPCODE_CS_TERMINATE:
      return 1;

   default:
      if (is_tex() && arg == 0 && src[0].file == GRF)
         return mlen;
      break;
   }

   switch (src[arg].file) {
   case BAD_FILE:
      return 0;
   case UNIFORM:
   case IMM:
      return 1;
   case GRF:
   case ATTR:
   case HW_REG:
      return DIV_ROUND_UP(components_read(arg) *
                          src[arg].component_size(exec_size),
                          REG_SIZE);
   case MRF:
      unreachable("MRF registers are not allowed as sources");
   default:
      unreachable("Invalid register file");
   }
}

bool
fs_inst::reads_flag() const
{
   return predicate;
}

bool
fs_inst::writes_flag() const
{
   return (conditional_mod && (opcode != BRW_OPCODE_SEL &&
                               opcode != BRW_OPCODE_IF &&
                               opcode != BRW_OPCODE_WHILE)) ||
          opcode == FS_OPCODE_MOV_DISPATCH_TO_FLAGS;
}

/**
 * Returns how many MRFs an FS opcode will write over.
 *
 * Note that this is not the 0 or 1 implied writes in an actual gen
 * instruction -- the FS opcodes often generate MOVs in addition.
 */
int
fs_visitor::implied_mrf_writes(fs_inst *inst)
{
   if (inst->mlen == 0)
      return 0;

   if (inst->base_mrf == -1)
      return 0;

   switch (inst->opcode) {
   case SHADER_OPCODE_RCP:
   case SHADER_OPCODE_RSQ:
   case SHADER_OPCODE_SQRT:
   case SHADER_OPCODE_EXP2:
   case SHADER_OPCODE_LOG2:
   case SHADER_OPCODE_SIN:
   case SHADER_OPCODE_COS:
      return 1 * dispatch_width / 8;
   case SHADER_OPCODE_POW:
   case SHADER_OPCODE_INT_QUOTIENT:
   case SHADER_OPCODE_INT_REMAINDER:
      return 2 * dispatch_width / 8;
   case SHADER_OPCODE_TEX:
   case FS_OPCODE_TXB:
   case SHADER_OPCODE_TXD:
   case SHADER_OPCODE_TXF:
   case SHADER_OPCODE_TXF_CMS:
   case SHADER_OPCODE_TXF_MCS:
   case SHADER_OPCODE_TG4:
   case SHADER_OPCODE_TG4_OFFSET:
   case SHADER_OPCODE_TXL:
   case SHADER_OPCODE_TXS:
   case SHADER_OPCODE_LOD:
      return 1;
   case FS_OPCODE_FB_WRITE:
      return 2;
   case FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD:
   case SHADER_OPCODE_GEN4_SCRATCH_READ:
      return 1;
   case FS_OPCODE_VARYING_PULL_CONSTANT_LOAD:
      return inst->mlen;
   case SHADER_OPCODE_GEN4_SCRATCH_WRITE:
      return inst->mlen;
   case SHADER_OPCODE_UNTYPED_ATOMIC:
   case SHADER_OPCODE_UNTYPED_SURFACE_READ:
   case SHADER_OPCODE_UNTYPED_SURFACE_WRITE:
   case SHADER_OPCODE_TYPED_ATOMIC:
   case SHADER_OPCODE_TYPED_SURFACE_READ:
   case SHADER_OPCODE_TYPED_SURFACE_WRITE:
   case SHADER_OPCODE_URB_WRITE_SIMD8:
   case FS_OPCODE_INTERPOLATE_AT_CENTROID:
   case FS_OPCODE_INTERPOLATE_AT_SAMPLE:
   case FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET:
   case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET:
      return 0;
   default:
      unreachable("not reached");
   }
}

fs_reg
fs_visitor::vgrf(const glsl_type *const type)
{
   int reg_width = dispatch_width / 8;
   return fs_reg(GRF, alloc.allocate(type_size(type) * reg_width),
                 brw_type_for_base_type(type));
}

/** Fixed HW reg constructor. */
fs_reg::fs_reg(enum register_file file, int reg)
{
   init();
   this->file = file;
   this->reg = reg;
   this->type = BRW_REGISTER_TYPE_F;
   this->stride = (file == UNIFORM ? 0 : 1);
}

/** Fixed HW reg constructor. */
fs_reg::fs_reg(enum register_file file, int reg, enum brw_reg_type type)
{
   init();
   this->file = file;
   this->reg = reg;
   this->type = type;
   this->stride = (file == UNIFORM ? 0 : 1);
}

/* For SIMD16, we need to follow from the uniform setup of SIMD8 dispatch.
 * This brings in those uniform definitions
 */
void
fs_visitor::import_uniforms(fs_visitor *v)
{
   this->push_constant_loc = v->push_constant_loc;
   this->pull_constant_loc = v->pull_constant_loc;
   this->uniforms = v->uniforms;
   this->param_size = v->param_size;
}

void
fs_visitor::setup_vector_uniform_values(const gl_constant_value *values, unsigned n)
{
   static const gl_constant_value zero = { 0 };

   for (unsigned i = 0; i < n; ++i)
      stage_prog_data->param[uniforms++] = &values[i];

   for (unsigned i = n; i < 4; ++i)
      stage_prog_data->param[uniforms++] = &zero;
}

fs_reg *
fs_visitor::emit_fragcoord_interpolation(bool pixel_center_integer,
                                         bool origin_upper_left)
{
   assert(stage == MESA_SHADER_FRAGMENT);
   brw_wm_prog_key *key = (brw_wm_prog_key*) this->key;
   fs_reg *reg = new(this->mem_ctx) fs_reg(vgrf(glsl_type::vec4_type));
   fs_reg wpos = *reg;
   bool flip = !origin_upper_left ^ key->render_to_fbo;

   /* gl_FragCoord.x */
   if (pixel_center_integer) {
      bld.MOV(wpos, this->pixel_x);
   } else {
      bld.ADD(wpos, this->pixel_x, fs_reg(0.5f));
   }
   wpos = offset(wpos, bld, 1);

   /* gl_FragCoord.y */
   if (!flip && pixel_center_integer) {
      bld.MOV(wpos, this->pixel_y);
   } else {
      fs_reg pixel_y = this->pixel_y;
      float offset = (pixel_center_integer ? 0.0f : 0.5f);

      if (flip) {
	 pixel_y.negate = true;
	 offset += key->drawable_height - 1.0f;
      }

      bld.ADD(wpos, pixel_y, fs_reg(offset));
   }
   wpos = offset(wpos, bld, 1);

   /* gl_FragCoord.z */
   if (devinfo->gen >= 6) {
      bld.MOV(wpos, fs_reg(brw_vec8_grf(payload.source_depth_reg, 0)));
   } else {
      bld.emit(FS_OPCODE_LINTERP, wpos,
           this->delta_xy[BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC],
           interp_reg(VARYING_SLOT_POS, 2));
   }
   wpos = offset(wpos, bld, 1);

   /* gl_FragCoord.w: Already set up in emit_interpolation */
   bld.MOV(wpos, this->wpos_w);

   return reg;
}

fs_inst *
fs_visitor::emit_linterp(const fs_reg &attr, const fs_reg &interp,
                         glsl_interp_qualifier interpolation_mode,
                         bool is_centroid, bool is_sample)
{
   brw_wm_barycentric_interp_mode barycoord_mode;
   if (devinfo->gen >= 6) {
      if (is_centroid) {
         if (interpolation_mode == INTERP_QUALIFIER_SMOOTH)
            barycoord_mode = BRW_WM_PERSPECTIVE_CENTROID_BARYCENTRIC;
         else
            barycoord_mode = BRW_WM_NONPERSPECTIVE_CENTROID_BARYCENTRIC;
      } else if (is_sample) {
          if (interpolation_mode == INTERP_QUALIFIER_SMOOTH)
            barycoord_mode = BRW_WM_PERSPECTIVE_SAMPLE_BARYCENTRIC;
         else
            barycoord_mode = BRW_WM_NONPERSPECTIVE_SAMPLE_BARYCENTRIC;
      } else {
         if (interpolation_mode == INTERP_QUALIFIER_SMOOTH)
            barycoord_mode = BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC;
         else
            barycoord_mode = BRW_WM_NONPERSPECTIVE_PIXEL_BARYCENTRIC;
      }
   } else {
      /* On Ironlake and below, there is only one interpolation mode.
       * Centroid interpolation doesn't mean anything on this hardware --
       * there is no multisampling.
       */
      barycoord_mode = BRW_WM_PERSPECTIVE_PIXEL_BARYCENTRIC;
   }
   return bld.emit(FS_OPCODE_LINTERP, attr,
                   this->delta_xy[barycoord_mode], interp);
}

void
fs_visitor::emit_general_interpolation(fs_reg attr, const char *name,
                                       const glsl_type *type,
                                       glsl_interp_qualifier interpolation_mode,
                                       int location, bool mod_centroid,
                                       bool mod_sample)
{
   attr.type = brw_type_for_base_type(type->get_scalar_type());

   assert(stage == MESA_SHADER_FRAGMENT);
   brw_wm_prog_data *prog_data = (brw_wm_prog_data*) this->prog_data;
   brw_wm_prog_key *key = (brw_wm_prog_key*) this->key;

   unsigned int array_elements;

   if (type->is_array()) {
      array_elements = type->length;
      if (array_elements == 0) {
         fail("dereferenced array '%s' has length 0\n", name);
      }
      type = type->fields.array;
   } else {
      array_elements = 1;
   }

   if (interpolation_mode == INTERP_QUALIFIER_NONE) {
      bool is_gl_Color =
         location == VARYING_SLOT_COL0 || location == VARYING_SLOT_COL1;
      if (key->flat_shade && is_gl_Color) {
         interpolation_mode = INTERP_QUALIFIER_FLAT;
      } else {
         interpolation_mode = INTERP_QUALIFIER_SMOOTH;
      }
   }

   for (unsigned int i = 0; i < array_elements; i++) {
      for (unsigned int j = 0; j < type->matrix_columns; j++) {
	 if (prog_data->urb_setup[location] == -1) {
	    /* If there's no incoming setup data for this slot, don't
	     * emit interpolation for it.
	     */
	    attr = offset(attr, bld, type->vector_elements);
	    location++;
	    continue;
	 }

	 if (interpolation_mode == INTERP_QUALIFIER_FLAT) {
	    /* Constant interpolation (flat shading) case. The SF has
	     * handed us defined values in only the constant offset
	     * field of the setup reg.
	     */
	    for (unsigned int k = 0; k < type->vector_elements; k++) {
	       struct brw_reg interp = interp_reg(location, k);
	       interp = suboffset(interp, 3);
               interp.type = attr.type;
               bld.emit(FS_OPCODE_CINTERP, attr, fs_reg(interp));
	       attr = offset(attr, bld, 1);
	    }
	 } else {
	    /* Smooth/noperspective interpolation case. */
	    for (unsigned int k = 0; k < type->vector_elements; k++) {
               struct brw_reg interp = interp_reg(location, k);
               if (devinfo->needs_unlit_centroid_workaround && mod_centroid) {
                  /* Get the pixel/sample mask into f0 so that we know
                   * which pixels are lit.  Then, for each channel that is
                   * unlit, replace the centroid data with non-centroid
                   * data.
                   */
                  bld.emit(FS_OPCODE_MOV_DISPATCH_TO_FLAGS);

                  fs_inst *inst;
                  inst = emit_linterp(attr, fs_reg(interp), interpolation_mode,
                                      false, false);
                  inst->predicate = BRW_PREDICATE_NORMAL;
                  inst->predicate_inverse = true;
                  if (devinfo->has_pln)
                     inst->no_dd_clear = true;

                  inst = emit_linterp(attr, fs_reg(interp), interpolation_mode,
                                      mod_centroid && !key->persample_shading,
                                      mod_sample || key->persample_shading);
                  inst->predicate = BRW_PREDICATE_NORMAL;
                  inst->predicate_inverse = false;
                  if (devinfo->has_pln)
                     inst->no_dd_check = true;

               } else {
                  emit_linterp(attr, fs_reg(interp), interpolation_mode,
                               mod_centroid && !key->persample_shading,
                               mod_sample || key->persample_shading);
               }
               if (devinfo->gen < 6 && interpolation_mode == INTERP_QUALIFIER_SMOOTH) {
                  bld.MUL(attr, attr, this->pixel_w);
               }
	       attr = offset(attr, bld, 1);
	    }

	 }
	 location++;
      }
   }
}

fs_reg *
fs_visitor::emit_frontfacing_interpolation()
{
   fs_reg *reg = new(this->mem_ctx) fs_reg(vgrf(glsl_type::bool_type));

   if (devinfo->gen >= 6) {
      /* Bit 15 of g0.0 is 0 if the polygon is front facing. We want to create
       * a boolean result from this (~0/true or 0/false).
       *
       * We can use the fact that bit 15 is the MSB of g0.0:W to accomplish
       * this task in only one instruction:
       *    - a negation source modifier will flip the bit; and
       *    - a W -> D type conversion will sign extend the bit into the high
       *      word of the destination.
       *
       * An ASR 15 fills the low word of the destination.
       */
      fs_reg g0 = fs_reg(retype(brw_vec1_grf(0, 0), BRW_REGISTER_TYPE_W));
      g0.negate = true;

      bld.ASR(*reg, g0, fs_reg(15));
   } else {
      /* Bit 31 of g1.6 is 0 if the polygon is front facing. We want to create
       * a boolean result from this (1/true or 0/false).
       *
       * Like in the above case, since the bit is the MSB of g1.6:UD we can use
       * the negation source modifier to flip it. Unfortunately the SHR
       * instruction only operates on UD (or D with an abs source modifier)
       * sources without negation.
       *
       * Instead, use ASR (which will give ~0/true or 0/false).
       */
      fs_reg g1_6 = fs_reg(retype(brw_vec1_grf(1, 6), BRW_REGISTER_TYPE_D));
      g1_6.negate = true;

      bld.ASR(*reg, g1_6, fs_reg(31));
   }

   return reg;
}

void
fs_visitor::compute_sample_position(fs_reg dst, fs_reg int_sample_pos)
{
   assert(stage == MESA_SHADER_FRAGMENT);
   brw_wm_prog_key *key = (brw_wm_prog_key*) this->key;
   assert(dst.type == BRW_REGISTER_TYPE_F);

   if (key->compute_pos_offset) {
      /* Convert int_sample_pos to floating point */
      bld.MOV(dst, int_sample_pos);
      /* Scale to the range [0, 1] */
      bld.MUL(dst, dst, fs_reg(1 / 16.0f));
   }
   else {
      /* From ARB_sample_shading specification:
       * "When rendering to a non-multisample buffer, or if multisample
       *  rasterization is disabled, gl_SamplePosition will always be
       *  (0.5, 0.5).
       */
      bld.MOV(dst, fs_reg(0.5f));
   }
}

fs_reg *
fs_visitor::emit_samplepos_setup()
{
   assert(devinfo->gen >= 6);

   const fs_builder abld = bld.annotate("compute sample position");
   fs_reg *reg = new(this->mem_ctx) fs_reg(vgrf(glsl_type::vec2_type));
   fs_reg pos = *reg;
   fs_reg int_sample_x = vgrf(glsl_type::int_type);
   fs_reg int_sample_y = vgrf(glsl_type::int_type);

   /* WM will be run in MSDISPMODE_PERSAMPLE. So, only one of SIMD8 or SIMD16
    * mode will be enabled.
    *
    * From the Ivy Bridge PRM, volume 2 part 1, page 344:
    * R31.1:0         Position Offset X/Y for Slot[3:0]
    * R31.3:2         Position Offset X/Y for Slot[7:4]
    * .....
    *
    * The X, Y sample positions come in as bytes in  thread payload. So, read
    * the positions using vstride=16, width=8, hstride=2.
    */
   struct brw_reg sample_pos_reg =
      stride(retype(brw_vec1_grf(payload.sample_pos_reg, 0),
                    BRW_REGISTER_TYPE_B), 16, 8, 2);

   if (dispatch_width == 8) {
      abld.MOV(int_sample_x, fs_reg(sample_pos_reg));
   } else {
      abld.half(0).MOV(half(int_sample_x, 0), fs_reg(sample_pos_reg));
      abld.half(1).MOV(half(int_sample_x, 1),
                       fs_reg(suboffset(sample_pos_reg, 16)));
   }
   /* Compute gl_SamplePosition.x */
   compute_sample_position(pos, int_sample_x);
   pos = offset(pos, abld, 1);
   if (dispatch_width == 8) {
      abld.MOV(int_sample_y, fs_reg(suboffset(sample_pos_reg, 1)));
   } else {
      abld.half(0).MOV(half(int_sample_y, 0),
                       fs_reg(suboffset(sample_pos_reg, 1)));
      abld.half(1).MOV(half(int_sample_y, 1),
                       fs_reg(suboffset(sample_pos_reg, 17)));
   }
   /* Compute gl_SamplePosition.y */
   compute_sample_position(pos, int_sample_y);
   return reg;
}

fs_reg *
fs_visitor::emit_sampleid_setup()
{
   assert(stage == MESA_SHADER_FRAGMENT);
   brw_wm_prog_key *key = (brw_wm_prog_key*) this->key;
   assert(devinfo->gen >= 6);

   const fs_builder abld = bld.annotate("compute sample id");
   fs_reg *reg = new(this->mem_ctx) fs_reg(vgrf(glsl_type::int_type));

   if (key->compute_sample_id) {
      fs_reg t1 = vgrf(glsl_type::int_type);
      fs_reg t2 = vgrf(glsl_type::int_type);
      t2.type = BRW_REGISTER_TYPE_UW;

      /* The PS will be run in MSDISPMODE_PERSAMPLE. For example with
       * 8x multisampling, subspan 0 will represent sample N (where N
       * is 0, 2, 4 or 6), subspan 1 will represent sample 1, 3, 5 or
       * 7. We can find the value of N by looking at R0.0 bits 7:6
       * ("Starting Sample Pair Index (SSPI)") and multiplying by two
       * (since samples are always delivered in pairs). That is, we
       * compute 2*((R0.0 & 0xc0) >> 6) == (R0.0 & 0xc0) >> 5. Then
       * we need to add N to the sequence (0, 0, 0, 0, 1, 1, 1, 1) in
       * case of SIMD8 and sequence (0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
       * 2, 3, 3, 3, 3) in case of SIMD16. We compute this sequence by
       * populating a temporary variable with the sequence (0, 1, 2, 3),
       * and then reading from it using vstride=1, width=4, hstride=0.
       * These computations hold good for 4x multisampling as well.
       *
       * For 2x MSAA and SIMD16, we want to use the sequence (0, 1, 0, 1):
       * the first four slots are sample 0 of subspan 0; the next four
       * are sample 1 of subspan 0; the third group is sample 0 of
       * subspan 1, and finally sample 1 of subspan 1.
       */
      abld.exec_all()
          .AND(t1, fs_reg(retype(brw_vec1_grf(0, 0), BRW_REGISTER_TYPE_UD)),
               fs_reg(0xc0));
      abld.exec_all().SHR(t1, t1, fs_reg(5));

      /* This works for both SIMD8 and SIMD16 */
      abld.exec_all()
          .MOV(t2, brw_imm_v(key->persample_2x ? 0x1010 : 0x3210));

      /* This special instruction takes care of setting vstride=1,
       * width=4, hstride=0 of t2 during an ADD instruction.
       */
      abld.emit(FS_OPCODE_SET_SAMPLE_ID, *reg, t1, t2);
   } else {
      /* As per GL_ARB_sample_shading specification:
       * "When rendering to a non-multisample buffer, or if multisample
       *  rasterization is disabled, gl_SampleID will always be zero."
       */
      abld.MOV(*reg, fs_reg(0));
   }

   return reg;
}

fs_reg
fs_visitor::resolve_source_modifiers(const fs_reg &src)
{
   if (!src.abs && !src.negate)
      return src;

   fs_reg temp = bld.vgrf(src.type);
   bld.MOV(temp, src);

   return temp;
}

void
fs_visitor::emit_discard_jump()
{
   assert(((brw_wm_prog_data*) this->prog_data)->uses_kill);

   /* For performance, after a discard, jump to the end of the
    * shader if all relevant channels have been discarded.
    */
   fs_inst *discard_jump = bld.emit(FS_OPCODE_DISCARD_JUMP);
   discard_jump->flag_subreg = 1;

   discard_jump->predicate = (dispatch_width == 8)
                             ? BRW_PREDICATE_ALIGN1_ANY8H
                             : BRW_PREDICATE_ALIGN1_ANY16H;
   discard_jump->predicate_inverse = true;
}

void
fs_visitor::assign_curb_setup()
{
   if (dispatch_width == 8) {
      prog_data->dispatch_grf_start_reg = payload.num_regs;
   } else {
      if (stage == MESA_SHADER_FRAGMENT) {
         brw_wm_prog_data *prog_data = (brw_wm_prog_data*) this->prog_data;
         prog_data->dispatch_grf_start_reg_16 = payload.num_regs;
      } else if (stage == MESA_SHADER_COMPUTE) {
         brw_cs_prog_data *prog_data = (brw_cs_prog_data*) this->prog_data;
         prog_data->dispatch_grf_start_reg_16 = payload.num_regs;
      } else {
         unreachable("Unsupported shader type!");
      }
   }

   prog_data->curb_read_length = ALIGN(stage_prog_data->nr_params, 8) / 8;

   /* Map the offsets in the UNIFORM file to fixed HW regs. */
   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      for (unsigned int i = 0; i < inst->sources; i++) {
	 if (inst->src[i].file == UNIFORM) {
            int uniform_nr = inst->src[i].reg + inst->src[i].reg_offset;
            int constant_nr;
            if (uniform_nr >= 0 && uniform_nr < (int) uniforms) {
               constant_nr = push_constant_loc[uniform_nr];
            } else {
               /* Section 5.11 of the OpenGL 4.1 spec says:
                * "Out-of-bounds reads return undefined values, which include
                *  values from other variables of the active program or zero."
                * Just return the first push constant.
                */
               constant_nr = 0;
            }

	    struct brw_reg brw_reg = brw_vec1_grf(payload.num_regs +
						  constant_nr / 8,
						  constant_nr % 8);

            assert(inst->src[i].stride == 0);
	    inst->src[i].file = HW_REG;
	    inst->src[i].fixed_hw_reg = byte_offset(
               retype(brw_reg, inst->src[i].type),
               inst->src[i].subreg_offset);
	 }
      }
   }
}

void
fs_visitor::calculate_urb_setup()
{
   assert(stage == MESA_SHADER_FRAGMENT);
   brw_wm_prog_data *prog_data = (brw_wm_prog_data*) this->prog_data;
   brw_wm_prog_key *key = (brw_wm_prog_key*) this->key;

   memset(prog_data->urb_setup, -1,
          sizeof(prog_data->urb_setup[0]) * VARYING_SLOT_MAX);

   int urb_next = 0;
   /* Figure out where each of the incoming setup attributes lands. */
   if (devinfo->gen >= 6) {
      if (_mesa_bitcount_64(prog->InputsRead &
                            BRW_FS_VARYING_INPUT_MASK) <= 16) {
         /* The SF/SBE pipeline stage can do arbitrary rearrangement of the
          * first 16 varying inputs, so we can put them wherever we want.
          * Just put them in order.
          *
          * This is useful because it means that (a) inputs not used by the
          * fragment shader won't take up valuable register space, and (b) we
          * won't have to recompile the fragment shader if it gets paired with
          * a different vertex (or geometry) shader.
          */
         for (unsigned int i = 0; i < VARYING_SLOT_MAX; i++) {
            if (prog->InputsRead & BRW_FS_VARYING_INPUT_MASK &
                BITFIELD64_BIT(i)) {
               prog_data->urb_setup[i] = urb_next++;
            }
         }
      } else {
         /* We have enough input varyings that the SF/SBE pipeline stage can't
          * arbitrarily rearrange them to suit our whim; we have to put them
          * in an order that matches the output of the previous pipeline stage
          * (geometry or vertex shader).
          */
         struct brw_vue_map prev_stage_vue_map;
         brw_compute_vue_map(devinfo, &prev_stage_vue_map,
                             key->input_slots_valid);
         int first_slot = 2 * BRW_SF_URB_ENTRY_READ_OFFSET;
         assert(prev_stage_vue_map.num_slots <= first_slot + 32);
         for (int slot = first_slot; slot < prev_stage_vue_map.num_slots;
              slot++) {
            int varying = prev_stage_vue_map.slot_to_varying[slot];
            /* Note that varying == BRW_VARYING_SLOT_COUNT when a slot is
             * unused.
             */
            if (varying != BRW_VARYING_SLOT_COUNT &&
                (prog->InputsRead & BRW_FS_VARYING_INPUT_MASK &
                 BITFIELD64_BIT(varying))) {
               prog_data->urb_setup[varying] = slot - first_slot;
            }
         }
         urb_next = prev_stage_vue_map.num_slots - first_slot;
      }
   } else {
      /* FINISHME: The sf doesn't map VS->FS inputs for us very well. */
      for (unsigned int i = 0; i < VARYING_SLOT_MAX; i++) {
         /* Point size is packed into the header, not as a general attribute */
         if (i == VARYING_SLOT_PSIZ)
            continue;

	 if (key->input_slots_valid & BITFIELD64_BIT(i)) {
	    /* The back color slot is skipped when the front color is
	     * also written to.  In addition, some slots can be
	     * written in the vertex shader and not read in the
	     * fragment shader.  So the register number must always be
	     * incremented, mapped or not.
	     */
	    if (_mesa_varying_slot_in_fs((gl_varying_slot) i))
	       prog_data->urb_setup[i] = urb_next;
            urb_next++;
	 }
      }

      /*
       * It's a FS only attribute, and we did interpolation for this attribute
       * in SF thread. So, count it here, too.
       *
       * See compile_sf_prog() for more info.
       */
      if (prog->InputsRead & BITFIELD64_BIT(VARYING_SLOT_PNTC))
         prog_data->urb_setup[VARYING_SLOT_PNTC] = urb_next++;
   }

   prog_data->num_varying_inputs = urb_next;
}

void
fs_visitor::assign_urb_setup()
{
   assert(stage == MESA_SHADER_FRAGMENT);
   brw_wm_prog_data *prog_data = (brw_wm_prog_data*) this->prog_data;

   int urb_start = payload.num_regs + prog_data->base.curb_read_length;

   /* Offset all the urb_setup[] index by the actual position of the
    * setup regs, now that the location of the constants has been chosen.
    */
   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      if (inst->opcode == FS_OPCODE_LINTERP) {
	 assert(inst->src[1].file == HW_REG);
	 inst->src[1].fixed_hw_reg.nr += urb_start;
      }

      if (inst->opcode == FS_OPCODE_CINTERP) {
	 assert(inst->src[0].file == HW_REG);
	 inst->src[0].fixed_hw_reg.nr += urb_start;
      }
   }

   /* Each attribute is 4 setup channels, each of which is half a reg. */
   this->first_non_payload_grf =
      urb_start + prog_data->num_varying_inputs * 2;
}

void
fs_visitor::assign_vs_urb_setup()
{
   brw_vs_prog_data *vs_prog_data = (brw_vs_prog_data *) prog_data;
   int grf, count, slot, channel, attr;

   assert(stage == MESA_SHADER_VERTEX);
   count = _mesa_bitcount_64(vs_prog_data->inputs_read);
   if (vs_prog_data->uses_vertexid || vs_prog_data->uses_instanceid)
      count++;

   /* Each attribute is 4 regs. */
   this->first_non_payload_grf =
      payload.num_regs + prog_data->curb_read_length + count * 4;

   unsigned vue_entries =
      MAX2(count, vs_prog_data->base.vue_map.num_slots);

   vs_prog_data->base.urb_entry_size = ALIGN(vue_entries, 4) / 4;
   vs_prog_data->base.urb_read_length = (count + 1) / 2;

   assert(vs_prog_data->base.urb_read_length <= 15);

   /* Rewrite all ATTR file references to the hw grf that they land in. */
   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      for (int i = 0; i < inst->sources; i++) {
         if (inst->src[i].file == ATTR) {

            if (inst->src[i].reg == VERT_ATTRIB_MAX) {
               slot = count - 1;
            } else {
               /* Attributes come in in a contiguous block, ordered by their
                * gl_vert_attrib value.  That means we can compute the slot
                * number for an attribute by masking out the enabled
                * attributes before it and counting the bits.
                */
               attr = inst->src[i].reg + inst->src[i].reg_offset / 4;
               slot = _mesa_bitcount_64(vs_prog_data->inputs_read &
                                        BITFIELD64_MASK(attr));
            }

            channel = inst->src[i].reg_offset & 3;

            grf = payload.num_regs +
               prog_data->curb_read_length +
               slot * 4 + channel;

            inst->src[i].file = HW_REG;
            inst->src[i].fixed_hw_reg =
               stride(byte_offset(retype(brw_vec8_grf(grf, 0), inst->src[i].type),
                                  inst->src[i].subreg_offset),
                      inst->exec_size * inst->src[i].stride,
                      inst->exec_size, inst->src[i].stride);
         }
      }
   }
}

/**
 * Split large virtual GRFs into separate components if we can.
 *
 * This is mostly duplicated with what brw_fs_vector_splitting does,
 * but that's really conservative because it's afraid of doing
 * splitting that doesn't result in real progress after the rest of
 * the optimization phases, which would cause infinite looping in
 * optimization.  We can do it once here, safely.  This also has the
 * opportunity to split interpolated values, or maybe even uniforms,
 * which we don't have at the IR level.
 *
 * We want to split, because virtual GRFs are what we register
 * allocate and spill (due to contiguousness requirements for some
 * instructions), and they're what we naturally generate in the
 * codegen process, but most virtual GRFs don't actually need to be
 * contiguous sets of GRFs.  If we split, we'll end up with reduced
 * live intervals and better dead code elimination and coalescing.
 */
void
fs_visitor::split_virtual_grfs()
{
   int num_vars = this->alloc.count;

   /* Count the total number of registers */
   int reg_count = 0;
   int vgrf_to_reg[num_vars];
   for (int i = 0; i < num_vars; i++) {
      vgrf_to_reg[i] = reg_count;
      reg_count += alloc.sizes[i];
   }

   /* An array of "split points".  For each register slot, this indicates
    * if this slot can be separated from the previous slot.  Every time an
    * instruction uses multiple elements of a register (as a source or
    * destination), we mark the used slots as inseparable.  Then we go
    * through and split the registers into the smallest pieces we can.
    */
   bool split_points[reg_count];
   memset(split_points, 0, sizeof(split_points));

   /* Mark all used registers as fully splittable */
   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      if (inst->dst.file == GRF) {
         int reg = vgrf_to_reg[inst->dst.reg];
         for (unsigned j = 1; j < this->alloc.sizes[inst->dst.reg]; j++)
            split_points[reg + j] = true;
      }

      for (int i = 0; i < inst->sources; i++) {
         if (inst->src[i].file == GRF) {
            int reg = vgrf_to_reg[inst->src[i].reg];
            for (unsigned j = 1; j < this->alloc.sizes[inst->src[i].reg]; j++)
               split_points[reg + j] = true;
         }
      }
   }

   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      if (inst->dst.file == GRF) {
         int reg = vgrf_to_reg[inst->dst.reg] + inst->dst.reg_offset;
         for (int j = 1; j < inst->regs_written; j++)
            split_points[reg + j] = false;
      }
      for (int i = 0; i < inst->sources; i++) {
         if (inst->src[i].file == GRF) {
            int reg = vgrf_to_reg[inst->src[i].reg] + inst->src[i].reg_offset;
            for (int j = 1; j < inst->regs_read(i); j++)
               split_points[reg + j] = false;
         }
      }
   }

   int new_virtual_grf[reg_count];
   int new_reg_offset[reg_count];

   int reg = 0;
   for (int i = 0; i < num_vars; i++) {
      /* The first one should always be 0 as a quick sanity check. */
      assert(split_points[reg] == false);

      /* j = 0 case */
      new_reg_offset[reg] = 0;
      reg++;
      int offset = 1;

      /* j > 0 case */
      for (unsigned j = 1; j < alloc.sizes[i]; j++) {
         /* If this is a split point, reset the offset to 0 and allocate a
          * new virtual GRF for the previous offset many registers
          */
         if (split_points[reg]) {
            assert(offset <= MAX_VGRF_SIZE);
            int grf = alloc.allocate(offset);
            for (int k = reg - offset; k < reg; k++)
               new_virtual_grf[k] = grf;
            offset = 0;
         }
         new_reg_offset[reg] = offset;
         offset++;
         reg++;
      }

      /* The last one gets the original register number */
      assert(offset <= MAX_VGRF_SIZE);
      alloc.sizes[i] = offset;
      for (int k = reg - offset; k < reg; k++)
         new_virtual_grf[k] = i;
   }
   assert(reg == reg_count);

   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      if (inst->dst.file == GRF) {
         reg = vgrf_to_reg[inst->dst.reg] + inst->dst.reg_offset;
         inst->dst.reg = new_virtual_grf[reg];
         inst->dst.reg_offset = new_reg_offset[reg];
         assert((unsigned)new_reg_offset[reg] < alloc.sizes[new_virtual_grf[reg]]);
      }
      for (int i = 0; i < inst->sources; i++) {
	 if (inst->src[i].file == GRF) {
            reg = vgrf_to_reg[inst->src[i].reg] + inst->src[i].reg_offset;
            inst->src[i].reg = new_virtual_grf[reg];
            inst->src[i].reg_offset = new_reg_offset[reg];
            assert((unsigned)new_reg_offset[reg] < alloc.sizes[new_virtual_grf[reg]]);
         }
      }
   }
   invalidate_live_intervals();
}

/**
 * Remove unused virtual GRFs and compact the virtual_grf_* arrays.
 *
 * During code generation, we create tons of temporary variables, many of
 * which get immediately killed and are never used again.  Yet, in later
 * optimization and analysis passes, such as compute_live_intervals, we need
 * to loop over all the virtual GRFs.  Compacting them can save a lot of
 * overhead.
 */
bool
fs_visitor::compact_virtual_grfs()
{
   bool progress = false;
   int remap_table[this->alloc.count];
   memset(remap_table, -1, sizeof(remap_table));

   /* Mark which virtual GRFs are used. */
   foreach_block_and_inst(block, const fs_inst, inst, cfg) {
      if (inst->dst.file == GRF)
         remap_table[inst->dst.reg] = 0;

      for (int i = 0; i < inst->sources; i++) {
         if (inst->src[i].file == GRF)
            remap_table[inst->src[i].reg] = 0;
      }
   }

   /* Compact the GRF arrays. */
   int new_index = 0;
   for (unsigned i = 0; i < this->alloc.count; i++) {
      if (remap_table[i] == -1) {
         /* We just found an unused register.  This means that we are
          * actually going to compact something.
          */
         progress = true;
      } else {
         remap_table[i] = new_index;
         alloc.sizes[new_index] = alloc.sizes[i];
         invalidate_live_intervals();
         ++new_index;
      }
   }

   this->alloc.count = new_index;

   /* Patch all the instructions to use the newly renumbered registers */
   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      if (inst->dst.file == GRF)
         inst->dst.reg = remap_table[inst->dst.reg];

      for (int i = 0; i < inst->sources; i++) {
         if (inst->src[i].file == GRF)
            inst->src[i].reg = remap_table[inst->src[i].reg];
      }
   }

   /* Patch all the references to delta_xy, since they're used in register
    * allocation.  If they're unused, switch them to BAD_FILE so we don't
    * think some random VGRF is delta_xy.
    */
   for (unsigned i = 0; i < ARRAY_SIZE(delta_xy); i++) {
      if (delta_xy[i].file == GRF) {
         if (remap_table[delta_xy[i].reg] != -1) {
            delta_xy[i].reg = remap_table[delta_xy[i].reg];
         } else {
            delta_xy[i].file = BAD_FILE;
         }
      }
   }

   return progress;
}

/*
 * Implements array access of uniforms by inserting a
 * PULL_CONSTANT_LOAD instruction.
 *
 * Unlike temporary GRF array access (where we don't support it due to
 * the difficulty of doing relative addressing on instruction
 * destinations), we could potentially do array access of uniforms
 * that were loaded in GRF space as push constants.  In real-world
 * usage we've seen, though, the arrays being used are always larger
 * than we could load as push constants, so just always move all
 * uniform array access out to a pull constant buffer.
 */
void
fs_visitor::move_uniform_array_access_to_pull_constants()
{
   if (dispatch_width != 8)
      return;

   pull_constant_loc = ralloc_array(mem_ctx, int, uniforms);
   memset(pull_constant_loc, -1, sizeof(pull_constant_loc[0]) * uniforms);

   /* Walk through and find array access of uniforms.  Put a copy of that
    * uniform in the pull constant buffer.
    *
    * Note that we don't move constant-indexed accesses to arrays.  No
    * testing has been done of the performance impact of this choice.
    */
   foreach_block_and_inst_safe(block, fs_inst, inst, cfg) {
      for (int i = 0 ; i < inst->sources; i++) {
         if (inst->src[i].file != UNIFORM || !inst->src[i].reladdr)
            continue;

         int uniform = inst->src[i].reg;

         /* If this array isn't already present in the pull constant buffer,
          * add it.
          */
         if (pull_constant_loc[uniform] == -1) {
            const gl_constant_value **values = &stage_prog_data->param[uniform];

            assert(param_size[uniform]);

            for (int j = 0; j < param_size[uniform]; j++) {
               pull_constant_loc[uniform + j] = stage_prog_data->nr_pull_params;

               stage_prog_data->pull_param[stage_prog_data->nr_pull_params++] =
                  values[j];
            }
         }
      }
   }
}

/**
 * Assign UNIFORM file registers to either push constants or pull constants.
 *
 * We allow a fragment shader to have more than the specified minimum
 * maximum number of fragment shader uniform components (64).  If
 * there are too many of these, they'd fill up all of register space.
 * So, this will push some of them out to the pull constant buffer and
 * update the program to load them.
 */
void
fs_visitor::assign_constant_locations()
{
   /* Only the first compile (SIMD8 mode) gets to decide on locations. */
   if (dispatch_width != 8)
      return;

   /* Find which UNIFORM registers are still in use. */
   bool is_live[uniforms];
   for (unsigned int i = 0; i < uniforms; i++) {
      is_live[i] = false;
   }

   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      for (int i = 0; i < inst->sources; i++) {
         if (inst->src[i].file != UNIFORM)
            continue;

         int constant_nr = inst->src[i].reg + inst->src[i].reg_offset;
         if (constant_nr >= 0 && constant_nr < (int) uniforms)
            is_live[constant_nr] = true;
      }
   }

   /* Only allow 16 registers (128 uniform components) as push constants.
    *
    * Just demote the end of the list.  We could probably do better
    * here, demoting things that are rarely used in the program first.
    *
    * If changing this value, note the limitation about total_regs in
    * brw_curbe.c.
    */
   unsigned int max_push_components = 16 * 8;
   unsigned int num_push_constants = 0;

   push_constant_loc = ralloc_array(mem_ctx, int, uniforms);

   for (unsigned int i = 0; i < uniforms; i++) {
      if (!is_live[i] || pull_constant_loc[i] != -1) {
         /* This UNIFORM register is either dead, or has already been demoted
          * to a pull const.  Mark it as no longer living in the param[] array.
          */
         push_constant_loc[i] = -1;
         continue;
      }

      if (num_push_constants < max_push_components) {
         /* Retain as a push constant.  Record the location in the params[]
          * array.
          */
         push_constant_loc[i] = num_push_constants++;
      } else {
         /* Demote to a pull constant. */
         push_constant_loc[i] = -1;

         int pull_index = stage_prog_data->nr_pull_params++;
         stage_prog_data->pull_param[pull_index] = stage_prog_data->param[i];
         pull_constant_loc[i] = pull_index;
      }
   }

   stage_prog_data->nr_params = num_push_constants;

   /* Up until now, the param[] array has been indexed by reg + reg_offset
    * of UNIFORM registers.  Condense it to only contain the uniforms we
    * chose to upload as push constants.
    */
   for (unsigned int i = 0; i < uniforms; i++) {
      int remapped = push_constant_loc[i];

      if (remapped == -1)
         continue;

      assert(remapped <= (int)i);
      stage_prog_data->param[remapped] = stage_prog_data->param[i];
   }
}

/**
 * Replace UNIFORM register file access with either UNIFORM_PULL_CONSTANT_LOAD
 * or VARYING_PULL_CONSTANT_LOAD instructions which load values into VGRFs.
 */
void
fs_visitor::demote_pull_constants()
{
   foreach_block_and_inst (block, fs_inst, inst, cfg) {
      for (int i = 0; i < inst->sources; i++) {
	 if (inst->src[i].file != UNIFORM)
	    continue;

         int pull_index;
         unsigned location = inst->src[i].reg + inst->src[i].reg_offset;
         if (location >= uniforms) /* Out of bounds access */
            pull_index = -1;
         else
            pull_index = pull_constant_loc[location];

         if (pull_index == -1)
	    continue;

         /* Set up the annotation tracking for new generated instructions. */
         const fs_builder ibld(this, block, inst);
         fs_reg surf_index(stage_prog_data->binding_table.pull_constants_start);
         fs_reg dst = vgrf(glsl_type::float_type);

         assert(inst->src[i].stride == 0);

         /* Generate a pull load into dst. */
         if (inst->src[i].reladdr) {
            VARYING_PULL_CONSTANT_LOAD(ibld, dst,
                                       surf_index,
                                       *inst->src[i].reladdr,
                                       pull_index);
            inst->src[i].reladdr = NULL;
            inst->src[i].stride = 1;
         } else {
            const fs_builder ubld = ibld.exec_all().group(8, 0);
            fs_reg offset = fs_reg((unsigned)(pull_index * 4) & ~15);
            ubld.emit(FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD,
                      dst, surf_index, offset);
            inst->src[i].set_smear(pull_index & 3);
         }

         /* Rewrite the instruction to use the temporary VGRF. */
         inst->src[i].file = GRF;
         inst->src[i].reg = dst.reg;
         inst->src[i].reg_offset = 0;
      }
   }
   invalidate_live_intervals();
}

bool
fs_visitor::opt_algebraic()
{
   bool progress = false;

   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      switch (inst->opcode) {
      case BRW_OPCODE_MOV:
         if (inst->src[0].file != IMM)
            break;

         if (inst->saturate) {
            if (inst->dst.type != inst->src[0].type)
               assert(!"unimplemented: saturate mixed types");

            if (brw_saturate_immediate(inst->dst.type,
                                       &inst->src[0].fixed_hw_reg)) {
               inst->saturate = false;
               progress = true;
            }
         }
         break;

      case BRW_OPCODE_MUL:
	 if (inst->src[1].file != IMM)
	    continue;

	 /* a * 1.0 = a */
	 if (inst->src[1].is_one()) {
	    inst->opcode = BRW_OPCODE_MOV;
	    inst->src[1] = reg_undef;
	    progress = true;
	    break;
	 }

         /* a * -1.0 = -a */
         if (inst->src[1].is_negative_one()) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[0].negate = !inst->src[0].negate;
            inst->src[1] = reg_undef;
            progress = true;
            break;
         }

         /* a * 0.0 = 0.0 */
         if (inst->src[1].is_zero()) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[0] = inst->src[1];
            inst->src[1] = reg_undef;
            progress = true;
            break;
         }

         if (inst->src[0].file == IMM) {
            assert(inst->src[0].type == BRW_REGISTER_TYPE_F);
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[0].fixed_hw_reg.dw1.f *= inst->src[1].fixed_hw_reg.dw1.f;
            inst->src[1] = reg_undef;
            progress = true;
            break;
         }
	 break;
      case BRW_OPCODE_ADD:
         if (inst->src[1].file != IMM)
            continue;

         /* a + 0.0 = a */
         if (inst->src[1].is_zero()) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[1] = reg_undef;
            progress = true;
            break;
         }

         if (inst->src[0].file == IMM) {
            assert(inst->src[0].type == BRW_REGISTER_TYPE_F);
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[0].fixed_hw_reg.dw1.f += inst->src[1].fixed_hw_reg.dw1.f;
            inst->src[1] = reg_undef;
            progress = true;
            break;
         }
         break;
      case BRW_OPCODE_OR:
         if (inst->src[0].equals(inst->src[1])) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[1] = reg_undef;
            progress = true;
            break;
         }
         break;
      case BRW_OPCODE_LRP:
         if (inst->src[1].equals(inst->src[2])) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[0] = inst->src[1];
            inst->src[1] = reg_undef;
            inst->src[2] = reg_undef;
            progress = true;
            break;
         }
         break;
      case BRW_OPCODE_CMP:
         if (inst->conditional_mod == BRW_CONDITIONAL_GE &&
             inst->src[0].abs &&
             inst->src[0].negate &&
             inst->src[1].is_zero()) {
            inst->src[0].abs = false;
            inst->src[0].negate = false;
            inst->conditional_mod = BRW_CONDITIONAL_Z;
            progress = true;
            break;
         }
         break;
      case BRW_OPCODE_SEL:
         if (inst->src[0].equals(inst->src[1])) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[1] = reg_undef;
            inst->predicate = BRW_PREDICATE_NONE;
            inst->predicate_inverse = false;
            progress = true;
         } else if (inst->saturate && inst->src[1].file == IMM) {
            switch (inst->conditional_mod) {
            case BRW_CONDITIONAL_LE:
            case BRW_CONDITIONAL_L:
               switch (inst->src[1].type) {
               case BRW_REGISTER_TYPE_F:
                  if (inst->src[1].fixed_hw_reg.dw1.f >= 1.0f) {
                     inst->opcode = BRW_OPCODE_MOV;
                     inst->src[1] = reg_undef;
                     inst->conditional_mod = BRW_CONDITIONAL_NONE;
                     progress = true;
                  }
                  break;
               default:
                  break;
               }
               break;
            case BRW_CONDITIONAL_GE:
            case BRW_CONDITIONAL_G:
               switch (inst->src[1].type) {
               case BRW_REGISTER_TYPE_F:
                  if (inst->src[1].fixed_hw_reg.dw1.f <= 0.0f) {
                     inst->opcode = BRW_OPCODE_MOV;
                     inst->src[1] = reg_undef;
                     inst->conditional_mod = BRW_CONDITIONAL_NONE;
                     progress = true;
                  }
                  break;
               default:
                  break;
               }
            default:
               break;
            }
         }
         break;
      case BRW_OPCODE_MAD:
         if (inst->src[1].is_zero() || inst->src[2].is_zero()) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[1] = reg_undef;
            inst->src[2] = reg_undef;
            progress = true;
         } else if (inst->src[0].is_zero()) {
            inst->opcode = BRW_OPCODE_MUL;
            inst->src[0] = inst->src[2];
            inst->src[2] = reg_undef;
            progress = true;
         } else if (inst->src[1].is_one()) {
            inst->opcode = BRW_OPCODE_ADD;
            inst->src[1] = inst->src[2];
            inst->src[2] = reg_undef;
            progress = true;
         } else if (inst->src[2].is_one()) {
            inst->opcode = BRW_OPCODE_ADD;
            inst->src[2] = reg_undef;
            progress = true;
         } else if (inst->src[1].file == IMM && inst->src[2].file == IMM) {
            inst->opcode = BRW_OPCODE_ADD;
            inst->src[1].fixed_hw_reg.dw1.f *= inst->src[2].fixed_hw_reg.dw1.f;
            inst->src[2] = reg_undef;
            progress = true;
         }
         break;
      case SHADER_OPCODE_RCP: {
         fs_inst *prev = (fs_inst *)inst->prev;
         if (prev->opcode == SHADER_OPCODE_SQRT) {
            if (inst->src[0].equals(prev->dst)) {
               inst->opcode = SHADER_OPCODE_RSQ;
               inst->src[0] = prev->src[0];
               progress = true;
            }
         }
         break;
      }
      case SHADER_OPCODE_BROADCAST:
         if (is_uniform(inst->src[0])) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->sources = 1;
            inst->force_writemask_all = true;
            progress = true;
         } else if (inst->src[1].file == IMM) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[0] = component(inst->src[0],
                                     inst->src[1].fixed_hw_reg.dw1.ud);
            inst->sources = 1;
            inst->force_writemask_all = true;
            progress = true;
         }
         break;

      default:
	 break;
      }

      /* Swap if src[0] is immediate. */
      if (progress && inst->is_commutative()) {
         if (inst->src[0].file == IMM) {
            fs_reg tmp = inst->src[1];
            inst->src[1] = inst->src[0];
            inst->src[0] = tmp;
         }
      }
   }
   return progress;
}

/**
 * Optimize sample messages that have constant zero values for the trailing
 * texture coordinates. We can just reduce the message length for these
 * instructions instead of reserving a register for it. Trailing parameters
 * that aren't sent default to zero anyway. This will cause the dead code
 * eliminator to remove the MOV instruction that would otherwise be emitted to
 * set up the zero value.
 */
bool
fs_visitor::opt_zero_samples()
{
   /* Gen4 infers the texturing opcode based on the message length so we can't
    * change it.
    */
   if (devinfo->gen < 5)
      return false;

   bool progress = false;

   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      if (!inst->is_tex())
         continue;

      fs_inst *load_payload = (fs_inst *) inst->prev;

      if (load_payload->is_head_sentinel() ||
          load_payload->opcode != SHADER_OPCODE_LOAD_PAYLOAD)
         continue;

      /* We don't want to remove the message header or the first parameter.
       * Removing the first parameter is not allowed, see the Haswell PRM
       * volume 7, page 149:
       *
       *     "Parameter 0 is required except for the sampleinfo message, which
       *      has no parameter 0"
       */
      while (inst->mlen > inst->header_size + inst->exec_size / 8 &&
             load_payload->src[(inst->mlen - inst->header_size) /
                               (inst->exec_size / 8) +
                               inst->header_size - 1].is_zero()) {
         inst->mlen -= inst->exec_size / 8;
         progress = true;
      }
   }

   if (progress)
      invalidate_live_intervals();

   return progress;
}

/**
 * Optimize sample messages which are followed by the final RT write.
 *
 * CHV, and GEN9+ can mark a texturing SEND instruction with EOT to have its
 * results sent directly to the framebuffer, bypassing the EU.  Recognize the
 * final texturing results copied to the framebuffer write payload and modify
 * them to write to the framebuffer directly.
 */
bool
fs_visitor::opt_sampler_eot()
{
   brw_wm_prog_key *key = (brw_wm_prog_key*) this->key;

   if (stage != MESA_SHADER_FRAGMENT)
      return false;

   if (devinfo->gen < 9 && !devinfo->is_cherryview)
      return false;

   /* FINISHME: It should be possible to implement this optimization when there
    * are multiple drawbuffers.
    */
   if (key->nr_color_regions != 1)
      return false;

   /* Look for a texturing instruction immediately before the final FB_WRITE. */
   bblock_t *block = cfg->blocks[cfg->num_blocks - 1];
   fs_inst *fb_write = (fs_inst *)block->end();
   assert(fb_write->eot);
   assert(fb_write->opcode == FS_OPCODE_FB_WRITE);

   fs_inst *tex_inst = (fs_inst *) fb_write->prev;

   /* There wasn't one; nothing to do. */
   if (unlikely(tex_inst->is_head_sentinel()) || !tex_inst->is_tex())
      return false;

   /* This optimisation doesn't seem to work for textureGather for some
    * reason. I can't find any documentation or known workarounds to indicate
    * that this is expected, but considering that it is probably pretty
    * unlikely that a shader would directly write out the results from
    * textureGather we might as well just disable it.
    */
   if (tex_inst->opcode == SHADER_OPCODE_TG4 ||
       tex_inst->opcode == SHADER_OPCODE_TG4_OFFSET)
      return false;

   /* If there's no header present, we need to munge the LOAD_PAYLOAD as well.
    * It's very likely to be the previous instruction.
    */
   fs_inst *load_payload = (fs_inst *) tex_inst->prev;
   if (load_payload->is_head_sentinel() ||
       load_payload->opcode != SHADER_OPCODE_LOAD_PAYLOAD)
      return false;

   assert(!tex_inst->eot); /* We can't get here twice */
   assert((tex_inst->offset & (0xff << 24)) == 0);

   const fs_builder ibld(this, block, tex_inst);

   tex_inst->offset |= fb_write->target << 24;
   tex_inst->eot = true;
   tex_inst->dst = ibld.null_reg_ud();
   fb_write->remove(cfg->blocks[cfg->num_blocks - 1]);

   /* If a header is present, marking the eot is sufficient. Otherwise, we need
    * to create a new LOAD_PAYLOAD command with the same sources and a space
    * saved for the header. Using a new destination register not only makes sure
    * we have enough space, but it will make sure the dead code eliminator kills
    * the instruction that this will replace.
    */
   if (tex_inst->header_size != 0)
      return true;

   fs_reg send_header = ibld.vgrf(BRW_REGISTER_TYPE_F,
                                  load_payload->sources + 1);
   fs_reg *new_sources =
      ralloc_array(mem_ctx, fs_reg, load_payload->sources + 1);

   new_sources[0] = fs_reg();
   for (int i = 0; i < load_payload->sources; i++)
      new_sources[i+1] = load_payload->src[i];

   /* The LOAD_PAYLOAD helper seems like the obvious choice here. However, it
    * requires a lot of information about the sources to appropriately figure
    * out the number of registers needed to be used. Given this stage in our
    * optimization, we may not have the appropriate GRFs required by
    * LOAD_PAYLOAD at this point (copy propagation). Therefore, we need to
    * manually emit the instruction.
    */
   fs_inst *new_load_payload = new(mem_ctx) fs_inst(SHADER_OPCODE_LOAD_PAYLOAD,
                                                    load_payload->exec_size,
                                                    send_header,
                                                    new_sources,
                                                    load_payload->sources + 1);

   new_load_payload->regs_written = load_payload->regs_written + 1;
   new_load_payload->header_size = 1;
   tex_inst->mlen++;
   tex_inst->header_size = 1;
   tex_inst->insert_before(cfg->blocks[cfg->num_blocks - 1], new_load_payload);
   tex_inst->src[0] = send_header;

   return true;
}

bool
fs_visitor::opt_register_renaming()
{
   bool progress = false;
   int depth = 0;

   int remap[alloc.count];
   memset(remap, -1, sizeof(int) * alloc.count);

   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      if (inst->opcode == BRW_OPCODE_IF || inst->opcode == BRW_OPCODE_DO) {
         depth++;
      } else if (inst->opcode == BRW_OPCODE_ENDIF ||
                 inst->opcode == BRW_OPCODE_WHILE) {
         depth--;
      }

      /* Rewrite instruction sources. */
      for (int i = 0; i < inst->sources; i++) {
         if (inst->src[i].file == GRF &&
             remap[inst->src[i].reg] != -1 &&
             remap[inst->src[i].reg] != inst->src[i].reg) {
            inst->src[i].reg = remap[inst->src[i].reg];
            progress = true;
         }
      }

      const int dst = inst->dst.reg;

      if (depth == 0 &&
          inst->dst.file == GRF &&
          alloc.sizes[inst->dst.reg] == inst->exec_size / 8 &&
          !inst->is_partial_write()) {
         if (remap[dst] == -1) {
            remap[dst] = dst;
         } else {
            remap[dst] = alloc.allocate(inst->exec_size / 8);
            inst->dst.reg = remap[dst];
            progress = true;
         }
      } else if (inst->dst.file == GRF &&
                 remap[dst] != -1 &&
                 remap[dst] != dst) {
         inst->dst.reg = remap[dst];
         progress = true;
      }
   }

   if (progress) {
      invalidate_live_intervals();

      for (unsigned i = 0; i < ARRAY_SIZE(delta_xy); i++) {
         if (delta_xy[i].file == GRF && remap[delta_xy[i].reg] != -1) {
            delta_xy[i].reg = remap[delta_xy[i].reg];
         }
      }
   }

   return progress;
}

/**
 * Remove redundant or useless discard jumps.
 *
 * For example, we can eliminate jumps in the following sequence:
 *
 * discard-jump       (redundant with the next jump)
 * discard-jump       (useless; jumps to the next instruction)
 * placeholder-halt
 */
bool
fs_visitor::opt_redundant_discard_jumps()
{
   bool progress = false;

   bblock_t *last_bblock = cfg->blocks[cfg->num_blocks - 1];

   fs_inst *placeholder_halt = NULL;
   foreach_inst_in_block_reverse(fs_inst, inst, last_bblock) {
      if (inst->opcode == FS_OPCODE_PLACEHOLDER_HALT) {
         placeholder_halt = inst;
         break;
      }
   }

   if (!placeholder_halt)
      return false;

   /* Delete any HALTs immediately before the placeholder halt. */
   for (fs_inst *prev = (fs_inst *) placeholder_halt->prev;
        !prev->is_head_sentinel() && prev->opcode == FS_OPCODE_DISCARD_JUMP;
        prev = (fs_inst *) placeholder_halt->prev) {
      prev->remove(last_bblock);
      progress = true;
   }

   if (progress)
      invalidate_live_intervals();

   return progress;
}

bool
fs_visitor::compute_to_mrf()
{
   bool progress = false;
   int next_ip = 0;

   /* No MRFs on Gen >= 7. */
   if (devinfo->gen >= 7)
      return false;

   calculate_live_intervals();

   foreach_block_and_inst_safe(block, fs_inst, inst, cfg) {
      int ip = next_ip;
      next_ip++;

      if (inst->opcode != BRW_OPCODE_MOV ||
	  inst->is_partial_write() ||
	  inst->dst.file != MRF || inst->src[0].file != GRF ||
	  inst->dst.type != inst->src[0].type ||
	  inst->src[0].abs || inst->src[0].negate ||
          !inst->src[0].is_contiguous() ||
          inst->src[0].subreg_offset)
	 continue;

      /* Work out which hardware MRF registers are written by this
       * instruction.
       */
      int mrf_low = inst->dst.reg & ~BRW_MRF_COMPR4;
      int mrf_high;
      if (inst->dst.reg & BRW_MRF_COMPR4) {
	 mrf_high = mrf_low + 4;
      } else if (inst->exec_size == 16) {
	 mrf_high = mrf_low + 1;
      } else {
	 mrf_high = mrf_low;
      }

      /* Can't compute-to-MRF this GRF if someone else was going to
       * read it later.
       */
      if (this->virtual_grf_end[inst->src[0].reg] > ip)
	 continue;

      /* Found a move of a GRF to a MRF.  Let's see if we can go
       * rewrite the thing that made this GRF to write into the MRF.
       */
      foreach_inst_in_block_reverse_starting_from(fs_inst, scan_inst, inst, block) {
	 if (scan_inst->dst.file == GRF &&
	     scan_inst->dst.reg == inst->src[0].reg) {
	    /* Found the last thing to write our reg we want to turn
	     * into a compute-to-MRF.
	     */

	    /* If this one instruction didn't populate all the
	     * channels, bail.  We might be able to rewrite everything
	     * that writes that reg, but it would require smarter
	     * tracking to delay the rewriting until complete success.
	     */
	    if (scan_inst->is_partial_write())
	       break;

            /* Things returning more than one register would need us to
             * understand coalescing out more than one MOV at a time.
             */
            if (scan_inst->regs_written > scan_inst->exec_size / 8)
               break;

	    /* SEND instructions can't have MRF as a destination. */
	    if (scan_inst->mlen)
	       break;

	    if (devinfo->gen == 6) {
	       /* gen6 math instructions must have the destination be
		* GRF, so no compute-to-MRF for them.
		*/
	       if (scan_inst->is_math()) {
		  break;
	       }
	    }

	    if (scan_inst->dst.reg_offset == inst->src[0].reg_offset) {
	       /* Found the creator of our MRF's source value. */
	       scan_inst->dst.file = MRF;
	       scan_inst->dst.reg = inst->dst.reg;
	       scan_inst->saturate |= inst->saturate;
	       inst->remove(block);
	       progress = true;
	    }
	    break;
	 }

	 /* We don't handle control flow here.  Most computation of
	  * values that end up in MRFs are shortly before the MRF
	  * write anyway.
	  */
	 if (block->start() == scan_inst)
	    break;

	 /* You can't read from an MRF, so if someone else reads our
	  * MRF's source GRF that we wanted to rewrite, that stops us.
	  */
	 bool interfered = false;
	 for (int i = 0; i < scan_inst->sources; i++) {
	    if (scan_inst->src[i].file == GRF &&
		scan_inst->src[i].reg == inst->src[0].reg &&
		scan_inst->src[i].reg_offset == inst->src[0].reg_offset) {
	       interfered = true;
	    }
	 }
	 if (interfered)
	    break;

	 if (scan_inst->dst.file == MRF) {
	    /* If somebody else writes our MRF here, we can't
	     * compute-to-MRF before that.
	     */
	    int scan_mrf_low = scan_inst->dst.reg & ~BRW_MRF_COMPR4;
	    int scan_mrf_high;

	    if (scan_inst->dst.reg & BRW_MRF_COMPR4) {
	       scan_mrf_high = scan_mrf_low + 4;
	    } else if (scan_inst->exec_size == 16) {
	       scan_mrf_high = scan_mrf_low + 1;
	    } else {
	       scan_mrf_high = scan_mrf_low;
	    }

	    if (mrf_low == scan_mrf_low ||
		mrf_low == scan_mrf_high ||
		mrf_high == scan_mrf_low ||
		mrf_high == scan_mrf_high) {
	       break;
	    }
	 }

	 if (scan_inst->mlen > 0 && scan_inst->base_mrf != -1) {
	    /* Found a SEND instruction, which means that there are
	     * live values in MRFs from base_mrf to base_mrf +
	     * scan_inst->mlen - 1.  Don't go pushing our MRF write up
	     * above it.
	     */
	    if (mrf_low >= scan_inst->base_mrf &&
		mrf_low < scan_inst->base_mrf + scan_inst->mlen) {
	       break;
	    }
	    if (mrf_high >= scan_inst->base_mrf &&
		mrf_high < scan_inst->base_mrf + scan_inst->mlen) {
	       break;
	    }
	 }
      }
   }

   if (progress)
      invalidate_live_intervals();

   return progress;
}

/**
 * Eliminate FIND_LIVE_CHANNEL instructions occurring outside any control
 * flow.  We could probably do better here with some form of divergence
 * analysis.
 */
bool
fs_visitor::eliminate_find_live_channel()
{
   bool progress = false;
   unsigned depth = 0;

   foreach_block_and_inst_safe(block, fs_inst, inst, cfg) {
      switch (inst->opcode) {
      case BRW_OPCODE_IF:
      case BRW_OPCODE_DO:
         depth++;
         break;

      case BRW_OPCODE_ENDIF:
      case BRW_OPCODE_WHILE:
         depth--;
         break;

      case FS_OPCODE_DISCARD_JUMP:
         /* This can potentially make control flow non-uniform until the end
          * of the program.
          */
         return progress;

      case SHADER_OPCODE_FIND_LIVE_CHANNEL:
         if (depth == 0) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[0] = fs_reg(0);
            inst->sources = 1;
            inst->force_writemask_all = true;
            progress = true;
         }
         break;

      default:
         break;
      }
   }

   return progress;
}

/**
 * Once we've generated code, try to convert normal FS_OPCODE_FB_WRITE
 * instructions to FS_OPCODE_REP_FB_WRITE.
 */
void
fs_visitor::emit_repclear_shader()
{
   brw_wm_prog_key *key = (brw_wm_prog_key*) this->key;
   int base_mrf = 1;
   int color_mrf = base_mrf + 2;

   fs_inst *mov = bld.exec_all().MOV(vec4(brw_message_reg(color_mrf)),
                                     fs_reg(UNIFORM, 0, BRW_REGISTER_TYPE_F));

   fs_inst *write;
   if (key->nr_color_regions == 1) {
      write = bld.emit(FS_OPCODE_REP_FB_WRITE);
      write->saturate = key->clamp_fragment_color;
      write->base_mrf = color_mrf;
      write->target = 0;
      write->header_size = 0;
      write->mlen = 1;
   } else {
      assume(key->nr_color_regions > 0);
      for (int i = 0; i < key->nr_color_regions; ++i) {
         write = bld.emit(FS_OPCODE_REP_FB_WRITE);
         write->saturate = key->clamp_fragment_color;
         write->base_mrf = base_mrf;
         write->target = i;
         write->header_size = 2;
         write->mlen = 3;
      }
   }
   write->eot = true;

   calculate_cfg();

   assign_constant_locations();
   assign_curb_setup();

   /* Now that we have the uniform assigned, go ahead and force it to a vec4. */
   assert(mov->src[0].file == HW_REG);
   mov->src[0] = brw_vec4_grf(mov->src[0].fixed_hw_reg.nr, 0);
}

/**
 * Walks through basic blocks, looking for repeated MRF writes and
 * removing the later ones.
 */
bool
fs_visitor::remove_duplicate_mrf_writes()
{
   fs_inst *last_mrf_move[16];
   bool progress = false;

   /* Need to update the MRF tracking for compressed instructions. */
   if (dispatch_width == 16)
      return false;

   memset(last_mrf_move, 0, sizeof(last_mrf_move));

   foreach_block_and_inst_safe (block, fs_inst, inst, cfg) {
      if (inst->is_control_flow()) {
	 memset(last_mrf_move, 0, sizeof(last_mrf_move));
      }

      if (inst->opcode == BRW_OPCODE_MOV &&
	  inst->dst.file == MRF) {
	 fs_inst *prev_inst = last_mrf_move[inst->dst.reg];
	 if (prev_inst && inst->equals(prev_inst)) {
	    inst->remove(block);
	    progress = true;
	    continue;
	 }
      }

      /* Clear out the last-write records for MRFs that were overwritten. */
      if (inst->dst.file == MRF) {
	 last_mrf_move[inst->dst.reg] = NULL;
      }

      if (inst->mlen > 0 && inst->base_mrf != -1) {
	 /* Found a SEND instruction, which will include two or fewer
	  * implied MRF writes.  We could do better here.
	  */
	 for (int i = 0; i < implied_mrf_writes(inst); i++) {
	    last_mrf_move[inst->base_mrf + i] = NULL;
	 }
      }

      /* Clear out any MRF move records whose sources got overwritten. */
      if (inst->dst.file == GRF) {
	 for (unsigned int i = 0; i < ARRAY_SIZE(last_mrf_move); i++) {
	    if (last_mrf_move[i] &&
		last_mrf_move[i]->src[0].reg == inst->dst.reg) {
	       last_mrf_move[i] = NULL;
	    }
	 }
      }

      if (inst->opcode == BRW_OPCODE_MOV &&
	  inst->dst.file == MRF &&
	  inst->src[0].file == GRF &&
	  !inst->is_partial_write()) {
	 last_mrf_move[inst->dst.reg] = inst;
      }
   }

   if (progress)
      invalidate_live_intervals();

   return progress;
}

static void
clear_deps_for_inst_src(fs_inst *inst, bool *deps, int first_grf, int grf_len)
{
   /* Clear the flag for registers that actually got read (as expected). */
   for (int i = 0; i < inst->sources; i++) {
      int grf;
      if (inst->src[i].file == GRF) {
         grf = inst->src[i].reg;
      } else if (inst->src[i].file == HW_REG &&
                 inst->src[i].fixed_hw_reg.file == BRW_GENERAL_REGISTER_FILE) {
         grf = inst->src[i].fixed_hw_reg.nr;
      } else {
         continue;
      }

      if (grf >= first_grf &&
          grf < first_grf + grf_len) {
         deps[grf - first_grf] = false;
         if (inst->exec_size == 16)
            deps[grf - first_grf + 1] = false;
      }
   }
}

/**
 * Implements this workaround for the original 965:
 *
 *     "[DevBW, DevCL] Implementation Restrictions: As the hardware does not
 *      check for post destination dependencies on this instruction, software
 *      must ensure that there is no destination hazard for the case of ‘write
 *      followed by a posted write’ shown in the following example.
 *
 *      1. mov r3 0
 *      2. send r3.xy <rest of send instruction>
 *      3. mov r2 r3
 *
 *      Due to no post-destination dependency check on the ‘send’, the above
 *      code sequence could have two instructions (1 and 2) in flight at the
 *      same time that both consider ‘r3’ as the target of their final writes.
 */
void
fs_visitor::insert_gen4_pre_send_dependency_workarounds(bblock_t *block,
                                                        fs_inst *inst)
{
   int write_len = inst->regs_written;
   int first_write_grf = inst->dst.reg;
   bool needs_dep[BRW_MAX_MRF];
   assert(write_len < (int)sizeof(needs_dep) - 1);

   memset(needs_dep, false, sizeof(needs_dep));
   memset(needs_dep, true, write_len);

   clear_deps_for_inst_src(inst, needs_dep, first_write_grf, write_len);

   /* Walk backwards looking for writes to registers we're writing which
    * aren't read since being written.  If we hit the start of the program,
    * we assume that there are no outstanding dependencies on entry to the
    * program.
    */
   foreach_inst_in_block_reverse_starting_from(fs_inst, scan_inst, inst, block) {
      /* If we hit control flow, assume that there *are* outstanding
       * dependencies, and force their cleanup before our instruction.
       */
      if (block->start() == scan_inst) {
         for (int i = 0; i < write_len; i++) {
            if (needs_dep[i])
               DEP_RESOLVE_MOV(fs_builder(this, block, inst),
                               first_write_grf + i);
         }
         return;
      }

      /* We insert our reads as late as possible on the assumption that any
       * instruction but a MOV that might have left us an outstanding
       * dependency has more latency than a MOV.
       */
      if (scan_inst->dst.file == GRF) {
         for (int i = 0; i < scan_inst->regs_written; i++) {
            int reg = scan_inst->dst.reg + i;

            if (reg >= first_write_grf &&
                reg < first_write_grf + write_len &&
                needs_dep[reg - first_write_grf]) {
               DEP_RESOLVE_MOV(fs_builder(this, block, inst), reg);
               needs_dep[reg - first_write_grf] = false;
               if (scan_inst->exec_size == 16)
                  needs_dep[reg - first_write_grf + 1] = false;
            }
         }
      }

      /* Clear the flag for registers that actually got read (as expected). */
      clear_deps_for_inst_src(scan_inst, needs_dep, first_write_grf, write_len);

      /* Continue the loop only if we haven't resolved all the dependencies */
      int i;
      for (i = 0; i < write_len; i++) {
         if (needs_dep[i])
            break;
      }
      if (i == write_len)
         return;
   }
}

/**
 * Implements this workaround for the original 965:
 *
 *     "[DevBW, DevCL] Errata: A destination register from a send can not be
 *      used as a destination register until after it has been sourced by an
 *      instruction with a different destination register.
 */
void
fs_visitor::insert_gen4_post_send_dependency_workarounds(bblock_t *block, fs_inst *inst)
{
   int write_len = inst->regs_written;
   int first_write_grf = inst->dst.reg;
   bool needs_dep[BRW_MAX_MRF];
   assert(write_len < (int)sizeof(needs_dep) - 1);

   memset(needs_dep, false, sizeof(needs_dep));
   memset(needs_dep, true, write_len);
   /* Walk forwards looking for writes to registers we're writing which aren't
    * read before being written.
    */
   foreach_inst_in_block_starting_from(fs_inst, scan_inst, inst, block) {
      /* If we hit control flow, force resolve all remaining dependencies. */
      if (block->end() == scan_inst) {
         for (int i = 0; i < write_len; i++) {
            if (needs_dep[i])
               DEP_RESOLVE_MOV(fs_builder(this, block, scan_inst),
                               first_write_grf + i);
         }
         return;
      }

      /* Clear the flag for registers that actually got read (as expected). */
      clear_deps_for_inst_src(scan_inst, needs_dep, first_write_grf, write_len);

      /* We insert our reads as late as possible since they're reading the
       * result of a SEND, which has massive latency.
       */
      if (scan_inst->dst.file == GRF &&
          scan_inst->dst.reg >= first_write_grf &&
          scan_inst->dst.reg < first_write_grf + write_len &&
          needs_dep[scan_inst->dst.reg - first_write_grf]) {
         DEP_RESOLVE_MOV(fs_builder(this, block, scan_inst),
                         scan_inst->dst.reg);
         needs_dep[scan_inst->dst.reg - first_write_grf] = false;
      }

      /* Continue the loop only if we haven't resolved all the dependencies */
      int i;
      for (i = 0; i < write_len; i++) {
         if (needs_dep[i])
            break;
      }
      if (i == write_len)
         return;
   }
}

void
fs_visitor::insert_gen4_send_dependency_workarounds()
{
   if (devinfo->gen != 4 || devinfo->is_g4x)
      return;

   bool progress = false;

   /* Note that we're done with register allocation, so GRF fs_regs always
    * have a .reg_offset of 0.
    */

   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      if (inst->mlen != 0 && inst->dst.file == GRF) {
         insert_gen4_pre_send_dependency_workarounds(block, inst);
         insert_gen4_post_send_dependency_workarounds(block, inst);
         progress = true;
      }
   }

   if (progress)
      invalidate_live_intervals();
}

/**
 * Turns the generic expression-style uniform pull constant load instruction
 * into a hardware-specific series of instructions for loading a pull
 * constant.
 *
 * The expression style allows the CSE pass before this to optimize out
 * repeated loads from the same offset, and gives the pre-register-allocation
 * scheduling full flexibility, while the conversion to native instructions
 * allows the post-register-allocation scheduler the best information
 * possible.
 *
 * Note that execution masking for setting up pull constant loads is special:
 * the channels that need to be written are unrelated to the current execution
 * mask, since a later instruction will use one of the result channels as a
 * source operand for all 8 or 16 of its channels.
 */
void
fs_visitor::lower_uniform_pull_constant_loads()
{
   foreach_block_and_inst (block, fs_inst, inst, cfg) {
      if (inst->opcode != FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD)
         continue;

      if (devinfo->gen >= 7) {
         /* The offset arg before was a vec4-aligned byte offset.  We need to
          * turn it into a dword offset.
          */
         fs_reg const_offset_reg = inst->src[1];
         assert(const_offset_reg.file == IMM &&
                const_offset_reg.type == BRW_REGISTER_TYPE_UD);
         const_offset_reg.fixed_hw_reg.dw1.ud /= 4;

         fs_reg payload, offset;
         if (devinfo->gen >= 9) {
            /* We have to use a message header on Skylake to get SIMD4x2
             * mode.  Reserve space for the register.
            */
            offset = payload = fs_reg(GRF, alloc.allocate(2));
            offset.reg_offset++;
            inst->mlen = 2;
         } else {
            offset = payload = fs_reg(GRF, alloc.allocate(1));
            inst->mlen = 1;
         }

         /* This is actually going to be a MOV, but since only the first dword
          * is accessed, we have a special opcode to do just that one.  Note
          * that this needs to be an operation that will be considered a def
          * by live variable analysis, or register allocation will explode.
          */
         fs_inst *setup = new(mem_ctx) fs_inst(FS_OPCODE_SET_SIMD4X2_OFFSET,
                                               8, offset, const_offset_reg);
         setup->force_writemask_all = true;

         setup->ir = inst->ir;
         setup->annotation = inst->annotation;
         inst->insert_before(block, setup);

         /* Similarly, this will only populate the first 4 channels of the
          * result register (since we only use smear values from 0-3), but we
          * don't tell the optimizer.
          */
         inst->opcode = FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD_GEN7;
         inst->src[1] = payload;
         inst->base_mrf = -1;

         invalidate_live_intervals();
      } else {
         /* Before register allocation, we didn't tell the scheduler about the
          * MRF we use.  We know it's safe to use this MRF because nothing
          * else does except for register spill/unspill, which generates and
          * uses its MRF within a single IR instruction.
          */
         inst->base_mrf = 14;
         inst->mlen = 1;
      }
   }
}

bool
fs_visitor::lower_load_payload()
{
   bool progress = false;

   foreach_block_and_inst_safe (block, fs_inst, inst, cfg) {
      if (inst->opcode != SHADER_OPCODE_LOAD_PAYLOAD)
         continue;

      assert(inst->dst.file == MRF || inst->dst.file == GRF);
      assert(inst->saturate == false);
      fs_reg dst = inst->dst;

      /* Get rid of COMPR4.  We'll add it back in if we need it */
      if (dst.file == MRF)
         dst.reg = dst.reg & ~BRW_MRF_COMPR4;

      const fs_builder ibld(this, block, inst);
      const fs_builder hbld = ibld.exec_all().group(8, 0);

      for (uint8_t i = 0; i < inst->header_size; i++) {
         if (inst->src[i].file != BAD_FILE) {
            fs_reg mov_dst = retype(dst, BRW_REGISTER_TYPE_UD);
            fs_reg mov_src = retype(inst->src[i], BRW_REGISTER_TYPE_UD);
            hbld.MOV(mov_dst, mov_src);
         }
         dst = offset(dst, hbld, 1);
      }

      if (inst->dst.file == MRF && (inst->dst.reg & BRW_MRF_COMPR4) &&
          inst->exec_size > 8) {
         /* In this case, the payload portion of the LOAD_PAYLOAD isn't
          * a straightforward copy.  Instead, the result of the
          * LOAD_PAYLOAD is treated as interleaved and the first four
          * non-header sources are unpacked as:
          *
          * m + 0: r0
          * m + 1: g0
          * m + 2: b0
          * m + 3: a0
          * m + 4: r1
          * m + 5: g1
          * m + 6: b1
          * m + 7: a1
          *
          * This is used for gen <= 5 fb writes.
          */
         assert(inst->exec_size == 16);
         assert(inst->header_size + 4 <= inst->sources);
         for (uint8_t i = inst->header_size; i < inst->header_size + 4; i++) {
            if (inst->src[i].file != BAD_FILE) {
               if (devinfo->has_compr4) {
                  fs_reg compr4_dst = retype(dst, inst->src[i].type);
                  compr4_dst.reg |= BRW_MRF_COMPR4;
                  ibld.MOV(compr4_dst, inst->src[i]);
               } else {
                  /* Platform doesn't have COMPR4.  We have to fake it */
                  fs_reg mov_dst = retype(dst, inst->src[i].type);
                  ibld.half(0).MOV(mov_dst, half(inst->src[i], 0));
                  mov_dst.reg += 4;
                  ibld.half(1).MOV(mov_dst, half(inst->src[i], 1));
               }
            }

            dst.reg++;
         }

         /* The loop above only ever incremented us through the first set
          * of 4 registers.  However, thanks to the magic of COMPR4, we
          * actually wrote to the first 8 registers, so we need to take
          * that into account now.
          */
         dst.reg += 4;

         /* The COMPR4 code took care of the first 4 sources.  We'll let
          * the regular path handle any remaining sources.  Yes, we are
          * modifying the instruction but we're about to delete it so
          * this really doesn't hurt anything.
          */
         inst->header_size += 4;
      }

      for (uint8_t i = inst->header_size; i < inst->sources; i++) {
         if (inst->src[i].file != BAD_FILE)
            ibld.MOV(retype(dst, inst->src[i].type), inst->src[i]);
         dst = offset(dst, ibld, 1);
      }

      inst->remove(block);
      progress = true;
   }

   if (progress)
      invalidate_live_intervals();

   return progress;
}

bool
fs_visitor::lower_integer_multiplication()
{
   bool progress = false;

   foreach_block_and_inst_safe(block, fs_inst, inst, cfg) {
      const fs_builder ibld(this, block, inst);

      if (inst->opcode == BRW_OPCODE_MUL) {
         if (inst->dst.is_accumulator() ||
             (inst->dst.type != BRW_REGISTER_TYPE_D &&
              inst->dst.type != BRW_REGISTER_TYPE_UD))
            continue;

         /* Gen8's MUL instruction can do a 32-bit x 32-bit -> 32-bit
          * operation directly, but CHV/BXT cannot.
          */
         if (devinfo->gen >= 8 &&
             !devinfo->is_cherryview && !devinfo->is_broxton)
            continue;

         if (inst->src[1].file == IMM &&
             inst->src[1].fixed_hw_reg.dw1.ud < (1 << 16)) {
            /* The MUL instruction isn't commutative. On Gen <= 6, only the low
             * 16-bits of src0 are read, and on Gen >= 7 only the low 16-bits of
             * src1 are used.
             *
             * If multiplying by an immediate value that fits in 16-bits, do a
             * single MUL instruction with that value in the proper location.
             */
            if (devinfo->gen < 7) {
               fs_reg imm(GRF, alloc.allocate(dispatch_width / 8),
                          inst->dst.type);
               ibld.MOV(imm, inst->src[1]);
               ibld.MUL(inst->dst, imm, inst->src[0]);
            } else {
               ibld.MUL(inst->dst, inst->src[0], inst->src[1]);
            }
         } else {
            /* Gen < 8 (and some Gen8+ low-power parts like Cherryview) cannot
             * do 32-bit integer multiplication in one instruction, but instead
             * must do a sequence (which actually calculates a 64-bit result):
             *
             *    mul(8)  acc0<1>D   g3<8,8,1>D      g4<8,8,1>D
             *    mach(8) null       g3<8,8,1>D      g4<8,8,1>D
             *    mov(8)  g2<1>D     acc0<8,8,1>D
             *
             * But on Gen > 6, the ability to use second accumulator register
             * (acc1) for non-float data types was removed, preventing a simple
             * implementation in SIMD16. A 16-channel result can be calculated by
             * executing the three instructions twice in SIMD8, once with quarter
             * control of 1Q for the first eight channels and again with 2Q for
             * the second eight channels.
             *
             * Which accumulator register is implicitly accessed (by AccWrEnable
             * for instance) is determined by the quarter control. Unfortunately
             * Ivybridge (and presumably Baytrail) has a hardware bug in which an
             * implicit accumulator access by an instruction with 2Q will access
             * acc1 regardless of whether the data type is usable in acc1.
             *
             * Specifically, the 2Q mach(8) writes acc1 which does not exist for
             * integer data types.
             *
             * Since we only want the low 32-bits of the result, we can do two
             * 32-bit x 16-bit multiplies (like the mul and mach are doing), and
             * adjust the high result and add them (like the mach is doing):
             *
             *    mul(8)  g7<1>D     g3<8,8,1>D      g4.0<8,8,1>UW
             *    mul(8)  g8<1>D     g3<8,8,1>D      g4.1<8,8,1>UW
             *    shl(8)  g9<1>D     g8<8,8,1>D      16D
             *    add(8)  g2<1>D     g7<8,8,1>D      g8<8,8,1>D
             *
             * We avoid the shl instruction by realizing that we only want to add
             * the low 16-bits of the "high" result to the high 16-bits of the
             * "low" result and using proper regioning on the add:
             *
             *    mul(8)  g7<1>D     g3<8,8,1>D      g4.0<16,8,2>UW
             *    mul(8)  g8<1>D     g3<8,8,1>D      g4.1<16,8,2>UW
             *    add(8)  g7.1<2>UW  g7.1<16,8,2>UW  g8<16,8,2>UW
             *
             * Since it does not use the (single) accumulator register, we can
             * schedule multi-component multiplications much better.
             */

            fs_reg orig_dst = inst->dst;
            if (orig_dst.is_null() || orig_dst.file == MRF) {
               inst->dst = fs_reg(GRF, alloc.allocate(dispatch_width / 8),
                                  inst->dst.type);
            }
            fs_reg low = inst->dst;
            fs_reg high(GRF, alloc.allocate(dispatch_width / 8),
                        inst->dst.type);

            if (devinfo->gen >= 7) {
               fs_reg src1_0_w = inst->src[1];
               fs_reg src1_1_w = inst->src[1];

               if (inst->src[1].file == IMM) {
                  src1_0_w.fixed_hw_reg.dw1.ud &= 0xffff;
                  src1_1_w.fixed_hw_reg.dw1.ud >>= 16;
               } else {
                  src1_0_w.type = BRW_REGISTER_TYPE_UW;
                  if (src1_0_w.stride != 0) {
                     assert(src1_0_w.stride == 1);
                     src1_0_w.stride = 2;
                  }

                  src1_1_w.type = BRW_REGISTER_TYPE_UW;
                  if (src1_1_w.stride != 0) {
                     assert(src1_1_w.stride == 1);
                     src1_1_w.stride = 2;
                  }
                  src1_1_w.subreg_offset += type_sz(BRW_REGISTER_TYPE_UW);
               }
               ibld.MUL(low, inst->src[0], src1_0_w);
               ibld.MUL(high, inst->src[0], src1_1_w);
            } else {
               fs_reg src0_0_w = inst->src[0];
               fs_reg src0_1_w = inst->src[0];

               src0_0_w.type = BRW_REGISTER_TYPE_UW;
               if (src0_0_w.stride != 0) {
                  assert(src0_0_w.stride == 1);
                  src0_0_w.stride = 2;
               }

               src0_1_w.type = BRW_REGISTER_TYPE_UW;
               if (src0_1_w.stride != 0) {
                  assert(src0_1_w.stride == 1);
                  src0_1_w.stride = 2;
               }
               src0_1_w.subreg_offset += type_sz(BRW_REGISTER_TYPE_UW);

               ibld.MUL(low, src0_0_w, inst->src[1]);
               ibld.MUL(high, src0_1_w, inst->src[1]);
            }

            fs_reg dst = inst->dst;
            dst.type = BRW_REGISTER_TYPE_UW;
            dst.subreg_offset = 2;
            dst.stride = 2;

            high.type = BRW_REGISTER_TYPE_UW;
            high.stride = 2;

            low.type = BRW_REGISTER_TYPE_UW;
            low.subreg_offset = 2;
            low.stride = 2;

            ibld.ADD(dst, low, high);

            if (inst->conditional_mod || orig_dst.file == MRF) {
               set_condmod(inst->conditional_mod,
                           ibld.MOV(orig_dst, inst->dst));
            }
         }

      } else if (inst->opcode == SHADER_OPCODE_MULH) {
         /* Should have been lowered to 8-wide. */
         assert(inst->exec_size <= 8);
         const fs_reg acc = retype(brw_acc_reg(inst->exec_size),
                                   inst->dst.type);
         fs_inst *mul = ibld.MUL(acc, inst->src[0], inst->src[1]);
         fs_inst *mach = ibld.MACH(inst->dst, inst->src[0], inst->src[1]);

         if (devinfo->gen >= 8) {
            /* Until Gen8, integer multiplies read 32-bits from one source,
             * and 16-bits from the other, and relying on the MACH instruction
             * to generate the high bits of the result.
             *
             * On Gen8, the multiply instruction does a full 32x32-bit
             * multiply, but in order to do a 64-bit multiply we can simulate
             * the previous behavior and then use a MACH instruction.
             *
             * FINISHME: Don't use source modifiers on src1.
             */
            assert(mul->src[1].type == BRW_REGISTER_TYPE_D ||
                   mul->src[1].type == BRW_REGISTER_TYPE_UD);
            mul->src[1].type = (type_is_signed(mul->src[1].type) ?
                                BRW_REGISTER_TYPE_W : BRW_REGISTER_TYPE_UW);
            mul->src[1].stride *= 2;

         } else if (devinfo->gen == 7 && !devinfo->is_haswell &&
                    inst->force_sechalf) {
            /* Among other things the quarter control bits influence which
             * accumulator register is used by the hardware for instructions
             * that access the accumulator implicitly (e.g. MACH).  A
             * second-half instruction would normally map to acc1, which
             * doesn't exist on Gen7 and up (the hardware does emulate it for
             * floating-point instructions *only* by taking advantage of the
             * extra precision of acc0 not normally used for floating point
             * arithmetic).
             *
             * HSW and up are careful enough not to try to access an
             * accumulator register that doesn't exist, but on earlier Gen7
             * hardware we need to make sure that the quarter control bits are
             * zero to avoid non-deterministic behaviour and emit an extra MOV
             * to get the result masked correctly according to the current
             * channel enables.
             */
            mach->force_sechalf = false;
            mach->force_writemask_all = true;
            mach->dst = ibld.vgrf(inst->dst.type);
            ibld.MOV(inst->dst, mach->dst);
         }
      } else {
         continue;
      }

      inst->remove(block);
      progress = true;
   }

   if (progress)
      invalidate_live_intervals();

   return progress;
}

static void
setup_color_payload(const fs_builder &bld, const brw_wm_prog_key *key,
                    fs_reg *dst, fs_reg color, unsigned components)
{
   if (key->clamp_fragment_color) {
      fs_reg tmp = bld.vgrf(BRW_REGISTER_TYPE_F, 4);
      assert(color.type == BRW_REGISTER_TYPE_F);

      for (unsigned i = 0; i < components; i++)
         set_saturate(true,
                      bld.MOV(offset(tmp, bld, i), offset(color, bld, i)));

      color = tmp;
   }

   for (unsigned i = 0; i < components; i++)
      dst[i] = offset(color, bld, i);
}

static void
lower_fb_write_logical_send(const fs_builder &bld, fs_inst *inst,
                            const brw_wm_prog_data *prog_data,
                            const brw_wm_prog_key *key,
                            const fs_visitor::thread_payload &payload)
{
   assert(inst->src[6].file == IMM);
   const brw_device_info *devinfo = bld.shader->devinfo;
   const fs_reg &color0 = inst->src[0];
   const fs_reg &color1 = inst->src[1];
   const fs_reg &src0_alpha = inst->src[2];
   const fs_reg &src_depth = inst->src[3];
   const fs_reg &dst_depth = inst->src[4];
   fs_reg sample_mask = inst->src[5];
   const unsigned components = inst->src[6].fixed_hw_reg.dw1.ud;

   /* We can potentially have a message length of up to 15, so we have to set
    * base_mrf to either 0 or 1 in order to fit in m0..m15.
    */
   fs_reg sources[15];
   int header_size = 2, payload_header_size;
   unsigned length = 0;

   /* From the Sandy Bridge PRM, volume 4, page 198:
    *
    *     "Dispatched Pixel Enables. One bit per pixel indicating
    *      which pixels were originally enabled when the thread was
    *      dispatched. This field is only required for the end-of-
    *      thread message and on all dual-source messages."
    */
   if (devinfo->gen >= 6 &&
       (devinfo->is_haswell || devinfo->gen >= 8 || !prog_data->uses_kill) &&
       color1.file == BAD_FILE &&
       key->nr_color_regions == 1) {
      header_size = 0;
   }

   if (header_size != 0) {
      assert(header_size == 2);
      /* Allocate 2 registers for a header */
      length += 2;
   }

   if (payload.aa_dest_stencil_reg) {
      sources[length] = fs_reg(GRF, bld.shader->alloc.allocate(1));
      bld.group(8, 0).exec_all().annotate("FB write stencil/AA alpha")
         .MOV(sources[length],
              fs_reg(brw_vec8_grf(payload.aa_dest_stencil_reg, 0)));
      length++;
   }

   if (prog_data->uses_omask) {
      sources[length] = fs_reg(GRF, bld.shader->alloc.allocate(1),
                               BRW_REGISTER_TYPE_UD);

      /* Hand over gl_SampleMask.  Only the lower 16 bits of each channel are
       * relevant.  Since it's unsigned single words one vgrf is always
       * 16-wide, but only the lower or higher 8 channels will be used by the
       * hardware when doing a SIMD8 write depending on whether we have
       * selected the subspans for the first or second half respectively.
       */
      assert(sample_mask.file != BAD_FILE && type_sz(sample_mask.type) == 4);
      sample_mask.type = BRW_REGISTER_TYPE_UW;
      sample_mask.stride *= 2;

      bld.exec_all().annotate("FB write oMask")
         .MOV(half(retype(sources[length], BRW_REGISTER_TYPE_UW),
                   inst->force_sechalf),
              sample_mask);
      length++;
   }

   payload_header_size = length;

   if (src0_alpha.file != BAD_FILE) {
      /* FIXME: This is being passed at the wrong location in the payload and
       * doesn't work when gl_SampleMask and MRTs are used simultaneously.
       * It's supposed to be immediately before oMask but there seems to be no
       * reasonable way to pass them in the correct order because LOAD_PAYLOAD
       * requires header sources to form a contiguous segment at the beginning
       * of the message and src0_alpha has per-channel semantics.
       */
      setup_color_payload(bld, key, &sources[length], src0_alpha, 1);
      length++;
   }

   setup_color_payload(bld, key, &sources[length], color0, components);
   length += 4;

   if (color1.file != BAD_FILE) {
      setup_color_payload(bld, key, &sources[length], color1, components);
      length += 4;
   }

   if (src_depth.file != BAD_FILE) {
      sources[length] = src_depth;
      length++;
   }

   if (dst_depth.file != BAD_FILE) {
      sources[length] = dst_depth;
      length++;
   }

   fs_inst *load;
   if (devinfo->gen >= 7) {
      /* Send from the GRF */
      fs_reg payload = fs_reg(GRF, -1, BRW_REGISTER_TYPE_F);
      load = bld.LOAD_PAYLOAD(payload, sources, length, payload_header_size);
      payload.reg = bld.shader->alloc.allocate(load->regs_written);
      load->dst = payload;

      inst->src[0] = payload;
      inst->resize_sources(1);
      inst->base_mrf = -1;
   } else {
      /* Send from the MRF */
      load = bld.LOAD_PAYLOAD(fs_reg(MRF, 1, BRW_REGISTER_TYPE_F),
                              sources, length, payload_header_size);

      /* On pre-SNB, we have to interlace the color values.  LOAD_PAYLOAD
       * will do this for us if we just give it a COMPR4 destination.
       */
      if (devinfo->gen < 6 && bld.dispatch_width() == 16)
         load->dst.reg |= BRW_MRF_COMPR4;

      inst->resize_sources(0);
      inst->base_mrf = 1;
   }

   inst->opcode = FS_OPCODE_FB_WRITE;
   inst->mlen = load->regs_written;
   inst->header_size = header_size;
}

static void
lower_sampler_logical_send_gen4(const fs_builder &bld, fs_inst *inst, opcode op,
                                const fs_reg &coordinate,
                                const fs_reg &shadow_c,
                                const fs_reg &lod, const fs_reg &lod2,
                                const fs_reg &sampler,
                                unsigned coord_components,
                                unsigned grad_components)
{
   const bool has_lod = (op == SHADER_OPCODE_TXL || op == FS_OPCODE_TXB ||
                         op == SHADER_OPCODE_TXF || op == SHADER_OPCODE_TXS);
   fs_reg msg_begin(MRF, 1, BRW_REGISTER_TYPE_F);
   fs_reg msg_end = msg_begin;

   /* g0 header. */
   msg_end = offset(msg_end, bld.group(8, 0), 1);

   for (unsigned i = 0; i < coord_components; i++)
      bld.MOV(retype(offset(msg_end, bld, i), coordinate.type),
              offset(coordinate, bld, i));

   msg_end = offset(msg_end, bld, coord_components);

   /* Messages other than SAMPLE and RESINFO in SIMD16 and TXD in SIMD8
    * require all three components to be present and zero if they are unused.
    */
   if (coord_components > 0 &&
       (has_lod || shadow_c.file != BAD_FILE ||
        (op == SHADER_OPCODE_TEX && bld.dispatch_width() == 8))) {
      for (unsigned i = coord_components; i < 3; i++)
         bld.MOV(offset(msg_end, bld, i), fs_reg(0.0f));

      msg_end = offset(msg_end, bld, 3 - coord_components);
   }

   if (op == SHADER_OPCODE_TXD) {
      /* TXD unsupported in SIMD16 mode. */
      assert(bld.dispatch_width() == 8);

      /* the slots for u and v are always present, but r is optional */
      if (coord_components < 2)
         msg_end = offset(msg_end, bld, 2 - coord_components);

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
      for (unsigned i = 0; i < grad_components; i++)
         bld.MOV(offset(msg_end, bld, i), offset(lod, bld, i));

      msg_end = offset(msg_end, bld, MAX2(grad_components, 2));

      for (unsigned i = 0; i < grad_components; i++)
         bld.MOV(offset(msg_end, bld, i), offset(lod2, bld, i));

      msg_end = offset(msg_end, bld, MAX2(grad_components, 2));
   }

   if (has_lod) {
      /* Bias/LOD with shadow comparitor is unsupported in SIMD16 -- *Without*
       * shadow comparitor (including RESINFO) it's unsupported in SIMD8 mode.
       */
      assert(shadow_c.file != BAD_FILE ? bld.dispatch_width() == 8 :
             bld.dispatch_width() == 16);

      const brw_reg_type type =
         (op == SHADER_OPCODE_TXF || op == SHADER_OPCODE_TXS ?
          BRW_REGISTER_TYPE_UD : BRW_REGISTER_TYPE_F);
      bld.MOV(retype(msg_end, type), lod);
      msg_end = offset(msg_end, bld, 1);
   }

   if (shadow_c.file != BAD_FILE) {
      if (op == SHADER_OPCODE_TEX && bld.dispatch_width() == 8) {
         /* There's no plain shadow compare message, so we use shadow
          * compare with a bias of 0.0.
          */
         bld.MOV(msg_end, fs_reg(0.0f));
         msg_end = offset(msg_end, bld, 1);
      }

      bld.MOV(msg_end, shadow_c);
      msg_end = offset(msg_end, bld, 1);
   }

   inst->opcode = op;
   inst->src[0] = reg_undef;
   inst->src[1] = sampler;
   inst->resize_sources(2);
   inst->base_mrf = msg_begin.reg;
   inst->mlen = msg_end.reg - msg_begin.reg;
   inst->header_size = 1;
}

static void
lower_sampler_logical_send_gen5(const fs_builder &bld, fs_inst *inst, opcode op,
                                fs_reg coordinate,
                                const fs_reg &shadow_c,
                                fs_reg lod, fs_reg lod2,
                                const fs_reg &sample_index,
                                const fs_reg &sampler,
                                const fs_reg &offset_value,
                                unsigned coord_components,
                                unsigned grad_components)
{
   fs_reg message(MRF, 2, BRW_REGISTER_TYPE_F);
   fs_reg msg_coords = message;
   unsigned header_size = 0;

   if (offset_value.file != BAD_FILE) {
      /* The offsets set up by the visitor are in the m1 header, so we can't
       * go headerless.
       */
      header_size = 1;
      message.reg--;
   }

   for (unsigned i = 0; i < coord_components; i++) {
      bld.MOV(retype(offset(msg_coords, bld, i), coordinate.type), coordinate);
      coordinate = offset(coordinate, bld, 1);
   }
   fs_reg msg_end = offset(msg_coords, bld, coord_components);
   fs_reg msg_lod = offset(msg_coords, bld, 4);

   if (shadow_c.file != BAD_FILE) {
      fs_reg msg_shadow = msg_lod;
      bld.MOV(msg_shadow, shadow_c);
      msg_lod = offset(msg_shadow, bld, 1);
      msg_end = msg_lod;
   }

   switch (op) {
   case SHADER_OPCODE_TXL:
   case FS_OPCODE_TXB:
      bld.MOV(msg_lod, lod);
      msg_end = offset(msg_lod, bld, 1);
      break;
   case SHADER_OPCODE_TXD:
      /**
       *  P   =  u,    v,    r
       * dPdx = dudx, dvdx, drdx
       * dPdy = dudy, dvdy, drdy
       *
       * Load up these values:
       * - dudx   dudy   dvdx   dvdy   drdx   drdy
       * - dPdx.x dPdy.x dPdx.y dPdy.y dPdx.z dPdy.z
       */
      msg_end = msg_lod;
      for (unsigned i = 0; i < grad_components; i++) {
         bld.MOV(msg_end, lod);
         lod = offset(lod, bld, 1);
         msg_end = offset(msg_end, bld, 1);

         bld.MOV(msg_end, lod2);
         lod2 = offset(lod2, bld, 1);
         msg_end = offset(msg_end, bld, 1);
      }
      break;
   case SHADER_OPCODE_TXS:
      msg_lod = retype(msg_end, BRW_REGISTER_TYPE_UD);
      bld.MOV(msg_lod, lod);
      msg_end = offset(msg_lod, bld, 1);
      break;
   case SHADER_OPCODE_TXF:
      msg_lod = offset(msg_coords, bld, 3);
      bld.MOV(retype(msg_lod, BRW_REGISTER_TYPE_UD), lod);
      msg_end = offset(msg_lod, bld, 1);
      break;
   case SHADER_OPCODE_TXF_CMS:
      msg_lod = offset(msg_coords, bld, 3);
      /* lod */
      bld.MOV(retype(msg_lod, BRW_REGISTER_TYPE_UD), fs_reg(0u));
      /* sample index */
      bld.MOV(retype(offset(msg_lod, bld, 1), BRW_REGISTER_TYPE_UD), sample_index);
      msg_end = offset(msg_lod, bld, 2);
      break;
   default:
      break;
   }

   inst->opcode = op;
   inst->src[0] = reg_undef;
   inst->src[1] = sampler;
   inst->resize_sources(2);
   inst->base_mrf = message.reg;
   inst->mlen = msg_end.reg - message.reg;
   inst->header_size = header_size;

   /* Message length > MAX_SAMPLER_MESSAGE_SIZE disallowed by hardware. */
   assert(inst->mlen <= MAX_SAMPLER_MESSAGE_SIZE);
}

static bool
is_high_sampler(const struct brw_device_info *devinfo, const fs_reg &sampler)
{
   if (devinfo->gen < 8 && !devinfo->is_haswell)
      return false;

   return sampler.file != IMM || sampler.fixed_hw_reg.dw1.ud >= 16;
}

static void
lower_sampler_logical_send_gen7(const fs_builder &bld, fs_inst *inst, opcode op,
                                fs_reg coordinate,
                                const fs_reg &shadow_c,
                                fs_reg lod, fs_reg lod2,
                                const fs_reg &sample_index,
                                const fs_reg &mcs, const fs_reg &sampler,
                                fs_reg offset_value,
                                unsigned coord_components,
                                unsigned grad_components)
{
   const brw_device_info *devinfo = bld.shader->devinfo;
   int reg_width = bld.dispatch_width() / 8;
   unsigned header_size = 0, length = 0;
   fs_reg sources[MAX_SAMPLER_MESSAGE_SIZE];
   for (unsigned i = 0; i < ARRAY_SIZE(sources); i++)
      sources[i] = bld.vgrf(BRW_REGISTER_TYPE_F);

   if (op == SHADER_OPCODE_TG4 || op == SHADER_OPCODE_TG4_OFFSET ||
       offset_value.file != BAD_FILE ||
       is_high_sampler(devinfo, sampler)) {
      /* For general texture offsets (no txf workaround), we need a header to
       * put them in.  Note that we're only reserving space for it in the
       * message payload as it will be initialized implicitly by the
       * generator.
       *
       * TG4 needs to place its channel select in the header, for interaction
       * with ARB_texture_swizzle.  The sampler index is only 4-bits, so for
       * larger sampler numbers we need to offset the Sampler State Pointer in
       * the header.
       */
      header_size = 1;
      sources[0] = fs_reg();
      length++;
   }

   if (shadow_c.file != BAD_FILE) {
      bld.MOV(sources[length], shadow_c);
      length++;
   }

   bool coordinate_done = false;

   /* The sampler can only meaningfully compute LOD for fragment shader
    * messages. For all other stages, we change the opcode to TXL and
    * hardcode the LOD to 0.
    */
   if (bld.shader->stage != MESA_SHADER_FRAGMENT &&
       op == SHADER_OPCODE_TEX) {
      op = SHADER_OPCODE_TXL;
      lod = fs_reg(0.0f);
   }

   /* Set up the LOD info */
   switch (op) {
   case FS_OPCODE_TXB:
   case SHADER_OPCODE_TXL:
      bld.MOV(sources[length], lod);
      length++;
      break;
   case SHADER_OPCODE_TXD:
      /* TXD should have been lowered in SIMD16 mode. */
      assert(bld.dispatch_width() == 8);

      /* Load dPdx and the coordinate together:
       * [hdr], [ref], x, dPdx.x, dPdy.x, y, dPdx.y, dPdy.y, z, dPdx.z, dPdy.z
       */
      for (unsigned i = 0; i < coord_components; i++) {
         bld.MOV(sources[length], coordinate);
         coordinate = offset(coordinate, bld, 1);
         length++;

         /* For cube map array, the coordinate is (u,v,r,ai) but there are
          * only derivatives for (u, v, r).
          */
         if (i < grad_components) {
            bld.MOV(sources[length], lod);
            lod = offset(lod, bld, 1);
            length++;

            bld.MOV(sources[length], lod2);
            lod2 = offset(lod2, bld, 1);
            length++;
         }
      }

      coordinate_done = true;
      break;
   case SHADER_OPCODE_TXS:
      bld.MOV(retype(sources[length], BRW_REGISTER_TYPE_UD), lod);
      length++;
      break;
   case SHADER_OPCODE_TXF:
      /* Unfortunately, the parameters for LD are intermixed: u, lod, v, r.
       * On Gen9 they are u, v, lod, r
       */
      bld.MOV(retype(sources[length], BRW_REGISTER_TYPE_D), coordinate);
      coordinate = offset(coordinate, bld, 1);
      length++;

      if (devinfo->gen >= 9) {
         if (coord_components >= 2) {
            bld.MOV(retype(sources[length], BRW_REGISTER_TYPE_D), coordinate);
            coordinate = offset(coordinate, bld, 1);
         }
         length++;
      }

      bld.MOV(retype(sources[length], BRW_REGISTER_TYPE_D), lod);
      length++;

      for (unsigned i = devinfo->gen >= 9 ? 2 : 1; i < coord_components; i++) {
         bld.MOV(retype(sources[length], BRW_REGISTER_TYPE_D), coordinate);
         coordinate = offset(coordinate, bld, 1);
         length++;
      }

      coordinate_done = true;
      break;
   case SHADER_OPCODE_TXF_CMS:
   case SHADER_OPCODE_TXF_UMS:
   case SHADER_OPCODE_TXF_MCS:
      if (op == SHADER_OPCODE_TXF_UMS || op == SHADER_OPCODE_TXF_CMS) {
         bld.MOV(retype(sources[length], BRW_REGISTER_TYPE_UD), sample_index);
         length++;
      }

      if (op == SHADER_OPCODE_TXF_CMS) {
         /* Data from the multisample control surface. */
         bld.MOV(retype(sources[length], BRW_REGISTER_TYPE_UD), mcs);
         length++;
      }

      /* There is no offsetting for this message; just copy in the integer
       * texture coordinates.
       */
      for (unsigned i = 0; i < coord_components; i++) {
         bld.MOV(retype(sources[length], BRW_REGISTER_TYPE_D), coordinate);
         coordinate = offset(coordinate, bld, 1);
         length++;
      }

      coordinate_done = true;
      break;
   case SHADER_OPCODE_TG4_OFFSET:
      /* gather4_po_c should have been lowered in SIMD16 mode. */
      assert(bld.dispatch_width() == 8 || shadow_c.file == BAD_FILE);

      /* More crazy intermixing */
      for (unsigned i = 0; i < 2; i++) { /* u, v */
         bld.MOV(sources[length], coordinate);
         coordinate = offset(coordinate, bld, 1);
         length++;
      }

      for (unsigned i = 0; i < 2; i++) { /* offu, offv */
         bld.MOV(retype(sources[length], BRW_REGISTER_TYPE_D), offset_value);
         offset_value = offset(offset_value, bld, 1);
         length++;
      }

      if (coord_components == 3) { /* r if present */
         bld.MOV(sources[length], coordinate);
         coordinate = offset(coordinate, bld, 1);
         length++;
      }

      coordinate_done = true;
      break;
   default:
      break;
   }

   /* Set up the coordinate (except for cases where it was done above) */
   if (!coordinate_done) {
      for (unsigned i = 0; i < coord_components; i++) {
         bld.MOV(sources[length], coordinate);
         coordinate = offset(coordinate, bld, 1);
         length++;
      }
   }

   int mlen;
   if (reg_width == 2)
      mlen = length * reg_width - header_size;
   else
      mlen = length * reg_width;

   const fs_reg src_payload = fs_reg(GRF, bld.shader->alloc.allocate(mlen),
                                     BRW_REGISTER_TYPE_F);
   bld.LOAD_PAYLOAD(src_payload, sources, length, header_size);

   /* Generate the SEND. */
   inst->opcode = op;
   inst->src[0] = src_payload;
   inst->src[1] = sampler;
   inst->resize_sources(2);
   inst->base_mrf = -1;
   inst->mlen = mlen;
   inst->header_size = header_size;

   /* Message length > MAX_SAMPLER_MESSAGE_SIZE disallowed by hardware. */
   assert(inst->mlen <= MAX_SAMPLER_MESSAGE_SIZE);
}

static void
lower_sampler_logical_send(const fs_builder &bld, fs_inst *inst, opcode op)
{
   const brw_device_info *devinfo = bld.shader->devinfo;
   const fs_reg &coordinate = inst->src[0];
   const fs_reg &shadow_c = inst->src[1];
   const fs_reg &lod = inst->src[2];
   const fs_reg &lod2 = inst->src[3];
   const fs_reg &sample_index = inst->src[4];
   const fs_reg &mcs = inst->src[5];
   const fs_reg &sampler = inst->src[6];
   const fs_reg &offset_value = inst->src[7];
   assert(inst->src[8].file == IMM && inst->src[9].file == IMM);
   const unsigned coord_components = inst->src[8].fixed_hw_reg.dw1.ud;
   const unsigned grad_components = inst->src[9].fixed_hw_reg.dw1.ud;

   if (devinfo->gen >= 7) {
      lower_sampler_logical_send_gen7(bld, inst, op, coordinate,
                                      shadow_c, lod, lod2, sample_index,
                                      mcs, sampler, offset_value,
                                      coord_components, grad_components);
   } else if (devinfo->gen >= 5) {
      lower_sampler_logical_send_gen5(bld, inst, op, coordinate,
                                      shadow_c, lod, lod2, sample_index,
                                      sampler, offset_value,
                                      coord_components, grad_components);
   } else {
      lower_sampler_logical_send_gen4(bld, inst, op, coordinate,
                                      shadow_c, lod, lod2, sampler,
                                      coord_components, grad_components);
   }
}

/**
 * Initialize the header present in some typed and untyped surface
 * messages.
 */
static fs_reg
emit_surface_header(const fs_builder &bld, const fs_reg &sample_mask)
{
   fs_builder ubld = bld.exec_all().group(8, 0);
   const fs_reg dst = ubld.vgrf(BRW_REGISTER_TYPE_UD);
   ubld.MOV(dst, fs_reg(0));
   ubld.MOV(component(dst, 7), sample_mask);
   return dst;
}

static void
lower_surface_logical_send(const fs_builder &bld, fs_inst *inst, opcode op,
                           const fs_reg &sample_mask)
{
   /* Get the logical send arguments. */
   const fs_reg &addr = inst->src[0];
   const fs_reg &src = inst->src[1];
   const fs_reg &surface = inst->src[2];
   const UNUSED fs_reg &dims = inst->src[3];
   const fs_reg &arg = inst->src[4];

   /* Calculate the total number of components of the payload. */
   const unsigned addr_sz = inst->components_read(0);
   const unsigned src_sz = inst->components_read(1);
   const unsigned header_sz = (sample_mask.file == BAD_FILE ? 0 : 1);
   const unsigned sz = header_sz + addr_sz + src_sz;

   /* Allocate space for the payload. */
   fs_reg *const components = new fs_reg[sz];
   const fs_reg payload = bld.vgrf(BRW_REGISTER_TYPE_UD, sz);
   unsigned n = 0;

   /* Construct the payload. */
   if (header_sz)
      components[n++] = emit_surface_header(bld, sample_mask);

   for (unsigned i = 0; i < addr_sz; i++)
      components[n++] = offset(addr, bld, i);

   for (unsigned i = 0; i < src_sz; i++)
      components[n++] = offset(src, bld, i);

   bld.LOAD_PAYLOAD(payload, components, sz, header_sz);

   /* Update the original instruction. */
   inst->opcode = op;
   inst->mlen = header_sz + (addr_sz + src_sz) * inst->exec_size / 8;
   inst->header_size = header_sz;

   inst->src[0] = payload;
   inst->src[1] = surface;
   inst->src[2] = arg;
   inst->resize_sources(3);

   delete[] components;
}

bool
fs_visitor::lower_logical_sends()
{
   bool progress = false;

   foreach_block_and_inst_safe(block, fs_inst, inst, cfg) {
      const fs_builder ibld(this, block, inst);

      switch (inst->opcode) {
      case FS_OPCODE_FB_WRITE_LOGICAL:
         assert(stage == MESA_SHADER_FRAGMENT);
         lower_fb_write_logical_send(ibld, inst,
                                     (const brw_wm_prog_data *)prog_data,
                                     (const brw_wm_prog_key *)key,
                                     payload);
         break;

      case SHADER_OPCODE_TEX_LOGICAL:
         lower_sampler_logical_send(ibld, inst, SHADER_OPCODE_TEX);
         break;

      case SHADER_OPCODE_TXD_LOGICAL:
         lower_sampler_logical_send(ibld, inst, SHADER_OPCODE_TXD);
         break;

      case SHADER_OPCODE_TXF_LOGICAL:
         lower_sampler_logical_send(ibld, inst, SHADER_OPCODE_TXF);
         break;

      case SHADER_OPCODE_TXL_LOGICAL:
         lower_sampler_logical_send(ibld, inst, SHADER_OPCODE_TXL);
         break;

      case SHADER_OPCODE_TXS_LOGICAL:
         lower_sampler_logical_send(ibld, inst, SHADER_OPCODE_TXS);
         break;

      case FS_OPCODE_TXB_LOGICAL:
         lower_sampler_logical_send(ibld, inst, FS_OPCODE_TXB);
         break;

      case SHADER_OPCODE_TXF_CMS_LOGICAL:
         lower_sampler_logical_send(ibld, inst, SHADER_OPCODE_TXF_CMS);
         break;

      case SHADER_OPCODE_TXF_UMS_LOGICAL:
         lower_sampler_logical_send(ibld, inst, SHADER_OPCODE_TXF_UMS);
         break;

      case SHADER_OPCODE_TXF_MCS_LOGICAL:
         lower_sampler_logical_send(ibld, inst, SHADER_OPCODE_TXF_MCS);
         break;

      case SHADER_OPCODE_LOD_LOGICAL:
         lower_sampler_logical_send(ibld, inst, SHADER_OPCODE_LOD);
         break;

      case SHADER_OPCODE_TG4_LOGICAL:
         lower_sampler_logical_send(ibld, inst, SHADER_OPCODE_TG4);
         break;

      case SHADER_OPCODE_TG4_OFFSET_LOGICAL:
         lower_sampler_logical_send(ibld, inst, SHADER_OPCODE_TG4_OFFSET);
         break;

      case SHADER_OPCODE_UNTYPED_SURFACE_READ_LOGICAL:
         lower_surface_logical_send(ibld, inst,
                                    SHADER_OPCODE_UNTYPED_SURFACE_READ,
                                    fs_reg(0xffff));
         break;

      case SHADER_OPCODE_UNTYPED_SURFACE_WRITE_LOGICAL:
         lower_surface_logical_send(ibld, inst,
                                    SHADER_OPCODE_UNTYPED_SURFACE_WRITE,
                                    ibld.sample_mask_reg());
         break;

      case SHADER_OPCODE_UNTYPED_ATOMIC_LOGICAL:
         lower_surface_logical_send(ibld, inst,
                                    SHADER_OPCODE_UNTYPED_ATOMIC,
                                    ibld.sample_mask_reg());
         break;

      case SHADER_OPCODE_TYPED_SURFACE_READ_LOGICAL:
         lower_surface_logical_send(ibld, inst,
                                    SHADER_OPCODE_TYPED_SURFACE_READ,
                                    fs_reg(0xffff));
         break;

      case SHADER_OPCODE_TYPED_SURFACE_WRITE_LOGICAL:
         lower_surface_logical_send(ibld, inst,
                                    SHADER_OPCODE_TYPED_SURFACE_WRITE,
                                    ibld.sample_mask_reg());
         break;

      case SHADER_OPCODE_TYPED_ATOMIC_LOGICAL:
         lower_surface_logical_send(ibld, inst,
                                    SHADER_OPCODE_TYPED_ATOMIC,
                                    ibld.sample_mask_reg());
         break;

      default:
         continue;
      }

      progress = true;
   }

   if (progress)
      invalidate_live_intervals();

   return progress;
}

/**
 * Get the closest native SIMD width supported by the hardware for instruction
 * \p inst.  The instruction will be left untouched by
 * fs_visitor::lower_simd_width() if the returned value is equal to the
 * original execution size.
 */
static unsigned
get_lowered_simd_width(const struct brw_device_info *devinfo,
                       const fs_inst *inst)
{
   switch (inst->opcode) {
   case BRW_OPCODE_MOV:
   case BRW_OPCODE_SEL:
   case BRW_OPCODE_NOT:
   case BRW_OPCODE_AND:
   case BRW_OPCODE_OR:
   case BRW_OPCODE_XOR:
   case BRW_OPCODE_SHR:
   case BRW_OPCODE_SHL:
   case BRW_OPCODE_ASR:
   case BRW_OPCODE_CMP:
   case BRW_OPCODE_CMPN:
   case BRW_OPCODE_CSEL:
   case BRW_OPCODE_F32TO16:
   case BRW_OPCODE_F16TO32:
   case BRW_OPCODE_BFREV:
   case BRW_OPCODE_BFE:
   case BRW_OPCODE_BFI1:
   case BRW_OPCODE_BFI2:
   case BRW_OPCODE_ADD:
   case BRW_OPCODE_MUL:
   case BRW_OPCODE_AVG:
   case BRW_OPCODE_FRC:
   case BRW_OPCODE_RNDU:
   case BRW_OPCODE_RNDD:
   case BRW_OPCODE_RNDE:
   case BRW_OPCODE_RNDZ:
   case BRW_OPCODE_LZD:
   case BRW_OPCODE_FBH:
   case BRW_OPCODE_FBL:
   case BRW_OPCODE_CBIT:
   case BRW_OPCODE_SAD2:
   case BRW_OPCODE_MAD:
   case BRW_OPCODE_LRP:
   case SHADER_OPCODE_RCP:
   case SHADER_OPCODE_RSQ:
   case SHADER_OPCODE_SQRT:
   case SHADER_OPCODE_EXP2:
   case SHADER_OPCODE_LOG2:
   case SHADER_OPCODE_POW:
   case SHADER_OPCODE_INT_QUOTIENT:
   case SHADER_OPCODE_INT_REMAINDER:
   case SHADER_OPCODE_SIN:
   case SHADER_OPCODE_COS: {
      /* According to the PRMs:
       *  "A. In Direct Addressing mode, a source cannot span more than 2
       *      adjacent GRF registers.
       *   B. A destination cannot span more than 2 adjacent GRF registers."
       *
       * Look for the source or destination with the largest register region
       * which is the one that is going to limit the overal execution size of
       * the instruction due to this rule.
       */
      unsigned reg_count = inst->regs_written;

      for (unsigned i = 0; i < inst->sources; i++)
         reg_count = MAX2(reg_count, (unsigned)inst->regs_read(i));

      /* Calculate the maximum execution size of the instruction based on the
       * factor by which it goes over the hardware limit of 2 GRFs.
       */
      return inst->exec_size / DIV_ROUND_UP(reg_count, 2);
   }
   case SHADER_OPCODE_MULH:
      /* MULH is lowered to the MUL/MACH sequence using the accumulator, which
       * is 8-wide on Gen7+.
       */
      return (devinfo->gen >= 7 ? 8 : inst->exec_size);

   case FS_OPCODE_FB_WRITE_LOGICAL:
      /* Gen6 doesn't support SIMD16 depth writes but we cannot handle them
       * here.
       */
      assert(devinfo->gen != 6 || inst->src[3].file == BAD_FILE ||
             inst->exec_size == 8);
      /* Dual-source FB writes are unsupported in SIMD16 mode. */
      return (inst->src[1].file != BAD_FILE ? 8 : inst->exec_size);

   case SHADER_OPCODE_TXD_LOGICAL:
      /* TXD is unsupported in SIMD16 mode. */
      return 8;

   case SHADER_OPCODE_TG4_OFFSET_LOGICAL: {
      /* gather4_po_c is unsupported in SIMD16 mode. */
      const fs_reg &shadow_c = inst->src[1];
      return (shadow_c.file != BAD_FILE ? 8 : inst->exec_size);
   }
   case SHADER_OPCODE_TXL_LOGICAL:
   case FS_OPCODE_TXB_LOGICAL: {
      /* Gen4 doesn't have SIMD8 non-shadow-compare bias/LOD instructions, and
       * Gen4-6 can't support TXL and TXB with shadow comparison in SIMD16
       * mode because the message exceeds the maximum length of 11.
       */
      const fs_reg &shadow_c = inst->src[1];
      if (devinfo->gen == 4 && shadow_c.file == BAD_FILE)
         return 16;
      else if (devinfo->gen < 7 && shadow_c.file != BAD_FILE)
         return 8;
      else
         return inst->exec_size;
   }
   case SHADER_OPCODE_TXF_LOGICAL:
   case SHADER_OPCODE_TXS_LOGICAL:
      /* Gen4 doesn't have SIMD8 variants for the RESINFO and LD-with-LOD
       * messages.  Use SIMD16 instead.
       */
      if (devinfo->gen == 4)
         return 16;
      else
         return inst->exec_size;

   case SHADER_OPCODE_TYPED_ATOMIC_LOGICAL:
   case SHADER_OPCODE_TYPED_SURFACE_READ_LOGICAL:
   case SHADER_OPCODE_TYPED_SURFACE_WRITE_LOGICAL:
      return 8;

   default:
      return inst->exec_size;
   }
}

/**
 * The \p rows array of registers represents a \p num_rows by \p num_columns
 * matrix in row-major order, write it in column-major order into the register
 * passed as destination.  \p stride gives the separation between matrix
 * elements in the input in fs_builder::dispatch_width() units.
 */
static void
emit_transpose(const fs_builder &bld,
               const fs_reg &dst, const fs_reg *rows,
               unsigned num_rows, unsigned num_columns, unsigned stride)
{
   fs_reg *const components = new fs_reg[num_rows * num_columns];

   for (unsigned i = 0; i < num_columns; ++i) {
      for (unsigned j = 0; j < num_rows; ++j)
         components[num_rows * i + j] = offset(rows[j], bld, stride * i);
   }

   bld.LOAD_PAYLOAD(dst, components, num_rows * num_columns, 0);

   delete[] components;
}

bool
fs_visitor::lower_simd_width()
{
   bool progress = false;

   foreach_block_and_inst_safe(block, fs_inst, inst, cfg) {
      const unsigned lower_width = get_lowered_simd_width(devinfo, inst);

      if (lower_width != inst->exec_size) {
         /* Builder matching the original instruction.  We may also need to
          * emit an instruction of width larger than the original, set the
          * execution size of the builder to the highest of both for now so
          * we're sure that both cases can be handled.
          */
         const fs_builder ibld = bld.at(block, inst)
                                    .exec_all(inst->force_writemask_all)
                                    .group(MAX2(inst->exec_size, lower_width),
                                           inst->force_sechalf);

         /* Split the copies in chunks of the execution width of either the
          * original or the lowered instruction, whichever is lower.
          */
         const unsigned copy_width = MIN2(lower_width, inst->exec_size);
         const unsigned n = inst->exec_size / copy_width;
         const unsigned dst_size = inst->regs_written * REG_SIZE /
            inst->dst.component_size(inst->exec_size);
         fs_reg dsts[4];

         assert(n > 0 && n <= ARRAY_SIZE(dsts) &&
                !inst->writes_accumulator && !inst->mlen);

         for (unsigned i = 0; i < n; i++) {
            /* Emit a copy of the original instruction with the lowered width.
             * If the EOT flag was set throw it away except for the last
             * instruction to avoid killing the thread prematurely.
             */
            fs_inst split_inst = *inst;
            split_inst.exec_size = lower_width;
            split_inst.eot = inst->eot && i == n - 1;

            /* Select the correct channel enables for the i-th group, then
             * transform the sources and destination and emit the lowered
             * instruction.
             */
            const fs_builder lbld = ibld.group(lower_width, i);

            for (unsigned j = 0; j < inst->sources; j++) {
               if (inst->src[j].file != BAD_FILE &&
                   !is_uniform(inst->src[j])) {
                  /* Get the i-th copy_width-wide chunk of the source. */
                  const fs_reg src = horiz_offset(inst->src[j], copy_width * i);
                  const unsigned src_size = inst->components_read(j);

                  /* Use a trivial transposition to copy one every n
                   * copy_width-wide components of the register into a
                   * temporary passed as source to the lowered instruction.
                   */
                  split_inst.src[j] = lbld.vgrf(inst->src[j].type, src_size);
                  emit_transpose(lbld.group(copy_width, 0),
                                 split_inst.src[j], &src, 1, src_size, n);
               }
            }

            if (inst->regs_written) {
               /* Allocate enough space to hold the result of the lowered
                * instruction and fix up the number of registers written.
                */
               split_inst.dst = dsts[i] =
                  lbld.vgrf(inst->dst.type, dst_size);
               split_inst.regs_written =
                  DIV_ROUND_UP(inst->regs_written * lower_width,
                               inst->exec_size);
            }

            lbld.emit(split_inst);
         }

         if (inst->regs_written) {
            /* Distance between useful channels in the temporaries, skipping
             * garbage if the lowered instruction is wider than the original.
             */
            const unsigned m = lower_width / copy_width;

            /* Interleave the components of the result from the lowered
             * instructions.  We need to set exec_all() when copying more than
             * one half per component, because LOAD_PAYLOAD (in terms of which
             * emit_transpose is implemented) can only use the same channel
             * enable signals for all of its non-header sources.
             */
            emit_transpose(ibld.exec_all(inst->exec_size > copy_width)
                               .group(copy_width, 0),
                           inst->dst, dsts, n, dst_size, m);
         }

         inst->remove(block);
         progress = true;
      }
   }

   if (progress)
      invalidate_live_intervals();

   return progress;
}

void
fs_visitor::dump_instructions()
{
   dump_instructions(NULL);
}

void
fs_visitor::dump_instructions(const char *name)
{
   FILE *file = stderr;
   if (name && geteuid() != 0) {
      file = fopen(name, "w");
      if (!file)
         file = stderr;
   }

   if (cfg) {
      calculate_register_pressure();
      int ip = 0, max_pressure = 0;
      foreach_block_and_inst(block, backend_instruction, inst, cfg) {
         max_pressure = MAX2(max_pressure, regs_live_at_ip[ip]);
         fprintf(file, "{%3d} %4d: ", regs_live_at_ip[ip], ip);
         dump_instruction(inst, file);
         ip++;
      }
      fprintf(file, "Maximum %3d registers live at once.\n", max_pressure);
   } else {
      int ip = 0;
      foreach_in_list(backend_instruction, inst, &instructions) {
         fprintf(file, "%4d: ", ip++);
         dump_instruction(inst, file);
      }
   }

   if (file != stderr) {
      fclose(file);
   }
}

void
fs_visitor::dump_instruction(backend_instruction *be_inst)
{
   dump_instruction(be_inst, stderr);
}

void
fs_visitor::dump_instruction(backend_instruction *be_inst, FILE *file)
{
   fs_inst *inst = (fs_inst *)be_inst;

   if (inst->predicate) {
      fprintf(file, "(%cf0.%d) ",
             inst->predicate_inverse ? '-' : '+',
             inst->flag_subreg);
   }

   fprintf(file, "%s", brw_instruction_name(inst->opcode));
   if (inst->saturate)
      fprintf(file, ".sat");
   if (inst->conditional_mod) {
      fprintf(file, "%s", conditional_modifier[inst->conditional_mod]);
      if (!inst->predicate &&
          (devinfo->gen < 5 || (inst->opcode != BRW_OPCODE_SEL &&
                              inst->opcode != BRW_OPCODE_IF &&
                              inst->opcode != BRW_OPCODE_WHILE))) {
         fprintf(file, ".f0.%d", inst->flag_subreg);
      }
   }
   fprintf(file, "(%d) ", inst->exec_size);

   if (inst->mlen) {
      fprintf(file, "(mlen: %d) ", inst->mlen);
   }

   switch (inst->dst.file) {
   case GRF:
      fprintf(file, "vgrf%d", inst->dst.reg);
      if (alloc.sizes[inst->dst.reg] != inst->regs_written ||
          inst->dst.subreg_offset)
         fprintf(file, "+%d.%d",
                 inst->dst.reg_offset, inst->dst.subreg_offset);
      break;
   case MRF:
      fprintf(file, "m%d", inst->dst.reg);
      break;
   case BAD_FILE:
      fprintf(file, "(null)");
      break;
   case UNIFORM:
      fprintf(file, "***u%d***", inst->dst.reg + inst->dst.reg_offset);
      break;
   case ATTR:
      fprintf(file, "***attr%d***", inst->dst.reg + inst->dst.reg_offset);
      break;
   case HW_REG:
      if (inst->dst.fixed_hw_reg.file == BRW_ARCHITECTURE_REGISTER_FILE) {
         switch (inst->dst.fixed_hw_reg.nr) {
         case BRW_ARF_NULL:
            fprintf(file, "null");
            break;
         case BRW_ARF_ADDRESS:
            fprintf(file, "a0.%d", inst->dst.fixed_hw_reg.subnr);
            break;
         case BRW_ARF_ACCUMULATOR:
            fprintf(file, "acc%d", inst->dst.fixed_hw_reg.subnr);
            break;
         case BRW_ARF_FLAG:
            fprintf(file, "f%d.%d", inst->dst.fixed_hw_reg.nr & 0xf,
                             inst->dst.fixed_hw_reg.subnr);
            break;
         default:
            fprintf(file, "arf%d.%d", inst->dst.fixed_hw_reg.nr & 0xf,
                               inst->dst.fixed_hw_reg.subnr);
            break;
         }
      } else {
         fprintf(file, "hw_reg%d", inst->dst.fixed_hw_reg.nr);
      }
      if (inst->dst.fixed_hw_reg.subnr)
         fprintf(file, "+%d", inst->dst.fixed_hw_reg.subnr);
      break;
   default:
      fprintf(file, "???");
      break;
   }
   fprintf(file, ":%s, ", brw_reg_type_letters(inst->dst.type));

   for (int i = 0; i < inst->sources; i++) {
      if (inst->src[i].negate)
         fprintf(file, "-");
      if (inst->src[i].abs)
         fprintf(file, "|");
      switch (inst->src[i].file) {
      case GRF:
         fprintf(file, "vgrf%d", inst->src[i].reg);
         if (alloc.sizes[inst->src[i].reg] != (unsigned)inst->regs_read(i) ||
             inst->src[i].subreg_offset)
            fprintf(file, "+%d.%d", inst->src[i].reg_offset,
                    inst->src[i].subreg_offset);
         break;
      case MRF:
         fprintf(file, "***m%d***", inst->src[i].reg);
         break;
      case ATTR:
         fprintf(file, "attr%d", inst->src[i].reg + inst->src[i].reg_offset);
         break;
      case UNIFORM:
         fprintf(file, "u%d", inst->src[i].reg + inst->src[i].reg_offset);
         if (inst->src[i].reladdr) {
            fprintf(file, "+reladdr");
         } else if (inst->src[i].subreg_offset) {
            fprintf(file, "+%d.%d", inst->src[i].reg_offset,
                    inst->src[i].subreg_offset);
         }
         break;
      case BAD_FILE:
         fprintf(file, "(null)");
         break;
      case IMM:
         switch (inst->src[i].type) {
         case BRW_REGISTER_TYPE_F:
            fprintf(file, "%ff", inst->src[i].fixed_hw_reg.dw1.f);
            break;
         case BRW_REGISTER_TYPE_W:
         case BRW_REGISTER_TYPE_D:
            fprintf(file, "%dd", inst->src[i].fixed_hw_reg.dw1.d);
            break;
         case BRW_REGISTER_TYPE_UW:
         case BRW_REGISTER_TYPE_UD:
            fprintf(file, "%uu", inst->src[i].fixed_hw_reg.dw1.ud);
            break;
         case BRW_REGISTER_TYPE_VF:
            fprintf(file, "[%-gF, %-gF, %-gF, %-gF]",
                    brw_vf_to_float((inst->src[i].fixed_hw_reg.dw1.ud >>  0) & 0xff),
                    brw_vf_to_float((inst->src[i].fixed_hw_reg.dw1.ud >>  8) & 0xff),
                    brw_vf_to_float((inst->src[i].fixed_hw_reg.dw1.ud >> 16) & 0xff),
                    brw_vf_to_float((inst->src[i].fixed_hw_reg.dw1.ud >> 24) & 0xff));
            break;
         default:
            fprintf(file, "???");
            break;
         }
         break;
      case HW_REG:
         if (inst->src[i].fixed_hw_reg.negate)
            fprintf(file, "-");
         if (inst->src[i].fixed_hw_reg.abs)
            fprintf(file, "|");
         if (inst->src[i].fixed_hw_reg.file == BRW_ARCHITECTURE_REGISTER_FILE) {
            switch (inst->src[i].fixed_hw_reg.nr) {
            case BRW_ARF_NULL:
               fprintf(file, "null");
               break;
            case BRW_ARF_ADDRESS:
               fprintf(file, "a0.%d", inst->src[i].fixed_hw_reg.subnr);
               break;
            case BRW_ARF_ACCUMULATOR:
               fprintf(file, "acc%d", inst->src[i].fixed_hw_reg.subnr);
               break;
            case BRW_ARF_FLAG:
               fprintf(file, "f%d.%d", inst->src[i].fixed_hw_reg.nr & 0xf,
                                inst->src[i].fixed_hw_reg.subnr);
               break;
            default:
               fprintf(file, "arf%d.%d", inst->src[i].fixed_hw_reg.nr & 0xf,
                                  inst->src[i].fixed_hw_reg.subnr);
               break;
            }
         } else {
            fprintf(file, "hw_reg%d", inst->src[i].fixed_hw_reg.nr);
         }
         if (inst->src[i].fixed_hw_reg.subnr)
            fprintf(file, "+%d", inst->src[i].fixed_hw_reg.subnr);
         if (inst->src[i].fixed_hw_reg.abs)
            fprintf(file, "|");
         break;
      default:
         fprintf(file, "???");
         break;
      }
      if (inst->src[i].abs)
         fprintf(file, "|");

      if (inst->src[i].file != IMM) {
         fprintf(file, ":%s", brw_reg_type_letters(inst->src[i].type));
      }

      if (i < inst->sources - 1 && inst->src[i + 1].file != BAD_FILE)
         fprintf(file, ", ");
   }

   fprintf(file, " ");

   if (dispatch_width == 16 && inst->exec_size == 8) {
      if (inst->force_sechalf)
         fprintf(file, "2ndhalf ");
      else
         fprintf(file, "1sthalf ");
   }

   fprintf(file, "\n");
}

/**
 * Possibly returns an instruction that set up @param reg.
 *
 * Sometimes we want to take the result of some expression/variable
 * dereference tree and rewrite the instruction generating the result
 * of the tree.  When processing the tree, we know that the
 * instructions generated are all writing temporaries that are dead
 * outside of this tree.  So, if we have some instructions that write
 * a temporary, we're free to point that temp write somewhere else.
 *
 * Note that this doesn't guarantee that the instruction generated
 * only reg -- it might be the size=4 destination of a texture instruction.
 */
fs_inst *
fs_visitor::get_instruction_generating_reg(fs_inst *start,
					   fs_inst *end,
					   const fs_reg &reg)
{
   if (end == start ||
       end->is_partial_write() ||
       reg.reladdr ||
       !reg.equals(end->dst)) {
      return NULL;
   } else {
      return end;
   }
}

void
fs_visitor::setup_payload_gen6()
{
   bool uses_depth =
      (prog->InputsRead & (1 << VARYING_SLOT_POS)) != 0;
   unsigned barycentric_interp_modes =
      (stage == MESA_SHADER_FRAGMENT) ?
      ((brw_wm_prog_data*) this->prog_data)->barycentric_interp_modes : 0;

   assert(devinfo->gen >= 6);

   /* R0-1: masks, pixel X/Y coordinates. */
   payload.num_regs = 2;
   /* R2: only for 32-pixel dispatch.*/

   /* R3-26: barycentric interpolation coordinates.  These appear in the
    * same order that they appear in the brw_wm_barycentric_interp_mode
    * enum.  Each set of coordinates occupies 2 registers if dispatch width
    * == 8 and 4 registers if dispatch width == 16.  Coordinates only
    * appear if they were enabled using the "Barycentric Interpolation
    * Mode" bits in WM_STATE.
    */
   for (int i = 0; i < BRW_WM_BARYCENTRIC_INTERP_MODE_COUNT; ++i) {
      if (barycentric_interp_modes & (1 << i)) {
         payload.barycentric_coord_reg[i] = payload.num_regs;
         payload.num_regs += 2;
         if (dispatch_width == 16) {
            payload.num_regs += 2;
         }
      }
   }

   /* R27: interpolated depth if uses source depth */
   if (uses_depth) {
      payload.source_depth_reg = payload.num_regs;
      payload.num_regs++;
      if (dispatch_width == 16) {
         /* R28: interpolated depth if not SIMD8. */
         payload.num_regs++;
      }
   }
   /* R29: interpolated W set if GEN6_WM_USES_SOURCE_W. */
   if (uses_depth) {
      payload.source_w_reg = payload.num_regs;
      payload.num_regs++;
      if (dispatch_width == 16) {
         /* R30: interpolated W if not SIMD8. */
         payload.num_regs++;
      }
   }

   if (stage == MESA_SHADER_FRAGMENT) {
      brw_wm_prog_data *prog_data = (brw_wm_prog_data*) this->prog_data;
      brw_wm_prog_key *key = (brw_wm_prog_key*) this->key;
      prog_data->uses_pos_offset = key->compute_pos_offset;
      /* R31: MSAA position offsets. */
      if (prog_data->uses_pos_offset) {
         payload.sample_pos_reg = payload.num_regs;
         payload.num_regs++;
      }
   }

   /* R32: MSAA input coverage mask */
   if (prog->SystemValuesRead & SYSTEM_BIT_SAMPLE_MASK_IN) {
      assert(devinfo->gen >= 7);
      payload.sample_mask_in_reg = payload.num_regs;
      payload.num_regs++;
      if (dispatch_width == 16) {
         /* R33: input coverage mask if not SIMD8. */
         payload.num_regs++;
      }
   }

   /* R34-: bary for 32-pixel. */
   /* R58-59: interp W for 32-pixel. */

   if (prog->OutputsWritten & BITFIELD64_BIT(FRAG_RESULT_DEPTH)) {
      source_depth_to_render_target = true;
   }
}

void
fs_visitor::setup_vs_payload()
{
   /* R0: thread header, R1: urb handles */
   payload.num_regs = 2;
}

void
fs_visitor::setup_cs_payload()
{
   assert(devinfo->gen >= 7);

   payload.num_regs = 1;
}

void
fs_visitor::assign_binding_table_offsets()
{
   assert(stage == MESA_SHADER_FRAGMENT);
   brw_wm_prog_data *prog_data = (brw_wm_prog_data*) this->prog_data;
   brw_wm_prog_key *key = (brw_wm_prog_key*) this->key;
   uint32_t next_binding_table_offset = 0;

   /* If there are no color regions, we still perform an FB write to a null
    * renderbuffer, which we place at surface index 0.
    */
   prog_data->binding_table.render_target_start = next_binding_table_offset;
   next_binding_table_offset += MAX2(key->nr_color_regions, 1);

   assign_common_binding_table_offsets(next_binding_table_offset);
}

void
fs_visitor::calculate_register_pressure()
{
   invalidate_live_intervals();
   calculate_live_intervals();

   unsigned num_instructions = 0;
   foreach_block(block, cfg)
      num_instructions += block->instructions.length();

   regs_live_at_ip = rzalloc_array(mem_ctx, int, num_instructions);

   for (unsigned reg = 0; reg < alloc.count; reg++) {
      for (int ip = virtual_grf_start[reg]; ip <= virtual_grf_end[reg]; ip++)
         regs_live_at_ip[ip] += alloc.sizes[reg];
   }
}

void
fs_visitor::optimize()
{
   /* bld is the common builder object pointing at the end of the program we
    * used to translate it into i965 IR.  For the optimization and lowering
    * passes coming next, any code added after the end of the program without
    * having explicitly called fs_builder::at() clearly points at a mistake.
    * Ideally optimization passes wouldn't be part of the visitor so they
    * wouldn't have access to bld at all, but they do, so just in case some
    * pass forgets to ask for a location explicitly set it to NULL here to
    * make it trip.  The dispatch width is initialized to a bogus value to
    * make sure that optimizations set the execution controls explicitly to
    * match the code they are manipulating instead of relying on the defaults.
    */
   bld = fs_builder(this, 64);

   move_uniform_array_access_to_pull_constants();
   assign_constant_locations();
   demote_pull_constants();

   split_virtual_grfs();

#define OPT(pass, args...) ({                                           \
      pass_num++;                                                       \
      bool this_progress = pass(args);                                  \
                                                                        \
      if (unlikely(INTEL_DEBUG & DEBUG_OPTIMIZER) && this_progress) {   \
         char filename[64];                                             \
         snprintf(filename, 64, "%s%d-%04d-%02d-%02d-" #pass,              \
                  stage_abbrev, dispatch_width, shader_prog ? shader_prog->Name : 0, iteration, pass_num); \
                                                                        \
         backend_shader::dump_instructions(filename);                   \
      }                                                                 \
                                                                        \
      progress = progress || this_progress;                             \
      this_progress;                                                    \
   })

   if (unlikely(INTEL_DEBUG & DEBUG_OPTIMIZER)) {
      char filename[64];
      snprintf(filename, 64, "%s%d-%04d-00-start",
               stage_abbrev, dispatch_width,
               shader_prog ? shader_prog->Name : 0);

      backend_shader::dump_instructions(filename);
   }

   bool progress = false;
   int iteration = 0;
   int pass_num = 0;

   OPT(lower_simd_width);
   OPT(lower_logical_sends);

   do {
      progress = false;
      pass_num = 0;
      iteration++;

      OPT(remove_duplicate_mrf_writes);

      OPT(opt_algebraic);
      OPT(opt_cse);
      OPT(opt_copy_propagate);
      OPT(opt_peephole_predicated_break);
      OPT(opt_cmod_propagation);
      OPT(dead_code_eliminate);
      OPT(opt_peephole_sel);
      OPT(dead_control_flow_eliminate, this);
      OPT(opt_register_renaming);
      OPT(opt_redundant_discard_jumps);
      OPT(opt_saturate_propagation);
      OPT(opt_zero_samples);
      OPT(register_coalesce);
      OPT(compute_to_mrf);
      OPT(eliminate_find_live_channel);

      OPT(compact_virtual_grfs);
   } while (progress);

   pass_num = 0;

   OPT(opt_sampler_eot);

   if (OPT(lower_load_payload)) {
      split_virtual_grfs();
      OPT(register_coalesce);
      OPT(compute_to_mrf);
      OPT(dead_code_eliminate);
   }

   OPT(opt_combine_constants);
   OPT(lower_integer_multiplication);

   lower_uniform_pull_constant_loads();
}

/**
 * Three source instruction must have a GRF/MRF destination register.
 * ARF NULL is not allowed.  Fix that up by allocating a temporary GRF.
 */
void
fs_visitor::fixup_3src_null_dest()
{
   foreach_block_and_inst_safe (block, fs_inst, inst, cfg) {
      if (inst->is_3src() && inst->dst.is_null()) {
         inst->dst = fs_reg(GRF, alloc.allocate(dispatch_width / 8),
                            inst->dst.type);
      }
   }
}

void
fs_visitor::allocate_registers()
{
   bool allocated_without_spills;

   static const enum instruction_scheduler_mode pre_modes[] = {
      SCHEDULE_PRE,
      SCHEDULE_PRE_NON_LIFO,
      SCHEDULE_PRE_LIFO,
   };

   /* Try each scheduling heuristic to see if it can successfully register
    * allocate without spilling.  They should be ordered by decreasing
    * performance but increasing likelihood of allocating.
    */
   for (unsigned i = 0; i < ARRAY_SIZE(pre_modes); i++) {
      schedule_instructions(pre_modes[i]);

      if (0) {
         assign_regs_trivial();
         allocated_without_spills = true;
      } else {
         allocated_without_spills = assign_regs(false);
      }
      if (allocated_without_spills)
         break;
   }

   if (!allocated_without_spills) {
      /* We assume that any spilling is worse than just dropping back to
       * SIMD8.  There's probably actually some intermediate point where
       * SIMD16 with a couple of spills is still better.
       */
      if (dispatch_width == 16) {
         fail("Failure to register allocate.  Reduce number of "
              "live scalar values to avoid this.");
      } else {
         compiler->shader_perf_log(log_data,
                                   "%s shader triggered register spilling.  "
                                   "Try reducing the number of live scalar "
                                   "values to improve performance.\n",
                                   stage_name);
      }

      /* Since we're out of heuristics, just go spill registers until we
       * get an allocation.
       */
      while (!assign_regs(true)) {
         if (failed)
            break;
      }
   }

   /* This must come after all optimization and register allocation, since
    * it inserts dead code that happens to have side effects, and it does
    * so based on the actual physical registers in use.
    */
   insert_gen4_send_dependency_workarounds();

   if (failed)
      return;

   if (!allocated_without_spills)
      schedule_instructions(SCHEDULE_POST);

   if (last_scratch > 0)
      prog_data->total_scratch = brw_get_scratch_size(last_scratch);
}

bool
fs_visitor::run_vs(gl_clip_plane *clip_planes)
{
   assert(stage == MESA_SHADER_VERTEX);

   assign_common_binding_table_offsets(0);
   setup_vs_payload();

   if (shader_time_index >= 0)
      emit_shader_time_begin();

   emit_nir_code();

   if (failed)
      return false;

   compute_clip_distance(clip_planes);

   emit_urb_writes();

   if (shader_time_index >= 0)
      emit_shader_time_end();

   calculate_cfg();

   optimize();

   assign_curb_setup();
   assign_vs_urb_setup();

   fixup_3src_null_dest();
   allocate_registers();

   return !failed;
}

bool
fs_visitor::run_fs(bool do_rep_send)
{
   brw_wm_prog_data *wm_prog_data = (brw_wm_prog_data *) this->prog_data;
   brw_wm_prog_key *wm_key = (brw_wm_prog_key *) this->key;

   assert(stage == MESA_SHADER_FRAGMENT);

   sanity_param_count = prog->Parameters->NumParameters;

   assign_binding_table_offsets();

   if (devinfo->gen >= 6)
      setup_payload_gen6();
   else
      setup_payload_gen4();

   if (0) {
      emit_dummy_fs();
   } else if (do_rep_send) {
      assert(dispatch_width == 16);
      emit_repclear_shader();
   } else {
      if (shader_time_index >= 0)
         emit_shader_time_begin();

      calculate_urb_setup();
      if (prog->InputsRead > 0) {
         if (devinfo->gen < 6)
            emit_interpolation_setup_gen4();
         else
            emit_interpolation_setup_gen6();
      }

      /* We handle discards by keeping track of the still-live pixels in f0.1.
       * Initialize it with the dispatched pixels.
       */
      if (wm_prog_data->uses_kill) {
         fs_inst *discard_init = bld.emit(FS_OPCODE_MOV_DISPATCH_TO_FLAGS);
         discard_init->flag_subreg = 1;
      }

      /* Generate FS IR for main().  (the visitor only descends into
       * functions called "main").
       */
      emit_nir_code();

      if (failed)
	 return false;

      if (wm_prog_data->uses_kill)
         bld.emit(FS_OPCODE_PLACEHOLDER_HALT);

      if (wm_key->alpha_test_func)
         emit_alpha_test();

      emit_fb_writes();

      if (shader_time_index >= 0)
         emit_shader_time_end();

      calculate_cfg();

      optimize();

      assign_curb_setup();
      assign_urb_setup();

      fixup_3src_null_dest();
      allocate_registers();

      if (failed)
         return false;
   }

   if (dispatch_width == 8)
      wm_prog_data->reg_blocks = brw_register_blocks(grf_used);
   else
      wm_prog_data->reg_blocks_16 = brw_register_blocks(grf_used);

   /* If any state parameters were appended, then ParameterValues could have
    * been realloced, in which case the driver uniform storage set up by
    * _mesa_associate_uniform_storage() would point to freed memory.  Make
    * sure that didn't happen.
    */
   assert(sanity_param_count == prog->Parameters->NumParameters);

   return !failed;
}

bool
fs_visitor::run_cs()
{
   assert(stage == MESA_SHADER_COMPUTE);
   assert(shader);

   sanity_param_count = prog->Parameters->NumParameters;

   assign_common_binding_table_offsets(0);

   setup_cs_payload();

   if (shader_time_index >= 0)
      emit_shader_time_begin();

   emit_nir_code();

   if (failed)
      return false;

   emit_cs_terminate();

   if (shader_time_index >= 0)
      emit_shader_time_end();

   calculate_cfg();

   optimize();

   assign_curb_setup();

   fixup_3src_null_dest();
   allocate_registers();

   if (failed)
      return false;

   /* If any state parameters were appended, then ParameterValues could have
    * been realloced, in which case the driver uniform storage set up by
    * _mesa_associate_uniform_storage() would point to freed memory.  Make
    * sure that didn't happen.
    */
   assert(sanity_param_count == prog->Parameters->NumParameters);

   return !failed;
}

const unsigned *
brw_wm_fs_emit(struct brw_context *brw,
               void *mem_ctx,
               const struct brw_wm_prog_key *key,
               struct brw_wm_prog_data *prog_data,
               struct gl_fragment_program *fp,
               struct gl_shader_program *prog,
               unsigned *final_assembly_size)
{
   bool start_busy = false;
   double start_time = 0;

   if (unlikely(brw->perf_debug)) {
      start_busy = (brw->batch.last_bo &&
                    drm_intel_bo_busy(brw->batch.last_bo));
      start_time = get_time();
   }

   struct brw_shader *shader = NULL;
   if (prog)
      shader = (brw_shader *) prog->_LinkedShaders[MESA_SHADER_FRAGMENT];

   if (unlikely(INTEL_DEBUG & DEBUG_WM))
      brw_dump_ir("fragment", prog, &shader->base, &fp->Base);

   int st_index8 = -1, st_index16 = -1;
   if (INTEL_DEBUG & DEBUG_SHADER_TIME) {
      st_index8 = brw_get_shader_time_index(brw, prog, &fp->Base, ST_FS8);
      st_index16 = brw_get_shader_time_index(brw, prog, &fp->Base, ST_FS16);
   }

   /* Now the main event: Visit the shader IR and generate our FS IR for it.
    */
   fs_visitor v(brw->intelScreen->compiler, brw,
                mem_ctx, MESA_SHADER_FRAGMENT, key, &prog_data->base,
                prog, &fp->Base, 8, st_index8);
   if (!v.run_fs(false /* do_rep_send */)) {
      if (prog) {
         prog->LinkStatus = false;
         ralloc_strcat(&prog->InfoLog, v.fail_msg);
      }

      _mesa_problem(NULL, "Failed to compile fragment shader: %s\n",
                    v.fail_msg);

      return NULL;
   }

   cfg_t *simd16_cfg = NULL;
   fs_visitor v2(brw->intelScreen->compiler, brw,
                 mem_ctx, MESA_SHADER_FRAGMENT, key, &prog_data->base,
                 prog, &fp->Base, 16, st_index16);
   if (likely(!(INTEL_DEBUG & DEBUG_NO16) || brw->use_rep_send)) {
      if (!v.simd16_unsupported) {
         /* Try a SIMD16 compile */
         v2.import_uniforms(&v);
         if (!v2.run_fs(brw->use_rep_send)) {
            perf_debug("SIMD16 shader failed to compile: %s", v2.fail_msg);
         } else {
            simd16_cfg = v2.cfg;
         }
      }
   }

   cfg_t *simd8_cfg;
   int no_simd8 = (INTEL_DEBUG & DEBUG_NO8) || brw->no_simd8;
   if ((no_simd8 || brw->gen < 5) && simd16_cfg) {
      simd8_cfg = NULL;
      prog_data->no_8 = true;
   } else {
      simd8_cfg = v.cfg;
      prog_data->no_8 = false;
   }

   fs_generator g(brw->intelScreen->compiler, brw,
                  mem_ctx, (void *) key, &prog_data->base,
                  &fp->Base, v.promoted_constants, v.runtime_check_aads_emit, "FS");

   if (unlikely(INTEL_DEBUG & DEBUG_WM)) {
      char *name;
      if (prog)
         name = ralloc_asprintf(mem_ctx, "%s fragment shader %d",
                                prog->Label ? prog->Label : "unnamed",
                                prog->Name);
      else
         name = ralloc_asprintf(mem_ctx, "fragment program %d", fp->Base.Id);

      g.enable_debug(name);
   }

   if (simd8_cfg)
      g.generate_code(simd8_cfg, 8);
   if (simd16_cfg)
      prog_data->prog_offset_16 = g.generate_code(simd16_cfg, 16);

   if (unlikely(brw->perf_debug) && shader) {
      if (shader->compiled_once)
         brw_wm_debug_recompile(brw, prog, key);
      shader->compiled_once = true;

      if (start_busy && !drm_intel_bo_busy(brw->batch.last_bo)) {
         perf_debug("FS compile took %.03f ms and stalled the GPU\n",
                    (get_time() - start_time) * 1000);
      }
   }

   return g.get_assembly(final_assembly_size);
}

extern "C" bool
brw_fs_precompile(struct gl_context *ctx,
                  struct gl_shader_program *shader_prog,
                  struct gl_program *prog)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_wm_prog_key key;

   struct gl_fragment_program *fp = (struct gl_fragment_program *) prog;
   struct brw_fragment_program *bfp = brw_fragment_program(fp);
   bool program_uses_dfdy = fp->UsesDFdy;

   memset(&key, 0, sizeof(key));

   if (brw->gen < 6) {
      if (fp->UsesKill)
         key.iz_lookup |= IZ_PS_KILL_ALPHATEST_BIT;

      if (fp->Base.OutputsWritten & BITFIELD64_BIT(FRAG_RESULT_DEPTH))
         key.iz_lookup |= IZ_PS_COMPUTES_DEPTH_BIT;

      /* Just assume depth testing. */
      key.iz_lookup |= IZ_DEPTH_TEST_ENABLE_BIT;
      key.iz_lookup |= IZ_DEPTH_WRITE_ENABLE_BIT;
   }

   if (brw->gen < 6 || _mesa_bitcount_64(fp->Base.InputsRead &
                                         BRW_FS_VARYING_INPUT_MASK) > 16)
      key.input_slots_valid = fp->Base.InputsRead | VARYING_BIT_POS;

   brw_setup_tex_for_precompile(brw, &key.tex, &fp->Base);

   if (fp->Base.InputsRead & VARYING_BIT_POS) {
      key.drawable_height = ctx->DrawBuffer->Height;
   }

   key.nr_color_regions = _mesa_bitcount_64(fp->Base.OutputsWritten &
         ~(BITFIELD64_BIT(FRAG_RESULT_DEPTH) |
         BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK)));

   if ((fp->Base.InputsRead & VARYING_BIT_POS) || program_uses_dfdy) {
      key.render_to_fbo = _mesa_is_user_fbo(ctx->DrawBuffer) ||
                          key.nr_color_regions > 1;
   }

   key.program_string_id = bfp->id;

   uint32_t old_prog_offset = brw->wm.base.prog_offset;
   struct brw_wm_prog_data *old_prog_data = brw->wm.prog_data;

   bool success = brw_codegen_wm_prog(brw, shader_prog, bfp, &key);

   brw->wm.base.prog_offset = old_prog_offset;
   brw->wm.prog_data = old_prog_data;

   return success;
}

void
brw_setup_tex_for_precompile(struct brw_context *brw,
                             struct brw_sampler_prog_key_data *tex,
                             struct gl_program *prog)
{
   const bool has_shader_channel_select = brw->is_haswell || brw->gen >= 8;
   unsigned sampler_count = _mesa_fls(prog->SamplersUsed);
   for (unsigned i = 0; i < sampler_count; i++) {
      if (!has_shader_channel_select && (prog->ShadowSamplers & (1 << i))) {
         /* Assume DEPTH_TEXTURE_MODE is the default: X, X, X, 1 */
         tex->swizzles[i] =
            MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_X, SWIZZLE_X, SWIZZLE_ONE);
      } else {
         /* Color sampler: assume no swizzling. */
         tex->swizzles[i] = SWIZZLE_XYZW;
      }
   }
}
