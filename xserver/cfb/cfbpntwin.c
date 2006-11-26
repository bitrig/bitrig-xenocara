/***********************************************************

Copyright 1987, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.


Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/X.h>

#include "windowstr.h"
#include "regionstr.h"
#include "pixmapstr.h"
#include "scrnintstr.h"

#include "cfb.h"
#include "cfbmskbits.h"
#include "mi.h"

#ifdef PANORAMIX
#include "panoramiX.h"
#include "panoramiXsrv.h"
#endif

void
cfbPaintWindow(pWin, pRegion, what)
    WindowPtr	pWin;
    RegionPtr	pRegion;
    int		what;
{
    register cfbPrivWin	*pPrivWin;
    WindowPtr	pBgWin;

    pPrivWin = cfbGetWindowPrivate(pWin);


    switch (what) {
    case PW_BACKGROUND:
	switch (pWin->backgroundState) {
	case None:
	    break;
	case ParentRelative:
	    do {
		pWin = pWin->parent;
	    } while (pWin->backgroundState == ParentRelative);
	    (*pWin->drawable.pScreen->PaintWindowBackground)(pWin, pRegion,
							     what);
	    break;
	case BackgroundPixmap:
	    if (pPrivWin->fastBackground)
	    {
		cfbFillBoxTile32 ((DrawablePtr)pWin,
				  (int)REGION_NUM_RECTS(pRegion),
				  REGION_RECTS(pRegion),
				  pPrivWin->pRotatedBackground);
	    }
	    else
	    {
		int xorg = pWin->drawable.x;
		int yorg = pWin->drawable.y;
#ifdef PANORAMIX
		if(!noPanoramiXExtension) {
		    int index = pWin->drawable.pScreen->myNum;
		    if(WindowTable[index] == pWin) {
			xorg -= panoramiXdataPtr[index].x;
			yorg -= panoramiXdataPtr[index].y;
		    }
		}
#endif
		cfbFillBoxTileOdd ((DrawablePtr)pWin,
				   (int)REGION_NUM_RECTS(pRegion),
				   REGION_RECTS(pRegion),
				   pWin->background.pixmap,
				   xorg, yorg);
	    }
	    break;
	case BackgroundPixel:
	    cfbFillBoxSolid ((DrawablePtr)pWin,
			     (int)REGION_NUM_RECTS(pRegion),
			     REGION_RECTS(pRegion),
			     pWin->background.pixel);
	    break;
    	}
    	break;
    case PW_BORDER:
	if (pWin->borderIsPixel)
	{
	    cfbFillBoxSolid ((DrawablePtr)pWin,
			     (int)REGION_NUM_RECTS(pRegion),
			     REGION_RECTS(pRegion),
			     pWin->border.pixel);
	}
	else if (pPrivWin->fastBorder)
	{
	    cfbFillBoxTile32 ((DrawablePtr)pWin,
			      (int)REGION_NUM_RECTS(pRegion),
			      REGION_RECTS(pRegion),
			      pPrivWin->pRotatedBorder);
	}
	else
	{
	    int xorg, yorg;

	    for (pBgWin = pWin;
		 pBgWin->backgroundState == ParentRelative;
		 pBgWin = pBgWin->parent);

	    xorg = pBgWin->drawable.x;
	    yorg = pBgWin->drawable.y;

#ifdef PANORAMIX
	    if(!noPanoramiXExtension) {
		int index = pWin->drawable.pScreen->myNum;
		if(WindowTable[index] == pBgWin) {
		    xorg -= panoramiXdataPtr[index].x;
		    yorg -= panoramiXdataPtr[index].y;
		}
	    }
#endif

	    cfbFillBoxTileOdd ((DrawablePtr)pWin,
			       (int)REGION_NUM_RECTS(pRegion),
			       REGION_RECTS(pRegion),
			       pWin->border.pixmap,
			       xorg, yorg);
	}
	break;
    }
}

/*
 * Use the RROP macros in copy mode
 */

#define RROP GXcopy
#include "cfbrrop.h"

#ifdef RROP_UNROLL
# define Expand(left,right,leftAdjust) {\
    int part = nmiddle & RROP_UNROLL_MASK; \
    int widthStep; \
    widthStep = widthDst - nmiddle - leftAdjust; \
    nmiddle >>= RROP_UNROLL_SHIFT; \
    while (h--) { \
	left \
	pdst += part; \
	switch (part) { \
	    RROP_UNROLL_CASE(pdst) \
	} \
	m = nmiddle; \
	while (m) { \
	    pdst += RROP_UNROLL; \
	    RROP_UNROLL_LOOP(pdst) \
	    m--; \
	} \
	right \
	pdst += widthStep; \
    } \
}

#else
# define Expand(left, right, leftAdjust) { \
    int widthStep; \
    widthStep = widthDst - nmiddle - leftAdjust; \
    while (h--) { \
	left \
	m = nmiddle; \
	while (m--) {\
	    RROP_SOLID(pdst); \
	    pdst++; \
	} \
	right \
	pdst += widthStep; \
    } \
}
#endif

void
cfbFillBoxSolid (pDrawable, nBox, pBox, pixel)
    DrawablePtr	    pDrawable;
    int		    nBox;
    BoxPtr	    pBox;
    unsigned long   pixel;
{
    CfbBits   *pdstBase;
    int		    widthDst;
    register int    h;
    register CfbBits   *pdst;
    int		    nmiddle;
    int		    w;
#if PSZ == 24
    int leftIndex, rightIndex;
    CfbBits piQxelArray[3], *pdstULC; /*upper left corner*/

    piQxelArray[0] = (pixel&0xFFFFFF) | ((pixel&0xFF)<<24);
    piQxelArray[1] = ((pixel&0xFFFF00)>>8) | ((pixel&0xFFFF)<<16);
    piQxelArray[2] = ((pixel&0xFFFFFF)<<8) | ((pixel&0xFF0000)>>16);
#else
    register CfbBits   rrop_xor;
    register CfbBits   leftMask, rightMask;
    register int    m;
#endif

    cfbGetLongWidthAndPointer(pDrawable, widthDst, pdstBase);

#if PSZ != 24
    rrop_xor = PFILL(pixel);
#endif
    for (; nBox; nBox--, pBox++)
    {
    	pdst = pdstBase + pBox->y1 * widthDst;
    	h = pBox->y2 - pBox->y1;
	w = pBox->x2 - pBox->x1;
#if PSZ == 8
	if (w == 1)
	{
	    register char    *pdstb = ((char *) pdst) + pBox->x1;
	    int	    incr = widthDst * PGSZB;

	    while (h--)
	    {
		*pdstb = rrop_xor;
		pdstb += incr;
	    }
	}
	else
	{
#endif
#if PSZ == 24
/* _Box has x1, y1, x2, y2*/
	  leftIndex = pBox->x1 & 3;
	  rightIndex = ((leftIndex+w)<5)?0:(pBox->x2 &3);
	  nmiddle = w - rightIndex;
	  if(leftIndex){
	      nmiddle -= (4 - leftIndex);
	  }
	  nmiddle >>= 2;
	  if(nmiddle < 0)
	    nmiddle = 0;

	  pdst = pdstBase + pBox->y1 * widthDst + ((pBox->x1*3) >> 2);

	  switch(leftIndex+w){
	  case 4:
	    switch(leftIndex){
	    case 0:
	      while(h--){
		*pdst++ = piQxelArray[0];
		*pdst++ = piQxelArray[1];
		*pdst   = piQxelArray[2];
		pdst -=2;
		pdst += widthDst;
	      }
	      break;
	    case 1:
	      while(h--){
		*pdst = ((*pdst) & 0xFFFFFF) | (piQxelArray[0] & 0xFF000000);
		pdst++;
		*pdst++ = piQxelArray[1];
		*pdst   = piQxelArray[2];
		pdst -=2;
		pdst += widthDst;
	      }
	      break;
	    case 2:
	      while(h--){
		*pdst = ((*pdst) & 0xFFFF) | (piQxelArray[1] & 0xFFFF0000);
		pdst++;
		*pdst-- = piQxelArray[2];
		pdst += widthDst;
	      }
	      break;
	    case 3:
	      while(h--){
		*pdst = ((*pdst) & 0xFF) | (piQxelArray[2] & 0xFFFFFF00);
		pdst += widthDst;
	      }
	      break;
	    }
	    break;
	  case 3:
	    switch(leftIndex){
	    case 0:
	      while(h--){
		*pdst++ = piQxelArray[0];
		*pdst++ = piQxelArray[1];
		*pdst = ((*pdst) & 0xFFFFFF00) | (piQxelArray[2] & 0xFF);
		pdst--;
		pdst--;
		pdst += widthDst;
	      }
	      break;
	    case 1:
	      while(h--){
		*pdst = ((*pdst) & 0xFFFFFF) | (piQxelArray[0] & 0xFF000000);
		pdst++;
		*pdst++ = piQxelArray[1];
		*pdst = ((*pdst) & 0xFFFFFF00) | (piQxelArray[2] & 0xFF);
		pdst--;
		pdst--;
		pdst += widthDst;
	      }
	      break;
	    case 2:
	      while(h--){
		*pdst = ((*pdst) & 0xFFFF) | (piQxelArray[1] & 0xFFFF0000);
		pdst++;
		*pdst = ((*pdst) & 0xFFFFFF00) | (piQxelArray[2] & 0xFF);
		pdst--;
		pdst += widthDst;
	      }
	      break;
	    }
	    break;
	  case 2:
	    while(h--){
	      if(leftIndex){
		*pdst = ((*pdst) & 0xFFFFFF) | (piQxelArray[0] & 0xFF000000);
		pdst++;
	      }
	      else{
		*pdst++ = piQxelArray[0];
	      }
		*pdst = ((*pdst) & 0xFFFF0000) | (piQxelArray[1] & 0xFFFF);
		pdst--;
		pdst += widthDst;
	    }
	    break;
	  case 1: /*only if leftIndex = 0 and w = 1*/
	    while(h--){
		*pdst = ((*pdst) & 0xFF000000) | (piQxelArray[0] & 0xFFFFFF);
		pdst += widthDst;
	      }
	    break;
	  case 0: /*never*/
	    break;
	  default:
	  {
	    w = nmiddle;
	    pdstULC = pdst;
/*	    maskbits (pBox->x1, w, leftMask, rightMask, nmiddle);*/
	    while(h--){
	      nmiddle = w;
	      pdst = pdstULC;
	      switch(leftIndex){
	      case 0:
		break;
	      case 1:
		*pdst = ((*pdst) & 0xFFFFFF) | (piQxelArray[0] & 0xFF000000);
		pdst++;
		*pdst++ = piQxelArray[1];
		*pdst++ = piQxelArray[2];
	        break;
	      case 2:
		*pdst = ((*pdst) & 0xFFFF) | (piQxelArray[1] & 0xFFFF0000);
		pdst++;
		*pdst++ = piQxelArray[2];
	        break;
	      case 3:
		*pdst = ((*pdst) & 0xFF) | (piQxelArray[2] & 0xFFFFFF00);
		pdst++;
	        break;
	      }
	      while(nmiddle--){
		*pdst++ = piQxelArray[0];
		*pdst++ = piQxelArray[1];
		*pdst++ = piQxelArray[2];
	      }
	      switch(rightIndex){
	      case 0:
		break;
	      case 1:
		*pdst = ((*pdst) & 0xFF000000) | (piQxelArray[0] & 0xFFFFFF);
	        break;
	      case 2:
		*pdst++ = piQxelArray[0];
		*pdst = ((*pdst) & 0xFFFF0000) | (piQxelArray[1] & 0xFFFF);
	        break;
	      case 3:
		*pdst++ = piQxelArray[0];
		*pdst++ = piQxelArray[1];
		*pdst = ((*pdst) & 0xFFFFFF00) | (piQxelArray[2] & 0xFF);
	        break;
	    }
	    pdstULC += widthDst;
	    }

	  }
	}
#else
	pdst += (pBox->x1 >> PWSH);
	if ((pBox->x1 & PIM) + w <= PPW)
	{
	    maskpartialbits(pBox->x1, w, leftMask);
	    while (h--) {
		*pdst = (*pdst & ~leftMask) | (rrop_xor & leftMask);
		pdst += widthDst;
	    }
	}
	else
	{
	    maskbits (pBox->x1, w, leftMask, rightMask, nmiddle);
	    if (leftMask)
	    {
		if (rightMask)
		{
		    Expand (RROP_SOLID_MASK (pdst, leftMask); pdst++; ,
			    RROP_SOLID_MASK (pdst, rightMask); ,
			    1)
		}
		else
		{
		    Expand (RROP_SOLID_MASK (pdst, leftMask); pdst++;,
			    ;,
			    1)
		}
	    }
	    else
	    {
		if (rightMask)
		{
		    Expand (;,
			    RROP_SOLID_MASK (pdst, rightMask);,
			    0)
		}
		else
		{
		    Expand (;,
			    ;,
			    0)
		}
	    }
	}
#endif
#if PSZ == 8
	}
#endif
    }
}

void
cfbFillBoxTile32 (pDrawable, nBox, pBox, tile)
    DrawablePtr	    pDrawable;
    int		    nBox;	/* number of boxes to fill */
    BoxPtr 	    pBox;	/* pointer to list of boxes to fill */
    PixmapPtr	    tile;	/* rotated, expanded tile */
{
    register CfbBits  *pdst;
    CfbBits	    *psrc;
    int			    tileHeight;

    int			    widthDst;
    int			    w;
    int			    h;
    int			    nmiddle;
    int			    y;
    int			    srcy;

    CfbBits	    *pdstBase;
#if PSZ == 24
    int			    leftIndex, rightIndex;
    CfbBits piQxelArray[3], *pdstULC;
#else
    register CfbBits  rrop_xor;	
    register CfbBits  leftMask;
    register CfbBits  rightMask;
    register int      m;
#endif

    tileHeight = tile->drawable.height;
    psrc = (CfbBits *)tile->devPrivate.ptr;

    cfbGetLongWidthAndPointer (pDrawable, widthDst, pdstBase);

    while (nBox--)
    {
#if PSZ == 24
	w = pBox->x2 - pBox->x1;
	h = pBox->y2 - pBox->y1;
	y = pBox->y1;
	leftIndex = pBox->x1 & 3;
/*	rightIndex = ((leftIndex+w)<5)?0:pBox->x2 &3;*/
	rightIndex = pBox->x2 &3;
	nmiddle = w - rightIndex;
	if(leftIndex){
	  nmiddle -= (4 - leftIndex);
	}
	nmiddle >>= 2;
	if(nmiddle < 0)
	  nmiddle = 0;

	pdst = pdstBase + ((pBox->x1 *3)>> 2) +  pBox->y1 * widthDst;
	srcy = y % tileHeight;

#define StepTile    piQxelArray[0] = (psrc[srcy] & 0xFFFFFF) | ((psrc[srcy] & 0xFF)<<24); \
		    piQxelArray[1] = (psrc[srcy] & 0xFFFF00) | ((psrc[srcy] & 0xFFFF)<<16); \
		    piQxelArray[2] = ((psrc[srcy] & 0xFF0000)>>16) | \
		    		     ((psrc[srcy] & 0xFFFFFF)<<8); \
		    /*rrop_xor = psrc[srcy];*/ \
		    ++srcy; \
		    if (srcy == tileHeight) \
		        srcy = 0;

	  switch(leftIndex+w){
	  case 4:
	    switch(leftIndex){
	    case 0:
	      while(h--){
		  StepTile
		*pdst++ = piQxelArray[0];
		*pdst++ = piQxelArray[1];
		*pdst   = piQxelArray[2];
		pdst-=2;
		pdst += widthDst;
	      }
	      break;
	    case 1:
	      while(h--){
		  StepTile
		*pdst = ((*pdst) & 0xFFFFFF) | (piQxelArray[0] & 0xFF000000);
		pdst++;
		*pdst++ = piQxelArray[1];
		*pdst   = piQxelArray[2];
		pdst-=2;
		pdst += widthDst;
	      }
	      break;
	    case 2:
	      while(h--){
		  StepTile
		*pdst = ((*pdst) & 0xFFFF) | (piQxelArray[1] & 0xFFFF0000);
		pdst++;
		*pdst--   = piQxelArray[2];
		pdst += widthDst;
	      }
	      break;
	    case 3:
	      while(h--){
		  StepTile
		*pdst = ((*pdst) & 0xFF) | (piQxelArray[2] & 0xFFFFFF00);
		pdst += widthDst;
	      }
	      break;
	    }
	    break;
	  case 3:
	    switch(leftIndex){
	    case 0:
	      while(h--){
		  StepTile
		*pdst++ = piQxelArray[0];
		*pdst++ = piQxelArray[1];
		*pdst = ((*pdst) & 0xFFFFFF00) | (piQxelArray[2] & 0xFF);
		pdst--;
		pdst--;
		pdst += widthDst;
	      }
	      break;
	    case 1:
	      while(h--){
		  StepTile
		*pdst = ((*pdst) & 0xFFFFFF) | (piQxelArray[0] & 0xFF000000);
		pdst++;
		*pdst++ = piQxelArray[1];
		*pdst = ((*pdst) & 0xFFFFFF00) | (piQxelArray[2] & 0xFF);
		pdst--;
		pdst--;
		pdst += widthDst;
	      }
	      break;
	    case 2:
	      while(h--){
		  StepTile
		*pdst = ((*pdst) & 0xFFFF) | (piQxelArray[1] & 0xFFFF0000);
		pdst++;
		*pdst = ((*pdst) & 0xFFFFFF00) | (piQxelArray[2] & 0xFF);
		pdst--;
		pdst += widthDst;
	      }
	      break;
	    }
	    break;
	  case 2:
	    while(h--){
		  StepTile
	      if(leftIndex){
		*pdst = ((*pdst) & 0xFFFFFF) | (piQxelArray[0] & 0xFF000000);
		pdst++;
	      }
	      else{
		*pdst++ = piQxelArray[0];
	      }
		*pdst = ((*pdst) & 0xFFFF0000) | (piQxelArray[1] & 0xFFFF);
		pdst--;
		pdst += widthDst;
	    }
	    break;
	  case 1: /*only if leftIndex = 0 and w = 1*/
	    while(h--){
		  StepTile
		*pdst = ((*pdst) & 0xFF000000) | (piQxelArray[0] & 0xFFFFFF);
		pdst += widthDst;
	      }
	    break;
	  case 0: /*never*/
	    break;
	  default:
	  {
	    w = nmiddle;
	    pdstULC = pdst;

	    while(h--){
	      StepTile
	      nmiddle = w;
	      pdst = pdstULC;
	      switch(leftIndex){
	      case 0:
		break;
	      case 1:
		*pdst = ((*pdst) & 0xFFFFFF) | (piQxelArray[0] & 0xFF000000);
		pdst++;
		*pdst++ = piQxelArray[1];
		*pdst++ = piQxelArray[2];
	        break;
	      case 2:
		*pdst = ((*pdst) & 0xFFFF) | (piQxelArray[1] & 0xFFFF0000);
		pdst++;
		*pdst++ = piQxelArray[2];
	        break;
	      case 3:
		*pdst = ((*pdst) & 0xFF) | (piQxelArray[2] & 0xFFFFFF00);
		pdst++;
	        break;
	      }
	      while(nmiddle--){
		*pdst++ = piQxelArray[0];
		*pdst++ = piQxelArray[1];
		*pdst++ = piQxelArray[2];
	      }
	      switch(rightIndex){
	      case 0:
		break;
	      case 1:
		*pdst = ((*pdst) & 0xFF000000) | (piQxelArray[0] & 0xFFFFFF);
		break;
	      case 2:
		*pdst++ = piQxelArray[0];
		*pdst = ((*pdst) & 0xFFFF0000) | (piQxelArray[1] & 0xFFFF);
		break;
	      case 3:
		*pdst++ = piQxelArray[0];
		*pdst++ = piQxelArray[1];
		*pdst = ((*pdst) & 0xFFFFFF00) | (piQxelArray[2] & 0xFF);
		break;
	      }
	      pdstULC += widthDst;
	    }
	  }
	  }
#else
	w = pBox->x2 - pBox->x1;
	h = pBox->y2 - pBox->y1;
	y = pBox->y1;
	pdst = pdstBase + (pBox->y1 * widthDst) + (pBox->x1 >> PWSH);
	srcy = y % tileHeight;

#define StepTile    rrop_xor = psrc[srcy]; \
		    ++srcy; \
		    if (srcy == tileHeight) \
		        srcy = 0;

	if ( ((pBox->x1 & PIM) + w) < PPW)
	{
	    maskpartialbits(pBox->x1, w, leftMask);
	    rightMask = ~leftMask;
	    while (h--)
	    {
		StepTile
		*pdst = (*pdst & rightMask) | (rrop_xor & leftMask);
		pdst += widthDst;
	    }
	}
	else
	{
	    maskbits(pBox->x1, w, leftMask, rightMask, nmiddle);

	    if (leftMask)
	    {
		if (rightMask)
		{
		    Expand (StepTile
			    RROP_SOLID_MASK(pdst, leftMask); pdst++;,
			    RROP_SOLID_MASK(pdst, rightMask);,
			    1)
		}
		else
		{
		    Expand (StepTile
			    RROP_SOLID_MASK(pdst, leftMask); pdst++;,
			    ;,
			    1)
		}
	    }
	    else
	    {
		if (rightMask)
		{
		    Expand (StepTile
			    ,
			    RROP_SOLID_MASK(pdst, rightMask);,
			    0)
		}
		else
		{
		    Expand (StepTile
			    ,
			    ;,
			    0)
		}
	    }
	}
#endif
        pBox++;
    }
}
