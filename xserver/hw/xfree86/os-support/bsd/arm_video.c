/* $XFree86: xc/programs/Xserver/hw/xfree86/os-support/bsd/ppc_video.c,v 1.6 2003/10/07 23:14:55 herrb Exp $ */
/* $OpenBSD: arm_video.c,v 1.14 2015/09/28 07:14:00 matthieu Exp $ */
/*
 * Copyright 1992 by Rich Murphey <Rich@Rice.edu>
 * Copyright 1993 by David Wexelblat <dwex@goblin.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of Rich Murphey and David Wexelblat
 * not be used in advertising or publicity pertaining to distribution of
 * the software without specific, written prior permission.  Rich Murphey and
 * David Wexelblat make no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * RICH MURPHEY AND DAVID WEXELBLAT DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL RICH MURPHEY OR DAVID WEXELBLAT BE LIABLE FOR
 * ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

/*
 * The ARM32 code here carries the following copyright:
 *
 * Copyright 1997
 * Digital Equipment Corporation. All rights reserved.
 * This software is furnished under license and may be used and copied only in
 * accordance with the following terms and conditions.  Subject to these
 * conditions, you may download, copy, install, use, modify and distribute
 * this software in source and/or binary form. No title or ownership is
 * transferred hereby.
 *
 * 1) Any source code used, modified or distributed must reproduce and retain
 *    this copyright notice and list of conditions as they appear in the
 *    source file.
 *
 * 2) No right is granted to use any trade name, trademark, or logo of Digital
 *    Equipment Corporation. Neither the "Digital Equipment Corporation"
 *    name nor any trademark or logo of Digital Equipment Corporation may be
 *    used to endorse or promote products derived from this software without
 *    the prior written permission of Digital Equipment Corporation.
 *
 * 3) This software is provided "AS-IS" and any express or implied warranties,
 *    including but not limited to, any implied warranties of merchantability,
 *    fitness for a particular purpose, or non-infringement are disclaimed.
 *    In no event shall DIGITAL be liable for any damages whatsoever, and in
 *    particular, DIGITAL shall not be liable for special, indirect,
 *    consequential, or incidental damages or damages for lost profits, loss
 *    of revenue or loss of use, whether such damages arise in contract,
 *    negligence, tort, under statute, in equity, at law or otherwise, even
 *    if advised of the possibility of such damage.
 *
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>
#include "xf86.h"
#include "xf86Priv.h"

#include "xf86_OSlib.h"
#include "xf86OSpriv.h"

/***************************************************************************/
/* Video Memory Mapping section                                            */
/***************************************************************************/

void
xf86OSInitVidMem(VidMemInfoPtr pVidMem)
{
    pVidMem->initialised = TRUE;
}

/*
 * Do all initialisation that need root privileges
 */
void
xf86PrivilegedInit(void)
{
    xf86OpenConsole();
}

#ifdef __VFP_FP__
/*
 * force softfloat functions into binary,
 * yes the protos/ret are all bogus.
 */
void arm_softfloat(void);

void
arm_softfloat()
{
void __adddf3();
void __addsf3();
void __eqdf2();
void __eqsf2();
void __extendsfdf2();
void __fixdfsi();
void __fixsfsi();
void __fixunsdfsi();
void __fixunssfsi();
void __floatsidf();
void __floatsisf();
void __gedf2();
void __gesf2();
void __gtdf2();
void __gtsf2();
void __ledf2();
void __lesf2();
void __ltdf2();
void __ltsf2();
void __nedf2();
void __negdf2();
void __negsf2();
void __nesf2();
void __subdf3();
void __subsf3();
void __truncdfsf2();

#if defined(__SOFTFP__)
/* softfloat only functions */
__adddf3();
__addsf3();
__extendsfdf2();
__fixdfsi();
__fixsfsi();
__floatsidf();
__floatsisf();
__subdf3();
__subsf3();
__truncdfsf2();
#endif

__eqdf2();
__eqsf2();
__fixunsdfsi();
__fixunssfsi();
__gedf2();
__gesf2();
__gtdf2();
__gtsf2();
__ledf2();
__lesf2();
__ltdf2();
__ltsf2();
__nedf2();
__negdf2();
__negsf2();
__nesf2();
}
#endif /* __VFP_FP__ */
