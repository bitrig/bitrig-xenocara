/*
   Copyright (c) 1999,  The XFree86 Project Inc. 
   Code adapted from mga/mga_shadow.c (Mark Vojkovich <markv@valinux.com>)
   by Michel Dänzer <michdaen@iiic.ethz.ch>
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86Pci.h"
#include "glint.h"
#include "shadowfb.h"
#include "servermd.h"



void
GLINTRefreshArea(ScrnInfoPtr pScrn, int num, BoxPtr pbox)
{
    GLINTPtr pGlint = GLINTPTR(pScrn);
    int width, height, Bpp, FBPitch;
    unsigned char *src, *dst;
   
    Bpp = pScrn->bitsPerPixel >> 3;
    FBPitch = BitmapBytePad(pScrn->displayWidth * pScrn->bitsPerPixel);

    while(num--) {
	width = (pbox->x2 - pbox->x1) * Bpp;
	height = pbox->y2 - pbox->y1;
	src = pGlint->ShadowPtr + (pbox->y1 * pGlint->ShadowPitch) + 
						(pbox->x1 * Bpp);
	dst = pGlint->FbBase + (pbox->y1 * FBPitch) + (pbox->x1 * Bpp);

	while(height--) {
	    memcpy(dst, src, width);
	    dst += FBPitch;
	    src += pGlint->ShadowPitch;
	}
	
	pbox++;
    }
} 
