#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "nv_local.h"
#include "nv_include.h"
#include "nv_type.h"
#include "nv_proto.h"
#ifdef HAVE_XAA_H
#include "xaalocal.h"
#endif
#include "dgaproc.h"


static Bool NV_OpenFramebuffer(ScrnInfoPtr, char **, unsigned char **, 
					int *, int *, int *);
static Bool NV_SetMode(ScrnInfoPtr, DGAModePtr);
static int  NV_GetViewport(ScrnInfoPtr);
static void NV_SetViewport(ScrnInfoPtr, int, int, int);
#ifdef HAVE_XAA_H
static void NV_FillRect(ScrnInfoPtr, int, int, int, int, unsigned long);
static void NV_BlitRect(ScrnInfoPtr, int, int, int, int, int, int);
static void NV_BlitTransRect(ScrnInfoPtr, int, int, int, int, int, int, 
					unsigned long);
#endif

static
DGAFunctionRec NV_DGAFuncs = {
   NV_OpenFramebuffer,
   NULL,
   NV_SetMode,
   NV_SetViewport,
   NV_GetViewport,
   NVSync,
#ifdef HAVE_XAA_H
   NV_FillRect,
   NV_BlitRect,
   NV_BlitTransRect
#else
   NULL, NULL, NULL
#endif
};



static DGAModePtr
NVSetupDGAMode(
   ScrnInfoPtr pScrn,
   DGAModePtr modes,
   int *num,
   int bitsPerPixel,
   int depth,
   Bool pixmap,
   int secondPitch,
   unsigned long red,
   unsigned long green,
   unsigned long blue,
   short visualClass
){
   DisplayModePtr firstMode, pMode;
   NVPtr pNv = NVPTR(pScrn);
   DGAModePtr mode, newmodes;
   int size, pitch, Bpp = bitsPerPixel >> 3;

SECOND_PASS:

   pMode = firstMode = pScrn->modes;

   while(1) {

	pitch = (pMode->HDisplay + 31) & ~31;
	size = pitch * Bpp * pMode->VDisplay;

	if((!secondPitch || (pitch != secondPitch)) &&
		(size <= pNv->ScratchBufferStart)) {

	    if(secondPitch)
		pitch = secondPitch; 

	    if(!(newmodes = realloc(modes, (*num + 1) * sizeof(DGAModeRec))))
		break;

	    modes = newmodes;
	    mode = modes + *num;

	    mode->mode = pMode;
	    mode->flags = DGA_CONCURRENT_ACCESS;

	    if(pixmap)
		mode->flags |= DGA_PIXMAP_AVAILABLE;
#ifdef HAVE_XAA_H
	    if(!pNv->NoAccel)
		mode->flags |= DGA_FILL_RECT | DGA_BLIT_RECT;
#endif
	    if(pMode->Flags & V_DBLSCAN)
		mode->flags |= DGA_DOUBLESCAN;
	    if(pMode->Flags & V_INTERLACE)
		mode->flags |= DGA_INTERLACED;
	    mode->byteOrder = pScrn->imageByteOrder;
	    mode->depth = depth;
	    mode->bitsPerPixel = bitsPerPixel;
	    mode->red_mask = red;
	    mode->green_mask = green;
	    mode->blue_mask = blue;
	    mode->visualClass = visualClass;
	    mode->viewportWidth = pMode->HDisplay;
	    mode->viewportHeight = pMode->VDisplay;
	    mode->xViewportStep = 4 / Bpp;
	    mode->yViewportStep = 1;
	    mode->viewportFlags = DGA_FLIP_RETRACE;
	    mode->offset = 0;
	    mode->address = pNv->FbStart;
	    mode->bytesPerScanline = pitch * Bpp;
	    mode->imageWidth = pitch;
	    mode->imageHeight =  pNv->ScratchBufferStart / 
                                 mode->bytesPerScanline; 
	    mode->pixmapWidth = mode->imageWidth;
	    mode->pixmapHeight = mode->imageHeight;
	    mode->maxViewportX = mode->imageWidth - mode->viewportWidth;
	    mode->maxViewportY = mode->imageHeight - mode->viewportHeight;
	    (*num)++;
	}

	pMode = pMode->next;
	if(pMode == firstMode)
	   break;
    }

    if(secondPitch) {
	secondPitch = 0;
	goto SECOND_PASS;
    }

    return modes;
}


Bool
NVDGAInit(ScreenPtr pScreen)
{   
   ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
   NVPtr pNv = NVPTR(pScrn);
   DGAModePtr modes = NULL;
   int num = 0;

   /* 8 */
   modes = NVSetupDGAMode (pScrn, modes, &num, 8, 8, 
		(pScrn->bitsPerPixel == 8),
		(pScrn->bitsPerPixel != 8) ? 0 : pScrn->displayWidth,
		0, 0, 0, PseudoColor);

   /* 15 */
   modes = NVSetupDGAMode (pScrn, modes, &num, 16, 15, 
		(pScrn->bitsPerPixel == 16),
		(pScrn->depth != 15) ? 0 : pScrn->displayWidth,
		0x7c00, 0x03e0, 0x001f, TrueColor);

   /* 16 */
   modes = NVSetupDGAMode (pScrn, modes, &num, 16, 16, 
		(pScrn->bitsPerPixel == 16),
		(pScrn->depth != 16) ? 0 : pScrn->displayWidth,
		0xf800, 0x07e0, 0x001f, TrueColor);

   /* 32 */
   modes = NVSetupDGAMode (pScrn, modes, &num, 32, 24, 
		(pScrn->bitsPerPixel == 32),
		(pScrn->bitsPerPixel != 32) ? 0 : pScrn->displayWidth,
		0xff0000, 0x00ff00, 0x0000ff, TrueColor);

   pNv->numDGAModes = num;
   pNv->DGAModes = modes;

   return DGAInit(pScreen, &NV_DGAFuncs, modes, num);  
}


static int 
BitsSet(unsigned long data)
{
   unsigned long mask;
   int set = 0;

   for(mask = 1; mask; mask <<= 1)
        if(mask & data) set++;   

   return set;
}

static Bool
NV_SetMode(
   ScrnInfoPtr pScrn,
   DGAModePtr pMode
){
   static NVFBLayout SavedLayouts[MAXSCREENS];
   int index = pScrn->pScreen->myNum;

   NVPtr pNv = NVPTR(pScrn);

   if(!pMode) { /* restore the original mode */
      if(pNv->DGAactive)
        memcpy(&pNv->CurrentLayout, &SavedLayouts[index], sizeof(NVFBLayout));
                
      pScrn->currentMode = pNv->CurrentLayout.mode;
      NVSwitchMode(SWITCH_MODE_ARGS(pScrn, pScrn->currentMode));
      NVAdjustFrame(ADJUST_FRAME_ARGS(pScrn, pScrn->frameX0, pScrn->frameY0));
      pNv->DGAactive = FALSE;
   } else {
      if(!pNv->DGAactive) {  /* save the old parameters */
	memcpy(&SavedLayouts[index], &pNv->CurrentLayout, sizeof(NVFBLayout));
	pNv->DGAactive = TRUE;
      }

      /* update CurrentLayout */
      pNv->CurrentLayout.bitsPerPixel = pMode->bitsPerPixel;
      pNv->CurrentLayout.depth = pMode->depth;
      pNv->CurrentLayout.displayWidth = pMode->bytesPerScanline / 
                              (pMode->bitsPerPixel >> 3);
      pNv->CurrentLayout.weight.red = BitsSet(pMode->red_mask);
      pNv->CurrentLayout.weight.green = BitsSet(pMode->green_mask);
      pNv->CurrentLayout.weight.blue = BitsSet(pMode->blue_mask);
      /* NVModeInit() will set the mode field */
      NVSwitchMode(SWITCH_MODE_ARGS(pScrn, pMode->mode));
   }
   
   return TRUE;
}



static int  
NV_GetViewport(
  ScrnInfoPtr pScrn
){
    NVPtr pNv = NVPTR(pScrn);

    return pNv->DGAViewportStatus;
}

static void 
NV_SetViewport(
   ScrnInfoPtr pScrn, 
   int x, int y, 
   int flags
){
   NVPtr pNv = NVPTR(pScrn);

   NVAdjustFrame(ADJUST_FRAME_ARGS(pScrn, x, y));

   while(VGA_RD08(pNv->PCIO, 0x3da) & 0x08);
   while(!(VGA_RD08(pNv->PCIO, 0x3da) & 0x08));

   pNv->DGAViewportStatus = 0;  
}

#ifdef HAVE_XAA_H
static void 
NV_FillRect (
   ScrnInfoPtr pScrn, 
   int x, int y, int w, int h, 
   unsigned long color
){
    NVPtr pNv = NVPTR(pScrn);

    if(!pNv->AccelInfoRec) return;

    (*pNv->AccelInfoRec->SetupForSolidFill)(pScrn, color, GXcopy, ~0);
    (*pNv->AccelInfoRec->SubsequentSolidFillRect)(pScrn, x, y, w, h);

    SET_SYNC_FLAG(pNv->AccelInfoRec);
}

static void 
NV_BlitRect(
   ScrnInfoPtr pScrn, 
   int srcx, int srcy, 
   int w, int h, 
   int dstx, int dsty
){
    NVPtr pNv = NVPTR(pScrn);
    int xdir = ((srcx < dstx) && (srcy == dsty)) ? -1 : 1;
    int ydir = (srcy < dsty) ? -1 : 1;

    if(!pNv->AccelInfoRec) return;

    (*pNv->AccelInfoRec->SetupForScreenToScreenCopy)(
		pScrn, xdir, ydir, GXcopy, ~0, -1);

    (*pNv->AccelInfoRec->SubsequentScreenToScreenCopy)(
		pScrn, srcx, srcy, dstx, dsty, w, h);

    SET_SYNC_FLAG(pNv->AccelInfoRec);
}


static void 
NV_BlitTransRect(
   ScrnInfoPtr pScrn, 
   int srcx, int srcy, 
   int w, int h, 
   int dstx, int dsty,
   unsigned long color
){
   /* not implemented */
}
#endif

static Bool 
NV_OpenFramebuffer(
   ScrnInfoPtr pScrn, 
   char **name,
   unsigned char **mem,
   int *size,
   int *offset,
   int *flags
){
    NVPtr pNv = NVPTR(pScrn);

    *name = NULL; 		/* no special device */
    *mem = (unsigned char*)pNv->FbAddress;
    *size = pNv->FbMapSize;
    *offset = 0;
    *flags = DGA_NEED_ROOT;

    return TRUE;
}
