# $OpenBSD: Makefile,v 1.52 2013/08/22 18:48:45 miod Exp $
.include <bsd.own.mk>
.include <bsd.xconf.mk>

LOCALAPPD=/usr/local/lib/X11/app-defaults
LOCALAPPX=/usr/local/lib/X11
REALAPPD=/etc/X11/app-defaults
XCONFIG=${XSRCDIR}/etc/X11.${MACHINE}/xorg.conf
RM?=rm

.if ${MACHINE_ARCH} != "vax"
XSERVER= xserver
.endif

.if defined(XENOCARA_BUILD_PIXMAN)
.if ${XENOCARA_BUILD_PIXMAN:L} == "yes" && \
    ${XENOCARA_HAVE_SHARED_LIBS:L} == "yes"
XSERVER+= kdrive
.endif
.endif

SUBDIR= proto font/util data/bitmaps lib app data \
	${XSERVER} driver util doc
.ifndef NOFONTS
SUBDIR+= font
.endif
SUBDIR+= distrib/notes

NOOBJ=

build: 
	exec ${SUDO} ${MAKE} bootstrap-root
	cd util/macros && exec ${MAKE} -f Makefile.bsd-wrapper
	exec ${SUDO} ${MAKE} beforebuild
	exec ${MAKE} realbuild
	exec ${SUDO} ${MAKE} afterbuild

realbuild: _SUBDIRUSE
	# that's all folks

bootstrap:
	exec ${SUDO} ${MAKE} bootstrap-root

bootstrap-root:
	exec ${MAKE} distrib-dirs
	exec ${MAKE} install-mk

beforeinstall beforebuild:
	cd util/macros && exec ${MAKE} -f Makefile.bsd-wrapper install
	exec ${MAKE} includes

afterinstall afterbuild:
	exec ${MAKE} fix-appd
	/usr/libexec/makewhatis -v ${DESTDIR}/usr/X11R6/man

install-mk:
	cd share/mk && exec ${MAKE} X11BASE=${X11BASE} install

fix-appd:
	# Make sure /usr/local/lib/X11/app-defaults is a link
	if [ ! -L $(DESTDIR)${LOCALAPPD} ]; then \
	    if [ -d $(DESTDIR)${LOCALAPPD} ]; then \
		mv $(DESTDIR)${LOCALAPPD}/* $(DESTDIR)${REALAPPD} || true; \
		rmdir $(DESTDIR)${LOCALAPPD}; \
	    fi; \
	    mkdir -p ${DESTDIR}${LOCALAPPX}; \
	    ln -s ${REALAPPD} ${DESTDIR}${LOCALAPPD}; \
	fi

font-cache:
	cd font/alias && exec ${MAKE} -f Makefile.bsd-wrapper afterinstall

release: release-clean release-install dist
.ORDER: release-clean release-install dist

release-clean:
.if ! ( defined(DESTDIR) && defined(RELEASEDIR) )
	@echo You must set DESTDIR and RELEASEDIR for a release.; exit 255
.endif
	${RM} -rf ${DESTDIR}/usr/X11R6/* ${DESTDIR}/usr/X11R6/.[a-zA-Z0-9]*
	${RM} -rf ${DESTDIR}/var/cache/*
	${RM} -rf ${DESTDIR}/etc/X11/*
	${RM} -rf ${DESTDIR}/etc/fonts/*
	@if [ -d ${DESTDIR}/usr/X11R6 ] && [ "`cd ${DESTDIR}/usr/X11R6;ls`" ]; then \
		echo "Files found in ${DESTDIR}/usr/X11R6:"; \
		(cd ${DESTDIR}/usr/X11R6;/bin/pwd;ls -a); \
		echo "Cleanup before proceeding."; \
		exit 255; \
	fi

release-install:
	@exec ${SUDO} ${MAKE} bootstrap-root
	@exec ${MAKE} install
.if ${MACHINE} == zaurus
	@if [ -f $(DESTDIR)/etc/X11/xorg.conf ]; then \
	 echo "Not overwriting existing" $(DESTDIR)/etc/X11/xorg.conf; \
	else set -x; \
	 ${INSTALL} ${INSTALL_COPY} -o root -g wheel -m 644 \
		${XCONFIG} ${DESTDIR}/etc/X11 ; \
	fi
.endif
	touch ${DESTDIR}/var/db/sysmerge/xetcsum
	TMPSUM=`mktemp /tmp/_xetcsum.XXXXXXXXXX` || exit 1; \
	sort distrib/sets/lists/xetc/{mi,md.${MACHINE}} > $${TMPSUM}; \
	cd ${DESTDIR} && \
		xargs cksum < $${TMPSUM} > ${DESTDIR}/var/db/sysmerge/xetcsum; \
	rm -f $${TMPSUM}

dist-rel:
	${MAKE} RELEASEDIR=`pwd`/rel DESTDIR=`pwd`/dest dist 2>&1 | tee distlog

dist:
	cd distrib/sets && \
		env MACHINE=${MACHINE} ksh ./maketars ${OSrev} ${OSREV} && \
		{ env MACHINE=${MACHINE} ksh ./checkflist ${OSREV} || true ; }


distrib-dirs:
.if defined(DESTDIR) && ${DESTDIR} != ""
	# running mtree under ${DESTDIR}
	mtree -qdef /etc/mtree/BSD.x11.dist -p ${DESTDIR} -U
.else
	# running mtree
	mtree -qdef /etc/mtree/BSD.x11.dist -p / -U
.endif


.PHONY: all build beforeinstall install afterinstall release clean cleandir \
	dist distrib-dirs fix-appd beforebuild bootstrap afterbuild realbuild \
	install-mk bootstrap-root

# snap build infrastructure
.if exists(/usr/snap)
SNAPDIR?=/usr/snap
.endif
.if !defined(SNAPDIR)
snapinfo buildworld snap do_snap do_snap_rel:
	@echo "SNAPDIR must defined or create the directory /usr/snap"
	@exit 1
.else
.NOTPARALLEL:
ARCH!= uname -m
.if defined(SNAPDIRSUFFIX)
SNAPROOTDIR=${SNAPDIR}/${ARCH}/xroot.${SNAPDIRSUFFIX}
SNAPRELDIR=${SNAPDIR}/${ARCH}/${SNAPDIRSUFFIX}
SNAPLOGFILE= echo ${SNAPDIR}/${ARCH}/xbuildlog.${SNAPDIRSUFFIX}
.else
SNAPROOTDIR=${SNAPDIR}/${ARCH}/xroot
.if defined(SNAPDATE)
SNAPRELDIR!= echo ${SNAPDIR}/${ARCH}/xrelease.$$(date "+%y%m%d%H%M")
.else
SNAPRELDIR!= echo ${SNAPDIR}/${ARCH}/xrelease
.endif
SNAPLOGFILE != echo ${SNAPDIR}/${ARCH}/xbuildlog.$$(date "+%y%m%d%H%M")
.endif
snapinfo:

	@echo rootdir = ${SNAPROOTDIR}
	@echo reldir = ${SNAPRELDIR}
	@echo logfile = ${SNAPLOGFILE}

snap:
	${MAKE} do_snap 2>&1 | tee ${SNAPLOGFILE}

buildworld:
	${SUDO} mkdir -p /usr/xobj
	${SUDO} rm -rf /usr/xobj/*
	${MAKE} bootstrap
	${MAKE} obj >/dev/null
	${MAKE} build

do_snap: buildworld do_snap_rel

do_snap_rel:
	date
	rm -rf ${SNAPROOTDIR}
	mkdir -p ${SNAPROOTDIR}
	mkdir -p ${SNAPRELDIR}
	DESTDIR=${SNAPROOTDIR} RELEASEDIR=${SNAPRELDIR} ${MAKE} release
.endif

.include <bsd.subdir.mk>
.include <bsd.xorg.mk>
