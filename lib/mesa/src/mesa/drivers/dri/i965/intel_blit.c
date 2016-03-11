/**************************************************************************
 *
 * Copyright 2003 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#include "main/mtypes.h"
#include "main/blit.h"
#include "main/context.h"
#include "main/enums.h"
#include "main/colormac.h"
#include "main/fbobject.h"

#include "brw_context.h"
#include "brw_defines.h"
#include "intel_blit.h"
#include "intel_buffers.h"
#include "intel_fbo.h"
#include "intel_reg.h"
#include "intel_batchbuffer.h"
#include "intel_mipmap_tree.h"

#define FILE_DEBUG_FLAG DEBUG_BLIT

#define SET_TILING_XY_FAST_COPY_BLT(tiling, tr_mode, type)           \
({                                                                   \
   switch (tiling) {                                                 \
   case I915_TILING_X:                                               \
      CMD |= type ## _TILED_X;                                       \
      break;                                                         \
   case I915_TILING_Y:                                               \
      if (tr_mode == INTEL_MIPTREE_TRMODE_YS)                        \
         CMD |= type ## _TILED_64K;                                  \
      else                                                           \
         CMD |= type ## _TILED_Y;                                    \
      break;                                                         \
   default:                                                          \
      unreachable("not reached");                                    \
   }                                                                 \
})

static void
intel_miptree_set_alpha_to_one(struct brw_context *brw,
                               struct intel_mipmap_tree *mt,
                               int x, int y, int width, int height);

static GLuint translate_raster_op(GLenum logicop)
{
   switch(logicop) {
   case GL_CLEAR: return 0x00;
   case GL_AND: return 0x88;
   case GL_AND_REVERSE: return 0x44;
   case GL_COPY: return 0xCC;
   case GL_AND_INVERTED: return 0x22;
   case GL_NOOP: return 0xAA;
   case GL_XOR: return 0x66;
   case GL_OR: return 0xEE;
   case GL_NOR: return 0x11;
   case GL_EQUIV: return 0x99;
   case GL_INVERT: return 0x55;
   case GL_OR_REVERSE: return 0xDD;
   case GL_COPY_INVERTED: return 0x33;
   case GL_OR_INVERTED: return 0xBB;
   case GL_NAND: return 0x77;
   case GL_SET: return 0xFF;
   default: return 0;
   }
}

static uint32_t
br13_for_cpp(int cpp)
{
   switch (cpp) {
   case 16:
      return BR13_32323232;
   case 8:
      return BR13_16161616;
   case 4:
      return BR13_8888;
   case 2:
      return BR13_565;
   case 1:
      return BR13_8;
   default:
      unreachable("not reached");
   }
}

static uint32_t
get_tr_horizontal_align(uint32_t tr_mode, uint32_t cpp, bool is_src) {
   /* Alignment tables for YF/YS tiled surfaces. */
   const uint32_t align_2d_yf[] = {64, 64, 32, 32, 16};
   const uint32_t bpp = cpp * 8;
   const uint32_t shift = is_src ? 17 : 10;
   uint32_t align;
   int i = 0;

   if (tr_mode == INTEL_MIPTREE_TRMODE_NONE)
      return 0;

   /* Compute array index. */
   assert (bpp >= 8 && bpp <= 128 && _mesa_is_pow_two(bpp));
   i = ffs(bpp / 8) - 1;

   align = tr_mode == INTEL_MIPTREE_TRMODE_YF ?
           align_2d_yf[i] :
           4 * align_2d_yf[i];

   assert(_mesa_is_pow_two(align));

   /* XY_FAST_COPY_BLT doesn't support horizontal alignment of 16. */
   if (align == 16)
      align = 32;

   return (ffs(align) - 6) << shift;
}

static uint32_t
get_tr_vertical_align(uint32_t tr_mode, uint32_t cpp, bool is_src) {
   /* Vertical alignment tables for YF/YS tiled surfaces. */
   const unsigned align_2d_yf[] = {64, 32, 32, 16, 16};
   const uint32_t bpp = cpp * 8;
   const uint32_t shift = is_src ? 15 : 8;
   uint32_t align;
   int i = 0;

   if (tr_mode == INTEL_MIPTREE_TRMODE_NONE)
      return 0;

   /* Compute array index. */
   assert (bpp >= 8 && bpp <= 128 && _mesa_is_pow_two(bpp));
   i = ffs(bpp / 8) - 1;

   align = tr_mode == INTEL_MIPTREE_TRMODE_YF ?
           align_2d_yf[i] :
           4 * align_2d_yf[i];

   assert(_mesa_is_pow_two(align));

   /* XY_FAST_COPY_BLT doesn't support vertical alignments of 16 and 32. */
   if (align == 16 || align == 32)
      align = 64;

   return (ffs(align) - 7) << shift;
}

/**
 * Emits the packet for switching the blitter from X to Y tiled or back.
 *
 * This has to be called in a single BEGIN_BATCH_BLT_TILED() /
 * ADVANCE_BATCH_TILED().  This is because BCS_SWCTRL is saved and restored as
 * part of the power context, not a render context, and if the batchbuffer was
 * to get flushed between setting and blitting, or blitting and restoring, our
 * tiling state would leak into other unsuspecting applications (like the X
 * server).
 */
static uint32_t *
set_blitter_tiling(struct brw_context *brw,
                   bool dst_y_tiled, bool src_y_tiled,
                   uint32_t *__map)
{
   assert(brw->gen >= 6);

   /* Idle the blitter before we update how tiling is interpreted. */
   OUT_BATCH(MI_FLUSH_DW);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);

   OUT_BATCH(MI_LOAD_REGISTER_IMM | (3 - 2));
   OUT_BATCH(BCS_SWCTRL);
   OUT_BATCH((BCS_SWCTRL_DST_Y | BCS_SWCTRL_SRC_Y) << 16 |
             (dst_y_tiled ? BCS_SWCTRL_DST_Y : 0) |
             (src_y_tiled ? BCS_SWCTRL_SRC_Y : 0));
   return __map;
}
#define SET_BLITTER_TILING(...) __map = set_blitter_tiling(__VA_ARGS__, __map)

#define BEGIN_BATCH_BLT_TILED(n, dst_y_tiled, src_y_tiled)              \
      BEGIN_BATCH_BLT(n + ((dst_y_tiled || src_y_tiled) ? 14 : 0));     \
      if (dst_y_tiled || src_y_tiled)                                   \
         SET_BLITTER_TILING(brw, dst_y_tiled, src_y_tiled)

#define ADVANCE_BATCH_TILED(dst_y_tiled, src_y_tiled)                   \
      if (dst_y_tiled || src_y_tiled)                                   \
         SET_BLITTER_TILING(brw, false, false);                         \
      ADVANCE_BATCH()

static int
blt_pitch(struct intel_mipmap_tree *mt)
{
   int pitch = mt->pitch;
   if (mt->tiling)
      pitch /= 4;
   return pitch;
}

bool
intel_miptree_blit_compatible_formats(mesa_format src, mesa_format dst)
{
   /* The BLT doesn't handle sRGB conversion */
   assert(src == _mesa_get_srgb_format_linear(src));
   assert(dst == _mesa_get_srgb_format_linear(dst));

   /* No swizzle or format conversions possible, except... */
   if (src == dst)
      return true;

   /* ...we can either discard the alpha channel when going from A->X,
    * or we can fill the alpha channel with 0xff when going from X->A
    */
   if (src == MESA_FORMAT_B8G8R8A8_UNORM || src == MESA_FORMAT_B8G8R8X8_UNORM)
      return (dst == MESA_FORMAT_B8G8R8A8_UNORM ||
              dst == MESA_FORMAT_B8G8R8X8_UNORM);

   if (src == MESA_FORMAT_R8G8B8A8_UNORM || src == MESA_FORMAT_R8G8B8X8_UNORM)
      return (dst == MESA_FORMAT_R8G8B8A8_UNORM ||
              dst == MESA_FORMAT_R8G8B8X8_UNORM);

   return false;
}

/**
 * Implements a rectangular block transfer (blit) of pixels between two
 * miptrees.
 *
 * Our blitter can operate on 1, 2, or 4-byte-per-pixel data, with generous,
 * but limited, pitches and sizes allowed.
 *
 * The src/dst coordinates are relative to the given level/slice of the
 * miptree.
 *
 * If @src_flip or @dst_flip is set, then the rectangle within that miptree
 * will be inverted (including scanline order) when copying.  This is common
 * in GL when copying between window system and user-created
 * renderbuffers/textures.
 */
bool
intel_miptree_blit(struct brw_context *brw,
                   struct intel_mipmap_tree *src_mt,
                   int src_level, int src_slice,
                   uint32_t src_x, uint32_t src_y, bool src_flip,
                   struct intel_mipmap_tree *dst_mt,
                   int dst_level, int dst_slice,
                   uint32_t dst_x, uint32_t dst_y, bool dst_flip,
                   uint32_t width, uint32_t height,
                   GLenum logicop)
{
   /* The blitter doesn't understand multisampling at all. */
   if (src_mt->num_samples > 0 || dst_mt->num_samples > 0)
      return false;

   /* No sRGB decode or encode is done by the hardware blitter, which is
    * consistent with what we want in the callers (glCopyTexSubImage(),
    * glBlitFramebuffer(), texture validation, etc.).
    */
   mesa_format src_format = _mesa_get_srgb_format_linear(src_mt->format);
   mesa_format dst_format = _mesa_get_srgb_format_linear(dst_mt->format);

   /* The blitter doesn't support doing any format conversions.  We do also
    * support blitting ARGB8888 to XRGB8888 (trivial, the values dropped into
    * the X channel don't matter), and XRGB8888 to ARGB8888 by setting the A
    * channel to 1.0 at the end.
    */
   if (!intel_miptree_blit_compatible_formats(src_format, dst_format)) {
      perf_debug("%s: Can't use hardware blitter from %s to %s, "
                 "falling back.\n", __func__,
                 _mesa_get_format_name(src_format),
                 _mesa_get_format_name(dst_format));
      return false;
   }

   /* According to the Ivy Bridge PRM, Vol1 Part4, section 1.2.1.2 (Graphics
    * Data Size Limitations):
    *
    *    The BLT engine is capable of transferring very large quantities of
    *    graphics data. Any graphics data read from and written to the
    *    destination is permitted to represent a number of pixels that
    *    occupies up to 65,536 scan lines and up to 32,768 bytes per scan line
    *    at the destination. The maximum number of pixels that may be
    *    represented per scan line’s worth of graphics data depends on the
    *    color depth.
    *
    * Furthermore, intelEmitCopyBlit (which is called below) uses a signed
    * 16-bit integer to represent buffer pitch, so it can only handle buffer
    * pitches < 32k. However, the pitch is measured in bytes for linear buffers
    * and dwords for tiled buffers.
    *
    * As a result of these two limitations, we can only use the blitter to do
    * this copy when the miptree's pitch is less than 32k linear or 128k tiled.
    */
   if (blt_pitch(src_mt) >= 32768 || blt_pitch(dst_mt) >= 32768) {
      perf_debug("Falling back due to >= 32k/128k pitch\n");
      return false;
   }

   /* The blitter has no idea about HiZ or fast color clears, so we need to
    * resolve the miptrees before we do anything.
    */
   intel_miptree_slice_resolve_depth(brw, src_mt, src_level, src_slice);
   intel_miptree_slice_resolve_depth(brw, dst_mt, dst_level, dst_slice);
   intel_miptree_resolve_color(brw, src_mt);
   intel_miptree_resolve_color(brw, dst_mt);

   if (src_flip)
      src_y = minify(src_mt->physical_height0, src_level - src_mt->first_level) - src_y - height;

   if (dst_flip)
      dst_y = minify(dst_mt->physical_height0, dst_level - dst_mt->first_level) - dst_y - height;

   int src_pitch = src_mt->pitch;
   if (src_flip != dst_flip)
      src_pitch = -src_pitch;

   uint32_t src_image_x, src_image_y, dst_image_x, dst_image_y;
   intel_miptree_get_image_offset(src_mt, src_level, src_slice,
                                  &src_image_x, &src_image_y);
   intel_miptree_get_image_offset(dst_mt, dst_level, dst_slice,
                                  &dst_image_x, &dst_image_y);
   src_x += src_image_x;
   src_y += src_image_y;
   dst_x += dst_image_x;
   dst_y += dst_image_y;

   /* The blitter interprets the 16-bit destination x/y as a signed 16-bit
    * value. The values we're working with are unsigned, so make sure we don't
    * overflow.
    */
   if (src_x >= 32768 || src_y >= 32768 || dst_x >= 32768 || dst_y >= 32768) {
      perf_debug("Falling back due to >=32k offset [src(%d, %d) dst(%d, %d)]\n",
                 src_x, src_y, dst_x, dst_y);
      return false;
   }

   if (!intelEmitCopyBlit(brw,
                          src_mt->cpp,
                          src_pitch,
                          src_mt->bo, src_mt->offset,
                          src_mt->tiling,
                          src_mt->tr_mode,
                          dst_mt->pitch,
                          dst_mt->bo, dst_mt->offset,
                          dst_mt->tiling,
                          dst_mt->tr_mode,
                          src_x, src_y,
                          dst_x, dst_y,
                          width, height,
                          logicop)) {
      return false;
   }

   /* XXX This could be done in a single pass using XY_FULL_MONO_PATTERN_BLT */
   if (_mesa_get_format_bits(src_format, GL_ALPHA_BITS) == 0 &&
       _mesa_get_format_bits(dst_format, GL_ALPHA_BITS) > 0) {
      intel_miptree_set_alpha_to_one(brw, dst_mt,
                                     dst_x, dst_y,
                                     width, height);
   }

   return true;
}

static bool
alignment_valid(struct brw_context *brw, unsigned offset, uint32_t tiling)
{
   /* Tiled buffers must be page-aligned (4K). */
   if (tiling != I915_TILING_NONE)
      return (offset & 4095) == 0;

   /* On Gen8+, linear buffers must be cacheline-aligned. */
   if (brw->gen >= 8)
      return (offset & 63) == 0;

   return true;
}

static bool
can_fast_copy_blit(struct brw_context *brw,
		   drm_intel_bo *src_buffer,
                   int16_t src_x, int16_t src_y,
                   uintptr_t src_offset, uint32_t src_pitch,
                   uint32_t src_tiling, uint32_t src_tr_mode,
		   drm_intel_bo *dst_buffer,
                   int16_t dst_x, int16_t dst_y,
                   uintptr_t dst_offset, uint32_t dst_pitch,
                   uint32_t dst_tiling, uint32_t dst_tr_mode,
                   int16_t w, int16_t h, uint32_t cpp)
{
   const bool dst_tiling_none = dst_tiling == I915_TILING_NONE;
   const bool src_tiling_none = src_tiling == I915_TILING_NONE;

   if (brw->gen < 9)
      return false;

   if (src_buffer->handle == dst_buffer->handle &&
       _mesa_regions_overlap(src_x, src_y, src_x + w, src_y + h,
                             dst_x, dst_y, dst_x + w, dst_y + h))
      return false;

   /* Enable fast copy blit only if the surfaces are Yf/Ys tiled.
    * FIXME: Based on performance data, remove this condition later to
    * enable for all types of surfaces.
    */
   if (src_tr_mode == INTEL_MIPTREE_TRMODE_NONE &&
       dst_tr_mode == INTEL_MIPTREE_TRMODE_NONE)
      return false;

   /* For all surface types buffers must be cacheline-aligned. */
   if ((dst_offset | src_offset) & 63)
      return false;

   /* Color depth greater than 128 bits not supported. */
   if (cpp > 16)
      return false;

   /* For Fast Copy Blits the pitch cannot be a negative number. So, bit 15
    * of the destination pitch must be zero.
    */
   if ((src_pitch >> 15 & 1) != 0 || (dst_pitch >> 15 & 1) != 0)
      return false;

   /* For Linear surfaces, the pitch has to be an OWord (16byte) multiple. */
   if ((src_tiling_none && src_pitch % 16 != 0) ||
       (dst_tiling_none && dst_pitch % 16 != 0))
      return false;

   /* For Tiled surfaces, the pitch has to be a multiple of the Tile width
    * (X direction width of the Tile). This means the pitch value will
    * always be Cache Line aligned (64byte multiple).
    */
   if ((!dst_tiling_none && dst_pitch % 64 != 0) ||
       (!src_tiling_none && src_pitch % 64 != 0))
      return false;

   return true;
}

static uint32_t
xy_blit_cmd(uint32_t src_tiling, uint32_t src_tr_mode,
            uint32_t dst_tiling, uint32_t dst_tr_mode,
            uint32_t cpp, bool use_fast_copy_blit)
{
   uint32_t CMD = 0;

   if (use_fast_copy_blit) {
      CMD = XY_FAST_COPY_BLT_CMD;

      if (dst_tiling != I915_TILING_NONE)
         SET_TILING_XY_FAST_COPY_BLT(dst_tiling, dst_tr_mode, XY_FAST_DST);

      if (src_tiling != I915_TILING_NONE)
         SET_TILING_XY_FAST_COPY_BLT(src_tiling, src_tr_mode, XY_FAST_SRC);

      CMD |= get_tr_horizontal_align(src_tr_mode, cpp, true /* is_src */);
      CMD |= get_tr_vertical_align(src_tr_mode, cpp, true /* is_src */);

      CMD |= get_tr_horizontal_align(dst_tr_mode, cpp, false /* is_src */);
      CMD |= get_tr_vertical_align(dst_tr_mode, cpp, false /* is_src */);

   } else {
      assert(cpp <= 4);
      switch (cpp) {
      case 1:
      case 2:
         CMD = XY_SRC_COPY_BLT_CMD;
         break;
      case 4:
         CMD = XY_SRC_COPY_BLT_CMD | XY_BLT_WRITE_ALPHA | XY_BLT_WRITE_RGB;
         break;
      default:
         unreachable("not reached");
      }

      if (dst_tiling != I915_TILING_NONE)
         CMD |= XY_DST_TILED;

      if (src_tiling != I915_TILING_NONE)
         CMD |= XY_SRC_TILED;
   }
   return CMD;
}

/* Copy BitBlt
 */
bool
intelEmitCopyBlit(struct brw_context *brw,
		  GLuint cpp,
		  GLshort src_pitch,
		  drm_intel_bo *src_buffer,
		  GLuint src_offset,
		  uint32_t src_tiling,
		  uint32_t src_tr_mode,
		  GLshort dst_pitch,
		  drm_intel_bo *dst_buffer,
		  GLuint dst_offset,
		  uint32_t dst_tiling,
		  uint32_t dst_tr_mode,
		  GLshort src_x, GLshort src_y,
		  GLshort dst_x, GLshort dst_y,
		  GLshort w, GLshort h,
		  GLenum logic_op)
{
   GLuint CMD, BR13, pass = 0;
   int dst_y2 = dst_y + h;
   int dst_x2 = dst_x + w;
   drm_intel_bo *aper_array[3];
   bool dst_y_tiled = dst_tiling == I915_TILING_Y;
   bool src_y_tiled = src_tiling == I915_TILING_Y;
   bool use_fast_copy_blit = false;

   if ((dst_y_tiled || src_y_tiled) && brw->gen < 6)
      return false;

   /* do space check before going any further */
   do {
       aper_array[0] = brw->batch.bo;
       aper_array[1] = dst_buffer;
       aper_array[2] = src_buffer;

       if (dri_bufmgr_check_aperture_space(aper_array, 3) != 0) {
           intel_batchbuffer_flush(brw);
           pass++;
       } else
           break;
   } while (pass < 2);

   if (pass >= 2)
      return false;

   unsigned length = brw->gen >= 8 ? 10 : 8;

   intel_batchbuffer_require_space(brw, length * 4, BLT_RING);
   DBG("%s src:buf(%p)/%d+%d %d,%d dst:buf(%p)/%d+%d %d,%d sz:%dx%d\n",
       __func__,
       src_buffer, src_pitch, src_offset, src_x, src_y,
       dst_buffer, dst_pitch, dst_offset, dst_x, dst_y, w, h);

   use_fast_copy_blit = can_fast_copy_blit(brw,
                                           src_buffer,
                                           src_x, src_y,
                                           src_offset, src_pitch,
                                           src_tiling, src_tr_mode,
                                           dst_buffer,
                                           dst_x, dst_y,
                                           dst_offset, dst_pitch,
                                           dst_tiling, dst_tr_mode,
                                           w, h, cpp);
   assert(use_fast_copy_blit ||
          (src_tr_mode == INTEL_MIPTREE_TRMODE_NONE &&
           dst_tr_mode == INTEL_MIPTREE_TRMODE_NONE));

   if (use_fast_copy_blit) {
      /* When two sequential fast copy blits have different source surfaces,
       * but their destinations refer to the same destination surfaces and
       * therefore destinations overlap it is imperative that a flush be
       * inserted between the two blits.
       *
       * FIXME: Figure out a way to avoid flushing when not required.
       */
      brw_emit_mi_flush(brw);

      assert(cpp <= 16);
      BR13 = br13_for_cpp(cpp);

      if (src_tr_mode == INTEL_MIPTREE_TRMODE_YF)
         BR13 |= XY_FAST_SRC_TRMODE_YF;

      if (dst_tr_mode == INTEL_MIPTREE_TRMODE_YF)
         BR13 |= XY_FAST_DST_TRMODE_YF;

      CMD = xy_blit_cmd(src_tiling, src_tr_mode,
                        dst_tiling, dst_tr_mode,
                        cpp, use_fast_copy_blit);

      /* For tiled source and destination, pitch value should be specified
       * as a number of Dwords.
       */
      if (dst_tiling != I915_TILING_NONE)
         dst_pitch /= 4;

      if (src_tiling != I915_TILING_NONE)
         src_pitch /= 4;

   } else {
      assert(!dst_y_tiled || (dst_pitch % 128) == 0);
      assert(!src_y_tiled || (src_pitch % 128) == 0);

      /* For big formats (such as floating point), do the copy using 16 or
       * 32bpp and multiply the coordinates.
       */
      if (cpp > 4) {
         if (cpp % 4 == 2) {
            dst_x *= cpp / 2;
            dst_x2 *= cpp / 2;
            src_x *= cpp / 2;
            cpp = 2;
         } else {
            assert(cpp % 4 == 0);
            dst_x *= cpp / 4;
            dst_x2 *= cpp / 4;
            src_x *= cpp / 4;
            cpp = 4;
         }
      }

      if (!alignment_valid(brw, dst_offset, dst_tiling))
         return false;
      if (!alignment_valid(brw, src_offset, src_tiling))
         return false;

      /* Blit pitch must be dword-aligned.  Otherwise, the hardware appears to drop
       * the low bits.  Offsets must be naturally aligned.
       */
      if (src_pitch % 4 != 0 || src_offset % cpp != 0 ||
          dst_pitch % 4 != 0 || dst_offset % cpp != 0)
         return false;

      assert(cpp <= 4);
      BR13 = br13_for_cpp(cpp) | translate_raster_op(logic_op) << 16;

      CMD = xy_blit_cmd(src_tiling, src_tr_mode,
                        dst_tiling, dst_tr_mode,
                        cpp, use_fast_copy_blit);

      if (dst_tiling != I915_TILING_NONE)
         dst_pitch /= 4;

      if (src_tiling != I915_TILING_NONE)
         src_pitch /= 4;
   }

   if (dst_y2 <= dst_y || dst_x2 <= dst_x) {
      return true;
   }

   assert(dst_x < dst_x2);
   assert(dst_y < dst_y2);
   assert(src_offset + (src_y + h - 1) * abs(src_pitch) +
          (w * cpp) <= src_buffer->size);
   assert(dst_offset + (dst_y + h - 1) * abs(dst_pitch) +
          (w * cpp) <= dst_buffer->size);

   BEGIN_BATCH_BLT_TILED(length, dst_y_tiled, src_y_tiled);
   OUT_BATCH(CMD | (length - 2));
   OUT_BATCH(BR13 | (uint16_t)dst_pitch);
   OUT_BATCH(SET_FIELD(dst_y, BLT_Y) | SET_FIELD(dst_x, BLT_X));
   OUT_BATCH(SET_FIELD(dst_y2, BLT_Y) | SET_FIELD(dst_x2, BLT_X));
   if (brw->gen >= 8) {
      OUT_RELOC64(dst_buffer,
                  I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                  dst_offset);
   } else {
      OUT_RELOC(dst_buffer,
                I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                dst_offset);
   }
   OUT_BATCH(SET_FIELD(src_y, BLT_Y) | SET_FIELD(src_x, BLT_X));
   OUT_BATCH((uint16_t)src_pitch);
   if (brw->gen >= 8) {
      OUT_RELOC64(src_buffer,
                  I915_GEM_DOMAIN_RENDER, 0,
                  src_offset);
   } else {
      OUT_RELOC(src_buffer,
                I915_GEM_DOMAIN_RENDER, 0,
                src_offset);
   }

   ADVANCE_BATCH_TILED(dst_y_tiled, src_y_tiled);

   brw_emit_mi_flush(brw);

   return true;
}

bool
intelEmitImmediateColorExpandBlit(struct brw_context *brw,
				  GLuint cpp,
				  GLubyte *src_bits, GLuint src_size,
				  GLuint fg_color,
				  GLshort dst_pitch,
				  drm_intel_bo *dst_buffer,
				  GLuint dst_offset,
				  uint32_t dst_tiling,
				  GLshort x, GLshort y,
				  GLshort w, GLshort h,
				  GLenum logic_op)
{
   int dwords = ALIGN(src_size, 8) / 4;
   uint32_t opcode, br13, blit_cmd;

   if (dst_tiling != I915_TILING_NONE) {
      if (dst_offset & 4095)
	 return false;
      if (dst_tiling == I915_TILING_Y)
	 return false;
   }

   assert((logic_op >= GL_CLEAR) && (logic_op <= (GL_CLEAR + 0x0f)));
   assert(dst_pitch > 0);

   if (w < 0 || h < 0)
      return true;

   DBG("%s dst:buf(%p)/%d+%d %d,%d sz:%dx%d, %d bytes %d dwords\n",
       __func__,
       dst_buffer, dst_pitch, dst_offset, x, y, w, h, src_size, dwords);

   unsigned xy_setup_blt_length = brw->gen >= 8 ? 10 : 8;
   intel_batchbuffer_require_space(brw, (xy_setup_blt_length * 4) +
                                        (3 * 4) + dwords * 4, BLT_RING);

   opcode = XY_SETUP_BLT_CMD;
   if (cpp == 4)
      opcode |= XY_BLT_WRITE_ALPHA | XY_BLT_WRITE_RGB;
   if (dst_tiling != I915_TILING_NONE) {
      opcode |= XY_DST_TILED;
      dst_pitch /= 4;
   }

   br13 = dst_pitch | (translate_raster_op(logic_op) << 16) | (1 << 29);
   br13 |= br13_for_cpp(cpp);

   blit_cmd = XY_TEXT_IMMEDIATE_BLIT_CMD | XY_TEXT_BYTE_PACKED; /* packing? */
   if (dst_tiling != I915_TILING_NONE)
      blit_cmd |= XY_DST_TILED;

   BEGIN_BATCH_BLT(xy_setup_blt_length + 3);
   OUT_BATCH(opcode | (xy_setup_blt_length - 2));
   OUT_BATCH(br13);
   OUT_BATCH((0 << 16) | 0); /* clip x1, y1 */
   OUT_BATCH((100 << 16) | 100); /* clip x2, y2 */
   if (brw->gen >= 8) {
      OUT_RELOC64(dst_buffer,
                  I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                  dst_offset);
   } else {
      OUT_RELOC(dst_buffer,
                I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                dst_offset);
   }
   OUT_BATCH(0); /* bg */
   OUT_BATCH(fg_color); /* fg */
   OUT_BATCH(0); /* pattern base addr */
   if (brw->gen >= 8)
      OUT_BATCH(0);

   OUT_BATCH(blit_cmd | ((3 - 2) + dwords));
   OUT_BATCH(SET_FIELD(y, BLT_Y) | SET_FIELD(x, BLT_X));
   OUT_BATCH(SET_FIELD(y + h, BLT_Y) | SET_FIELD(x + w, BLT_X));
   ADVANCE_BATCH();

   intel_batchbuffer_data(brw, src_bits, dwords * 4, BLT_RING);

   brw_emit_mi_flush(brw);

   return true;
}

/* We don't have a memmove-type blit like some other hardware, so we'll do a
 * rectangular blit covering a large space, then emit 1-scanline blit at the
 * end to cover the last if we need.
 */
void
intel_emit_linear_blit(struct brw_context *brw,
		       drm_intel_bo *dst_bo,
		       unsigned int dst_offset,
		       drm_intel_bo *src_bo,
		       unsigned int src_offset,
		       unsigned int size)
{
   struct gl_context *ctx = &brw->ctx;
   GLuint pitch, height;
   int16_t src_x, dst_x;
   bool ok;

   do {
      /* The pitch given to the GPU must be DWORD aligned, and
       * we want width to match pitch. Max width is (1 << 15 - 1),
       * rounding that down to the nearest DWORD is 1 << 15 - 4
       */
      pitch = ROUND_DOWN_TO(MIN2(size, (1 << 15) - 64), 4);
      height = (size < pitch || pitch == 0) ? 1 : size / pitch;

      src_x = src_offset % 64;
      dst_x = dst_offset % 64;
      pitch = ALIGN(MIN2(size, (1 << 15) - 64), 4);
      assert(src_x + pitch < 1 << 15);
      assert(dst_x + pitch < 1 << 15);

      ok = intelEmitCopyBlit(brw, 1,
                             pitch, src_bo, src_offset - src_x, I915_TILING_NONE,
                             INTEL_MIPTREE_TRMODE_NONE,
                             pitch, dst_bo, dst_offset - dst_x, I915_TILING_NONE,
                             INTEL_MIPTREE_TRMODE_NONE,
                             src_x, 0, /* src x/y */
                             dst_x, 0, /* dst x/y */
                             MIN2(size, pitch), height, /* w, h */
                             GL_COPY);
      if (!ok) {
         _mesa_problem(ctx, "Failed to linear blit %dx%d\n",
                       MIN2(size, pitch), height);
         return;
      }

      pitch *= height;
      if (size <= pitch)
         return;

      src_offset += pitch;
      dst_offset += pitch;
      size -= pitch;
   } while (1);
}

/**
 * Used to initialize the alpha value of an ARGB8888 miptree after copying
 * into it from an XRGB8888 source.
 *
 * This is very common with glCopyTexImage2D().  Note that the coordinates are
 * relative to the start of the miptree, not relative to a slice within the
 * miptree.
 */
static void
intel_miptree_set_alpha_to_one(struct brw_context *brw,
                              struct intel_mipmap_tree *mt,
                              int x, int y, int width, int height)
{
   uint32_t BR13, CMD;
   int pitch, cpp;
   drm_intel_bo *aper_array[2];

   pitch = mt->pitch;
   cpp = mt->cpp;

   DBG("%s dst:buf(%p)/%d %d,%d sz:%dx%d\n",
       __func__, mt->bo, pitch, x, y, width, height);

   BR13 = br13_for_cpp(cpp) | 0xf0 << 16;
   CMD = XY_COLOR_BLT_CMD;
   CMD |= XY_BLT_WRITE_ALPHA;

   if (mt->tiling != I915_TILING_NONE) {
      CMD |= XY_DST_TILED;
      pitch /= 4;
   }
   BR13 |= pitch;

   /* do space check before going any further */
   aper_array[0] = brw->batch.bo;
   aper_array[1] = mt->bo;

   if (drm_intel_bufmgr_check_aperture_space(aper_array,
					     ARRAY_SIZE(aper_array)) != 0) {
      intel_batchbuffer_flush(brw);
   }

   unsigned length = brw->gen >= 8 ? 7 : 6;
   bool dst_y_tiled = mt->tiling == I915_TILING_Y;

   BEGIN_BATCH_BLT_TILED(length, dst_y_tiled, false);
   OUT_BATCH(CMD | (length - 2));
   OUT_BATCH(BR13);
   OUT_BATCH(SET_FIELD(y, BLT_Y) | SET_FIELD(x, BLT_X));
   OUT_BATCH(SET_FIELD(y + height, BLT_Y) | SET_FIELD(x + width, BLT_X));
   if (brw->gen >= 8) {
      OUT_RELOC64(mt->bo,
                  I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                  0);
   } else {
      OUT_RELOC(mt->bo,
                I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                0);
   }
   OUT_BATCH(0xffffffff); /* white, but only alpha gets written */
   ADVANCE_BATCH_TILED(dst_y_tiled, false);

   brw_emit_mi_flush(brw);
}
