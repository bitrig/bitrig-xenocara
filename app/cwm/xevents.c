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
 * $Id: xevents.c,v 1.39 2009/03/28 16:38:54 martynas Exp $
 */

/*
 * NOTE:
 *   It is the responsibility of the caller to deal with memory
 *   management of the xevent's.
 */

#include "headers.h"
#include "calmwm.h"

/*
 * NOTE: in reality, many of these should move to client.c now that
 * we've got this nice event layer.
 */

void
xev_handle_maprequest(struct xevent *xev, XEvent *ee)
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
	xev_register(xev);
}

void
xev_handle_unmapnotify(struct xevent *xev, XEvent *ee)
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

	xev_register(xev);
}

void
xev_handle_destroynotify(struct xevent *xev, XEvent *ee)
{
	XDestroyWindowEvent	*e = &ee->xdestroywindow;
	struct client_ctx	*cc;

	if ((cc = client_find(e->window)) != NULL)
		client_delete(cc);

	xev_register(xev);
}

void
xev_handle_configurerequest(struct xevent *xev, XEvent *ee)
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

	xev_register(xev);
}

void
xev_handle_propertynotify(struct xevent *xev, XEvent *ee)
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

	xev_register(xev);
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

void
xev_handle_enternotify(struct xevent *xev, XEvent *ee)
{
	XCrossingEvent		*e = &ee->xcrossing;
	struct client_ctx	*cc;

	if ((cc = client_find(e->window)) != NULL)
		client_setactive(cc, 1);

	xev_register(xev);
}

void
xev_handle_leavenotify(struct xevent *xev, XEvent *ee)
{
	client_leave(NULL);

	xev_register(xev);
}

/* We can split this into two event handlers. */
void
xev_handle_buttonpress(struct xevent *xev, XEvent *ee)
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
		goto out;

	if (mb->context == MOUSEBIND_CTX_ROOT) {
		if (e->window != sc->rootwin)
			goto out;
	} else if (mb->context == MOUSEBIND_CTX_WIN) {
		cc = client_find(e->window);
		if (cc == NULL)
			goto out;
	}

	(*mb->callback)(cc, e);
out:
	xev_register(xev);
}

void
xev_handle_buttonrelease(struct xevent *xev, XEvent *ee)
{
	struct client_ctx *cc;

	if ((cc = client_current()) != NULL)
		group_sticky_toggle_exit(cc);

	xev_register(xev);
}

void
xev_handle_keypress(struct xevent *xev, XEvent *ee)
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
		goto out;

	if ((kb->flags & (KBFLAG_NEEDCLIENT)) &&
	    (cc = client_find(e->window)) == NULL &&
	    (cc = client_current()) == NULL)
		if (kb->flags & KBFLAG_NEEDCLIENT)
			goto out;

	(*kb->callback)(cc, &kb->argument);

out:
	xev_register(xev);
}

/*
 * This is only used for the alt suppression detection.
 */
void
xev_handle_keyrelease(struct xevent *xev, XEvent *ee)
{
	XKeyEvent		*e = &ee->xkey;
	struct screen_ctx	*sc;
	struct client_ctx	*cc;
	int			 keysym;

	sc = screen_fromroot(e->root);
	cc = client_current();

	keysym = XKeycodeToKeysym(X_Dpy, e->keycode, 0);
	if (keysym != XK_Alt_L && keysym != XK_Alt_R)
		goto out;

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

out:
	xev_register(xev);
}

void
xev_handle_clientmessage(struct xevent *xev, XEvent *ee)
{
	XClientMessageEvent	*e = &ee->xclient;
	Atom			 xa_wm_change_state;
	struct client_ctx	*cc;

	xa_wm_change_state = XInternAtom(X_Dpy, "WM_CHANGE_STATE", False);

	if ((cc = client_find(e->window)) == NULL)
		goto out;

	if (e->message_type == xa_wm_change_state && e->format == 32 &&
	    e->data.l[0] == IconicState)
		client_hide(cc);
out:
	xev_register(xev);
}

void
xev_handle_randr(struct xevent *xev, XEvent *ee)
{
	XRRScreenChangeNotifyEvent	*rev = (XRRScreenChangeNotifyEvent *)ee;
	struct client_ctx		*cc;
	struct screen_ctx		*sc;

	if ((cc = client_find(rev->window)) != NULL) {
		XRRUpdateConfiguration(ee);
		sc = CCTOSC(cc);
		sc->xmax = rev->width;
		sc->ymax = rev->height;
		screen_init_xinerama(sc);
	}
}

/* 
 * Called when the keymap has changed.
 * Ungrab all keys, reload keymap and then regrab
 */
void
xev_handle_mapping(struct xevent *xev, XEvent *ee)
{
	XMappingEvent		*e = &ee->xmapping;
	struct keybinding	*kb;

	TAILQ_FOREACH(kb, &Conf.keybindingq, entry)
		conf_ungrab(&Conf, kb);

	XRefreshKeyboardMapping(e);

	TAILQ_FOREACH(kb, &Conf.keybindingq, entry)
		conf_grab(&Conf, kb);

	xev_register(xev);
}

/*
 * X Event handling
 */

static struct xevent_q	_xevq, _xevq_putaway;
static short		_xev_q_lock = 0;
volatile sig_atomic_t	_xev_quit = 0;

void
xev_init(void)
{
	TAILQ_INIT(&_xevq);
	TAILQ_INIT(&_xevq_putaway);
}

struct xevent *
xev_new(Window *win, Window *root,
    int type, void (*cb)(struct xevent *, XEvent *), void *arg)
{
	struct xevent	*xev;

	XMALLOC(xev, struct xevent);
	xev->xev_win = win;
	xev->xev_root = root;
	xev->xev_type = type;
	xev->xev_cb = cb;
	xev->xev_arg = arg;

	return (xev);
}

void
xev_register(struct xevent *xev)
{
	struct xevent_q	*xq;

	xq = _xev_q_lock ? &_xevq_putaway : &_xevq;
	TAILQ_INSERT_TAIL(xq, xev, entry);
}

static void
_xev_reincorporate(void)
{
	struct xevent	*xev;

	while ((xev = TAILQ_FIRST(&_xevq_putaway)) != NULL) {
		TAILQ_REMOVE(&_xevq_putaway, xev, entry);
		TAILQ_INSERT_TAIL(&_xevq, xev, entry);
	}
}

void
xev_handle_expose(struct xevent *xev, XEvent *ee)
{
	XExposeEvent		*e = &ee->xexpose;
	struct client_ctx	*cc;

	if ((cc = client_find(e->window)) != NULL && e->count == 0)
		client_draw_border(cc);

	xev_register(xev);
}

#define ASSIGN(xtype) do {			\
	root = e. xtype .root;			\
	win = e. xtype .window;			\
} while (0)

#define ASSIGN1(xtype) do {			\
	win = e. xtype .window;			\
} while (0)

void
xev_loop(void)
{
	Window		 win, root;
	XEvent		 e;
	struct xevent	*xev = NULL, *nextxev;
	int type;

	while (_xev_quit == 0) {
#ifdef DIAGNOSTIC
		if (TAILQ_EMPTY(&_xevq))
			errx(1, "X event queue empty");
#endif

		XNextEvent(X_Dpy, &e);
		type = e.type;

		win = root = 0;

		switch (type) {
		case MapRequest:
			ASSIGN1(xmaprequest);
			break;
		case UnmapNotify:
			ASSIGN1(xunmap);
			break;
		case ConfigureRequest:
			ASSIGN1(xconfigurerequest);
			break;
		case PropertyNotify:
			ASSIGN1(xproperty);
			break;
		case EnterNotify:
		case LeaveNotify:
			ASSIGN(xcrossing);
			break;
		case ButtonPress:
			ASSIGN(xbutton);
			break;
		case ButtonRelease:
			ASSIGN(xbutton);
			break;
		case KeyPress:
		case KeyRelease:
			ASSIGN(xkey);
			break;
		case DestroyNotify:
			ASSIGN1(xdestroywindow);
			break;
		case ClientMessage:
			ASSIGN1(xclient);
			break;
		default:
			if (e.type == Randr_ev)
				xev_handle_randr(xev, &e);
			break;
		}

		/*
		 * Now, search for matches, and call each of them.
		 */
		_xev_q_lock = 1;
		for (xev = TAILQ_FIRST(&_xevq); xev != NULL; xev = nextxev) {
			nextxev = TAILQ_NEXT(xev, entry);

			if ((type != xev->xev_type && xev->xev_type != 0) ||
			    (xev->xev_win != NULL && win != *xev->xev_win) ||
			    (xev->xev_root != NULL && root != *xev->xev_root))
				continue;

			TAILQ_REMOVE(&_xevq, xev, entry);

			(*xev->xev_cb)(xev, &e);
		}
		_xev_q_lock = 0;
		_xev_reincorporate();
	}
}

#undef ASSIGN
#undef ASSIGN1
