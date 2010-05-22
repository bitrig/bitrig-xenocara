/*
 Copyright (C) Intel Corp.  2006.  All Rights Reserved.
 Intel funded Tungsten Graphics (http://www.tungstengraphics.com) to
 develop this 3D driver.
 
 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:
 
 The above copyright notice and this permission notice (including the
 next paragraph) shall be included in all copies or substantial
 portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 
 **********************************************************************/
 /*
  * Authors:
  *   Keith Whitwell <keith@tungstengraphics.com>
  */
               

#include "main/macros.h"
#include "brw_context.h"
#include "brw_wm.h"

/* Not quite sure how correct this is - need to understand horiz
 * vs. vertical strides a little better.
 */
static INLINE struct brw_reg sechalf( struct brw_reg reg )
{
   if (reg.vstride)
      reg.nr++;
   return reg;
}


/* Payload R0:
 *
 * R0.0 -- pixel mask, one bit for each of 4 pixels in 4 tiles,
 *         corresponding to each of the 16 execution channels.
 * R0.1..8 -- ?
 * R1.0 -- triangle vertex 0.X
 * R1.1 -- triangle vertex 0.Y
 * R1.2 -- tile 0 x,y coords (2 packed uwords)
 * R1.3 -- tile 1 x,y coords (2 packed uwords)
 * R1.4 -- tile 2 x,y coords (2 packed uwords)
 * R1.5 -- tile 3 x,y coords (2 packed uwords)
 * R1.6 -- ?
 * R1.7 -- ?
 * R1.8 -- ?
 */

void emit_pixel_xy(struct brw_wm_compile *c,
		   const struct brw_reg *dst,
		   GLuint mask)
{
   struct brw_compile *p = &c->func;
   struct brw_reg r1 = brw_vec1_grf(1, 0);
   struct brw_reg r1_uw = retype(r1, BRW_REGISTER_TYPE_UW);
   struct brw_reg dst0_uw, dst1_uw;

   brw_push_insn_state(p);
   brw_set_compression_control(p, BRW_COMPRESSION_NONE);

   if (c->dispatch_width == 16) {
      dst0_uw = vec16(retype(dst[0], BRW_REGISTER_TYPE_UW));
      dst1_uw = vec16(retype(dst[1], BRW_REGISTER_TYPE_UW));
   } else {
      dst0_uw = vec8(retype(dst[0], BRW_REGISTER_TYPE_UW));
      dst1_uw = vec8(retype(dst[1], BRW_REGISTER_TYPE_UW));
   }

   /* Calculate pixel centers by adding 1 or 0 to each of the
    * micro-tile coordinates passed in r1.
    */
   if (mask & WRITEMASK_X) {
      brw_ADD(p,
	      dst0_uw,
	      stride(suboffset(r1_uw, 4), 2, 4, 0),
	      brw_imm_v(0x10101010));
   }

   if (mask & WRITEMASK_Y) {
      brw_ADD(p,
	      dst1_uw,
	      stride(suboffset(r1_uw,5), 2, 4, 0),
	      brw_imm_v(0x11001100));
   }
   brw_pop_insn_state(p);
}


void emit_delta_xy(struct brw_compile *p,
		   const struct brw_reg *dst,
		   GLuint mask,
		   const struct brw_reg *arg0)
{
   struct brw_reg r1 = brw_vec1_grf(1, 0);

   /* Calc delta X,Y by subtracting origin in r1 from the pixel
    * centers.
    */
   if (mask & WRITEMASK_X) {
      brw_ADD(p,
	      dst[0],
	      retype(arg0[0], BRW_REGISTER_TYPE_UW),
	      negate(r1));
   }

   if (mask & WRITEMASK_Y) {
      brw_ADD(p,
	      dst[1],
	      retype(arg0[1], BRW_REGISTER_TYPE_UW),
	      negate(suboffset(r1,1)));

   }
}

void emit_wpos_xy(struct brw_wm_compile *c,
		  const struct brw_reg *dst,
		  GLuint mask,
		  const struct brw_reg *arg0)
{
   struct brw_compile *p = &c->func;

   /* Calculate the pixel offset from window bottom left into destination
    * X and Y channels.
    */
   if (mask & WRITEMASK_X) {
      if (c->fp->program.PixelCenterInteger) {
	 /* X' = X */
	 brw_MOV(p,
		 dst[0],
		 retype(arg0[0], BRW_REGISTER_TYPE_W));
      } else {
	 /* X' = X + 0.5 */
	 brw_ADD(p,
		 dst[0],
		 retype(arg0[0], BRW_REGISTER_TYPE_W),
		 brw_imm_f(0.5));
      }
   }

   if (mask & WRITEMASK_Y) {
      if (c->fp->program.OriginUpperLeft) {
	 if (c->fp->program.PixelCenterInteger) {
	    /* Y' = Y */
	    brw_MOV(p,
		    dst[1],
		    retype(arg0[1], BRW_REGISTER_TYPE_W));
	 } else {
	    /* Y' = Y + 0.5 */
	    brw_ADD(p,
		    dst[1],
		    retype(arg0[1], BRW_REGISTER_TYPE_W),
		    brw_imm_f(0.5));
	 }
      } else {
	 float center_offset = c->fp->program.PixelCenterInteger ? 0.0 : 0.5;

	 /* Y' = (height - 1) - Y + center */
	 brw_ADD(p,
		 dst[1],
		 negate(retype(arg0[1], BRW_REGISTER_TYPE_W)),
		 brw_imm_f(c->key.drawable_height - 1 + center_offset));
      }
   }
}


void emit_pixel_w(struct brw_wm_compile *c,
		  const struct brw_reg *dst,
		  GLuint mask,
		  const struct brw_reg *arg0,
		  const struct brw_reg *deltas)
{
   struct brw_compile *p = &c->func;

   /* Don't need this if all you are doing is interpolating color, for
    * instance.
    */
   if (mask & WRITEMASK_W) {      
      struct brw_reg interp3 = brw_vec1_grf(arg0[0].nr+1, 4);

      /* Calc 1/w - just linterp wpos[3] optimized by putting the
       * result straight into a message reg.
       */
      brw_LINE(p, brw_null_reg(), interp3, deltas[0]);
      brw_MAC(p, brw_message_reg(2), suboffset(interp3, 1), deltas[1]);

      /* Calc w */
      if (c->dispatch_width == 16) {
	 brw_math_16(p, dst[3],
		     BRW_MATH_FUNCTION_INV,
		     BRW_MATH_SATURATE_NONE,
		     2, brw_null_reg(),
		     BRW_MATH_PRECISION_FULL);
      } else {
	 brw_math(p, dst[3],
		  BRW_MATH_FUNCTION_INV,
		  BRW_MATH_SATURATE_NONE,
		  2, brw_null_reg(),
		  BRW_MATH_DATA_VECTOR,
		  BRW_MATH_PRECISION_FULL);
      }
   }
}


void emit_linterp(struct brw_compile *p,
		  const struct brw_reg *dst,
		  GLuint mask,
		  const struct brw_reg *arg0,
		  const struct brw_reg *deltas)
{
   struct brw_reg interp[4];
   GLuint nr = arg0[0].nr;
   GLuint i;

   interp[0] = brw_vec1_grf(nr, 0);
   interp[1] = brw_vec1_grf(nr, 4);
   interp[2] = brw_vec1_grf(nr+1, 0);
   interp[3] = brw_vec1_grf(nr+1, 4);

   for (i = 0; i < 4; i++) {
      if (mask & (1<<i)) {
	 brw_LINE(p, brw_null_reg(), interp[i], deltas[0]);
	 brw_MAC(p, dst[i], suboffset(interp[i],1), deltas[1]);
      }
   }
}


void emit_pinterp(struct brw_compile *p,
		  const struct brw_reg *dst,
		  GLuint mask,
		  const struct brw_reg *arg0,
		  const struct brw_reg *deltas,
		  const struct brw_reg *w)
{
   struct brw_reg interp[4];
   GLuint nr = arg0[0].nr;
   GLuint i;

   interp[0] = brw_vec1_grf(nr, 0);
   interp[1] = brw_vec1_grf(nr, 4);
   interp[2] = brw_vec1_grf(nr+1, 0);
   interp[3] = brw_vec1_grf(nr+1, 4);

   for (i = 0; i < 4; i++) {
      if (mask & (1<<i)) {
	 brw_LINE(p, brw_null_reg(), interp[i], deltas[0]);
	 brw_MAC(p, dst[i], suboffset(interp[i],1), deltas[1]);
      }
   }
   for (i = 0; i < 4; i++) {
      if (mask & (1<<i)) {
	 brw_MUL(p, dst[i], dst[i], w[3]);
      }
   }
}


void emit_cinterp(struct brw_compile *p,
		  const struct brw_reg *dst,
		  GLuint mask,
		  const struct brw_reg *arg0)
{
   struct brw_reg interp[4];
   GLuint nr = arg0[0].nr;
   GLuint i;

   interp[0] = brw_vec1_grf(nr, 0);
   interp[1] = brw_vec1_grf(nr, 4);
   interp[2] = brw_vec1_grf(nr+1, 0);
   interp[3] = brw_vec1_grf(nr+1, 4);

   for (i = 0; i < 4; i++) {
      if (mask & (1<<i)) {
         brw_MOV(p, dst[i], suboffset(interp[i],3));	/* TODO: optimize away like other moves */
      }
   }
}

/* Sets the destination channels to 1.0 or 0.0 according to glFrontFacing. */
void emit_frontfacing(struct brw_compile *p,
		      const struct brw_reg *dst,
		      GLuint mask)
{
   struct brw_reg r1_6ud = retype(brw_vec1_grf(1, 6), BRW_REGISTER_TYPE_UD);
   GLuint i;

   if (!(mask & WRITEMASK_XYZW))
      return;

   for (i = 0; i < 4; i++) {
      if (mask & (1<<i)) {
	 brw_MOV(p, dst[i], brw_imm_f(0.0));
      }
   }

   /* bit 31 is "primitive is back face", so checking < (1 << 31) gives
    * us front face
    */
   brw_CMP(p, brw_null_reg(), BRW_CONDITIONAL_L, r1_6ud, brw_imm_ud(1 << 31));
   for (i = 0; i < 4; i++) {
      if (mask & (1<<i)) {
	 brw_MOV(p, dst[i], brw_imm_f(1.0));
      }
   }
   brw_set_predicate_control_flag_value(p, 0xff);
}

/* For OPCODE_DDX and OPCODE_DDY, per channel of output we've got input
 * looking like:
 *
 * arg0: ss0.tl ss0.tr ss0.bl ss0.br ss1.tl ss1.tr ss1.bl ss1.br
 *
 * and we're trying to produce:
 *
 *           DDX                     DDY
 * dst: (ss0.tr - ss0.tl)     (ss0.tl - ss0.bl)
 *      (ss0.tr - ss0.tl)     (ss0.tr - ss0.br)
 *      (ss0.br - ss0.bl)     (ss0.tl - ss0.bl)
 *      (ss0.br - ss0.bl)     (ss0.tr - ss0.br)
 *      (ss1.tr - ss1.tl)     (ss1.tl - ss1.bl)
 *      (ss1.tr - ss1.tl)     (ss1.tr - ss1.br)
 *      (ss1.br - ss1.bl)     (ss1.tl - ss1.bl)
 *      (ss1.br - ss1.bl)     (ss1.tr - ss1.br)
 *
 * and add another set of two more subspans if in 16-pixel dispatch mode.
 *
 * For DDX, it ends up being easy: width = 2, horiz=0 gets us the same result
 * for each pair, and vertstride = 2 jumps us 2 elements after processing a
 * pair. But for DDY, it's harder, as we want to produce the pairs swizzled
 * between each other.  We could probably do it like ddx and swizzle the right
 * order later, but bail for now and just produce
 * ((ss0.tl - ss0.bl)x4 (ss1.tl - ss1.bl)x4)
 */
void emit_ddxy(struct brw_compile *p,
	       const struct brw_reg *dst,
	       GLuint mask,
	       GLboolean is_ddx,
	       const struct brw_reg *arg0)
{
   int i;
   struct brw_reg src0, src1;

   if (mask & SATURATE)
      brw_set_saturate(p, 1);
   for (i = 0; i < 4; i++ ) {
      if (mask & (1<<i)) {
	 if (is_ddx) {
	    src0 = brw_reg(arg0[i].file, arg0[i].nr, 1,
			   BRW_REGISTER_TYPE_F,
			   BRW_VERTICAL_STRIDE_2,
			   BRW_WIDTH_2,
			   BRW_HORIZONTAL_STRIDE_0,
			   BRW_SWIZZLE_XYZW, WRITEMASK_XYZW);
	    src1 = brw_reg(arg0[i].file, arg0[i].nr, 0,
			   BRW_REGISTER_TYPE_F,
			   BRW_VERTICAL_STRIDE_2,
			   BRW_WIDTH_2,
			   BRW_HORIZONTAL_STRIDE_0,
			   BRW_SWIZZLE_XYZW, WRITEMASK_XYZW);
	 } else {
	    src0 = brw_reg(arg0[i].file, arg0[i].nr, 0,
			   BRW_REGISTER_TYPE_F,
			   BRW_VERTICAL_STRIDE_4,
			   BRW_WIDTH_4,
			   BRW_HORIZONTAL_STRIDE_0,
			   BRW_SWIZZLE_XYZW, WRITEMASK_XYZW);
	    src1 = brw_reg(arg0[i].file, arg0[i].nr, 2,
			   BRW_REGISTER_TYPE_F,
			   BRW_VERTICAL_STRIDE_4,
			   BRW_WIDTH_4,
			   BRW_HORIZONTAL_STRIDE_0,
			   BRW_SWIZZLE_XYZW, WRITEMASK_XYZW);
	 }
	 brw_ADD(p, dst[i], src0, negate(src1));
      }
   }
   if (mask & SATURATE)
      brw_set_saturate(p, 0);
}

void emit_alu1(struct brw_compile *p,
	       struct brw_instruction *(*func)(struct brw_compile *,
					       struct brw_reg,
					       struct brw_reg),
	       const struct brw_reg *dst,
	       GLuint mask,
	       const struct brw_reg *arg0)
{
   GLuint i;

   if (mask & SATURATE)
      brw_set_saturate(p, 1);

   for (i = 0; i < 4; i++) {
      if (mask & (1<<i)) {
	 func(p, dst[i], arg0[i]);
      }
   }

   if (mask & SATURATE)
      brw_set_saturate(p, 0);
}


void emit_alu2(struct brw_compile *p,
	       struct brw_instruction *(*func)(struct brw_compile *,
					       struct brw_reg,
					       struct brw_reg,
					       struct brw_reg),
	       const struct brw_reg *dst,
	       GLuint mask,
	       const struct brw_reg *arg0,
	       const struct brw_reg *arg1)
{
   GLuint i;

   if (mask & SATURATE)
      brw_set_saturate(p, 1);

   for (i = 0; i < 4; i++) {
      if (mask & (1<<i)) {
	 func(p, dst[i], arg0[i], arg1[i]);
      }
   }

   if (mask & SATURATE)
      brw_set_saturate(p, 0);
}


void emit_mad(struct brw_compile *p,
	      const struct brw_reg *dst,
	      GLuint mask,
	      const struct brw_reg *arg0,
	      const struct brw_reg *arg1,
	      const struct brw_reg *arg2)
{
   GLuint i;

   for (i = 0; i < 4; i++) {
      if (mask & (1<<i)) {
	 brw_MUL(p, dst[i], arg0[i], arg1[i]);

	 brw_set_saturate(p, (mask & SATURATE) ? 1 : 0);
	 brw_ADD(p, dst[i], dst[i], arg2[i]);
	 brw_set_saturate(p, 0);
      }
   }
}

void emit_lrp(struct brw_compile *p,
	      const struct brw_reg *dst,
	      GLuint mask,
	      const struct brw_reg *arg0,
	      const struct brw_reg *arg1,
	      const struct brw_reg *arg2)
{
   GLuint i;

   /* Uses dst as a temporary:
    */
   for (i = 0; i < 4; i++) {
      if (mask & (1<<i)) {	
	 /* Can I use the LINE instruction for this? 
	  */
	 brw_ADD(p, dst[i], negate(arg0[i]), brw_imm_f(1.0));
	 brw_MUL(p, brw_null_reg(), dst[i], arg2[i]);

	 brw_set_saturate(p, (mask & SATURATE) ? 1 : 0);
	 brw_MAC(p, dst[i], arg0[i], arg1[i]);
	 brw_set_saturate(p, 0);
      }
   }
}

void emit_sop(struct brw_compile *p,
	      const struct brw_reg *dst,
	      GLuint mask,
	      GLuint cond,
	      const struct brw_reg *arg0,
	      const struct brw_reg *arg1)
{
   GLuint i;

   for (i = 0; i < 4; i++) {
      if (mask & (1<<i)) {	
	 brw_push_insn_state(p);
	 brw_CMP(p, brw_null_reg(), cond, arg0[i], arg1[i]);
	 brw_set_predicate_control(p, BRW_PREDICATE_NONE);
	 brw_MOV(p, dst[i], brw_imm_f(0));
	 brw_set_predicate_control(p, BRW_PREDICATE_NORMAL);
	 brw_MOV(p, dst[i], brw_imm_f(1.0));
	 brw_pop_insn_state(p);
      }
   }
}

static void emit_slt( struct brw_compile *p, 
		      const struct brw_reg *dst,
		      GLuint mask,
		      const struct brw_reg *arg0,
		      const struct brw_reg *arg1 )
{
   emit_sop(p, dst, mask, BRW_CONDITIONAL_L, arg0, arg1);
}

static void emit_sle( struct brw_compile *p, 
		      const struct brw_reg *dst,
		      GLuint mask,
		      const struct brw_reg *arg0,
		      const struct brw_reg *arg1 )
{
   emit_sop(p, dst, mask, BRW_CONDITIONAL_LE, arg0, arg1);
}

static void emit_sgt( struct brw_compile *p, 
		      const struct brw_reg *dst,
		      GLuint mask,
		      const struct brw_reg *arg0,
		      const struct brw_reg *arg1 )
{
   emit_sop(p, dst, mask, BRW_CONDITIONAL_G, arg0, arg1);
}

static void emit_sge( struct brw_compile *p, 
		      const struct brw_reg *dst,
		      GLuint mask,
		      const struct brw_reg *arg0,
		      const struct brw_reg *arg1 )
{
   emit_sop(p, dst, mask, BRW_CONDITIONAL_GE, arg0, arg1);
}

static void emit_seq( struct brw_compile *p, 
		      const struct brw_reg *dst,
		      GLuint mask,
		      const struct brw_reg *arg0,
		      const struct brw_reg *arg1 )
{
   emit_sop(p, dst, mask, BRW_CONDITIONAL_EQ, arg0, arg1);
}

static void emit_sne( struct brw_compile *p, 
		      const struct brw_reg *dst,
		      GLuint mask,
		      const struct brw_reg *arg0,
		      const struct brw_reg *arg1 )
{
   emit_sop(p, dst, mask, BRW_CONDITIONAL_NEQ, arg0, arg1);
}

void emit_cmp(struct brw_compile *p,
	      const struct brw_reg *dst,
	      GLuint mask,
	      const struct brw_reg *arg0,
	      const struct brw_reg *arg1,
	      const struct brw_reg *arg2)
{
   GLuint i;

   for (i = 0; i < 4; i++) {
      if (mask & (1<<i)) {	
	 brw_set_saturate(p, (mask & SATURATE) ? 1 : 0);
	 brw_MOV(p, dst[i], arg2[i]);
	 brw_set_saturate(p, 0);

	 brw_CMP(p, brw_null_reg(), BRW_CONDITIONAL_L, arg0[i], brw_imm_f(0));

	 brw_set_saturate(p, (mask & SATURATE) ? 1 : 0);
	 brw_MOV(p, dst[i], arg1[i]);
	 brw_set_saturate(p, 0);
	 brw_set_predicate_control_flag_value(p, 0xff);
      }
   }
}

void emit_max(struct brw_compile *p,
	      const struct brw_reg *dst,
	      GLuint mask,
	      const struct brw_reg *arg0,
	      const struct brw_reg *arg1)
{
   GLuint i;

   for (i = 0; i < 4; i++) {
      if (mask & (1<<i)) {	
	 brw_set_saturate(p, (mask & SATURATE) ? 1 : 0);
	 brw_MOV(p, dst[i], arg0[i]);
	 brw_set_saturate(p, 0);

	 brw_CMP(p, brw_null_reg(), BRW_CONDITIONAL_L, arg0[i], arg1[i]);

	 brw_set_saturate(p, (mask & SATURATE) ? 1 : 0);
	 brw_MOV(p, dst[i], arg1[i]);
	 brw_set_saturate(p, 0);
	 brw_set_predicate_control_flag_value(p, 0xff);
      }
   }
}

void emit_min(struct brw_compile *p,
	      const struct brw_reg *dst,
	      GLuint mask,
	      const struct brw_reg *arg0,
	      const struct brw_reg *arg1)
{
   GLuint i;

   for (i = 0; i < 4; i++) {
      if (mask & (1<<i)) {	
	 brw_set_saturate(p, (mask & SATURATE) ? 1 : 0);
	 brw_MOV(p, dst[i], arg1[i]);
	 brw_set_saturate(p, 0);

	 brw_CMP(p, brw_null_reg(), BRW_CONDITIONAL_L, arg0[i], arg1[i]);

	 brw_set_saturate(p, (mask & SATURATE) ? 1 : 0);
	 brw_MOV(p, dst[i], arg0[i]);
	 brw_set_saturate(p, 0);
	 brw_set_predicate_control_flag_value(p, 0xff);
      }
   }
}


void emit_dp3(struct brw_compile *p,
	      const struct brw_reg *dst,
	      GLuint mask,
	      const struct brw_reg *arg0,
	      const struct brw_reg *arg1)
{
   int dst_chan = _mesa_ffs(mask & WRITEMASK_XYZW) - 1;

   if (!(mask & WRITEMASK_XYZW))
      return; /* Do not emit dead code */

   assert(is_power_of_two(mask & WRITEMASK_XYZW));

   brw_MUL(p, brw_null_reg(), arg0[0], arg1[0]);
   brw_MAC(p, brw_null_reg(), arg0[1], arg1[1]);

   brw_set_saturate(p, (mask & SATURATE) ? 1 : 0);
   brw_MAC(p, dst[dst_chan], arg0[2], arg1[2]);
   brw_set_saturate(p, 0);
}


void emit_dp4(struct brw_compile *p,
	      const struct brw_reg *dst,
	      GLuint mask,
	      const struct brw_reg *arg0,
	      const struct brw_reg *arg1)
{
   int dst_chan = _mesa_ffs(mask & WRITEMASK_XYZW) - 1;

   if (!(mask & WRITEMASK_XYZW))
      return; /* Do not emit dead code */

   assert(is_power_of_two(mask & WRITEMASK_XYZW));

   brw_MUL(p, brw_null_reg(), arg0[0], arg1[0]);
   brw_MAC(p, brw_null_reg(), arg0[1], arg1[1]);
   brw_MAC(p, brw_null_reg(), arg0[2], arg1[2]);

   brw_set_saturate(p, (mask & SATURATE) ? 1 : 0);
   brw_MAC(p, dst[dst_chan], arg0[3], arg1[3]);
   brw_set_saturate(p, 0);
}


void emit_dph(struct brw_compile *p,
	      const struct brw_reg *dst,
	      GLuint mask,
	      const struct brw_reg *arg0,
	      const struct brw_reg *arg1)
{
   const int dst_chan = _mesa_ffs(mask & WRITEMASK_XYZW) - 1;

   if (!(mask & WRITEMASK_XYZW))
      return; /* Do not emit dead code */

   assert(is_power_of_two(mask & WRITEMASK_XYZW));

   brw_MUL(p, brw_null_reg(), arg0[0], arg1[0]);
   brw_MAC(p, brw_null_reg(), arg0[1], arg1[1]);
   brw_MAC(p, dst[dst_chan], arg0[2], arg1[2]);

   brw_set_saturate(p, (mask & SATURATE) ? 1 : 0);
   brw_ADD(p, dst[dst_chan], dst[dst_chan], arg1[3]);
   brw_set_saturate(p, 0);
}


void emit_xpd(struct brw_compile *p,
	      const struct brw_reg *dst,
	      GLuint mask,
	      const struct brw_reg *arg0,
	      const struct brw_reg *arg1)
{
   GLuint i;

   assert((mask & WRITEMASK_W) != WRITEMASK_W);
   
   for (i = 0 ; i < 3; i++) {
      if (mask & (1<<i)) {
	 GLuint i2 = (i+2)%3;
	 GLuint i1 = (i+1)%3;

	 brw_MUL(p, brw_null_reg(), negate(arg0[i2]), arg1[i1]);

	 brw_set_saturate(p, (mask & SATURATE) ? 1 : 0);
	 brw_MAC(p, dst[i], arg0[i1], arg1[i2]);
	 brw_set_saturate(p, 0);
      }
   }
}


void emit_math1(struct brw_wm_compile *c,
		GLuint function,
		const struct brw_reg *dst,
		GLuint mask,
		const struct brw_reg *arg0)
{
   struct brw_compile *p = &c->func;
   int dst_chan = _mesa_ffs(mask & WRITEMASK_XYZW) - 1;
   GLuint saturate = ((mask & SATURATE) ?
		      BRW_MATH_SATURATE_SATURATE :
		      BRW_MATH_SATURATE_NONE);

   if (!(mask & WRITEMASK_XYZW))
      return; /* Do not emit dead code */

   assert(is_power_of_two(mask & WRITEMASK_XYZW));

   /* If compressed, this will write message reg 2,3 from arg0.x's 16
    * channels.
    */
   brw_MOV(p, brw_message_reg(2), arg0[0]);

   /* Send two messages to perform all 16 operations:
    */
   brw_push_insn_state(p);
   brw_set_compression_control(p, BRW_COMPRESSION_NONE);
   brw_math(p,
	    dst[dst_chan],
	    function,
	    saturate,
	    2,
	    brw_null_reg(),
	    BRW_MATH_DATA_VECTOR,
	    BRW_MATH_PRECISION_FULL);

   if (c->dispatch_width == 16) {
      brw_set_compression_control(p, BRW_COMPRESSION_2NDHALF);
      brw_math(p,
	       offset(dst[dst_chan],1),
	       function,
	       saturate,
	       3,
	       brw_null_reg(),
	       BRW_MATH_DATA_VECTOR,
	       BRW_MATH_PRECISION_FULL);
   }
   brw_pop_insn_state(p);
}


void emit_math2(struct brw_wm_compile *c,
		GLuint function,
		const struct brw_reg *dst,
		GLuint mask,
		const struct brw_reg *arg0,
		const struct brw_reg *arg1)
{
   struct brw_compile *p = &c->func;
   int dst_chan = _mesa_ffs(mask & WRITEMASK_XYZW) - 1;
   GLuint saturate = ((mask & SATURATE) ?
		      BRW_MATH_SATURATE_SATURATE :
		      BRW_MATH_SATURATE_NONE);

   if (!(mask & WRITEMASK_XYZW))
      return; /* Do not emit dead code */

   assert(is_power_of_two(mask & WRITEMASK_XYZW));

   brw_push_insn_state(p);

   brw_set_compression_control(p, BRW_COMPRESSION_NONE);
   brw_MOV(p, brw_message_reg(2), arg0[0]);
   if (c->dispatch_width == 16) {
      brw_set_compression_control(p, BRW_COMPRESSION_2NDHALF);
      brw_MOV(p, brw_message_reg(4), sechalf(arg0[0]));
   }

   brw_set_compression_control(p, BRW_COMPRESSION_NONE);
   brw_MOV(p, brw_message_reg(3), arg1[0]);
   if (c->dispatch_width == 16) {
      brw_set_compression_control(p, BRW_COMPRESSION_2NDHALF);
      brw_MOV(p, brw_message_reg(5), sechalf(arg1[0]));
   }

   brw_set_compression_control(p, BRW_COMPRESSION_NONE);
   brw_math(p, 
	    dst[dst_chan],
	    function,
	    saturate,
	    2,
	    brw_null_reg(),
	    BRW_MATH_DATA_VECTOR,
	    BRW_MATH_PRECISION_FULL);

   /* Send two messages to perform all 16 operations:
    */
   if (c->dispatch_width == 16) {
      brw_set_compression_control(p, BRW_COMPRESSION_2NDHALF);
      brw_math(p,
	       offset(dst[dst_chan],1),
	       function,
	       saturate,
	       4,
	       brw_null_reg(),
	       BRW_MATH_DATA_VECTOR,
	       BRW_MATH_PRECISION_FULL);
   }
   brw_pop_insn_state(p);
}


void emit_tex(struct brw_wm_compile *c,
	      struct brw_reg *dst,
	      GLuint dst_flags,
	      struct brw_reg *arg,
	      struct brw_reg depth_payload,
	      GLuint tex_idx,
	      GLuint sampler,
	      GLboolean shadow)
{
   struct brw_compile *p = &c->func;
   struct intel_context *intel = &p->brw->intel;
   struct brw_reg dst_retyped;
   GLuint cur_mrf = 2, response_length;
   GLuint i, nr_texcoords;
   GLuint emit;
   GLuint msg_type;
   GLuint mrf_per_channel;
   GLuint simd_mode;

   if (c->dispatch_width == 16) {
      mrf_per_channel = 2;
      response_length = 8;
      dst_retyped = retype(vec16(dst[0]), BRW_REGISTER_TYPE_UW);
      simd_mode = BRW_SAMPLER_SIMD_MODE_SIMD16;
   } else {
      mrf_per_channel = 1;
      response_length = 4;
      dst_retyped = retype(vec8(dst[0]), BRW_REGISTER_TYPE_UW);
      simd_mode = BRW_SAMPLER_SIMD_MODE_SIMD8;
   }

   /* How many input regs are there?
    */
   switch (tex_idx) {
   case TEXTURE_1D_INDEX:
      emit = WRITEMASK_X;
      nr_texcoords = 1;
      break;
   case TEXTURE_2D_INDEX:
   case TEXTURE_RECT_INDEX:
      emit = WRITEMASK_XY;
      nr_texcoords = 2;
      break;
   case TEXTURE_3D_INDEX:
   case TEXTURE_CUBE_INDEX:
      emit = WRITEMASK_XYZ;
      nr_texcoords = 3;
      break;
   default:
      /* unexpected target */
      abort();
   }

   /* Pre-Ironlake, the 8-wide sampler always took u,v,r. */
   if (intel->gen < 5 && c->dispatch_width == 8)
      nr_texcoords = 3;

   /* For shadow comparisons, we have to supply u,v,r. */
   if (shadow)
      nr_texcoords = 3;

   /* Emit the texcoords. */
   for (i = 0; i < nr_texcoords; i++) {
      if (emit & (1<<i))
	 brw_MOV(p, brw_message_reg(cur_mrf), arg[i]);
      else
	 brw_MOV(p, brw_message_reg(cur_mrf), brw_imm_f(0));
      cur_mrf += mrf_per_channel;
   }

   /* Fill in the shadow comparison reference value. */
   if (shadow) {
      if (intel->gen == 5) {
	 /* Fill in the cube map array index value. */
	 brw_MOV(p, brw_message_reg(cur_mrf), brw_imm_f(0));
	 cur_mrf += mrf_per_channel;
      } else if (c->dispatch_width == 8) {
	 /* Fill in the LOD bias value. */
	 brw_MOV(p, brw_message_reg(cur_mrf), brw_imm_f(0));
	 cur_mrf += mrf_per_channel;
      }
      brw_MOV(p, brw_message_reg(cur_mrf), arg[2]);
      cur_mrf += mrf_per_channel;
   }

   if (intel->gen == 5) {
      if (shadow)
	 msg_type = BRW_SAMPLER_MESSAGE_SAMPLE_COMPARE_GEN5;
      else
	 msg_type = BRW_SAMPLER_MESSAGE_SAMPLE_GEN5;
   } else {
      /* Note that G45 and older determines shadow compare and dispatch width
       * from message length for most messages.
       */
      if (c->dispatch_width == 16 && shadow)
	 msg_type = BRW_SAMPLER_MESSAGE_SIMD16_SAMPLE_COMPARE;
      else
	 msg_type = BRW_SAMPLER_MESSAGE_SIMD16_SAMPLE;
   }

   brw_SAMPLE(p,
	      dst_retyped,
	      1,
	      retype(depth_payload, BRW_REGISTER_TYPE_UW),
              SURF_INDEX_TEXTURE(sampler),
	      sampler,
	      dst_flags & WRITEMASK_XYZW,
	      msg_type,
	      response_length,
	      cur_mrf - 1,
	      0,
	      1,
	      simd_mode);
}


void emit_txb(struct brw_wm_compile *c,
	      struct brw_reg *dst,
	      GLuint dst_flags,
	      struct brw_reg *arg,
	      struct brw_reg depth_payload,
	      GLuint tex_idx,
	      GLuint sampler)
{
   struct brw_compile *p = &c->func;
   struct intel_context *intel = &p->brw->intel;
   GLuint msgLength;
   GLuint msg_type;
   GLuint mrf_per_channel;
   GLuint response_length;
   struct brw_reg dst_retyped;

   /* The G45 and older chipsets don't support 8-wide dispatch for LOD biased
    * samples, so we'll use the 16-wide instruction, leave the second halves
    * undefined, and trust the execution mask to keep the undefined pixels
    * from mattering.
    */
   if (c->dispatch_width == 16 || intel->gen < 5) {
      if (intel->gen == 5)
	 msg_type = BRW_SAMPLER_MESSAGE_SAMPLE_BIAS_GEN5;
      else
	 msg_type = BRW_SAMPLER_MESSAGE_SIMD16_SAMPLE_BIAS;
      mrf_per_channel = 2;
      dst_retyped = retype(vec16(dst[0]), BRW_REGISTER_TYPE_UW);
      response_length = 8;
   } else {
      msg_type = BRW_SAMPLER_MESSAGE_SAMPLE_BIAS_GEN5;
      mrf_per_channel = 1;
      dst_retyped = retype(vec8(dst[0]), BRW_REGISTER_TYPE_UW);
      response_length = 4;
   }

   /* Shadow ignored for txb. */
   switch (tex_idx) {
   case TEXTURE_1D_INDEX:
      brw_MOV(p, brw_message_reg(2 + 0 * mrf_per_channel), arg[0]);
      brw_MOV(p, brw_message_reg(2 + 1 * mrf_per_channel), brw_imm_f(0));
      brw_MOV(p, brw_message_reg(2 + 2 * mrf_per_channel), brw_imm_f(0));
      break;
   case TEXTURE_2D_INDEX:
   case TEXTURE_RECT_INDEX:
      brw_MOV(p, brw_message_reg(2 + 0 * mrf_per_channel), arg[0]);
      brw_MOV(p, brw_message_reg(2 + 1 * mrf_per_channel), arg[1]);
      brw_MOV(p, brw_message_reg(2 + 2 * mrf_per_channel), brw_imm_f(0));
      break;
   case TEXTURE_3D_INDEX:
   case TEXTURE_CUBE_INDEX:
      brw_MOV(p, brw_message_reg(2 + 0 * mrf_per_channel), arg[0]);
      brw_MOV(p, brw_message_reg(2 + 1 * mrf_per_channel), arg[1]);
      brw_MOV(p, brw_message_reg(2 + 2 * mrf_per_channel), arg[2]);
      break;
   default:
      /* unexpected target */
      abort();
   }

   brw_MOV(p, brw_message_reg(2 + 3 * mrf_per_channel), arg[3]);
   msgLength = 2 + 4 * mrf_per_channel - 1;

   brw_SAMPLE(p, 
	      dst_retyped,
	      1,
	      retype(depth_payload, BRW_REGISTER_TYPE_UW),
              SURF_INDEX_TEXTURE(sampler),
	      sampler,
	      dst_flags & WRITEMASK_XYZW,
	      msg_type,
	      response_length,
	      msgLength,
	      0,	
	      1,
	      BRW_SAMPLER_SIMD_MODE_SIMD16);	
}


static void emit_lit(struct brw_wm_compile *c,
		     const struct brw_reg *dst,
		     GLuint mask,
		     const struct brw_reg *arg0)
{
   struct brw_compile *p = &c->func;

   assert((mask & WRITEMASK_XW) == 0);

   if (mask & WRITEMASK_Y) {
      brw_set_saturate(p, (mask & SATURATE) ? 1 : 0);
      brw_MOV(p, dst[1], arg0[0]);
      brw_set_saturate(p, 0);
   }

   if (mask & WRITEMASK_Z) {
      emit_math2(c, BRW_MATH_FUNCTION_POW,
		 &dst[2],
		 WRITEMASK_X | (mask & SATURATE),
		 &arg0[1],
		 &arg0[3]);
   }

   /* Ordinarily you'd use an iff statement to skip or shortcircuit
    * some of the POW calculations above, but 16-wide iff statements
    * seem to lock c1 hardware, so this is a nasty workaround:
    */
   brw_CMP(p, brw_null_reg(), BRW_CONDITIONAL_LE, arg0[0], brw_imm_f(0));
   {
      if (mask & WRITEMASK_Y) 
	 brw_MOV(p, dst[1], brw_imm_f(0));

      if (mask & WRITEMASK_Z) 
	 brw_MOV(p, dst[2], brw_imm_f(0)); 
   }
   brw_set_predicate_control(p, BRW_PREDICATE_NONE);
}


/* Kill pixel - set execution mask to zero for those pixels which
 * fail.
 */
static void emit_kil( struct brw_wm_compile *c,
		      struct brw_reg *arg0)
{
   struct brw_compile *p = &c->func;
   struct brw_reg r0uw = retype(brw_vec1_grf(0, 0), BRW_REGISTER_TYPE_UW);
   GLuint i;
   
   /* XXX - usually won't need 4 compares!
    */
   for (i = 0; i < 4; i++) {
      brw_push_insn_state(p);
      brw_CMP(p, brw_null_reg(), BRW_CONDITIONAL_GE, arg0[i], brw_imm_f(0));   
      brw_set_predicate_control_flag_value(p, 0xff);
      brw_set_compression_control(p, BRW_COMPRESSION_NONE);
      brw_AND(p, r0uw, brw_flag_reg(), r0uw);
      brw_pop_insn_state(p);
   }
}

/* KIL_NV kills the pixels that are currently executing, not based on a test
 * of the arguments.
 */
static void emit_kil_nv( struct brw_wm_compile *c )
{
   struct brw_compile *p = &c->func;
   struct brw_reg r0uw = retype(brw_vec1_grf(0, 0), BRW_REGISTER_TYPE_UW);

   brw_push_insn_state(p);
   brw_set_mask_control(p, BRW_MASK_DISABLE);
   brw_NOT(p, c->emit_mask_reg, brw_mask_reg(1)); /* IMASK */
   brw_AND(p, r0uw, c->emit_mask_reg, r0uw);
   brw_pop_insn_state(p);
}

static void fire_fb_write( struct brw_wm_compile *c,
			   GLuint base_reg,
			   GLuint nr,
			   GLuint target,
			   GLuint eot )
{
   struct brw_compile *p = &c->func;
   struct brw_reg dst;

   if (c->dispatch_width == 16)
      dst = retype(vec16(brw_null_reg()), BRW_REGISTER_TYPE_UW);
   else
      dst = retype(vec8(brw_null_reg()), BRW_REGISTER_TYPE_UW);

   /* Pass through control information:
    */
/*  mov (8) m1.0<1>:ud   r1.0<8;8,1>:ud   { Align1 NoMask } */
   {
      brw_push_insn_state(p);
      brw_set_mask_control(p, BRW_MASK_DISABLE); /* ? */
      brw_set_compression_control(p, BRW_COMPRESSION_NONE);
      brw_MOV(p, 
	       brw_message_reg(base_reg + 1),
	       brw_vec8_grf(1, 0));
      brw_pop_insn_state(p);
   }

   /* Send framebuffer write message: */
/*  send (16) null.0<1>:uw m0               r0.0<8;8,1>:uw   0x85a04000:ud    { Align1 EOT } */
   brw_fb_WRITE(p,
		dst,
		base_reg,
		retype(brw_vec8_grf(0, 0), BRW_REGISTER_TYPE_UW),
		target,		
		nr,
		0, 
		eot);
}


static void emit_aa( struct brw_wm_compile *c,
		     struct brw_reg *arg1,
		     GLuint reg )
{
   struct brw_compile *p = &c->func;
   GLuint comp = c->key.aa_dest_stencil_reg / 2;
   GLuint off = c->key.aa_dest_stencil_reg % 2;
   struct brw_reg aa = offset(arg1[comp], off);

   brw_push_insn_state(p);
   brw_set_compression_control(p, BRW_COMPRESSION_NONE); /* ?? */
   brw_MOV(p, brw_message_reg(reg), aa);
   brw_pop_insn_state(p);
}


/* Post-fragment-program processing.  Send the results to the
 * framebuffer.
 * \param arg0  the fragment color
 * \param arg1  the pass-through depth value
 * \param arg2  the shader-computed depth value
 */
void emit_fb_write(struct brw_wm_compile *c,
		   struct brw_reg *arg0,
		   struct brw_reg *arg1,
		   struct brw_reg *arg2,
		   GLuint target,
		   GLuint eot)
{
   struct brw_compile *p = &c->func;
   struct brw_context *brw = p->brw;
   GLuint nr = 2;
   GLuint channel;

   /* Reserve a space for AA - may not be needed:
    */
   if (c->key.aa_dest_stencil_reg)
      nr += 1;

   /* I don't really understand how this achieves the color interleave
    * (ie RGBARGBA) in the result:  [Do the saturation here]
    */
   brw_push_insn_state(p);

   for (channel = 0; channel < 4; channel++) {
      if (c->dispatch_width == 16 && brw->has_compr4) {
	 /* By setting the high bit of the MRF register number, we indicate
	  * that we want COMPR4 mode - instead of doing the usual destination
	  * + 1 for the second half we get destination + 4.
	  */
	 brw_MOV(p,
		 brw_message_reg(nr + channel + (1 << 7)),
		 arg0[channel]);
      } else {
	 /*  mov (8) m2.0<1>:ud   r28.0<8;8,1>:ud  { Align1 } */
	 /*  mov (8) m6.0<1>:ud   r29.0<8;8,1>:ud  { Align1 SecHalf } */
	 brw_set_compression_control(p, BRW_COMPRESSION_NONE);
	 brw_MOV(p,
		 brw_message_reg(nr + channel),
		 arg0[channel]);

	 if (c->dispatch_width == 16) {
	    brw_set_compression_control(p, BRW_COMPRESSION_2NDHALF);
	    brw_MOV(p,
		    brw_message_reg(nr + channel + 4),
		    sechalf(arg0[channel]));
	 }
      }
   }
   /* skip over the regs populated above:
    */
   nr += 8;
   brw_pop_insn_state(p);

   if (c->key.source_depth_to_render_target)
   {
      if (c->key.computes_depth) 
	 brw_MOV(p, brw_message_reg(nr), arg2[2]);
      else 
	 brw_MOV(p, brw_message_reg(nr), arg1[1]); /* ? */

      nr += 2;
   }

   if (c->key.dest_depth_reg)
   {
      GLuint comp = c->key.dest_depth_reg / 2;
      GLuint off = c->key.dest_depth_reg % 2;

      if (off != 0) {
         brw_push_insn_state(p);
         brw_set_compression_control(p, BRW_COMPRESSION_NONE);

         brw_MOV(p, brw_message_reg(nr), offset(arg1[comp],1));
         /* 2nd half? */
         brw_MOV(p, brw_message_reg(nr+1), arg1[comp+1]);
         brw_pop_insn_state(p);
      }
      else {
         brw_MOV(p, brw_message_reg(nr), arg1[comp]);
      }
      nr += 2;
   }

   if (!c->key.runtime_check_aads_emit) {
      if (c->key.aa_dest_stencil_reg)
	 emit_aa(c, arg1, 2);

      fire_fb_write(c, 0, nr, target, eot);
   }
   else {
      struct brw_reg v1_null_ud = vec1(retype(brw_null_reg(), BRW_REGISTER_TYPE_UD));
      struct brw_reg ip = brw_ip_reg();
      struct brw_instruction *jmp;
      
      brw_set_compression_control(p, BRW_COMPRESSION_NONE);
      brw_set_conditionalmod(p, BRW_CONDITIONAL_Z);
      brw_AND(p, 
	      v1_null_ud, 
	      get_element_ud(brw_vec8_grf(1,0), 6), 
	      brw_imm_ud(1<<26)); 

      jmp = brw_JMPI(p, ip, ip, brw_imm_w(0));
      {
	 emit_aa(c, arg1, 2);
	 fire_fb_write(c, 0, nr, target, eot);
	 /* note - thread killed in subroutine */
      }
      brw_land_fwd_jump(p, jmp);

      /* ELSE: Shuffle up one register to fill in the hole left for AA:
       */
      fire_fb_write(c, 1, nr-1, target, eot);
   }
}

/**
 * Move a GPR to scratch memory. 
 */
static void emit_spill( struct brw_wm_compile *c,
			struct brw_reg reg,
			GLuint slot )
{
   struct brw_compile *p = &c->func;

   /*
     mov (16) m2.0<1>:ud   r2.0<8;8,1>:ud   { Align1 Compr }
   */
   brw_MOV(p, brw_message_reg(2), reg);

   /*
     mov (1) r0.2<1>:d    0x00000080:d     { Align1 NoMask }
     send (16) null.0<1>:uw m1               r0.0<8;8,1>:uw   0x053003ff:ud    { Align1 }
   */
   brw_dp_WRITE_16(p, 
		   retype(vec16(brw_vec8_grf(0, 0)), BRW_REGISTER_TYPE_UW),
		   slot);
}


/**
 * Load a GPR from scratch memory. 
 */
static void emit_unspill( struct brw_wm_compile *c,
			  struct brw_reg reg,
			  GLuint slot )
{
   struct brw_compile *p = &c->func;

   /* Slot 0 is the undef value.
    */
   if (slot == 0) {
      brw_MOV(p, reg, brw_imm_f(0));
      return;
   }

   /*
     mov (1) r0.2<1>:d    0x000000c0:d     { Align1 NoMask }
     send (16) r110.0<1>:uw m1               r0.0<8;8,1>:uw   0x041243ff:ud    { Align1 }
   */

   brw_dp_READ_16(p,
		  retype(vec16(reg), BRW_REGISTER_TYPE_UW),
		  slot);
}


/**
 * Retrieve up to 4 GEN4 register pairs for the given wm reg:
 * Args with unspill_reg != 0 will be loaded from scratch memory.
 */
static void get_argument_regs( struct brw_wm_compile *c,
			       struct brw_wm_ref *arg[],
			       struct brw_reg *regs )
{
   GLuint i;

   for (i = 0; i < 4; i++) {
      if (arg[i]) {
	 if (arg[i]->unspill_reg)
	    emit_unspill(c,
			 brw_vec8_grf(arg[i]->unspill_reg, 0),
			 arg[i]->value->spill_slot);

	 regs[i] = arg[i]->hw_reg;
      }
      else {
	 regs[i] = brw_null_reg();
      }
   }
}


/**
 * For values that have a spill_slot!=0, write those regs to scratch memory.
 */
static void spill_values( struct brw_wm_compile *c,
			  struct brw_wm_value *values,
			  GLuint nr )
{
   GLuint i;

   for (i = 0; i < nr; i++)
      if (values[i].spill_slot) 
	 emit_spill(c, values[i].hw_reg, values[i].spill_slot);
}


/* Emit the fragment program instructions here.
 */
void brw_wm_emit( struct brw_wm_compile *c )
{
   struct brw_compile *p = &c->func;
   GLuint insn;

   brw_set_compression_control(p, BRW_COMPRESSION_COMPRESSED);

   /* Check if any of the payload regs need to be spilled:
    */
   spill_values(c, c->payload.depth, 4);
   spill_values(c, c->creg, c->nr_creg);
   spill_values(c, c->payload.input_interp, FRAG_ATTRIB_MAX);
   

   for (insn = 0; insn < c->nr_insns; insn++) {

      struct brw_wm_instruction *inst = &c->instruction[insn];
      struct brw_reg args[3][4], dst[4];
      GLuint i, dst_flags;
      
      /* Get argument regs:
       */
      for (i = 0; i < 3; i++) 
	 get_argument_regs(c, inst->src[i], args[i]);

      /* Get dest regs:
       */
      for (i = 0; i < 4; i++)
	 if (inst->dst[i])
	    dst[i] = inst->dst[i]->hw_reg;
	 else
	    dst[i] = brw_null_reg();
      
      /* Flags
       */
      dst_flags = inst->writemask;
      if (inst->saturate) 
	 dst_flags |= SATURATE;

      switch (inst->opcode) {
	 /* Generated instructions for calculating triangle interpolants:
	  */
      case WM_PIXELXY:
	 emit_pixel_xy(c, dst, dst_flags);
	 break;

      case WM_DELTAXY:
	 emit_delta_xy(p, dst, dst_flags, args[0]);
	 break;

      case WM_WPOSXY:
	 emit_wpos_xy(c, dst, dst_flags, args[0]);
	 break;

      case WM_PIXELW:
	 emit_pixel_w(c, dst, dst_flags, args[0], args[1]);
	 break;

      case WM_LINTERP:
	 emit_linterp(p, dst, dst_flags, args[0], args[1]);
	 break;

      case WM_PINTERP:
	 emit_pinterp(p, dst, dst_flags, args[0], args[1], args[2]);
	 break;

      case WM_CINTERP:
	 emit_cinterp(p, dst, dst_flags, args[0]);
	 break;

      case WM_FB_WRITE:
	 emit_fb_write(c, args[0], args[1], args[2], inst->target, inst->eot);
	 break;

      case WM_FRONTFACING:
	 emit_frontfacing(p, dst, dst_flags);
	 break;

	 /* Straightforward arithmetic:
	  */
      case OPCODE_ADD:
	 emit_alu2(p, brw_ADD, dst, dst_flags, args[0], args[1]);
	 break;

      case OPCODE_FRC:
	 emit_alu1(p, brw_FRC, dst, dst_flags, args[0]);
	 break;

      case OPCODE_FLR:
	 emit_alu1(p, brw_RNDD, dst, dst_flags, args[0]);
	 break;

      case OPCODE_DDX:
	 emit_ddxy(p, dst, dst_flags, GL_TRUE, args[0]);
	 break;

      case OPCODE_DDY:
	 emit_ddxy(p, dst, dst_flags, GL_FALSE, args[0]);
	 break;

      case OPCODE_DP3:
	 emit_dp3(p, dst, dst_flags, args[0], args[1]);
	 break;

      case OPCODE_DP4:
	 emit_dp4(p, dst, dst_flags, args[0], args[1]);
	 break;

      case OPCODE_DPH:
	 emit_dph(p, dst, dst_flags, args[0], args[1]);
	 break;

      case OPCODE_TRUNC:
	 emit_alu1(p, brw_RNDZ, dst, dst_flags, args[0]);
	 break;

      case OPCODE_LRP:
	 emit_lrp(p, dst, dst_flags, args[0], args[1], args[2]);
	 break;

      case OPCODE_MAD:	
	 emit_mad(p, dst, dst_flags, args[0], args[1], args[2]);
	 break;

      case OPCODE_MOV:
      case OPCODE_SWZ:
	 emit_alu1(p, brw_MOV, dst, dst_flags, args[0]);
	 break;

      case OPCODE_MUL:
	 emit_alu2(p, brw_MUL, dst, dst_flags, args[0], args[1]);
	 break;

      case OPCODE_XPD:
	 emit_xpd(p, dst, dst_flags, args[0], args[1]);
	 break;

	 /* Higher math functions:
	  */
      case OPCODE_RCP:
	 emit_math1(c, BRW_MATH_FUNCTION_INV, dst, dst_flags, args[0]);
	 break;

      case OPCODE_RSQ:
	 emit_math1(c, BRW_MATH_FUNCTION_RSQ, dst, dst_flags, args[0]);
	 break;

      case OPCODE_SIN:
	 emit_math1(c, BRW_MATH_FUNCTION_SIN, dst, dst_flags, args[0]);
	 break;

      case OPCODE_COS:
	 emit_math1(c, BRW_MATH_FUNCTION_COS, dst, dst_flags, args[0]);
	 break;

      case OPCODE_EX2:
	 emit_math1(c, BRW_MATH_FUNCTION_EXP, dst, dst_flags, args[0]);
	 break;

      case OPCODE_LG2:
	 emit_math1(c, BRW_MATH_FUNCTION_LOG, dst, dst_flags, args[0]);
	 break;

      case OPCODE_SCS:
	 /* There is an scs math function, but it would need some
	  * fixup for 16-element execution.
	  */
	 if (dst_flags & WRITEMASK_X)
	    emit_math1(c, BRW_MATH_FUNCTION_COS, dst, (dst_flags&SATURATE)|WRITEMASK_X, args[0]);
	 if (dst_flags & WRITEMASK_Y)
	    emit_math1(c, BRW_MATH_FUNCTION_SIN, dst+1, (dst_flags&SATURATE)|WRITEMASK_X, args[0]);
	 break;

      case OPCODE_POW:
	 emit_math2(c, BRW_MATH_FUNCTION_POW, dst, dst_flags, args[0], args[1]);
	 break;

	 /* Comparisons:
	  */
      case OPCODE_CMP:
	 emit_cmp(p, dst, dst_flags, args[0], args[1], args[2]);
	 break;

      case OPCODE_MAX:
	 emit_max(p, dst, dst_flags, args[0], args[1]);
	 break;

      case OPCODE_MIN:
	 emit_min(p, dst, dst_flags, args[0], args[1]);
	 break;

      case OPCODE_SLT:
	 emit_slt(p, dst, dst_flags, args[0], args[1]);
	 break;

      case OPCODE_SLE:
	 emit_sle(p, dst, dst_flags, args[0], args[1]);
	break;
      case OPCODE_SGT:
	 emit_sgt(p, dst, dst_flags, args[0], args[1]);
	break;
      case OPCODE_SGE:
	 emit_sge(p, dst, dst_flags, args[0], args[1]);
	 break;
      case OPCODE_SEQ:
	 emit_seq(p, dst, dst_flags, args[0], args[1]);
	break;
      case OPCODE_SNE:
	 emit_sne(p, dst, dst_flags, args[0], args[1]);
	break;

      case OPCODE_LIT:
	 emit_lit(c, dst, dst_flags, args[0]);
	 break;

	 /* Texturing operations:
	  */
      case OPCODE_TEX:
	 emit_tex(c, dst, dst_flags, args[0], c->payload.depth[0].hw_reg,
		  inst->tex_idx, inst->tex_unit,
		  inst->tex_shadow);
	 break;

      case OPCODE_TXB:
	 emit_txb(c, dst, dst_flags, args[0], c->payload.depth[0].hw_reg,
		  inst->tex_idx, inst->tex_unit);
	 break;

      case OPCODE_KIL:
	 emit_kil(c, args[0]);
	 break;

      case OPCODE_KIL_NV:
	 emit_kil_nv(c);
	 break;

      default:
	 printf("Unsupported opcode %i (%s) in fragment shader\n",
		inst->opcode, inst->opcode < MAX_OPCODE ?
		_mesa_opcode_string(inst->opcode) :
		"unknown");
      }
      
      for (i = 0; i < 4; i++)
	if (inst->dst[i] && inst->dst[i]->spill_slot) 
	   emit_spill(c, 
		      inst->dst[i]->hw_reg, 
		      inst->dst[i]->spill_slot);
   }

   if (INTEL_DEBUG & DEBUG_WM) {
      int i;

      printf("wm-native:\n");
      for (i = 0; i < p->nr_insn; i++)
	 brw_disasm(stderr, &p->store[i]);
      printf("\n");
   }
}
