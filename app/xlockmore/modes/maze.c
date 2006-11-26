/* -*- Mode: C; tab-width: 4 -*- */
/* maze --- a varying maze */

#if !defined( lint ) && !defined( SABER )
static const char sccsid[] = "@(#)maze.c	5.00 2000/11/01 xlockmore";

#endif

/*-
 * Copyright (c) 1988 by Sun Microsystems
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
 * 27-Nov-2001: interactive maze mode Ephraim Yawitz fyawitz@actcom.co.il
 * 01-Nov-2000: Allocation checks
 * 27-Oct-1997: xpm and ras capability added
 * 10-May-1997: Compatible with xscreensaver
 * 27-Feb-1996: Add ModeInfo args to init and callback hooks, use new
 *              ModeInfo handle to specify long pauses (eliminate onepause).
 *		        Ron Hitchens <ron@idiom.com>
 * 20-Jul-1995: minimum size fix Peter Schmitzberger <schmitz@coma.sbg.ac.at>
 * 17-Jun-1995: removed sleep statements
 * 22-Mar-1995: multidisplay fix Caleb Epstein <epstein_caleb@jpmorgan.com>
 * 09-Mar-1995: changed how batchcount is used
 * 27-Feb-1995: patch for VMS
 * 04-Feb-1995: patch to slow down maze Heath Kehoe <hakehoe@icaen.uiowa.edu>
 * 17-Jun-1994: HP ANSI C compiler needs a type cast for gray_bits
 *              Richard Lloyd <R.K.Lloyd@csc.liv.ac.uk>
 * 02-Sep-1993: xlock version David Bagley <bagleyd@tux.org>
 * 07-Mar-1993: Good ideas from xscreensaver Jamie Zawinski <jwz@jwz.org>
 * 06-Jun-1985: Martin Weiss Sun Microsystems
 */

/*-
 * original copyright
 * **************************************************************************
 * Copyright 1988 by Sun Microsystems, Inc. Mountain View, CA.
 *
 * All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of Sun or MIT not be used in advertising
 * or publicity pertaining to distribution of the software without specific
 * prior written permission. Sun and M.I.T. make no representations about the
 * suitability of this software for any purpose. It is provided "as is"
 * without any express or implied warranty.
 *
 * SUN DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 * IN NO EVENT SHALL SUN BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 * ***************************************************************************
 */

#ifdef STANDALONE
#define MODE_maze
#define PROGCLASS "Maze"
#define HACK_INIT init_maze
#define HACK_DRAW draw_maze
#define maze_opts xlockmore_opts
#define DEFAULTS "*delay: 1000 \n" \
 "*cycles: 3000 \n" \
 "*size: -40 \n" \
 "*ncolors: 64 \n" \
 "*bitmap: \n" \
 "*trackmouse: False \n"
#include "xlockmore.h"		/* in xscreensaver distribution */
#else /* STANDALONE */
#include "xlock.h"		/* in xlockmore distribution */
#include "color.h"
#endif /* STANDALONE */
#include "iostuff.h"

#ifdef MODE_maze

#define DEF_TRACKMOUSE  "False"
static Bool trackmouse;

#ifdef DISABLE_INTERACTIVE
ModeSpecOpt maze_opts =
{0, (XrmOptionDescRec *) NULL, 0, (argtype *) NULL, (OptionStruct *) NULL};
#else
static XrmOptionDescRec opts[] =
{
  {(char *) "-trackmouse", (char *) ".maze.trackmouse", XrmoptionNoArg, (caddr_t) "on"},
  {(char *) "+trackmouse", (char *) ".maze.trackmouse", XrmoptionNoArg, (caddr_t) "off"}
};

static argtype vars[] =
{
  {(void *) & trackmouse, (char *) "trackmouse", (char *) "TrackMouse", (char *) DEF_TRACKMOUSE, t_Bool}
};

static OptionStruct desc[] =
{
  {(char *) "-/+trackmouse", (char *) "turn on/off the tracking of the mouse"},
};

ModeSpecOpt maze_opts =
{sizeof opts / sizeof opts[0], opts, sizeof vars / sizeof vars[0], vars, desc};
#endif

#ifdef USE_MODULES
ModStruct   maze_description =
{"maze", "init_maze", "draw_maze", "release_maze",
 "refresh_maze", "init_maze", (char *) NULL, &maze_opts,
 1000, 1, 3000, -40, 64, 1.0, "",
 "Shows a random maze and a depth first search solution", 0, NULL};

#endif

#include "bitmaps/gray1.xbm"

/* aliases for vars defined in the bitmap file */
#define ICON_WIDTH   image_width
#define ICON_HEIGHT    image_height
#define ICON_BITS    image_bits

#include "maze.xbm"

#ifdef HAVE_XPM
#define ICON_NAME  image_name
#include "maze.xpm"
#define DEFAULT_XPM 1
#endif

#define MINGRIDSIZE	3
#define MINSIZE        8

#define WALL_TOP	0x8000
#define WALL_RIGHT	0x4000
#define WALL_BOTTOM	0x2000
#define WALL_LEFT	0x1000

#define DOOR_IN_TOP	0x800
#define DOOR_IN_RIGHT	0x400
#define DOOR_IN_BOTTOM	0x200
#define DOOR_IN_LEFT	0x100
#define DOOR_IN_ANY	0xF00

#define DOOR_OUT_TOP	0x80
#define DOOR_OUT_RIGHT	0x40
#define DOOR_OUT_BOTTOM	0x20
#define DOOR_OUT_LEFT	0x10

#define DIR_UP          0
#define DIR_RIGHT       1
#define DIR_DOWN        2
#define DIR_LEFT        3

#define START_SQUARE	0x2
#define END_SQUARE	0x1

typedef struct {
	unsigned int x;
	unsigned int y;
	char        dir;
} paths;

typedef struct {
	int         ncols, nrows, maze_size, xb, yb;
	int         sqnum, cur_sq_x, cur_sq_y, path_length;
	int         start_x, start_y, start_dir, end_x, end_y, end_dir;
	int         logo_x, logo_y;
	int         width, height;
	int         xs, ys, logo_size_x, logo_size_y;
	int         solving, current_path, stage;
	int         cycles;
	unsigned int *maze;
	paths      *move_list;
	paths      *save_path, *path;
	GC          grayGC;
	Pixmap      graypix;
	XImage     *logo;
	Colormap    cmap;
	unsigned long black, color;
	int         graphics_format;
	GC          backGC;
	int         time;
} mazestruct;

static mazestruct *mazes = (mazestruct *) NULL;

static void
draw_wall(ModeInfo * mi, int i, int j, int dir)
{				/* draw a single wall */
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	mazestruct *mp = &mazes[MI_SCREEN(mi)];
	GC          gc = mp->backGC;

	switch (dir) {
		case 0:
			XDrawLine(display, window, gc,
				  mp->xb + mp->xs * i,
				  mp->yb + mp->ys * j,
				  mp->xb + mp->xs * (i + 1),
				  mp->yb + mp->ys * j);
			break;
		case 1:
			XDrawLine(display, window, gc,
				  mp->xb + mp->xs * (i + 1),
				  mp->yb + mp->ys * j,
				  mp->xb + mp->xs * (i + 1),
				  mp->yb + mp->ys * (j + 1));
			break;
		case 2:
			XDrawLine(display, window, gc,
				  mp->xb + mp->xs * i,
				  mp->yb + mp->ys * (j + 1),
				  mp->xb + mp->xs * (i + 1),
				  mp->yb + mp->ys * (j + 1));
			break;
		case 3:
			XDrawLine(display, window, gc,
				  mp->xb + mp->xs * i,
				  mp->yb + mp->ys * j,
				  mp->xb + mp->xs * i,
				  mp->yb + mp->ys * (j + 1));
			break;
	}
}				/* end of draw_wall */

static void
draw_solid_square(ModeInfo * mi, GC gc,
		  register int i, register int j, register int dir)
{				/* draw a solid square in a square */
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	mazestruct *mp = &mazes[MI_SCREEN(mi)];

	switch (dir) {
		case 0:
			XFillRectangle(display, window, gc,
				       mp->xb + 3 + mp->xs * i,
				       mp->yb - 3 + mp->ys * j,
				       mp->xs - 6, mp->ys);
			break;
		case 1:
			XFillRectangle(display, window, gc,
				       mp->xb + 3 + mp->xs * i,
				       mp->yb + 3 + mp->ys * j,
				       mp->xs, mp->ys - 6);
			break;
		case 2:
			XFillRectangle(display, window, gc,
				       mp->xb + 3 + mp->xs * i,
				       mp->yb + 3 + mp->ys * j,
				       mp->xs - 6, mp->ys);
			break;
		case 3:
			XFillRectangle(display, window, gc,
				       mp->xb - 3 + mp->xs * i,
				       mp->yb + 3 + mp->ys * j,
				       mp->xs, mp->ys - 6);
			break;
	}

}				/* end of draw_solid_square() */

static void
enter_square(ModeInfo * mi, int n)
{				/* move into a neighboring square */
	mazestruct *mp = &mazes[MI_SCREEN(mi)];

	draw_solid_square(mi, mp->backGC, (int) mp->path[n].x, (int) mp->path[n].y,
			  (int) mp->path[n].dir);

	mp->path[n + 1].dir = -1;
	switch (mp->path[n].dir) {
		case 0:
			mp->path[n + 1].x = mp->path[n].x;
			mp->path[n + 1].y = mp->path[n].y - 1;
			break;
		case 1:
			mp->path[n + 1].x = mp->path[n].x + 1;
			mp->path[n + 1].y = mp->path[n].y;
			break;
		case 2:
			mp->path[n + 1].x = mp->path[n].x;
			mp->path[n + 1].y = mp->path[n].y + 1;
			break;
		case 3:
			mp->path[n + 1].x = mp->path[n].x - 1;
			mp->path[n + 1].y = mp->path[n].y;
			break;
	}

}				/* end of enter_square() */

static void
free_path(mazestruct * mp)
{
	if (mp->maze) {
		free(mp->maze);
		mp->maze = (unsigned int *) NULL;
	}
	if (mp->move_list) {
		free(mp->move_list);
		mp->move_list = (paths *) NULL;
	}
	if (mp->save_path) {
		free(mp->save_path);
		mp->save_path = (paths *) NULL;
	}
	if (mp->path) {
		free(mp->path);
		mp->path = (paths *) NULL;
	}
}

static void
free_stuff(Display * display, mazestruct * mp)
{
	if (mp->cmap != None) {
		XFreeColormap(display, mp->cmap);
		if (mp->backGC != None) {
			XFreeGC(display, mp->backGC);
			mp->backGC = None;
		}
		mp->cmap = None;
	} else
		mp->backGC = None;
}

static void
free_maze(Display * display, mazestruct * mp)
{
	free_path(mp);
	if (mp->grayGC != None) {
		XFreeGC(display, mp->grayGC);
		mp->grayGC = None;
	}
	if (mp->graypix != None) {
		XFreePixmap(display, mp->graypix);
		mp->graypix = None;
	}
	free_stuff(display, mp);
	if (mp->logo != None) {
		destroyImage(&mp->logo, &mp->graphics_format);
		mp->logo = None;
	}
}

static Bool
set_maze_sizes(ModeInfo * mi)
{
	mazestruct *mp = &mazes[MI_SCREEN(mi)];
	Display *display = MI_DISPLAY(mi);
	int         size = MI_SIZE(mi);

	if (size < -MINSIZE) {
		free_path(mp);
		mp->ys = NRAND(MIN(-size, MAX(MINSIZE, (MIN(mp->width, mp->height) - 1) /
				      MINGRIDSIZE)) - MINSIZE + 1) + MINSIZE;
	} else if (size < MINSIZE) {
		if (!size)
			mp->ys = MAX(MINSIZE, (MIN(mp->width, mp->height) - 1) / MINGRIDSIZE);
		else
			mp->ys = MINSIZE;
	} else
		mp->ys = MIN(size, MAX(MINSIZE, (MIN(mp->width, mp->height) - 1) /
				       MINGRIDSIZE));
	mp->xs = mp->ys;
	mp->logo_size_x = mp->logo->width / mp->xs + 1;
	mp->logo_size_y = mp->logo->height / mp->ys + 1;

	mp->ncols = MAX((mp->width - 1) / mp->xs, MINGRIDSIZE);
	mp->nrows = MAX((mp->height - 1) / mp->ys, MINGRIDSIZE);

	mp->xb = (mp->width - mp->ncols * mp->xs) / 2;
	mp->yb = (mp->height - mp->nrows * mp->ys) / 2;
	mp->maze_size = mp->ncols * mp->nrows;
	if (!mp->maze)
		if ((mp->maze = (unsigned int *) calloc(mp->maze_size,
				 sizeof (unsigned int))) == NULL) {
			free_maze(display, mp);
			return False;
		}
	if (!mp->move_list)
		if ((mp->move_list = (paths *) calloc(mp->maze_size,
				sizeof (paths))) == NULL) {
			free_maze(display, mp);
			return False;
		}
	if (!mp->save_path)
		if ((mp->save_path = (paths *) calloc(mp->maze_size,
				sizeof (paths))) == NULL) {
			free_maze(display, mp);
			return False;
		}
	if (!mp->path)
		if (( mp->path = (paths *) calloc(mp->maze_size,
				sizeof (paths))) == NULL) {
			free_maze(display, mp);
			return False;
		}
	return True;
}				/* end of set_maze_sizes */


static void
initialize_maze(ModeInfo * mi)
{				/* draw the surrounding wall and start/end squares */
	Display    *display = MI_DISPLAY(mi);
	mazestruct *mp = &mazes[MI_SCREEN(mi)];
	register int i, j, wall;

	if (MI_NPIXELS(mi) <= 2) {
		mp->color = MI_WHITE_PIXEL(mi);
	} else {
		mp->color = MI_PIXEL(mi, NRAND(MI_NPIXELS(mi)));
	}
	XSetForeground(display, mp->backGC, mp->color);
	XSetForeground(display, mp->grayGC, mp->color);
	/* initialize all squares */
	for (i = 0; i < mp->ncols; i++) {
		for (j = 0; j < mp->nrows; j++) {
			mp->maze[i * mp->nrows + j] = 0;
		}
	}

	/* top wall */
	for (i = 0; i < mp->ncols; i++) {
		mp->maze[i * mp->nrows] |= WALL_TOP;
	}

	/* right wall */
	for (j = 0; j < mp->nrows; j++) {
		mp->maze[(mp->ncols - 1) * mp->nrows + j] |= WALL_RIGHT;
	}

	/* bottom wall */
	for (i = 0; i < mp->ncols; i++) {
		mp->maze[i * mp->nrows + mp->nrows - 1] |= WALL_BOTTOM;
	}

	/* left wall */
	for (j = 0; j < mp->nrows; j++) {
		mp->maze[j] |= WALL_LEFT;
	}

	/* set start square */
	wall = NRAND(4);
	switch (wall) {
		case 0:
			i = NRAND(mp->ncols);
			j = 0;
			break;
		case 1:
			i = mp->ncols - 1;
			j = NRAND(mp->nrows);
			break;
		case 2:
			i = NRAND(mp->ncols);
			j = mp->nrows - 1;
			break;
		case 3:
			i = 0;
			j = NRAND(mp->nrows);
			break;
	}
	mp->maze[i * mp->nrows + j] |= START_SQUARE;
	mp->maze[i * mp->nrows + j] |= (DOOR_IN_TOP >> wall);
	mp->maze[i * mp->nrows + j] &= ~(WALL_TOP >> wall);
	mp->cur_sq_x = i;
	mp->cur_sq_y = j;
	mp->start_x = i;
	mp->start_y = j;
	mp->start_dir = wall;
	mp->sqnum = 0;

	/* set end square */
	wall = (wall + 2) % 4;
	switch (wall) {
		case 0:
			i = NRAND(mp->ncols);
			j = 0;
			break;
		case 1:
			i = mp->ncols - 1;
			j = NRAND(mp->nrows);
			break;
		case 2:
			i = NRAND(mp->ncols);
			j = mp->nrows - 1;
			break;
		case 3:
			i = 0;
			j = NRAND(mp->nrows);
			break;
	}
	mp->maze[i * mp->nrows + j] |= END_SQUARE;
	mp->maze[i * mp->nrows + j] |= (DOOR_OUT_TOP >> wall);
	mp->maze[i * mp->nrows + j] &= ~(WALL_TOP >> wall);
	mp->end_x = i;
	mp->end_y = j;
	mp->end_dir = wall;

	/* set logo */
	if ((mp->ncols > mp->logo_size_x + 6) &&
	    (mp->nrows > mp->logo_size_y + 6)) {
		mp->logo_x = NRAND(mp->ncols - mp->logo_size_x - 6) + 3;
		mp->logo_y = NRAND(mp->nrows - mp->logo_size_y - 6) + 3;

		for (i = 0; i < mp->logo_size_x; i++)
			for (j = 0; j < mp->logo_size_y; j++)
				mp->maze[(mp->logo_x + i) * mp->nrows + mp->logo_y + j] |=
					DOOR_IN_TOP;
	} else
		mp->logo_y = mp->logo_x = -1;
}

static int
choose_door(ModeInfo * mi)
{				/* pick a new path */
	mazestruct *mp = &mazes[MI_SCREEN(mi)];
	int         candidates[3];
	register int num_candidates;

	num_candidates = 0;

	/* top wall */
	if ((!(mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] &
	       DOOR_IN_TOP)) &&
	    (!(mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] &
	       DOOR_OUT_TOP)) &&
	    (!(mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] &
	       WALL_TOP))) {
		if (mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y - 1] &
		    DOOR_IN_ANY) {
			mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] |= WALL_TOP;
			mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y - 1] |=
				WALL_BOTTOM;
		} else
			candidates[num_candidates++] = 0;
	}
	/* right wall */
	if ((!(mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] &
	       DOOR_IN_RIGHT)) &&
	    (!(mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] &
	       DOOR_OUT_RIGHT)) &&
	    (!(mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] &
	       WALL_RIGHT))) {
		if (mp->maze[(mp->cur_sq_x + 1) * mp->nrows + mp->cur_sq_y] &
		    DOOR_IN_ANY) {
			mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] |= WALL_RIGHT;
			mp->maze[(mp->cur_sq_x + 1) * mp->nrows + mp->cur_sq_y] |=
				WALL_LEFT;
		} else
			candidates[num_candidates++] = 1;
	}
	/* bottom wall */
	if ((!(mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] &
	       DOOR_IN_BOTTOM)) &&
	    (!(mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] &
	       DOOR_OUT_BOTTOM)) &&
	    (!(mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] &
	       WALL_BOTTOM))) {
		if (mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y + 1] &
		    DOOR_IN_ANY) {
			mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] |= WALL_BOTTOM;
			mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y + 1] |= WALL_TOP;
		} else
			candidates[num_candidates++] = 2;
	}
	/* left wall */
	if ((!(mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] &
	       DOOR_IN_LEFT)) &&
	    (!(mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] &
	       DOOR_OUT_LEFT)) &&
	    (!(mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] &
	       WALL_LEFT))) {
		if (mp->maze[(mp->cur_sq_x - 1) * mp->nrows + mp->cur_sq_y] &
		    DOOR_IN_ANY) {
			mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] |= WALL_LEFT;
			mp->maze[(mp->cur_sq_x - 1) * mp->nrows + mp->cur_sq_y] |=
				WALL_RIGHT;
		} else
			candidates[num_candidates++] = 3;
	}
	/* done wall */
	if (num_candidates == 0)
		return (-1);
	if (num_candidates == 1)
		return (candidates[0]);
	return (candidates[NRAND(num_candidates)]);

}				/* end of choose_door() */

static void
draw_maze_walls(ModeInfo * mi)
{				/* pick a new path */
	mazestruct *mp = &mazes[MI_SCREEN(mi)];
	int         i, j, isize;

	MI_IS_DRAWN(mi) = True;

	for (i = 0; i < mp->ncols; i++) {
		isize = i * mp->nrows;
		for (j = 0; j < mp->nrows; j++) {
			/* Only need to draw half the walls... since they are shared */
			/* top wall */
			if (	/*(!(mp->maze[isize + j] & DOOR_IN_TOP)) &&
				   (!(mp->maze[isize + j] & DOOR_OUT_TOP)) && */
				   (mp->maze[isize + j] & WALL_TOP))
				draw_wall(mi, i, j, 0);
			/* left wall */
			if (	/*(!(mp->maze[isize + j] & DOOR_IN_RIGHT)) &&
				   (!(mp->maze[isize + j] & DOOR_OUT_RIGHT)) && */
				   (mp->maze[isize + j] & WALL_RIGHT))
				draw_wall(mi, i, j, 1);
		}
	}
}				/* end of draw_maze_walls() */

static int
backup(mazestruct * mp)
{				/* back up a move */
	mp->sqnum--;
	if (mp->sqnum >= 0) {
		mp->cur_sq_x = mp->move_list[mp->sqnum].x;
		mp->cur_sq_y = mp->move_list[mp->sqnum].y;
	}
	return (mp->sqnum);
}				/* end of backup() */

static void
create_maze_walls(ModeInfo * mi)
{				/* create a maze layout given the intiialized maze */
	mazestruct *mp = &mazes[MI_SCREEN(mi)];
	register int i, newdoor;

	for (;;) {
		mp->move_list[mp->sqnum].x = mp->cur_sq_x;
		mp->move_list[mp->sqnum].y = mp->cur_sq_y;
		mp->move_list[mp->sqnum].dir = -1;
		while ((newdoor = choose_door(mi)) == -1)	/* pick a door */
			if (backup(mp) == -1)	/* no more doors ... backup */
				return;		/* done ... return */

		/* mark the out door */
		mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] |=
			(DOOR_OUT_TOP >> newdoor);

		switch (newdoor) {
			case 0:
				mp->cur_sq_y--;
				break;
			case 1:
				mp->cur_sq_x++;
				break;
			case 2:
				mp->cur_sq_y++;
				break;
			case 3:
				mp->cur_sq_x--;
				break;
		}
		mp->sqnum++;

		/* mark the in door */
		mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] |=
			(DOOR_IN_TOP >> ((newdoor + 2) % 4));

		/* if end square set path length and save path */
		if (mp->maze[mp->cur_sq_x * mp->nrows + mp->cur_sq_y] & END_SQUARE) {
			mp->path_length = mp->sqnum;
			for (i = 0; i < mp->path_length; i++) {
				mp->save_path[i].x = mp->move_list[i].x;
				mp->save_path[i].y = mp->move_list[i].y;
				mp->save_path[i].dir = mp->move_list[i].dir;
			}
		}
	}

}				/* end of create_maze_walls() */

static void
draw_maze_border(ModeInfo * mi)
{				/* draw the maze outline */
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	mazestruct *mp = &mazes[MI_SCREEN(mi)];
	GC          gc = mp->backGC;
	register int i, j;

	if (mp->logo_x != -1) {
		(void) XPutImage(display, window, gc, mp->logo,
				 0, 0,
				 mp->xb + mp->xs * mp->logo_x +
			(mp->xs * mp->logo_size_x - mp->logo->width + 1) / 2,
				 mp->yb + mp->ys * mp->logo_y +
		       (mp->ys * mp->logo_size_y - mp->logo->height + 1) / 2,
				 mp->logo->width, mp->logo->height);
	}
	for (i = 0; i < mp->ncols; i++) {
		if (mp->maze[i * mp->nrows] & WALL_TOP) {
			XDrawLine(display, window, gc,
				  mp->xb + mp->xs * i, mp->yb,
				  mp->xb + mp->xs * (i + 1), mp->yb);
		}
		if ((mp->maze[i * mp->nrows + mp->nrows - 1] & WALL_BOTTOM)) {
			XDrawLine(display, window, gc,
				  mp->xb + mp->xs * i,
				  mp->yb + mp->ys * (mp->nrows),
				  mp->xb + mp->xs * (i + 1),
				  mp->yb + mp->ys * (mp->nrows));
		}
	}
	for (j = 0; j < mp->nrows; j++) {
		if (mp->maze[(mp->ncols - 1) * mp->nrows + j] & WALL_RIGHT) {
			XDrawLine(display, window, gc,
				  mp->xb + mp->xs * mp->ncols,
				  mp->yb + mp->ys * j,
				  mp->xb + mp->xs * mp->ncols,
				  mp->yb + mp->ys * (j + 1));
		}
		if (mp->maze[j] & WALL_LEFT) {
			XDrawLine(display, window, gc,
				  mp->xb, mp->yb + mp->ys * j,
				  mp->xb, mp->yb + mp->ys * (j + 1));
		}
	}

	draw_solid_square(mi, gc, mp->start_x, mp->start_y, mp->start_dir);
	draw_solid_square(mi, gc, mp->end_x, mp->end_y, mp->end_dir);
}				/* end of draw_maze() */

static void
try_to_move(ModeInfo *mi, int dir)
{                               /* based on solve_maze */
	mazestruct *mp = &mazes[MI_SCREEN(mi)];
	int colliding = mp->maze[mp->path[mp->current_path].x * mp->nrows +
		mp->path[mp->current_path].y] &
		(WALL_TOP >> dir); /*  Are we trying to go through a wall? */

	if (!colliding && ((mp->current_path == 0) ||
	/* We're either on the first path or we're not going backwards */
		((dir != (unsigned char) (mp->path[mp->current_path - 1].dir +
			2) % 4))) ) {
		mp->path[mp->current_path].dir = dir;
		enter_square(mi, mp->current_path);
		mp->current_path++;
#if 0
		(void) fprintf(stderr,
	"Calling draw_solid_square with params: x=%d, y= %d, dir=%d\n",
			(int) (mp->path[mp->current_path].x),
			(int) (mp->path[mp->current_path].y),
			(int) (dir));
#endif
		/* The following call is supposed to make things look more
		   intutive, i.e., that the square we think we are on is
		   filled in.
                   The direction has to be reversed to prevent it from
		   drawing squares on top of lines.
		*/
		draw_solid_square(mi, mp->backGC,
			(int) (mp->path[mp->current_path].x),
			(int) (mp->path[mp->current_path].y),
			(int) ((dir + 2) % 4));
		if (mp->maze[mp->path[mp->current_path].x * mp->nrows +
			  mp->path[mp->current_path].y] & START_SQUARE) {
			mp->solving = 0;
			return;
		}
	} else {
		if (!colliding) {
			draw_solid_square(mi, mp->grayGC,
				(int) (mp->path[mp->current_path].x),
				(int) (mp->path[mp->current_path].y),
				(int) (mp->path[mp->current_path - 1].dir +
				2) % 4);
			mp->path[mp->current_path].dir = 4;
			mp->current_path--;
			draw_solid_square(mi, mp->grayGC,
				(int) (mp->path[mp->current_path].x),
				(int) (mp->path[mp->current_path].y),
				(int) (mp->path[mp->current_path].dir));
		}
#if 0
		else {
			/* Beep if we can't go in that direction */
			(void) fprintf(stdout, "\a");
			(void) fflush(stdout);
		}
#endif
	}
}         /* end of try_to_move() */

static void
solve_maze(ModeInfo * mi)
{				/* solve it with graphical feedback */
	mazestruct *mp = &mazes[MI_SCREEN(mi)];

	if (!mp->solving) {
		/* plug up the surrounding wall */
		mp->maze[mp->start_x * mp->nrows + mp->start_y] |=
			(WALL_TOP >> mp->start_dir);
		mp->maze[mp->end_x * mp->nrows + mp->end_y] |=
			(WALL_TOP >> mp->end_dir);

		/* initialize search path */
		mp->current_path = 0;
		mp->path[mp->current_path].x = mp->end_x;
		mp->path[mp->current_path].y = mp->end_y;
		mp->path[mp->current_path].dir = -1;

		mp->solving = 1;
	}
	if (++mp->path[mp->current_path].dir >= 4) {
		if (mp->current_path == 0) {
			/* this can happen if a person backs up from going
			   the right way and then autosolves */
			mp->path[mp->current_path].dir = -1;
			return;
		}
		/* This draw is to fill in the dead ends,
		   it ends up drawing more gray boxes then it needs to. */
		draw_solid_square(mi, mp->grayGC,
				  (int) (mp->path[mp->current_path].x),
				  (int) (mp->path[mp->current_path].y),
			 (int) (mp->path[mp->current_path - 1].dir + 2) % 4);

		mp->current_path--;
		draw_solid_square(mi, mp->grayGC,
				  (int) (mp->path[mp->current_path].x),
				  (int) (mp->path[mp->current_path].y),
				  (int) (mp->path[mp->current_path].dir));
	} else if (!(mp->maze[mp->path[mp->current_path].x * mp->nrows +
			      mp->path[mp->current_path].y] &
		     (WALL_TOP >> mp->path[mp->current_path].dir)) &&
		   ((mp->current_path == 0) ||
		    ((mp->path[mp->current_path].dir !=
		      (unsigned char) (mp->path[mp->current_path - 1].dir +
				       2) % 4)))) {
		enter_square(mi, mp->current_path);
		mp->current_path++;
		/* This next call is to make the appearance more 'intuitive' */
		draw_solid_square(mi, mp->backGC,
                                  (int) (mp->path[mp->current_path].x),
                                  (int) (mp->path[mp->current_path].y),
                         (int) (mp->path[mp->current_path - 1].dir + 2) % 4);
		if (mp->maze[mp->path[mp->current_path].x * mp->nrows +
			     mp->path[mp->current_path].y] & START_SQUARE) {
			mp->solving = 0;
			return;
		}
	}
}				/* end of solve_maze() */

static void
mouse_maze(ModeInfo * mi)
{	/* solve it with graphical feedback */
	mazestruct *mp = &mazes[MI_SCREEN(mi)];
	Window      r, c;
	int         dx, dy, cx, cy;
	unsigned int m;

	(void) XQueryPointer(MI_DISPLAY(mi), MI_WINDOW(mi),
	  &r, &c, &dx, &dy, &cx, &cy, &m);
	dx = cx - (mp->path[mp->current_path].x * mp->xs + mp->xb) - mp->xs / 2;
	dy = cy - (mp->path[mp->current_path].y * mp->ys + mp->yb) - mp->ys / 2;
	if (cx <= mp->xb || cy <= mp->yb || cx > mp->ncols * mp->xs + mp->xb ||
			cy > mp->nrows * mp->ys + mp->yb) {
		solve_maze(mi);
		return;
	}
	if (2 * abs(dx) / (mp->xs + 1) > 2 * abs(dy) / mp->ys) {
		if (dx > 0) {
			try_to_move(mi, DIR_RIGHT);
		} else {
			try_to_move(mi, DIR_LEFT);
		}
	} else if (2 * abs(dx) / mp->xs < 2 * abs(dy) / (mp->ys + 1)) {
		if (dy > 0) {
			try_to_move(mi, DIR_DOWN);
		} else {
			try_to_move(mi, DIR_UP);
		}
	}
}

static Bool
init_stuff(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	mazestruct *mp = &mazes[MI_SCREEN(mi)];

	if (mp->logo == None) {
		getImage(mi, &mp->logo, ICON_WIDTH, ICON_HEIGHT, ICON_BITS,
#ifdef HAVE_XPM
			 DEFAULT_XPM, ICON_NAME,
#endif
			 &mp->graphics_format, &mp->cmap, &mp->black);
		if (mp->logo == None) {
			free_maze(display, mp);
			return False;
		}
	}
#ifndef STANDALONE
	if (mp->cmap != None) {
		setColormap(display, window, mp->cmap, MI_IS_INWINDOW(mi));
		if (mp->backGC == None) {
			XGCValues   xgcv;

			xgcv.background = mp->black;
			if ((mp->backGC = XCreateGC(display, window, GCBackground,
					 &xgcv)) == None) {
				free_maze(display, mp);
				return False;
			}
		}
	} else
#endif /* STANDALONE */
	{
		mp->black = MI_BLACK_PIXEL(mi);
		mp->backGC = MI_GC(mi);
	}
	return True;
}

void
init_maze(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	mazestruct *mp;

	if (mazes == NULL) {
		if ((mazes = (mazestruct *) calloc(MI_NUM_SCREENS(mi),
					       sizeof (mazestruct))) == NULL)
			return;
	}
	mp = &mazes[MI_SCREEN(mi)];

	if (!init_stuff(mi))
		return;

	mp->width = MI_WIDTH(mi);
	mp->height = MI_HEIGHT(mi);
	mp->width = (mp->width >= 32) ? mp->width : 32;
	mp->height = (mp->height >= 32) ? mp->height : 32;

	if (mp->graypix == None)
		if ((mp->graypix = XCreateBitmapFromData(display, MI_WINDOW(mi),
			     (char *) gray1_bits, gray1_width, gray1_height)) == None) {
			free_maze(display, mp);
			return;
		}
	if (!mp->grayGC) {
		if ((mp->grayGC = XCreateGC(display, MI_WINDOW(mi),
				 0L, (XGCValues *) 0)) == None) {
			free_maze(display, mp);
			return;
		}
		XSetBackground(display, mp->grayGC, mp->black);
		XSetFillStyle(display, mp->grayGC, FillOpaqueStippled);
		XSetStipple(display, mp->grayGC, mp->graypix);
	}
	mp->solving = 0;
	mp->stage = 0;
	mp->time = 0;
	mp->cycles = MI_CYCLES(mi);
	if (mp->cycles < 4)
		mp->cycles = 4;
}

void
draw_maze(ModeInfo * mi)
{
	mazestruct *mp;

	if (mazes == NULL)
		return;
	mp = &mazes[MI_SCREEN(mi)];
	if (mp->graypix == None)
		return;
	if (mp->solving) {
		if (trackmouse)
			mouse_maze(mi);
		else
			solve_maze(mi);
		return;
	}
	switch (mp->stage) {
		case 0:
			MI_CLEARWINDOWCOLORMAP(mi, mp->backGC, mp->black);

			if (!set_maze_sizes(mi))
				return;
			initialize_maze(mi);
			create_maze_walls(mi);
			mp->stage++;
			break;
		case 1:
			draw_maze_border(mi);
			mp->stage++;
			break;
		case 2:
			draw_maze_walls(mi);
			mp->stage++;
			break;
		case 3:
			if (++mp->time > mp->cycles / (4 * (1 + trackmouse)))
				mp->stage++;
			break;
		case 4:
			if (trackmouse) {
				/* Initialize paths and draw a square or two */
				int i;
				for (i = 0; i < 4; i++)
					solve_maze(mi);
			} else {
				solve_maze(mi);
			}
			mp->stage++;
			break;
		case 5:
			if (++mp->time > 3 * mp->cycles / 4)
				init_maze(mi);
			break;
	}
}				/*  end of draw_maze() */

void
release_maze(ModeInfo * mi)
{
	if (mazes != NULL) {
		int         screen;

		for (screen = 0; screen < MI_NUM_SCREENS(mi); screen++)
			free_maze(MI_DISPLAY(mi), &mazes[screen]);
		free(mazes);
		mazes = (mazestruct *) NULL;
	}
}

void
refresh_maze(ModeInfo * mi)
{
	mazestruct *mp;

	if (mazes == NULL)
		return;
	mp = &mazes[MI_SCREEN(mi)];
	if (mp->graypix == None)
		return;

#ifdef HAVE_XPM
	if (mp->graphics_format >= IS_XPM) {
		/* This is needed when another program changes the colormap. */
		free_maze(MI_DISPLAY(mi), mp);
		init_maze(mi);
		return;
	}
#endif
	MI_CLEARWINDOWCOLORMAP(mi, mp->backGC, mp->black);
	XSetForeground(MI_DISPLAY(mi), mp->backGC, mp->color);
	if (mp->stage >= 1) {
		mp->stage = 3;
		mp->sqnum = 0;
		mp->cur_sq_x = mp->start_x;
		mp->cur_sq_y = mp->start_y;
		mp->maze[mp->start_x * mp->nrows + mp->start_y] |= START_SQUARE;
		mp->maze[mp->start_x * mp->nrows + mp->start_y] |=
			(DOOR_IN_TOP >> mp->start_dir);
		mp->maze[mp->start_x * mp->nrows + mp->start_y] &=
			~(WALL_TOP >> mp->start_dir);
		mp->maze[mp->end_x * mp->nrows + mp->end_y] |= END_SQUARE;
		mp->maze[mp->end_x * mp->nrows + mp->end_y] |=
			(DOOR_OUT_TOP >> mp->end_dir);
		mp->maze[mp->end_x * mp->nrows + mp->end_y] &=
			~(WALL_TOP >> mp->end_dir);
		draw_maze_border(mi);
		draw_maze_walls(mi);
	}
	mp->solving = 0;
}

#endif /* MODE_maze */
