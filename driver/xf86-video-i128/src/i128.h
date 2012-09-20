/*
 * Number Nine I128 functions
 *
 * Copyright 1996 The XFree86 Project, Inc.
 *
 * Author
 *		Robin Cutshaw
 *			robin@XFree86.Org
 */

#ifndef I128_H
#define I128_H

#include "i128pcirename.h"

#include "compiler.h"
#ifdef HAVE_XAA_H
#include "xaa.h"
#endif
#include "exa.h"
#include "xf86Cursor.h"
#include "vgaHW.h"
#include "colormapst.h"
#include "xf86DDC.h"
#include "i128reg.h"

#include "compat-api.h"
struct source_format {
    int render_format;
    int i128_format;
    int swap_flags;
    int ignore_alpha;
};

/* Card-specific driver information */

#define I128PTR(p) ((I128Ptr)((p)->driverPrivate))

typedef struct {
    EntityInfoPtr	pEnt;
    pciVideoPtr		PciInfo;
#ifndef XSERVER_LIBPCIACCESS
    PCITAG		PciTag;
    xf86AccessRec	Access;
#endif
    int			Chipset;
    int                 ChipRev;
    Bool		Primary;

    /* Ramdac specific */
    int		RamdacType;
    Bool	DAC8Bit;
    Bool	DACSyncOnGreen;
    int		hotX;
    int		hotY;
    Bool	HWCursor;
    Bool	BlockCursor;
    Bool	ReloadCursor;
    Bool	CursorNeedsInit;
    int		CursorStartX;
    int		CursorStartY;
    int		CursorLines;
    int		AdjustCursorXPos;
    int		CursGeneration;

    /* layout specific */
    int bitsPerPixel;
    int depth;
    rgb weight;
    int displayWidth;
    int displayOffset;
    DisplayModePtr mode;

    /* accel specific */
    CARD32              buf_ctrl;
    CARD32		blitdir;
    CARD32              planemask;
    CARD32		cmd;
    CARD32		rop; /* XXX XAA only */
    CARD32		clptl;
    CARD32		clpbr;
    CARD32              sorg;
    CARD32              sptch;
    CARD32              dorg;
    CARD32              dptch;
    CARD32              wh;
    CARD32              torg;
    CARD32              tptch;
    CARD32              tex_ctl;
    CARD32              threedctl;
    CARD32              acntrl;
    struct source_format *source;
    /* struct dest_format *dest; */
    
    Bool		NoAccel;
    Bool                exa;
    Bool		FlatPanel;
    Bool		DoubleScan;
    Bool		ShowCache;
    Bool		ModeSwitched;
    Bool		Debug;
    unsigned char	*MemoryPtr;
    int			MemorySize;
    int			MemoryType;
    volatile struct i128mem	mem;
    struct i128io	io;
    I128RegRec		RegRec;
    Bool		StateSaved;
    Bool		Initialized;
    Bool		FontsSaved;
    Bool		LUTSaved;
    Bool		InitCursorFlag;
    LUTENTRY		lutorig[256];
    LUTENTRY		lutcur[256];
    int			HDisplay;
    int			maxClock;
    int			minClock;

    CloseScreenProcPtr  CloseScreen;
#ifdef HAVE_XAA_H
    XAAInfoRecPtr	XaaInfoRec;
#endif
    ExaDriverPtr        ExaDriver;
    xf86CursorInfoPtr	CursorInfoRec;
    I2CBusPtr		I2C;
    Bool		DGAactive;
    int			DGAViewportStatus;
    int			numDGAModes;
    DGAModePtr		DGAModes;
    Bool		(*ProgramDAC)(ScrnInfoPtr, DisplayModePtr);
    unsigned int	(*ddc1Read)(ScrnInfoPtr);
    Bool		(*i2cInit)(ScrnInfoPtr);

    OptionInfoPtr	Options;

} I128Rec, *I128Ptr;


/* Prototypes */

void I128AdjustFrame(ADJUST_FRAME_ARGS_DECL);
Bool I128SwitchMode(SWITCH_MODE_ARGS_DECL);

Bool I128HWCursorInit(ScreenPtr pScreen);

Bool I128XaaInit(ScreenPtr pScreen);
Bool I128ExaInit(ScreenPtr pScreen);
void I128EngineDone(ScrnInfoPtr pScrn);

Bool I128Init(ScrnInfoPtr pScrn, DisplayModePtr mode);

Bool I128DGAInit(ScreenPtr pScreen);

void I128LoadPalette(ScrnInfoPtr pScrn, int numColors,
	int *indices, LOCO *colors, VisualPtr pVisual);

void I128SaveState(ScrnInfoPtr pScrn);
void I128RestoreState(ScrnInfoPtr pScrn);
void I128InitLUT(I128Ptr pI128);

Bool I128RestoreCursor(ScrnInfoPtr pScrn);
Bool I128RepositionCursor(ScrnInfoPtr pScrn);

Bool I128IBMHWCursorInit(ScrnInfoPtr pScrn);
Bool I128TIHWCursorInit(ScrnInfoPtr pScrn);

Bool I128ProgramTi3025(ScrnInfoPtr pScrn, DisplayModePtr mode);
Bool I128ProgramIBMRGB(ScrnInfoPtr pScrn, DisplayModePtr mode);
Bool I128ProgramSilverHammer(ScrnInfoPtr pScrn, DisplayModePtr mode);

/* void I128DumpBaseRegisters(ScrnInfoPtr pScrn); */
void I128DumpActiveRegisters(ScrnInfoPtr pScrn);
/* void I128DumpIBMDACRegisters(ScrnInfoPtr pScrn, volatile CARD32 *vrbg); */
 
#endif
