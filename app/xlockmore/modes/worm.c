/* -*- Mode: C; tab-width: 4 -*- */
/* worm --- draw wiggly worms */

#if !defined( lint ) && !defined( SABER )
static const char sccsid[] = "@(#)worm.c	5.00 2000/11/01 xlockmore";

#endif

/*-
 * Copyright (c) 1991 by Patrick J. Naughton.
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
 * 03-Sep-1996: fixed bug in allocation of space for worms, added 3d support
 *              Henrik Theiling <theiling@coli.uni-sb.de>
 * 27-Sep-1995: put back malloc
 * 23-Sep-1993: got rid of "rint". (David Bagley)
 * 27-Sep-1991: got rid of all malloc calls since there were no calls to free().
 * 25-Sep-1991: Integrated into X11R5 contrib xlock.
 *
 * Adapted from a concept in the Dec 87 issue of Scientific American p. 142.
 *
 * SunView version: Brad Taylor <brad@sun.com>
 * X11 version: Dave Lemke <lemke@ncd.com>
 * xlock version: Boris Putanec <bp@cs.brown.edu>
 */

#ifdef STANDALONE
#define MODE_worm
#define PROGCLASS "Worm"
#define HACK_INIT init_worm
#define HACK_DRAW draw_worm
#define worm_opts xlockmore_opts
#define DEFAULTS "*delay: 17000 \n" \
 "*count: -20 \n" \
 "*cycles: 10 \n" \
 "*size: -3 \n" \
 "*ncolors: 200 \n" \
 "*use3d: False \n" \
 "*delta3d: 1.5 \n" \
 "*right3d: red \n" \
 "*left3d: blue \n" \
 "*both3d: magenta \n" \
 "*none3d: black \n"
#define SMOOTH_COLORS
#include "xlockmore.h"		/* in xscreensaver distribution */
#else /* STANDALONE */
#include "xlock.h"		/* in xlockmore distribution */
#endif /* STANDALONE */

#ifdef MODE_worm

ModeSpecOpt worm_opts =
{0, (XrmOptionDescRec *) NULL, 0, (argtype *) NULL, (OptionStruct *) NULL};

#ifdef USE_MODULES
ModStruct   worm_description =
{"worm", "init_worm", "draw_worm", "release_worm",
 "refresh_worm", "init_worm", (char *) NULL, &worm_opts,
 17000, -20, 10, -3, 64, 1.0, "",
 "Shows wiggly worms", 0, NULL};

#endif

#define MINSIZE 1

#define SEGMENTS  36
#define MINWORMS 1

#define MAXZ      750
#define MINZ      100
#define SCREENZ   200
#define GETZDIFF(z) (MI_DELTA3D(mi)*20.0*(1.0-(SCREENZ)/((float)(z)+MINZ)))
#define IRINT(x) ((int)(((x)>0.0)?(x)+0.5:(x)-0.5))

/* How many segments to draw per cycle when redrawing */
#define REDRAWSTEP 3

typedef struct {
	XPoint     *circ;
	int        *diffcirc;
	int         dir, dir2;
	int         tail;
	int         x, y, z;
	int         redrawing, redrawpos;
} wormstuff;

typedef struct {
	int         xsize, ysize, zsize;
	int         wormlength;
	int         nc;
	int         nw;
	int         circsize;
	wormstuff  *worm;
	XRectangle *rects;
	int         maxsize;
	int        *size;
	unsigned int chromo;
} wormstruct;

static float sintab[SEGMENTS];
static float costab[SEGMENTS];
static int  init_table = 0;

static wormstruct *worms = (wormstruct *) NULL;

static void
worm_doit(ModeInfo * mi, int which, unsigned long color)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	GC          gc = MI_GC(mi);
	wormstruct *wp = &worms[MI_SCREEN(mi)];
	wormstuff  *ws = &wp->worm[which];
	int         x, y, z;
	int         diff;

	ws->tail++;
	if (ws->tail == wp->wormlength)
		ws->tail = 0;

	x = ws->circ[ws->tail].x;
	y = ws->circ[ws->tail].y;

	if (MI_IS_USE3D(mi)) {
		diff = ws->diffcirc[ws->tail];
		if (MI_IS_INSTALL(mi)) {
			XSetForeground(display, gc, MI_NONE_COLOR(mi));
		} else {
			XSetForeground(display, gc, MI_BLACK_PIXEL(mi));
		}
		XFillRectangle(display, window, gc, x - diff, y,
			       wp->circsize, wp->circsize);
		XFillRectangle(display, window, gc, x + diff, y,
			       wp->circsize, wp->circsize);
	} else {
		XSetForeground(display, gc, MI_BLACK_PIXEL(mi));
		XFillRectangle(display, window, gc, x, y, wp->circsize, wp->circsize);
	}

	if (LRAND() & 1)
		ws->dir = (ws->dir + 1) % SEGMENTS;
	else
		ws->dir = (ws->dir + SEGMENTS - 1) % SEGMENTS;

	x = (ws->x + IRINT((float) wp->circsize * costab[ws->dir]) +
	     wp->xsize) % wp->xsize;
	y = (ws->y + IRINT((float) wp->circsize * sintab[ws->dir]) +
	     wp->ysize) % wp->ysize;

	ws->circ[ws->tail].x = x;
	ws->circ[ws->tail].y = y;
	ws->x = x;
	ws->y = y;

	if (MI_IS_USE3D(mi)) {
		if (LRAND() & 1)
			ws->dir2 = (ws->dir2 + 1) % SEGMENTS;
		else
			ws->dir2 = (ws->dir2 + SEGMENTS - 1) % SEGMENTS;
		/* for the z-axis the wrap-around looks bad,
		   * so worms should just turn around.
		 */
		z = (int) (ws->z + wp->circsize * sintab[ws->dir2]);
		if (z < 0 || z >= wp->zsize)
			z = (int) (ws->z - wp->circsize * sintab[ws->dir2]);

		diff = (int) (GETZDIFF(z) + 0.5);	/* ROUND */
		ws->diffcirc[ws->tail] = diff;

		ws->z = z;

		/* right eye */
		color = 0;
		wp->rects[color * wp->maxsize + wp->size[color]].x = x + diff;
		wp->rects[color * wp->maxsize + wp->size[color]].y = y;
		wp->size[color]++;

		/* left eye */
		color = 1;
		wp->rects[color * wp->maxsize + wp->size[color]].x = x - diff;
		wp->rects[color * wp->maxsize + wp->size[color]].y = y;
		wp->size[color]++;

#if 0
		if (ws->redrawing) {	/* Too hard for now */
			int         j;

			for (j = 0; j < REDRAWSTEP; j++) {
				int         k = (ws->tail - ws->redrawpos + wp->wormlength)
				% wp->wormlength;

				color = 0;
				wp->rects[color * wp->maxsize + wp->size[color]].x =
					ws->circ[k].x + ws->diffcirc[k];
				wp->rects[color * wp->maxsize + wp->size[color]].y =
					ws->circ[k].y;
				wp->size[color]++;

				color = 1;
				wp->rects[color * wp->maxsize + wp->size[color]].x =
					ws->circ[k].x - ws->diffcirc[k];
				wp->rects[color * wp->maxsize + wp->size[color]].y =
					ws->circ[k].y;
				wp->size[color]++;

				if (++(ws->redrawpos) >= wp->wormlength) {
					ws->redrawing = 0;
					break;
				}
			}
		}
#endif

	} else {

		wp->rects[color * wp->maxsize + wp->size[color]].x = x;
		wp->rects[color * wp->maxsize + wp->size[color]].y = y;
		wp->size[color]++;
		if (ws->redrawing) {
			int         j;

			ws->redrawpos++;
			/* Compensates for the changed ws->tail
			   since the last callback. */

			for (j = 0; j < REDRAWSTEP; j++) {
				int         k = (ws->tail - ws->redrawpos + wp->wormlength)
				% wp->wormlength;

				wp->rects[color * wp->maxsize + wp->size[color]].x = ws->circ[k].x;
				wp->rects[color * wp->maxsize + wp->size[color]].y = ws->circ[k].y;
				wp->size[color]++;

				if (++(ws->redrawpos) >= wp->wormlength) {
					ws->redrawing = 0;
					break;
				}
			}
		}
	}
}

static void
free_worm(wormstruct * wp)
{
	int         wn;

	if (wp->worm) {
		for (wn = 0; wn < wp->nw; wn++) {
			if (wp->worm[wn].circ)
				free(wp->worm[wn].circ);
			if (wp->worm[wn].diffcirc)
				free(wp->worm[wn].diffcirc);
		}
		free(wp->worm);
		wp->worm = (wormstuff *) NULL;
	}
	if (wp->rects) {
		free(wp->rects);
		wp->rects = (XRectangle *) NULL;
	}
	if (wp->size) {
		free(wp->size);
		wp->size = (int *) NULL;
	}
}

void
init_worm(ModeInfo * mi)
{
	wormstruct *wp;
	int         size = MI_SIZE(mi);
	int         i, j;

	if (worms == NULL) {
		if ((worms = (wormstruct *) calloc(MI_NUM_SCREENS(mi),
					       sizeof (wormstruct))) == NULL)
			return;
	}
	wp = &worms[MI_SCREEN(mi)];
	if (MI_NPIXELS(mi) <= 2 || MI_IS_USE3D(mi))
		wp->nc = 2;
	else
		wp->nc = MI_NPIXELS(mi);

	free_worm(wp);
	wp->nw = MI_COUNT(mi);
	if (wp->nw < -MINWORMS)
		wp->nw = NRAND(-wp->nw - MINWORMS + 1) + MINWORMS;
	else if (wp->nw < MINWORMS)
		wp->nw = MINWORMS;
	if (!wp->worm)
		if ((wp->worm = (wormstuff *) malloc(wp->nw *
				sizeof (wormstuff))) == NULL) {
			free_worm(wp);
			return;
		}

	if (!wp->size)
		if ((wp->size = (int *) malloc(MI_NPIXELS(mi) *
				sizeof (int))) == NULL) {
			free_worm(wp);
			return;
		}

	wp->maxsize = (REDRAWSTEP + 1) * wp->nw;	/*  / wp->nc + 1; */
	if (!wp->rects)
		if ((wp->rects = (XRectangle *) malloc(wp->maxsize * MI_NPIXELS(mi) *
				sizeof (XRectangle))) == NULL) {
			free_worm(wp);
			return;
		}

	if (!init_table) {
		init_table = 1;
		for (i = 0; i < SEGMENTS; i++) {
			sintab[i] = SINF(i * 2.0 * M_PI / SEGMENTS);
			costab[i] = COSF(i * 2.0 * M_PI / SEGMENTS);
		}
	}
	wp->xsize = MI_WIDTH(mi);
	wp->ysize = MI_HEIGHT(mi);
	wp->zsize = MAXZ - MINZ + 1;
	if (MI_NPIXELS(mi) > 2)
		wp->chromo = NRAND(MI_NPIXELS(mi));

	if (size < -MINSIZE)
		wp->circsize = NRAND(-size - MINSIZE + 1) + MINSIZE;
	else if (size < MINSIZE)
		wp->circsize = MINSIZE;
	else
		wp->circsize = size;

	for (i = 0; i < wp->nc; i++) {
		for (j = 0; j < wp->maxsize; j++) {
			wp->rects[i * wp->maxsize + j].width = wp->circsize;
			wp->rects[i * wp->maxsize + j].height = wp->circsize;

		}
	}
	(void) memset((char *) wp->size, 0, wp->nc * sizeof (int));

	wp->wormlength = (int) sqrt((double) wp->xsize + wp->ysize) *
		MI_CYCLES(mi) / 8;	/* Fudge this to something reasonable */
	if (wp->wormlength < 1)
		wp->wormlength = 1;
	for (i = 0; i < wp->nw; i++) {
		if (((wp->worm[i].circ = (XPoint *) malloc(wp->wormlength *
				sizeof (XPoint))) == NULL) ||
		    ((wp->worm[i].diffcirc = (int *) malloc(wp->wormlength *
				sizeof (int))) == NULL)) {
			free_worm(wp);
			return;
		}

		for (j = 0; j < wp->wormlength; j++) {
			wp->worm[i].circ[j].x = wp->xsize / 2;
			wp->worm[i].circ[j].y = wp->ysize / 2;
			if (MI_IS_USE3D(mi))
				wp->worm[i].diffcirc[j] = 0;
		}
		wp->worm[i].dir = NRAND(SEGMENTS);
		wp->worm[i].dir2 = NRAND(SEGMENTS);
		wp->worm[i].tail = 0;
		wp->worm[i].x = wp->xsize / 2;
		wp->worm[i].y = wp->ysize / 2;
		wp->worm[i].z = SCREENZ - MINZ;
		wp->worm[i].redrawing = 0;
	}

	if (MI_IS_INSTALL(mi) && MI_IS_USE3D(mi)) {
		MI_CLEARWINDOWCOLOR(mi, MI_NONE_COLOR(mi));
	} else {
		MI_CLEARWINDOW(mi);
	}
}

void
draw_worm(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	GC          gc = MI_GC(mi);
	wormstruct *wp = &worms[MI_SCREEN(mi)];
	unsigned long wcolor;
	int         i;

	(void) memset((char *) wp->size, 0, wp->nc * sizeof (int));

	MI_IS_DRAWN(mi) = True;

	for (i = 0; i < wp->nw; i++) {
		if (MI_NPIXELS(mi) > 2) {
			wcolor = (i + wp->chromo) % wp->nc;

			worm_doit(mi, i, wcolor);
		} else
			worm_doit(mi, i, (unsigned long) 0);
	}

	if (MI_IS_USE3D(mi)) {
		if (MI_IS_INSTALL(mi))
			XSetFunction(display, gc, GXor);
		XSetForeground(display, gc, MI_RIGHT_COLOR(mi));
		XFillRectangles(display, window, gc, &(wp->rects[0]), wp->size[0]);

		XSetForeground(display, gc, MI_LEFT_COLOR(mi));
		XFillRectangles(display, window, gc, &(wp->rects[wp->maxsize]), wp->size[1]);
		if (MI_IS_INSTALL(mi))
			XSetFunction(display, gc, GXcopy);
	} else if (MI_NPIXELS(mi) > 2) {
		for (i = 0; i < wp->nc; i++) {
			XSetForeground(display, gc, MI_PIXEL(mi, i));
			XFillRectangles(display, window, gc, &(wp->rects[i * wp->maxsize]), wp->size[i]);
		}
	} else {
		XSetForeground(display, gc, MI_WHITE_PIXEL(mi));
		XFillRectangles(display, window, gc,
				&(wp->rects[0]), wp->size[0]);
	}

	if (++wp->chromo == (unsigned long) wp->nc)
		wp->chromo = 0;
}

void
release_worm(ModeInfo * mi)
{
	if (worms != NULL) {
		int         screen;

		for (screen = 0; screen < MI_NUM_SCREENS(mi); screen++)
			free_worm(&worms[screen]);
		free(worms);
		worms = (wormstruct *) NULL;
	}
}

void
refresh_worm(ModeInfo * mi)
{
	if (MI_IS_INSTALL(mi) && MI_IS_USE3D(mi)) {
		MI_CLEARWINDOWCOLOR(mi, MI_NONE_COLOR(mi));
	} else {
		MI_CLEARWINDOW(mi);
	}
	/* The 3D code does drawing&clearing by XORing.  We do not
	   want to go to too much trouble here to make it redraw
	   correctly. */
	if (!MI_IS_USE3D(mi) && worms != NULL) {
		wormstruct *wp = &worms[MI_SCREEN(mi)];
		int         i;

		for (i = 0; i < wp->nw; i++) {
			wp->worm[i].redrawing = 1;
			wp->worm[i].redrawpos = 0;
		}
	}
}

#endif /* MODE_worm */
