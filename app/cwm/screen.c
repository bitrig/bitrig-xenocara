/*
 * calmwm - the calm window manager
 *
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
 * $OpenBSD: screen.c,v 1.38 2012/11/29 03:54:46 okan Exp $
 */

#include <sys/param.h>
#include <sys/queue.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "calmwm.h"

static void	 screen_init_xinerama(struct screen_ctx *);

void
screen_init(struct screen_ctx *sc, u_int which)
{
	Window			*wins, w0, w1;
	XWindowAttributes	 winattr;
	XSetWindowAttributes	 rootattr;
	u_int			 nwins, i;

	sc->which = which;
	sc->rootwin = RootWindow(X_Dpy, sc->which);

	xu_ewmh_net_supported(sc);
	xu_ewmh_net_supported_wm_check(sc);

	conf_gap(&Conf, sc);

	screen_update_geometry(sc);

	conf_color(&Conf, sc);

	group_init(sc);
	conf_font(&Conf, sc);

	TAILQ_INIT(&sc->mruq);

	menu_init(sc);

	rootattr.cursor = Cursor_normal;
	rootattr.event_mask = CHILDMASK|PropertyChangeMask|EnterWindowMask|
	    LeaveWindowMask|ColormapChangeMask|BUTTONMASK;

	XChangeWindowAttributes(X_Dpy, sc->rootwin,
	    CWEventMask|CWCursor, &rootattr);

	/* Deal with existing clients. */
	XQueryTree(X_Dpy, sc->rootwin, &w0, &w1, &wins, &nwins);

	for (i = 0; i < nwins; i++) {
		XGetWindowAttributes(X_Dpy, wins[i], &winattr);
		if (winattr.override_redirect ||
		    winattr.map_state != IsViewable)
			continue;
		(void)client_new(wins[i], sc, winattr.map_state != IsUnmapped);
	}
	XFree(wins);

	screen_updatestackingorder(sc);

	if (HasRandr)
		XRRSelectInput(X_Dpy, sc->rootwin, RRScreenChangeNotifyMask);

	XSync(X_Dpy, False);
}

struct screen_ctx *
screen_fromroot(Window rootwin)
{
	struct screen_ctx	*sc;

	TAILQ_FOREACH(sc, &Screenq, entry)
		if (sc->rootwin == rootwin)
			return (sc);

	/* XXX FAIL HERE */
	return (TAILQ_FIRST(&Screenq));
}

void
screen_updatestackingorder(struct screen_ctx *sc)
{
	Window			*wins, w0, w1;
	struct client_ctx	*cc;
	u_int			 nwins, i, s;

	if (!XQueryTree(X_Dpy, sc->rootwin, &w0, &w1, &wins, &nwins))
		return;

	for (s = 0, i = 0; i < nwins; i++) {
		/* Skip hidden windows */
		if ((cc = client_find(wins[i])) == NULL ||
		    cc->flags & CLIENT_HIDDEN)
			continue;

		cc->stackingorder = s++;
	}

	XFree(wins);
}

/*
 * If we're using RandR then we'll redo this whenever the screen
 * changes since a CTRC may have been added or removed
 */
void
screen_init_xinerama(struct screen_ctx *sc)
{
	XineramaScreenInfo	*info = NULL;
	int			 no = 0;

	if (XineramaIsActive(X_Dpy))
		info = XineramaQueryScreens(X_Dpy, &no);

	if (sc->xinerama != NULL)
		XFree(sc->xinerama);
	sc->xinerama = info;
	sc->xinerama_no = no;
}

/*
 * Find which xinerama screen the coordinates (x,y) is on.
 */
XineramaScreenInfo *
screen_find_xinerama(struct screen_ctx *sc, int x, int y)
{
	XineramaScreenInfo	*info;
	int			 i;

	if (sc->xinerama == NULL)
		return (NULL);

	for (i = 0; i < sc->xinerama_no; i++) {
		info = &sc->xinerama[i];
		if (x >= info->x_org && x < info->x_org + info->width &&
		    y >= info->y_org && y < info->y_org + info->height)
			return (info);
	}
	return (NULL);
}

void
screen_update_geometry(struct screen_ctx *sc)
{
	sc->view.x = 0;
	sc->view.y = 0;
	sc->view.w = DisplayWidth(X_Dpy, sc->which);
	sc->view.h = DisplayHeight(X_Dpy, sc->which);

	sc->work.x = sc->view.x + sc->gap.left;
	sc->work.y = sc->view.y + sc->gap.top;
	sc->work.w = sc->view.w - (sc->gap.left + sc->gap.right);
	sc->work.h = sc->view.h - (sc->gap.top + sc->gap.bottom);

	screen_init_xinerama(sc);

	xu_ewmh_net_desktop_geometry(sc);
	xu_ewmh_net_workarea(sc);
}
