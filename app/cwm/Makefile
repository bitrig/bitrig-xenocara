# $OpenBSD: Makefile,v 1.16 2010/11/27 14:25:47 matthieu Exp $

.include <bsd.xconf.mk>

PROG=		cwm

SRCS=		calmwm.c screen.c xmalloc.c client.c menu.c \
		search.c util.c xutil.c conf.c input.c xevents.c group.c \
		kbfunc.c mousefunc.c font.c parse.y

CPPFLAGS+=	-I${X11BASE}/include -I${X11BASE}/include/freetype2 -I${.CURDIR}

CFLAGS+=	-Wall
.if ${XENOCARA_BUILD_XCB:L} != "no"
LIBXCB=		-lxcb
.endif

LDADD+=		-L${X11BASE}/lib -lXft -lXrender -lX11 ${LIBXCB} -lXau -lXdmcp \
		-lfontconfig -lexpat -lfreetype -lz -lXinerama -lXrandr -lXext

MANDIR=		${X11BASE}/man/cat
MAN=		cwm.1 cwmrc.5

CLEANFILES=	cwm.cat1 cwmrc.cat5

obj: _xenocara_obj

.include <bsd.prog.mk>
.include <bsd.xorg.mk>
