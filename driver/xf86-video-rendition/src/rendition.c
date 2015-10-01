/*
 * Copyright (C) 1998 The XFree86 Project, Inc.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the XFree86 Project shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from the
 * XFree86 Project.
 */

/*
 * This is essentially a transfer of the 3.3 sources written by 
 * Marc Langenbach and Tim Rowley.
 *
 * The initial port of this driver to XFree86 4.0 was done by
 * Marc Langenbach <mlangen@studcs.uni-sb.de>
 * Additions, updates and bugfixes by Dejan Ilic <dejan.ilic@home.se>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*
 * includes 
 */

#include "rendition.h"
#include "rendition_options.h"

#include "hwcursor.h"
#include "xf86int10.h"

#include "vtypes.h"
#include "vboard.h"
#include "vmodes.h"
#include "vramdac.h"
#include "rendition_shadow.h"
#include "vbe.h"

#ifdef XSERVER_LIBPCIACCESS
# include <pciaccess.h>
# define DEVICE_ID(p)  (p)->device_id
#else
# define DEVICE_ID(p)  (p)->chipType
#endif


/*
 * defines
 */

#undef DEBUG

#define RENDITION_NAME            "RENDITION"
#define RENDITION_DRIVER_NAME     "rendition"
#define RENDITION_VERSION_NAME    PACKAGE_VERSION
#define RENDITION_VERSION_MAJOR   PACKAGE_VERSION_MAJOR
#define RENDITION_VERSION_MINOR   PACKAGE_VERSION_MINOR
#define RENDITION_PATCHLEVEL      PACKAGE_VERSION_PATCHLEVEL
#define RENDITION_VERSION_CURRENT ((RENDITION_VERSION_MAJOR << 24) | \
                 (RENDITION_VERSION_MINOR << 16) | RENDITION_PATCHLEVEL)

/*
 * Constants for the (theoretical) maximum width and height that can
 * be used to display data on the CRT.  These were calculated from 
 * the HORZ and VERT macors, respectively, in vmodes.c.
 */
static const int MAX_HDISPLAY = 2048;
static const int MAX_VDISPLAY = 2048;

/*
 * Constants for the (theoretical) maximum line length of a scan line
 * and scan lines per screen (including overdraw).  These were 
 * calculated from the HORZ and VERT macors, respectively, in vmodes.c.
 */
static const int MAX_HTOTAL   = 2880;
static const int MAX_VTOTAL   = 2184;

/* 
 * local function prototypes
 */

static const OptionInfoRec * renditionAvailableOptions(int, int);
static void       renditionIdentify(int);
#ifdef XSERVER_LIBPCIACCESS
static Bool renditionPciProbe(DriverPtr drv, int entity_num,
    struct pci_device *dev, intptr_t match_data);
#else
static Bool       renditionProbe(DriverPtr, int);
#endif
static Bool       renditionPreInit(ScrnInfoPtr, int);
static Bool       renditionScreenInit(SCREEN_INIT_ARGS_DECL);
static Bool       renditionSwitchMode(SWITCH_MODE_ARGS_DECL);
static void       renditionAdjustFrame(ADJUST_FRAME_ARGS_DECL);
static Bool       renditionEnterVT(VT_FUNC_ARGS_DECL);
static void       renditionLeaveVT(VT_FUNC_ARGS_DECL);
static void       renditionFreeScreen(FREE_SCREEN_ARGS_DECL);

static ModeStatus renditionValidMode(SCRN_ARG_TYPE, DisplayModePtr, Bool, int);
static Bool renditionMapMem(ScrnInfoPtr pScreenInfo);
static Bool renditionUnmapMem(ScrnInfoPtr pScreenInfo);
#if 0
static xf86MonPtr renditionDDC(ScrnInfoPtr pScreenInfo);
static unsigned int renditionDDC1Read (ScrnInfoPtr pScreenInfo);
#endif
static xf86MonPtr renditionProbeDDC(ScrnInfoPtr pScrn, int index);

static void renditionLoadPalette(ScrnInfoPtr, int, int *, LOCO *, VisualPtr);
static renditionPtr renditionGetRec(ScrnInfoPtr pScreenInfo);


/* 
 * global data
 */

OptionInfoRec const renditionOptions[]={
    { OPTION_FBWC,      "FramebufferWC", OPTV_BOOLEAN, {0}, FALSE },
    { OPTION_SW_CURSOR, "SW_Cursor", OPTV_BOOLEAN, {0}, FALSE },
    { OPTION_NOACCEL,   "NoAccel",  OPTV_BOOLEAN, {0}, FALSE },
    { OPTION_OVERCLOCK_MEM,"Overclock_Mem",  OPTV_BOOLEAN, {0}, FALSE },
    { OPTION_NO_DDC,    "NoDDC",    OPTV_BOOLEAN, {0}, FALSE },
    { OPTION_SHADOW_FB, "ShadowFB", OPTV_BOOLEAN, {0}, FALSE },
    { OPTION_ROTATE,    "Rotate",   OPTV_ANYSTR,  {0}, FALSE },
    { -1,                NULL,      OPTV_NONE,    {0}, FALSE }
};

enum renditionTypes {
    CHIP_RENDITION_V1000,
    CHIP_RENDITION_V2x00
};

/* supported chipsets */
static SymTabRec renditionChipsets[] = {
    {CHIP_RENDITION_V1000, "V1000"},
    {CHIP_RENDITION_V2x00, "V2x00"},
    {-1,                   NULL}
};

#ifdef XSERVER_LIBPCIACCESS
#define RENDITION_DEVICE_MATCH(d, i) \
    { 0x1163, (d), PCI_MATCH_ANY, PCI_MATCH_ANY, 0, 0, (i) }

static const struct pci_id_match rendition_device_match[] = {
    RENDITION_DEVICE_MATCH(PCI_CHIP_V1000, CHIP_RENDITION_V1000),
    RENDITION_DEVICE_MATCH(PCI_CHIP_V2x00, CHIP_RENDITION_V2x00),

    { 0, 0, 0 }
};
#else
static PciChipsets renditionPCIchipsets[] = {
  { CHIP_RENDITION_V1000, PCI_CHIP_V1000, RES_SHARED_VGA },
  { CHIP_RENDITION_V2x00, PCI_CHIP_V2x00, RES_SHARED_VGA },
  { -1,                   -1,             RES_UNDEFINED }
};
#endif

_X_EXPORT DriverRec RENDITION={
    RENDITION_VERSION_CURRENT,
    "rendition",
    renditionIdentify,
#ifdef XSERVER_LIBPCIACCESS
    NULL,
#else
    renditionProbe,
#endif
    renditionAvailableOptions,
    NULL,
    0,
    NULL,

#ifdef XSERVER_LIBPCIACCESS
    rendition_device_match,
    renditionPciProbe
#endif
};

#ifdef XFree86LOADER

/* Module loader interface */

static MODULESETUPPROTO(renditionSetup);

static XF86ModuleVersionInfo renditionVersionRec = {
    RENDITION_DRIVER_NAME,
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    RENDITION_VERSION_MAJOR, RENDITION_VERSION_MINOR, RENDITION_PATCHLEVEL,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData renditionModuleData = 
               { &renditionVersionRec, renditionSetup, NULL };

static pointer
renditionSetup(pointer Module, pointer Options, int *ErrorMajor, 
               int *ErrorMinor)
{
    static Bool Initialised = FALSE;

    if (!Initialised) {
        Initialised = TRUE;
        xf86AddDriver(&RENDITION, Module, 1);
        return (pointer) TRUE;
    }

    if (ErrorMajor)
        *ErrorMajor = LDR_ONCEONLY;

    return NULL;
}

#endif


/*
 * functions
 */

static const OptionInfoRec *
renditionAvailableOptions(int chipid, int busid)
{
    return renditionOptions;
}

static void
renditionIdentify(int flags)
{
    xf86PrintChipsets(RENDITION_NAME,
        "rendition driver (version " RENDITION_VERSION_NAME ") for chipsets",
        renditionChipsets);
}



#ifdef XSERVER_LIBPCIACCESS
static Bool
renditionPciProbe(DriverPtr drv, int entity_num, struct pci_device *dev,
		  intptr_t match_data)
{
    ScrnInfoPtr pScrn;


    /* Allocate a ScrnInfoRec and claim the slot */
    pScrn = xf86ConfigPciEntity(NULL, 0, entity_num, NULL, RES_SHARED_VGA,
				NULL, NULL, NULL, NULL);
    if (pScrn != NULL) {
	renditionPtr pRendition;


	pScrn->driverVersion = RENDITION_VERSION_CURRENT;
	pScrn->driverName    = RENDITION_DRIVER_NAME;
	pScrn->name          = RENDITION_NAME;
	pScrn->Probe         = NULL;
	pScrn->PreInit       = renditionPreInit;
	pScrn->ScreenInit    = renditionScreenInit;
	pScrn->SwitchMode    = renditionSwitchMode;
	pScrn->AdjustFrame   = renditionAdjustFrame;
	pScrn->EnterVT       = renditionEnterVT;
	pScrn->LeaveVT       = renditionLeaveVT;
	pScrn->FreeScreen    = renditionFreeScreen;
	pScrn->ValidMode     = renditionValidMode;

	/* allocate driver private structure */
	pRendition = renditionGetRec(pScrn);
	if (pRendition == NULL) {
	    return FALSE;
	}

	pRendition->pEnt = xf86GetEntityInfo(entity_num);
	pRendition->PciInfo = dev;
    }

    return (pScrn != NULL);
}

#else

/*
 * This function is called once, at the start of the first server generation to
 * do a minimal probe for supported hardware.
 */
static Bool
renditionProbe(DriverPtr drv, int flags)
{
    Bool foundScreen=FALSE;
    int numDevSections, numUsed;
    GDevPtr *devSections;
    int *usedChips;
    int c;

    /* Find the config file Device sections that match this
     * driver, and return if there are none. */
    if ((numDevSections=xf86MatchDevice(RENDITION_DRIVER_NAME, &devSections)) <= 0)
        return FALSE;
  
    /* PCI BUS */
    if (xf86GetPciVideoInfo()) {
        numUsed=xf86MatchPciInstances(RENDITION_DRIVER_NAME, PCI_VENDOR_RENDITION,
                    renditionChipsets, renditionPCIchipsets, 
                    devSections, numDevSections, drv, &usedChips);

	free(devSections);
	if (numUsed <= 0)
	    return FALSE;

        if (flags & PROBE_DETECT)
            foundScreen = TRUE;
        else for (c=0; c<numUsed; c++) {
            ScrnInfoPtr pScrn;
            /* Allocate a ScrnInfoRec and claim the slot */
            pScrn=NULL;
            if ((pScrn = xf86ConfigPciEntity(pScrn, 0,usedChips[c],
						   renditionPCIchipsets, NULL,
						   NULL, NULL, NULL, NULL))) {
						       
		pScrn->driverVersion=RENDITION_VERSION_CURRENT;
		pScrn->driverName   =RENDITION_DRIVER_NAME;
		pScrn->name         =RENDITION_NAME;
		pScrn->Probe        =renditionProbe;
		pScrn->PreInit      =renditionPreInit;
		pScrn->ScreenInit   =renditionScreenInit;
		pScrn->SwitchMode   =renditionSwitchMode;
		pScrn->AdjustFrame  =renditionAdjustFrame;
		pScrn->EnterVT      =renditionEnterVT;
		pScrn->LeaveVT      =renditionLeaveVT;
		pScrn->FreeScreen   =renditionFreeScreen;
		pScrn->ValidMode    =renditionValidMode;
		foundScreen=TRUE;
	    }
        }
	free(usedChips);
    }
    return foundScreen;
}
#endif

#if 0
static Bool
renditionClockSelect(ScrnInfoPtr pScreenInfo, int ClockNumber)
{
        vgaHWPtr pvgaHW = VGAHWPTR(pScreenInfo);
        static CARD8 save_misc;

        switch (ClockNumber)
        {
            case CLK_REG_SAVE:
                save_misc = inb(pvgaHW->PIOOffset + VGA_MISC_OUT_R);
                break;

            case CLK_REG_RESTORE:
                outb(pvgaHW->PIOOffset + VGA_MISC_OUT_W, save_misc);
                break;

            default:
                outb(pvgaHW->PIOOffset + VGA_MISC_OUT_W,
		     (save_misc & 0xF3) | ((ClockNumber << 2) & 0x0C));
                break;
        }

    return TRUE;
}
#endif

static renditionPtr
renditionGetRec(ScrnInfoPtr pScreenInfo)
{
#ifdef DEBUG
    ErrorF("GetRec ...!!!!\n");
    sleep(1);
#endif
    if (!pScreenInfo->driverPrivate)
        pScreenInfo->driverPrivate=calloc(sizeof(renditionRec), 1);

    /* perhaps some initialization? <ml> */

#ifdef DEBUG
    ErrorF("GetRec ...!!!!\n");
    sleep(1);
#endif
    return (renditionPtr)pScreenInfo->driverPrivate;
}


static void
renditionFreeRec(ScrnInfoPtr pScreenInfo)
{
#ifdef DEBUG
    ErrorF("FreeRec...!!!!\n");
    sleep(1);
#endif
    if (xf86LoaderCheckSymbol("vgaHWFreeHWRec"))
	vgaHWFreeHWRec(pScreenInfo);
    free(pScreenInfo->driverPrivate);
    pScreenInfo->driverPrivate=NULL;

#ifdef DEBUG
    ErrorF("FreeRec OK...!!!!\n");
    sleep(1);
#endif
}

#if 0
static void
renditionProtect(ScrnInfoPtr pScreenInfo, Bool On)
{
#ifdef DEBUG
    ErrorF("Protect...!!!!\n");
    sleep(1);
#endif

    vgaHWProtect(pScreenInfo, On);

#ifdef DEBUG
    ErrorF("Protect OK...!!!!\n");
    sleep(1);
#endif
}
#endif

static Bool
renditionSaveScreen(ScreenPtr pScreen, int mode)
{
#ifdef DEBUG
    ErrorF("Savescreen...!!!!\n");
    sleep(1);
#endif

    return vgaHWSaveScreen(pScreen, mode);
}

#if 0
static void
renditionBlankScreen(ScrnInfoPtr pScreenInfo, Bool Unblank)
{
#ifdef DEBUG
    ErrorF("Blankscreen...!!!!\n");
    sleep(1);
#endif

    vgaHWBlankScreen(pScreenInfo, Unblank);
#ifdef DEBUG
    ErrorF("Blankscreen OK...!!!!\n");
    sleep(1);
#endif
}
#endif


/*
 * This function is called once for each screen at the start of the first
 * server generation to initialise the screen for all server generations.
 */

static Bool
renditionPreInit(ScrnInfoPtr pScreenInfo, int flags)
{
    static ClockRange renditionClockRange = {NULL, 0, 135000, -1, FALSE, TRUE, 1, 1, 0};
    MessageType       From;
    int               videoRam, Rounding, nModes = 0;
    renditionPtr      pRendition;
    char             *in_string;
    vgaHWPtr          pvgaHW;
    
#ifdef DEBUG
    ErrorF("Rendition: renditionPreInit() called\n");
#endif

    /* Check the number of entities, and fail if it isn't one. */
    if (pScreenInfo->numEntities != 1)
	return FALSE;

#ifndef XSERVER_LIBPCIACCESS
    /* allocate driver private structure */
    if (!renditionGetRec(pScreenInfo))
        return FALSE;
#endif

    pRendition=RENDITIONPTR(pScreenInfo);

#ifndef XSERVER_LIBPCIACCESS
    /* Get the entity, and make sure it is PCI. */
    pRendition->pEnt = xf86GetEntityInfo(pScreenInfo->entityList[0]);
    if (pRendition->pEnt->location.type != BUS_PCI)
	return FALSE;
#endif

    if (flags & PROBE_DETECT) {
        ConfiguredMonitor = 
	    renditionProbeDDC(pScreenInfo, pRendition->pEnt->index);
        return TRUE;
    }

    /* set the monitor */
    pScreenInfo->monitor=pScreenInfo->confScreen->monitor;

    /* Initialize the card through int10 interface if needed */
    if (xf86LoadSubModule(pScreenInfo, "int10")){
        xf86Int10InfoPtr pInt=NULL;

        xf86DrvMsg(pScreenInfo->scrnIndex, X_INFO, "Initializing int10\n");
        pInt = xf86InitInt10(pRendition->pEnt->index);
        xf86FreeInt10(pInt);
    }

#ifndef XSERVER_LIBPCIACCESS
    /* Find the PCI info for this screen */
    pRendition->PciInfo = xf86GetPciInfoForEntity(pRendition->pEnt->index);
    pRendition->pcitag= pciTag(pRendition->PciInfo->bus,
               pRendition->PciInfo->device, pRendition->PciInfo->func);

    /*
     * XXX This could be refined if some VGA memory resources are not
     * decoded in operating mode.
     */
    xf86SetOperatingState(resVgaMem, pRendition->pEnt->index, ResUnusedOpr);

    if (xf86RegisterResources(pRendition->pEnt->index, NULL, ResExclusive))
         return FALSE;


    /* Operations for which memory access is required. */
    pScreenInfo->racMemFlags = RAC_FB | RAC_CURSOR;
    /* Operations for which I/O access is required. (XXX Check this) */
    pScreenInfo->racIoFlags = RAC_FB | RAC_COLORMAP | RAC_CURSOR | RAC_VIEWPORT;
#endif
    /* determine depth, bpp, etc. */
    if (!xf86SetDepthBpp(pScreenInfo, 0, 0, 0, Support32bppFb))
        return FALSE;

    /* Verify that the color depth is supported. */
    switch( pScreenInfo->depth ) {

        case 8:
        case 16:
        case 24:
        {
            break;
        }

        case 15:
        {
            if (PCI_CHIP_V1000 == DEVICE_ID(pRendition->PciInfo)) {
                xf86DrvMsg( pScreenInfo->scrnIndex, X_ERROR,
                        "Given depth (%d) is not supported by this chipset.\n",
                        pScreenInfo->depth);
                return FALSE;
            }
        }

        default:
        {
            xf86DrvMsg( pScreenInfo->scrnIndex, X_ERROR,
                    "Given depth (%d) is not supported by this driver\n",
                    pScreenInfo->depth );
            return FALSE;
        }

    } /* End of switch( pScreenInfo->depth ) {*/


    /* Print the color depth and frame buffer bits per pixel. */
    xf86PrintDepthBpp( pScreenInfo );


    /* collect all of the options flags and process them */

    xf86CollectOptions(pScreenInfo, NULL);
    if (!(pRendition->Options = malloc(sizeof(renditionOptions))))
	return FALSE;
    memcpy(pRendition->Options, renditionOptions, sizeof(renditionOptions));
    xf86ProcessOptions(pScreenInfo->scrnIndex, pScreenInfo->options, 
        pRendition->Options);


    /* Load fb */
    if (!xf86LoadSubModule(pScreenInfo, "fb"))
      return FALSE;

    /* determine colour weights */
    pScreenInfo->rgbBits=8;
    
    if (pScreenInfo->depth > 8) {
      rgb defaultWeight = {0, 0, 0};
      rgb defaultMask = {0, 0, 0};

      xf86PrintDepthBpp(pScreenInfo);

      /* Standard defaults are OK if depths are OK */
      if (!xf86SetWeight(pScreenInfo, defaultWeight, defaultMask))
        return FALSE;
      else{
	/* XXX:  Check that returned weight is supported */
      }
    }

    /* determine default visual */
    if (!xf86SetDefaultVisual(pScreenInfo, -1))
      return FALSE;

    /* the gamma fields must be initialised when using the new cmap code */
    if (pScreenInfo->depth > 1) {
        Gamma zeros = {0.0, 0.0, 0.0};

        if (!xf86SetGamma(pScreenInfo, zeros))
            return FALSE;
    }

    /* the Rendition chips have a programmable clock */
    pScreenInfo->progClock=TRUE;

    /* set various fields according to the given options */
    /* to be filled in <ml> */

    if (PCI_CHIP_V1000 == DEVICE_ID(pRendition->PciInfo)) {
      pRendition->board.chip=V1000_DEVICE;
    }
    else {
      pRendition->board.chip=V2000_DEVICE;
      renditionClockRange.maxClock = 170000;
      renditionClockRange.clockIndex = -1;
    }

    if (!xf86LoadSubModule(pScreenInfo, "vgahw")){
        return FALSE;
    }

    if (!vgaHWGetHWRec(pScreenInfo))
        return FALSE;

    pvgaHW = VGAHWPTR(pScreenInfo);
    pvgaHW->MapSize = 0x00010000;       /* Standard 64kB VGA window */
    vgaHWSetStdFuncs(pvgaHW);
    vgaHWGetIOBase(pvgaHW);             /* Get VGA I/O base */

#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 12
    pRendition->board.vgaio_base = pvgaHW->PIOOffset;
#else
    pRendition->board.vgaio_base = 0;
#endif
    pRendition->board.io_base = pRendition->board.vgaio_base 
#ifdef XSERVER_LIBPCIACCESS
	+ pRendition->PciInfo->regions[1].base_addr;
#else
	+ pRendition->PciInfo->ioBase[1]
#endif
	;
    pRendition->board.mmio_base=0;
    pRendition->board.vmmio_base=0;
    pRendition->board.mem_size=0;
#ifndef XSERVER_LIBPCIACCESS
    pRendition->board.mem_base=(vu8 *)pRendition->PciInfo->memBase[0];
#endif
    pRendition->board.vmem_base=NULL;
    pRendition->board.init=0;

    if (pScreenInfo->chipset)
        xf86DrvMsg(pScreenInfo->scrnIndex, X_CONFIG, "Chipset: \"%s\".\n",
            pScreenInfo->chipset);
    else
        xf86DrvMsg(pScreenInfo->scrnIndex, X_PROBED, "Chipset: \"%s\".\n",
            renditionChipsets[
        pRendition->board.chip==V1000_DEVICE ? 0:1].name);

    /* I do not get the IO base addres <ml> */
    /* XXX Is this still true?  If so, the wrong base is being checked */
    xf86DrvMsg(pScreenInfo->scrnIndex, X_PROBED,
	       "Rendition %s @ %lx/%lx\n",
	       renditionChipsets[pRendition->board.chip==V1000_DEVICE ? 0:1]
	       .name,
#ifdef XSERVER_LIBPCIACCESS
	       pRendition->PciInfo->regions[1].base_addr,
	       pRendition->PciInfo->regions[0].base_addr
#else
	       pRendition->PciInfo->ioBase[1],
	       pRendition->PciInfo->memBase[0]
#endif
	       );

    /* First of all get a "clean" starting state */
    verite_resetboard(pScreenInfo);

    /* determine video ram -- to do so, we assume a full size memory of 16M,
     * then map it and use verite_getmemorysize() to determine the real 
     * amount of memory */
    pScreenInfo->videoRam = 16<<10;
    pRendition->board.mem_size = pScreenInfo->videoRam * 1024;
    renditionMapMem(pScreenInfo);

    videoRam=verite_getmemorysize(pScreenInfo)>>10;
    renditionUnmapMem(pScreenInfo);

    From = X_PROBED;
    xf86DrvMsg(pScreenInfo->scrnIndex, From, "videoRam: %d kBytes\n", videoRam);
    pScreenInfo->videoRam=videoRam;
    pRendition->board.mem_size=videoRam * 1024;

    /* Load the needed symbols */

    pRendition->board.shadowfb=TRUE;

    if ((in_string = xf86GetOptValString(pRendition->Options, OPTION_ROTATE))){
	if(!xf86NameCmp(in_string, "CW")) {
	    /* accel is disabled below for shadowFB */
	    pRendition->board.shadowfb = TRUE;
	    pRendition->board.rotate = 1;
	    xf86DrvMsg(pScreenInfo->scrnIndex, X_CONFIG,
		       "Rotating screen clockwise - acceleration disabled\n");
	} else if(!xf86NameCmp(in_string, "CCW")) {
	    pRendition->board.shadowfb = TRUE;
	    pRendition->board.rotate = -1;
	    xf86DrvMsg(pScreenInfo->scrnIndex, X_CONFIG,  "Rotating screen "
		       "counter clockwise - acceleration disabled\n");
	} else {
	    xf86DrvMsg(pScreenInfo->scrnIndex, X_CONFIG, 
		       "\"%s\" is not a valid value for Option \"Rotate\"\n",
		       in_string);
	    xf86DrvMsg(pScreenInfo->scrnIndex, X_INFO,
		       "Valid options are \"CW\" or \"CCW\"\n");
	}
    }
    
    if (xf86ReturnOptValBool(pRendition->Options, OPTION_SHADOW_FB,1)||
	pRendition->board.rotate) {
	if (!xf86LoadSubModule(pScreenInfo, "shadowfb")) {
	    xf86DrvMsg(pScreenInfo->scrnIndex, X_WARNING,
		       "Oops, \"ShadowFB\" module loading failed, disabling ShadowFB!\n");
	}
	else{
	    pRendition->board.shadowfb=TRUE;
	    xf86DrvMsg(pScreenInfo->scrnIndex, X_INFO,
		       "Using \"Shadow Framebuffer\"\n");
	}
    }
    else {
	pRendition->board.shadowfb=FALSE;
	xf86DrvMsg(pScreenInfo->scrnIndex, X_CONFIG,
		   "\"Shadow Framebuffer\" disabled\n");
    }


    /* Load Ramdac module if needed */
    if (!xf86ReturnOptValBool(pRendition->Options, OPTION_SW_CURSOR,0) &&
	!pRendition->board.rotate){
      if (!xf86LoadSubModule(pScreenInfo, "ramdac")) {
	return FALSE;
      }
    }

#if 0
    /* Load DDC module if needed */
    if (!xf86ReturnOptValBool(pRendition->Options, OPTION_NO_DDC,0)){
      if (!xf86LoadSubModule(pScreenInfo, "ddc")) {
	xf86DrvMsg(pScreenInfo->scrnIndex, X_ERROR,
		   ("Loading of DDC library failed, skipping DDC-probe\n"));
      }
      else {
	pScreenInfo->monitor->DDC = renditionDDC(pScreenInfo);
      }
    }
    else {
      xf86DrvMsg(pScreenInfo->scrnIndex, X_CONFIG,
		 ("Skipping DDC probe on users request\n"));
    }
#else
    /* Load DDC module if needed */
    if (!xf86ReturnOptValBool(pRendition->Options, OPTION_NO_DDC,0)){
      if (!xf86LoadSubModule(pScreenInfo, "ddc")) {
	xf86DrvMsg(pScreenInfo->scrnIndex, X_ERROR,
		   ("Loading of DDC library failed, skipping DDC-probe\n"));
      }
      else {
	  xf86MonPtr mon;
	  mon = renditionProbeDDC(pScreenInfo, pRendition->pEnt->index);
	  xf86PrintEDID(mon);
	  xf86SetDDCproperties(pScreenInfo, mon);
      }
    }
    else {
      xf86DrvMsg(pScreenInfo->scrnIndex, X_CONFIG,
		 ("Skipping DDC probe on users request\n"));
    }
#endif

    /* Set the virtual X rounding (in bits) */
    if (pScreenInfo->depth == 8)
        Rounding = 16 * 8;
    else
        Rounding = 16;

    /*
     * Validate the modes.  Note that the limits passed to
     * xf86ValidateModes() are VGA CRTC architectural limits.
     */
    pScreenInfo->maxHValue = MAX_HTOTAL;
    pScreenInfo->maxVValue = MAX_VTOTAL;
    nModes = xf86ValidateModes(pScreenInfo,
            pScreenInfo->monitor->Modes, pScreenInfo->display->modes,
            &renditionClockRange, NULL, 8, MAX_HDISPLAY, Rounding,
            1, MAX_VDISPLAY, pScreenInfo->display->virtualX,
            pScreenInfo->display->virtualY,
            0x10000, LOOKUP_CLOSEST_CLOCK | LOOKUP_CLKDIV2);

    if (nModes < 0)
        return FALSE;

    /* Remove invalid modes */
    xf86PruneDriverModes(pScreenInfo);

    /* Set CRTC values for the modes */
    xf86SetCrtcForModes(pScreenInfo, 0);

    /* Set current mode to the first in list */
    pScreenInfo->currentMode = pScreenInfo->modes;

    /* Print mode list */
    xf86PrintModes(pScreenInfo);

    /* Set display resolution */
    xf86SetDpi(pScreenInfo, 0, 0);

    /* Only one chipset here */
    if (!pScreenInfo->chipset)
        pScreenInfo->chipset = (char *)renditionChipsets[0].name;

    if(!xf86ReturnOptValBool(pRendition->Options, OPTION_SW_CURSOR,0)){
      if(!pRendition->board.rotate)
	/* Do preemtive things for HW cursor */
	RenditionHWCursorPreInit(pScreenInfo);
      else{
	xf86DrvMsg(pScreenInfo->scrnIndex, X_WARNING,
		   "Hardware cursor not supported on rotated screen\n");
	xf86DrvMsg(pScreenInfo->scrnIndex, X_INFO,
		   "Software cursor activated\n");
      }
    }
    else 
      xf86DrvMsg(pScreenInfo->scrnIndex, X_CONFIG,
		 "Software cursor selected\n");

#ifdef DEBUG
    ErrorF("PreInit OK...!!!!\n");
    sleep(2);
#endif

    return TRUE;        /* Tada! */
}


/* Save mode on server entry */
static void
renditionSave(ScrnInfoPtr pScreenInfo)
{
#ifdef DEBUG
    ErrorF("Save...!!!!\n");
    sleep(1);
#endif
    vgaHWSave(pScreenInfo, &VGAHWPTR(pScreenInfo)->SavedReg,VGA_SR_ALL);
	
#ifdef DEBUG
    ErrorF("Save OK...!!!!\n");
    sleep(1);
#endif
}

#if 0
/* Restore the mode that was saved on server entry */
static void
renditionRestore(ScrnInfoPtr pScreenInfo)
{
#ifdef DEBUG
    ErrorF("Restore...!!!!\n");
    sleep(1);
#endif

    vgaHWProtect(pScreenInfo, TRUE);
    vgaHWRestore(pScreenInfo, &VGAHWPTR(pScreenInfo)->SavedReg, VGA_SR_ALL);
    vgaHWProtect(pScreenInfo, FALSE);

    verite_setmode(pScreenInfo, &RENDITIONPTR(pScreenInfo)->mode);

#ifdef DEBUG
    ErrorF("Restore OK...!!!!\n");
    sleep(1);
#endif
}
#endif

/* Set a graphics mode */
static Bool
renditionSetMode(ScrnInfoPtr pScreenInfo, DisplayModePtr pMode)
{
    struct verite_modeinfo_t *modeinfo=&RENDITIONPTR(pScreenInfo)->mode;

#ifdef DEBUG
    ErrorF("RENDITION: renditionSetMode() called\n");
    ErrorF("Setmode...!!!!\n");
    sleep(1);
#endif

    /* construct a modeinfo for the verite_setmode function */
    modeinfo->clock=pMode->SynthClock;
    modeinfo->hdisplay=pMode->HDisplay;
    modeinfo->hsyncstart=pMode->HSyncStart;
    modeinfo->hsyncend=pMode->HSyncEnd;
    modeinfo->htotal=pMode->HTotal;
    modeinfo->hskew=pMode->HSkew;
    modeinfo->vdisplay=pMode->VDisplay;
    modeinfo->vsyncstart=pMode->VSyncStart;
    modeinfo->vsyncend=pMode->VSyncEnd;
    modeinfo->vtotal=pMode->VTotal;

    modeinfo->screenwidth = pMode->HDisplay;
    modeinfo->virtualwidth = pScreenInfo->virtualX & 0xfff8;
    modeinfo->screenheight = pMode->VDisplay;
    modeinfo->virtualheight = pScreenInfo->virtualY & 0xfff8;

    if ((pMode->Flags&(V_PHSYNC|V_NHSYNC)) 
        && (pMode->Flags&(V_PVSYNC|V_NVSYNC))) {
        modeinfo->hsynchi=((pMode->Flags&V_PHSYNC) == V_PHSYNC);
        modeinfo->vsynchi=((pMode->Flags&V_PVSYNC) == V_PVSYNC);
    }
    else {
        int VDisplay=pMode->VDisplay;
        if (pMode->Flags & V_DBLSCAN)
            VDisplay*=2;
        if (VDisplay < 400) {
            /* +hsync -vsync */
            modeinfo->hsynchi=1;
            modeinfo->vsynchi=0;
        }
        else if (VDisplay < 480) {
            /* -hsync +vsync */
            modeinfo->hsynchi=0;
            modeinfo->vsynchi=1;
        }
        else if (VDisplay < 768) {
            /* -hsync -vsync */
            modeinfo->hsynchi=0;
            modeinfo->vsynchi=0;
        }
        else {
            /* +hsync +vsync */
            modeinfo->hsynchi=1;
            modeinfo->vsynchi=1;
        }
    }

    switch (pScreenInfo->bitsPerPixel) {
        case 8:
            modeinfo->bitsperpixel=8;
            modeinfo->pixelformat=V_PIXFMT_8I;
            break;
        case 16:
            modeinfo->bitsperpixel=16;
            if (pScreenInfo->weight.green == 5)
                /* on a V1000, this looks too 'red/magenta' <ml> */
                modeinfo->pixelformat=V_PIXFMT_1555;
            else
                modeinfo->pixelformat=V_PIXFMT_565;
            break;
        case 32:
            modeinfo->bitsperpixel=32;
            modeinfo->pixelformat=V_PIXFMT_8888;
            break;
    }
    modeinfo->fifosize=128;
    modeinfo->flags=pMode->Flags;

    verite_setmode(pScreenInfo,&RENDITIONPTR(pScreenInfo)->mode);
#ifdef DEBUG
    ErrorF("Setmode OK...!!!!\n");
    sleep(1);
#endif
    return TRUE;
}

static void
renditionLeaveGraphics(ScrnInfoPtr pScreenInfo)
{
    renditionPtr pRendition = RENDITIONPTR(pScreenInfo);

#ifdef DEBUG
    ErrorF("RENDITION: renditionLeaveGraphics() called\n");
    sleep(1);
#endif
    verite_restore(pScreenInfo, &pRendition->saveRegs);

    vgaHWProtect(pScreenInfo, TRUE);
    vgaHWRestore(pScreenInfo, &VGAHWPTR(pScreenInfo)->SavedReg, VGA_SR_ALL);
    vgaHWProtect(pScreenInfo, FALSE);

    vgaHWLock(VGAHWPTR(pScreenInfo));

#ifdef DEBUG
    ErrorF("Leavegraphics OK...!!!!\n");
    sleep(1);
#endif
}


/* Unravel the screen */
static Bool
renditionCloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
    ScrnInfoPtr pScreenInfo = xf86ScreenToScrn(pScreen);
    renditionPtr prenditionPriv=renditionGetRec(pScreenInfo);
    Bool Closed = TRUE;

#ifdef DEBUG
    ErrorF("RENDITION: renditionCloseScreen() called\n");
    sleep(1);
#endif

    if (prenditionPriv->board.hwcursor_used)
	RenditionHWCursorRelease(pScreenInfo);

    if (pScreenInfo->vtSema)
	renditionLeaveGraphics(pScreenInfo);

    pScreenInfo->vtSema = FALSE;

    if (prenditionPriv 
	&& (pScreen->CloseScreen = prenditionPriv->CloseScreen)) {
        prenditionPriv->CloseScreen = NULL;
        Closed = (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);
    }
    
#ifdef DEBUG
    ErrorF("Closescreen OK...!!!!\n");
    sleep(1);
#endif
    return Closed;
}


static void
renditionDPMSSet(ScrnInfoPtr pScreen, int mode, int flags)
{
#ifdef DEBUG
    ErrorF("RENDITION: renditionDPMSSet() called\n");
#endif

    vgaHWDPMSSet(pScreen, mode, flags);
}

static Bool
renditionScreenInit(SCREEN_INIT_ARGS_DECL)
{
    ScrnInfoPtr pScreenInfo = xf86ScreenToScrn(pScreen);
    renditionPtr pRendition = RENDITIONPTR(pScreenInfo);
    Bool Inited = FALSE;
    unsigned char *FBBase;
    VisualPtr visual;
    vgaHWPtr pvgaHW;
    int displayWidth,width,height;
    int scrnIndex = pScreenInfo->scrnIndex;

#ifdef DEBUG
    ErrorF("RENDITION: renditionScreenInit() called\n");
    sleep(1);
#endif
    /* Get vgahw private     */
    pvgaHW = VGAHWPTR(pScreenInfo);

    /* Get driver private */
    pRendition=renditionGetRec(pScreenInfo);

    /* Save the current state and setup the current mode */
    renditionSave(pScreenInfo);

    /* Map VGA aperture */
    if (!vgaHWMapMem(pScreenInfo))
        return FALSE;

    if (!renditionMapMem(pScreenInfo))
	return FALSE;

    /* Unlock VGA registers */
    vgaHWUnlock(pvgaHW);

    verite_save(pScreenInfo);

    pScreenInfo->vtSema = TRUE;

    if (!renditionSetMode(pScreenInfo, pScreenInfo->currentMode))
        return FALSE;

    /* blank the screen */
    renditionSaveScreen(pScreen, SCREEN_SAVER_ON);

    (*pScreenInfo->AdjustFrame)(ADJUST_FRAME_ARGS(pScreenInfo,
						  pScreenInfo->frameX0, pScreenInfo->frameY0));


    miClearVisualTypes();

    if (!miSetVisualTypes(pScreenInfo->depth,
			  miGetDefaultVisualMask(pScreenInfo->depth),
			  pScreenInfo->rgbBits, pScreenInfo->defaultVisual))
	return FALSE;

    miSetPixmapDepths ();
	
    if (pRendition->board.rotate) {
	height = pScreenInfo->virtualX;
	width = pScreenInfo->virtualY;
    } else {
	width = pScreenInfo->virtualX;
	height = pScreenInfo->virtualY;
    }

    if(pRendition->board.shadowfb) {
	pRendition->board.shadowPitch 
	    = BitmapBytePad(pScreenInfo->bitsPerPixel * width);
	pRendition->board.shadowPtr 
	    = malloc(pRendition->board.shadowPitch * height);
	displayWidth = pRendition->board.shadowPitch 
	    / (pScreenInfo->bitsPerPixel >> 3);
	FBBase = pRendition->board.shadowPtr;
    } else {
	pRendition->board.shadowPtr = NULL;
	FBBase = pRendition->board.vmem_base+pRendition->board.fbOffset;
	displayWidth=pScreenInfo->displayWidth;
    }

    Inited = fbScreenInit(pScreen, FBBase,
			  width, height,
			  pScreenInfo->xDpi, pScreenInfo->yDpi,
			  displayWidth,
			  pScreenInfo->bitsPerPixel);
    
    if (!Inited)
        return FALSE;

    if (pScreenInfo->bitsPerPixel > 8) {
        /* Fixup RGB ordering */
        visual=pScreen->visuals+pScreen->numVisuals;
        while (--visual >= pScreen->visuals) {
	    if ((visual->class | DynamicClass) == DirectColor){
		visual->offsetRed = pScreenInfo->offset.red;
		visual->offsetGreen = pScreenInfo->offset.green;
		visual->offsetBlue = pScreenInfo->offset.blue;
		visual->redMask = pScreenInfo->mask.red;
		visual->greenMask = pScreenInfo->mask.green;
		visual->blueMask = pScreenInfo->mask.blue;
	    }
	}
    }

    /* must be after RGB ordering fixed */
    fbPictureInit (pScreen, 0, 0);

    xf86SetBlackWhitePixels(pScreen);
   
    /*********************************************************/
    /* The actual setup of the driver-specific code          */
    /* has to be after fbScreenInit and before cursor init */
    /*********************************************************/

    /* Initialise cursor functions */
    xf86SetSilkenMouse(pScreen);
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

    if(!xf86ReturnOptValBool(pRendition->Options, OPTION_SW_CURSOR,0)&&
       !pRendition->board.rotate){
	/* Initialise HW cursor */
	if(!RenditionHWCursorInit(pScreen)){
	    xf86DrvMsg(pScreenInfo->scrnIndex, X_ERROR,
		       "Hardware Cursor initalization failed!!\n");
	}
    }

    if (pRendition->board.shadowfb) {
	RefreshAreaFuncPtr refreshArea = renditionRefreshArea;

	if(pRendition->board.rotate) {
	    if (!pRendition->board.PointerMoved) {
		pRendition->board.PointerMoved = pScreenInfo->PointerMoved;
		pScreenInfo->PointerMoved = renditionPointerMoved;
	    }

	    switch(pScreenInfo->bitsPerPixel) {
		case 8:         refreshArea = renditionRefreshArea8;  break;
		case 16:        refreshArea = renditionRefreshArea16; break;
		case 24:        refreshArea = renditionRefreshArea24; break;
		case 32:        refreshArea = renditionRefreshArea32; break;
	    }
	}

	ShadowFBInit(pScreen, refreshArea);
    }

    /* Setup default colourmap */
    if (!miCreateDefColormap(pScreen))
	return FALSE;

    /* Try the new code based on the new colormap layer */
    if (pScreenInfo->depth > 1)
	if (!xf86HandleColormaps(pScreen, 256, pScreenInfo->rgbBits,
				 renditionLoadPalette, NULL,
				 CMAP_RELOAD_ON_MODE_SWITCH)) {
	    xf86DrvMsg(pScreenInfo->scrnIndex, X_ERROR, 
		       "Colormap initialization failed\n");
	    return FALSE;
	}

    xf86DPMSInit(pScreen, renditionDPMSSet, 0);

    if (xf86ReturnOptValBool(pRendition->Options, OPTION_OVERCLOCK_MEM,0)) {
	pRendition->board.overclock_mem=TRUE;
    }

    /* Wrap the screen's CloseScreen vector and set its SaveScreen vector */
    pRendition->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = renditionCloseScreen;
    pScreen->SaveScreen = renditionSaveScreen;

    if (!Inited)
        renditionCloseScreen(CLOSE_SCREEN_ARGS);

    if (serverGeneration == 1)
	xf86ShowUnusedOptions(pScreenInfo->scrnIndex, pScreenInfo->options);

#ifdef DEBUG
    ErrorF("ScreenInit OK...!!!!\n");
    sleep(1);
#endif
    return Inited;
}

static Bool
renditionSwitchMode(SWITCH_MODE_ARGS_DECL)
{
    SCRN_INFO_PTR(arg);
#ifdef DEBUG
    ErrorF("RENDITION: renditionSwitchMode() called\n");
#endif
    return renditionSetMode(pScreenInfo, mode);
}


static void
renditionAdjustFrame(ADJUST_FRAME_ARGS_DECL)
{
    SCRN_INFO_PTR(arg);
    renditionPtr pRendition = RENDITIONPTR(pScreenInfo);
    int offset, virtualwidth, bitsPerPixel;

#ifdef DEBUG
    ErrorF("RENDITION: renditionAdjustFrame() called\n");
#endif

    bitsPerPixel=pScreenInfo->bitsPerPixel;
    virtualwidth=pRendition->mode.virtualwidth;
    offset=(y*virtualwidth+x)*(bitsPerPixel>>3);

    offset+= pRendition->board.fbOffset;

#ifdef DEBUG
    ErrorF ("MOVING SCREEN %d bytes!!\n",offset);
#endif
    verite_setframebase(pScreenInfo, offset);
}


static Bool
renditionEnterVT(VT_FUNC_ARGS_DECL)
{
    SCRN_INFO_PTR(arg);
    vgaHWPtr pvgaHW = VGAHWPTR(pScreenInfo);

#ifdef DEBUG
    ErrorF("RENDITION: renditionEnterVT() called\n");
#endif

    /* Map VGA aperture */
    if (!vgaHWMapMem(pScreenInfo))
        return FALSE;

    /* Unlock VGA registers */
    vgaHWUnlock(pvgaHW);

    if (!renditionSetMode(pScreenInfo, pScreenInfo->currentMode))
        return FALSE;

    (*pScreenInfo->AdjustFrame)(ADJUST_FRAME_ARGS(pScreenInfo,
						  pScreenInfo->frameX0, pScreenInfo->frameY0));

    return TRUE;
}


static void
renditionLeaveVT(VT_FUNC_ARGS_DECL)
{
    SCRN_INFO_PTR(arg);
#ifdef DEBUG
    ErrorF("RENDITION: renditionLeaveVT() called\n");
#endif
    renditionLeaveGraphics(pScreenInfo);
}


static void
renditionFreeScreen(FREE_SCREEN_ARGS_DECL)
{
    SCRN_INFO_PTR(arg);
    renditionFreeRec(pScreenInfo);
}


static ModeStatus
renditionValidMode(SCRN_ARG_TYPE arg, DisplayModePtr pMode, Bool Verbose, 
		   int flags)
{
    if (pMode->Flags & V_INTERLACE)
        return MODE_NO_INTERLACE;

    return MODE_OK;
}

static Bool
renditionMapMem(ScrnInfoPtr pScreenInfo)
{
    Bool WriteCombine;
    int mapOption;
    renditionPtr pRendition = RENDITIONPTR(pScreenInfo);
#ifdef XSERVER_LIBPCIACCESS
    int err;
#endif

#ifdef DEBUG
    ErrorF("Mapping ...\n");
#ifndef XSERVER_LIBPCIACCESS
    ErrorF("%d %d %d %x %d\n", pScreenInfo->scrnIndex, VIDMEM_FRAMEBUFFER, 
	   pRendition->pcitag,
	   pRendition->board.mem_base, pScreenInfo->videoRam * 1024);
#endif
#endif

    if (pRendition->board.chip == V1000_DEVICE){
	/* Some V1000 boards are known to have problems with Write-Combining */
	/* V2x00 also found to have similar problems with memcpy & WC ! */
	WriteCombine = 0;
    } else {
	/* Activate Write_Combine if possible */
	WriteCombine = 1;
    }
       /* Override on users request */
    WriteCombine
	= xf86ReturnOptValBool(pRendition->Options, OPTION_FBWC, WriteCombine);
#ifdef XSERVER_LIBPCIACCESS
    mapOption = PCI_DEV_MAP_FLAG_WRITABLE;
    if (WriteCombine)
	mapOption |= PCI_DEV_MAP_FLAG_WRITE_COMBINE;

    err = pci_device_map_range(pRendition->PciInfo,
			       pRendition->PciInfo->regions[0].base_addr,
			       pRendition->PciInfo->regions[0].size,
			       mapOption, (void *)&pRendition->board.vmem_base);

    return (err == 0);
#else
    if (WriteCombine) {
	xf86DrvMsg(pScreenInfo->scrnIndex, X_CONFIG,
		   ("Requesting Write-Combined memory access\n"));
	mapOption = VIDMEM_FRAMEBUFFER;
    } else {
	xf86DrvMsg(pScreenInfo->scrnIndex, X_CONFIG,
		   ("Requesting MMIO-style memory access\n"));
	mapOption = VIDMEM_MMIO;
    }

    pRendition->board.vmem_base=
        xf86MapPciMem(pScreenInfo->scrnIndex, mapOption,
		      pRendition->pcitag,
		      (unsigned long)pRendition->board.mem_base,
		      pScreenInfo->videoRam * 1024);
    return TRUE;
#endif
    
#ifdef DEBUG0
    ErrorF("Done\n");
#endif
}

static Bool
renditionUnmapMem(ScrnInfoPtr pScreenInfo)
{
  renditionPtr pRendition = RENDITIONPTR(pScreenInfo);
#ifdef DEBUG
  ErrorF("Unmapping ...\n");
#endif

#ifndef XSERVER_LIBPCIACCESS
    xf86UnMapVidMem(pScreenInfo->scrnIndex,
        pRendition->board.vmem_base, 
		    pScreenInfo->videoRam * 1024);
#else
    pci_device_unmap_range(pRendition->PciInfo, 
			   pRendition->board.vmem_base,
			   pRendition->PciInfo->regions[0].size);
#endif
    return TRUE;
#ifdef DEBUG0
    ErrorF("Done\n");
#endif
}

static void
renditionLoadPalette(ScrnInfoPtr pScreenInfo, int numColors,
		     int *indices, LOCO *colors,
		     VisualPtr pVisual)
{
  verite_setpalette(pScreenInfo, numColors, indices, colors, pVisual);
}

xf86MonPtr
renditionProbeDDC(ScrnInfoPtr pScreenInfo, int index)
{
  vbeInfoPtr pVbe;
  xf86MonPtr mon = NULL;

  if (xf86LoadSubModule(pScreenInfo, "vbe")) {
    pVbe = VBEInit(NULL,index);
    mon = vbeDoEDID(pVbe, NULL);
    vbeFree(pVbe);
  }
  return mon;
}

# if 0
static xf86MonPtr
renditionDDC (ScrnInfoPtr pScreenInfo)
{
  renditionPtr pRendition = RENDITIONPTR(pScreenInfo);
  unsigned long iob=pRendition->board.io_base;
  vu32 temp;

  xf86MonPtr MonInfo = NULL;
  temp = verite_in32(iob+CRTCCTL); /* Remember original value */

  /* Enable DDC1 */
  verite_out32(iob+CRTCCTL,(temp|
		       CRTCCTL_ENABLEDDC|
		       CRTCCTL_VSYNCENABLE|
		       CRTCCTL_VIDEOENABLE));

  MonInfo = xf86DoEDID_DDC1(pScreenInfo->scrnIndex,
			    vgaHWddc1SetSpeed,
			    renditionDDC1Read );

  verite_out32(iob+CRTCCTL,temp); /* return the original values */

  xf86DrvMsg(pScreenInfo->scrnIndex, X_INFO,
	     "DDC Monitor info: %p\n", MonInfo);

  xf86PrintEDID( MonInfo );
  xf86DrvMsg(pScreenInfo->scrnIndex, X_INFO,
	     "end of DDC Monitor info\n\n");

  /* xf86SetDDCproperties(pScreenInfo, MonInfo); */
  return MonInfo;
}

static unsigned int
renditionDDC1Read (ScrnInfoPtr pScreenInfo)
{
  renditionPtr pRendition = RENDITIONPTR(pScreenInfo);
  unsigned long iob=pRendition->board.io_base;
  vu32 value = 0;

  /* wait for Vsync */
  while (!(verite_in32(iob+CRTCSTATUS) & CRTCSTATUS_VERT_SYNC));
  while (verite_in32(iob+CRTCSTATUS) & CRTCSTATUS_VERT_SYNC);

  /* Read the value */
  value = verite_in32(iob+CRTCCTL) & CRTCCTL_DDCDATA;
  return value;
}

#endif
