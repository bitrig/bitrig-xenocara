/**************************************************************************
 * 
 * Copyright 2006 Tungsten Graphics, Inc., Cedar Park, Texas.
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
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/
 /*
  * Authors:
  *   Keith Whitwell <keith@tungstengraphics.com>
  *   Michel Dänzer <michel@tungstengraphics.com>
  */

#include "intel_mipmap_tree.h"
#include "intel_tex_layout.h"
#include "intel_context.h"
#include "main/macros.h"

void intel_get_texture_alignment_unit(GLenum internalFormat, GLuint *w, GLuint *h)
{
    switch (internalFormat) {
    case GL_COMPRESSED_RGB_FXT1_3DFX:
    case GL_COMPRESSED_RGBA_FXT1_3DFX:
        *w = 8;
        *h = 4;
        break;

    case GL_RGB_S3TC:
    case GL_RGB4_S3TC:
    case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
    case GL_RGBA_S3TC:
    case GL_RGBA4_S3TC:
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        *w = 4;
        *h = 4;
        break;

    default:
        *w = 4;
        *h = 2;
        break;
    }
}

void i945_miptree_layout_2d(struct intel_context *intel,
			    struct intel_mipmap_tree *mt,
			    uint32_t tiling, int nr_images)
{
   GLuint align_h = 2, align_w = 4;
   GLuint level;
   GLuint x = 0;
   GLuint y = 0;
   GLuint width = mt->width0;
   GLuint height = mt->height0;

   mt->total_width = mt->width0;
   intel_get_texture_alignment_unit(mt->internal_format, &align_w, &align_h);

   if (mt->compressed) {
       mt->total_width = ALIGN(mt->width0, align_w);
   }

   /* May need to adjust width to accomodate the placement of
    * the 2nd mipmap.  This occurs when the alignment
    * constraints of mipmap placement push the right edge of the
    * 2nd mipmap out past the width of its parent.
    */
   if (mt->first_level != mt->last_level) {
       GLuint mip1_width;

       if (mt->compressed) {
           mip1_width = ALIGN(minify(mt->width0), align_w)
               + ALIGN(minify(minify(mt->width0)), align_w);
       } else {
           mip1_width = ALIGN(minify(mt->width0), align_w)
               + minify(minify(mt->width0));
       }

       if (mip1_width > mt->total_width) {
           mt->total_width = mip1_width;
       }
   }

   mt->total_height = 0;

   for ( level = mt->first_level ; level <= mt->last_level ; level++ ) {
      GLuint img_height;

      intel_miptree_set_level_info(mt, level, nr_images, x, y, width,
				   height, 1);

      if (mt->compressed)
	 img_height = MAX2(1, height/4);
      else
	 img_height = ALIGN(height, align_h);


      /* Because the images are packed better, the final offset
       * might not be the maximal one:
       */
      mt->total_height = MAX2(mt->total_height, y + img_height);

      /* Layout_below: step right after second mipmap.
       */
      if (level == mt->first_level + 1) {
	 x += ALIGN(width, align_w);
      }
      else {
	 y += img_height;
      }

      width  = minify(width);
      height = minify(height);
   }
}
