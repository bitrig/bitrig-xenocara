/*
 * calmwm - the calm window manager
 *
 * Copyright (c) 2005 Marius Eriksen <marius@monkey.org>
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
 * $OpenBSD: font.c,v 1.27 2013/05/19 23:09:59 okan Exp $
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

void
font_init(struct screen_ctx *sc, const char *name)
{
	sc->xftdraw = XftDrawCreate(X_Dpy, sc->rootwin,
	    sc->visual, sc->colormap);
	if (sc->xftdraw == NULL)
		errx(1, "XftDrawCreate");

	sc->xftfont = XftFontOpenName(X_Dpy, sc->which, name);
	if (sc->xftfont == NULL)
		errx(1, "XftFontOpenName");
}

int
font_width(XftFont *xftfont, const char *text, int len)
{
	XGlyphInfo	 extents;

	XftTextExtentsUtf8(X_Dpy, xftfont, (const FcChar8*)text,
	    len, &extents);

	return (extents.xOff);
}

void
font_draw(struct screen_ctx *sc, const char *text,
    Drawable d, int color, int x, int y)
{
	XftDrawChange(sc->xftdraw, d);
	XftDrawStringUtf8(sc->xftdraw, &sc->xftcolor[color], sc->xftfont, x, y,
	    (const FcChar8*)text, strlen(text));
}
