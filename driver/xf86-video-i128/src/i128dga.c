
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86Pci.h"
#include "xf86PciInfo.h"
#include "i128.h"
#include "dgaproc.h"


static Bool I128_OpenFramebuffer(ScrnInfoPtr, char **, unsigned char **, 
					int *, int *, int *);
static Bool I128_SetMode(ScrnInfoPtr, DGAModePtr);
static int  I128_GetViewport(ScrnInfoPtr);
static void I128_SetViewport(ScrnInfoPtr, int, int, int);
#ifdef HAVE_XAA_H
static void I128_FillRect(ScrnInfoPtr, int, int, int, int, unsigned long);
static void I128_BlitRect(ScrnInfoPtr, int, int, int, int, int, int);
#if 0
static void I128_BlitTransRect(ScrnInfoPtr, int, int, int, int, int, int, 
					unsigned long);
#endif
#endif

static
DGAFunctionRec I128_DGAFuncs = {
   I128_OpenFramebuffer,
   NULL,
   I128_SetMode,
   I128_SetViewport,
   I128_GetViewport,
   I128EngineDone,
#ifdef HAVE_XAA_H
   I128_FillRect,
   I128_BlitRect,
#if 0
   I128_BlitTransRect
#else
   NULL
#endif
#else
   NULL, NULL, NULL
#endif
};


Bool
I128DGAInit(ScreenPtr pScreen)
{   
   ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
   I128Ptr pI128 = I128PTR(pScrn);
   DGAModePtr modes = NULL, newmodes = NULL, currentMode;
   DisplayModePtr pMode, firstMode;
   int Bpp = pScrn->bitsPerPixel >> 3;
   int num = 0;
   Bool oneMore;

   pMode = firstMode = pScrn->modes;

   while(pMode) {

	if(0 /*pScrn->displayWidth != pMode->HDisplay*/) {
	    newmodes = realloc(modes, (num + 2) * sizeof(DGAModeRec));
	    oneMore = TRUE;
	} else {
	    newmodes = realloc(modes, (num + 1) * sizeof(DGAModeRec));
	    oneMore = FALSE;
	}

	if(!newmodes) {
	   free(modes);
	   return FALSE;
	}
	modes = newmodes;

SECOND_PASS:

	currentMode = modes + num;
	num++;

	currentMode->mode = pMode;
	currentMode->flags = DGA_CONCURRENT_ACCESS | DGA_PIXMAP_AVAILABLE;
	currentMode->flags |= DGA_FILL_RECT | DGA_BLIT_RECT;
	if(pMode->Flags & V_DBLSCAN)
	   currentMode->flags |= DGA_DOUBLESCAN;
	if(pMode->Flags & V_INTERLACE)
	   currentMode->flags |= DGA_INTERLACED;
	currentMode->byteOrder = pScrn->imageByteOrder;
	currentMode->depth = pScrn->depth;
	currentMode->bitsPerPixel = pScrn->bitsPerPixel;
	currentMode->red_mask = pScrn->mask.red;
	currentMode->green_mask = pScrn->mask.green;
	currentMode->blue_mask = pScrn->mask.blue;
	currentMode->visualClass = (Bpp == 1) ? PseudoColor : TrueColor;
	currentMode->viewportWidth = pMode->HDisplay;
	currentMode->viewportHeight = pMode->VDisplay;
	currentMode->xViewportStep = 1;
	currentMode->yViewportStep = 1;
	currentMode->viewportFlags = DGA_FLIP_RETRACE;
	currentMode->offset = 0;
	currentMode->address = pI128->MemoryPtr;

	if(oneMore) { /* first one is narrow width */
	    currentMode->bytesPerScanline = ((pMode->HDisplay * Bpp) + 3) & ~3L;
	    currentMode->imageWidth = pMode->HDisplay;
	    currentMode->imageHeight =  pMode->VDisplay;
	    currentMode->pixmapWidth = currentMode->imageWidth;
	    currentMode->pixmapHeight = currentMode->imageHeight;
	    currentMode->maxViewportX = currentMode->imageWidth - 
					currentMode->viewportWidth;
	    /* this might need to get clamped to some maximum */
	    currentMode->maxViewportY = currentMode->imageHeight -
					currentMode->viewportHeight;
	    oneMore = FALSE;
	    goto SECOND_PASS;
	} else {
	    currentMode->bytesPerScanline = 
			((pScrn->displayWidth * Bpp) + 3) & ~3L;
	    currentMode->imageWidth = pScrn->displayWidth;
	    currentMode->imageHeight =  pMode->VDisplay;
	    currentMode->pixmapWidth = currentMode->imageWidth;
	    currentMode->pixmapHeight = currentMode->imageHeight;
	    currentMode->maxViewportX = currentMode->imageWidth - 
					currentMode->viewportWidth;
	    /* this might need to get clamped to some maximum */
	    currentMode->maxViewportY = currentMode->imageHeight -
					currentMode->viewportHeight;
	}	    

	pMode = pMode->next;
	if(pMode == firstMode)
	   break;
   }

   pI128->numDGAModes = num;
   pI128->DGAModes = modes;

    return DGAInit(pScreen, &I128_DGAFuncs, modes, num);  
}


static Bool
I128_SetMode(
   ScrnInfoPtr pScrn,
   DGAModePtr pMode
){
   static int OldDisplayWidth[MAXSCREENS];
   int index = pScrn->pScreen->myNum;

   I128Ptr pI128 = I128PTR(pScrn);

   if(!pMode) { /* restore the original mode */
	/* put the ScreenParameters back */
	
	pScrn->displayWidth = OldDisplayWidth[index];
	
        I128SwitchMode(SWITCH_MODE_ARGS(pScrn, pScrn->currentMode));
	pI128->DGAactive = FALSE;
   } else {
	if(!pI128->DGAactive) {  /* save the old parameters */
	    OldDisplayWidth[index] = pScrn->displayWidth;

	    pI128->DGAactive = TRUE;
	}

	pScrn->displayWidth = pMode->bytesPerScanline / 
			      (pMode->bitsPerPixel >> 3);

        I128SwitchMode(SWITCH_MODE_ARGS(pScrn, pMode->mode));
   }
   
   return TRUE;
}



static int  
I128_GetViewport(
  ScrnInfoPtr pScrn
){
    I128Ptr pI128 = I128PTR(pScrn);

    return pI128->DGAViewportStatus;
}

static void 
I128_SetViewport(
   ScrnInfoPtr pScrn, 
   int x, int y, 
   int flags
){
   I128Ptr pI128 = I128PTR(pScrn);

   I128AdjustFrame(ADJUST_FRAME_ARGS(pScrn, x, y));
   pI128->DGAViewportStatus = 0;  /* I128AdjustFrame loops until finished */
}

#ifdef HAVE_XAA_H
static void 
I128_FillRect (
   ScrnInfoPtr pScrn, 
   int x, int y, int w, int h, 
   unsigned long color
){
    I128Ptr pI128 = I128PTR(pScrn);

    if(pI128->XaaInfoRec) {
	(*pI128->XaaInfoRec->SetupForSolidFill)(pScrn, color, GXcopy, ~0);
	(*pI128->XaaInfoRec->SubsequentSolidFillRect)(pScrn, x, y, w, h);
	SET_SYNC_FLAG(pI128->XaaInfoRec);
    }
}

static void 
I128_BlitRect(
   ScrnInfoPtr pScrn, 
   int srcx, int srcy, 
   int w, int h, 
   int dstx, int dsty
){
    I128Ptr pI128 = I128PTR(pScrn);

    if(pI128->XaaInfoRec) {
	int xdir = ((srcx < dstx) && (srcy == dsty)) ? -1 : 1;
	int ydir = (srcy < dsty) ? -1 : 1;

	(*pI128->XaaInfoRec->SetupForScreenToScreenCopy)(
		pScrn, xdir, ydir, GXcopy, ~0, -1);
	(*pI128->XaaInfoRec->SubsequentScreenToScreenCopy)(
		pScrn, srcx, srcy, dstx, dsty, w, h);
	SET_SYNC_FLAG(pI128->XaaInfoRec);
    }
}

#if 0
static void 
I128_BlitTransRect(
   ScrnInfoPtr pScrn, 
   int srcx, int srcy, 
   int w, int h, 
   int dstx, int dsty,
   unsigned long color
){
    I128Ptr pI128 = I128PTR(pScrn);

    if(pI128->XaaInfoRec) {
	int xdir = ((srcx < dstx) && (srcy == dsty)) ? -1 : 1;
	int ydir = (srcy < dsty) ? -1 : 1;

	(*pI128->XaaInfoRec->SetupForScreenToScreenCopy)(
		pScrn, xdir, ydir, GXcopy, ~0, color);
	(*pI128->XaaInfoRec->SubsequentScreenToScreenCopy)(
		pScrn, srcx, srcy, dstx, dsty, w, h);
	SET_SYNC_FLAG(pI128->XaaInfoRec);
    }
}
#endif
#endif
static Bool 
I128_OpenFramebuffer(
   ScrnInfoPtr pScrn, 
   char **name,
   unsigned char **mem,
   int *size,
   int *offset,
   int *flags
){
    I128Ptr pI128 = I128PTR(pScrn);
    unsigned long FbAddress = PCI_REGION_BASE(pI128->PciInfo, 0, REGION_MEM) & 0xFFC00000;

    *name = NULL; 		/* no special device */
    *mem = (unsigned char*)FbAddress;
    *size = pI128->MemorySize*1024;
    *offset = 0;
    *flags = DGA_NEED_ROOT;

    return TRUE;
}
