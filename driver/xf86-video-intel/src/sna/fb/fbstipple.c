/*
 * Copyright © 1998 Keith Packard
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

#include "fb.h"

/*
 * This is a slight abuse of the preprocessor to generate repetitive
 * code, the idea is to generate code for each case of a copy-mode
 * transparent stipple
 */
#define LaneCases1(c,a) \
	case c: while (n--) { FbLaneCase(c,a); a++; } break
#define LaneCases2(c,a)	    LaneCases1(c,a); LaneCases1(c+1,a)
#define LaneCases4(c,a)	    LaneCases2(c,a); LaneCases2(c+2,a)
#define LaneCases8(c,a)	    LaneCases4(c,a); LaneCases4(c+4,a)
#define LaneCases16(c,a)    LaneCases8(c,a); LaneCases8(c+8,a)

#define LaneCases(a)	    LaneCases16(0,a)

/*
 * Repeat a transparent stipple across a scanline n times
 */

void
fbTransparentSpan(FbBits * dst, FbBits stip, FbBits fgxor, int n)
{
	FbStip s;

	s = ((FbStip) (stip) & 0x01);
	s |= ((FbStip) (stip >> 8) & 0x02);
	s |= ((FbStip) (stip >> 16) & 0x04);
	s |= ((FbStip) (stip >> 24) & 0x08);
	switch (s) {
		LaneCases(dst);
	}
}

static void
fbEvenStipple(FbBits *dst, FbStride dstStride, int dstX, int dstBpp,
              int width, int height,
              FbStip *stip, FbStride stipStride,
              int stipHeight,
              FbBits fgand, FbBits fgxor, FbBits bgand, FbBits bgxor,
	      int xRot, int yRot)
{
	FbBits startmask, endmask;
	FbBits mask, and, xor;
	int nmiddle, n;
	FbStip *s, *stipEnd, bits;
	int rot, stipX, stipY;
	int pixelsPerDst;
	const FbBits *fbBits;
	Bool transparent;
	int startbyte, endbyte;

	/*
	 * Check for a transparent stipple (stencil)
	 */
	transparent = FALSE;
	if (dstBpp >= 8 && fgand == 0 && bgand == FB_ALLONES && bgxor == 0)
		transparent = TRUE;

	pixelsPerDst = FB_UNIT / dstBpp;
	/*
	 * Adjust dest pointers
	 */
	dst += dstX >> FB_SHIFT;
	dstX &= FB_MASK;
	FbMaskBitsBytes(dstX, width, fgand == 0 && bgand == 0,
			startmask, startbyte, nmiddle, endmask, endbyte);

	if (startmask)
		dstStride--;
	dstStride -= nmiddle;

	xRot *= dstBpp;
	/*
	 * Compute stip start scanline and rotation parameters
	 */
	stipEnd = stip + stipStride * stipHeight;
	modulus(-yRot, stipHeight, stipY);
	s = stip + stipStride * stipY;
	modulus(-xRot, FB_UNIT, stipX);
	rot = stipX;

	/*
	 * Get pointer to stipple mask array for this depth
	 */
	/* fbStippleTable covers all valid bpp (4,8,16,32) */
	fbBits = fbStippleTable[pixelsPerDst];

	while (height--) {
		/*
		 * Extract stipple bits for this scanline;
		 */
		bits = READ(s);
		s += stipStride;
		if (s == stipEnd)
			s = stip;
		mask = fbBits[FbLeftStipBits(bits, pixelsPerDst)];
		/*
		 * Rotate into position and compute reduced rop values
		 */
		mask = FbRotLeft(mask, rot);
		and = (fgand & mask) | (bgand & ~mask);
		xor = (fgxor & mask) | (bgxor & ~mask);

		if (transparent) {
			if (startmask) {
				fbTransparentSpan(dst, mask & startmask, fgxor, 1);
				dst++;
			}
			fbTransparentSpan(dst, mask, fgxor, nmiddle);
			dst += nmiddle;
			if (endmask)
				fbTransparentSpan(dst, mask & endmask, fgxor, 1);
		} else {
			/*
			 * Fill scanline
			 */
			if (startmask) {
				FbDoLeftMaskByteRRop(dst, startbyte, startmask, and, xor);
				dst++;
			}
			n = nmiddle;
			if (!and)
				while (n--)
					WRITE(dst++, xor);
			else {
				while (n--) {
					WRITE(dst, FbDoRRop(READ(dst), and, xor));
					dst++;
				}
			}
			if (endmask)
				FbDoRightMaskByteRRop(dst, endbyte, endmask, and, xor);
		}
		dst += dstStride;
	}
}

static void
fbOddStipple(FbBits *dst, FbStride dstStride, int dstX, int dstBpp,
             int width, int height,
             FbStip *stip, FbStride stipStride,
             int stipWidth, int stipHeight,
             FbBits fgand, FbBits fgxor, FbBits bgand, FbBits bgxor,
	     int xRot, int yRot)
{
	int stipX, stipY, sx;
	int widthTmp;
	int h, w;
	int x, y;

	modulus(-yRot, stipHeight, stipY);
	modulus(dstX / dstBpp - xRot, stipWidth, stipX);
	y = 0;
	while (height) {
		h = stipHeight - stipY;
		if (h > height)
			h = height;
		height -= h;
		widthTmp = width;
		x = dstX;
		sx = stipX;
		while (widthTmp) {
			w = (stipWidth - sx) * dstBpp;
			if (w > widthTmp)
				w = widthTmp;
			widthTmp -= w;
			fbBltOne(stip + stipY * stipStride,
				 stipStride,
				 sx,
				 dst + y * dstStride,
				 dstStride, x, dstBpp, w, h, fgand, fgxor, bgand, bgxor);
			x += w;
			sx = 0;
		}
		y += h;
		stipY = 0;
	}
}

void
fbStipple(FbBits *dst, FbStride dstStride, int dstX, int dstBpp,
          int width, int height,
          FbStip *stip, FbStride stipStride,
          int stipWidth, int stipHeight, Bool even,
          FbBits fgand, FbBits fgxor, FbBits bgand, FbBits bgxor,
	  int xRot, int yRot)
{
	DBG(("%s stipple=%dx%d, size=%dx%d\n",
	     __FUNCTION__, stipWidth, stipHeight, width, height));

	if (even)
		fbEvenStipple(dst, dstStride, dstX, dstBpp, width, height,
			      stip, stipStride, stipHeight,
			      fgand, fgxor, bgand, bgxor, xRot, yRot);
	else
		fbOddStipple(dst, dstStride, dstX, dstBpp, width, height,
			     stip, stipStride, stipWidth, stipHeight,
			     fgand, fgxor, bgand, bgxor, xRot, yRot);
}
