
/*
 * Copyright (c) 2000-2003 by The XFree86 Project, Inc.
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
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#ifdef __UNIXOS2__
#define I_NEED_OS2_H
#endif

#include <sys/time.h>
#include <unistd.h> 
#include <X11/X.h>
#include "os.h"
#include "xf86.h"
#include "xf86Priv.h"
#include "xf86_OSlib.h"
#include "compiler.h"

_X_EXPORT void xf86Break1(void)
{
}

_X_EXPORT void xf86Break2(void)
{
}

_X_EXPORT void xf86Break3(void)
{
}

_X_EXPORT char *xf86p8bit[] = { "00000000", "00000001", "00000010", "00000011", 
		      "00000100", "00000101", "00000110", "00000111", 
		      "00001000", "00001001", "00001010", "00001011", 
		      "00001100", "00001101", "00001110", "00001111", 
		      "00010000", "00010001", "00010010", "00010011", 
		      "00010100", "00010101", "00010110", "00010111", 
		      "00011000", "00011001", "00011010", "00011011", 
		      "00011100", "00011101", "00011110", "00011111", 
		      "00100000", "00100001", "00100010", "00100011", 
		      "00100100", "00100101", "00100110", "00100111", 
		      "00101000", "00101001", "00101010", "00101011", 
		      "00101100", "00101101", "00101110", "00101111", 
		      "00110000", "00110001", "00110010", "00110011", 
		      "00110100", "00110101", "00110110", "00110111", 
		      "00111000", "00111001", "00111010", "00111011", 
		      "00111100", "00111101", "00111110", "00111111", 
		      "01000000", "01000001", "01000010", "01000011", 
		      "01000100", "01000101", "01000110", "01000111", 
		      "01001000", "01001001", "01001010", "01001011", 
		      "01001100", "01001101", "01001110", "01001111", 
		      "01010000", "01010001", "01010010", "01010011", 
		      "01010100", "01010101", "01010110", "01010111", 
		      "01011000", "01011001", "01011010", "01011011", 
		      "01011100", "01011101", "01011110", "01011111", 
		      "01100000", "01100001", "01100010", "01100011", 
		      "01100100", "01100101", "01100110", "01100111", 
		      "01101000", "01101001", "01101010", "01101011", 
		      "01101100", "01101101", "01101110", "01101111", 
		      "01110000", "01110001", "01110010", "01110011", 
		      "01110100", "01110101", "01110110", "01110111", 
		      "01111000", "01111001", "01111010", "01111011", 
		      "01111100", "01111101", "01111110", "01111111", 
		      "10000000", "10000001", "10000010", "10000011", 
		      "10000100", "10000101", "10000110", "10000111", 
		      "10001000", "10001001", "10001010", "10001011", 
		      "10001100", "10001101", "10001110", "10001111", 
		      "10010000", "10010001", "10010010", "10010011", 
		      "10010100", "10010101", "10010110", "10010111", 
		      "10011000", "10011001", "10011010", "10011011", 
		      "10011100", "10011101", "10011110", "10011111", 
		      "10100000", "10100001", "10100010", "10100011", 
		      "10100100", "10100101", "10100110", "10100111", 
		      "10101000", "10101001", "10101010", "10101011", 
		      "10101100", "10101101", "10101110", "10101111", 
		      "10110000", "10110001", "10110010", "10110011", 
		      "10110100", "10110101", "10110110", "10110111", 
		      "10111000", "10111001", "10111010", "10111011", 
		      "10111100", "10111101", "10111110", "10111111", 
		      "11000000", "11000001", "11000010", "11000011", 
		      "11000100", "11000101", "11000110", "11000111", 
		      "11001000", "11001001", "11001010", "11001011", 
		      "11001100", "11001101", "11001110", "11001111", 
		      "11010000", "11010001", "11010010", "11010011", 
		      "11010100", "11010101", "11010110", "11010111", 
		      "11011000", "11011001", "11011010", "11011011", 
		      "11011100", "11011101", "11011110", "11011111", 
		      "11100000", "11100001", "11100010", "11100011", 
		      "11100100", "11100101", "11100110", "11100111", 
		      "11101000", "11101001", "11101010", "11101011", 
		      "11101100", "11101101", "11101110", "11101111", 
		      "11110000", "11110001", "11110010", "11110011", 
		      "11110100", "11110101", "11110110", "11110111", 
		      "11111000", "11111001", "11111010", "11111011", 
		      "11111100", "11111101", "11111110", "11111111"
};

_X_EXPORT CARD32 xf86DummyVar1;
_X_EXPORT CARD32 xf86DummyVar2;
_X_EXPORT CARD32 xf86DummyVar3;

CARD8  xf86PeekFb8(CARD8  *p) { return *p; }
CARD16 xf86PeekFb16(CARD16 *p) { return *p; }
CARD32 xf86PeekFb32(CARD32 *p) { return *p; }
void xf86PokeFb8(CARD8  *p, CARD8  v) { *p = v; }
void xf86PokeFb16(CARD16 *p, CARD16 v) { *p = v; }
void xf86PokeFb32(CARD16 *p, CARD32 v) { *p = v; }

CARD8  xf86PeekMmio8(pointer Base, unsigned long Offset)
{
    return MMIO_IN8(Base,Offset);
}

CARD16 xf86PeekMmio16(pointer Base, unsigned long Offset)
{
    return MMIO_IN16(Base,Offset);
}

CARD32 xf86PeekMmio32(pointer Base, unsigned long Offset)
{
    return MMIO_IN32(Base,Offset);
}

void xf86PokeMmio8(pointer Base, unsigned long Offset, CARD8  v)
{
    MMIO_OUT8(Base,Offset,v);
}

void xf86PokeMmio16(pointer Base, unsigned long Offset, CARD16 v)
{
    MMIO_OUT16(Base,Offset,v);
}

void xf86PokeMmio32(pointer Base, unsigned long Offset, CARD32 v)
{
    MMIO_OUT32(Base,Offset,v);
}


_X_EXPORT void
xf86STimestamp(xf86TsPtr* timestamp)
{
    if (*timestamp) {
	gettimeofday((struct timeval*)*timestamp,NULL);
    } else {
	*timestamp = xnfalloc(sizeof(xf86TsRec));
	gettimeofday((struct timeval*)*timestamp,NULL);
    }
}

_X_EXPORT void
xf86SPTimestamp(xf86TsPtr* timestamp, char *str)
{
    if (*timestamp) {
	long diff;
	struct timeval ts;
	ts = **(struct timeval**)timestamp;
	gettimeofday((struct timeval*)*timestamp,NULL);
	if (ts.tv_usec > (*timestamp)->usec) 
	    diff = ((*timestamp)->sec - ts.tv_sec - 1) * 1000
		+ (ts.tv_usec - (*timestamp)->usec) / 1000;
	else
	    diff =  ((*timestamp)->sec - ts.tv_sec) * 1000
		+(- ts.tv_usec + (*timestamp)->usec) / 1000;
	ErrorF("%s Elapsed: %li\n",str,diff);
    } else {
	*timestamp = xnfalloc(sizeof(xf86TsRec));
	gettimeofday((struct timeval*)*timestamp,NULL);
    }
}
