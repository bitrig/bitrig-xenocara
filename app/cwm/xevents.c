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
 * $OpenBSD: xevents.c,v 1.90 2013/07/15 23:51:59 okan Exp $
 */

/*
 * NOTE:
 *   It is the responsibility of the caller to deal with memory
 *   management of the xevent's.
 */

#include <sys/param.h>
#include <sys/queue.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "calmwm.h"

static void	 xev_handle_maprequest(XEvent *);
static void	 xev_handle_unmapnotify(XEvent *);
static void	 xev_handle_destroynotify(XEvent *);
static void	 xev_handle_configurerequest(XEvent *);
static void	 xev_handle_propertynotify(XEvent *);
static void	 xev_handle_enternotify(XEvent *);
static void	 xev_handle_leavenotify(XEvent *);
static void	 xev_handle_buttonpress(XEvent *);
static void	 xev_handle_buttonrelease(XEvent *);
static void	 xev_handle_keypress(XEvent *);
static void	 xev_handle_keyrelease(XEvent *);
static void	 xev_handle_expose(XEvent *);
static void	 xev_handle_clientmessage(XEvent *);
static void	 xev_handle_randr(XEvent *);
static void	 xev_handle_mappingnotify(XEvent *);


void		(*xev_handlers[LASTEvent])(XEvent *) = {
			[MapRequest] = xev_handle_maprequest,
			[UnmapNotify] = xev_handle_unmapnotify,
			[ConfigureRequest] = xev_handle_configurerequest,
			[PropertyNotify] = xev_handle_propertynotify,
			[EnterNotify] = xev_handle_enternotify,
			[LeaveNotify] = xev_handle_leavenotify,
			[ButtonPress] = xev_handle_buttonpress,
			[ButtonRelease] = xev_handle_buttonrelease,
			[KeyPress] = xev_handle_keypress,
			[KeyRelease] = xev_handle_keyrelease,
			[Expose] = xev_handle_expose,
			[DestroyNotify] = xev_handle_destroynotify,
			[ClientMessage] = xev_handle_clientmessage,
			[MappingNotify] = xev_handle_mappingnotify,
};

static KeySym modkeys[] = { XK_Alt_L, XK_Alt_R, XK_Super_L, XK_Super_R,
			    XK_Control_L, XK_Control_R };

static void
xev_handle_maprequest(XEvent *ee)
{
	XMapRequestEvent	*e = &ee->xmaprequest;
	struct client_ctx	*cc = NULL, *old_cc;
	XWindowAttributes	 xattr;

	if ((old_cc = client_current()) != NULL)
		client_ptrsave(old_cc);

	if ((cc = client_find(e->window)) == NULL) {
		XGetWindowAttributes(X_Dpy, e->window, &xattr);
		cc = client_init(e->window, screen_fromroot(xattr.root), 1);
	}

	if ((cc->flags & CLIENT_IGNORE) == 0)
		client_ptrwarp(cc);
}

static void
xev_handle_unmapnotify(XEvent *ee)
{
	XUnmapEvent		*e = &ee->xunmap;
	XEvent			ev;
	struct client_ctx	*cc;

	/* XXX, we need a recursive locking wrapper around grab server */
	XGrabServer(X_Dpy);
	if ((cc = client_find(e->window)) != NULL) {
		/*
		 * If it's going to die anyway, nuke it.
		 *
		 * Else, if it's a synthetic event delete state, since they
		 * want it to be withdrawn. ICCM recommends you withdraw on
		 * this even if we haven't alredy been told to iconify, to
		 * deal with legacy clients.
		 */
		if (XCheckTypedWindowEvent(X_Dpy, cc->win,
		    DestroyNotify, &ev) || e->send_event != 0) {
			client_delete(cc);
		} else
			client_hide(cc);
	}
	XUngrabServer(X_Dpy);
}

static void
xev_handle_destroynotify(XEvent *ee)
{
	XDestroyWindowEvent	*e = &ee->xdestroywindow;
	struct client_ctx	*cc;

	if ((cc = client_find(e->window)) != NULL)
		client_delete(cc);
}

static void
xev_handle_configurerequest(XEvent *ee)
{
	XConfigureRequestEvent	*e = &ee->xconfigurerequest;
	struct client_ctx	*cc;
	struct screen_ctx	*sc;
	XWindowChanges		 wc;

	if ((cc = client_find(e->window)) != NULL) {
		sc = cc->sc;

		if (e->value_mask & CWWidth)
			cc->geom.w = e->width;
		if (e->value_mask & CWHeight)
			cc->geom.h = e->height;
		if (e->value_mask & CWX)
			cc->geom.x = e->x;
		if (e->value_mask & CWY)
			cc->geom.y = e->y;
		if (e->value_mask & CWBorderWidth)
			cc->bwidth = e->border_width;
		if (e->value_mask & CWSibling)
			wc.sibling = e->above;
		if (e->value_mask & CWStackMode)
			wc.stack_mode = e->detail;

		if (cc->geom.x == 0 && cc->geom.w >= sc->view.w)
			cc->geom.x -= cc->bwidth;

		if (cc->geom.y == 0 && cc->geom.h >= sc->view.h)
			cc->geom.y -= cc->bwidth;

		wc.x = cc->geom.x;
		wc.y = cc->geom.y;
		wc.width = cc->geom.w;
		wc.height = cc->geom.h;
		wc.border_width = cc->bwidth;

		XConfigureWindow(X_Dpy, cc->win, e->value_mask, &wc);
		client_config(cc);
	} else {
		/* let it do what it wants, it'll be ours when we map it. */
		wc.x = e->x;
		wc.y = e->y;
		wc.width = e->width;
		wc.height = e->height;
		wc.border_width = e->border_width;
		wc.stack_mode = Above;
		e->value_mask &= ~CWStackMode;

		XConfigureWindow(X_Dpy, e->window, e->value_mask, &wc);
	}
}

static void
xev_handle_propertynotify(XEvent *ee)
{
	XPropertyEvent		*e = &ee->xproperty;
	struct screen_ctx	*sc;
	struct client_ctx	*cc;

	if ((cc = client_find(e->window)) != NULL) {
		switch (e->atom) {
		case XA_WM_NORMAL_HINTS:
			client_getsizehints(cc);
			break;
		case XA_WM_NAME:
			client_setname(cc);
			break;
		case XA_WM_TRANSIENT_FOR:
			client_transient(cc);
			break;
		default:
			/* do nothing */
			break;
		}
	} else {
		TAILQ_FOREACH(sc, &Screenq, entry) {
			if (sc->rootwin == e->window) {
				if (e->atom == ewmh[_NET_DESKTOP_NAMES])
					group_update_names(sc);
			}
		}
	}
}

static void
xev_handle_enternotify(XEvent *ee)
{
	XCrossingEvent		*e = &ee->xcrossing;
	struct client_ctx	*cc;

	if ((cc = client_find(e->window)) != NULL)
		client_setactive(cc, 1);
}

static void
xev_handle_leavenotify(XEvent *ee)
{
	client_leave(NULL);
}

/* We can split this into two event handlers. */
static void
xev_handle_buttonpress(XEvent *ee)
{
	XButtonEvent		*e = &ee->xbutton;
	struct client_ctx	*cc, fakecc;
	struct mousebinding	*mb;

	e->state &= ~IGNOREMODMASK;

	TAILQ_FOREACH(mb, &Conf.mousebindingq, entry) {
		if (e->button == mb->button && e->state == mb->modmask)
			break;
	}

	if (mb == NULL)
		return;
	if (mb->flags == MOUSEBIND_CTX_WIN) {
		if (((cc = client_find(e->window)) == NULL) &&
		    (cc = client_current()) == NULL)
			return;
	} else { /* (mb->flags == MOUSEBIND_CTX_ROOT) */
		if (e->window != e->root)
			return;
		cc = &fakecc;
		cc->sc = screen_fromroot(e->window);
	}

	(*mb->callback)(cc, e);
}

static void
xev_handle_buttonrelease(XEvent *ee)
{
	struct client_ctx *cc;

	if ((cc = client_current()) != NULL)
		group_sticky_toggle_exit(cc);
}

static void
xev_handle_keypress(XEvent *ee)
{
	XKeyEvent		*e = &ee->xkey;
	struct client_ctx	*cc = NULL, fakecc;
	struct keybinding	*kb;
	KeySym			 keysym, skeysym;
	u_int			 modshift;

	keysym = XkbKeycodeToKeysym(X_Dpy, e->keycode, 0, 0);
	skeysym = XkbKeycodeToKeysym(X_Dpy, e->keycode, 0, 1);

	e->state &= ~IGNOREMODMASK;

	TAILQ_FOREACH(kb, &Conf.keybindingq, entry) {
		if (keysym != kb->keysym && skeysym == kb->keysym)
			modshift = ShiftMask;
		else
			modshift = 0;

		if ((kb->modmask | modshift) != e->state)
			continue;

		if ((kb->keycode != 0 && kb->keysym == NoSymbol &&
		    kb->keycode == e->keycode) || kb->keysym ==
		    (modshift == 0 ? keysym : skeysym))
			break;
	}

	if (kb == NULL)
		return;
	if (kb->flags & KBFLAG_NEEDCLIENT) {
		if (((cc = client_find(e->window)) == NULL) &&
		    (cc = client_current()) == NULL)
			return;
	} else {
		cc = &fakecc;
		cc->sc = screen_fromroot(e->window);
	}

	(*kb->callback)(cc, &kb->argument);
}

/*
 * This is only used for the modifier suppression detection.
 */
static void
xev_handle_keyrelease(XEvent *ee)
{
	XKeyEvent		*e = &ee->xkey;
	struct screen_ctx	*sc;
	struct client_ctx	*cc;
	KeySym			 keysym;
	u_int			 i;

	sc = screen_fromroot(e->root);
	cc = client_current();

	keysym = XkbKeycodeToKeysym(X_Dpy, e->keycode, 0, 0);
	for (i = 0; i < nitems(modkeys); i++) {
		if (keysym == modkeys[i]) {
			client_cycle_leave(sc, cc);
			break;
		}
	}
}

static void
xev_handle_clientmessage(XEvent *ee)
{
	XClientMessageEvent	*e = &ee->xclient;
	struct client_ctx	*cc, *old_cc;

	if ((cc = client_find(e->window)) == NULL)
		return;

	if (e->message_type == cwmh[WM_CHANGE_STATE] && e->format == 32 &&
	    e->data.l[0] == IconicState)
		client_hide(cc);

	if (e->message_type == ewmh[_NET_CLOSE_WINDOW])
		client_send_delete(cc);

	if (e->message_type == ewmh[_NET_ACTIVE_WINDOW] && e->format == 32) {
		old_cc = client_current();
		if (old_cc)
			client_ptrsave(old_cc);
		client_ptrwarp(cc);
	}
	if (e->message_type == ewmh[_NET_WM_STATE] && e->format == 32)
		xu_ewmh_handle_net_wm_state_msg(cc,
		    e->data.l[0], e->data.l[1], e->data.l[2]);
}

static void
xev_handle_randr(XEvent *ee)
{
	XRRScreenChangeNotifyEvent	*rev = (XRRScreenChangeNotifyEvent *)ee;
	struct screen_ctx		*sc;
	int				 i;

	i = XRRRootToScreen(X_Dpy, rev->root);
	TAILQ_FOREACH(sc, &Screenq, entry) {
		if (sc->which == i) {
			XRRUpdateConfiguration(ee);
			screen_update_geometry(sc);
		}
	}
}

/*
 * Called when the keymap has changed.
 * Ungrab all keys, reload keymap and then regrab
 */
static void
xev_handle_mappingnotify(XEvent *ee)
{
	XMappingEvent		*e = &ee->xmapping;
	struct screen_ctx	*sc;

	XRefreshKeyboardMapping(e);
	if (e->request == MappingKeyboard) {
		TAILQ_FOREACH(sc, &Screenq, entry)
			conf_grab_kbd(sc->rootwin);
	}
}

static void
xev_handle_expose(XEvent *ee)
{
	XExposeEvent		*e = &ee->xexpose;
	struct client_ctx	*cc;

	if ((cc = client_find(e->window)) != NULL && e->count == 0)
		client_draw_border(cc);
}

volatile sig_atomic_t	xev_quit = 0;

void
xev_loop(void)
{
	XEvent		 e;

	while (xev_quit == 0) {
		XNextEvent(X_Dpy, &e);
		if (e.type - Randr_ev == RRScreenChangeNotify)
			xev_handle_randr(&e);
		else if (e.type < LASTEvent && xev_handlers[e.type] != NULL)
			(*xev_handlers[e.type])(&e);
	}
}
