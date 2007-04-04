
/**************************************************************************

Copyright 1998-1999 Precision Insight, Inc., Cedar Park, Texas.
Copyright © 2002 David Dawes
All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sub license, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/
/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/i810/i830_cursor.c,v 1.6 2002/12/18 15:49:01 dawes Exp $ */

/*
 * Reformatted with GNU indent (2.2.8), using the following options:
 *
 *    -bad -bap -c41 -cd0 -ncdb -ci6 -cli0 -cp0 -ncs -d0 -di3 -i3 -ip3 -l78
 *    -lp -npcs -psl -sob -ss -br -ce -sc -hnl
 *
 * This provides a good match with the original i810 code and preferred
 * XFree86 formatting conventions.
 *
 * When editing this driver, please follow the existing formatting, and edit
 * with <TAB> characters expanded at 8-column intervals.
 */

/*
 * Authors:
 *   Keith Whitwell <keith@tungstengraphics.com>
 *   David Dawes <dawes@xfree86.org>
 *
 * Updated for Dual Head capabilities:
 *   Alan Hourihane <alanh@tungstengraphics.com>
 *
 * Add ARGB HW cursor support:
 *   Alan Hourihane <alanh@tungstengraphics.com>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86_ansic.h"
#include "compiler.h"

#include "xf86fbman.h"

#include "i830.h"

static void I830LoadCursorImage(ScrnInfoPtr pScrn, unsigned char *src);
static void I830ShowCursor(ScrnInfoPtr pScrn);
static void I830HideCursor(ScrnInfoPtr pScrn);
static void I830SetCursorColors(ScrnInfoPtr pScrn, int bg, int fb);
static void I830SetCursorPosition(ScrnInfoPtr pScrn, int x, int y);
static Bool I830UseHWCursor(ScreenPtr pScrn, CursorPtr pCurs);
#ifdef ARGB_CURSOR
static void I830LoadCursorARGB(ScrnInfoPtr pScrn, CursorPtr pCurs);
static Bool I830UseHWCursorARGB(ScreenPtr pScrn, CursorPtr pCurs);
#endif

void
I830InitHWCursor(ScrnInfoPtr pScrn)
{
   I830Ptr pI830 = I830PTR(pScrn);
   CARD32 temp;

   DPRINTF(PFX, "I830InitHWCursor\n");
   /* Initialise the HW cursor registers, leaving the cursor hidden. */
   if (IS_MOBILE(pI830) || IS_I915G(pI830) || IS_I945G(pI830)) {
      temp = INREG(CURSOR_A_CONTROL);
      temp &= ~(CURSOR_MODE | MCURSOR_GAMMA_ENABLE | MCURSOR_MEM_TYPE_LOCAL |
		MCURSOR_PIPE_SELECT);
      temp |= CURSOR_MODE_DISABLE;
      temp |= (pI830->pipe << 28);
      /* Need to set control, then address. */
      OUTREG(CURSOR_A_CONTROL, temp);
      if (pI830->CursorIsARGB)
         OUTREG(CURSOR_A_BASE, pI830->CursorMemARGB->Physical);
      else
         OUTREG(CURSOR_A_BASE, pI830->CursorMem->Physical);
      if (pI830->Clone) {
         temp &= ~MCURSOR_PIPE_SELECT;
         temp |= (!pI830->pipe << 28);
         OUTREG(CURSOR_B_CONTROL, temp);
         if (pI830->CursorIsARGB)
            OUTREG(CURSOR_B_BASE, pI830->CursorMemARGB->Physical);
         else
            OUTREG(CURSOR_B_BASE, pI830->CursorMem->Physical);
      }
   } else {
      temp = INREG(CURSOR_CONTROL);
      temp &= ~(CURSOR_FORMAT_MASK | CURSOR_GAMMA_ENABLE |
		CURSOR_ENABLE  | CURSOR_STRIDE_MASK);
      temp |= (CURSOR_FORMAT_3C);
      /* This initialises the format and leave the cursor disabled. */
      OUTREG(CURSOR_CONTROL, temp);
      /* Need to set address and size after disabling. */
      if (pI830->CursorIsARGB)
         OUTREG(CURSOR_BASEADDR, pI830->CursorMemARGB->Start);
      else
         OUTREG(CURSOR_BASEADDR, pI830->CursorMem->Start);
      temp = ((I810_CURSOR_X & CURSOR_SIZE_MASK) << CURSOR_SIZE_HSHIFT) |
	     ((I810_CURSOR_Y & CURSOR_SIZE_MASK) << CURSOR_SIZE_VSHIFT);
      OUTREG(CURSOR_SIZE, temp);
   }
}

Bool
I830CursorInit(ScreenPtr pScreen)
{
   ScrnInfoPtr pScrn;
   I830Ptr pI830;
   xf86CursorInfoPtr infoPtr;

   DPRINTF(PFX, "I830CursorInit\n");
   pScrn = xf86Screens[pScreen->myNum];
   pI830 = I830PTR(pScrn);
   pI830->CursorInfoRec = infoPtr = xf86CreateCursorInfoRec();
   if (!infoPtr)
      return FALSE;

   infoPtr->MaxWidth = 64;
   infoPtr->MaxHeight = 64;
   infoPtr->Flags = (HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
		     HARDWARE_CURSOR_BIT_ORDER_MSBFIRST |
		     HARDWARE_CURSOR_INVERT_MASK |
		     HARDWARE_CURSOR_SWAP_SOURCE_AND_MASK |
		     HARDWARE_CURSOR_AND_SOURCE_WITH_MASK |
		     HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_64 | 0);

   infoPtr->SetCursorColors = I830SetCursorColors;
   infoPtr->SetCursorPosition = I830SetCursorPosition;
   infoPtr->LoadCursorImage = I830LoadCursorImage;
   infoPtr->HideCursor = I830HideCursor;
   infoPtr->ShowCursor = I830ShowCursor;
   infoPtr->UseHWCursor = I830UseHWCursor;

#ifdef ARGB_CURSOR
   pI830->CursorIsARGB = FALSE;

   if (pI830->CursorMemARGB->Start) {
      /* Use ARGB if we were able to allocate the 16kb needed */
      infoPtr->UseHWCursorARGB = I830UseHWCursorARGB;
      infoPtr->LoadCursorARGB = I830LoadCursorARGB;
   }
#endif

   if (pI830->CursorNeedsPhysical && !pI830->CursorMem->Physical) 
      return FALSE;

   I830HideCursor(pScrn);

   return xf86InitCursor(pScreen, infoPtr);
}

static Bool
I830UseHWCursor(ScreenPtr pScreen, CursorPtr pCurs)
{
   ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
   I830Ptr pI830 = I830PTR(pScrn);

   DPRINTF(PFX, "I830UseHWCursor\n");
   if (pI830->CursorNeedsPhysical && !pI830->CursorMem->Physical) 
      return FALSE;

   return TRUE;
}

static void
I830LoadCursorImage(ScrnInfoPtr pScrn, unsigned char *src)
{
   I830Ptr pI830 = I830PTR(pScrn);
   CARD8 *pcurs = (CARD8 *) (pI830->FbBase + pI830->CursorMem->Start);
   int x, y;

   DPRINTF(PFX, "I830LoadCursorImage\n");

#ifdef ARGB_CURSOR
   pI830->CursorIsARGB = FALSE;
#endif

   for (y = 0; y < 64; y++) {
      for (x = 0; x < 64 / 4; x++) {
	 *pcurs++ = *src++;
      }
   }
}

#ifdef ARGB_CURSOR
#include "cursorstr.h"

static Bool I830UseHWCursorARGB (ScreenPtr pScreen, CursorPtr pCurs)
{
   ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
   I830Ptr pI830 = I830PTR(pScrn);

   DPRINTF(PFX, "I830UseHWCursorARGB\n");
   if (pScrn->bitsPerPixel == 8)
      return FALSE;

   if (pI830->CursorNeedsPhysical && !pI830->CursorMemARGB->Physical) 
      return FALSE;

   if (pCurs->bits->height <= 64 && pCurs->bits->width <= 64) 
	return TRUE;

   return FALSE;
}

static void I830LoadCursorARGB (ScrnInfoPtr pScrn, CursorPtr pCurs)
{
   I830Ptr pI830 = I830PTR(pScrn);
   CARD32 *dst = (CARD32 *) (pI830->FbBase + pI830->CursorMemARGB->Start);
   CARD32 *image = (CARD32 *)pCurs->bits->argb;
   int x, y, w, h;

   DPRINTF(PFX, "I830LoadCursorARGB\n");

   if (!image)
	return;	/* XXX can't happen */
    
#ifdef ARGB_CURSOR
   pI830->CursorIsARGB = TRUE;
#endif

   w = pCurs->bits->width;
   h = pCurs->bits->height;

   for(y = 0; y < h; y++) {
      for(x = 0; x < w; x++)
          *dst++ = *image++;
      for(; x < 64; x++)
          *dst++ = 0;
   }

   for(; y < 64; y++) {
      for(x = 0; x < 64; x++)
          *dst++ = 0;
   }
}
#endif

static void
I830SetCursorPosition(ScrnInfoPtr pScrn, int x, int y)
{
   I830Ptr pI830 = I830PTR(pScrn);
   CARD32 temp = 0;
   static Bool outsideViewport = FALSE;
   Bool hide = FALSE, show = FALSE;

   /*
    * There is a screen display problem when the cursor position is set
    * wholely outside of the viewport.  We trap that here, turning the
    * cursor off when that happens, and back on when it comes back into
    * the viewport.
    */
   if (x >= pScrn->currentMode->HDisplay ||
       y >= pScrn->currentMode->VDisplay ||
       x <= -I810_CURSOR_X || y <= -I810_CURSOR_Y) {
      hide = TRUE;
      outsideViewport = TRUE;
   } else if (outsideViewport) {
      show = TRUE;
      outsideViewport = FALSE;
   }

   if (x < 0) {
      temp |= (CURSOR_POS_SIGN << CURSOR_X_SHIFT);
      x = -x;
   }
   if (y < 0) {
      temp |= (CURSOR_POS_SIGN << CURSOR_Y_SHIFT);
      y = -y;
   }
   temp |= ((x & CURSOR_POS_MASK) << CURSOR_X_SHIFT);
   temp |= ((y & CURSOR_POS_MASK) << CURSOR_Y_SHIFT);

   OUTREG(CURSOR_A_POSITION, temp);
   if (pI830->Clone)
      OUTREG(CURSOR_B_POSITION, temp);

   if (pI830->cursorOn) {
      if (hide)
	 pI830->CursorInfoRec->HideCursor(pScrn);
      else if (show)
	 pI830->CursorInfoRec->ShowCursor(pScrn);
      pI830->cursorOn = TRUE;
   }

   /* have to upload the base for the new position */
   if (IS_I915G(pI830) || IS_I915GM(pI830) || IS_I945G(pI830)) {
      if (pI830->CursorIsARGB)
         OUTREG(CURSOR_A_BASE, pI830->CursorMemARGB->Physical);
      else
         OUTREG(CURSOR_A_BASE, pI830->CursorMem->Physical);
      if (pI830->Clone) {
         if (pI830->CursorIsARGB)
            OUTREG(CURSOR_B_BASE, pI830->CursorMemARGB->Physical);
         else
            OUTREG(CURSOR_B_BASE, pI830->CursorMem->Physical);
      }
   }
}

static void
I830ShowCursor(ScrnInfoPtr pScrn)
{
   I830Ptr pI830 = I830PTR(pScrn);
   CARD32 temp;

   DPRINTF(PFX, "I830ShowCursor\n");
   DPRINTF(PFX,
	   "Value of CursorMem->Physical is %x, "
	   " Value of CursorMem->Start is %x ",
	   pI830->CursorMem->Physical, pI830->CursorMem->Start);
   DPRINTF(PFX,
	   "Value of CursorMemARGB->Physical is %x, "
	   " Value of CursorMemARGB->Start is %x ",
	   pI830->CursorMemARGB->Physical, pI830->CursorMemARGB->Start);

   pI830->cursorOn = TRUE;
   if (IS_MOBILE(pI830) || IS_I915G(pI830) || IS_I945G(pI830)) {
      temp = INREG(CURSOR_A_CONTROL);
      temp &= ~(CURSOR_MODE | MCURSOR_PIPE_SELECT);
      if (pI830->CursorIsARGB)
         temp |= CURSOR_MODE_64_ARGB_AX;
      else
         temp |= CURSOR_MODE_64_4C_AX;
      temp |= (pI830->pipe << 28); /* Connect to correct pipe */
      /* Need to set mode, then address. */
      OUTREG(CURSOR_A_CONTROL, temp);
      if (pI830->CursorIsARGB)
         OUTREG(CURSOR_A_BASE, pI830->CursorMemARGB->Physical);
      else
         OUTREG(CURSOR_A_BASE, pI830->CursorMem->Physical);
      if (pI830->Clone) {
         temp &= ~MCURSOR_PIPE_SELECT;
         temp |= (!pI830->pipe << 28);
         OUTREG(CURSOR_B_CONTROL, temp);
         if (pI830->CursorIsARGB)
            OUTREG(CURSOR_B_BASE, pI830->CursorMemARGB->Physical);
         else
            OUTREG(CURSOR_B_BASE, pI830->CursorMem->Physical);
      }
   } else {
      temp = INREG(CURSOR_CONTROL);
      temp &= ~(CURSOR_FORMAT_MASK);
      temp |= CURSOR_ENABLE;
      if (pI830->CursorIsARGB)
         temp |= CURSOR_FORMAT_ARGB;
      else 
         temp |= CURSOR_FORMAT_3C;
      OUTREG(CURSOR_CONTROL, temp);
      if (pI830->CursorIsARGB)
         OUTREG(CURSOR_BASEADDR, pI830->CursorMemARGB->Start);
      else
         OUTREG(CURSOR_BASEADDR, pI830->CursorMem->Start);
   }
}

static void
I830HideCursor(ScrnInfoPtr pScrn)
{
   CARD32 temp;
   I830Ptr pI830 = I830PTR(pScrn);

   DPRINTF(PFX, "I830HideCursor\n");

   pI830->cursorOn = FALSE;
   if (IS_MOBILE(pI830) || IS_I915G(pI830) || IS_I945G(pI830)) {
      temp = INREG(CURSOR_A_CONTROL);
      temp &= ~CURSOR_MODE;
      temp |= CURSOR_MODE_DISABLE;
      OUTREG(CURSOR_A_CONTROL, temp);
      /* This is needed to flush the above change. */
      if (pI830->CursorIsARGB)
         OUTREG(CURSOR_A_BASE, pI830->CursorMemARGB->Physical);
      else
         OUTREG(CURSOR_A_BASE, pI830->CursorMem->Physical);
      if (pI830->Clone) {
         OUTREG(CURSOR_B_CONTROL, temp);
         if (pI830->CursorIsARGB)
            OUTREG(CURSOR_B_BASE, pI830->CursorMemARGB->Physical);
         else
            OUTREG(CURSOR_B_BASE, pI830->CursorMem->Physical);
      }
   } else {
      temp = INREG(CURSOR_CONTROL);
      temp &= ~CURSOR_ENABLE;
      OUTREG(CURSOR_CONTROL, temp);
   }
}

static void
I830SetCursorColors(ScrnInfoPtr pScrn, int bg, int fg)
{
   I830Ptr pI830 = I830PTR(pScrn);

#ifdef ARGB_CURSOR
    /* Don't recolour cursors set with SetCursorARGB. */
    if (pI830->CursorIsARGB)
       return;
#endif

   DPRINTF(PFX, "I830SetCursorColors\n");

   OUTREG(CURSOR_A_PALETTE0, bg & 0x00ffffff);
   OUTREG(CURSOR_A_PALETTE1, fg & 0x00ffffff);
   OUTREG(CURSOR_A_PALETTE2, fg & 0x00ffffff);
   OUTREG(CURSOR_A_PALETTE3, bg & 0x00ffffff);
   if (pI830->Clone) {
      OUTREG(CURSOR_B_PALETTE0, bg & 0x00ffffff);
      OUTREG(CURSOR_B_PALETTE1, fg & 0x00ffffff);
      OUTREG(CURSOR_B_PALETTE2, fg & 0x00ffffff);
      OUTREG(CURSOR_B_PALETTE3, bg & 0x00ffffff);
   }
}
