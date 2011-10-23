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
           

#include "brw_context.h"
#include "brw_vs.h"
#include "brw_util.h"
#include "brw_state.h"
#include "program/prog_print.h"
#include "program/prog_parameter.h"



static void do_vs_prog( struct brw_context *brw, 
			struct brw_vertex_program *vp,
			struct brw_vs_prog_key *key )
{
   struct gl_context *ctx = &brw->intel.ctx;
   GLuint program_size;
   const GLuint *program;
   struct brw_vs_compile c;
   int aux_size;
   int i;

   memset(&c, 0, sizeof(c));
   memcpy(&c.key, key, sizeof(*key));

   brw_init_compile(brw, &c.func);
   c.vp = vp;

   c.prog_data.outputs_written = vp->program.Base.OutputsWritten;
   c.prog_data.inputs_read = vp->program.Base.InputsRead;

   if (c.key.copy_edgeflag) {
      c.prog_data.outputs_written |= BITFIELD64_BIT(VERT_RESULT_EDGE);
      c.prog_data.inputs_read |= 1<<VERT_ATTRIB_EDGEFLAG;
   }

   /* Put dummy slots into the VUE for the SF to put the replaced
    * point sprite coords in.  We shouldn't need these dummy slots,
    * which take up precious URB space, but it would mean that the SF
    * doesn't get nice aligned pairs of input coords into output
    * coords, which would be a pain to handle.
    */
   for (i = 0; i < 8; i++) {
      if (c.key.point_coord_replace & (1 << i))
	 c.prog_data.outputs_written |= BITFIELD64_BIT(VERT_RESULT_TEX0 + i);
   }

   if (0) {
      _mesa_fprint_program_opt(stdout, &c.vp->program.Base, PROG_PRINT_DEBUG,
			       GL_TRUE);
   }

   /* Emit GEN4 code.
    */
   brw_vs_emit(&c);

   /* get the program
    */
   program = brw_get_program(&c.func, &program_size);

   /* We upload from &c.prog_data including the constant_map assuming
    * they're packed together.  It would be nice to have a
    * compile-time assert macro here.
    */
   assert(c.constant_map == (int8_t *)&c.prog_data +
	  sizeof(c.prog_data));
   assert(ctx->Const.VertexProgram.MaxNativeParameters ==
	  ARRAY_SIZE(c.constant_map));
   (void) ctx;

   aux_size = sizeof(c.prog_data);
   /* constant_map */
   aux_size += c.vp->program.Base.Parameters->NumParameters;

   drm_intel_bo_unreference(brw->vs.prog_bo);
   brw->vs.prog_bo = brw_upload_cache_with_auxdata(&brw->cache, BRW_VS_PROG,
						   &c.key, sizeof(c.key),
						   NULL, 0,
						   program, program_size,
						   &c.prog_data,
						   aux_size,
						   &brw->vs.prog_data);
}


static void brw_upload_vs_prog(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->intel.ctx;
   struct brw_vs_prog_key key;
   struct brw_vertex_program *vp = 
      (struct brw_vertex_program *)brw->vertex_program;
   int i;

   memset(&key, 0, sizeof(key));

   /* Just upload the program verbatim for now.  Always send it all
    * the inputs it asks for, whether they are varying or not.
    */
   key.program_string_id = vp->id;
   key.nr_userclip = brw_count_bits(ctx->Transform.ClipPlanesEnabled);
   key.copy_edgeflag = (ctx->Polygon.FrontMode != GL_FILL ||
			ctx->Polygon.BackMode != GL_FILL);
   key.two_side_color = (ctx->Light.Enabled && ctx->Light.Model.TwoSide);

   /* _NEW_POINT */
   if (ctx->Point.PointSprite) {
      for (i = 0; i < 8; i++) {
	 if (ctx->Point.CoordReplace[i])
	    key.point_coord_replace |= (1 << i);
      }
   }

   /* Make an early check for the key.
    */
   drm_intel_bo_unreference(brw->vs.prog_bo);
   brw->vs.prog_bo = brw_search_cache(&brw->cache, BRW_VS_PROG,
				      &key, sizeof(key),
				      NULL, 0,
				      &brw->vs.prog_data);
   if (brw->vs.prog_bo == NULL)
      do_vs_prog(brw, vp, &key);
   brw->vs.constant_map = ((int8_t *)brw->vs.prog_data +
			   sizeof(*brw->vs.prog_data));
}


/* See brw_vs.c:
 */
const struct brw_tracked_state brw_vs_prog = {
   .dirty = {
      .mesa  = _NEW_TRANSFORM | _NEW_POLYGON | _NEW_POINT | _NEW_LIGHT,
      .brw   = BRW_NEW_VERTEX_PROGRAM,
      .cache = 0
   },
   .prepare = brw_upload_vs_prog
};
