# $OpenBSD: Makefile,v 1.6 2008/03/25 23:41:50 matthieu Exp $

.include <bsd.xconf.mk>

PROG=		cwm

SRCS=		calmwm.c screen.c xmalloc.c client.c grab.c search.c \
		util.c xutil.c conf.c input.c xevents.c group.c \
		kbfunc.c font.c parse.y

CPPFLAGS+=	-I${X11BASE}/include -I${X11BASE}/include/freetype2 -I${.CURDIR}

LDADD+=		-L${X11BASE}/lib -lXft -lXrender -lX11 -lXau -lXdmcp \
		-lfontconfig -lexpat -lfreetype -lz -lX11 -lXau -lXdmcp -lXext

MANDIR=		${X11BASE}/man/cat
MAN=		cwm.1 cwmrc.5

CLEANFILES=	cwm.cat1 cwmrc.cat5

obj: _xenocara_obj

.include <bsd.prog.mk>
.include <bsd.xorg.mk>
