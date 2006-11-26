/* -*- Mode: C; tab-width: 4 -*- */
/* spline --- spline fun */

#if !defined( lint ) && !defined( SABER )
static const char sccsid[] = "@(#)spline.c	5.00 2000/11/01 xlockmore";

#endif

/*-
 * Copyright (c) 1992 by Jef Poskanzer
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
 * 01-Nov-2000:
 * 10-May-1997: Compatible with xscreensaver
 * 13-Sep-1996: changed FOLLOW to a runtime option "-erase"
 * 17-Jan-1996: added compile time option, FOLLOW to erase old splines like Qix
 *              thanks to Richard Duran <rduran@cs.utep.edu>
 * 09-Mar-1995: changed how batchcount is used
 * 02-Sep-1993: xlock version: David Bagley <bagleyd@tux.org>
 *              reminds me of a great "Twilight Zone" episode.
 * 1992: X11 version Jef Poskanzer <jef@netcom.com>, <jef@well.sf.ca.us>
 *
 * spline fun #3
 */

/*-
 * original copyright
 * xsplinefun.c - X11 version of spline fun #3
 * Displays colorful moving splines in the X11 root window.
 * Copyright (C) 1992 by Jef Poskanzer
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  This software is provided "as is" without express or
 * implied warranty.
 */

#ifdef STANDALONE
#define MODE_spline
#define PROGCLASS "Spline"
#define HACK_INIT init_spline
#define HACK_DRAW draw_spline
#define spline_opts xlockmore_opts
#define DEFAULTS "*delay: 30000 \n" \
 "*count: -6 \n" \
 "*cycles: 2048 \n" \
 "*ncolors: 200 \n" \
 "*fullrandom: True \n"
#define UNIFORM_COLORS
#define BRIGHT_COLORS
#include "xlockmore.h"		/* in xscreensaver distribution */
#else /* STANDALONE */
#include "xlock.h"		/* in xlockmore distribution */

#endif /* STANDALONE */

#ifdef MODE_spline

#define DEF_ERASE  "False"

static Bool erase;

static XrmOptionDescRec opts[] =
{
	{(char *) "-erase", (char *) ".spline.erase", XrmoptionNoArg, (caddr_t) "on"},
	{(char *) "+erase", (char *) ".spline.erase", XrmoptionNoArg, (caddr_t) "off"}
};

static argtype vars[] =
{
	{(void *) & erase, (char *) "erase", (char *) "Erase", (char *) DEF_ERASE, t_Bool}
};

static OptionStruct desc[] =
{
	{(char *) "-/+erase", (char *) "turn on/off the following erase of splines"}
};

ModeSpecOpt spline_opts =
{sizeof opts / sizeof opts[0], opts, sizeof vars / sizeof vars[0], vars, desc};

#ifdef USE_MODULES
ModStruct   spline_description =
{"spline", "init_spline", "draw_spline", "release_spline",
 "refresh_spline", "init_spline", (char *) NULL, &spline_opts,
 30000, -6, 2048, 1, 64, 0.3, "",
 "Shows colorful moving splines", 0, NULL};

#endif

#define MINPOINTS 3

/* How many segments to draw per cycle when redrawing */
#define REDRAWSTEP 3

#define SPLINE_THRESH 5

typedef struct {
	XPoint      pos, delta;
} splinepointstruct;

typedef struct {
	/* ERASE stuff */
	int         first;
	int         last;
	int         max_delta;
	XPoint    **splineq;
	int         redrawing, redrawpos;
	Bool        erase;

	int         width;
	int         height;
	int         color;
	int         points;
	int         nsplines;
	splinepointstruct *pt;
} splinestruct;

static splinestruct *splines = (splinestruct *) NULL;

static void XDrawSpline(Display * display, Drawable d, GC gc,
			int x0, int y0, int x1, int y1, int x2, int y2);

static void
free_spline(splinestruct *sp)
{

	if (sp->pt != NULL) {
		free(sp->pt);
		sp->pt = (splinepointstruct *) NULL;
	}
	if (sp->splineq != NULL) {
		int         spline;

		for (spline = 0; spline < sp->nsplines; ++spline)
			if (sp->splineq[spline] != NULL)
				free(sp->splineq[spline]);
		free(sp->splineq);
		sp->splineq = (XPoint **) NULL;
	}
}

void
init_spline(ModeInfo * mi)
{
	int         i;
	splinestruct *sp;

	if (splines == NULL) {
		if ((splines = (splinestruct *) calloc(MI_NUM_SCREENS(mi),
				sizeof (splinestruct))) == NULL)
			return;
	}
	sp = &splines[MI_SCREEN(mi)];

	sp->width = MI_WIDTH(mi);
	sp->height = MI_HEIGHT(mi);
	/* batchcount is the upper bound on the number of points */
	sp->points = MI_COUNT(mi);
	if (sp->points < -MINPOINTS) {
		if (sp->pt) {
			free(sp->pt);
			sp->pt = (splinepointstruct *) NULL;
		}
		if (sp->erase) {
			if (sp->splineq)
				for (i = 0; i < sp->nsplines; ++i)
					if (sp->splineq[i]) {
						free(sp->splineq[i]);
						sp->splineq[i] = (XPoint *) NULL;
					}
		}
		sp->points = NRAND(-sp->points - MINPOINTS + 1) + MINPOINTS;
	} else if (sp->points < MINPOINTS)
		sp->points = MINPOINTS;
	if (MI_IS_FULLRANDOM(mi))
		sp->erase = (Bool) (LRAND() & 1);
	else
		sp->erase = erase;
	if (sp->pt == NULL)
		if ((sp->pt = (splinepointstruct *) malloc(sp->points *
				sizeof (splinepointstruct))) == NULL) {
			free_spline(sp);
			return;
		}

	if (sp->erase) {
		sp->max_delta = 16;
		sp->redrawing = 0;
		sp->last = 0;
		sp->nsplines = MI_CYCLES(mi) / 64 + 1;	/* So as to be compatible */
		if (sp->splineq == NULL)
			if ((sp->splineq = (XPoint **) calloc(sp->nsplines,
					sizeof (XPoint *))) == NULL) {
				free_spline(sp);
				return;
			}
		for (i = 0; i < sp->nsplines; ++i)
			if (sp->splineq[i] == NULL)
				if ((sp->splineq[i] = (XPoint *) calloc(sp->points,
						sizeof (XPoint))) == NULL) {
					free_spline(sp);
					return;
				}
	} else {
		sp->max_delta = 3;
		sp->nsplines = 0;
	}

	MI_CLEARWINDOW(mi);

	/* Initialize points. */
	for (i = 0; i < sp->points; ++i) {
		sp->pt[i].pos.x = NRAND(sp->width);
		sp->pt[i].pos.y = NRAND(sp->height);
		sp->pt[i].delta.x = NRAND(sp->max_delta * 2) - sp->max_delta;
		if (sp->pt[i].delta.x <= 0 && sp->width > 1)
			--sp->pt[i].delta.x;
		sp->pt[i].delta.y = NRAND(sp->max_delta * 2) - sp->max_delta;
		if (sp->pt[i].delta.y <= 0 && sp->height > 1)
			--sp->pt[i].delta.y;
	}
	if (MI_NPIXELS(mi) > 2)
		sp->color = NRAND(MI_NPIXELS(mi));
}

void
draw_spline(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	GC          gc = MI_GC(mi);
	int         i, t, px, py, zx, zy, nx, ny;
	splinestruct *sp;

	if (splines == NULL)
		return;
	sp = &splines[MI_SCREEN(mi)];
	if (sp->pt == NULL)
		return;

	MI_IS_DRAWN(mi) = True;
	if (sp->erase)
		sp->first = (sp->last + 2) % sp->nsplines;

	/* Move the points. */
	for (i = 0; i < sp->points; i++) {
		for (;;) {
			t = sp->pt[i].pos.x + sp->pt[i].delta.x;
			if (t >= 0 && t < sp->width)
				break;
			sp->pt[i].delta.x = NRAND(sp->max_delta * 2) - sp->max_delta;
			if (sp->pt[i].delta.x <= 0 && sp->width > 1)
				--sp->pt[i].delta.x;
		}
		sp->pt[i].pos.x = t;
		for (;;) {
			t = sp->pt[i].pos.y + sp->pt[i].delta.y;
			if (t >= 0 && t < sp->height)
				break;
			sp->pt[i].delta.y = NRAND(sp->max_delta * 2) - sp->max_delta;
			if (sp->pt[i].delta.y <= 0 && sp->height > 1)
				--sp->pt[i].delta.y;
		}
		sp->pt[i].pos.y = t;
	}

	if (sp->erase) {
		XSetForeground(display, gc, MI_BLACK_PIXEL(mi));

		/* Erase first figure. */
		px = zx = (sp->splineq[sp->first][0].x +
			   sp->splineq[sp->first][sp->points - 1].x) / 2;
		py = zy = (sp->splineq[sp->first][0].y +
			   sp->splineq[sp->first][sp->points - 1].y) / 2;
		for (i = 0; i < sp->points - 1; ++i) {
			nx = (sp->splineq[sp->first][i + 1].x +
			      sp->splineq[sp->first][i].x) / 2;
			ny = (sp->splineq[sp->first][i + 1].y +
			      sp->splineq[sp->first][i].y) / 2;
			XDrawSpline(display, window, gc,
				    px, py, sp->splineq[sp->first][i].x,
				    sp->splineq[sp->first][i].y, nx, ny);
			px = nx;
			py = ny;
		}

		XDrawSpline(display, window, gc,
			    px, py, sp->splineq[sp->first][sp->points - 1].x,
			    sp->splineq[sp->first][sp->points - 1].y, zx, zy);
	}
	if (MI_NPIXELS(mi) > 2) {
		XSetForeground(display, gc, MI_PIXEL(mi, sp->color));
		if (++sp->color >= MI_NPIXELS(mi))
			sp->color = 0;
	} else
		XSetForeground(display, gc, MI_WHITE_PIXEL(mi));

	/* Draw the figure. */
	px = zx = (sp->pt[0].pos.x + sp->pt[sp->points - 1].pos.x) / 2;
	py = zy = (sp->pt[0].pos.y + sp->pt[sp->points - 1].pos.y) / 2;
	for (i = 0; i < sp->points - 1; ++i) {
		nx = (sp->pt[i + 1].pos.x + sp->pt[i].pos.x) / 2;
		ny = (sp->pt[i + 1].pos.y + sp->pt[i].pos.y) / 2;
		XDrawSpline(display, window, gc,
			    px, py, sp->pt[i].pos.x, sp->pt[i].pos.y, nx, ny);
		px = nx;
		py = ny;
	}

	XDrawSpline(display, window, gc, px, py,
	 sp->pt[sp->points - 1].pos.x, sp->pt[sp->points - 1].pos.y, zx, zy);

	if (sp->erase) {
		for (i = 0; i < sp->points; ++i) {
			sp->splineq[sp->last][i].x = sp->pt[i].pos.x;
			sp->splineq[sp->last][i].y = sp->pt[i].pos.y;
		}
		sp->last++;
		if (sp->last >= sp->nsplines)
			sp->last = 0;

		if (sp->redrawing) {
			int         j, k;

			sp->redrawpos++;
			/* This compensates for the changed sp->last
			   since last callback */

			for (k = 0; k < REDRAWSTEP; k++) {
				j = (sp->last - sp->redrawpos + sp->nsplines) %
					sp->nsplines;
#ifdef BACKWARDS
				/* Draw backwards, probably will not see it,
				   but its the thought ... */
				px = zx = (sp->splineq[j][0].x +
				       sp->splineq[j][sp->points - 1].x) / 2;
				py = zy = (sp->splineq[j][0].y +
				       sp->splineq[j][sp->points - 1].y) / 2;
				for (i = sp->points - 1; i > 0; --i) {
					nx = (sp->splineq[j][i - 1].x +
					      sp->splineq[j][i].x) / 2;
					ny = (sp->splineq[j][i - 1].y +
					      sp->splineq[j][i].y) / 2;
					XDrawSpline(display, window, gc,
						 px, py, sp->splineq[j][i].x,
						sp->splineq[j][i].y, nx, ny);
					px = nx;
					py = ny;
				}

				XDrawSpline(display, window, gc,
					    px, py, sp->splineq[j][0].x,
					    sp->splineq[j][0].y, zx, zy);
#else
				px = zx = (sp->splineq[j][0].x +
				       sp->splineq[j][sp->points - 1].x) / 2;
				py = zy = (sp->splineq[j][0].y +
				       sp->splineq[j][sp->points - 1].y) / 2;
				for (i = 0; i < sp->points - 1; ++i) {
					nx = (sp->splineq[j][i + 1].x +
					      sp->splineq[j][i].x) / 2;
					ny = (sp->splineq[j][i + 1].y +
					      sp->splineq[j][i].y) / 2;
					XDrawSpline(display, window, gc,
						 px, py, sp->splineq[j][i].x,
						sp->splineq[j][i].y, nx, ny);
					px = nx;
					py = ny;
				}

				XDrawSpline(display, window, gc,
				    px, py, sp->splineq[j][sp->points - 1].x,
				   sp->splineq[j][sp->points - 1].y, zx, zy);
#endif
				if (++(sp->redrawpos) >= sp->nsplines - 1) {
					sp->redrawing = 0;
					break;
				}
			}
		}
	} else if (++sp->nsplines > MI_CYCLES(mi))
		init_spline(mi);
}

/* X spline routine. */

static void
XDrawSpline(Display * display, Drawable d, GC gc, int x0, int y0, int x1, int y1, int x2, int y2)
{
	register int xa, ya, xb, yb, xc, yc, xp, yp;

	xa = (x0 + x1) / 2;
	ya = (y0 + y1) / 2;
	xc = (x1 + x2) / 2;
	yc = (y1 + y2) / 2;
	xb = (xa + xc) / 2;
	yb = (ya + yc) / 2;

	xp = (x0 + xb) / 2;
	yp = (y0 + yb) / 2;
	if (ABS(xa - xp) + ABS(ya - yp) > SPLINE_THRESH)
		XDrawSpline(display, d, gc, x0, y0, xa, ya, xb, yb);
	else
		XDrawLine(display, d, gc, x0, y0, xb, yb);

	xp = (x2 + xb) / 2;
	yp = (y2 + yb) / 2;
	if (ABS(xc - xp) + ABS(yc - yp) > SPLINE_THRESH)
		XDrawSpline(display, d, gc, xb, yb, xc, yc, x2, y2);
	else
		XDrawLine(display, d, gc, xb, yb, x2, y2);
}

void
release_spline(ModeInfo * mi)
{
	if (splines != NULL) {
		int         screen;

		for (screen = 0; screen < MI_NUM_SCREENS(mi); screen++)
			free_spline(&splines[screen]);
		free(splines);
		splines = (splinestruct *) NULL;
	}
}

void
refresh_spline(ModeInfo * mi)
{
	splinestruct *sp;

	if (splines == NULL)
		return;
	sp = &splines[MI_SCREEN(mi)];

	if (sp->erase) {
		sp->redrawing = 1;
		sp->redrawpos = 1;
	} else {
		MI_CLEARWINDOW(mi);
	}
}

#endif /* MODE_spline */
