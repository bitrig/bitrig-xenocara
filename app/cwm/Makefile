# $OpenBSD: Makefile,v 1.11 2008/09/22 14:28:04 oga Exp $

.include <bsd.xconf.mk>

PROG=		cwm

SRCS=		calmwm.c screen.c xmalloc.c client.c grab.c menu.c \
		search.c util.c xutil.c conf.c input.c xevents.c group.c \
		kbfunc.c mousefunc.c font.c parse.y

CPPFLAGS+=	-I${X11BASE}/include -I${X11BASE}/include/freetype2 -I${.CURDIR}

LDADD+=		-L${X11BASE}/lib -lXft -lXrender -lX11 -lXau -lXdmcp -lXext \
		-lfontconfig -lexpat -lfreetype -lz 

MANDIR=		${X11BASE}/man/cat
MAN=		cwm.1 cwmrc.5

CLEANFILES=	cwm.cat1 cwmrc.cat5

obj: _xenocara_obj

.include <bsd.prog.mk>
.include <bsd.xorg.mk>
