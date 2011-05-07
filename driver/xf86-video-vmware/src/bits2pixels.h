/* **********************************************************
 * Copyright (C) 1999-2001 VMware, Inc.
 * All Rights Reserved
 * **********************************************************/

/*
 * bits2pixels.h --
 *
 *      Drawing emulation routines
 */

#ifndef _BITS2PIXELS_H_
#define _BITS2PIXELS_H_

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

void
vmwareRaster_BitsToPixels(uint8 *bits, uint32 bits_increment,
			  uint8 *pix, uint32 pix_increment, int bytes_per_pixel,
			  uint32 width, uint32 height, uint32 fg, uint32 bg);

#endif /* _BITS4PIXELS_H_ */
