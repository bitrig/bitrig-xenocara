/* -*- Mode: C; tab-width: 4 -*- */
/* forest --- binary trees in a fractal forest */

#if !defined( lint ) && !defined( SABER )
static const char sccsid[] = "@(#)forest.c	5.00 2000/11/01 xlockmore";

#endif

/*-
 * Copyright (c) 1995 Pascal Pensa <pensa@aurora.unice.fr>
 *
 * Original idea : Guillaume Ramey <ramey@aurora.unice.fr>
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
 * Revision History:
 * 01-Nov-2000: Allocation checks
 * 10-May-1997: Compatible with xscreensaver
 *
 */

#ifdef STANDALONE
#define MODE_forest
#define PROGCLASS "Forest"
#define HACK_INIT init_forest
#define HACK_DRAW draw_forest
#define forest_opts xlockmore_opts
#define DEFAULTS "*delay: 400000 \n" \
 "*count: 100 \n" \
 "*cycles: 200 \n" \
 "*ncolors: 100 \n"
#define UNIFORM_COLORS
#include "xlockmore.h"		/* in xscreensaver distribution */
#else /* STANDALONE */
#include "xlock.h"		/* in xlockmore distribution */

#endif /* STANDALONE */

#ifdef MODE_forest

ModeSpecOpt forest_opts =
{0, (XrmOptionDescRec *) NULL, 0, (argtype *) NULL, (OptionStruct *) NULL};

#ifdef USE_MODULES
ModStruct   forest_description =
{"forest", "init_forest", "draw_forest", "release_forest",
 "refresh_forest", "init_forest", (char *) NULL, &forest_opts,
 400000, 100, 200, 1, 64, 1.0, "",
 "Shows binary trees of a fractal forest", 0, NULL};

#endif

#define MINTREES   1

#define MINHEIGHT  20		/* Tree height range */
#define MAXHEIGHT  40

#define MINANGLE   15		/* (degree) angle between soon */
#define MAXANGLE   35
#define RANDANGLE  15		/* (degree) Max random angle from default */

#define REDUCE     90		/* Height % from father */

#define ITERLEVEL  10		/* Tree iteration */

#define COLORSPEED  2		/* Color increment */

/* degree to radian */
#define DEGTORAD(x) (((float)(x)) * M_PI / 180.0)

#define RANGE_RAND(min,max) ((min) + NRAND((max) - (min)))

typedef struct {
	int         width;
	int         height;
	int         time;	/* up time */
	int         ntrees;
} foreststruct;

static foreststruct *forests = (foreststruct *) NULL;

static void
draw_tree(ModeInfo * mi,
	  short int x, short int y, short int len,
	  float a, float as, short int c, short int level)
				/* Father's end */
				/* Length */
				/* color */
				/* Height level */
				/* Father's angle */
				/* Father's angle step */
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	GC          gc = MI_GC(mi);
	short       x_1, y_1, x_2, y_2;
	float       a1, a2;

	/* left */

	a1 = a + as + DEGTORAD(NRAND(2 * RANDANGLE) - RANDANGLE);

	x_1 = x + (short) (COSF(a1) * ((float) len));
	y_1 = y + (short) (SINF(a1) * ((float) len));

	/* right */

	a2 = a - as + DEGTORAD(NRAND(2 * RANDANGLE) - RANDANGLE);

	x_2 = x + (short) (COSF(a2) * ((float) len));
	y_2 = y + (short) (SINF(a2) * ((float) len));

	if (MI_NPIXELS(mi) > 2) {
		XSetForeground(display, gc, MI_PIXEL(mi, c));
		c = (c + COLORSPEED) % MI_NPIXELS(mi);
	} else
		XSetForeground(display, gc, MI_WHITE_PIXEL(mi));

	XDrawLine(display, window, gc, x, y, x_1, y_1);
	XDrawLine(display, window, gc, x, y, x_2, y_2);

	if (level < 2) {
		XDrawLine(display, window, gc, x + 1, y, x_1 + 1, y_1);
		XDrawLine(display, window, gc, x + 1, y, x_2 + 1, y_2);
	}
	len = (len * REDUCE * 10) / 1000;

	if (level < ITERLEVEL) {
		draw_tree(mi, x_1, y_1, len, a1, as, c, level + 1);
		draw_tree(mi, x_2, y_2, len, a2, as, c, level + 1);
	}
}

void
init_forest(ModeInfo * mi)
{
	foreststruct *fp;

	if (forests == NULL) {
		if ((forests = (foreststruct *) calloc(MI_NUM_SCREENS(mi),
					     sizeof (foreststruct))) == NULL)
			return;
	}
	fp = &forests[MI_SCREEN(mi)];

	fp->width = MI_WIDTH(mi);
	fp->height = MI_HEIGHT(mi);
	fp->time = 0;

	fp->ntrees = MI_COUNT(mi);
	if (fp->ntrees < -MINTREES)
		fp->ntrees = NRAND(-fp->ntrees - MINTREES + 1) + MINTREES;
	else if (fp->ntrees < MINTREES)
		fp->ntrees = MINTREES;

	MI_CLEARWINDOW(mi);
}

void
draw_forest(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	GC          gc = MI_GC(mi);
	short       x, y, x_2, y_2, len, c = 0;
	float       a, as;
	foreststruct *fp;

	if (forests == NULL)
		return;
	fp = &forests[MI_SCREEN(mi)];

	MI_IS_DRAWN(mi) = True;
	if (fp->time < fp->ntrees) {

		x = RANGE_RAND(0, fp->width);
		y = RANGE_RAND(0, fp->height + MAXHEIGHT);
		a = -M_PI / 2.0 + DEGTORAD(NRAND(2 * RANDANGLE) - RANDANGLE);
		as = DEGTORAD(RANGE_RAND(MINANGLE, MAXANGLE));
		len = ((RANGE_RAND(MINHEIGHT, MAXHEIGHT) * (fp->width / 20)) / 50) + 2;

		if (MI_NPIXELS(mi) > 2) {
			c = NRAND(MI_NPIXELS(mi));
			XSetForeground(display, gc, MI_PIXEL(mi, c));
			c = (c + COLORSPEED) % MI_NPIXELS(mi);
		} else
			XSetForeground(display, gc, MI_WHITE_PIXEL(mi));

		x_2 = x + (short) (COSF(a) * ((float) len));
		y_2 = y + (short) (SINF(a) * ((float) len));

		XDrawLine(display, MI_WINDOW(mi), gc, x, y, x_2, y_2);
		XDrawLine(display, MI_WINDOW(mi), gc, x + 1, y, x_2 + 1, y_2);

		draw_tree(mi, x_2, y_2, (len * REDUCE) / 100, a, as, c, 1);
	}
	if (++fp->time > MI_CYCLES(mi)) {
		init_forest(mi);
	}
}

void
release_forest(ModeInfo * mi)
{
	if (forests != NULL) {
		free(forests);
		forests = (foreststruct *) NULL;
	}
}

void
refresh_forest(ModeInfo * mi)
{
	MI_CLEARWINDOW(mi);
}

#endif /* MODE_forest */
