/* -*- Mode: C; tab-width: 4 -*- */
/* blot --- Rorschach's ink blot test */

#if !defined( lint ) && !defined( SABER )
static const char sccsid[] = "@(#)blot.c	5.00 2000/11/01 xlockmore";

#endif

/*-
 * Copyright (c) 1992 by Jamie Zawinski
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appear in all copies and that
 * both that copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * This file is provided AS IS with no warranties of any kind.  The author
 * shall have no liability with respect to the infringement of copyrights,
 * trade secrets or any patents by this file or any part thereof.  In no
 * event will the author be liable for any lost revenue or profits or
 * other special, indirect and consequential damages.
 *
 * 01-Nov-2000: Allocation checks
 * 10-May-1997: Compatible with xscreensaver
 * 05-Jan-1995: patch for Dual-Headed machines from Greg Onufer
 *              <Greg.Onufer@Eng.Sun.COM>
 * 07-Dec-1994: now randomly has xsym, ysym, or both.
 * 02-Sep-1993: xlock version David Bagley <bagleyd@tux.org>
 * 1992:        xscreensaver version Jamie Zawinski <jwz@jwz.org>
 */

/*-
 * original copyright
 * Copyright (c) 1992 by Jamie Zawinski
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 */

#ifdef STANDALONE
#define MODE_blot
#define PROGCLASS "Blot"
#define HACK_INIT init_blot
#define HACK_DRAW draw_blot
#define blot_opts xlockmore_opts
#define DEFAULTS "*delay: 2000000 \n" \
 "*count: 6 \n" \
 "*cycles: 30 \n" \
 "*ncolors: 200 \n"
#include "xlockmore.h"		/* in xscreensaver distribution */
#else /* STANDALONE */
#include "xlock.h"		/* in xlockmore distribution */
#endif /* STANDALONE */

#ifdef MODE_blot

ModeSpecOpt blot_opts =
{0, (XrmOptionDescRec *) NULL, 0, (argtype *) NULL, (OptionStruct *) NULL};

#ifdef USE_MODULES
const ModStruct blot_description =
{"blot", "init_blot", "draw_blot", "release_blot",
 "refresh_blot", "init_blot", (char *) NULL, &blot_opts,
 200000, 6, 30, 1, 64, 0.3, "",
 "Shows Rorschach's ink blot test", 0, NULL};

#endif

typedef struct {
	int         width;
	int         height;
	int         xmid, ymid;
	int         offset;
	int         xsym, ysym;
	int         size;
	int         pix;
	int         count;
	XPoint     *pointBuffer;
	unsigned int pointBufferSize;
} blotstruct;

static blotstruct *blots = (blotstruct *) NULL;

void
init_blot(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	blotstruct *bp;

	if (blots == NULL) {
		if ((blots = (blotstruct *) calloc(MI_NUM_SCREENS(mi),
					       sizeof (blotstruct))) == NULL)
			return;
	}
	bp = &blots[MI_SCREEN(mi)];

	bp->width = MI_WIDTH(mi);
	bp->height = MI_HEIGHT(mi);
	bp->xmid = bp->width / 2;
	bp->ymid = bp->height / 2;

	bp->offset = 4;
	bp->ysym = (int) LRAND() & 1;
	bp->xsym = (bp->ysym) ? (int) LRAND() & 1 : 1;
	if (MI_NPIXELS(mi) > 2)
		bp->pix = NRAND(MI_NPIXELS(mi));
	if (bp->offset <= 0)
		bp->offset = 3;
	if (MI_COUNT(mi) < 0)
		bp->size = NRAND(-MI_COUNT(mi) + 1);
	else
		bp->size = MI_COUNT(mi);

	/* Fudge the size so it takes up the whole screen */
	bp->size *= (bp->width / 32 + 1) * (bp->height / 32 + 1);
	if (!bp->pointBuffer || bp->pointBufferSize < bp->size * sizeof (XPoint)) {
		if (bp->pointBuffer != NULL)
			free(bp->pointBuffer);
		bp->pointBufferSize = bp->size * sizeof (XPoint);
		if ((bp->pointBuffer = (XPoint *) malloc(bp->pointBufferSize)) ==
				NULL) {
			return;
		}
	}
	MI_CLEARWINDOW(mi);
	XSetForeground(display, MI_GC(mi), MI_WHITE_PIXEL(mi));
	bp->count = 0;
}

void
draw_blot(ModeInfo * mi)
{
	blotstruct *bp;
	XPoint     *xp;
	int         x, y, k;

	if (blots == NULL)
		return;
	bp = &blots[MI_SCREEN(mi)];
	xp = bp->pointBuffer;
	if (xp == NULL)
		init_blot(mi);

	MI_IS_DRAWN(mi) = True;
	if (MI_NPIXELS(mi) > 2) {
		XSetForeground(MI_DISPLAY(mi), MI_GC(mi), MI_PIXEL(mi, bp->pix));
		if (++bp->pix >= MI_NPIXELS(mi))
			bp->pix = 0;
	}
	x = bp->xmid;
	y = bp->ymid;
	k = bp->size;
	while (k >= 4) {
		x += (NRAND(1 + (bp->offset << 1)) - bp->offset);
		y += (NRAND(1 + (bp->offset << 1)) - bp->offset);
		k--;
		xp->x = x;
		xp->y = y;
		xp++;
		if (bp->xsym) {
			k--;
			xp->x = bp->width - x;
			xp->y = y;
			xp++;
		}
		if (bp->ysym) {
			k--;
			xp->x = x;
			xp->y = bp->height - y;
			xp++;
		}
		if (bp->xsym && bp->ysym) {
			k--;
			xp->x = bp->width - x;
			xp->y = bp->height - y;
			xp++;
		}
	}
	XDrawPoints(MI_DISPLAY(mi), MI_WINDOW(mi), MI_GC(mi),
		    bp->pointBuffer, (bp->size - k), CoordModeOrigin);
	if (++bp->count > MI_CYCLES(mi))
		init_blot(mi);
}


void
release_blot(ModeInfo * mi)
{
	if (blots != NULL) {
		int         screen;

		for (screen = 0; screen < MI_NUM_SCREENS(mi); screen++) {
			blotstruct *bp = &blots[screen];

			if (bp->pointBuffer != NULL) {
				free(bp->pointBuffer);
				/* bp->pointBuffer == NULL; */
			}
		}
		free(blots);
		blots = (blotstruct *) NULL;
	}
}

void
refresh_blot(ModeInfo * mi)
{
	MI_CLEARWINDOW(mi);
}

#endif /* MODE_blot */
