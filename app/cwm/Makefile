# $OpenBSD: Makefile,v 1.7 2008/05/20 14:50:51 oga Exp $

.include <bsd.xconf.mk>

PROG=		cwm

SRCS=		calmwm.c screen.c xmalloc.c client.c grab.c menu.c \
		search.c util.c xutil.c conf.c input.c xevents.c group.c \
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
