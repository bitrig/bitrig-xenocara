/* -*- Mode: C; tab-width: 4 -*- */
/* sproingiewrap.c - sproingies wrapper */

#if !defined( lint ) && !defined( SABER )
static const char sccsid[] = "@(#)sproingiewrap.c	5.00 2000/11/01 xlockmore";

#endif

/*-
 * sproingiewrap.c - Copyright 1996 Sproingie Technologies Incorporated.
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
 *    Programming:  Ed Mackey, http://www.netaxs.com/~emackey/
 *    Sproingie 3D objects modeled by:  Al Mackey, al@iam.com
 *       (using MetaNURBS in NewTek's Lightwave 3D v5).
 *
 * Revision History:
 * 01-Nov-2000: Allocation checks
 * 26-Apr-1997: Added glPointSize() calls around explosions, plus other fixes.
 * 28-Mar-1997: Added size support.
 * 22-Mar-1997: Updated to use glX interface instead of xmesa one.
 *              Also, support for multiscreens added.
 * 20-Mar-1997: Updated for xlockmore v4.02alpha7 and higher, using
 *              xlockmore's built-in Mesa/OpenGL support instead of
 *              my own.  Submitted for inclusion in xlockmore.
 * 09-Dec-1996: Written.
 */

/*-
 * The sproingies have six "real" frames, (s1_1 to s1_6) that show a
 * sproingie jumping off a block, headed down and to the right.  But
 * the program thinks of sproingies as having twelve "virtual" frames,
 * with the latter six being copies of the first, only lowered and
 * rotated by 90 degrees (jumping to the left).  So after going
 * through 12 frames, a sproingie has gone down two rows but not
 * moved horizontally.
 *
 * To have the sproingies randomly choose left/right jumps at each
 * block, the program should go back to thinking of only 6 frames,
 * and jumping down only one row when it is done.  Then it can pick a
 * direction for the next row.
 *
 * (Falling off the end might not be so bad either.  :) )
 */

#ifdef STANDALONE
#define MODE_sproingies
#define PROGCLASS "Sproingies"
#define HACK_INIT init_sproingies
#define HACK_DRAW draw_sproingies
#define sproingies_opts xlockmore_opts
#define DEFAULTS "*delay: 80000 \n" \
 "*count: 5 \n" \
 "*cycles: 0 \n" \
 "*size: 0 \n" \
 "*wireframe: False \n"
#include "xlockmore.h"		/* from the xscreensaver distribution */
#else /* !STANDALONE */
#include "xlock.h"		/* from the xlockmore distribution */
#include "vis.h"
#endif /* !STANDALONE */

#ifdef MODE_sproingies

ModeSpecOpt sproingies_opts =
{0, (XrmOptionDescRec *) NULL, 0, (argtype *) NULL, (OptionStruct *) NULL};

#ifdef USE_MODULES
ModStruct   sproingies_description =
{"sproingies", "init_sproingies", "draw_sproingies", "release_sproingies",
 "refresh_sproingies", "init_sproingies", (char *) NULL, &sproingies_opts,
 80000, 5, 0, 0, 64, 1.0, "",
 "Shows Sproingies!  Nontoxic.  Safe for pets and small children", 0, NULL};

#endif

#define MINSIZE 32

#include <GL/glu.h>
#include <time.h>

void        NextSproingie(int screen);
void        NextSproingieDisplay(int screen);
void        DisplaySproingies(int screen);

#if 0
void        ReshapeSproingies(int w, int h);

#endif
void        CleanupSproingies(int screen);
Bool        InitSproingies(int wfmode, int grnd, int mspr, int screen, int numscreens, int mono);

typedef struct {
	GLfloat     view_rotx, view_roty, view_rotz;
	GLint       gear1, gear2, gear3;
	GLfloat     angle;
	GLuint      limit;
	GLuint      count;
	GLXContext *glx_context;
	int         mono;
	Window      window;
	int         init;
} sproingiesstruct;

static sproingiesstruct *sproingies = (sproingiesstruct *) NULL;

static Display *swap_display;
static Window swap_window;

void
SproingieSwap(void)
{
	glFinish();
	glXSwapBuffers(swap_display, swap_window);
}


void
init_sproingies(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);

	int         cycles = MI_CYCLES(mi);
	int         count = MI_COUNT(mi);
	int         size = MI_SIZE(mi);
	int         wfmode = 0, grnd, mspr, w, h;
	sproingiesstruct *sp;

	if (sproingies == NULL) {
		if ((sproingies = (sproingiesstruct *) calloc(MI_NUM_SCREENS(mi),
					 sizeof (sproingiesstruct))) == NULL)
			return;
	}
	sp = &sproingies[MI_SCREEN(mi)];

	sp->mono = (MI_IS_MONO(mi) ? 1 : 0);
	sp->window = window;
	if ((sp->glx_context = init_GL(mi)) != NULL) {

		if ((cycles & 1) || MI_IS_WIREFRAME(mi))
			wfmode = 1;
		grnd = (cycles >> 1);
		if (grnd > 2)
			grnd = 2;

		mspr = count;
		if (mspr > 100)
			mspr = 100;

		/* wireframe, ground, maxsproingies */
		if (!InitSproingies(wfmode, grnd, mspr, MI_SCREEN(mi),
				 MI_NUM_SCREENS(mi), sp->mono)) {
			return;
		}
		sp->init = True;
		/* Viewport is specified size if size >= MINSIZE && size < screensize */
		if (size <= 1) {
			w = MI_WIDTH(mi);
			h = MI_HEIGHT(mi);
		} else if (size < MINSIZE) {
			w = MINSIZE;
			h = MINSIZE;
		} else {
			w = (size > MI_WIDTH(mi)) ? MI_WIDTH(mi) : size;
			h = (size > MI_HEIGHT(mi)) ? MI_HEIGHT(mi) : size;
		}

		glViewport((MI_WIDTH(mi) - w) / 2, (MI_HEIGHT(mi) - h) / 2, w, h);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		gluPerspective(65.0, (GLfloat) w / (GLfloat) h, 0.1, 2000.0);	/* was 200000.0 */
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		swap_display = display;
		swap_window = window;
		DisplaySproingies(MI_SCREEN(mi));
	} else {
		MI_CLEARWINDOW(mi);
	}
}

/* ARGSUSED */
void
draw_sproingies(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	sproingiesstruct *sp;

	if (sproingies == NULL)
		return;
	sp = &sproingies[MI_SCREEN(mi)];
	if (!sp->init)
		return;

	MI_IS_DRAWN(mi) = True;
	if (!sp->glx_context)
		return;

	glDrawBuffer(GL_BACK);
	glXMakeCurrent(display, window, *(sp->glx_context));

	swap_display = display;
	swap_window = window;

	NextSproingieDisplay(MI_SCREEN(mi));	/* It will swap. */
}

void
refresh_sproingies(ModeInfo * mi)
{
	/* No need to do anything here... The whole screen is updated
	 * every frame anyway.  Otherwise this would be just like
	 * draw_sproingies, above, but replace NextSproingieDisplay(...)
	 * with DisplaySproingies(...).
	 */
}

void
release_sproingies(ModeInfo * mi)
{
	if (sproingies != NULL) {
		int         screen;

		for (screen = 0; screen < MI_NUM_SCREENS(mi); screen++) {
			sproingiesstruct *sp = &sproingies[screen];

			if (sp->glx_context) {

				glXMakeCurrent(MI_DISPLAY(mi), sp->window, *(sp->glx_context));
				CleanupSproingies(screen);
			}
		}

		free(sproingies);
		sproingies = (sproingiesstruct *) NULL;
	}
	FreeAllGL(mi);
}

#endif

/* End of sproingiewrap.c */
