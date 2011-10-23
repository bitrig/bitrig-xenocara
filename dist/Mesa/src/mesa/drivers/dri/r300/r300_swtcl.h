/*
Copyright (C) The Weather Channel, Inc.  2002.  All Rights Reserved.

The Weather Channel (TM) funded Tungsten Graphics to develop the
initial release of the Radeon 8500 driver under the XFree86 license.
This notice must be preserved.

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
*/

/*
 * Authors:
 *   Keith Whitwell <keith@tungstengraphics.com> - original r200 code
 *   Dave Airlie <airlied@linux.ie>
 */

#ifndef __R300_SWTCL_H__
#define __R300_SWTCL_H__

#include "main/mtypes.h"
#include "swrast/swrast.h"
#include "r300_context.h"

/*
 * Here are definitions of OVM locations of vertex attributes for non TCL hw
 */
#define SWTCL_OVM_POS 0
#define SWTCL_OVM_COLOR0 2
#define SWTCL_OVM_COLOR1 3
#define SWTCL_OVM_COLOR2 4
#define SWTCL_OVM_COLOR3 5
#define SWTCL_OVM_TEX(n) ((n) + 6)
#define SWTCL_OVM_POINT_SIZE 15

extern void r300ChooseSwtclVertexFormat(struct gl_context *ctx, GLuint *InputsRead,  GLuint *OutputsWritten);

extern void r300InitSwtcl( struct gl_context *ctx );
extern void r300DestroySwtcl( struct gl_context *ctx );

extern void r300RenderStart(struct gl_context *ctx);
extern void r300RenderFinish(struct gl_context *ctx);
extern void r300RenderPrimitive(struct gl_context *ctx, GLenum prim);
extern void r300ResetLineStipple(struct gl_context *ctx);

extern void r300_swtcl_flush(struct gl_context *ctx, uint32_t current_offset);

#endif
