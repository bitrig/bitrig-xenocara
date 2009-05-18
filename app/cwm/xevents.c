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
 * $Id: xevents.c,v 1.41 2009/05/18 00:14:19 oga Exp $
 */

/*
 * NOTE:
 *   It is the responsibility of the caller to deal with memory
 *   management of the xevent's.
 */

#include "headers.h"
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
		cc = client_new(e->window, screen_fromroot(xattr.root), 1);
	}

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
		sc = CCTOSC(cc);

		if (e->value_mask & CWWidth)
			cc->geom.width = e->width;
		if (e->value_mask & CWHeight)
			cc->geom.height = e->height;
		if (e->value_mask & CWX)
			cc->geom.x = e->x;
		if (e->value_mask & CWY)
			cc->geom.y = e->y;
		if (e->value_mask & CWBorderWidth)
			wc.border_width = e->border_width;

		if (cc->geom.x == 0 && cc->geom.width >= sc->xmax)
			cc->geom.x -= cc->bwidth;

		if (cc->geom.y == 0 && cc->geom.height >= sc->ymax)
			cc->geom.y -= cc->bwidth;

		wc.x = cc->geom.x;
		wc.y = cc->geom.y;
		wc.width = cc->geom.width;
		wc.height = cc->geom.height;
		wc.border_width = cc->bwidth;

		XConfigureWindow(X_Dpy, cc->win, e->value_mask, &wc);
		xev_reconfig(cc);
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
	struct client_ctx	*cc;
	long			 tmp;

	if ((cc = client_find(e->window)) != NULL) {
		switch (e->atom) {
		case XA_WM_NORMAL_HINTS:
			XGetWMNormalHints(X_Dpy, cc->win, cc->size, &tmp);
			break;
		case XA_WM_NAME:
			client_setname(cc);
			break;
		default:
			/* do nothing */
			break;
		}
	}
}

void
xev_reconfig(struct client_ctx *cc)
{
	XConfigureEvent	 ce;

	ce.type = ConfigureNotify;
	ce.event = cc->win;
	ce.window = cc->win;
	ce.x = cc->geom.x;
	ce.y = cc->geom.y;
	ce.width = cc->geom.width;
	ce.height = cc->geom.height;
	ce.border_width = cc->bwidth;
	ce.above = None;
	ce.override_redirect = 0;

	XSendEvent(X_Dpy, cc->win, False, StructureNotifyMask, (XEvent *)&ce);
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
	struct client_ctx	*cc;
	struct screen_ctx	*sc;
	struct mousebinding	*mb;

	sc = screen_fromroot(e->root);
	cc = client_find(e->window);

	/* Ignore caps lock and numlock */
	e->state &= ~(Mod2Mask | LockMask);

	TAILQ_FOREACH(mb, &Conf.mousebindingq, entry) {
		if (e->button == mb->button && e->state == mb->modmask)
			break;
	}

	if (mb == NULL)
		return;

	if (mb->context == MOUSEBIND_CTX_ROOT) {
		if (e->window != sc->rootwin)
			return;
	} else if (mb->context == MOUSEBIND_CTX_WIN) {
		cc = client_find(e->window);
		if (cc == NULL)
			return;
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
	struct client_ctx	*cc = NULL;
	struct keybinding	*kb;
	KeySym			 keysym, skeysym;
	int			 modshift;

	keysym = XKeycodeToKeysym(X_Dpy, e->keycode, 0);
	skeysym = XKeycodeToKeysym(X_Dpy, e->keycode, 1);

	/* we don't care about caps lock and numlock here */
	e->state &= ~(LockMask | Mod2Mask);

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

	if ((kb->flags & (KBFLAG_NEEDCLIENT)) &&
	    (cc = client_find(e->window)) == NULL &&
	    (cc = client_current()) == NULL)
		if (kb->flags & KBFLAG_NEEDCLIENT)
			return;

	(*kb->callback)(cc, &kb->argument);
}

/*
 * This is only used for the alt suppression detection.
 */
static void
xev_handle_keyrelease(XEvent *ee)
{
	XKeyEvent		*e = &ee->xkey;
	struct screen_ctx	*sc;
	struct client_ctx	*cc;
	int			 keysym;

	sc = screen_fromroot(e->root);
	cc = client_current();

	keysym = XKeycodeToKeysym(X_Dpy, e->keycode, 0);
	if (keysym != XK_Alt_L && keysym != XK_Alt_R)
		return;

	sc->altpersist = 0;

	/*
	 * XXX - better interface... xevents should not know about
	 * how/when to mtf.
	 */
	client_mtf(NULL);

	if (cc != NULL) {
		group_sticky_toggle_exit(cc);
		XUngrabKeyboard(X_Dpy, CurrentTime);
	}
}

static void
xev_handle_clientmessage(XEvent *ee)
{
	XClientMessageEvent	*e = &ee->xclient;
	Atom			 xa_wm_change_state;
	struct client_ctx	*cc;

	xa_wm_change_state = XInternAtom(X_Dpy, "WM_CHANGE_STATE", False);

	if ((cc = client_find(e->window)) == NULL)
		return;

	if (e->message_type == xa_wm_change_state && e->format == 32 &&
	    e->data.l[0] == IconicState)
		client_hide(cc);
}

static void
xev_handle_randr(XEvent *ee)
{
	XRRScreenChangeNotifyEvent	*rev = (XRRScreenChangeNotifyEvent *)ee;
	struct screen_ctx		*sc;
	int				 i;

	i = XRRRootToScreen(X_Dpy, rev->root);
	TAILQ_FOREACH(sc, &Screenq, entry) {
		if (sc->which == (u_int)i) {
			XRRUpdateConfiguration(ee);
			sc->xmax = rev->width;
			sc->ymax = rev->height;
			screen_init_xinerama(sc);
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
	struct keybinding	*kb;

	TAILQ_FOREACH(kb, &Conf.keybindingq, entry)
		conf_ungrab(&Conf, kb);

	XRefreshKeyboardMapping(e);

	TAILQ_FOREACH(kb, &Conf.keybindingq, entry)
		conf_grab(&Conf, kb);
}

static void
xev_handle_expose(XEvent *ee)
{
	XExposeEvent		*e = &ee->xexpose;
	struct client_ctx	*cc;

	if ((cc = client_find(e->window)) != NULL && e->count == 0)
		client_draw_border(cc);
}


volatile sig_atomic_t	_xev_quit = 0;

void
xev_loop(void)
{
	XEvent		 e;

	while (_xev_quit == 0) {
		XNextEvent(X_Dpy, &e);
		if (e.type - Randr_ev == RRScreenChangeNotify)
			xev_handle_randr(&e);
		else if (e.type < LASTEvent && xev_handlers[e.type] != NULL)
			(*xev_handlers[e.type])(&e);
	}
}
