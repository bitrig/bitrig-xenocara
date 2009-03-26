/* Header:   //Mercury/Projects/archives/XFree86/4.0/smi_dac.c-arc   1.8   27 Nov 2000 15:47:08   Frido  $ */

/*
Copyright (C) 1994-1998 The XFree86 Project, Inc.  All Rights Reserved.
Copyright (C) 2000 Silicon Motion, Inc.  All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the names of the XFree86 Project and
Silicon Motion shall not be used in advertising or otherwise to promote the
sale, use or other dealings in this Software without prior written
authorization from the XFree86 Project and Silicon Motion.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "smi.h"

#define BASE_FREQ	14.31818	/* MHz */

void
SMI_CommonCalcClock(int scrnIndex, long freq, int min_m, int min_n1, 
		    int max_n1, int min_n2, int max_n2, long freq_min, 
		    long freq_max, unsigned char *mdiv, unsigned char *ndiv)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    SMIPtr pSmi = SMIPTR(pScrn);
    double div, diff, best_diff;
    unsigned int m;
    unsigned char n1, n2;
    unsigned char best_n1 = 63, best_n2 = 3, best_m = 255;

    double ffreq     = freq     / 1000.0 / BASE_FREQ;
    double ffreq_min = freq_min / 1000.0 / BASE_FREQ;
    double ffreq_max = freq_max / 1000.0 / BASE_FREQ;

    if (ffreq < ffreq_min / (1 << max_n2)) {
	xf86DrvMsg(scrnIndex,X_WARNING,"invalid frequency %1.3f MHz  [freq >= %1.3f MHz]\n",
		ffreq * BASE_FREQ, ffreq_min * BASE_FREQ / (1 << max_n2));
	ffreq = ffreq_min / (1 << max_n2);
    }
    if (ffreq > ffreq_max / (1 << min_n2)) {
	xf86DrvMsg(scrnIndex,X_WARNING,"invalid frequency %1.3f MHz  [freq <= %1.3f MHz]\n",
		ffreq * BASE_FREQ, ffreq_max * BASE_FREQ / (1 << min_n2));
	ffreq = ffreq_max / (1 << min_n2);
    }

    /* work out suitable timings */
    best_diff = ffreq;

    for (n2 = min_n2; n2 <= max_n2; n2++) {
	for (n1 = min_n1; n1 <= max_n1; n1++) {
	    m = (int)(ffreq * n1 * (1 << n2) + 0.5);
	    if ( (m < min_m) || (m > 255) ) {
		continue;
	    }
	    div = (double)(m) / (double)(n1);
	    if ( (div >= ffreq_min) && (div <= ffreq_max) ) {
		diff = ffreq - div / (1 << n2);
		if (diff < 0.0) {
		    diff = -diff;
		}
		if (diff < best_diff) {
		    best_diff = diff;
		    best_m    = m;
		    best_n1   = n1;
		    best_n2   = n2;
		}
	    }
	}
    }

    DEBUG("Clock parameters for %1.6f MHz: m=%d, n1=%d, n2=%d\n",
	  ((double)(best_m) / (double)(best_n1) / (1 << best_n2)) * BASE_FREQ,
	  best_m, best_n1, best_n2);

    if (SMI_LYNX_SERIES(pSmi->Chipset)) {
	/* Prefer post scalar enabled for even denominators */
	if (freq < 70000 && max_n2 > 0 &&
	    best_n2 == 0 && best_n1 % 2 == 0){
	    best_n1 >>= 1;
	    best_n2 = 1;
	}

	*ndiv = best_n1 |
	    (best_n2 & 0x1) << 7 |
	    (best_n2 & 0x2) >> 1 <<6;
    } else {
	*ndiv = best_n1 | (best_n2 << 7);

	/* Enable second VCO */
	if (freq > 120000)
	    *ndiv |= 1 << 6;
    }

    *mdiv = best_m;
}

