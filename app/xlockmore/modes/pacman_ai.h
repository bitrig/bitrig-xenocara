/*-
 * Copyright (c) 2002 by Edwin de Jong <mauddib@gmx.net>.
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
 */

/* this file is a part of pacman.c, and included via pacman.h.  It handles the
   AI of the ghosts and the pacman. */


/* fills array of DIRVECS size with possible directions, returns number of
   directions. 'posdirs' points to a possibly undefined array of four
   integers.  The vector will contain booleans wether the direction is
   a possible direction or not.  Reverse directions are deleted. */
static int
ghost_get_posdirs(pacmangamestruct *pp, int *posdirs, ghoststruct *g)
{
	unsigned int i, nrdirs = 0;

	/* bit of black magic here */
	for (i = 0; i < DIRVECS; i++) {
		/* remove opposite */
		if (g->lastbox != NOWHERE && i == (g->lastbox + 2) % DIRVECS
				&& g->aistate != goingout) {
			posdirs[i] = 0;
			continue;
		}
		if (g->aistate == goingout && i == 1) {
			posdirs[i] = 0;
			continue;
		}
		/* check if possible direction */
		if ((posdirs[i] =
			check_pos(pp, g->row + dirvecs[i].dy,
				g->col + dirvecs[i].dx,
				g->aistate == goingout ? True : False)) ==
				True) {
			nrdirs++;
		}
	}

	return nrdirs;
}

/* Directs ghost to a random direction, exluding opposite (except in the
   impossible situation that there is only one valid direction). */
static void
ghost_random(pacmangamestruct *pp, ghoststruct *g)
{
	int posdirs[DIRVECS], nrdirs = 0, i, dir = 0;

	nrdirs = ghost_get_posdirs(pp, posdirs, g);
	for (i = 0; i < DIRVECS; i++)
		if (posdirs[i] == True) dir = i;

	if (nrdirs == 0) dir = (g->lastbox + 2) % DIRVECS;
	else if (nrdirs > 1)
		for (i = 0; i < DIRVECS; i++) {
			if (posdirs[i] == True && NRAND(nrdirs) == 0) {
				dir = i;
				break;
			}
		}

	g->nextrow = g->row + dirvecs[dir].dy;
	g->nextcol = g->col + dirvecs[dir].dx;
	g->lastbox = dir;
}

/* Determines best direction to chase the pacman and goes that direction. */
static void
ghost_chasing(pacmangamestruct *pp, ghoststruct *g)
{
	int posdirs[DIRVECS], nrdirs = 0, i, dir = 0, highest = -100000,
		thisvecx, thisvecy, thisvec;

	nrdirs = ghost_get_posdirs(pp, posdirs, g);
	for (i = 0; i < DIRVECS; i++)
		if (posdirs[i] == True) dir = i;

	if (nrdirs == 0) dir = (g->lastbox + 2) % DIRVECS;
	else if (nrdirs > 1)
		for (i = 0; i < DIRVECS; i++) {
			if (posdirs[i] == False) continue;
			thisvecx = (pp->pacman.col - g->col) * dirvecs[i].dx;
			thisvecy = (pp->pacman.row - g->row) * dirvecs[i].dy;
			thisvec = thisvecx + thisvecy;
			if (thisvec >= highest) {
				dir = i;
				highest = thisvec;
			}
		}

	g->nextrow = g->row + dirvecs[dir].dy;
	g->nextcol = g->col + dirvecs[dir].dx;
	g->lastbox = dir;
}

/* Determines the best direction to go away from the pacman, and goes that
   direction. */
static void
ghost_hiding(pacmangamestruct *pp, ghoststruct *g)
{
	int posdirs[DIRVECS], nrdirs = 0, i, dir = 0, highest = -100000,
		thisvecx, thisvecy, thisvec;

	nrdirs = ghost_get_posdirs(pp, posdirs, g);
	for (i = 0; i < DIRVECS; i++)
		if (posdirs[i] == True) dir = i;

	if (nrdirs == 0) dir = (g->lastbox + 2) % DIRVECS;
	else if (nrdirs > 1)
		for (i = 0; i < DIRVECS; i++) {
			if (posdirs[i] == False) continue;
			thisvecx = (g->col - pp->pacman.col) * dirvecs[i].dx;
			thisvecy = (g->row - pp->pacman.row) * dirvecs[i].dy;
			thisvec = thisvecx + thisvecy;
			if (thisvec >= highest)
				dir = i;
		}

	g->nextrow = g->row + dirvecs[dir].dy;
	g->nextcol = g->col + dirvecs[dir].dx;
	g->lastbox = dir;
}

/* Determines a vector from the pacman position, towards all dots.  The vector
   is inversely proportional to the square of the distance of each dot.
   (so, close dots attract more than far away dots). */
static void
pac_dot_vec(pacmangamestruct *pp, pacmanstruct *p, long *vx, long *vy)
{
	int x, y, bx = 0, by = 0, ex = LEVWIDTH, ey = LEVHEIGHT;
	long dx, dy, dist, top = 0;

#if 0
	int rnr = NRAND(50);
	/* determine begin and end vectors */

	switch (rnr) {
		case 0: ex = LEVHEIGHT/2; break;
		case 1: bx = LEVHEIGHT/2; break;
		case 2: ey = LEVHEIGHT/2; break;
		case 3: by = LEVHEIGHT/2; break;
	}
#endif
	*vx = *vy = 0;

	for (y = by; y < ey; y++)
		for (x = bx; x < ex; x++)
			if (check_dot(pp, x, y) == 1) {
				dx = (long)x - (long)(p->col);
				dy = (long)y - (long)(p->row);
				dist = dx * dx + dy * dy;
				if (dist > top)
					top = dist;
				*vx += (dx * ((long)LEVWIDTH * (long)LEVHEIGHT))
				       / dist;
				*vy += (dy * ((long)LEVWIDTH * (long)LEVHEIGHT))
				       / dist;
			}
}

/* Determine a vector towards the closest ghost (in one loop, so we spare a
   couple of cycles which we can spoil somewhere else just as fast). */
static int
pac_ghost_prox_and_vector(pacmangamestruct *pp, pacmanstruct *p,
		long *vx, long *vy)
{
	long dx, dy, dist, closest = 100000;
	unsigned int g;

	if (vx != NULL)
		*vx = *vy = 0;

	for (g = 0; g < pp->nghosts; g++) {
		if (pp->ghosts[g].dead    == True  ||
		    pp->ghosts[g].aistate == inbox ||
		    pp->ghosts[g].aistate == goingout)
			continue;
		dx = pp->ghosts[g].col + /*dirvecs[pp->ghosts[g].lastbox].dx*/ -
			p->col;
		dy = pp->ghosts[g].row + /*dirvecs[pp->ghosts[g].lastbox].dy*/ -
		       	p->row;
		dist = dx * dx + dy * dy;
		if (dist < closest) {
			closest = dist;
			if (vx != NULL) {
				*vx = dx; *vy = dy;
			}
		}
	}
	return closest;
}

/* fills array of DIRVECS size with possible directions, returns number of
   directions.  posdirs should point to an array of 4 integers. */
static int
pac_get_posdirs(pacmangamestruct *pp, pacmanstruct *p, int *posdirs)
{
	int nrdirs = 0;
	unsigned int i;

	for (i = 0; i < DIRVECS; i++) {
		/* if we just ate, or we are in a statechange, it is allowed
		   to go the opposite direction */
		if (p->justate == 0 &&
		    p->state_change == 0 &&
		    p->lastbox != NOWHERE &&
		    i == (p->lastbox + 2) % DIRVECS) {
			posdirs[i] = 0;
		} else if ((posdirs[i] =
			check_pos(pp, p->row + dirvecs[i].dy,
				p->col + dirvecs[i].dx, 0)) == 1)
			nrdirs++;
	}
	p->state_change = 0;

	return nrdirs;
}

/* Clears the trace of vectors. */
static void
pac_clear_trace(pacmanstruct *p)
{
	int i;

	for(i = 0; i < TRACEVECS; i++) {
		p->trace[i].vx = NOWHERE; p->trace[i].vy = NOWHERE;
	}
	p->cur_trace = 0;
}

/* Adds a new vector to the trace. */
static void
pac_save_trace(pacmanstruct *p, const int vx, const int vy)
{
	if (!(vx == NOWHERE && vy == NOWHERE)) {
		p->trace[p->cur_trace].vx = vx;
		p->trace[p->cur_trace].vy = vy;
		p->cur_trace = (p->cur_trace + 1) % TRACEVECS;
	}
}

/* Check if a vector can be found in the trace. */
static int
pac_check_trace(const pacmanstruct *p, const int vx, const int vy)
{
	int i, curel;

	for (i = 1; i < TRACEVECS; i++) {
		curel = (p->cur_trace - i + TRACEVECS) % TRACEVECS;
		if (p->trace[curel].vx == NOWHERE &&
		    p->trace[curel].vy == NOWHERE)
			continue;
		if (p->trace[curel].vx == vx &&
		    p->trace[curel].vy == vy)
			return 1;
	}


	return 0;
}

/* AI mode "Eating" for pacman. Tries to eat as many dots as possible, goes
   to "hiding" if too close to a ghost. If in state "hiding" the vectors
   of the ghosts are also taken into account (thus not running towards them
   is the general idea). */
static void
pac_eating(pacmangamestruct *pp, pacmanstruct *p)
{
	int posdirs[DIRVECS], nrdirs, i, highest = -(1 << 16),
		score, dir = 0, dotfound = 0, prox, worst = 0;
	long vx, vy;

	if ((prox = pac_ghost_prox_and_vector(pp, p, &vx, &vy)) <
				4 * 4 && p->aistate == ps_eating) {
		p->aistate = ps_hiding;
		p->state_change = 1;
		pac_clear_trace(p);
	}

	if (prox > 6 * 6 && p->aistate == ps_hiding) {
		p->aistate = ps_eating;
		if (p->justate == 0) p->state_change = 1;
		pac_clear_trace(p);
	}

	if (prox < 3 * 3) p->state_change = 1;

	nrdirs = pac_get_posdirs(pp, p, posdirs);

	/* remove directions which lead to ghosts */
	if (p->aistate == ps_hiding) {
		for (i = 0; i < DIRVECS; i++) {
			if (posdirs[i] == 0) continue;
			score = vx * dirvecs[i].dx + vy * dirvecs[i].dy;
			if (score > highest) {
				worst = i;
				highest = score;
			}
			dir = i;
		}
		nrdirs--;
		posdirs[worst] = 0;
		highest = -(1 << 16);
	}

	/* get last possible direction if all else fails */
	for (i = 0; i < DIRVECS; i++)
		if (posdirs[i] != 0) dir = i;

	pac_dot_vec(pp, p, &vx, &vy);

	if (vx != NOWHERE && vy != NOWHERE && pac_check_trace(p, vx, vy) > 0) {
		p->roundscore++;
		if (p->roundscore >= 12) {
			p->roundscore = 0;
			p->aistate = ps_random;
			pac_clear_trace(p);
		}
	} else
		p->roundscore = 0;

	if (p->justate == 0) pac_save_trace(p, vx, vy);

	for (i = 0; i < DIRVECS; i++) {
		if (posdirs[i] == 0) continue;
		score = dirvecs[i].dx * vx + dirvecs[i].dy * vy;
		if (check_dot(pp, p->col + dirvecs[i].dx,
				  p->row + dirvecs[i].dy) == 1) {
			if (dotfound == 0) {
				highest = score;
				dir = i;
				dotfound = 1;
			} else if (score > highest) {
				highest = score;
				dir = i;
			}
		} else if (score > highest && dotfound == 0) {
			dir = i;
			highest = score;
		}
	}

	p->nextrow = p->row + dirvecs[dir].dy;
	p->nextcol = p->col + dirvecs[dir].dx;
	p->lastbox = dir;
}

#if 0
/* Tries to catch the ghosts. */
static void
pac_chasing(pacmangamestruct *pp, pacmanstruct *p)
{
}
#endif

/* Goes completely random, but not in the opposite direction.  Used when a
   loop is detected. */
static void
pac_random(pacmangamestruct *pp, pacmanstruct *p)
{
	int posdirs[DIRVECS], nrdirs, i, dir = -1, lastdir = 0;

	if (pac_ghost_prox_and_vector(pp, p, NULL, NULL) < 5 * 5) {
		p->aistate = ps_hiding;
		p->state_change = 1;
	}
	if (NRAND(20) == 0) {
		p->aistate = ps_eating;
		p->state_change = 1;
		pac_clear_trace(p);
	}

	nrdirs = pac_get_posdirs(pp, p, posdirs);

	for (i = 0; i < DIRVECS; i++) {
		if (posdirs[i] == 0) continue;
		lastdir = i;
		if (check_dot(pp, p->col + dirvecs[i].dx,
			p->row + dirvecs[i].dy) == 1) {
			dir = i;
			p->aistate = ps_eating;
			p->state_change = 1;
			pac_clear_trace(p);
			break;
		} else if (NRAND(nrdirs) == 0)
			dir = i;
	}

	if (dir == -1) dir = lastdir;

	p->nextrow = p->row + dirvecs[dir].dy;
	p->nextcol = p->col + dirvecs[dir].dx;
	p->lastbox = dir;
}

static int
pac_get_vector_screen(pacmangamestruct *pp, pacmanstruct *p,
		const int x, const int y, int *vx, int *vy)
{
	int lx, ly;

	lx = (x - pp->xb) / pp->xs;
	ly = (y - pp->yb) / pp->ys;

	if (lx < 0) lx = 0;
	else if ((unsigned int) lx > LEVWIDTH) lx = LEVWIDTH - 1;

	if (ly < 0) ly = 0;
	else if ((unsigned int) ly > LEVHEIGHT) ly = LEVHEIGHT - 1;

	*vx = lx - p->col;
	*vy = ly - p->row;

	if (lx == p->oldlx && ly == p->oldly) return 0;
	p->oldlx = lx; p->oldly = ly;
	return 1;
}

static int
pac_trackmouse(ModeInfo * mi, pacmangamestruct *pp, pacmanstruct *p)
{
	int dx, dy, cx, cy, vx, vy;
	unsigned int m;
	int posdirs[DIRVECS], i, dir = -1, highest = -(2 << 16), score;
	Window r, c;

	(void) XQueryPointer(MI_DISPLAY(mi), MI_WINDOW(mi),
			 &r, &c, &dx, &dy, &cx, &cy, &m);

	if (cx <= 0 || cy <= 0 ||
                    cx >= MI_WIDTH(mi) - 1 ||
                    cy >= MI_HEIGHT(mi) - 1)
		return 0;
	/* get vector */
	p->state_change = pac_get_vector_screen(pp, p, cx, cy, &vx, &vy);

	(void) pac_get_posdirs(pp, p, posdirs);

	for (i = 0; i < DIRVECS; i++) {
		if (posdirs[i] == 0) continue;
		score = dirvecs[i].dx * vx + dirvecs[i].dy * vy;
		if (score > highest) {
			highest = score;
			dir = i;
		}
	}

	p->nextrow = p->row + dirvecs[dir].dy;
	p->nextcol = p->col + dirvecs[dir].dx;
	p->lastbox = dir;
	return 1;
}

/* Calls correct state function, and changes between states. */
static void
ghost_update(pacmangamestruct *pp, ghoststruct *g)
{

	if (!(g->nextrow == NOWHERE && g->nextcol == NOWHERE)) {
		g->row = g->nextrow;
		g->col = g->nextcol;
	}

	/* update ghost */
	if (g->dead == True) /* dead ghosts are dead,
				so they don't run around */
		return;

	if ((g->aistate == randdir || g->aistate == chasing) &&
			NRAND(10) == 0) {
		switch (NRAND(3)) {
			case 0:
				g->aistate = randdir; break;
			case 1:
				g->aistate = chasing; break;
			case 2:
				g->aistate = chasing; break;
		}

	} else if (g->aistate == inbox) {
		if (g->timeleft < 0)
			g->aistate = goingout;
		else
			g->timeleft--;

	} else if (g->aistate == goingout) {
		if (g->col < LEVWIDTH/2 - JAILWIDTH/2 ||
		    g->col > LEVWIDTH/2 + JAILWIDTH/2 ||
		    g->row < LEVHEIGHT/2 - JAILHEIGHT/2 ||
		    g->row > LEVHEIGHT/2 + JAILHEIGHT/2 )
			g->aistate = randdir;
	}

	switch (g->aistate) {
		case inbox:
		case goingout:
		case randdir:
			ghost_random(pp, g);
			break;
		case chasing:
			ghost_chasing(pp, g);
			break;
		case hiding:
			ghost_hiding(pp, g);
			break;
	}

	g->cfactor = GETFACTOR(g->nextcol, g->col);
	g->rfactor = GETFACTOR(g->nextrow, g->row);
}

/* Calls correct pacman state function. */
static void
pac_update(ModeInfo *mi, pacmangamestruct *pp, pacmanstruct *p)
{
	if (!(p->nextrow == NOWHERE && p->nextcol == NOWHERE)) {
		p->row = p->nextrow;
		p->col = p->nextcol;
	}

	if (pp->level[p->row * LEVWIDTH + p->col] == '.') {
		pp->level[p->row * LEVWIDTH + p->col] = ' ';
		if (!trackmouse)
			p->justate = 1;
		pp->dotsleft--;
	} else if (!trackmouse) {
		p->justate = 0;
	}

	if (!(trackmouse && pac_trackmouse(mi, pp, p))) {
		/* update pacman */
		switch (p->aistate) {
			case ps_eating:
				pac_eating(pp, p);
				break;
			case ps_hiding:
				pac_eating(pp, p);
				break;
			case ps_random:
				pac_random(pp, p);
				break;
			case ps_chasing:
#if 0
				pac_chasing(pp, p);
#endif
				break;
		}
	}

	p->cfactor = GETFACTOR(p->nextcol, p->col);
	p->rfactor = GETFACTOR(p->nextrow, p->row);
}
