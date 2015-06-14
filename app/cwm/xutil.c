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
 * $OpenBSD: xutil.c,v 1.99 2015/03/28 23:12:47 okan Exp $
 */

#include <sys/types.h>
#include <sys/queue.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "calmwm.h"

static unsigned int ign_mods[] = { 0, LockMask, Mod2Mask, Mod2Mask | LockMask };

void
xu_btn_grab(Window win, int mask, unsigned int btn)
{
	unsigned int	i;

	for (i = 0; i < nitems(ign_mods); i++)
		XGrabButton(X_Dpy, btn, (mask | ign_mods[i]), win,
		    False, BUTTONMASK, GrabModeAsync,
		    GrabModeSync, None, None);
}

void
xu_btn_ungrab(Window win)
{
	XUngrabButton(X_Dpy, AnyButton, AnyModifier, win);
}

void
xu_key_grab(Window win, unsigned int mask, KeySym keysym)
{
	KeyCode		 code;
	unsigned int	 i;

	code = XKeysymToKeycode(X_Dpy, keysym);
	if ((XkbKeycodeToKeysym(X_Dpy, code, 0, 0) != keysym) &&
	    (XkbKeycodeToKeysym(X_Dpy, code, 0, 1) == keysym))
		mask |= ShiftMask;

	for (i = 0; i < nitems(ign_mods); i++)
		XGrabKey(X_Dpy, code, (mask | ign_mods[i]), win,
		    True, GrabModeAsync, GrabModeAsync);
}

void
xu_key_ungrab(Window win)
{
	XUngrabKey(X_Dpy, AnyKey, AnyModifier, win);
}

int
xu_ptr_grab(Window win, unsigned int mask, Cursor curs)
{
	return(XGrabPointer(X_Dpy, win, False, mask,
	    GrabModeAsync, GrabModeAsync,
	    None, curs, CurrentTime) == GrabSuccess ? 0 : -1);
}

int
xu_ptr_regrab(unsigned int mask, Cursor curs)
{
	return(XChangeActivePointerGrab(X_Dpy, mask,
	    curs, CurrentTime) == GrabSuccess ? 0 : -1);
}

void
xu_ptr_ungrab(void)
{
	XUngrabPointer(X_Dpy, CurrentTime);
}

void
xu_ptr_getpos(Window win, int *x, int *y)
{
	Window		 w0, w1;
	int		 tmp0, tmp1;
	unsigned int	 tmp2;

	XQueryPointer(X_Dpy, win, &w0, &w1, &tmp0, &tmp1, x, y, &tmp2);
}

void
xu_ptr_setpos(Window win, int x, int y)
{
	XWarpPointer(X_Dpy, None, win, 0, 0, 0, 0, x, y);
}

int
xu_getprop(Window win, Atom atm, Atom type, long len, unsigned char **p)
{
	Atom		 realtype;
	unsigned long	 n, extra;
	int		 format;

	if (XGetWindowProperty(X_Dpy, win, atm, 0L, len, False, type,
	    &realtype, &format, &n, &extra, p) != Success || *p == NULL)
		return(-1);

	if (n == 0)
		XFree(*p);

	return(n);
}

int
xu_getstrprop(Window win, Atom atm, char **text) {
	XTextProperty	 prop;
	char		**list;
	int		 nitems = 0;

	*text = NULL;

	XGetTextProperty(X_Dpy, win, &prop, atm);
	if (!prop.nitems)
		return(0);

	if (Xutf8TextPropertyToTextList(X_Dpy, &prop, &list,
	    &nitems) == Success && nitems > 0 && *list) {
		if (nitems > 1) {
			XTextProperty    prop2;
			if (Xutf8TextListToTextProperty(X_Dpy, list, nitems,
			    XUTF8StringStyle, &prop2) == Success) {
				*text = xstrdup((const char *)prop2.value);
				XFree(prop2.value);
			}
		} else {
			*text = xstrdup(*list);
		}
		XFreeStringList(list);
	}

	XFree(prop.value);

	return(nitems);
}

/* Root Window Properties */
void
xu_ewmh_net_supported(struct screen_ctx *sc)
{
	XChangeProperty(X_Dpy, sc->rootwin, ewmh[_NET_SUPPORTED],
	    XA_ATOM, 32, PropModeReplace, (unsigned char *)ewmh, EWMH_NITEMS);
}

void
xu_ewmh_net_supported_wm_check(struct screen_ctx *sc)
{
	Window	 w;

	w = XCreateSimpleWindow(X_Dpy, sc->rootwin, -1, -1, 1, 1, 0, 0, 0);
	XChangeProperty(X_Dpy, sc->rootwin, ewmh[_NET_SUPPORTING_WM_CHECK],
	    XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
	XChangeProperty(X_Dpy, w, ewmh[_NET_SUPPORTING_WM_CHECK],
	    XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
	XChangeProperty(X_Dpy, w, ewmh[_NET_WM_NAME],
	    cwmh[UTF8_STRING], 8, PropModeReplace, (unsigned char *)WMNAME,
	    strlen(WMNAME));
}

void
xu_ewmh_net_desktop_geometry(struct screen_ctx *sc)
{
	long	 geom[2] = { sc->view.w, sc->view.h };

	XChangeProperty(X_Dpy, sc->rootwin, ewmh[_NET_DESKTOP_GEOMETRY],
	    XA_CARDINAL, 32, PropModeReplace, (unsigned char *)geom , 2);
}

void
xu_ewmh_net_workarea(struct screen_ctx *sc)
{
	long	 workareas[CALMWM_NGROUPS][4];
	int	 i;

	for (i = 0; i < CALMWM_NGROUPS; i++) {
		workareas[i][0] = sc->work.x;
		workareas[i][1] = sc->work.y;
		workareas[i][2] = sc->work.w;
		workareas[i][3] = sc->work.h;
	}

	XChangeProperty(X_Dpy, sc->rootwin, ewmh[_NET_WORKAREA],
	    XA_CARDINAL, 32, PropModeReplace, (unsigned char *)workareas,
	    CALMWM_NGROUPS * 4);
}

void
xu_ewmh_net_client_list(struct screen_ctx *sc)
{
	struct client_ctx	*cc;
	Window			*winlist;
	int			 i = 0, j = 0;

	TAILQ_FOREACH(cc, &sc->clientq, entry)
		i++;
	if (i == 0)
		return;

	winlist = xreallocarray(NULL, i, sizeof(*winlist));
	TAILQ_FOREACH(cc, &sc->clientq, entry)
		winlist[j++] = cc->win;
	XChangeProperty(X_Dpy, sc->rootwin, ewmh[_NET_CLIENT_LIST],
	    XA_WINDOW, 32, PropModeReplace, (unsigned char *)winlist, i);
	free(winlist);
}

void
xu_ewmh_net_active_window(struct screen_ctx *sc, Window w)
{
	XChangeProperty(X_Dpy, sc->rootwin, ewmh[_NET_ACTIVE_WINDOW],
	    XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
}

void
xu_ewmh_net_wm_desktop_viewport(struct screen_ctx *sc)
{
	long	 viewports[2] = {0, 0};

	/* We don't support large desktops, so this is (0, 0). */
	XChangeProperty(X_Dpy, sc->rootwin, ewmh[_NET_DESKTOP_VIEWPORT],
	    XA_CARDINAL, 32, PropModeReplace, (unsigned char *)viewports, 2);
}

void
xu_ewmh_net_wm_number_of_desktops(struct screen_ctx *sc)
{
	long	 ndesks = CALMWM_NGROUPS;

	XChangeProperty(X_Dpy, sc->rootwin, ewmh[_NET_NUMBER_OF_DESKTOPS],
	    XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&ndesks, 1);
}

void
xu_ewmh_net_showing_desktop(struct screen_ctx *sc)
{
	long	 zero = 0;

	/* We don't support `showing desktop' mode, so this is zero.
	 * Note that when we hide all groups, or when all groups are
	 * hidden we could technically set this later on.
	 */
	XChangeProperty(X_Dpy, sc->rootwin, ewmh[_NET_SHOWING_DESKTOP],
	    XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&zero, 1);
}

void
xu_ewmh_net_virtual_roots(struct screen_ctx *sc)
{
	/* We don't support virtual roots, so delete if set by previous wm. */
	XDeleteProperty(X_Dpy, sc->rootwin, ewmh[_NET_VIRTUAL_ROOTS]);
}

void
xu_ewmh_net_current_desktop(struct screen_ctx *sc)
{
	long	 num = sc->group_active->num;

	XChangeProperty(X_Dpy, sc->rootwin, ewmh[_NET_CURRENT_DESKTOP],
	    XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&num, 1);
}

void
xu_ewmh_net_desktop_names(struct screen_ctx *sc)
{
	struct group_ctx	*gc;
	char			*p, *q;
	unsigned char		*prop_ret;
	int			 i = 0, j = 0, nstrings = 0, n = 0;
	size_t			 len = 0, tlen, slen;

	/* Let group names be overwritten if _NET_DESKTOP_NAMES is set. */

	if ((j = xu_getprop(sc->rootwin, ewmh[_NET_DESKTOP_NAMES],
	    cwmh[UTF8_STRING], 0xffffff, (unsigned char **)&prop_ret)) > 0) {
		prop_ret[j - 1] = '\0'; /* paranoia */
		while (i < j) {
			if (prop_ret[i++] == '\0')
				nstrings++;
		}
	}

	p = (char *)prop_ret;
	while (n < nstrings) {
		TAILQ_FOREACH(gc, &sc->groupq, entry) {
			if (gc->num == n) {
				free(gc->name);
				gc->name = xstrdup(p);
				p += strlen(p) + 1;
				break;
			}
		}
		n++;
	}
	if (prop_ret != NULL)
		XFree(prop_ret);

	TAILQ_FOREACH(gc, &sc->groupq, entry)
		len += strlen(gc->name) + 1;
	q = p = xreallocarray(NULL, len, sizeof(*p));

	tlen = len;
	TAILQ_FOREACH(gc, &sc->groupq, entry) {
		slen = strlen(gc->name) + 1;
		(void)strlcpy(q, gc->name, tlen);
		tlen -= slen;
		q += slen;
	}

	XChangeProperty(X_Dpy, sc->rootwin, ewmh[_NET_DESKTOP_NAMES],
	    cwmh[UTF8_STRING], 8, PropModeReplace, (unsigned char *)p, len);
	free(p);
}

/* Application Window Properties */
void
xu_ewmh_net_wm_desktop(struct client_ctx *cc)
{
	long	 num = 0xffffffff;

	if (cc->group)
		num = cc->group->num;

	XChangeProperty(X_Dpy, cc->win, ewmh[_NET_WM_DESKTOP],
	    XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&num, 1);
}

Atom *
xu_ewmh_get_net_wm_state(struct client_ctx *cc, int *n)
{
	Atom	*state, *p = NULL;

	if ((*n = xu_getprop(cc->win, ewmh[_NET_WM_STATE], XA_ATOM, 64L,
	    (unsigned char **)&p)) <= 0)
		return(NULL);

	state = xreallocarray(NULL, *n, sizeof(Atom));
	(void)memcpy(state, p, *n * sizeof(Atom));
	XFree((char *)p);

	return(state);
}

void
xu_ewmh_handle_net_wm_state_msg(struct client_ctx *cc, int action,
    Atom first, Atom second)
{
	unsigned int i;
	static struct handlers {
		int atom;
		int property;
		void (*toggle)(struct client_ctx *);
	} handlers[] = {
		{ _NET_WM_STATE_STICKY,
			CLIENT_STICKY,
			client_toggle_sticky },
		{ _NET_WM_STATE_MAXIMIZED_VERT,
			CLIENT_VMAXIMIZED,
			client_toggle_vmaximize },
		{ _NET_WM_STATE_MAXIMIZED_HORZ,
			CLIENT_HMAXIMIZED,
			client_toggle_hmaximize },
		{ _NET_WM_STATE_HIDDEN,
			CLIENT_HIDDEN,
			client_toggle_hidden },
		{ _NET_WM_STATE_FULLSCREEN,
			CLIENT_FULLSCREEN,
			client_toggle_fullscreen },
		{ _NET_WM_STATE_DEMANDS_ATTENTION,
			CLIENT_URGENCY,
			client_urgency },
	};

	for (i = 0; i < nitems(handlers); i++) {
		if (first != ewmh[handlers[i].atom] &&
		    second != ewmh[handlers[i].atom])
			continue;
		switch (action) {
		case _NET_WM_STATE_ADD:
			if (!(cc->flags & handlers[i].property))
				handlers[i].toggle(cc);
			break;
		case _NET_WM_STATE_REMOVE:
			if (cc->flags & handlers[i].property)
				handlers[i].toggle(cc);
			break;
		case _NET_WM_STATE_TOGGLE:
			handlers[i].toggle(cc);
		}
	}
}

void
xu_ewmh_restore_net_wm_state(struct client_ctx *cc)
{
	Atom	*atoms;
	int	 i, n;

	atoms = xu_ewmh_get_net_wm_state(cc, &n);
	for (i = 0; i < n; i++) {
		if (atoms[i] == ewmh[_NET_WM_STATE_STICKY])
			client_toggle_sticky(cc);
		if (atoms[i] == ewmh[_NET_WM_STATE_MAXIMIZED_HORZ])
			client_toggle_hmaximize(cc);
		if (atoms[i] == ewmh[_NET_WM_STATE_MAXIMIZED_VERT])
			client_toggle_vmaximize(cc);
		if (atoms[i] == ewmh[_NET_WM_STATE_HIDDEN])
			client_toggle_hidden(cc);
		if (atoms[i] == ewmh[_NET_WM_STATE_FULLSCREEN])
			client_toggle_fullscreen(cc);
		if (atoms[i] == ewmh[_NET_WM_STATE_DEMANDS_ATTENTION])
			client_urgency(cc);
	}
	free(atoms);
}

void
xu_ewmh_set_net_wm_state(struct client_ctx *cc)
{
	Atom	*atoms, *oatoms;
	int	 n, i, j;

	oatoms = xu_ewmh_get_net_wm_state(cc, &n);
	atoms = xreallocarray(NULL, (n + _NET_WM_STATES_NITEMS), sizeof(Atom));
	for (i = j = 0; i < n; i++) {
		if (oatoms[i] != ewmh[_NET_WM_STATE_STICKY] &&
		    oatoms[i] != ewmh[_NET_WM_STATE_MAXIMIZED_HORZ] &&
		    oatoms[i] != ewmh[_NET_WM_STATE_MAXIMIZED_VERT] &&
		    oatoms[i] != ewmh[_NET_WM_STATE_HIDDEN] &&
		    oatoms[i] != ewmh[_NET_WM_STATE_FULLSCREEN] &&
		    oatoms[i] != ewmh[_NET_WM_STATE_DEMANDS_ATTENTION])
			atoms[j++] = oatoms[i];
	}
	free(oatoms);
	if (cc->flags & CLIENT_STICKY)
		atoms[j++] = ewmh[_NET_WM_STATE_STICKY];
	if (cc->flags & CLIENT_HIDDEN)
		atoms[j++] = ewmh[_NET_WM_STATE_HIDDEN];
	if (cc->flags & CLIENT_FULLSCREEN)
		atoms[j++] = ewmh[_NET_WM_STATE_FULLSCREEN];
	else {
		if (cc->flags & CLIENT_HMAXIMIZED)
			atoms[j++] = ewmh[_NET_WM_STATE_MAXIMIZED_HORZ];
		if (cc->flags & CLIENT_VMAXIMIZED)
			atoms[j++] = ewmh[_NET_WM_STATE_MAXIMIZED_VERT];
	}
	if (cc->flags & CLIENT_URGENCY)
		atoms[j++] = ewmh[_NET_WM_STATE_DEMANDS_ATTENTION];
	if (j > 0)
		XChangeProperty(X_Dpy, cc->win, ewmh[_NET_WM_STATE],
		    XA_ATOM, 32, PropModeReplace, (unsigned char *)atoms, j);
	else
		XDeleteProperty(X_Dpy, cc->win, ewmh[_NET_WM_STATE]);
	free(atoms);
}

void
xu_xorcolor(XftColor a, XftColor b, XftColor *r)
{
	r->pixel = a.pixel ^ b.pixel;
	r->color.red = a.color.red ^ b.color.red;
	r->color.green = a.color.green ^ b.color.green;
	r->color.blue = a.color.blue ^ b.color.blue;
	r->color.alpha = 0xffff;
}

int
xu_xft_width(XftFont *xftfont, const char *text, int len)
{
	XGlyphInfo	 extents;

	XftTextExtentsUtf8(X_Dpy, xftfont, (const FcChar8*)text,
	    len, &extents);

	return(extents.xOff);
}

void
xu_xft_draw(struct screen_ctx *sc, const char *text, int color, int x, int y)
{
	XftDrawStringUtf8(sc->xftdraw, &sc->xftcolor[color], sc->xftfont,
	    x, y, (const FcChar8*)text, strlen(text));
}
