/*
 * Copyright © 2000 Keith Packard, member of The XFree86 Project, Inc.
 * Copyright © 2012 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef FBPICT_H
#define FBPICT_H

#include "sfb.h"

extern void
fbComposite(CARD8 op,
            PicturePtr pSrc,
            PicturePtr pMask,
            PicturePtr pDst,
            INT16 xSrc,
            INT16 ySrc,
            INT16 xMask,
            INT16 yMask, INT16 xDst, INT16 yDst, CARD16 width, CARD16 height);

extern pixman_image_t *image_from_pict(PicturePtr pict,
				       Bool has_clip,
				       int *xoff, int *yoff);

extern void free_pixman_pict(PicturePtr, pixman_image_t *);

#endif  /* FBPICT_H */
