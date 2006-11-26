/* -*- Mode: C; tab-width: 4 -*- */
/* life1d --- Stephen Wolfram's 1d game of Life */

#if !defined( lint ) && !defined( SABER )
static const char sccsid[] = "@(#)life1d.c	5.00 2000/11/01 xlockmore";

#endif

/*-
 * Copyright (c) 1995 by David Bagley.
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
 * 27-Oct-1997: xpm and ras capability added... it does not make too much
 *              sense here but ...
 * 10-May-1997: Compatible with xscreensaver
 * 27-Jul-1995: written, used life.c as a basis, using totalistic rules
 *              (default).  Special thanks to Harold V. McIntosh
 *              <mcintosh@servidor.unam.mx> for providing me with the
 *              LCAU collection and references.
 *
 * References:
 * Dewdney, A.K., "The Armchair Universe, Computer Recreations from the
 *   Pages of Scientific American Magazine", W.H. Freedman and Company,
 *   New York, 1988 (May 1985).
 * Perry, Kenneth E., "Abstract Mathematical Art", BYTE, December, 1986
 *   pp. 181-192
 * Hayes, Brian, "Computer Recreations", Scientific American, March 1984,
 *   p. 12
 * Wolfram, Stephen, "Cellular automata as models of complexity", Nature,
 *   4 October 1984, pp. 419-424
 * Wolfram, Stephen, "Computer Software in Science and Mathematics",
 *   Scientific American, September 1984, pp. 188-203
 *
 */

#ifdef STANDALONE
#define MODE_life1d
#define PROGCLASS "Life1D"
#define HACK_INIT init_life1d
#define HACK_DRAW draw_life1d
#define life1d_opts xlockmore_opts
#define DEFAULTS "*delay: 10000 \n" \
 "*cycles: 10 \n" \
 "*size: 0 \n" \
 "*ncolors: 200 \n" \
 "*bitmap: \n" \
 "*verbose: \n"
#include "xlockmore.h"		/* in xscreensaver distribution */
#else /* STANDALONE */
#include "xlock.h"		/* in xlockmore distribution */
#include "color.h"
#endif /* STANDALONE */
#include "automata.h"
#include "iostuff.h"

#ifdef MODE_life1d

#define DEF_TOTALISTIC  "True"	/* False is LCAU */

static Bool totalistic;

static XrmOptionDescRec opts[] =
{
	{(char *) "-totalistic", (char *) ".life1d.totalistic", XrmoptionNoArg, (caddr_t) "on"},
	{(char *) "+totalistic", (char *) ".life1d.totalistic", XrmoptionNoArg, (caddr_t) "off"}
};
static argtype vars[] =
{
	{(void *) & totalistic, (char *) "totalistic", (char *) "Totalistic", (char *) DEF_TOTALISTIC, t_Bool}
};
static OptionStruct desc[] =
{
	{(char *) "-/+totalistic", (char *) "turn on/off totalistic rules (else LCAU rules)"}
};

ModeSpecOpt life1d_opts =
{sizeof opts / sizeof opts[0], opts, sizeof vars / sizeof vars[0], vars, desc};

#ifdef USE_MODULES
ModStruct   life1d_description =
{"life1d", "init_life1d", "draw_life1d", "release_life1d",
 "refresh_life1d", "init_life1d", (char *) NULL, &life1d_opts,
 10000, 1, 10, 0, 64, 1.0, "",
 "Shows Wolfram's game of 1D Life", 0, NULL};

#endif

#define LIFE1DBITS(n,w,h)\
  if ((lp->pixmaps[lp->init_bits]=\
  XCreatePixmapFromBitmapData(display,window,(char *)n,w,h,1,0,1))==None){\
  free_life1d(display,lp); return;} else {lp->init_bits++;}

/* aliases for vars defined in the bitmap file */
#define CELL_WIDTH   image_width
#define CELL_HEIGHT    image_height
#define CELL_BITS    image_bits

#include "life1d.xbm"

#ifdef HAVE_XPM
static char *image_name[] =
{(char *) ""};

#define CELL_NAME image_name
#define DEFAULT_XPM 0
#endif

static int  maxstates;
static int  maxradius;
static int  maxsum_size;

#define MINGRIDSIZE 10
#define MINSIZE 2		/* 3 may be better here */
#define MAXSTATES 5

typedef struct {
	int         init_bits;
	int         pixelmode;
	int         xs, ys, xb, yb;	/* cell size, grid border */
	int         screen_generation, row;
	int         nrows, ncols, border;
	int         width, height;
	int         k, r;
	long        code;
	int         repeating;
	char       *nextstate;
	int         colors[MAXSTATES];
	Pixmap      pixmaps[MAXSTATES];
	GC          stippledGC;
	unsigned char *newcells;
	unsigned char *oldcells;
	unsigned char *buffer;
	unsigned char *previousBuffer;
	XImage     *logo;
	Colormap    cmap;
	unsigned long black;
	int         graphics_format;
	GC          backGC;
	int         busyLoop;
} life1dstruct;

static life1dstruct *life1ds = (life1dstruct *) NULL;

static int  totalistic_rules[][3] =
{

  /* Well behaved rules */
  /* Scientific American (Dewdney) */
	{2, 2, 20},
	{2, 2, 52},		/* A bit too prolific but I like it anyway */
	{2, 3, 88},		/* James K. Park's 1D Gun (1111111111011) */
	{2, 4, 368},

  /* Nature */
	{3, 1, 792},

  /* BYTE (Translated to Wolfram's notation) */
	{4, 1, 39744},
	{4, 1, 81036},
	{4, 1, 126092},
	{4, 1, 147824},
	{4, 1, 156272},
	{4, 1, 189468},
	{4, 1, 176412},
	{4, 1, 214840},
	{4, 1, 245028},
	{4, 1, 267320},
	{4, 1, 257808},
	{4, 1, 258596},
	{4, 1, 260224},
	{4, 1, 267408},
	{4, 1, 290960},
	{4, 1, 330056},
	{4, 1, 330436},
	{4, 1, 400192},
	{4, 1, 433296},
	{4, 1, 434492},
	{4, 1, 447368},
	{4, 1, 453768},
	{4, 1, 454416},
	{4, 1, 485488},
	{4, 1, 505904},
	{4, 1, 618960},
	{4, 1, 642948},
	{4, 1, 680528},
	{4, 1, 708484},
	{4, 1, 741004},
	{4, 1, 749708},
	{4, 1, 756420},
	{4, 1, 761356},
	{4, 1, 769088},
	{4, 1, 778568},
	{4, 1, 779792},
	{4, 1, 797456},
	{4, 1, 803728},
	{4, 1, 844092},
	{4, 1, 874524},
	{4, 1, 881440},
	{4, 1, 921476},
	{4, 1, 936832},
	{4, 1, 937792},
	{4, 1, 1004600},

  /* Nature */
	{5, 1, 580020},
	{5, 1, 5694390},
	{5, 1, 59123000},

#if 0				/* OK but annoying */

  /* BYTE */
	{4, 1, 10552},
	{4, 1, 14708},
	{4, 1, 25284},
	{4, 1, 42848},
	{4, 1, 44328},
	{4, 1, 51788},
	{4, 1, 107364},
	{4, 1, 111448},
	{4, 1, 155848},
	{4, 1, 173024},
	{4, 1, 224148},
	{4, 1, 238372},
	{4, 1, 241656},
	{4, 1, 243764},
	{4, 1, 255856},
	{4, 1, 259222},
	{4, 1, 310148},
	{4, 1, 324148},
	{4, 1, 346696},
	{4, 1, 364424},
	{4, 1, 403652},
	{4, 1, 436072},
	{4, 1, 456708},
	{4, 1, 461912},
	{4, 1, 534812},
	{4, 1, 546700},
	{4, 1, 552708},
	{4, 1, 569092},
	{4, 1, 616736},
	{4, 1, 658564},
	{4, 1, 717956},
	{4, 1, 748432},
	{4, 1, 800964},
	{4, 1, 800972},
	{4, 1, 801144},
	{4, 1, 821116},
	{4, 1, 840172},
	{4, 1, 858312},
	{4, 1, 865394},
	{4, 1, 914952},
	{4, 1, 919244},
	{4, 1, 984296},
	{4, 1, 997964},
	{4, 1, 1018488},
	{4, 1, 1018808},
	{4, 1, 1023864},
	{4, 1, 1024472},
	{4, 1, 1033776},
	{4, 1, 1033784},
	{4, 1, 1034552},

  /* Nature */
	{5, 1, 583330},
	{5, 1, 672900},
#endif

#if 0				/* rejects */
  /* Nature */
	{2, 1, 4},
	{2, 3, 18},
	{2, 3, 22},
	{2, 3, 90},
	{2, 3, 94},
	{2, 3, 126},
	{2, 3, 128},

  /* Scientific American (Dewdney) */
	{2, 3, 54},
	{3, 2, 66},		/* RIPPLE, Dewdney's personal 1d rule */
	{3, 1, 257},

  /* BYTE */
	{4, 1, 16},
	{4, 1, 56},
	{4, 1, 4408},
	{4, 1, 101988},
	{4, 1, 113688},
	{4, 1, 227892},
	{4, 1, 254636},
	{4, 1, 258598},
	{4, 1, 294146},
	{4, 1, 377576},
	{4, 1, 472095},
	{4, 1, 538992},
	{4, 1, 615028},
	{4, 1, 901544},
	{4, 1, 911876},

  /* Nature */
	{5, 1, 10175},
	{5, 1, 566780},
	{5, 1, 570090},
#endif
};

#define TOTALISTICRULES (sizeof totalistic_rules / sizeof totalistic_rules[0])

#if 0
static char lcau21_rules[][9] =
{
	"00010010",		/* 18 Hollow enlarging triangles */
	"00110110",		/* 54 Hollow triangles */
	"01101110",		/* 110 Hollow right triangles */
	"01111100",		/* 124 Hollow left triangles */
	"10010011",		/* 147 Solid triangles */
	"10001001",		/* 137 Solid right triangles */
	"11000001",		/* 193 Solid left triangles */
};

#define LCAU2RULES (sizeof lcau21_rules / sizeof lcau21_rules[0])
#endif

static char lcau31_rules[][28] =
{
	"000222111000111222222111000",	/* equal thirds */
	"002220210110110211110002200",	/* threads on triangles */
	"001121102222110110111002100",	/* interfaces of 2 vel */
	"020201010201010102010102020",	/* class iv, sparse */
	"020201011201011112011112120",	/* blue bground class iv */
	"021211110211110101110101020",	/* diagonal black gaps */
	"022221211221211012222111120",	/* macrocell w/ 012 membrane */
	"100002021002021210021210100",	/* totalistic rule 792 (iv) */
	"100002200112201200020121200",	/* binary counter */
	"101021220111100222112102020",	/* two patterns compete #1 */
	"110001000112221222112221000",	/* reversible rule */
	"111012101002110122121102120",	/* small blue triangles */
	"111021210012110202120212122",	/* 2 glider on 1 background */
	"111111101010202020202010101",	/* motes and triangles */
	"111220012222012120001221020",	/* black triangles */
	"112110201012001210101200010",	/* dendrites */
	"112122000000122112112122000",	/* reversible rule's reverse */
	"200211020111110002001101210",	/* crocodile skin */
	"202000200112001200120211100",	/* two slow gliders collide */
	"202211000201021001100200200",	/* Red Queen's binary counter */
	"210101012101012120012120200",	/* blue on red background */
	"212021022100200221201101020",	/* two patterns compete #2 */
	"220222010220211111000211010",	/* Fisch's cyclic eater */
	"222022001222211202022120012",	/* macrocell w/ 0122 membrane */
};

#define LCAU3RULES (sizeof lcau31_rules / sizeof lcau31_rules[0])

static char lcau41_rules[][65] =
{
	"0000000313131323232312121213232323231313131101010202020202010101",	/* skewed triangle */
	"0000020202000000020232220203300000000101020030000100322121003020",	/* slow glider - copies bar */
	"0000213323132331123303003213323113233123002033211332313233120001",	/* slightly chaotic symmetri */
	"0000332030033323010020122303001002120210000000100110020220000010",	/* shuttle squeeze */
	"0100030000030323200323120202001003000203000220001310220100200010",	/* coo gldrs */
	"0121200113213320110223311213201013131010111011101120012132103210",	/* mixture of types */
	"0212301103023320313223121332310023203323333231301020023120303020",	/* slo gl w/ many f gl */
	"0320032100113332112321213211003321233210121032320000321000010200",	/* crosshatching */
	"0323323323313310323323313310310223313310310210213310310210210210",	/* Perry's 245028 */
	"0323330023210000121122232322122113310131032101002110122321102120",	/*  */
	"0332200131100112230221012111210202022013210332120210222311212100",	/* cycles on dgl bgrnd */
	"0332200131100112230221012111210202022013210332120210222311212300",	/* cycles on dgl bgrnd */
	"1230320301231030321132211021112010202330101010112312220310311000",	/* very complex glider */
	"1230320301321030321132211021112010212121232212112312220310311000",	/* nice cross hatching */
	"2031122112031101123123233000321210301112010303203232213000311000",	/* gliders among stills */
	"2202003300010200010011010011020003033000032302002203110033010200",	/* bin ctr */
	"2313032202112320111221023212120203132000233322021310311101010030",	/* diagonal growth on mesh */
	"2332102112210200233210120311023110322213112123233132331213211212",	/* gliderettes & latice */
	"3000213323132331123302003213323113233123001033211332313233120000",	/* symmetric rule */
	"3003003203213211003203213211211303213211211311323211211311321320",	/* Perry's blue background */
	"3010213223113300321221011010123112301033010221203333211323110100",	/* v. bars w/entanglement */
	"3111213323132331123313113213323113233123220133211332313233121110",	/* not purely symmetric */
	"3112001202313030102330122003321023102122030102001210223122203020",	/* slow & fast gliders */
	"3130123232333201012202313210121032302200211020313130002001103210",	/* crocodile skin */
	"3230120202031130033311232111223012311113322032103210123012303210",	/* y/o on b/g */
	"3330333312110010333032222222001033303222121111110000322212110010",	/* Fisch's rule */
	"3332032303133100221122122213220022000212021022100200221203103020",	/* slow glider */
	"3333101022220101323201012323101033331010222201013232010123231010",	/* reverse of rvble #22 */
	"3333111122000022333311112200002233111133002222003311113300222200",	/* reversible Rule 22 */
};

#define LCAU4RULES (sizeof lcau41_rules / sizeof lcau41_rules[0])

#ifdef LCAU2RULES
#define LCAURULES (LCAU2RULES + LCAU3RULES + LCAU4RULES)
#else
#define LCAURULES (LCAU3RULES + LCAU4RULES)
#endif

static void
drawcell(ModeInfo * mi, int col, int row, unsigned int state)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	life1dstruct *lp = &life1ds[MI_SCREEN(mi)];
	GC          gc = lp->backGC;

	if (!state) {
		XSetForeground(display, gc, lp->black);
		XFillRectangle(display, window, gc,
		lp->xb + lp->xs * col, lp->yb + lp->ys * row, lp->xs, lp->ys);
		return;
	}
	if (MI_NPIXELS(mi) > 2)
		XSetForeground(display, gc, MI_PIXEL(mi, lp->colors[state - 1]));
	if (lp->pixelmode || (MI_NPIXELS(mi) <= 2)) {
		if (MI_NPIXELS(mi) <= 2) {
			XGCValues   gcv;

			gcv.stipple = lp->pixmaps[state - 1];
			gcv.foreground = MI_WHITE_PIXEL(mi);
			gcv.background = lp->black;
			gcv.fill_style = FillOpaqueStippled;
			XChangeGC(display, lp->stippledGC,
				  GCStipple | GCFillStyle | GCForeground | GCBackground, &gcv);
			XFillRectangle(display, window, lp->stippledGC,
				       lp->xb + lp->xs * col, lp->yb + lp->ys * row, lp->xs, lp->ys);
		} else
			XFillRectangle(display, window, gc,
				       lp->xb + lp->xs * col, lp->yb + lp->ys * row, lp->xs, lp->ys);
	}
	 else {
		(void) XPutImage(display, window, gc,
		lp->logo, 0, 0, lp->xb + lp->xs * col, lp->yb + lp->ys * row,
				 lp->logo->width, lp->logo->height);
	}
}

static void
RandomSoup(life1dstruct * lp, int n, int v)
{
	int         col;

	v /= 2;
	if (v < 1)
		v = 1;
	for (col = lp->ncols / 2 - v; col < lp->ncols / 2 + v; ++col)
		if (NRAND(100) < n && col >= 0 && col < lp->ncols)
			lp->newcells[col + lp->border] = (unsigned char) NRAND(lp->k - 1) + 1;
}

static long
power(int x, int n)
{				/* raise x to the nth power n >= 0 */
	int         i;
	long        p = 1;

	for (i = 1; i <= n; ++i)
		p = p * x;
	return p;
}

static void
GetRule(life1dstruct * lp, int i)
{
	long        sum_size, j;

	if (totalistic) {
		long        code, pow_size;

		lp->k = totalistic_rules[i][0];
		lp->r = totalistic_rules[i][1];
		sum_size = (lp->k - 1) * (lp->r * 2 + 1) + 1;
		code = lp->code = totalistic_rules[i][2];

		pow_size = power(lp->k, (int) (sum_size - 1));	/* Should be < max long */
		for (j = 0; j < sum_size; j++) {
			lp->nextstate[sum_size - 1 - j] = (char) (code / pow_size);
			code -= ((long) lp->nextstate[sum_size - 1 - j]) * pow_size;
			pow_size /= (long) lp->k;
		}
	} else {
		lp->r = 1;
#ifdef LCAU2RULES
		if (i < (int) LCAU2RULES) {
			lp->k = 2;
			sum_size = power(lp->k, 2 * lp->r + 1);
			for (j = 0; j < sum_size; j++)
				lp->nextstate[sum_size - 1 - j] = lcau21_rules[i][j] - '0';
		} else if (i < LCAU2RULES + LCAU3RULES)
#else
		if (i < (int) LCAU3RULES)
#endif
		{
			lp->k = 3;
			sum_size = power(lp->k, 2 * lp->r + 1);
#ifdef LCAU2RULES
			i -= LCAU2RULES;
#endif
			for (j = 0; j < sum_size; j++)
				lp->nextstate[sum_size - 1 - j] = lcau31_rules[i][j] - '0';
		} else {
			lp->k = 4;
			sum_size = power(lp->k, 2 * lp->r + 1);
#ifdef LCAU2RULES
			i -= (LCAU2RULES + LCAU3RULES);
#else
			i -= LCAU3RULES;
#endif
			for (j = 0; j < sum_size; j++)
				lp->nextstate[sum_size - 1 - j] = lcau41_rules[i][j] - '0';
		}
	}
}

static int
compare(ModeInfo * mi)
{
	life1dstruct *lp = &life1ds[MI_SCREEN(mi)];
	int         row, col, tryagain = False;
	unsigned char *initl, *cmpr;

        /* The following sometimes does not detect when there
	   are multiple periodic life forms side by side. */
	initl = lp->buffer + lp->row * lp->ncols;
	for (row = lp->row - 1; row >= 0; row--) {
		cmpr = lp->buffer + row * lp->ncols;
		for (col = 0; col < lp->ncols; col++) {
			tryagain = False;
			if (*(initl + col) != *cmpr) {
				tryagain = True;
				break;
			}
			cmpr++;
		}
		if (!tryagain) {
			return True;
		}
	}
        /* This is not guaranteed but its twice as good as without. */
	if (lp->previousBuffer) {
		initl = lp->buffer + lp->row * lp->ncols;
		for (row = lp->nrows - 1; row >= 0; row--) {
			cmpr = lp->previousBuffer + row * lp->ncols;
			for (col = 0; col < lp->ncols; col++) {
				tryagain = False;
				if (*(initl + col) != *cmpr) {
					tryagain = True;
					break;
				}
				cmpr++;
			}
			if (!tryagain) {
				return True;
			}
		}
	}
	return False;

}

static Bool
init_stuff(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	life1dstruct *lp = &life1ds[MI_SCREEN(mi)];

	if (lp->logo == None) {
		getImage(mi, &lp->logo, CELL_WIDTH, CELL_HEIGHT, CELL_BITS,
#ifdef HAVE_XPM
			 DEFAULT_XPM, CELL_NAME,
#endif
			 &lp->graphics_format, &lp->cmap, &lp->black);
		if (lp->logo == None) {
			return False;
		}
	}
#ifndef STANDALONE
	if (lp->cmap != None) {
		setColormap(display, window, lp->cmap, MI_IS_INWINDOW(mi));
		if (lp->backGC == None) {
			XGCValues   xgcv;

			xgcv.background = lp->black;
			if ((lp->backGC = XCreateGC(display, window, GCBackground,
					&xgcv)) == None) {
				return False;
			}
		}
	} else
#endif /* STANDALONE */
	{
		lp->black = MI_BLACK_PIXEL(mi);
		lp->backGC = MI_GC(mi);
	}
	return True;
}

static void
free_stuff(Display * display, life1dstruct * lp)
{
	if (lp->cmap != None) {
		XFreeColormap(display, lp->cmap);
		if (lp->backGC != None) {
			XFreeGC(display, lp->backGC);
			lp->backGC = None;
		}
		lp->cmap = None;
	} else
		lp->backGC = None;
}

static void
free_life1d(Display * display, life1dstruct * lp)
{
	int         shade;

	if (lp->stippledGC != None) {
		XFreeGC(display, lp->stippledGC);
		lp->stippledGC = None;
	}
	if (lp->init_bits != 0) {
		for (shade = 0; shade < MAXSTATES; shade++)
			XFreePixmap(display, lp->pixmaps[shade]);
		lp->init_bits = 0;
	}
	if (lp->newcells != NULL) {
		free(lp->newcells);
		lp->newcells = (unsigned char *) NULL;
	}
	if (lp->oldcells != NULL) {
		free(lp->oldcells);
		lp->oldcells = (unsigned char *) NULL;
	}
	if (lp->buffer != NULL) {
		free(lp->buffer);
		lp->buffer = (unsigned char *) NULL;
	}
	if (lp->previousBuffer != NULL) {
		free(lp->previousBuffer);
		lp->previousBuffer = (unsigned char *) NULL;
	}
	if (lp->nextstate != NULL) {
		free(lp->nextstate);
		lp->nextstate = (char *) NULL;
	}
	free_stuff(display, lp);
	if (lp->logo != None) {
		destroyImage(&lp->logo, &lp->graphics_format);
		lp->logo = None;
	}
}

void
init_life1d(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	int         size = MI_SIZE(mi);
	int         i;
	life1dstruct *lp;

	if (life1ds == NULL) {
		if ((life1ds = (life1dstruct *) calloc(MI_NUM_SCREENS(mi),
					     sizeof (life1dstruct))) == NULL)
			return;
	}
	lp = &life1ds[MI_SCREEN(mi)];

	if (!init_stuff(mi)) {
		free_life1d(display, lp);
		return;
	}

	lp->screen_generation = 0;
	lp->row = 0;

	if (totalistic) {
		maxstates = MAXSTATES;
		maxradius = 4;
		maxsum_size = (maxstates - 1) * (maxradius * 2 + 1) + 1;
	} else {
		maxstates = MAXSTATES - 1;
		maxradius = 1;
		maxsum_size = (int) power(maxstates, (2 * maxradius + 1));
	}
	if (lp->nextstate == NULL) {
		if ((lp->nextstate = (char *) malloc(maxsum_size *
				sizeof (char))) == NULL) {
			free_life1d(display, lp);
			return;
		}
	}

	if (lp->init_bits == 0) {
		XGCValues   gcv;

		gcv.fill_style = FillOpaqueStippled;
		if ((lp->stippledGC = XCreateGC(display, window, GCFillStyle,
				&gcv)) == None) {
			free_life1d(display, lp);
			return;
		}
		for (i = 0; i < MAXSTATES - 1; i++) {
			LIFE1DBITS(stipples[i + NUMSTIPPLES - MAXSTATES + 1],
				   STIPPLESIZE, STIPPLESIZE);
		}
		LIFE1DBITS(stipples[NUMSTIPPLES / 2],
			   STIPPLESIZE, STIPPLESIZE);	/* grey */
	}
	if (lp->newcells != NULL)
		free(lp->newcells);
	if (lp->oldcells != NULL)
		free(lp->oldcells);
	if (lp->buffer != NULL)
		free(lp->buffer);
	if (lp->previousBuffer != NULL)
		free(lp->previousBuffer);
	lp->previousBuffer = (unsigned char *) NULL;
	lp->width = MI_WIDTH(mi);
	lp->height = MI_HEIGHT(mi);
	if (lp->width < 2)
		lp->width = 2;
	if (lp->height < 2)
		lp->height = 2;
	if (size == 0 ||
	 MINGRIDSIZE * size > lp->width || MINGRIDSIZE * size > lp->height) {
		if (lp->width > MINGRIDSIZE * lp->logo->width &&
		    lp->height > MINGRIDSIZE * lp->logo->height) {
			lp->pixelmode = False;
			lp->xs = lp->logo->width;
			lp->ys = lp->logo->height;
		} else
		{
			int min = MIN(lp->width, lp->height) / (12 * MINGRIDSIZE);
			int max = MIN(lp->width, lp->height) / (4 * MINGRIDSIZE);


			lp->xs = lp->ys = MAX(MINSIZE, min + NRAND(max - min + 1));
			lp->pixelmode = True;
		}
	} else {
		lp->pixelmode = True;
		if (size < -MINSIZE) {
			lp->ys = NRAND(MIN(-size, MAX(MINSIZE, MIN(lp->width, lp->height) /
				      MINGRIDSIZE)) - MINSIZE + 1) + MINSIZE;
		} else if (size < MINSIZE) {
			lp->ys = MINSIZE;
		} else {
			lp->ys = MIN(size, MAX(MINSIZE, MIN(lp->width, lp->height) /
					       MINGRIDSIZE));
		}
		lp->xs = lp->ys;
	}
	lp->ncols = MAX(lp->width / lp->xs, 2);
	lp->nrows = MAX(lp->height / lp->ys, 2);
	lp->border = (lp->nrows / 2 + 1) * MI_CYCLES(mi);
	if ((lp->newcells = (unsigned char *) calloc(lp->ncols + 2 * lp->border,
			sizeof (unsigned char))) == NULL) {
		free_life1d(display, lp);
		return;
	}

	if ((lp->oldcells = (unsigned char *) calloc(lp->ncols + 2 *
			(maxradius + lp->border),
			sizeof (unsigned char))) == NULL) {
		free_life1d(display, lp);
		return;
	}

	if ((lp->buffer = (unsigned char *) calloc(lp->ncols * lp->nrows,
			sizeof (unsigned char))) == NULL) {
		free_life1d(display, lp);
		return;
	}

	lp->xb = (lp->width - lp->xs * lp->ncols) / 2;
	lp->yb = (lp->height - lp->ys * lp->nrows) / 2;

	GetRule(lp, (int) NRAND((totalistic) ? TOTALISTICRULES : LCAURULES));
	if (MI_IS_VERBOSE(mi)) {
		(void) fprintf(stdout, "colors %d, radius %d, code %ld, ",
			       lp->k, lp->r, lp->code);
		if (totalistic) {
			(void) fprintf(stdout, "totalistic rule ");
			for (i = (lp->k - 1) * (lp->r * 2 + 1); i >= 0; i--)
				(void) fprintf(stdout, "%d", (int) lp->nextstate[i]);
		} else {
			(void) fprintf(stdout, "LCAU rule ");
			for (i = (int) power(lp->k, (lp->r * 2 + 1)); i >= 0; i--)
				(void) fprintf(stdout, "%d", (int) lp->nextstate[i]);
		}
		(void) fprintf(stdout, "\n");
	}
	if (MI_NPIXELS(mi) > 2)
		for (i = 0; i < lp->k - 1; i++)
			lp->colors[i] = (NRAND(MI_NPIXELS(mi)) + i * MI_NPIXELS(mi)) /
				(lp->k - 1);
	RandomSoup(lp, 40, 25);
	(void) memcpy((char *) (lp->oldcells + maxradius + lp->border),
		      (char *) (lp->newcells + lp->border), lp->ncols);
	lp->busyLoop = 0;

	MI_CLEARWINDOWCOLORMAP(mi, lp->backGC, lp->black);
}

void
draw_life1d(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	int         col;
	life1dstruct *lp;

	if (life1ds == NULL)
		return;
	lp = &life1ds[MI_SCREEN(mi)];
	if (lp->buffer == NULL)
		return;

	MI_IS_DRAWN(mi) = True;
	if (lp->busyLoop) {
		if (lp->busyLoop >= 250)
			lp->busyLoop = 0;
		else
			lp->busyLoop++;
		return;
	}
	if (lp->row == 0) {
		lp->repeating = 0;
		if (lp->screen_generation > MI_CYCLES(mi))
			init_life1d(mi);
		if (!lp->previousBuffer && lp->screen_generation > 1) {
			lp->previousBuffer = (unsigned char *) calloc(lp->ncols *
				lp->nrows, sizeof (unsigned char));
		}
		if (lp->previousBuffer) {
			(void) memcpy((char *) (lp->previousBuffer),
				(char *) (lp->buffer), lp->nrows * lp->ncols);
		}

		for (col = 0; col < lp->ncols; col++)
			if (lp->buffer[col] != lp->newcells[col + lp->border])
				drawcell(mi, col, 0, lp->newcells[col + lp->border]);
		(void) memcpy((char *) lp->buffer, (char *) (lp->newcells + lp->border),
			      lp->ncols);
	} else {
		for (col = 0; col < lp->ncols + 2 * lp->border; col++) {
			int         sum = 0, m;

			if (totalistic) {
				for (m = col - lp->r; m <= col + lp->r; m++)
					sum += lp->oldcells[m + maxradius];
			} else {
				int         pow_size = 1;

				for (m = col + lp->r; m >= col - lp->r; m--) {
					sum += lp->oldcells[m + maxradius] * pow_size;
					pow_size *= lp->k;
				}
			}
			lp->newcells[col] = (unsigned char) lp->nextstate[sum];
		}
		(void) memcpy((char *) (lp->oldcells + maxradius),
			  (char *) lp->newcells, lp->ncols + 2 * lp->border);

		for (col = 0; col < lp->ncols; col++) {
			if (lp->buffer[col + lp->row * lp->ncols] !=
			    lp->newcells[col + lp->border])
				drawcell(mi, col, lp->row, lp->newcells[col + lp->border]);
		}
		(void) memcpy((char *) (lp->buffer + lp->row * lp->ncols),
			    (char *) (lp->newcells + lp->border), lp->ncols);
		{
			int         temp = compare(mi);

			if (temp)
				lp->repeating += temp;
			else
				lp->repeating = 0;
		}
		lp->repeating += (lp->row == lp->nrows - 1) ?
			(lp->nrows - 1) * compare(mi) : 0;
	}
	if (lp->repeating >= 1) {
		XGCValues   gcv;

		gcv.stipple = lp->pixmaps[MAXSTATES - 1];
		gcv.fill_style = FillStippled;
		gcv.foreground = lp->black;
		XChangeGC(MI_DISPLAY(mi), lp->stippledGC,
			  GCStipple | GCFillStyle | GCForeground, &gcv);
		XFillRectangle(display, MI_WINDOW(mi), lp->stippledGC,
			       0, lp->yb + lp->ys * lp->row,
			       lp->width, lp->ys);
	}
	lp->row++;
	if (lp->repeating >= lp->nrows - 1) {
		if (lp->row < lp->nrows) {
			XSetForeground(display, lp->backGC, lp->black);
			XFillRectangle(display, MI_WINDOW(mi), lp->backGC,
				       0, lp->yb + lp->ys * lp->row,
			  lp->width, lp->height - lp->ys * lp->row - lp->yb);
		}
		lp->screen_generation = MI_CYCLES(mi);
		lp->row = lp->nrows;
	}
	if (lp->row >= lp->nrows) {
		lp->screen_generation++;
		lp->busyLoop = 1;
		lp->row = 0;
	}
}

void
release_life1d(ModeInfo * mi)
{
	if (life1ds != NULL) {
		int         screen;

		for (screen = 0; screen < MI_NUM_SCREENS(mi); screen++)
			free_life1d(MI_DISPLAY(mi), &life1ds[screen]);
		free(life1ds);
		life1ds = (life1dstruct *) NULL;
	}
}

void
refresh_life1d(ModeInfo * mi)
{
	int         row, col, nrow;
	life1dstruct *lp;

	if (life1ds == NULL)
		return;
	lp = &life1ds[MI_SCREEN(mi)];
	if (lp->buffer == NULL)
		return;

#ifdef HAVE_XPM
        if (lp->graphics_format >= IS_XPM) {
		/* This is needed when another program changes the colormap. */
		free_life1d(MI_DISPLAY(mi), lp);
		init_life1d(mi);
		return;
	}
#endif

	for (row = 0; row < lp->nrows; row++) {
		nrow = row * lp->ncols;
		for (col = 0; col < lp->ncols; col++)
			drawcell(mi, col, row, lp->buffer[col + nrow]);
	}
}

#endif /* MODE_life1d */
