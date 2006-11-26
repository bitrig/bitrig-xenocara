/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/ati/radeon_dri.c,v 1.39 2003/11/06 18:38:00 tsi Exp $ */
/*
 * Copyright 2000 ATI Technologies Inc., Markham, Ontario,
 *                VA Linux Systems Inc., Fremont, California.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL ATI, VA LINUX SYSTEMS AND/OR
 * THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*
 * Authors:
 *   Kevin E. Martin <martin@xfree86.org>
 *   Rickard E. Faith <faith@valinux.com>
 *   Gareth Hughes <gareth@valinux.com>
 *
 */

#include <string.h>
#include <stdio.h>

				/* Driver data structures */
#include "radeon.h"
#include "radeon_video.h"
#include "radeon_reg.h"
#include "radeon_macros.h"
#include "radeon_dri.h"
#include "radeon_version.h"

				/* X and server generic header files */
#include "xf86.h"
#include "xf86PciInfo.h"
#include "windowstr.h"


#include "shadowfb.h"
				/* GLX/DRI/DRM definitions */
#define _XF86DRI_SERVER_
#include "GL/glxtokens.h"
#include "sarea.h"
#include "radeon_sarea.h"

static size_t radeon_drm_page_size;


static void RADEONDRITransitionTo2d(ScreenPtr pScreen);
static void RADEONDRITransitionTo3d(ScreenPtr pScreen);
static void RADEONDRITransitionMultiToSingle3d(ScreenPtr pScreen);
static void RADEONDRITransitionSingleToMulti3d(ScreenPtr pScreen);

#ifdef USE_XAA
static void RADEONDRIRefreshArea(ScrnInfoPtr pScrn, int num, BoxPtr pbox);
#endif

/* Initialize the visual configs that are supported by the hardware.
 * These are combined with the visual configs that the indirect
 * rendering core supports, and the intersection is exported to the
 * client.
 */
static Bool RADEONInitVisualConfigs(ScreenPtr pScreen)
{
    ScrnInfoPtr          pScrn             = xf86Screens[pScreen->myNum];
    RADEONInfoPtr        info              = RADEONPTR(pScrn);
    int                  numConfigs        = 0;
    __GLXvisualConfig   *pConfigs          = 0;
    RADEONConfigPrivPtr  pRADEONConfigs    = 0;
    RADEONConfigPrivPtr *pRADEONConfigPtrs = 0;
    int                  i, accum, stencil, db, use_db;

    use_db = !info->noBackBuffer ? 1 : 0;

    switch (info->CurrentLayout.pixel_code) {
    case 8:  /* 8bpp mode is not support */
    case 15: /* FIXME */
    case 24: /* FIXME */
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[dri] RADEONInitVisualConfigs failed "
		   "(depth %d not supported).  "
		   "Disabling DRI.\n", info->CurrentLayout.pixel_code);
	return FALSE;

#define RADEON_USE_ACCUM   1
#define RADEON_USE_STENCIL 1

    case 16:
	numConfigs = 1;
	if (RADEON_USE_ACCUM)   numConfigs *= 2;
	if (RADEON_USE_STENCIL) numConfigs *= 2;
	if (use_db)             numConfigs *= 2;

	if (!(pConfigs
	      = (__GLXvisualConfig *)xcalloc(sizeof(__GLXvisualConfig),
					     numConfigs))) {
	    return FALSE;
	}
	if (!(pRADEONConfigs
	      = (RADEONConfigPrivPtr)xcalloc(sizeof(RADEONConfigPrivRec),
					     numConfigs))) {
	    xfree(pConfigs);
	    return FALSE;
	}
	if (!(pRADEONConfigPtrs
	      = (RADEONConfigPrivPtr *)xcalloc(sizeof(RADEONConfigPrivPtr),
					       numConfigs))) {
	    xfree(pConfigs);
	    xfree(pRADEONConfigs);
	    return FALSE;
	}

	i = 0;
	for (db = use_db; db >= 0; db--) {
	  for (accum = 0; accum <= RADEON_USE_ACCUM; accum++) {
	    for (stencil = 0; stencil <= RADEON_USE_STENCIL; stencil++) {
		pRADEONConfigPtrs[i] = &pRADEONConfigs[i];

		pConfigs[i].vid                = (VisualID)(-1);
		pConfigs[i].class              = -1;
		pConfigs[i].rgba               = TRUE;
		pConfigs[i].redSize            = 5;
		pConfigs[i].greenSize          = 6;
		pConfigs[i].blueSize           = 5;
		pConfigs[i].alphaSize          = 0;
		pConfigs[i].redMask            = 0x0000F800;
		pConfigs[i].greenMask          = 0x000007E0;
		pConfigs[i].blueMask           = 0x0000001F;
		pConfigs[i].alphaMask          = 0x00000000;
		if (accum) { /* Simulated in software */
		    pConfigs[i].accumRedSize   = 16;
		    pConfigs[i].accumGreenSize = 16;
		    pConfigs[i].accumBlueSize  = 16;
		    pConfigs[i].accumAlphaSize = 0;
		} else {
		    pConfigs[i].accumRedSize   = 0;
		    pConfigs[i].accumGreenSize = 0;
		    pConfigs[i].accumBlueSize  = 0;
		    pConfigs[i].accumAlphaSize = 0;
		}
		if (db)
		    pConfigs[i].doubleBuffer   = TRUE;
		else
		    pConfigs[i].doubleBuffer   = FALSE;
		pConfigs[i].stereo             = FALSE;
		pConfigs[i].bufferSize         = 16;
		pConfigs[i].depthSize          = info->depthBits;
		if (pConfigs[i].depthSize == 24 ? (RADEON_USE_STENCIL - stencil)
						: stencil) {
		    pConfigs[i].stencilSize    = 8;
		} else {
		    pConfigs[i].stencilSize    = 0;
		}
		pConfigs[i].auxBuffers         = 0;
		pConfigs[i].level              = 0;
		if (accum ||
		    (pConfigs[i].stencilSize && pConfigs[i].depthSize == 16)) {
		   pConfigs[i].visualRating    = GLX_SLOW_CONFIG;
		} else {
		   pConfigs[i].visualRating    = GLX_NONE;
		}
		pConfigs[i].transparentPixel   = GLX_NONE;
		pConfigs[i].transparentRed     = 0;
		pConfigs[i].transparentGreen   = 0;
		pConfigs[i].transparentBlue    = 0;
		pConfigs[i].transparentAlpha   = 0;
		pConfigs[i].transparentIndex   = 0;
		i++;
	    }
	  }
	}
	break;

    case 32:
	numConfigs = 1;
	if (RADEON_USE_ACCUM)   numConfigs *= 2;
	if (RADEON_USE_STENCIL) numConfigs *= 2;
	if (use_db)             numConfigs *= 2;

	if (!(pConfigs
	      = (__GLXvisualConfig *)xcalloc(sizeof(__GLXvisualConfig),
					     numConfigs))) {
	    return FALSE;
	}
	if (!(pRADEONConfigs
	      = (RADEONConfigPrivPtr)xcalloc(sizeof(RADEONConfigPrivRec),
					     numConfigs))) {
	    xfree(pConfigs);
	    return FALSE;
	}
	if (!(pRADEONConfigPtrs
	      = (RADEONConfigPrivPtr *)xcalloc(sizeof(RADEONConfigPrivPtr),
					       numConfigs))) {
	    xfree(pConfigs);
	    xfree(pRADEONConfigs);
	    return FALSE;
	}

	i = 0;
	for (db = use_db; db >= 0; db--) {
	  for (accum = 0; accum <= RADEON_USE_ACCUM; accum++) {
	    for (stencil = 0; stencil <= RADEON_USE_STENCIL; stencil++) {
		pRADEONConfigPtrs[i] = &pRADEONConfigs[i];

		pConfigs[i].vid                = (VisualID)(-1);
		pConfigs[i].class              = -1;
		pConfigs[i].rgba               = TRUE;
		pConfigs[i].redSize            = 8;
		pConfigs[i].greenSize          = 8;
		pConfigs[i].blueSize           = 8;
		pConfigs[i].alphaSize          = 8;
		pConfigs[i].redMask            = 0x00FF0000;
		pConfigs[i].greenMask          = 0x0000FF00;
		pConfigs[i].blueMask           = 0x000000FF;
		pConfigs[i].alphaMask          = 0xFF000000;
		if (accum) { /* Simulated in software */
		    pConfigs[i].accumRedSize   = 16;
		    pConfigs[i].accumGreenSize = 16;
		    pConfigs[i].accumBlueSize  = 16;
		    pConfigs[i].accumAlphaSize = 16;
		} else {
		    pConfigs[i].accumRedSize   = 0;
		    pConfigs[i].accumGreenSize = 0;
		    pConfigs[i].accumBlueSize  = 0;
		    pConfigs[i].accumAlphaSize = 0;
		}
		if (db)
		    pConfigs[i].doubleBuffer   = TRUE;
		else
		    pConfigs[i].doubleBuffer   = FALSE;
		pConfigs[i].stereo             = FALSE;
		pConfigs[i].bufferSize         = 32;
		pConfigs[i].depthSize          = info->depthBits;
		if (pConfigs[i].depthSize == 24 ? (RADEON_USE_STENCIL - stencil)
						: stencil) {
		    pConfigs[i].stencilSize    = 8;
		} else {
		    pConfigs[i].stencilSize    = 0;
		}
		pConfigs[i].auxBuffers         = 0;
		pConfigs[i].level              = 0;
		if (accum ||
		    (pConfigs[i].stencilSize && pConfigs[i].depthSize == 16)) {
		   pConfigs[i].visualRating    = GLX_SLOW_CONFIG;
		} else {
		   pConfigs[i].visualRating    = GLX_NONE;
		}
		pConfigs[i].transparentPixel   = GLX_NONE;
		pConfigs[i].transparentRed     = 0;
		pConfigs[i].transparentGreen   = 0;
		pConfigs[i].transparentBlue    = 0;
		pConfigs[i].transparentAlpha   = 0;
		pConfigs[i].transparentIndex   = 0;
		i++;
	    }
	  }
	}
	break;
    }

    info->numVisualConfigs   = numConfigs;
    info->pVisualConfigs     = pConfigs;
    info->pVisualConfigsPriv = pRADEONConfigs;
    GlxSetVisualConfigs(numConfigs, pConfigs, (void**)pRADEONConfigPtrs);
    return TRUE;
}

/* Create the Radeon-specific context information */
static Bool RADEONCreateContext(ScreenPtr pScreen, VisualPtr visual,
				drm_context_t hwContext, void *pVisualConfigPriv,
				DRIContextType contextStore)
{
#ifdef PER_CONTEXT_SAREA
    ScrnInfoPtr          pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr        info  = RADEONPTR(pScrn);
    RADEONDRIContextPtr  ctx_info;

    ctx_info = (RADEONDRIContextPtr)contextStore;
    if (!ctx_info) return FALSE;

    if (drmAddMap(info->drmFD, 0,
		  info->perctx_sarea_size,
		  DRM_SHM,
		  DRM_REMOVABLE,
		  &ctx_info->sarea_handle) < 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "[dri] could not create private sarea for ctx id (%d)\n",
		   (int)hwContext);
	return FALSE;
    }

    if (drmAddContextPrivateMapping(info->drmFD, hwContext,
				    ctx_info->sarea_handle) < 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "[dri] could not associate private sarea to ctx id (%d)\n",
		   (int)hwContext);
	drmRmMap(info->drmFD, ctx_info->sarea_handle);
	return FALSE;
    }

    ctx_info->ctx_id = hwContext;
#endif
    return TRUE;
}

/* Destroy the Radeon-specific context information */
static void RADEONDestroyContext(ScreenPtr pScreen, drm_context_t hwContext,
				 DRIContextType contextStore)
{
#ifdef PER_CONTEXT_SAREA
    ScrnInfoPtr          pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr        info = RADEONPTR(pScrn);
    RADEONDRIContextPtr  ctx_info;

    ctx_info = (RADEONDRIContextPtr)contextStore;
    if (!ctx_info) return;

    if (drmRmMap(info->drmFD, ctx_info->sarea_handle) < 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "[dri] could not remove private sarea for ctx id (%d)\n",
		   (int)hwContext);
    }
#endif
}

/* Called when the X server is woken up to allow the last client's
 * context to be saved and the X server's context to be loaded.  This is
 * not necessary for the Radeon since the client detects when it's
 * context is not currently loaded and then load's it itself.  Since the
 * registers to start and stop the CP are privileged, only the X server
 * can start/stop the engine.
 */
static void RADEONEnterServer(ScreenPtr pScreen)
{
    ScrnInfoPtr    pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr  info  = RADEONPTR(pScrn);
    RADEONSAREAPrivPtr pSAREAPriv;


    RADEON_MARK_SYNC(info, pScrn);

    pSAREAPriv = DRIGetSAREAPrivate(pScrn->pScreen);
    if (pSAREAPriv->ctxOwner != DRIGetContext(pScrn->pScreen))
	info->XInited3D = FALSE;


    /* TODO: Fix this more elegantly.
     * Sometimes (especially with multiple DRI clients), this code
     * runs immediately after a DRI client issues a rendering command.
     *
     * The accel code regularly inserts WAIT_UNTIL_IDLE into the
     * command buffer that is sent with the indirect buffer below.
     * The accel code fails to set the 3D cache flush registers for
     * the R300 before sending WAIT_UNTIL_IDLE. Sending a cache flush
     * on these new registers is not necessary for pure 2D functionality,
     * but it *is* necessary after 3D operations.
     * Without the cache flushes before WAIT_UNTIL_IDLE, the R300 locks up.
     *
     * The CP_IDLE call into the DRM indirectly flushes all caches and
     * thus avoids the lockup problem, but the solution is far from ideal.
     * Better solutions could be:
     *  - always flush caches when entering the X server
     *  - track the type of rendering commands somewhere and issue
     *    cache flushes when they change
     * However, I don't feel confident enough with the control flow
     * inside the X server to implement either fix. -- nh
     */
    
    /* On my computer (Radeon Mobility M10)
       The fix below results in x11perf -shmput500 rate of 245.0/sec
       which is lower than 264.0/sec I get without it.
       
       Doing the same each time before indirect buffer is submitted
       results in x11perf -shmput500 rate of 225.0/sec.
       
       On the other hand, not using CP acceleration at all benchmarks
       at 144.0/sec.
      
       For now let us accept this as a lesser evil, especially as the
       DRM driver for R300 is still in flux.
       
       Once the code is more stable this should probably be moved into DRM driver.
    */ 
     
    if (info->ChipFamily>=CHIP_FAMILY_R300)
        drmCommandNone(info->drmFD, DRM_RADEON_CP_IDLE);


}

/* Called when the X server goes to sleep to allow the X server's
 * context to be saved and the last client's context to be loaded.  This
 * is not necessary for the Radeon since the client detects when it's
 * context is not currently loaded and then load's it itself.  Since the
 * registers to start and stop the CP are privileged, only the X server
 * can start/stop the engine.
 */
static void RADEONLeaveServer(ScreenPtr pScreen)
{
    ScrnInfoPtr    pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr  info  = RADEONPTR(pScrn);
    RING_LOCALS;

    /* The CP is always running, but if we've generated any CP commands
     * we must flush them to the kernel module now.
     */
    if (info->CPInUse) {
	RADEON_FLUSH_CACHE();
	RADEON_WAIT_UNTIL_IDLE();
	RADEONCPReleaseIndirect(pScrn);

	info->CPInUse = FALSE;
    }

#ifdef USE_EXA
    info->engineMode = EXA_ENGINEMODE_UNKNOWN;
#endif
}

/* Contexts can be swapped by the X server if necessary.  This callback
 * is currently only used to perform any functions necessary when
 * entering or leaving the X server, and in the future might not be
 * necessary.
 */
static void RADEONDRISwapContext(ScreenPtr pScreen, DRISyncType syncType,
				 DRIContextType oldContextType,
				 void *oldContext,
				 DRIContextType newContextType,
				 void *newContext)
{
    if ((syncType==DRI_3D_SYNC) && (oldContextType==DRI_2D_CONTEXT) &&
	(newContextType==DRI_2D_CONTEXT)) { /* Entering from Wakeup */
	RADEONEnterServer(pScreen);
    }

    if ((syncType==DRI_2D_SYNC) && (oldContextType==DRI_NO_CONTEXT) &&
	(newContextType==DRI_2D_CONTEXT)) { /* Exiting from Block Handler */
	RADEONLeaveServer(pScreen);
    }
}

#ifdef USE_XAA

/* The Radeon has depth tiling on all the time. Rely on surface regs to
 * translate the addresses (only works if allowColorTiling is true).
 */

/* 16-bit depth buffer functions */
#define WRITE_DEPTH16(_x, _y, d)					\
    *(CARD16 *)(pointer)(buf + 2*(_x + _y*info->frontPitch)) = (d)

#define READ_DEPTH16(d, _x, _y)						\
    (d) = *(CARD16 *)(pointer)(buf + 2*(_x + _y*info->frontPitch))

/* 32-bit depth buffer (stencil and depth simultaneously) functions */
#define WRITE_DEPTHSTENCIL32(_x, _y, d)					\
    *(CARD32 *)(pointer)(buf + 4*(_x + _y*info->frontPitch)) = (d)

#define READ_DEPTHSTENCIL32(d, _x, _y)					\
    (d) = *(CARD32 *)(pointer)(buf + 4*(_x + _y*info->frontPitch))

/* Screen to screen copy of data in the depth buffer */
static void RADEONScreenToScreenCopyDepth(ScrnInfoPtr pScrn,
					  int xa, int ya,
					  int xb, int yb,
					  int w, int h)
{
    RADEONInfoPtr  info = RADEONPTR(pScrn);
    unsigned char *buf  = info->FB + info->depthOffset;
    int            xstart, xend, xdir;
    int            ystart, yend, ydir;
    int            x, y, d;

    if (xa < xb) xdir = -1, xstart = w-1, xend = 0;
    else         xdir =  1, xstart = 0,   xend = w-1;

    if (ya < yb) ydir = -1, ystart = h-1, yend = 0;
    else         ydir =  1, ystart = 0,   yend = h-1;

    switch (pScrn->bitsPerPixel) {
    case 16:
	for (x = xstart; x != xend; x += xdir) {
	    for (y = ystart; y != yend; y += ydir) {
		READ_DEPTH16(d, xa+x, ya+y);
		WRITE_DEPTH16(xb+x, yb+y, d);
	    }
	}
	break;

    case 32:
	for (x = xstart; x != xend; x += xdir) {
	    for (y = ystart; y != yend; y += ydir) {
		READ_DEPTHSTENCIL32(d, xa+x, ya+y);
		WRITE_DEPTHSTENCIL32(xb+x, yb+y, d);
	    }
	}
	break;

    default:
	break;
    }
}

#endif /* USE_XAA */

/* Initialize the state of the back and depth buffers */
static void RADEONDRIInitBuffers(WindowPtr pWin, RegionPtr prgn, CARD32 indx)
{
   /* NOOP.  There's no need for the 2d driver to be clearing buffers
    * for the 3d client.  It knows how to do that on its own.
    */
}

/* Copy the back and depth buffers when the X server moves a window.
 *
 * This routine is a modified form of XAADoBitBlt with the calls to
 * ScreenToScreenBitBlt built in. My routine has the prgnSrc as source
 * instead of destination. My origin is upside down so the ydir cases
 * are reversed.
 */
static void RADEONDRIMoveBuffers(WindowPtr pParent, DDXPointRec ptOldOrg,
				 RegionPtr prgnSrc, CARD32 indx)
{
#ifdef USE_XAA
    ScreenPtr      pScreen  = pParent->drawable.pScreen;
    ScrnInfoPtr    pScrn    = xf86Screens[pScreen->myNum];
    RADEONInfoPtr  info     = RADEONPTR(pScrn);

    BoxPtr         pboxTmp, pboxNext, pboxBase;
    DDXPointPtr    pptTmp;
    int            xdir, ydir;

    int            screenwidth = pScrn->virtualX;
    int            screenheight = pScrn->virtualY;

    BoxPtr         pbox     = REGION_RECTS(prgnSrc);
    int            nbox     = REGION_NUM_RECTS(prgnSrc);

    BoxPtr         pboxNew1 = NULL;
    BoxPtr         pboxNew2 = NULL;
    DDXPointPtr    pptNew1  = NULL;
    DDXPointPtr    pptNew2  = NULL;
    DDXPointPtr    pptSrc   = &ptOldOrg;

    int            dx       = pParent->drawable.x - ptOldOrg.x;
    int            dy       = pParent->drawable.y - ptOldOrg.y;

    /* XXX: Fix in EXA case. */
    if (info->useEXA)
	return;

    /* If the copy will overlap in Y, reverse the order */
    if (dy > 0) {
	ydir = -1;

	if (nbox > 1) {
	    /* Keep ordering in each band, reverse order of bands */
	    pboxNew1 = (BoxPtr)ALLOCATE_LOCAL(sizeof(BoxRec)*nbox);
	    if (!pboxNew1) return;

	    pptNew1 = (DDXPointPtr)ALLOCATE_LOCAL(sizeof(DDXPointRec)*nbox);
	    if (!pptNew1) {
		DEALLOCATE_LOCAL(pboxNew1);
		return;
	    }

	    pboxBase = pboxNext = pbox+nbox-1;

	    while (pboxBase >= pbox) {
		while ((pboxNext >= pbox) && (pboxBase->y1 == pboxNext->y1))
		    pboxNext--;

		pboxTmp = pboxNext+1;
		pptTmp  = pptSrc + (pboxTmp - pbox);

		while (pboxTmp <= pboxBase) {
		    *pboxNew1++ = *pboxTmp++;
		    *pptNew1++  = *pptTmp++;
		}

		pboxBase = pboxNext;
	    }

	    pboxNew1 -= nbox;
	    pbox      = pboxNew1;
	    pptNew1  -= nbox;
	    pptSrc    = pptNew1;
	}
    } else {
	/* No changes required */
	ydir = 1;
    }

    /* If the regions will overlap in X, reverse the order */
    if (dx > 0) {
	xdir = -1;

	if (nbox > 1) {
	    /* reverse order of rects in each band */
	    pboxNew2 = (BoxPtr)ALLOCATE_LOCAL(sizeof(BoxRec)*nbox);
	    pptNew2  = (DDXPointPtr)ALLOCATE_LOCAL(sizeof(DDXPointRec)*nbox);

	    if (!pboxNew2 || !pptNew2) {
		DEALLOCATE_LOCAL(pptNew2);
		DEALLOCATE_LOCAL(pboxNew2);
		DEALLOCATE_LOCAL(pptNew1);
		DEALLOCATE_LOCAL(pboxNew1);
		return;
	    }

	    pboxBase = pboxNext = pbox;

	    while (pboxBase < pbox+nbox) {
		while ((pboxNext < pbox+nbox)
		       && (pboxNext->y1 == pboxBase->y1))
		    pboxNext++;

		pboxTmp = pboxNext;
		pptTmp  = pptSrc + (pboxTmp - pbox);

		while (pboxTmp != pboxBase) {
		    *pboxNew2++ = *--pboxTmp;
		    *pptNew2++  = *--pptTmp;
		}

		pboxBase = pboxNext;
	    }

	    pboxNew2 -= nbox;
	    pbox      = pboxNew2;
	    pptNew2  -= nbox;
	    pptSrc    = pptNew2;
	}
    } else {
	/* No changes are needed */
	xdir = 1;
    }

    /* pretty much a hack. */
    info->dst_pitch_offset = info->backPitchOffset;
    if (info->tilingEnabled)
       info->dst_pitch_offset |= RADEON_DST_TILE_MACRO;

    (*info->accel->SetupForScreenToScreenCopy)(pScrn, xdir, ydir, GXcopy,
					       (CARD32)(-1), -1);

    for (; nbox-- ; pbox++) {
	int  xa    = pbox->x1;
	int  ya    = pbox->y1;
	int  destx = xa + dx;
	int  desty = ya + dy;
	int  w     = pbox->x2 - xa + 1;
	int  h     = pbox->y2 - ya + 1;

	if (destx < 0)                xa -= destx, w += destx, destx = 0;
	if (desty < 0)                ya -= desty, h += desty, desty = 0;
	if (destx + w > screenwidth)  w = screenwidth  - destx;
	if (desty + h > screenheight) h = screenheight - desty;

	if (w <= 0) continue;
	if (h <= 0) continue;

	(*info->accel->SubsequentScreenToScreenCopy)(pScrn,
						     xa, ya,
						     destx, desty,
						     w, h);

	if (info->depthMoves) {
	    RADEONScreenToScreenCopyDepth(pScrn,
					  xa, ya,
					  destx, desty,
					  w, h);
	}
    }

    info->dst_pitch_offset = info->frontPitchOffset;;

    DEALLOCATE_LOCAL(pptNew2);
    DEALLOCATE_LOCAL(pboxNew2);
    DEALLOCATE_LOCAL(pptNew1);
    DEALLOCATE_LOCAL(pboxNew1);

    info->accel->NeedToSync = TRUE;
#endif /* USE_XAA */
}

static void RADEONDRIInitGARTValues(RADEONInfoPtr info)
{
    int            s, l;

    info->gartOffset = 0;

				/* Initialize the CP ring buffer data */
    info->ringStart       = info->gartOffset;
    info->ringMapSize     = info->ringSize*1024*1024 + radeon_drm_page_size;
    info->ringSizeLog2QW  = RADEONMinBits(info->ringSize*1024*1024/8)-1;

    info->ringReadOffset  = info->ringStart + info->ringMapSize;
    info->ringReadMapSize = radeon_drm_page_size;

				/* Reserve space for vertex/indirect buffers */
    info->bufStart        = info->ringReadOffset + info->ringReadMapSize;
    info->bufMapSize      = info->bufSize*1024*1024;

				/* Reserve the rest for GART textures */
    info->gartTexStart     = info->bufStart + info->bufMapSize;
    s = (info->gartSize*1024*1024 - info->gartTexStart);
    l = RADEONMinBits((s-1) / RADEON_NR_TEX_REGIONS);
    if (l < RADEON_LOG_TEX_GRANULARITY) l = RADEON_LOG_TEX_GRANULARITY;
    info->gartTexMapSize   = (s >> l) << l;
    info->log2GARTTexGran  = l;
}

/* Set AGP transfer mode according to requests and constraints */
static Bool RADEONSetAgpMode(RADEONInfoPtr info, ScreenPtr pScreen)
{
    unsigned char *RADEONMMIO = info->MMIO;
    unsigned long mode   = drmAgpGetMode(info->drmFD);	/* Default mode */
    unsigned int  vendor = drmAgpVendorId(info->drmFD);
    unsigned int  device = drmAgpDeviceId(info->drmFD);

    mode &= ~RADEON_AGP_MODE_MASK;
    if ((mode & RADEON_AGPv3_MODE) &&
	(INREG(RADEON_AGP_STATUS) & RADEON_AGPv3_MODE)) {
	/* only set one mode bit for AGPv3 */
	switch (info->agpMode) {
	case 8:          mode |= RADEON_AGPv3_8X_MODE; break;
	case 4: default: mode |= RADEON_AGPv3_4X_MODE;
	}
	/*TODO: need to take care of other bits valid for v3 mode
	 *      currently these bits are not used in all tested cards.
	 */
    } else {
	switch (info->agpMode) {
	case 4:          mode |= RADEON_AGP_4X_MODE;
	case 2:          mode |= RADEON_AGP_2X_MODE;
	case 1: default: mode |= RADEON_AGP_1X_MODE;
	}
    }

    if (info->agpFastWrite &&
	(vendor == PCI_VENDOR_AMD) &&
	(device == PCI_CHIP_AMD761)) {

	/* Disable fast write for AMD 761 chipset, since they cause
	 * lockups when enabled.
	 */
	info->agpFastWrite = FALSE;
	xf86DrvMsg(pScreen->myNum, X_WARNING,
		   "[agp] Not enabling Fast Writes on AMD 761 chipset to avoid "
		   "lockups");
    }

    if (info->agpFastWrite) mode |= RADEON_AGP_FW_MODE;

    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[agp] Mode 0x%08lx [AGP 0x%04x/0x%04x; Card 0x%04x/0x%04x]\n",
	       mode, vendor, device,
	       info->PciInfo->vendor,
	       info->PciInfo->chipType);

    if (drmAgpEnable(info->drmFD, mode) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR, "[agp] AGP not enabled\n");
	drmAgpRelease(info->drmFD);
	return FALSE;
    }

    /* Workaround for some hardware bugs */
    if (info->ChipFamily < CHIP_FAMILY_R200)
	OUTREG(RADEON_AGP_CNTL, INREG(RADEON_AGP_CNTL) | 0x000e0000);

				/* Modify the mode if the default mode
				 * is not appropriate for this
				 * particular combination of graphics
				 * card and AGP chipset.
				 */

    return TRUE;
}

/* Initialize Radeon's AGP registers */
static void RADEONSetAgpBase(RADEONInfoPtr info)
{
    unsigned char *RADEONMMIO = info->MMIO;

    OUTREG(RADEON_AGP_BASE, drmAgpBase(info->drmFD));
}

/* Initialize the AGP state.  Request memory for use in AGP space, and
 * initialize the Radeon registers to point to that memory.
 */
static Bool RADEONDRIAgpInit(RADEONInfoPtr info, ScreenPtr pScreen)
{
    int            ret;

    if (drmAgpAcquire(info->drmFD) < 0) {
	xf86DrvMsg(pScreen->myNum, X_WARNING, "[agp] AGP not available\n");
	return FALSE;
    }

    if (!RADEONSetAgpMode(info, pScreen))
	return FALSE;

    RADEONDRIInitGARTValues(info);

    if ((ret = drmAgpAlloc(info->drmFD, info->gartSize*1024*1024, 0, NULL,
			   &info->agpMemHandle)) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR, "[agp] Out of memory (%d)\n", ret);
	drmAgpRelease(info->drmFD);
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[agp] %d kB allocated with handle 0x%08x\n",
	       info->gartSize*1024, info->agpMemHandle);

    if (drmAgpBind(info->drmFD,
		   info->agpMemHandle, info->gartOffset) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR, "[agp] Could not bind\n");
	drmAgpFree(info->drmFD, info->agpMemHandle);
	drmAgpRelease(info->drmFD);
	return FALSE;
    }

    if (drmAddMap(info->drmFD, info->ringStart, info->ringMapSize,
		  DRM_AGP, DRM_READ_ONLY, &info->ringHandle) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[agp] Could not add ring mapping\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[agp] ring handle = 0x%08x\n", info->ringHandle);

    if (drmMap(info->drmFD, info->ringHandle, info->ringMapSize,
	       &info->ring) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR, "[agp] Could not map ring\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[agp] Ring mapped at 0x%08lx\n",
	       (unsigned long)info->ring);

    if (drmAddMap(info->drmFD, info->ringReadOffset, info->ringReadMapSize,
		  DRM_AGP, DRM_READ_ONLY, &info->ringReadPtrHandle) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[agp] Could not add ring read ptr mapping\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
 	       "[agp] ring read ptr handle = 0x%08x\n",
	       info->ringReadPtrHandle);

    if (drmMap(info->drmFD, info->ringReadPtrHandle, info->ringReadMapSize,
	       &info->ringReadPtr) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[agp] Could not map ring read ptr\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[agp] Ring read ptr mapped at 0x%08lx\n",
	       (unsigned long)info->ringReadPtr);

    if (drmAddMap(info->drmFD, info->bufStart, info->bufMapSize,
		  DRM_AGP, 0, &info->bufHandle) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[agp] Could not add vertex/indirect buffers mapping\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
 	       "[agp] vertex/indirect buffers handle = 0x%08x\n",
	       info->bufHandle);

    if (drmMap(info->drmFD, info->bufHandle, info->bufMapSize,
	       &info->buf) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[agp] Could not map vertex/indirect buffers\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[agp] Vertex/indirect buffers mapped at 0x%08lx\n",
	       (unsigned long)info->buf);

    if (drmAddMap(info->drmFD, info->gartTexStart, info->gartTexMapSize,
		  DRM_AGP, 0, &info->gartTexHandle) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[agp] Could not add GART texture map mapping\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
 	       "[agp] GART texture map handle = 0x%08x\n",
	       info->gartTexHandle);

    if (drmMap(info->drmFD, info->gartTexHandle, info->gartTexMapSize,
	       &info->gartTex) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[agp] Could not map GART texture map\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[agp] GART Texture map mapped at 0x%08lx\n",
	       (unsigned long)info->gartTex);

    RADEONSetAgpBase(info);

    return TRUE;
}

/* Initialize the PCI GART state.  Request memory for use in PCI space,
 * and initialize the Radeon registers to point to that memory.
 */
static Bool RADEONDRIPciInit(RADEONInfoPtr info, ScreenPtr pScreen)
{
    int  ret;
    int  flags = DRM_READ_ONLY | DRM_LOCKED | DRM_KERNEL;

    ret = drmScatterGatherAlloc(info->drmFD, info->gartSize*1024*1024,
				&info->pciMemHandle);
    if (ret < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR, "[pci] Out of memory (%d)\n", ret);
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[pci] %d kB allocated with handle 0x%08x\n",
	       info->gartSize*1024, info->pciMemHandle);

    RADEONDRIInitGARTValues(info);

    if (drmAddMap(info->drmFD, info->ringStart, info->ringMapSize,
		  DRM_SCATTER_GATHER, flags, &info->ringHandle) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[pci] Could not add ring mapping\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[pci] ring handle = 0x%08x\n", info->ringHandle);

    if (drmMap(info->drmFD, info->ringHandle, info->ringMapSize,
	       &info->ring) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR, "[pci] Could not map ring\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[pci] Ring mapped at 0x%08lx\n",
	       (unsigned long)info->ring);
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[pci] Ring contents 0x%08lx\n",
	       *(unsigned long *)(pointer)info->ring);

    if (drmAddMap(info->drmFD, info->ringReadOffset, info->ringReadMapSize,
		  DRM_SCATTER_GATHER, flags, &info->ringReadPtrHandle) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[pci] Could not add ring read ptr mapping\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
 	       "[pci] ring read ptr handle = 0x%08x\n",
	       info->ringReadPtrHandle);

    if (drmMap(info->drmFD, info->ringReadPtrHandle, info->ringReadMapSize,
	       &info->ringReadPtr) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[pci] Could not map ring read ptr\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[pci] Ring read ptr mapped at 0x%08lx\n",
	       (unsigned long)info->ringReadPtr);
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[pci] Ring read ptr contents 0x%08lx\n",
	       *(unsigned long *)(pointer)info->ringReadPtr);

    if (drmAddMap(info->drmFD, info->bufStart, info->bufMapSize,
		  DRM_SCATTER_GATHER, 0, &info->bufHandle) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[pci] Could not add vertex/indirect buffers mapping\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
 	       "[pci] vertex/indirect buffers handle = 0x%08x\n",
	       info->bufHandle);

    if (drmMap(info->drmFD, info->bufHandle, info->bufMapSize,
	       &info->buf) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[pci] Could not map vertex/indirect buffers\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[pci] Vertex/indirect buffers mapped at 0x%08lx\n",
	       (unsigned long)info->buf);
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[pci] Vertex/indirect buffers contents 0x%08lx\n",
	       *(unsigned long *)(pointer)info->buf);

    if (drmAddMap(info->drmFD, info->gartTexStart, info->gartTexMapSize,
		  DRM_SCATTER_GATHER, 0, &info->gartTexHandle) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[pci] Could not add GART texture map mapping\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
 	       "[pci] GART texture map handle = 0x%08x\n",
	       info->gartTexHandle);

    if (drmMap(info->drmFD, info->gartTexHandle, info->gartTexMapSize,
	       &info->gartTex) < 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[pci] Could not map GART texture map\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[pci] GART Texture map mapped at 0x%08lx\n",
	       (unsigned long)info->gartTex);

    return TRUE;
}

/* Add a map for the MMIO registers that will be accessed by any
 * DRI-based clients.
 */
static Bool RADEONDRIMapInit(RADEONInfoPtr info, ScreenPtr pScreen)
{
				/* Map registers */
    info->registerSize = info->MMIOSize;
    if (drmAddMap(info->drmFD, info->MMIOAddr, info->registerSize,
		  DRM_REGISTERS, DRM_READ_ONLY, &info->registerHandle) < 0) {
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[drm] register handle = 0x%08x\n", info->registerHandle);

    return TRUE;
}

/* Initialize the kernel data structures */
static int RADEONDRIKernelInit(RADEONInfoPtr info, ScreenPtr pScreen)
{
    ScrnInfoPtr    pScrn = xf86Screens[pScreen->myNum];
    int            cpp   = info->CurrentLayout.pixel_bytes;
    drmRadeonInit  drmInfo;

    memset(&drmInfo, 0, sizeof(drmRadeonInit));
    if ( info->ChipFamily >= CHIP_FAMILY_R300 )
       drmInfo.func             = DRM_RADEON_INIT_R300_CP;
    else
    if ( info->ChipFamily >= CHIP_FAMILY_R200 )
       drmInfo.func		= DRM_RADEON_INIT_R200_CP;
    else
       drmInfo.func		= DRM_RADEON_INIT_CP;

    drmInfo.sarea_priv_offset   = sizeof(XF86DRISAREARec);
    drmInfo.is_pci              = (info->cardType!=CARD_AGP);
    drmInfo.cp_mode             = info->CPMode;
    drmInfo.gart_size           = info->gartSize*1024*1024;
    drmInfo.ring_size           = info->ringSize*1024*1024;
    drmInfo.usec_timeout        = info->CPusecTimeout;

    drmInfo.fb_bpp              = info->CurrentLayout.pixel_code;
    drmInfo.depth_bpp           = (info->depthBits - 8) * 2;

    drmInfo.front_offset        = info->frontOffset;
    drmInfo.front_pitch         = info->frontPitch * cpp;
    drmInfo.back_offset         = info->backOffset;
    drmInfo.back_pitch          = info->backPitch * cpp;
    drmInfo.depth_offset        = info->depthOffset;
    drmInfo.depth_pitch         = info->depthPitch * drmInfo.depth_bpp / 8;

    drmInfo.fb_offset           = info->fbHandle;
    drmInfo.mmio_offset         = info->registerHandle;
    drmInfo.ring_offset         = info->ringHandle;
    drmInfo.ring_rptr_offset    = info->ringReadPtrHandle;
    drmInfo.buffers_offset      = info->bufHandle;
    drmInfo.gart_textures_offset= info->gartTexHandle;

    if (drmCommandWrite(info->drmFD, DRM_RADEON_CP_INIT,
			&drmInfo, sizeof(drmRadeonInit)) < 0)
	return FALSE;

    /* DRM_RADEON_CP_INIT does an engine reset, which resets some engine
     * registers back to their default values, so we need to restore
     * those engine register here.
     */
    RADEONEngineRestore(pScrn);

    return TRUE;
}

static void RADEONDRIGartHeapInit(RADEONInfoPtr info, ScreenPtr pScreen)
{
    drmRadeonMemInitHeap drmHeap;

    /* Start up the simple memory manager for GART space */
    drmHeap.region = RADEON_MEM_REGION_GART;
    drmHeap.start  = 0;
    drmHeap.size   = info->gartTexMapSize;

    if (drmCommandWrite(info->drmFD, DRM_RADEON_INIT_HEAP,
			&drmHeap, sizeof(drmHeap))) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[drm] Failed to initialize GART heap manager\n");
    } else {
	xf86DrvMsg(pScreen->myNum, X_INFO,
		   "[drm] Initialized kernel GART heap manager, %d\n",
		   info->gartTexMapSize);
    }
}

/* Add a map for the vertex buffers that will be accessed by any
 * DRI-based clients.
 */
static Bool RADEONDRIBufInit(RADEONInfoPtr info, ScreenPtr pScreen)
{
				/* Initialize vertex buffers */
    info->bufNumBufs = drmAddBufs(info->drmFD,
				  info->bufMapSize / RADEON_BUFFER_SIZE,
				  RADEON_BUFFER_SIZE,
				  (info->cardType!=CARD_AGP) ? DRM_SG_BUFFER : DRM_AGP_BUFFER,
				  info->bufStart);

    if (info->bufNumBufs <= 0) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[drm] Could not create vertex/indirect buffers list\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[drm] Added %d %d byte vertex/indirect buffers\n",
	       info->bufNumBufs, RADEON_BUFFER_SIZE);

    if (!(info->buffers = drmMapBufs(info->drmFD))) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[drm] Failed to map vertex/indirect buffers list\n");
	return FALSE;
    }
    xf86DrvMsg(pScreen->myNum, X_INFO,
	       "[drm] Mapped %d vertex/indirect buffers\n",
	       info->buffers->count);

    return TRUE;
}

static void RADEONDRIIrqInit(RADEONInfoPtr info, ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

    if (!info->irq) {
	info->irq = drmGetInterruptFromBusID(
	    info->drmFD,
	    ((pciConfigPtr)info->PciInfo->thisCard)->busnum,
	    ((pciConfigPtr)info->PciInfo->thisCard)->devnum,
	    ((pciConfigPtr)info->PciInfo->thisCard)->funcnum);

	if ((drmCtlInstHandler(info->drmFD, info->irq)) != 0) {
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		       "[drm] failure adding irq handler, "
		       "there is a device already using that irq\n"
		       "[drm] falling back to irq-free operation\n");
	    info->irq = 0;
	} else {
	    unsigned char *RADEONMMIO = info->MMIO;
	    info->ModeReg.gen_int_cntl = INREG( RADEON_GEN_INT_CNTL );
	}
    }

    if (info->irq)
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "[drm] dma control initialized, using IRQ %d\n",
		   info->irq);
}


/* Initialize the CP state, and start the CP (if used by the X server) */
static void RADEONDRICPInit(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr  info = RADEONPTR(pScrn);

				/* Turn on bus mastering */
    info->BusCntl &= ~RADEON_BUS_MASTER_DIS;

				/* Make sure the CP is on for the X server */
    RADEONCP_START(pScrn, info);
#ifdef USE_XAA
    if (!info->useEXA)
	info->dst_pitch_offset = info->frontPitchOffset;
#endif
}


/* Get the DRM version and do some basic useability checks of DRI */
Bool RADEONDRIGetVersion(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr  info    = RADEONPTR(pScrn);
    int            major, minor, patch, fd;
    int		   req_minor, req_patch;
    char           *busId;

    /* Check that the GLX, DRI, and DRM modules have been loaded by testing
     * for known symbols in each module.
     */
    if (!xf86LoaderCheckSymbol("GlxSetVisualConfigs")) return FALSE;
    if (!xf86LoaderCheckSymbol("drmAvailable"))        return FALSE;
    if (!xf86LoaderCheckSymbol("DRIQueryVersion")) {
      xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		 "[dri] RADEONDRIGetVersion failed (libdri.a too old)\n"
		 "[dri] Disabling DRI.\n");
      return FALSE;
    }

    /* Check the DRI version */
    DRIQueryVersion(&major, &minor, &patch);
    if (major != DRIINFO_MAJOR_VERSION || minor < DRIINFO_MINOR_VERSION) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "[dri] RADEONDRIGetVersion failed because of a version "
		   "mismatch.\n"
		   "[dri] libdri version is %d.%d.%d but version %d.%d.x is "
		   "needed.\n"
		   "[dri] Disabling DRI.\n",
		   major, minor, patch,
                   DRIINFO_MAJOR_VERSION, DRIINFO_MINOR_VERSION);
	return FALSE;
    }

    /* Check the lib version */
    if (xf86LoaderCheckSymbol("drmGetLibVersion"))
	info->pLibDRMVersion = drmGetLibVersion(info->drmFD);
    if (info->pLibDRMVersion == NULL) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "[dri] RADEONDRIGetVersion failed because libDRM is really "
		   "way to old to even get a version number out of it.\n"
		   "[dri] Disabling DRI.\n");
	return FALSE;
    }
    if (info->pLibDRMVersion->version_major != 1 ||
	info->pLibDRMVersion->version_minor < 2) {
	    /* incompatible drm library version */
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "[dri] RADEONDRIGetVersion failed because of a "
		   "version mismatch.\n"
		   "[dri] libdrm.a module version is %d.%d.%d but "
		   "version 1.2.x is needed.\n"
		   "[dri] Disabling DRI.\n",
		   info->pLibDRMVersion->version_major,
		   info->pLibDRMVersion->version_minor,
		   info->pLibDRMVersion->version_patchlevel);
	drmFreeVersion(info->pLibDRMVersion);
	info->pLibDRMVersion = NULL;
	return FALSE;
    }

    /* Create a bus Id */
    if (xf86LoaderCheckSymbol("DRICreatePCIBusID")) {
	busId = DRICreatePCIBusID(info->PciInfo);
    } else {
	busId = xalloc(64);
	sprintf(busId,
		"PCI:%d:%d:%d",
		info->PciInfo->bus,
		info->PciInfo->device,
		info->PciInfo->func);
    }

    /* Low level DRM open */
    fd = drmOpen(RADEON_DRIVER_NAME, busId);
    xfree(busId);
    if (fd < 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "[dri] RADEONDRIGetVersion failed to open the DRM\n"
		   "[dri] Disabling DRI.\n");
	return FALSE;
    }

    /* Get DRM version & close DRM */
    info->pKernelDRMVersion = drmGetVersion(fd);
    drmClose(fd);
    if (info->pKernelDRMVersion == NULL) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "[dri] RADEONDRIGetVersion failed to get the DRM version\n"
		   "[dri] Disabling DRI.\n");
	return FALSE;
    }

    /* Now check if we qualify */
    if (info->ChipFamily >= CHIP_FAMILY_R300) {
        req_minor = 17;
        req_patch = 0;
    } else if (info->IsIGP) {
        req_minor = 10;
	req_patch = 0;
    } else { /* Many problems have been reported with 1.7 in the 2.4 kernel */
	req_minor = 8;
	req_patch = 0;
    }

    /* We don't, bummer ! */
    if (info->pKernelDRMVersion->version_major != 1 ||
	info->pKernelDRMVersion->version_minor < req_minor ||
	(info->pKernelDRMVersion->version_minor == req_minor &&
	 info->pKernelDRMVersion->version_patchlevel < req_patch)) {
        /* Incompatible drm version */
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "[dri] RADEONDRIGetVersion failed because of a version "
		   "mismatch.\n"
		   "[dri] radeon.o kernel module version is %d.%d.%d "
		   "but version 1.%d.%d or newer is needed.\n"
		   "[dri] Disabling DRI.\n",
		   info->pKernelDRMVersion->version_major,
		   info->pKernelDRMVersion->version_minor,
		   info->pKernelDRMVersion->version_patchlevel,
		   req_minor,
		   req_patch);
	drmFreeVersion(info->pKernelDRMVersion);
	info->pKernelDRMVersion = NULL;
	return FALSE;
    }

    return TRUE;
}

/* Initialize the screen-specific data structures for the DRI and the
 * Radeon.  This is the main entry point to the device-specific
 * initialization code.  It calls device-independent DRI functions to
 * create the DRI data structures and initialize the DRI state.
 */
Bool RADEONDRIScreenInit(ScreenPtr pScreen)
{
    ScrnInfoPtr    pScrn   = xf86Screens[pScreen->myNum];
    RADEONInfoPtr  info    = RADEONPTR(pScrn);
    DRIInfoPtr     pDRIInfo;
    RADEONDRIPtr   pRADEONDRI;

    info->DRICloseScreen = NULL;

    switch (info->CurrentLayout.pixel_code) {
    case 8:
    case 15:
    case 24:
	/* These modes are not supported (yet). */
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[dri] RADEONInitVisualConfigs failed "
		   "(depth %d not supported).  "
		   "Disabling DRI.\n", info->CurrentLayout.pixel_code);
	return FALSE;

	/* Only 16 and 32 color depths are supports currently. */
    case 16:
    case 32:
	break;
    }

    radeon_drm_page_size = getpagesize();

    /* Create the DRI data structure, and fill it in before calling the
     * DRIScreenInit().
     */
    if (!(pDRIInfo = DRICreateInfoRec())) return FALSE;

    info->pDRIInfo                       = pDRIInfo;
    pDRIInfo->drmDriverName              = RADEON_DRIVER_NAME;

    if ( (info->ChipFamily >= CHIP_FAMILY_R300) ) {
       pDRIInfo->clientDriverName        = R300_DRIVER_NAME;
    } else    
    if ( info->ChipFamily >= CHIP_FAMILY_R200 )
       pDRIInfo->clientDriverName	 = R200_DRIVER_NAME;
    else 
       pDRIInfo->clientDriverName	 = RADEON_DRIVER_NAME;

    if (xf86LoaderCheckSymbol("DRICreatePCIBusID")) {
	pDRIInfo->busIdString = DRICreatePCIBusID(info->PciInfo);
    } else {
	pDRIInfo->busIdString            = xalloc(64);
	sprintf(pDRIInfo->busIdString,
		"PCI:%d:%d:%d",
		info->PciInfo->bus,
		info->PciInfo->device,
		info->PciInfo->func);
    }
    pDRIInfo->ddxDriverMajorVersion      = info->allowColorTiling ?
    				RADEON_VERSION_MAJOR_TILED : RADEON_VERSION_MAJOR;
    pDRIInfo->ddxDriverMinorVersion      = RADEON_VERSION_MINOR;
    pDRIInfo->ddxDriverPatchVersion      = RADEON_VERSION_PATCH;
    pDRIInfo->frameBufferPhysicalAddress = (void *)info->LinearAddr;
    pDRIInfo->frameBufferSize            = info->FbMapSize - info->FbSecureSize;
    pDRIInfo->frameBufferStride          = (pScrn->displayWidth *
					    info->CurrentLayout.pixel_bytes);
    pDRIInfo->ddxDrawableTableEntry      = RADEON_MAX_DRAWABLES;
    pDRIInfo->maxDrawableTableEntry      = (SAREA_MAX_DRAWABLES
					    < RADEON_MAX_DRAWABLES
					    ? SAREA_MAX_DRAWABLES
					    : RADEON_MAX_DRAWABLES);
    /* kill DRIAdjustFrame. We adjust sarea frame info ourselves to work
       correctly with pageflip + mergedfb/color tiling */
    pDRIInfo->wrap.AdjustFrame = NULL;

#ifdef PER_CONTEXT_SAREA
    /* This is only here for testing per-context SAREAs.  When used, the
       magic number below would be properly defined in a header file. */
    info->perctx_sarea_size = 64 * 1024;
#endif

#ifdef NOT_DONE
    /* FIXME: Need to extend DRI protocol to pass this size back to
     * client for SAREA mapping that includes a device private record
     */
    pDRIInfo->SAREASize = ((sizeof(XF86DRISAREARec) + 0xfff)
			   & 0x1000); /* round to page */
    /* + shared memory device private rec */
#else
    /* For now the mapping works by using a fixed size defined
     * in the SAREA header
     */
    if (sizeof(XF86DRISAREARec)+sizeof(RADEONSAREAPriv) > SAREA_MAX) {
	ErrorF("Data does not fit in SAREA\n");
	return FALSE;
    }
    pDRIInfo->SAREASize = SAREA_MAX;
#endif

    if (!(pRADEONDRI = (RADEONDRIPtr)xcalloc(sizeof(RADEONDRIRec),1))) {
	DRIDestroyInfoRec(info->pDRIInfo);
	info->pDRIInfo = NULL;
	return FALSE;
    }
    pDRIInfo->devPrivate     = pRADEONDRI;
    pDRIInfo->devPrivateSize = sizeof(RADEONDRIRec);
    pDRIInfo->contextSize    = sizeof(RADEONDRIContextRec);

    pDRIInfo->CreateContext  = RADEONCreateContext;
    pDRIInfo->DestroyContext = RADEONDestroyContext;
    pDRIInfo->SwapContext    = RADEONDRISwapContext;
    pDRIInfo->InitBuffers    = RADEONDRIInitBuffers;
    pDRIInfo->MoveBuffers    = RADEONDRIMoveBuffers;
    pDRIInfo->bufferRequests = DRI_ALL_WINDOWS;
    pDRIInfo->TransitionTo2d = RADEONDRITransitionTo2d;
    pDRIInfo->TransitionTo3d = RADEONDRITransitionTo3d;
    pDRIInfo->TransitionSingleToMulti3D = RADEONDRITransitionSingleToMulti3d;
    pDRIInfo->TransitionMultiToSingle3D = RADEONDRITransitionMultiToSingle3d;

    pDRIInfo->createDummyCtx     = TRUE;
    pDRIInfo->createDummyCtxPriv = FALSE;

    if (!DRIScreenInit(pScreen, pDRIInfo, &info->drmFD)) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[dri] DRIScreenInit failed.  Disabling DRI.\n");
	xfree(pDRIInfo->devPrivate);
	pDRIInfo->devPrivate = NULL;
	DRIDestroyInfoRec(pDRIInfo);
	pDRIInfo = NULL;
	return FALSE;
    }
				/* Initialize AGP */
    if (info->cardType==CARD_AGP && !RADEONDRIAgpInit(info, pScreen)) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[agp] AGP failed to initialize. Disabling the DRI.\n" );
	xf86DrvMsg(pScreen->myNum, X_INFO,
		   "[agp] You may want to make sure the agpgart kernel "
		   "module\nis loaded before the radeon kernel module.\n");
	RADEONDRICloseScreen(pScreen);
	return FALSE;
    }

				/* Initialize PCI */
    if ((info->cardType!=CARD_AGP) && !RADEONDRIPciInit(info, pScreen)) {
	xf86DrvMsg(pScreen->myNum, X_ERROR,
		   "[pci] PCI failed to initialize. Disabling the DRI.\n" );
	RADEONDRICloseScreen(pScreen);
	return FALSE;
    }

				/* DRIScreenInit doesn't add all the
				 * common mappings.  Add additional
				 * mappings here.
				 */
    if (!RADEONDRIMapInit(info, pScreen)) {
	RADEONDRICloseScreen(pScreen);
	return FALSE;
    }

				/* DRIScreenInit adds the frame buffer
				   map, but we need it as well */
    {
	void *scratch_ptr;
        int scratch_int;

	DRIGetDeviceInfo(pScreen, &info->fbHandle,
                         &scratch_int, &scratch_int,
                         &scratch_int, &scratch_int,
                         &scratch_ptr);
    }

				/* FIXME: When are these mappings unmapped? */

    if (!RADEONInitVisualConfigs(pScreen)) {
	RADEONDRICloseScreen(pScreen);
	return FALSE;
    }
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[dri] Visual configs initialized\n");

    return TRUE;
}

static Bool RADEONDRIDoCloseScreen(int scrnIndex, ScreenPtr pScreen)
{
    ScrnInfoPtr    pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr  info  = RADEONPTR(pScrn);

    RADEONDRICloseScreen(pScreen);

    pScreen->CloseScreen = info->DRICloseScreen;
    return (*pScreen->CloseScreen)(scrnIndex, pScreen);
}

/* Finish initializing the device-dependent DRI state, and call
 * DRIFinishScreenInit() to complete the device-independent DRI
 * initialization.
 */
Bool RADEONDRIFinishScreenInit(ScreenPtr pScreen)
{
    ScrnInfoPtr         pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr       info  = RADEONPTR(pScrn);
    RADEONSAREAPrivPtr  pSAREAPriv;
    RADEONDRIPtr        pRADEONDRI;

    info->pDRIInfo->driverSwapMethod = DRI_HIDE_X_CONTEXT;
    /* info->pDRIInfo->driverSwapMethod = DRI_SERVER_SWAP; */

    /* NOTE: DRIFinishScreenInit must be called before *DRIKernelInit
     * because *DRIKernelInit requires that the hardware lock is held by
     * the X server, and the first time the hardware lock is grabbed is
     * in DRIFinishScreenInit.
     */
    if (!DRIFinishScreenInit(pScreen)) {
	RADEONDRICloseScreen(pScreen);
	return FALSE;
    }

    /* Initialize the kernel data structures */
    if (!RADEONDRIKernelInit(info, pScreen)) {
	RADEONDRICloseScreen(pScreen);
	return FALSE;
    }

    /* Initialize the vertex buffers list */
    if (!RADEONDRIBufInit(info, pScreen)) {
	RADEONDRICloseScreen(pScreen);
	return FALSE;
    }

    /* Initialize IRQ */
    RADEONDRIIrqInit(info, pScreen);

    /* Initialize kernel GART memory manager */
    RADEONDRIGartHeapInit(info, pScreen);

    /* Initialize and start the CP if required */
    RADEONDRICPInit(pScrn);

    /* Initialize the SAREA private data structure */
    pSAREAPriv = (RADEONSAREAPrivPtr)DRIGetSAREAPrivate(pScreen);
    memset(pSAREAPriv, 0, sizeof(*pSAREAPriv));

    pRADEONDRI                    = (RADEONDRIPtr)info->pDRIInfo->devPrivate;

    pRADEONDRI->deviceID          = info->Chipset;
    pRADEONDRI->width             = pScrn->virtualX;
    pRADEONDRI->height            = pScrn->virtualY;
    pRADEONDRI->depth             = pScrn->depth;
    pRADEONDRI->bpp               = pScrn->bitsPerPixel;

    pRADEONDRI->IsPCI             = (info->cardType!=CARD_AGP);
    pRADEONDRI->AGPMode           = info->agpMode;

    pRADEONDRI->frontOffset       = info->frontOffset;
    pRADEONDRI->frontPitch        = info->frontPitch;
    pRADEONDRI->backOffset        = info->backOffset;
    pRADEONDRI->backPitch         = info->backPitch;
    pRADEONDRI->depthOffset       = info->depthOffset;
    pRADEONDRI->depthPitch        = info->depthPitch;
    pRADEONDRI->textureOffset     = info->textureOffset;
    pRADEONDRI->textureSize       = info->textureSize;
    pRADEONDRI->log2TexGran       = info->log2TexGran;

    pRADEONDRI->registerHandle    = info->registerHandle;
    pRADEONDRI->registerSize      = info->registerSize;

    pRADEONDRI->statusHandle      = info->ringReadPtrHandle;
    pRADEONDRI->statusSize        = info->ringReadMapSize;

    pRADEONDRI->gartTexHandle     = info->gartTexHandle;
    pRADEONDRI->gartTexMapSize    = info->gartTexMapSize;
    pRADEONDRI->log2GARTTexGran   = info->log2GARTTexGran;
    pRADEONDRI->gartTexOffset     = info->gartTexStart;

    pRADEONDRI->sarea_priv_offset = sizeof(XF86DRISAREARec);

#ifdef PER_CONTEXT_SAREA
    /* Set per-context SAREA size */
    pRADEONDRI->perctx_sarea_size = info->perctx_sarea_size;
#endif

    info->directRenderingInited = TRUE;

    /* Wrap CloseScreen */
    info->DRICloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = RADEONDRIDoCloseScreen;

    return TRUE;
}

void RADEONDRIInitPageFlip(ScreenPtr pScreen)
{
    ScrnInfoPtr         pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr       info  = RADEONPTR(pScrn);

#ifdef USE_XAA
   /* Have shadowfb run only while there is 3d active. This must happen late,
     * after XAAInit has been called 
     */
    if (!info->useEXA) {
	if (!ShadowFBInit( pScreen, RADEONDRIRefreshArea )) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		       "ShadowFB init failed, Page Flipping disabled\n");
	    info->allowPageFlip = 0;
	} else
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		       "ShadowFB initialized for Page Flipping\n");
    } else
#endif /* USE_XAA */
    {
       info->allowPageFlip = 0;
    }
}

/**
 * This function will attempt to get the Radeon hardware back into shape
 * after a resume from disc.
 *
 * Charl P. Botha <http://cpbotha.net>
 */
void RADEONDRIResume(ScreenPtr pScreen)
{
    int _ret;
    ScrnInfoPtr   pScrn   = xf86Screens[pScreen->myNum];
    RADEONInfoPtr info    = RADEONPTR(pScrn);

    if (info->pKernelDRMVersion->version_minor >= 9) {
	xf86DrvMsg(pScreen->myNum, X_INFO,
		   "[RESUME] Attempting to re-init Radeon hardware.\n");
    } else {
	xf86DrvMsg(pScreen->myNum, X_WARNING,
		   "[RESUME] Cannot re-init Radeon hardware, DRM too old\n"
		   "(need 1.9.0  or newer)\n");
	return;
    }

    if (info->cardType==CARD_AGP) {
	if (!RADEONSetAgpMode(info, pScreen))
	    return;

	RADEONSetAgpBase(info);
    }

    _ret = drmCommandNone(info->drmFD, DRM_RADEON_CP_RESUME);
    if (_ret) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "%s: CP resume %d\n", __FUNCTION__, _ret);
	/* FIXME: return? */
    }

    RADEONEngineRestore(pScrn);

    RADEONDRICPInit(pScrn);
}

void RADEONDRIStop(ScreenPtr pScreen)
{
    ScrnInfoPtr    pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr  info  = RADEONPTR(pScrn);
    RING_LOCALS;

    RADEONTRACE(("RADEONDRIStop\n"));

    /* Stop the CP */
    if (info->directRenderingInited) {
	/* If we've generated any CP commands, we must flush them to the
	 * kernel module now.
	 */
	if (info->CPInUse) {
	    RADEON_FLUSH_CACHE();
	    RADEON_WAIT_UNTIL_IDLE();
	    RADEONCPReleaseIndirect(pScrn);

	    info->CPInUse = FALSE;
	}
	RADEONCP_STOP(pScrn, info);
    }
    info->directRenderingInited = FALSE;
}

/* The screen is being closed, so clean up any state and free any
 * resources used by the DRI.
 */
void RADEONDRICloseScreen(ScreenPtr pScreen)
{
    ScrnInfoPtr    pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr  info  = RADEONPTR(pScrn);
    drmRadeonInit  drmInfo;

     RADEONTRACE(("RADEONDRICloseScreen\n"));
    
     if (info->irq) {
	drmCtlUninstHandler(info->drmFD);
	info->irq = 0;
	info->ModeReg.gen_int_cntl = 0;
    }

    /* De-allocate vertex buffers */
    if (info->buffers) {
	drmUnmapBufs(info->buffers);
	info->buffers = NULL;
    }

    /* De-allocate all kernel resources */
    memset(&drmInfo, 0, sizeof(drmRadeonInit));
    drmInfo.func = DRM_RADEON_CLEANUP_CP;
    drmCommandWrite(info->drmFD, DRM_RADEON_CP_INIT,
		    &drmInfo, sizeof(drmRadeonInit));

    /* De-allocate all GART resources */
    if (info->gartTex) {
	drmUnmap(info->gartTex, info->gartTexMapSize);
	info->gartTex = NULL;
    }
    if (info->buf) {
	drmUnmap(info->buf, info->bufMapSize);
	info->buf = NULL;
    }
    if (info->ringReadPtr) {
	drmUnmap(info->ringReadPtr, info->ringReadMapSize);
	info->ringReadPtr = NULL;
    }
    if (info->ring) {
	drmUnmap(info->ring, info->ringMapSize);
	info->ring = NULL;
    }
    if (info->agpMemHandle != DRM_AGP_NO_HANDLE) {
	drmAgpUnbind(info->drmFD, info->agpMemHandle);
	drmAgpFree(info->drmFD, info->agpMemHandle);
	info->agpMemHandle = DRM_AGP_NO_HANDLE;
	drmAgpRelease(info->drmFD);
    }
    if (info->pciMemHandle) {
	drmScatterGatherFree(info->drmFD, info->pciMemHandle);
	info->pciMemHandle = 0;
    }

    if (info->pciGartBackup) {
	xfree(info->pciGartBackup);
	info->pciGartBackup = NULL;
    }

    /* De-allocate all DRI resources */
    DRICloseScreen(pScreen);

    /* De-allocate all DRI data structures */
    if (info->pDRIInfo) {
	if (info->pDRIInfo->devPrivate) {
	    xfree(info->pDRIInfo->devPrivate);
	    info->pDRIInfo->devPrivate = NULL;
	}
	DRIDestroyInfoRec(info->pDRIInfo);
	info->pDRIInfo = NULL;
    }
    if (info->pVisualConfigs) {
	xfree(info->pVisualConfigs);
	info->pVisualConfigs = NULL;
    }
    if (info->pVisualConfigsPriv) {
	xfree(info->pVisualConfigsPriv);
	info->pVisualConfigsPriv = NULL;
    }
}

#ifdef USE_XAA

/* Use callbacks from dri.c to support pageflipping mode for a single
 * 3d context without need for any specific full-screen extension.
 *
 * Also use these callbacks to allocate and free 3d-specific memory on
 * demand.
 */


/* Use the shadowfb module to maintain a list of dirty rectangles.
 * These are blitted to the back buffer to keep both buffers clean
 * during page-flipping when the 3d application isn't fullscreen.
 *
 * Unlike most use of the shadowfb code, both buffers are in video memory.
 *
 * An alternative to this would be to organize for all on-screen drawing
 * operations to be duplicated for the two buffers.  That might be
 * faster, but seems like a lot more work...
 */


static void RADEONDRIRefreshArea(ScrnInfoPtr pScrn, int num, BoxPtr pbox)
{
    RADEONInfoPtr       info       = RADEONPTR(pScrn);
    int                 i;
    RADEONSAREAPrivPtr  pSAREAPriv = DRIGetSAREAPrivate(pScrn->pScreen);

    if (!info->directRenderingInited)
	return;

    /* Don't want to do this when no 3d is active and pages are
     * right-way-round
     */
    if (!pSAREAPriv->pfAllowPageFlip && pSAREAPriv->pfCurrentPage == 0)
	return;

    /* XXX: implement for EXA */
    /* pretty much a hack. */

    /* Make sure accel has been properly inited */
    if (info->accel == NULL || info->accel->SetupForScreenToScreenCopy == NULL)
	return;
    if (info->tilingEnabled)
       info->dst_pitch_offset |= RADEON_DST_TILE_MACRO;
    (*info->accel->SetupForScreenToScreenCopy)(pScrn,
					       1, 1, GXcopy,
					       (CARD32)(-1), -1);

    for (i = 0 ; i < num ; i++, pbox++) {
	int xa = max(pbox->x1, 0), xb = min(pbox->x2, pScrn->virtualX-1);
	int ya = max(pbox->y1, 0), yb = min(pbox->y2, pScrn->virtualY-1);

	if (xa <= xb && ya <= yb) {
	    (*info->accel->SubsequentScreenToScreenCopy)(pScrn, xa, ya,
							 xa + info->backX,
							 ya + info->backY,
							 xb - xa + 1,
							 yb - ya + 1);
	}
    }
    info->dst_pitch_offset &= ~RADEON_DST_TILE_MACRO;
}

#endif /* USE_XAA */

static void RADEONEnablePageFlip(ScreenPtr pScreen)
{
#ifdef USE_XAA
    ScrnInfoPtr         pScrn      = xf86Screens[pScreen->myNum];
    RADEONInfoPtr       info       = RADEONPTR(pScrn);
    RADEONSAREAPrivPtr  pSAREAPriv = DRIGetSAREAPrivate(pScreen);

    /* XXX: Fix in EXA case */
    if (info->allowPageFlip) {
        /* pretty much a hack. */
	if (info->tilingEnabled)
	    info->dst_pitch_offset |= RADEON_DST_TILE_MACRO;
	/* Duplicate the frontbuffer to the backbuffer */
	(*info->accel->SetupForScreenToScreenCopy)(pScrn,
						   1, 1, GXcopy,
						   (CARD32)(-1), -1);

	(*info->accel->SubsequentScreenToScreenCopy)(pScrn,
						     0,
						     0,
						     info->backX,
						     info->backY,
						     pScrn->virtualX,
						     pScrn->virtualY);

	info->dst_pitch_offset &= ~RADEON_DST_TILE_MACRO;
	pSAREAPriv->pfAllowPageFlip = 1;
    }
#endif /* USE_XAA */
}

static void RADEONDisablePageFlip(ScreenPtr pScreen)
{
    /* Tell the clients not to pageflip.  How?
     *   -- Field in sarea, plus bumping the window counters.
     *   -- DRM needs to cope with Front-to-Back swapbuffers.
     */
    RADEONSAREAPrivPtr  pSAREAPriv = DRIGetSAREAPrivate(pScreen);

    pSAREAPriv->pfAllowPageFlip = 0;
}

static void RADEONDRITransitionSingleToMulti3d(ScreenPtr pScreen)
{
    RADEONDisablePageFlip(pScreen);
}

static void RADEONDRITransitionMultiToSingle3d(ScreenPtr pScreen)
{
    /* Let the remaining 3d app start page flipping again */
    RADEONEnablePageFlip(pScreen);
}

static void RADEONDRITransitionTo3d(ScreenPtr pScreen)
{
    ScrnInfoPtr    pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr  info  = RADEONPTR(pScrn);
#ifdef USE_XAA
    FBAreaPtr      fbarea;
    int            width, height;

    /* EXA allocates these areas up front, so it doesn't do the following
     * stuff.
     */
    if (!info->useEXA) {
	/* reserve offscreen area for back and depth buffers and textures */

	/* If we still have an area for the back buffer reserved, free it
	 * first so we always start with all free offscreen memory, except
	 * maybe for Xv
	 */
	if (info->backArea) {
	    xf86FreeOffscreenArea(info->backArea);
	    info->backArea = NULL;
        }

	xf86PurgeUnlockedOffscreenAreas(pScreen);

	xf86QueryLargestOffscreenArea(pScreen, &width, &height, 0, 0, 0);

	/* Free Xv linear offscreen memory if necessary
	 * FIXME: This is hideous.  What about telling xv "oh btw you have no memory
	 * any more?" -- anholt
	 */
	if (height < (info->depthTexLines + info->backLines)) {
	    RADEONPortPrivPtr portPriv = info->adaptor->pPortPrivates[0].ptr;
	    xf86FreeOffscreenLinear((FBLinearPtr)portPriv->video_memory);
	    portPriv->video_memory = NULL;
	    xf86QueryLargestOffscreenArea(pScreen, &width, &height, 0, 0, 0);
	}

	/* Reserve placeholder area so the other areas will match the
	 * pre-calculated offsets
	 * FIXME: We may have other locked allocations and thus this would allocate
	 * in the wrong place.  The XV surface allocations seem likely. -- anholt
	 */
	fbarea = xf86AllocateOffscreenArea(pScreen, pScrn->displayWidth,
					   height
					   - info->depthTexLines
					   - info->backLines,
					   pScrn->displayWidth,
					   NULL, NULL, NULL);
	if (!fbarea)
	    xf86DrvMsg(pScreen->myNum, X_ERROR, "Unable to reserve placeholder "
		       "offscreen area, you might experience screen corruption\n");

	info->backArea = xf86AllocateOffscreenArea(pScreen, pScrn->displayWidth,
						   info->backLines,
						   pScrn->displayWidth,
						   NULL, NULL, NULL);
	if (!info->backArea)
	    xf86DrvMsg(pScreen->myNum, X_ERROR, "Unable to reserve offscreen "
		       "area for back buffer, you might experience screen "
		       "corruption\n");

	info->depthTexArea = xf86AllocateOffscreenArea(pScreen,
						       pScrn->displayWidth,
						       info->depthTexLines,
						       pScrn->displayWidth,
						       NULL, NULL, NULL);
	if (!info->depthTexArea)
	    xf86DrvMsg(pScreen->myNum, X_ERROR, "Unable to reserve offscreen "
		       "area for depth buffer and textures, you might "
		       "experience screen corruption\n");

	xf86FreeOffscreenArea(fbarea);
    }
#endif /* USE_XAA */

    info->have3DWindows = 1;

    RADEONChangeSurfaces(pScrn);
    RADEONEnablePageFlip(pScreen);

    if (info->cursor)
	xf86ForceHWCursor (pScreen, TRUE);
}

static void RADEONDRITransitionTo2d(ScreenPtr pScreen)
{
    ScrnInfoPtr         pScrn      = xf86Screens[pScreen->myNum];
    RADEONInfoPtr       info       = RADEONPTR(pScrn);
    RADEONSAREAPrivPtr  pSAREAPriv = DRIGetSAREAPrivate(pScreen);

    /* Try flipping back to the front page if necessary */
    if (pSAREAPriv->pfCurrentPage == 1)
	drmCommandNone(info->drmFD, DRM_RADEON_FLIP);

    /* Shut down shadowing if we've made it back to the front page */
    if (pSAREAPriv->pfCurrentPage == 0) {
	RADEONDisablePageFlip(pScreen);
#ifdef USE_XAA
	if (!info->useEXA) {
	    xf86FreeOffscreenArea(info->backArea);
	    info->backArea = NULL;
	}
#endif
    } else {
	xf86DrvMsg(pScreen->myNum, X_WARNING,
		   "[dri] RADEONDRITransitionTo2d: "
		   "kernel failed to unflip buffers.\n");
    }

#ifdef USE_XAA
    if (!info->useEXA)
	xf86FreeOffscreenArea(info->depthTexArea);
#endif

    info->have3DWindows = 0;

    RADEONChangeSurfaces(pScrn);

    if (info->cursor)
	xf86ForceHWCursor (pScreen, FALSE);
}

void RADEONDRIAllocatePCIGARTTable(ScreenPtr pScreen)
{
    ScrnInfoPtr        pScrn   = xf86Screens[pScreen->myNum];
    RADEONInfoPtr      info    = RADEONPTR(pScrn);

    if (info->cardType != CARD_PCIE ||
	info->pKernelDRMVersion->version_minor < 19)
      return;

    if (info->FbSecureSize==0)
      return;

    info->pciGartSize = RADEON_PCIGART_TABLE_SIZE;

    /* allocate space to back up PCIEGART table */
    info->pciGartBackup = xnfcalloc(1, info->pciGartSize);
    if (info->pciGartBackup == NULL)
      return;

    info->pciGartOffset = (info->FbMapSize - info->FbSecureSize);


}
