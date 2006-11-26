#if !defined( lint ) && !defined( SABER )
/* #ident        "@(#)ras.h      4.14 99/06/17 xlockmore" */

#endif

/*-
 * Utilities for Sun rasterfile processing
 *
 * Copyright (c) 1995 by Tobias Gloth
 *
 * See xlock.c for copying information.
 *
 * Revision History:
 * 12-Dec-95: Modified to be a used in more than one mode
 *            <joukj@hrem.stm.tudelft.nl>
 * 22-May-95: Written.
 */

#include "xlockimage.h"

#define RasterColorError   1
#define RasterSuccess      0
#define RasterOpenFailed  -1
#define RasterFileInvalid -2
#define RasterNoMemory    -3
#define RasterColorFailed -4

extern int  RasterFileToImage(ModeInfo * mi, char *filename, XImage ** image ,
			      Colormap m_cmap );
