/*
 * calmwm - the calm window manager
 *
 * Copyright (c) 2008 Owain G. Ainsworth <oga@openbsd.org>
 * Copyright (c) 2004 Marius Aamodt Eriksen <marius@monkey.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $OpenBSD: menu.c,v 1.42 2012/11/07 14:39:44 okan Exp $
 */

#include <sys/param.h>
#include <sys/queue.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>

#include "calmwm.h"

#define PROMPT_SCHAR	"\xc2\xbb"
#define PROMPT_ECHAR	"\xc2\xab"

enum ctltype {
	CTL_NONE = -1,
	CTL_ERASEONE = 0, CTL_WIPE, CTL_UP, CTL_DOWN, CTL_RETURN,
	CTL_TAB, CTL_ABORT, CTL_ALL
};

struct menu_ctx {
	struct screen_ctx 	*sc;
	char			 searchstr[MENU_MAXENTRY + 1];
	char			 dispstr[MENU_MAXENTRY*2 + 1];
	char			 promptstr[MENU_MAXENTRY + 1];
	int			 hasprompt;
	int			 list;
	int			 listing;
	int			 changed;
	int			 noresult;
	int			 prev;
	int			 entry;
	int			 height;
	int			 width;
	int			 num;
	int 			 flags;
	int			 x;
	int			 y;
    	void (*match)(struct menu_q *, struct menu_q *, char *);
    	void (*print)(struct menu *, int);
};
static struct menu	*menu_handle_key(XEvent *, struct menu_ctx *,
			     struct menu_q *, struct menu_q *);
static void		 menu_handle_move(XEvent *, struct menu_ctx *,
			     struct screen_ctx *);
static struct menu	*menu_handle_release(XEvent *, struct menu_ctx *,
			     struct screen_ctx *, struct menu_q *);
static void		 menu_draw(struct screen_ctx *, struct menu_ctx *,
			     struct menu_q *, struct menu_q *);
static int		 menu_calc_entry(struct screen_ctx *, struct menu_ctx *,
			     int, int);
static int		 menu_keycode(XKeyEvent *, enum ctltype *,
                             char *);

void
menu_init(struct screen_ctx *sc)
{
	XGCValues	 gv;

	sc->menuwin = XCreateSimpleWindow(X_Dpy, sc->rootwin, 0, 0, 1, 1,
	    Conf.bwidth,
	    sc->color[CWM_COLOR_FG_MENU].pixel,
	    sc->color[CWM_COLOR_BG_MENU].pixel);

	gv.foreground =
	    sc->color[CWM_COLOR_FG_MENU].pixel^sc->color[CWM_COLOR_BG_MENU].pixel;
	gv.background = sc->color[CWM_COLOR_BG_MENU].pixel;
	gv.function = GXxor;

	sc->gc = XCreateGC(X_Dpy, sc->menuwin,
	    GCForeground|GCBackground|GCFunction, &gv);
}

struct menu *
menu_filter(struct screen_ctx *sc, struct menu_q *menuq, char *prompt,
    char *initial, int flags, 
    void (*match)(struct menu_q *, struct menu_q *, char *),
    void (*print)(struct menu *, int))
{
	struct menu_ctx		 mc;
	struct menu_q		 resultq;
	struct menu		*mi = NULL;
	XEvent			 e;
	Window			 focuswin;
	int			 evmask, focusrevert;
	int			 xsave, ysave, xcur, ycur;

	TAILQ_INIT(&resultq);

	bzero(&mc, sizeof(mc));

	xu_ptr_getpos(sc->rootwin, &mc.x, &mc.y);

	xsave = mc.x;
	ysave = mc.y;

	mc.sc = sc;
	mc.flags = flags;
	if (prompt == NULL) {
		evmask = MENUMASK;
		mc.promptstr[0] = '\0';
		mc.list = 1;
	} else {
		evmask = MENUMASK | KEYMASK; /* only accept keys if prompt */
		(void)snprintf(mc.promptstr, sizeof(mc.promptstr), "%s%s",
		    prompt, PROMPT_SCHAR);
		(void)snprintf(mc.dispstr, sizeof(mc.dispstr), "%s%s%s",
		    mc.promptstr, mc.searchstr, PROMPT_ECHAR);
		mc.width = font_width(sc, mc.dispstr, strlen(mc.dispstr));
		mc.hasprompt = 1;
	}

	if (initial != NULL)
		(void)strlcpy(mc.searchstr, initial, sizeof(mc.searchstr));
	else
		mc.searchstr[0] = '\0';

	mc.match = match;
	mc.print = print;
	mc.entry = mc.prev = -1;

	XMoveResizeWindow(X_Dpy, sc->menuwin, mc.x, mc.y, mc.width,
	    font_height(sc));
	XSelectInput(X_Dpy, sc->menuwin, evmask);
	XMapRaised(X_Dpy, sc->menuwin);

	if (xu_ptr_grab(sc->menuwin, MENUGRABMASK, Cursor_question) < 0) {
		XUnmapWindow(X_Dpy, sc->menuwin);
		return (NULL);
	}

	XGetInputFocus(X_Dpy, &focuswin, &focusrevert);
	XSetInputFocus(X_Dpy, sc->menuwin, RevertToPointerRoot, CurrentTime);

	/* make sure keybindings don't remove keys from the menu stream */
	XGrabKeyboard(X_Dpy, sc->menuwin, True,
	    GrabModeAsync, GrabModeAsync, CurrentTime);

	for (;;) {
		mc.changed = 0;

		XWindowEvent(X_Dpy, sc->menuwin, evmask, &e);

		switch (e.type) {
		case KeyPress:
			if ((mi = menu_handle_key(&e, &mc, menuq, &resultq))
			    != NULL)
				goto out;
			/* FALLTHROUGH */
		case Expose:
			menu_draw(sc, &mc, menuq, &resultq);
			break;
		case MotionNotify:
			menu_handle_move(&e, &mc, sc);
			break;
		case ButtonRelease:
			if ((mi = menu_handle_release(&e, &mc, sc, &resultq))
			    != NULL)
				goto out;
			break;
		default:
			break;
		}
	}
out:
	if ((mc.flags & CWM_MENU_DUMMY) == 0 && mi->dummy) {
	       	/* no mouse based match */
		xfree(mi);
		mi = NULL;
	}

	XSetInputFocus(X_Dpy, focuswin, focusrevert, CurrentTime);
	/* restore if user didn't move */
	xu_ptr_getpos(sc->rootwin, &xcur, &ycur);
	if (xcur == mc.x && ycur == mc.y)
		xu_ptr_setpos(sc->rootwin, xsave, ysave);
	xu_ptr_ungrab();

	XUnmapWindow(X_Dpy, sc->menuwin);
	XUngrabKeyboard(X_Dpy, CurrentTime);

	return (mi);
}

static struct menu *
menu_complete_path(struct menu_ctx *mc)
{
	struct menu		*mi, *mr;
	struct menu_q		 menuq;
	char *path = NULL;

	path = xcalloc(1, sizeof(mr->text));
	mr = xcalloc(1, sizeof(*mr));

	TAILQ_INIT(&menuq);
	if ((mi = menu_filter(mc->sc, &menuq, mc->searchstr, NULL,
	    CWM_MENU_DUMMY, search_match_path_any, NULL)) != NULL) {
		mr->abort = mi->abort;
		mr->dummy = mi->dummy;
		strlcpy(path, mi->text, sizeof(mi->text));
	}
	
	while ((mi = TAILQ_FIRST(&menuq)) != NULL) {
		TAILQ_REMOVE(&menuq, mi, entry);
		xfree(mi);
	}

	if (path[0] != '\0') 
		snprintf(mr->text, sizeof(mr->text), "%s \"%s\"",
			mc->searchstr, path);
	else if (!mr->abort)
		strlcpy(mr->text,  mc->searchstr, sizeof(mr->text));
	xfree(path);
	return (mr);
}

static struct menu *
menu_handle_key(XEvent *e, struct menu_ctx *mc, struct menu_q *menuq,
    struct menu_q *resultq)
{
	struct menu	*mi;
	enum ctltype	 ctl;
	char		 chr[32];
	size_t		 len;
	int 		 clen, i;
	wchar_t 	 wc;

	if (menu_keycode(&e->xkey, &ctl, chr) < 0)
		return (NULL);

	switch (ctl) {
	case CTL_ERASEONE:
		if ((len = strlen(mc->searchstr)) > 0) {
			clen = 1;
			while (mbtowc(&wc, &mc->searchstr[len-clen], MB_CUR_MAX) == -1)
				clen++;
			for (i = 1; i <= clen; i++)
				mc->searchstr[len - i] = '\0';
			mc->changed = 1;
		}
		break;
	case CTL_UP:
		mi = TAILQ_LAST(resultq, menu_q);
		if (mi == NULL)
			break;

		TAILQ_REMOVE(resultq, mi, resultentry);
		TAILQ_INSERT_HEAD(resultq, mi, resultentry);
		break;
	case CTL_DOWN:
		mi = TAILQ_FIRST(resultq);
		if (mi == NULL)
			break;

		TAILQ_REMOVE(resultq, mi, resultentry);
		TAILQ_INSERT_TAIL(resultq, mi, resultentry);
		break;
	case CTL_RETURN:
		/*
		 * Return whatever the cursor is currently on. Else
		 * even if dummy is zero, we need to return something.
		 */
		if ((mi = TAILQ_FIRST(resultq)) == NULL) {
			mi = xmalloc(sizeof *mi);
			(void)strlcpy(mi->text,
			    mc->searchstr, sizeof(mi->text));
			mi->dummy = 1;
		}
		mi->abort = 0;
		return (mi);
	case CTL_WIPE:
		mc->searchstr[0] = '\0';
		mc->changed = 1;
		break;
	case CTL_TAB:
		if ((mi = TAILQ_FIRST(resultq)) != NULL) {
			/* 
			 * - We are in exec_path menu mode
			 * - There's only one result
			 * - It is equal to the input
			 * We got a command, launch the file menu
			 */
			if ((mc->flags & CWM_MENU_FILE) &&
			    (TAILQ_NEXT(mi, resultentry) == NULL) &&
			    (strncmp(mc->searchstr, mi->text,
					strlen(mi->text))) == 0)
				return (menu_complete_path(mc));

			/* 
			 * Put common prefix of the results into searchstr
			 */
			(void)strlcpy(mc->searchstr,
					mi->text, sizeof(mc->searchstr));
			while ((mi = TAILQ_NEXT(mi, resultentry)) != NULL) {
				i = 0;
				while (tolower(mc->searchstr[i]) ==
					       tolower(mi->text[i]))
					i++;
				mc->searchstr[i] = '\0';
			}
			mc->changed = 1;
		}
		break;
	case CTL_ALL:
		mc->list = !mc->list;
		break;
	case CTL_ABORT:
		mi = xmalloc(sizeof *mi);
		mi->text[0] = '\0';
		mi->dummy = 1;
		mi->abort = 1;
		return (mi);
	default:
		break;
	}

	if (chr[0] != '\0') {
		mc->changed = 1;
		(void)strlcat(mc->searchstr, chr, sizeof(mc->searchstr));
	}

	mc->noresult = 0;
	if (mc->changed && mc->searchstr[0] != '\0') {
		(*mc->match)(menuq, resultq, mc->searchstr);
		/* If menuq is empty, never show we've failed */
		mc->noresult = TAILQ_EMPTY(resultq) && !TAILQ_EMPTY(menuq);
	} else if (mc->changed)
		TAILQ_INIT(resultq);

	if (!mc->list && mc->listing && !mc->changed) {
		TAILQ_INIT(resultq);
		mc->listing = 0;
	}

	return (NULL);
}

static void
menu_draw(struct screen_ctx *sc, struct menu_ctx *mc, struct menu_q *menuq,
    struct menu_q *resultq)
{
	struct menu		*mi;
	XineramaScreenInfo	*xine;
	int			 xmin, xmax, ymin, ymax;
	int			 n, xsave, ysave;

	if (mc->list) {
		if (TAILQ_EMPTY(resultq) && mc->list) {
			/* Copy them all over. */
			TAILQ_FOREACH(mi, menuq, entry)
				TAILQ_INSERT_TAIL(resultq, mi,
				    resultentry);

			mc->listing = 1;
		} else if (mc->changed)
			mc->listing = 0;
	}

	mc->num = 0;
	mc->width = 0;
	mc->height = 0;
	if (mc->hasprompt) {
		(void)snprintf(mc->dispstr, sizeof(mc->dispstr), "%s%s%s",
		    mc->promptstr, mc->searchstr, PROMPT_ECHAR);
		mc->width = font_width(sc, mc->dispstr, strlen(mc->dispstr));
		mc->height = font_height(sc);
		mc->num = 1;
	}

	TAILQ_FOREACH(mi, resultq, resultentry) {
		char *text;

		if (mc->print != NULL) {
			(*mc->print)(mi, mc->listing);
			text = mi->print;
		} else {
			mi->print[0] = '\0';
			text = mi->text;
		}

		mc->width = MAX(mc->width, font_width(sc, text,
		    MIN(strlen(text), MENU_MAXENTRY)));
		mc->height += font_height(sc);
		mc->num++;
	}

	xine = screen_find_xinerama(sc, mc->x, mc->y);
	if (xine) {
		xmin = xine->x_org;
		xmax = xine->x_org + xine->width;
		ymin = xine->y_org;
		ymax = xine->y_org + xine->height;
	} else {
		xmin = ymin = 0;
		xmax = sc->view.w;
		ymax = sc->view.h;
	}

	xsave = mc->x;
	ysave = mc->y;

	/* Never hide the top, or left side, of the menu. */
	if (mc->x + mc->width >= xmax)
		mc->x = xmax - mc->width;
	if (mc->x < xmin) {
		mc->x = xmin;
		mc->width = xmax - xmin;
	}
	if (mc->y + mc->height >= ymax)
		mc->y = ymax - mc->height;
	if (mc->y < ymin) {
		mc->y = ymin;
		mc->height = ymax - ymin;
	}

	if (mc->x != xsave || mc->y != ysave)
		xu_ptr_setpos(sc->rootwin, mc->x, mc->y);

	XClearWindow(X_Dpy, sc->menuwin);
	XMoveResizeWindow(X_Dpy, sc->menuwin, mc->x, mc->y,
	    mc->width, mc->height);

	if (mc->hasprompt) {
		font_draw(sc, mc->dispstr, strlen(mc->dispstr), sc->menuwin,
		    0, font_ascent(sc) + 1);
		n = 1;
	} else
		n = 0;

	TAILQ_FOREACH(mi, resultq, resultentry) {
		char *text = mi->print[0] != '\0' ?
		    mi->print : mi->text;
		int y = n * font_height(sc) + font_ascent(sc) + 1;

		/* Stop drawing when menu doesn't fit inside the screen. */
		if (mc->y + y > ymax)
			break;

		font_draw(sc, text, MIN(strlen(text), MENU_MAXENTRY),
		    sc->menuwin, 0, y);
		n++;
	}

	if (mc->hasprompt && n > 1 && (mc->searchstr[0] != '\0'))
		XFillRectangle(X_Dpy, sc->menuwin, sc->gc,
		    0, font_height(sc), mc->width, font_height(sc));

	if (mc->noresult)
		XFillRectangle(X_Dpy, sc->menuwin, sc->gc,
		    0, 0, mc->width, font_height(sc));
}

static void
menu_handle_move(XEvent *e, struct menu_ctx *mc, struct screen_ctx *sc)
{
	mc->prev = mc->entry;
	mc->entry = menu_calc_entry(sc, mc, e->xbutton.x, e->xbutton.y);

	if (mc->prev != -1)
		XFillRectangle(X_Dpy, sc->menuwin, sc->gc, 0,
		    font_height(sc) * mc->prev, mc->width, font_height(sc));
	if (mc->entry != -1) {
		(void)xu_ptr_regrab(MENUGRABMASK, Cursor_normal);
		XFillRectangle(X_Dpy, sc->menuwin, sc->gc, 0,
		    font_height(sc) * mc->entry, mc->width, font_height(sc));
	} else
		(void)xu_ptr_regrab(MENUGRABMASK, Cursor_default);
}

static struct menu *
menu_handle_release(XEvent *e, struct menu_ctx *mc, struct screen_ctx *sc,
    struct menu_q *resultq)
{
	struct menu	*mi;
	int		 entry, i = 0;

	entry = menu_calc_entry(sc, mc, e->xbutton.x, e->xbutton.y);

	if (mc->hasprompt)
		i = 1;

	TAILQ_FOREACH(mi, resultq, resultentry)
		if (entry == i++)
			break;
	if (mi == NULL) {
		mi = xmalloc(sizeof(*mi));
		mi->text[0] = '\0';
		mi->dummy = 1;
	}
	return (mi);
}

static int
menu_calc_entry(struct screen_ctx *sc, struct menu_ctx *mc, int x, int y)
{
	int	 entry;

	entry = y / font_height(sc);

	/* in bounds? */
	if (x <= 0 || x > mc->width || y <= 0 ||
	    y > font_height(sc) * mc->num || entry < 0 || entry >= mc->num)
		entry = -1;

	if (mc->hasprompt && entry == 0)
		entry = -1;

	return (entry);
}

static int
menu_keycode(XKeyEvent *ev, enum ctltype *ctl, char *chr)
{
	KeySym	 ks;
	u_int 	 state = ev->state;

	*ctl = CTL_NONE;
	chr[0] = '\0';

	ks = XkbKeycodeToKeysym(X_Dpy, ev->keycode, 0,
	     (state & ShiftMask) ? 1 : 0);

	/* Look for control characters. */
	switch (ks) {
	case XK_BackSpace:
		*ctl = CTL_ERASEONE;
		break;
	case XK_Return:
		*ctl = CTL_RETURN;
		break;
	case XK_Tab:
		*ctl = CTL_TAB;
		break;
	case XK_Up:
		*ctl = CTL_UP;
		break;
	case XK_Down:
		*ctl = CTL_DOWN;
		break;
	case XK_Escape:
		*ctl = CTL_ABORT;
		break;
	}

	if (*ctl == CTL_NONE && (state & ControlMask)) {
		switch (ks) {
		case XK_s:
		case XK_S:
			/* Emacs "next" */
			*ctl = CTL_DOWN;
			break;
		case XK_r:
		case XK_R:
			/* Emacs "previous" */
			*ctl = CTL_UP;
			break;
		case XK_u:
		case XK_U:
			*ctl = CTL_WIPE;
			break;
		case XK_h:
		case XK_H:
			*ctl = CTL_ERASEONE;
			break;
		case XK_a:
		case XK_A:
			*ctl = CTL_ALL;
			break;
		}
	}

	if (*ctl == CTL_NONE && (state & Mod1Mask)) {
		switch (ks) {
		case XK_j:
		case XK_J:
			/* Vi "down" */
			*ctl = CTL_DOWN;
			break;
		case XK_k:
		case XK_K:
			/* Vi "up" */
			*ctl = CTL_UP;
			break;
		}
	}

	if (*ctl != CTL_NONE)
		return (0);

	if (XLookupString(ev, chr, 32, &ks, NULL) < 0)
		return (-1);

	return (0);
}
