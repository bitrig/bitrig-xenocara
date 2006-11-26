/*
 * Copyright 2004-2005 The Unichrome Project  [unichrome.sf.net]
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef _VIA_VGAHW_H_
#define _VIA_VGAHW_H_

#include "vgaHW.h"

/* not used currently */
/*
CARD8 ViaVgahwIn(vgaHWPtr hwp, int address);
void ViaVgahwOut(vgaHWPtr hwp, int address, CARD8 value);

CARD8 ViaVgahwRead(vgaHWPtr hwp, int indexaddress, CARD8 index, 
		   int valueaddress);
*/

void ViaVgahwWrite(vgaHWPtr hwp, int indexaddress, CARD8 index,
		  int valueaddress, CARD8 value);

void ViaVgahwMask(vgaHWPtr hwp, int indexaddress, CARD8 index,
			int valueaddress, CARD8 value, CARD8 mask);

void ViaCrtcMask(vgaHWPtr hwp, CARD8 index, CARD8 value, CARD8 mask);
void ViaSeqMask(vgaHWPtr hwp, CARD8 index, CARD8 value, CARD8 mask);
void ViaGrMask(vgaHWPtr hwp, CARD8 index, CARD8 value, CARD8 mask);

#ifdef HAVE_DEBUG
void ViaVgahwPrint(vgaHWPtr hwp);
#endif /* HAVE_DEBUG */

#endif /* _VIA_VGAHW_H_ */
