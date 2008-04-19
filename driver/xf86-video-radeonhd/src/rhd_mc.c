/*
 * Copyright 2007  Luc Verhaegen <lverhaegen@novell.com>
 * Copyright 2007  Matthias Hopf <mhopf@novell.com>
 * Copyright 2007  Egbert Eich   <eich@novell.com>
 * Copyright 2007  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if HAVE_XF86_ANSIC_H
# include "xf86_ansic.h"
#else
# include <unistd.h>
#endif

#include "xf86.h"

#include "rhd.h"
#include "rhd_regs.h"

struct rhdMC {
    CARD32 FbLocation;
    CARD32 MiscOffset;
    Bool Stored;
};


/*
 *
 */
void
RHDMCInit(RHDPtr rhdPtr)
{
    struct rhdMC *MC;

    RHDFUNC(rhdPtr);

    /* we know nothing about RS600, yet */
    if (rhdPtr->ChipSet == RHD_RS600)
	return;

    MC = xnfcalloc(1, sizeof(struct rhdMC));
    MC->Stored = FALSE;

    rhdPtr->MC = MC;
}

/*
 * Free structure.
 */
void
RHDMCDestroy(RHDPtr rhdPtr)
{
    RHDFUNC(rhdPtr);

    if (!rhdPtr->MC)
	return;

    xfree(rhdPtr->MC);
    rhdPtr->MC = NULL;
}

/*
 * Save MC_VM state.
 */
void
RHDSaveMC(RHDPtr rhdPtr)
{
    struct rhdMC *MC = rhdPtr->MC;

    RHDFUNC(rhdPtr);

    if (!MC)
	return;

    if (rhdPtr->ChipSet < RHD_RS690) {
	if (RHDFamily(rhdPtr->ChipSet) == RHD_FAMILY_RV515)
	    MC->FbLocation = RHDReadMC(rhdPtr, MC_IND_ALL | RV515_MC_FB_LOCATION);
	else
	    MC->FbLocation = RHDReadMC(rhdPtr, MC_IND_ALL | R5XX_MC_FB_LOCATION);
    } else if (RHDFamily(rhdPtr->ChipSet) == RHD_FAMILY_RS690) {
	MC->FbLocation = RHDReadMC(rhdPtr, RS69_MCCFG_FB_LOCATION);
    } else {
	MC->FbLocation = RHDRegRead(rhdPtr, R6XX_MC_VM_FB_LOCATION);
	MC->MiscOffset = RHDRegRead(rhdPtr, R6XX_HDP_NONSURFACE_BASE);
    }
    MC->Stored = TRUE;
}

/*
 * Restore MC VM state.
 */
void
RHDRestoreMC(RHDPtr rhdPtr)
{
    struct rhdMC *MC = rhdPtr->MC;

    RHDFUNC(rhdPtr);

    if (!MC)
	return;

    if (!MC->Stored) {
	xf86DrvMsg(rhdPtr->scrnIndex, X_ERROR,
		   "%s: trying to restore uninitialized values.\n",__func__);
	return;
    }
    if (rhdPtr->ChipSet < RHD_RS690) {
	if (RHDFamily(rhdPtr->ChipSet) == RHD_FAMILY_RV515)
	    RHDWriteMC(rhdPtr, MC_IND_ALL |  RV515_MC_FB_LOCATION,
		       MC->FbLocation);
	else
	    RHDWriteMC(rhdPtr, MC_IND_ALL | R5XX_MC_FB_LOCATION,
		       MC->FbLocation);
    } else if (RHDFamily(rhdPtr->ChipSet) == RHD_FAMILY_RS690) {
	RHDWriteMC(rhdPtr,  RS69_MCCFG_FB_LOCATION, MC->FbLocation);
    } else {
	RHDRegWrite(rhdPtr, R6XX_MC_VM_FB_LOCATION, MC->FbLocation);
	RHDRegWrite(rhdPtr, R6XX_HDP_NONSURFACE_BASE, MC->MiscOffset);
    }
}

void
RHDMCSetup(RHDPtr rhdPtr)
{
    struct rhdMC *MC = rhdPtr->MC;
    CARD32 fb_location, fb_location_tmp, fb_offset_tmp;
    CARD16 fb_size;

    RHDFUNC(rhdPtr);

    if (!MC)
	return;

    if (rhdPtr->ChipSet < RHD_RS600) {
	unsigned int reg;

	if (RHDFamily(rhdPtr->ChipSet) == RHD_FAMILY_RV515)
	    reg = RV515_MC_FB_LOCATION | MC_IND_ALL;
	else
	    reg = R5XX_MC_FB_LOCATION | MC_IND_ALL;

	fb_location = RHDReadMC(rhdPtr, reg);
	fb_size = (fb_location >> 16) - (fb_location & 0xFFFF);
	fb_location_tmp = rhdPtr->FbIntAddress >> 16;
	fb_location_tmp |= (fb_location_tmp + fb_size) << 16;

	RHDDebug(rhdPtr->scrnIndex, "%s: fb_location: 0x%08X "
		 "[fb_size: 0x%04X] -> fb_location: 0x%08X\n",
		 __func__, (unsigned int)fb_location,
		 fb_size,(unsigned int)fb_location_tmp);
	RHDWriteMC(rhdPtr, reg, fb_location_tmp);
    } else if (rhdPtr->ChipSet < RHD_R600) {
	fb_location = RHDReadMC(rhdPtr, RS69_MCCFG_FB_LOCATION);
	fb_size = (fb_location >> 16) - (fb_location & 0xFFFF);
	fb_location_tmp = rhdPtr->FbIntAddress >> 16;
	fb_location_tmp |= (fb_location_tmp + fb_size) << 16;

	RHDDebug(rhdPtr->scrnIndex, "%s: fb_location: 0x%08X "
		 "[fb_size: 0x%04X] -> fb_location: 0x%08X\n",
		 __func__, (unsigned int)fb_location,
		 fb_size,(unsigned int)fb_location_tmp);
	RHDWriteMC(rhdPtr, RS69_MCCFG_FB_LOCATION,
		   fb_location_tmp);
    } else {
	fb_location = RHDRegRead(rhdPtr, R6XX_MC_VM_FB_LOCATION);
	fb_size = (fb_location >> 16) - (fb_location & 0xFFFF);
	fb_location_tmp = rhdPtr->FbIntAddress >> 24;
	fb_location_tmp |= (fb_location_tmp + fb_size) << 16;
	fb_offset_tmp = (rhdPtr->FbIntAddress >> 8) & 0xff0000;

	RHDDebug(rhdPtr->scrnIndex, "%s: fb_location: 0x%08X "
		 "fb_offset: 0x%08X [fb_size: 0x%04X] -> fb_location: 0x%08X "
		 "fb_offset: 0x%08X\n",
		 __func__, (unsigned int)fb_location,
		 RHDRegRead(rhdPtr,R6XX_HDP_NONSURFACE_BASE), fb_size,
		 (unsigned int)fb_location_tmp, (unsigned int)fb_offset_tmp);

	RHDRegWrite(rhdPtr, R6XX_MC_VM_FB_LOCATION, fb_location_tmp);
	RHDRegWrite(rhdPtr, R6XX_HDP_NONSURFACE_BASE, fb_offset_tmp);
    }
}

Bool
RHDMCIdle(RHDPtr rhdPtr, CARD32 count)
{
    RHDFUNC(rhdPtr);

    do {
	if (RHDFamily(rhdPtr->ChipSet) == RHD_FAMILY_RV515) {
	    if (RHDReadMC(rhdPtr, MC_IND_ALL | RV515_MC_STATUS) & RV515_MC_IDLE)
		return TRUE;
	} else if (rhdPtr->ChipSet < RHD_RS690) {
	    if (RHDReadMC(rhdPtr, MC_IND_ALL | R5XX_MC_STATUS) & R5XX_MC_IDLE)
		return TRUE;
	} else if (RHDFamily(rhdPtr->ChipSet) == RHD_FAMILY_RS690) {
	    if (RHDReadMC(rhdPtr, RS69_MC_SYSTEM_STATUS) & RS69_MC_SEQUENCER_IDLE)
		return TRUE;
	} else {
	    if (!(RHDRegRead(rhdPtr, R6_MCLK_PWRMGT_CNTL) & R6_MC_BUSY))
		return TRUE;
	}

	usleep(10);
    } while (count--);

    RHDDebug(rhdPtr->scrnIndex, "%s: MC not idle\n",__func__);

    return FALSE;
}
