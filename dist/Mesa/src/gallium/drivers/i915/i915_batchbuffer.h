/**************************************************************************
 *
 * Copyright 2007 Tungsten Graphics, Inc., Cedar Park, Texas.
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

#ifndef I915_BATCHBUFFER_H
#define I915_BATCHBUFFER_H

#include "i915_winsys.h"
#include "util/u_debug.h"

struct i915_context;

static INLINE size_t
i915_winsys_batchbuffer_space(struct i915_winsys_batchbuffer *batch)
{
   return batch->size - (batch->ptr - batch->map);
}

static INLINE boolean
i915_winsys_batchbuffer_check(struct i915_winsys_batchbuffer *batch,
                              size_t dwords,
                              size_t relocs)
{
   return dwords * 4 <= i915_winsys_batchbuffer_space(batch) &&
          relocs <= (batch->max_relocs - batch->relocs);
}

static INLINE void
i915_winsys_batchbuffer_dword_unchecked(struct i915_winsys_batchbuffer *batch,
                              unsigned dword)
{
   *(unsigned *)batch->ptr = dword;
   batch->ptr += 4;
}

static INLINE void
i915_winsys_batchbuffer_dword(struct i915_winsys_batchbuffer *batch,
                              unsigned dword)
{
   assert (i915_winsys_batchbuffer_space(batch) >= 4);
   i915_winsys_batchbuffer_dword_unchecked(batch, dword);
}

static INLINE void
i915_winsys_batchbuffer_write(struct i915_winsys_batchbuffer *batch,
			      void *data,
			      size_t size)
{
   assert (i915_winsys_batchbuffer_space(batch) >= size);

   memcpy(data, batch->ptr, size);
   batch->ptr += size;
}

static INLINE int
i915_winsys_batchbuffer_reloc(struct i915_winsys_batchbuffer *batch,
                              struct i915_winsys_buffer *buffer,
                              enum i915_winsys_buffer_usage usage,
                              size_t offset, bool fenced)
{
   return batch->iws->batchbuffer_reloc(batch, buffer, usage, offset, fenced);
}

#endif
