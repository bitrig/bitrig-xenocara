/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2013 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#include "genhw/genhw.h"
#include "util/u_prim.h"
#include "intel_winsys.h"

#include "ilo_builder.h"
#include "ilo_builder_mi.h"
#include "ilo_builder_render.h"
#include "ilo_query.h"
#include "ilo_render_gen.h"

/* in U0.4 */
struct sample_position {
   uint8_t x, y;
};

/* \see gen6_get_sample_position() */
static const struct sample_position sample_position_1x[1] = {
   {  8,  8 },
};

static const struct sample_position sample_position_4x[4] = {
   {  6,  2 }, /* distance from the center is sqrt(40) */
   { 14,  6 }, /* distance from the center is sqrt(40) */
   {  2, 10 }, /* distance from the center is sqrt(40) */
   { 10, 14 }, /* distance from the center is sqrt(40) */
};

static const struct sample_position sample_position_8x[8] = {
   {  7,  9 }, /* distance from the center is sqrt(2) */
   {  9, 13 }, /* distance from the center is sqrt(26) */
   { 11,  3 }, /* distance from the center is sqrt(34) */
   { 13, 11 }, /* distance from the center is sqrt(34) */
   {  1,  7 }, /* distance from the center is sqrt(50) */
   {  5,  1 }, /* distance from the center is sqrt(58) */
   { 15,  5 }, /* distance from the center is sqrt(58) */
   {  3, 15 }, /* distance from the center is sqrt(74) */
};

struct ilo_render *
ilo_render_create(struct ilo_builder *builder)
{
   struct ilo_render *render;
   int i;

   render = CALLOC_STRUCT(ilo_render);
   if (!render)
      return NULL;

   render->dev = builder->dev;
   render->builder = builder;

   render->workaround_bo = intel_winsys_alloc_buffer(builder->winsys,
         "PIPE_CONTROL workaround", 4096, false);
   if (!render->workaround_bo) {
      ilo_warn("failed to allocate PIPE_CONTROL workaround bo\n");
      FREE(render);
      return NULL;
   }

   render->packed_sample_position_1x =
      sample_position_1x[0].x << 4 |
      sample_position_1x[0].y;

   /* pack into dwords */
   for (i = 0; i < 4; i++) {
      render->packed_sample_position_4x |=
         sample_position_4x[i].x << (8 * i + 4) |
         sample_position_4x[i].y << (8 * i);

      render->packed_sample_position_8x[0] |=
         sample_position_8x[i].x << (8 * i + 4) |
         sample_position_8x[i].y << (8 * i);

      render->packed_sample_position_8x[1] |=
         sample_position_8x[4 + i].x << (8 * i + 4) |
         sample_position_8x[4 + i].y << (8 * i);
   }

   ilo_render_invalidate_hw(render);
   ilo_render_invalidate_builder(render);

   return render;
}

void
ilo_render_destroy(struct ilo_render *render)
{
   if (render->workaround_bo)
      intel_bo_unreference(render->workaround_bo);

   FREE(render);
}

void
ilo_render_get_sample_position(const struct ilo_render *render,
                               unsigned sample_count,
                               unsigned sample_index,
                               float *x, float *y)
{
   const struct sample_position *pos;

   switch (sample_count) {
   case 1:
      assert(sample_index < Elements(sample_position_1x));
      pos = sample_position_1x;
      break;
   case 4:
      assert(sample_index < Elements(sample_position_4x));
      pos = sample_position_4x;
      break;
   case 8:
      assert(sample_index < Elements(sample_position_8x));
      pos = sample_position_8x;
      break;
   default:
      assert(!"unknown sample count");
      *x = 0.5f;
      *y = 0.5f;
      return;
      break;
   }

   *x = (float) pos[sample_index].x / 16.0f;
   *y = (float) pos[sample_index].y / 16.0f;
}

void
ilo_render_invalidate_hw(struct ilo_render *render)
{
   render->hw_ctx_changed = true;
}

void
ilo_render_invalidate_builder(struct ilo_render *render)
{
   render->batch_bo_changed = true;
   render->state_bo_changed = true;
   render->instruction_bo_changed = true;

   /* Kernel flushes everything.  Shouldn't we set all bits here? */
   render->state.current_pipe_control_dw1 = 0;
}

/**
 * Return the command length of ilo_render_emit_flush().
 */
int
ilo_render_get_flush_len(const struct ilo_render *render)
{
   int len;

   ILO_DEV_ASSERT(render->dev, 6, 7.5);

   len = GEN6_PIPE_CONTROL__SIZE;

   /* plus gen6_wa_pre_pipe_control() */
   if (ilo_dev_gen(render->dev) == ILO_GEN(6))
      len *= 3;

   return len;
}

/**
 * Emit PIPE_CONTROLs to flush all caches.
 */
void
ilo_render_emit_flush(struct ilo_render *render)
{
   const uint32_t dw1 = GEN6_PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE |
                        GEN6_PIPE_CONTROL_RENDER_CACHE_FLUSH |
                        GEN6_PIPE_CONTROL_DEPTH_CACHE_FLUSH |
                        GEN6_PIPE_CONTROL_VF_CACHE_INVALIDATE |
                        GEN6_PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE |
                        GEN6_PIPE_CONTROL_CS_STALL;
   const unsigned batch_used = ilo_builder_batch_used(render->builder);

   ILO_DEV_ASSERT(render->dev, 6, 7.5);

   if (ilo_dev_gen(render->dev) == ILO_GEN(6))
      gen6_wa_pre_pipe_control(render, dw1);

   gen6_PIPE_CONTROL(render->builder, dw1, NULL, 0, false);

   render->state.current_pipe_control_dw1 |= dw1;
   render->state.deferred_pipe_control_dw1 &= ~dw1;

   assert(ilo_builder_batch_used(render->builder) <= batch_used +
         ilo_render_get_flush_len(render));
}

/**
 * Return the command length of ilo_render_emit_query().
 */
int
ilo_render_get_query_len(const struct ilo_render *render,
                         unsigned query_type)
{
   int len;

   ILO_DEV_ASSERT(render->dev, 6, 7.5);

   /* always a flush or a variant of flush */
   len = ilo_render_get_flush_len(render);

   switch (query_type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_TIME_ELAPSED:
      /* no reg */
      break;
   case PIPE_QUERY_PRIMITIVES_GENERATED:
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      len += GEN6_MI_STORE_REGISTER_MEM__SIZE * 2;
      break;
   case PIPE_QUERY_PIPELINE_STATISTICS:
      {
         const int num_regs =
            (ilo_dev_gen(render->dev) >= ILO_GEN(7)) ? 10 : 8;
         const int num_pads =
            (ilo_dev_gen(render->dev) >= ILO_GEN(7)) ? 1 : 3;

         len += GEN6_MI_STORE_REGISTER_MEM__SIZE * 2 * num_regs +
                GEN6_MI_STORE_DATA_IMM__SIZE * num_pads;
      }
      break;
   default:
      len = 0;
      break;
   }

   return len;
}

/**
 * Emit PIPE_CONTROLs or MI_STORE_REGISTER_MEMs to store register values.
 */
void
ilo_render_emit_query(struct ilo_render *render,
                      struct ilo_query *q, uint32_t offset)
{
   const uint32_t pipeline_statistics_regs[11] = {
      GEN6_REG_IA_VERTICES_COUNT,
      GEN6_REG_IA_PRIMITIVES_COUNT,
      GEN6_REG_VS_INVOCATION_COUNT,
      GEN6_REG_GS_INVOCATION_COUNT,
      GEN6_REG_GS_PRIMITIVES_COUNT,
      GEN6_REG_CL_INVOCATION_COUNT,
      GEN6_REG_CL_PRIMITIVES_COUNT,
      GEN6_REG_PS_INVOCATION_COUNT,
      (ilo_dev_gen(render->dev) >= ILO_GEN(7)) ?
         GEN7_REG_HS_INVOCATION_COUNT : 0,
      (ilo_dev_gen(render->dev) >= ILO_GEN(7)) ?
         GEN7_REG_DS_INVOCATION_COUNT : 0,
      0,
   };
   const uint32_t primitives_generated_reg =
      (ilo_dev_gen(render->dev) >= ILO_GEN(7) && q->index > 0) ?
      GEN7_REG_SO_PRIM_STORAGE_NEEDED(q->index) :
      GEN6_REG_CL_INVOCATION_COUNT;
   const uint32_t primitives_emitted_reg =
      (ilo_dev_gen(render->dev) >= ILO_GEN(7)) ?
      GEN7_REG_SO_NUM_PRIMS_WRITTEN(q->index) :
      GEN6_REG_SO_NUM_PRIMS_WRITTEN;
   const unsigned batch_used = ilo_builder_batch_used(render->builder);
   const uint32_t *regs;
   int reg_count = 0, i;
   uint32_t pipe_control_dw1 = 0;

   ILO_DEV_ASSERT(render->dev, 6, 7.5);

   switch (q->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
      pipe_control_dw1 = GEN6_PIPE_CONTROL_DEPTH_STALL |
                         GEN6_PIPE_CONTROL_WRITE_PS_DEPTH_COUNT;
      break;
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_TIME_ELAPSED:
      pipe_control_dw1 = GEN6_PIPE_CONTROL_WRITE_TIMESTAMP;
      break;
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      regs = &primitives_generated_reg;
      reg_count = 1;
      break;
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      regs = &primitives_emitted_reg;
      reg_count = 1;
      break;
   case PIPE_QUERY_PIPELINE_STATISTICS:
      regs = pipeline_statistics_regs;
      reg_count = Elements(pipeline_statistics_regs);
      break;
   default:
      break;
   }

   if (pipe_control_dw1) {
      assert(!reg_count);

      if (ilo_dev_gen(render->dev) == ILO_GEN(6))
         gen6_wa_pre_pipe_control(render, pipe_control_dw1);

      gen6_PIPE_CONTROL(render->builder, pipe_control_dw1,
            q->bo, offset, true);

      render->state.current_pipe_control_dw1 |= pipe_control_dw1;
      render->state.deferred_pipe_control_dw1 &= ~pipe_control_dw1;
   } else if (reg_count) {
      ilo_render_emit_flush(render);
   }

   for (i = 0; i < reg_count; i++) {
      if (regs[i]) {
         /* store lower 32 bits */
         gen6_MI_STORE_REGISTER_MEM(render->builder, q->bo, offset, regs[i]);
         /* store higher 32 bits */
         gen6_MI_STORE_REGISTER_MEM(render->builder, q->bo,
               offset + 4, regs[i] + 4);
      } else {
         gen6_MI_STORE_DATA_IMM(render->builder, q->bo, offset, 0, true);
      }

      offset += 8;
   }

   assert(ilo_builder_batch_used(render->builder) <= batch_used +
         ilo_render_get_query_len(render, q->type));
}

int
ilo_render_get_rectlist_len(const struct ilo_render *render,
                            const struct ilo_blitter *blitter)
{
   ILO_DEV_ASSERT(render->dev, 6, 7.5);

   return ilo_render_get_rectlist_dynamic_states_len(render, blitter) +
          ilo_render_get_rectlist_commands_len(render, blitter);
}

void
ilo_render_emit_rectlist(struct ilo_render *render,
                         const struct ilo_blitter *blitter)
{
   struct ilo_render_rectlist_session session;

   ILO_DEV_ASSERT(render->dev, 6, 7.5);

   memset(&session, 0, sizeof(session));
   ilo_render_emit_rectlist_dynamic_states(render, blitter, &session);
   ilo_render_emit_rectlist_commands(render, blitter, &session);
}

int
ilo_render_get_draw_len(const struct ilo_render *render,
                        const struct ilo_state_vector *vec)
{
   ILO_DEV_ASSERT(render->dev, 6, 7.5);

   return ilo_render_get_draw_dynamic_states_len(render, vec) +
          ilo_render_get_draw_surface_states_len(render, vec) +
          ilo_render_get_draw_commands_len(render, vec);
}

static void
draw_session_prepare(struct ilo_render *render,
                     const struct ilo_state_vector *vec,
                     struct ilo_render_draw_session *session)
{
   memset(session, 0, sizeof(*session));
   session->pipe_dirty = vec->dirty;
   session->reduced_prim = u_reduced_prim(vec->draw->mode);

   if (render->hw_ctx_changed) {
      /* these should be enough to make everything uploaded */
      render->batch_bo_changed = true;
      render->state_bo_changed = true;
      render->instruction_bo_changed = true;

      session->prim_changed = true;
      session->primitive_restart_changed = true;
   } else {
      session->prim_changed =
         (render->state.reduced_prim != session->reduced_prim);
      session->primitive_restart_changed =
         (render->state.primitive_restart != vec->draw->primitive_restart);
   }
}

static void
draw_session_end(struct ilo_render *render,
                 const struct ilo_state_vector *vec,
                 struct ilo_render_draw_session *session)
{
   render->hw_ctx_changed = false;

   render->batch_bo_changed = false;
   render->state_bo_changed = false;
   render->instruction_bo_changed = false;

   render->state.reduced_prim = session->reduced_prim;
   render->state.primitive_restart = vec->draw->primitive_restart;
}

void
ilo_render_emit_draw(struct ilo_render *render,
                     const struct ilo_state_vector *vec)
{
   struct ilo_render_draw_session session;

   ILO_DEV_ASSERT(render->dev, 6, 7.5);

   draw_session_prepare(render, vec, &session);

   /* force all states to be uploaded if the state bo changed */
   if (render->state_bo_changed)
      session.pipe_dirty = ILO_DIRTY_ALL;
   else
      session.pipe_dirty = vec->dirty;

   ilo_render_emit_draw_dynamic_states(render, vec, &session);
   ilo_render_emit_draw_surface_states(render, vec, &session);

   /* force all commands to be uploaded if the HW context changed */
   if (render->hw_ctx_changed)
      session.pipe_dirty = ILO_DIRTY_ALL;
   else
      session.pipe_dirty = vec->dirty;

   ilo_render_emit_draw_commands(render, vec, &session);

   draw_session_end(render, vec, &session);
}

int
ilo_render_get_launch_grid_len(const struct ilo_render *render,
                               const struct ilo_state_vector *vec)
{
   ILO_DEV_ASSERT(render->dev, 7, 7.5);

   return ilo_render_get_launch_grid_surface_states_len(render, vec) +
          ilo_render_get_launch_grid_dynamic_states_len(render, vec) +
          ilo_render_get_launch_grid_commands_len(render, vec);
}

void
ilo_render_emit_launch_grid(struct ilo_render *render,
                            const struct ilo_state_vector *vec,
                            const unsigned thread_group_offset[3],
                            const unsigned thread_group_dim[3],
                            unsigned thread_group_size,
                            const struct pipe_constant_buffer *input,
                            uint32_t pc)
{
   struct ilo_render_launch_grid_session session;

   ILO_DEV_ASSERT(render->dev, 7, 7.5);

   assert(input->buffer);

   memset(&session, 0, sizeof(session));

   session.thread_group_offset = thread_group_offset;
   session.thread_group_dim = thread_group_dim;
   session.thread_group_size = thread_group_size;
   session.input = input;
   session.pc = pc;

   ilo_render_emit_launch_grid_surface_states(render, vec, &session);
   ilo_render_emit_launch_grid_dynamic_states(render, vec, &session);
   ilo_render_emit_launch_grid_commands(render, vec, &session);
}
