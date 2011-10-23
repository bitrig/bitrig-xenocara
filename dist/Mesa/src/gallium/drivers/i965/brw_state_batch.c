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
     


#include "brw_state.h"
#include "brw_batchbuffer.h"



/* A facility similar to the data caching code above, which aims to
 * prevent identical commands being issued repeatedly.
 */
GLboolean brw_cached_batch_struct( struct brw_context *brw,
				   const void *data,
				   GLuint sz )
{
   struct brw_cached_batch_item *item = brw->cached_batch_items;
   struct header *newheader = (struct header *)data;

   if (brw->flags.always_emit_state) {
      brw_batchbuffer_data(brw->batch, data, sz, IGNORE_CLIPRECTS);
      return GL_TRUE;
   }

   while (item) {
      if (item->header->opcode == newheader->opcode) {
	 if (item->sz == sz && memcmp(item->header, newheader, sz) == 0)
	    return GL_FALSE;
	 if (item->sz != sz) {
	    FREE(item->header);
	    item->header = MALLOC(sz);
	    item->sz = sz;
	 }
	 goto emit;
      }
      item = item->next;
   }

   assert(!item);
   item = CALLOC_STRUCT(brw_cached_batch_item);
   item->header = MALLOC(sz);
   item->sz = sz;
   item->next = brw->cached_batch_items;
   brw->cached_batch_items = item;

 emit:
   memcpy(item->header, newheader, sz);
   brw_batchbuffer_data(brw->batch, data, sz, IGNORE_CLIPRECTS);
   return GL_TRUE;
}

void brw_clear_batch_cache( struct brw_context *brw )
{
   struct brw_cached_batch_item *item = brw->cached_batch_items;

   while (item) {
      struct brw_cached_batch_item *next = item->next;
      FREE((void *)item->header);
      FREE(item);
      item = next;
   }

   brw->cached_batch_items = NULL;
}

void brw_destroy_batch_cache( struct brw_context *brw )
{
   brw_clear_batch_cache(brw);
}
