/* -*- Mode: C; tab-width: 4 -*- */
/* ifs --- modified iterated functions system */

#if !defined( lint ) && !defined( SABER )
static const char sccsid[] = "@(#)ifs.c	5.00 2000/11/01 xlockmore";

#endif

/*-
 * Copyright (c) 1997 by Massimino Pascal <Pascal.Massimon@ens.fr>
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
 * If this mode is weird and you have an old MetroX server, it is buggy.
 * There is a free SuSE-enhanced MetroX X server that is fine.
 *
 * When shown ifs, Diana Rose (4 years old) said, "It looks like dancing."
 *
 * Revision History:
 * 22-Dec-2004: TDA: Replace Gauss_Rand with a real Gaussian.  Fix
 *              glitches where x=0 or y=0.
 * 01-Nov-2000: Allocation checks
 * 10-May-1997: jwz@jwz.org: turned into a standalone program.
 *              Made it render into an offscreen bitmap and then copy
 *              that onto the screen, to reduce flicker.
 */

#ifdef STANDALONE
#define MODE_ifs
#define PROGCLASS "IFS"
#define HACK_INIT init_ifs
#define HACK_DRAW draw_ifs
#define ifs_opts xlockmore_opts
#define DEFAULTS "*delay: 1000 \n" \
 "*ncolors: 100 \n"
#define SMOOTH_COLORS
#include "xlockmore.h"		/* in xscreensaver distribution */
#else /* STANDALONE */
#include "xlock.h"		/* in xlockmore distribution */
#endif /* STANDALONE */

#ifdef MODE_ifs

ModeSpecOpt ifs_opts =
{0, (XrmOptionDescRec *) NULL, 0, (argtype *) NULL, (OptionStruct *) NULL};

#ifdef USE_MODULES
ModStruct   ifs_description =
{"ifs", "init_ifs", "draw_ifs", "release_ifs",
 "init_ifs", "init_ifs", (char *) NULL, &ifs_opts,
 1000, 1, 1, 1, 64, 1.0, "",
 "Shows a modified iterated function system", 0, NULL};

#endif

/*****************************************************/

typedef float DBL;
typedef int F_PT;

/* typedef float               F_PT; */

/*****************************************************/

#define FIX 12
#define UNIT   ( 1<<FIX )
#define MAX_SIMI  6

   /* settings for a PC 120Mhz... */
#define MAX_DEPTH_2  10
#define MAX_DEPTH_3  6
#define MAX_DEPTH_4  4
#define MAX_DEPTH_5  3

#define DBL_To_F_PT(x)  (F_PT)( (DBL)(UNIT)*(x) )

typedef struct Similitude_Struct SIMI;
typedef struct Fractal_Struct FRACTAL;

struct Similitude_Struct {

	DBL         c_x, c_y;
	DBL         r, r2, A, A2;
	F_PT        Ct, St, Ct2, St2;
	F_PT        Cx, Cy;
	F_PT        R, R2;
};

struct Fractal_Struct {

	int         Nb_Simi;
	SIMI        Components[5 * MAX_SIMI];
	int         Depth, Col;
	int         Count, Speed;
	int         Width, Height, Lx, Ly;
	DBL         r_mean, dr_mean, dr2_mean;
	int         Cur_Pt, Max_Pt;
	XPoint     *Buffer1, *Buffer2;
	Pixmap      dbuf;
	GC          dbuf_gc;
};

static FRACTAL *Root = (FRACTAL *) NULL, *Cur_F;
static XPoint *Buf;
static int  Cur_Pt;


/*****************************************************/
#if 0
static      DBL
Gauss_Rand(DBL c, DBL A, DBL S)
{
	DBL         y;

	y = (DBL) LRAND() / MAXRAND;
	y = A * (1.0 - exp(-y * y * S)) / (1.0 - exp(-S));
	if (NRAND(2))
		return (c + y);
	return (c - y);
}

static      DBL
Half_Gauss_Rand(DBL c, DBL A, DBL S)
{
	DBL         y;

	y = (DBL) LRAND() / MAXRAND;
	y = A * (1.0 - exp(-y * y * S)) / (1.0 - exp(-S));
	return (c + y);
}

#else
/* I don't know that it makes a difference, but these actually *are*
   Gaussians.  [TDA] */

/* Generate Gaussian random number: mean c, "amplitude" A (actually
   A is 3*standard deviation).  'S' is unused.  */

/* Note this generates a pair of gaussian variables, so it saves one
   to give out next time it's called */

static double
Gauss_Rand(DBL c, DBL A, DBL S)
{
	static double d;
	static Bool ready = 0;
	if(ready) {
		ready = 0;
		return c + A/3 * d;
	} else {
		double x, y, w;
		do {
			x = 2.0 * (double)LRAND() / MAXRAND - 1.0;
			y = 2.0 * (double)LRAND() / MAXRAND - 1.0;
			w = x*x + y*y;
		} while(w >= 1.0);

		w = sqrt((-2 * log(w))/w);
		ready = 1;
		d =          x * w;
		return c + A/3 * y * w;
	}
}

static double
Half_Gauss_Rand(DBL c, DBL A, DBL S)
{
	static double d;
	static Bool ready = 0;
	if(ready) {
		ready = 0;
		return c + A/3 * d;
	} else {
		double x, y, w;
		do {
			x = (double)LRAND() / MAXRAND;
			y = (double)LRAND() / MAXRAND;
			w = x*x + y*y;
		} while(w >= 1.0);

		w = sqrt((-2 * log(w))/w);
		ready = 1;
		d =          x * w;
		return c + A/3 * y * w;
	}
}
#endif

static void
Random_Simis(FRACTAL * F, SIMI * Cur, int i)
{
	while (i--) {
		Cur->c_x = Gauss_Rand(0.0, .8, 4.0);
		Cur->c_y = Gauss_Rand(0.0, .8, 4.0);
		Cur->r = Gauss_Rand(F->r_mean, F->dr_mean, 3.0);
		Cur->r2 = Half_Gauss_Rand(0.0, F->dr2_mean, 2.0);
		Cur->A = Gauss_Rand(0.0, 360.0, 4.0) * (M_PI / 180.0);
		Cur->A2 = Gauss_Rand(0.0, 360.0, 4.0) * (M_PI / 180.0);
		Cur++;
	}
}

static void
free_ifs_buffers(FRACTAL *Fractal)
{
	if (Fractal->Buffer1 != NULL) {
		free(Fractal->Buffer1);
		Fractal->Buffer1 = (XPoint *) NULL;
	}
	if (Fractal->Buffer2 != NULL) {
		free(Fractal->Buffer2);
		Fractal->Buffer2 = (XPoint *) NULL;
	}
}


static void
free_ifs(Display *display, FRACTAL *Fractal)
{
	free_ifs_buffers(Fractal);
	if (Fractal->dbuf != None) {
		XFreePixmap(display, Fractal->dbuf);
		Fractal->dbuf = None;
	}
	if (Fractal->dbuf_gc != None) {
		XFreeGC(display, Fractal->dbuf_gc);
		Fractal->dbuf_gc = None;
	}
}

/***************************************************************/

void
init_ifs(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	GC          gc = MI_GC(mi);
	int         i;
	FRACTAL    *Fractal;

	if (Root == NULL) {
		Root = (FRACTAL *) calloc(
				       MI_NUM_SCREENS(mi), sizeof (FRACTAL));
		if (Root == NULL)
			return;
	}
	Fractal = &Root[MI_SCREEN(mi)];

	free_ifs_buffers(Fractal);
	i = (NRAND(4)) + 2;	/* Number of centers */
	switch (i) {
		case 3:
			Fractal->Depth = MAX_DEPTH_3;
			Fractal->r_mean = .6;
			Fractal->dr_mean = .4;
			Fractal->dr2_mean = .3;
			break;

		case 4:
			Fractal->Depth = MAX_DEPTH_4;
			Fractal->r_mean = .5;
			Fractal->dr_mean = .4;
			Fractal->dr2_mean = .3;
			break;

		case 5:
			Fractal->Depth = MAX_DEPTH_5;
			Fractal->r_mean = .5;
			Fractal->dr_mean = .4;
			Fractal->dr2_mean = .3;
			break;

		default:
		case 2:
			Fractal->Depth = MAX_DEPTH_2;
			Fractal->r_mean = .7;
			Fractal->dr_mean = .3;
			Fractal->dr2_mean = .4;
			break;
	}
	/* (void) fprintf( stderr, "N=%d\n", i ); */
	Fractal->Nb_Simi = i;
	Fractal->Max_Pt = Fractal->Nb_Simi - 1;
	for (i = 0; i <= Fractal->Depth + 2; ++i)
		Fractal->Max_Pt *= Fractal->Nb_Simi;

	if ((Fractal->Buffer1 = (XPoint *) calloc(Fractal->Max_Pt,
			sizeof (XPoint))) == NULL) {
		free_ifs(display, Fractal);
		return;
	}
	if ((Fractal->Buffer2 = (XPoint *) calloc(Fractal->Max_Pt,
			sizeof (XPoint))) == NULL) {
		free_ifs(display, Fractal);
		return;
	}
	Fractal->Speed = 6;
	Fractal->Width = MI_WIDTH(mi);
	Fractal->Height = MI_HEIGHT(mi);
	Fractal->Cur_Pt = 0;
	Fractal->Count = 0;
	Fractal->Lx = (Fractal->Width - 1) / 2;
	Fractal->Ly = (Fractal->Height - 1) / 2;
	Fractal->Col = NRAND(MI_NPIXELS(mi) - 1) + 1;

	Random_Simis(Fractal, Fractal->Components, 5 * MAX_SIMI);

#ifndef NO_DBUF
	if (Fractal->dbuf != None)
		XFreePixmap(display, Fractal->dbuf);
	Fractal->dbuf = XCreatePixmap(display, window,
				      Fractal->Width, Fractal->Height, 1);
	/* Allocation checked */
	if (Fractal->dbuf != None) {
		XGCValues   gcv;

		gcv.foreground = 0;
		gcv.background = 0;
		gcv.graphics_exposures = False;
		gcv.function = GXcopy;

		if (Fractal->dbuf_gc != None)
			XFreeGC(display, Fractal->dbuf_gc);
		if ((Fractal->dbuf_gc = XCreateGC(display, Fractal->dbuf,
				GCForeground | GCBackground | GCGraphicsExposures | GCFunction,
				&gcv)) == None) {
			XFreePixmap(display, Fractal->dbuf);
			Fractal->dbuf = None;
		} else {
			XFillRectangle(display, Fractal->dbuf,
			    Fractal->dbuf_gc, 0, 0, Fractal->Width, Fractal->Height);
			XSetBackground(display, gc, MI_BLACK_PIXEL(mi));
			XSetFunction(display, gc, GXcopy);
		}
	}
#endif
	MI_CLEARWINDOW(mi);

	/* don't want any exposure events from XCopyPlane */
	XSetGraphicsExposures(display, gc, False);

}


/***************************************************************/

/* Should be taken care of already... but just in case */
#if !defined( __GNUC__ ) && !defined(__cplusplus) && !defined(c_plusplus)
#undef inline
#define inline			/* */
#endif
static inline void
Transform(SIMI * Simi, F_PT xo, F_PT yo, F_PT * x, F_PT * y)
{
	F_PT        xx, yy;

	xo = xo - Simi->Cx;
	xo = (xo * Simi->R) / UNIT;
	yo = yo - Simi->Cy;
	yo = (yo * Simi->R) / UNIT;

	xx = xo - Simi->Cx;
	xx = (xx * Simi->R2) / UNIT;
	yy = -yo - Simi->Cy;
	yy = (yy * Simi->R2) / UNIT;

	*x = ((xo * Simi->Ct - yo * Simi->St + xx * Simi->Ct2 - yy * Simi->St2) / UNIT) + Simi->Cx;
	*y = ((xo * Simi->St + yo * Simi->Ct + xx * Simi->St2 + yy * Simi->Ct2) / UNIT) + Simi->Cy;
}

/***************************************************************/

static void
Trace(FRACTAL * F, F_PT xo, F_PT yo)
{
	F_PT        x, y, i;
	SIMI       *Cur;

	Cur = Cur_F->Components;
	for (i = Cur_F->Nb_Simi; i; --i, Cur++) {
		Transform(Cur, xo, yo, &x, &y);
		Buf->x = (short) (F->Lx + ((double)x * F->Lx / (UNIT * 2)));
		Buf->y = (short) (F->Ly - ((double)y * F->Ly / (UNIT * 2)));
		Buf++;
		Cur_Pt++;

		if (F->Depth && ((x - xo) >> 4) && ((y - yo) >> 4)) {
			F->Depth--;
			Trace(F, x, y);
			F->Depth++;
		}
	}
}

static void
Draw_Fractal(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	GC          gc = MI_GC(mi);
	FRACTAL    *F = &Root[MI_SCREEN(mi)];
	int         i, j;
	F_PT        x, y, xo, yo;
	SIMI       *Cur, *Simi;

	for (Cur = F->Components, i = F->Nb_Simi; i; --i, Cur++) {
		Cur->Cx = DBL_To_F_PT(Cur->c_x);
		Cur->Cy = DBL_To_F_PT(Cur->c_y);

		Cur->Ct = DBL_To_F_PT(cos(Cur->A));
		Cur->St = DBL_To_F_PT(sin(Cur->A));
		Cur->Ct2 = DBL_To_F_PT(cos(Cur->A2));
		Cur->St2 = DBL_To_F_PT(sin(Cur->A2));

		Cur->R = DBL_To_F_PT(Cur->r);
		Cur->R2 = DBL_To_F_PT(Cur->r2);
	}


	Cur_Pt = 0;
	Cur_F = F;
	Buf = F->Buffer2;
	for (Cur = F->Components, i = F->Nb_Simi; i; --i, Cur++) {
		xo = Cur->Cx;
		yo = Cur->Cy;
		for (Simi = F->Components, j = F->Nb_Simi; j; --j, Simi++) {
			if (Simi == Cur)
				continue;
			Transform(Simi, xo, yo, &x, &y);
			Trace(F, x, y);
		}
	}

	/* Erase previous */
	if (F->Cur_Pt) {
		XSetForeground(display, gc, MI_BLACK_PIXEL(mi));
		if (F->dbuf != None) {
			XSetForeground(display, F->dbuf_gc, 0);
			/* XDrawPoints(display, F->dbuf, F->dbuf_gc, F->Buffer1, F->Cur_Pt,
  				CoordModeOrigin); */
			XFillRectangle(display, F->dbuf, F->dbuf_gc, 0, 0,
				       F->Width, F->Height);
		} else
			XDrawPoints(display, window, gc, F->Buffer1, F->Cur_Pt, CoordModeOrigin);
	}
	if (MI_NPIXELS(mi) < 2)
		XSetForeground(display, gc, MI_WHITE_PIXEL(mi));
	else
		XSetForeground(display, gc, MI_PIXEL(mi, F->Col % MI_NPIXELS(mi)));
	if (Cur_Pt) {
		if (F->dbuf != None) {
			XSetForeground(display, F->dbuf_gc, 1);
			XDrawPoints(display, F->dbuf, F->dbuf_gc, F->Buffer2, Cur_Pt,
				    CoordModeOrigin);
		} else
			XDrawPoints(display, window, gc, F->Buffer2, Cur_Pt, CoordModeOrigin);
	}
	if (F->dbuf != None)
		XCopyPlane(display, F->dbuf, window, gc, 0, 0, F->Width, F->Height, 0, 0, 1);

	F->Cur_Pt = Cur_Pt;
	Buf = F->Buffer1;
	F->Buffer1 = F->Buffer2;
	F->Buffer2 = Buf;
}


void
draw_ifs(ModeInfo * mi)
{
	int         i;
	DBL         u, uu, v, vv, u0, u1, u2, u3;
	SIMI       *S, *S1, *S2, *S3, *S4;
	FRACTAL    *F;

	if (Root == NULL)
		return;
	F = &Root[MI_SCREEN(mi)];
	if (F->Buffer1 == NULL)
		return;

	u = (DBL) (F->Count) * (DBL) (F->Speed) / 1000.0;
	uu = u * u;
	v = 1.0 - u;
	vv = v * v;
	u0 = vv * v;
	u1 = 3.0 * vv * u;
	u2 = 3.0 * v * uu;
	u3 = u * uu;

	S = F->Components;
	S1 = S + F->Nb_Simi;
	S2 = S1 + F->Nb_Simi;
	S3 = S2 + F->Nb_Simi;
	S4 = S3 + F->Nb_Simi;

	for (i = F->Nb_Simi; i; --i, S++, S1++, S2++, S3++, S4++) {
		S->c_x = u0 * S1->c_x + u1 * S2->c_x + u2 * S3->c_x + u3 * S4->c_x;
		S->c_y = u0 * S1->c_y + u1 * S2->c_y + u2 * S3->c_y + u3 * S4->c_y;
		S->r = u0 * S1->r + u1 * S2->r + u2 * S3->r + u3 * S4->r;
		S->r2 = u0 * S1->r2 + u1 * S2->r2 + u2 * S3->r2 + u3 * S4->r2;
		S->A = u0 * S1->A + u1 * S2->A + u2 * S3->A + u3 * S4->A;
		S->A2 = u0 * S1->A2 + u1 * S2->A2 + u2 * S3->A2 + u3 * S4->A2;
	}

	MI_IS_DRAWN(mi) = True;

	Draw_Fractal(mi);

	if (F->Count >= 1000 / F->Speed) {
		S = F->Components;
		S1 = S + F->Nb_Simi;
		S2 = S1 + F->Nb_Simi;
		S3 = S2 + F->Nb_Simi;
		S4 = S3 + F->Nb_Simi;

		for (i = F->Nb_Simi; i; --i, S++, S1++, S2++, S3++, S4++) {
			S2->c_x = 2.0 * S4->c_x - S3->c_x;
			S2->c_y = 2.0 * S4->c_y - S3->c_y;
			S2->r = 2.0 * S4->r - S3->r;
			S2->r2 = 2.0 * S4->r2 - S3->r2;
			S2->A = 2.0 * S4->A - S3->A;
			S2->A2 = 2.0 * S4->A2 - S3->A2;

			*S1 = *S4;
		}
		Random_Simis(F, F->Components + 3 * F->Nb_Simi, F->Nb_Simi);

		Random_Simis(F, F->Components + 4 * F->Nb_Simi, F->Nb_Simi);

		F->Count = 0;
	} else
		F->Count++;

	F->Col++;
}


/***************************************************************/

void
release_ifs(ModeInfo * mi)
{
	if (Root != NULL) {
		int         screen;

		for (screen = 0; screen < MI_NUM_SCREENS(mi); screen++)
			free_ifs(MI_DISPLAY(mi), &Root[screen]);
		free(Root);
		Root = (FRACTAL *) NULL;
	}
}

#endif /* MODE_ifs */
