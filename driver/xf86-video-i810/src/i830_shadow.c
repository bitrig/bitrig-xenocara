/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/s3virge/s3v_shadow.c,v 1.3 2000/03/31 20:13:33 dawes Exp $ */

/*
   Copyright (c) 1999,2000  The XFree86 Project Inc. 
   based on code written by Mark Vojkovich <markv@valinux.com>
*/

/*
 * Ported from the savage driver to the I830 by
 * Helmar Spangenberg <hspangenberg@frey.de> and Dima Dorfman
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86.h"
#include "i830.h"
#include "shadowfb.h"
#include "servermd.h"


void
I830RefreshArea(ScrnInfoPtr pScrn, int num, BoxPtr pbox)
{
    I830Ptr pI830 = I830PTR(pScrn);
    int width, height, Bpp, FBPitch;
    unsigned char *src, *dst;
   
    Bpp = pScrn->bitsPerPixel >> 3;
    FBPitch = BitmapBytePad(pScrn->displayWidth * pScrn->bitsPerPixel);

    while(num--) {
	width = (pbox->x2 - pbox->x1) * Bpp;
	height = pbox->y2 - pbox->y1;
	src = pI830->shadowPtr + (pbox->y1 * pI830->shadowPitch) + 
						(pbox->x1 * Bpp);
	dst = pI830->FbBase + (pbox->y1 * FBPitch) + (pbox->x1 * Bpp);

	while(height--) {
	    memcpy(dst, src, width);
	    dst += FBPitch;
	    src += pI830->shadowPitch;
	}
	
	pbox++;
    }
} 


void
I830PointerMoved(int index, int x, int y)
{
    ScrnInfoPtr pScrn = xf86Screens[index];
    I830Ptr pI830 = I830PTR(pScrn);
    int newX, newY;

    if(pI830->rotate == 1) {
	newX = pScrn->pScreen->height - y - 1;
	newY = x;
    } else {
	newX = y;
	newY = pScrn->pScreen->width - x - 1;
    }

    (*pI830->PointerMoved)(index, newX, newY);
}

void
I830RefreshArea8(ScrnInfoPtr pScrn, int num, BoxPtr pbox)
{
    I830Ptr pI830 = I830PTR(pScrn);
    int count, width, height, y1, y2, dstPitch, srcPitch;
    CARD8 *dstPtr, *srcPtr, *src;
    CARD32 *dst;

    dstPitch = pScrn->displayWidth;
    srcPitch = -pI830->rotate * pI830->shadowPitch;

    while(num--) {
	width = pbox->x2 - pbox->x1;
	y1 = pbox->y1 & ~3;
	y2 = (pbox->y2 + 3) & ~3;
	height = (y2 - y1) >> 2;  /* in dwords */

	if(pI830->rotate == 1) {
	    dstPtr = pI830->FbBase + 
			(pbox->x1 * dstPitch) + pScrn->virtualX - y2;
	    srcPtr = pI830->shadowPtr + ((1 - y2) * srcPitch) + pbox->x1;
	} else {
	    dstPtr = pI830->FbBase + 
			((pScrn->virtualY - pbox->x2) * dstPitch) + y1;
	    srcPtr = pI830->shadowPtr + (y1 * srcPitch) + pbox->x2 - 1;
	}

	while(width--) {
	    src = srcPtr;
	    dst = (CARD32*)dstPtr;
	    count = height;
	    while(count--) {
		*(dst++) = src[0] | (src[srcPitch] << 8) | 
					(src[srcPitch * 2] << 16) | 
					(src[srcPitch * 3] << 24);
		src += srcPitch * 4;
	    }
	    srcPtr += pI830->rotate;
	    dstPtr += dstPitch;
	}

	pbox++;
    }
} 


void
I830RefreshArea16(ScrnInfoPtr pScrn, int num, BoxPtr pbox)
{
    I830Ptr pI830 = I830PTR(pScrn);
    int count, width, height, y1, y2, dstPitch, srcPitch;
    CARD16 *dstPtr, *srcPtr, *src;
    CARD32 *dst;

    dstPitch = pScrn->displayWidth;
    srcPitch = -pI830->rotate * pI830->shadowPitch >> 1;

    while(num--) {
	width = pbox->x2 - pbox->x1;
	y1 = pbox->y1 & ~1;
	y2 = (pbox->y2 + 1) & ~1;
	height = (y2 - y1) >> 1;  /* in dwords */

	if(pI830->rotate == 1) {
	    dstPtr = (CARD16*)pI830->FbBase + 
			(pbox->x1 * dstPitch) + pScrn->virtualX - y2;
	    srcPtr = (CARD16*)pI830->shadowPtr + 
			((1 - y2) * srcPitch) + pbox->x1;
	} else {
	    dstPtr = (CARD16*)pI830->FbBase + 
			((pScrn->virtualY - pbox->x2) * dstPitch) + y1;
	    srcPtr = (CARD16*)pI830->shadowPtr + 
			(y1 * srcPitch) + pbox->x2 - 1;
	}

	while(width--) {
	    src = srcPtr;
	    dst = (CARD32*)dstPtr;
	    count = height;
	    while(count--) {
		*(dst++) = src[0] | (src[srcPitch] << 16);
		src += srcPitch * 2;
	    }
	    srcPtr += pI830->rotate;
	    dstPtr += dstPitch;
	}

	pbox++;
    }
}


/* this one could be faster */
void
I830RefreshArea24(ScrnInfoPtr pScrn, int num, BoxPtr pbox)
{
    I830Ptr pI830 = I830PTR(pScrn);
    int count, width, height, y1, y2, dstPitch, srcPitch;
    CARD8 *dstPtr, *srcPtr, *src;
    CARD32 *dst;

    dstPitch = BitmapBytePad(pScrn->displayWidth * 24);
    srcPitch = -pI830->rotate * pI830->shadowPitch;

    while(num--) {
        width = pbox->x2 - pbox->x1;
        y1 = pbox->y1 & ~3;
        y2 = (pbox->y2 + 3) & ~3;
        height = (y2 - y1) >> 2;  /* blocks of 3 dwords */

	if(pI830->rotate == 1) {
	    dstPtr = pI830->FbBase + 
			(pbox->x1 * dstPitch) + ((pScrn->virtualX - y2) * 3);
	    srcPtr = pI830->shadowPtr + ((1 - y2) * srcPitch) + (pbox->x1 * 3);
	} else {
	    dstPtr = pI830->FbBase + 
			((pScrn->virtualY - pbox->x2) * dstPitch) + (y1 * 3);
	    srcPtr = pI830->shadowPtr + (y1 * srcPitch) + (pbox->x2 * 3) - 3;
	}

	while(width--) {
	    src = srcPtr;
	    dst = (CARD32*)dstPtr;
	    count = height;
	    while(count--) {
		dst[0] = src[0] | (src[1] << 8) | (src[2] << 16) |
				(src[srcPitch] << 24);		
		dst[1] = src[srcPitch + 1] | (src[srcPitch + 2] << 8) |
				(src[srcPitch * 2] << 16) |
				(src[(srcPitch * 2) + 1] << 24);		
		dst[2] = src[(srcPitch * 2) + 2] | (src[srcPitch * 3] << 8) |
				(src[(srcPitch * 3) + 1] << 16) |
				(src[(srcPitch * 3) + 2] << 24);	
		dst += 3;
		src += srcPitch * 4;
	    }
	    srcPtr += pI830->rotate * 3;
	    dstPtr += dstPitch; 
	}

	pbox++;
    }
}

void
I830RefreshArea32(ScrnInfoPtr pScrn, int num, BoxPtr pbox)
{
    I830Ptr pI830 = I830PTR(pScrn);
    int count, width, height, dstPitch, srcPitch;
    CARD32 *dstPtr, *srcPtr, *src, *dst;

    dstPitch = pScrn->displayWidth;
    srcPitch = -pI830->rotate * pI830->shadowPitch >> 2;

    while(num--) {
	width = pbox->x2 - pbox->x1;
	height = pbox->y2 - pbox->y1;

	if(pI830->rotate == 1) {
	    dstPtr = (CARD32*)pI830->FbBase + 
			(pbox->x1 * dstPitch) + pScrn->virtualX - pbox->y2;
	    srcPtr = (CARD32*)pI830->shadowPtr + 
			((1 - pbox->y2) * srcPitch) + pbox->x1;
	} else {
	    dstPtr = (CARD32*)pI830->FbBase + 
			((pScrn->virtualY - pbox->x2) * dstPitch) + pbox->y1;
	    srcPtr = (CARD32*)pI830->shadowPtr + 
			(pbox->y1 * srcPitch) + pbox->x2 - 1;
	}

	while(width--) {
	    src = srcPtr;
	    dst = dstPtr;
	    count = height;
	    while(count--) {
		*(dst++) = *src;
		src += srcPitch;
	    }
	    srcPtr += pI830->rotate;
	    dstPtr += dstPitch;
	}

	pbox++;
    }
}
