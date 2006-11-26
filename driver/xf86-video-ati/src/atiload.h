/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/ati/atiload.h,v 1.6 2003/07/24 22:08:28 tsi Exp $ */
/*
 * Copyright 2000 through 2004 by Marc Aurele La France (TSI @ UQV), tsi@xfree86.org
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of Marc Aurele La France not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  Marc Aurele La France makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as-is" without express or implied warranty.
 *
 * MARC AURELE LA FRANCE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.  IN NO
 * EVENT SHALL MARC AURELE LA FRANCE BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * DRI support by:
 *    Leif Delgass <ldelgass@retinalburn.net>
 */

#ifndef ___ATILOAD_H___
#define ___ATILOAD_H___ 1

#ifdef XFree86LOADER

#include "atipriv.h"

#include "xf86str.h"

extern const char *ATIint10Symbols[], *ATIddcSymbols[], *ATIvbeSymbols[],

#ifndef AVOID_CPIO

                  *ATIxf1bppSymbols[], *ATIxf4bppSymbols[],

#endif /* AVOID_CPIO */

#ifdef XF86DRI_DEVEL
 
                  *ATIdrmSymbols[], *ATIdriSymbols[],
 
#endif /* XF86DRI_DEVEL */

                  *ATIfbSymbols[], *ATIshadowfbSymbols[],

#ifdef USE_EXA
 
                  *ATIexaSymbols[],

#endif /* USE_EXA */

#ifdef USE_XAA
 
                  *ATIxaaSymbols[],

#endif /* USE_XAA */

                  *ATIramdacSymbols[], *ATIi2cSymbols[];

extern pointer ATILoadModule(ScrnInfoPtr, const char *, const char **);
extern pointer ATILoadModules(ScrnInfoPtr, ATIPtr);

#else /* XFree86LOADER */

#define ATILoadModule(pScreenInfo, Module, SymboList) ((pointer)1)
#define ATILoadModules(pScreenInfo, pATI)             ((pointer)1)

#endif /* XFree86LOADER */

#endif /* ___ATILOAD_H___ */
