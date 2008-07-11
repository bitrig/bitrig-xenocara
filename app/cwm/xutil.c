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
 * $Id: xutil.c,v 1.8 2008/07/11 14:21:28 okan Exp $
 */

#include "headers.h"
#include "calmwm.h"

unsigned int ign_mods[] = { 0, LockMask, Mod2Mask, Mod2Mask | LockMask };

int
xu_ptr_grab(Window win, int mask, Cursor curs)
{
	return (XGrabPointer(X_Dpy, win, False, mask,
	    GrabModeAsync, GrabModeAsync,
	    None, curs, CurrentTime) == GrabSuccess ? 0 : -1);
}

int
xu_ptr_regrab(int mask, Cursor curs)
{
	return (XChangeActivePointerGrab(X_Dpy, mask,
	    curs, CurrentTime) == GrabSuccess ? 0 : -1);
}

void
xu_ptr_ungrab(void)
{
	XUngrabPointer(X_Dpy, CurrentTime);
}

void
xu_btn_grab(Window win, int mask, u_int btn)
{
	int	i;
	for (i = 0; i < sizeof(ign_mods)/sizeof(*ign_mods); i++)
		XGrabButton(X_Dpy, btn, (mask | ign_mods[i]), win,
		    False, ButtonMask, GrabModeAsync,
		    GrabModeSync, None, None);
}

void
xu_btn_ungrab(Window win, int mask, u_int btn)
{
	int	i;
	for (i = 0; i < sizeof(ign_mods)/sizeof(*ign_mods); i++)
		XUngrabButton(X_Dpy, btn, (mask | ign_mods[i]), win);
}

void
xu_ptr_getpos(Window rootwin, int *x, int *y)
{
	Window	 w0, w1;
	int	 tmp0, tmp1;
	u_int	 tmp2;

	XQueryPointer(X_Dpy, rootwin, &w0, &w1, &tmp0, &tmp1, x, y, &tmp2);
}

void
xu_ptr_setpos(Window win, int x, int y)
{
	XWarpPointer(X_Dpy, None, win, 0, 0, 0, 0, x, y);
}

void
xu_key_grab(Window win, int mask, int keysym)
{
	KeyCode	 code;
	int	 i;

	code = XKeysymToKeycode(X_Dpy, keysym);
	if ((XKeycodeToKeysym(X_Dpy, code, 0) != keysym) &&
	    (XKeycodeToKeysym(X_Dpy, code, 1) == keysym))
		mask |= ShiftMask;

	for (i = 0; i < sizeof(ign_mods)/sizeof(*ign_mods); i++)
		XGrabKey(X_Dpy, XKeysymToKeycode(X_Dpy, keysym),
		    (mask | ign_mods[i]), win, True, GrabModeAsync,
		    GrabModeAsync);
}

void
xu_sendmsg(struct client_ctx *cc, Atom atm, long val)
{
	XEvent	 e;

	memset(&e, 0, sizeof(e));
	e.xclient.type = ClientMessage;
	e.xclient.window = cc->win;
	e.xclient.message_type = atm;
	e.xclient.format = 32;
	e.xclient.data.l[0] = val;
	e.xclient.data.l[1] = CurrentTime;

	XSendEvent(X_Dpy, cc->win, False, 0, &e);
}

int
xu_getprop(struct client_ctx *cc, Atom atm, Atom type, long len, u_char **p)
{
	Atom	 realtype;
	u_long	 n, extra;
	int	 format;

	if (XGetWindowProperty(X_Dpy, cc->win, atm, 0L, len, False, type,
	    &realtype, &format, &n, &extra, p) != Success || *p == NULL)
		return (-1);

	if (n == 0)
		XFree(*p);

	return (n);
}

int
xu_getstate(struct client_ctx *cc, int *state)
{
	Atom	 wm_state;
	long	*p = NULL;

	wm_state = XInternAtom(X_Dpy, "WM_STATE", False);

	if (xu_getprop(cc, wm_state, wm_state, 2L, (u_char **)&p) <= 0)
		return (-1);

	*state = (int)*p;
	XFree((char *)p);

	return (0);
}

void
xu_setstate(struct client_ctx *cc, int state)
{
	Atom	 wm_state;
	long	 dat[2];

	/* XXX cache */
	wm_state = XInternAtom(X_Dpy, "WM_STATE", False);

	dat[0] = (long)state;
	dat[1] = (long)None;

	cc->state = state;
	XChangeProperty(X_Dpy, cc->win, wm_state, wm_state, 32,
	    PropModeReplace, (unsigned char *)dat, 2);
}
