/*
 * Cg3 framebuffer - defines.
 *
 * Copyright (C) 2000 Jakub Jelinek (jakub@redhat.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * JAKUB JELINEK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/* $XFree86:$ */

#ifndef CG3_H
#define CG3_H

#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86RamDac.h"
#include <X11/Xmd.h>
#include "gcstruct.h"
#include "xf86sbusBus.h"

/* Various offsets in virtual (ie. mmap()) spaces Linux and Solaris support. */
#define CG3_RAM_VOFF	0x4000000

typedef struct {
	unsigned char	*fb;
	int		width;
	int		height;

	sbusDevicePtr	psdp;
	CloseScreenProcPtr CloseScreen;
} Cg3Rec, *Cg3Ptr;

#define GET_CG3_FROM_SCRN(p)    ((Cg3Ptr)((p)->driverPrivate))

#endif /* CG3_H */
