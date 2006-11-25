/* $Xorg: t1stub.c,v 1.3 2000/08/17 19:46:34 cpqbld Exp $ */
/* Copyright International Business Machines,Corp. 1991
 * All Rights Reserved
 *
 * License to use, copy, modify, and distribute this software
 * and its documentation for any purpose and without fee is
 * hereby granted, provided that the above copyright notice
 * appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation,
 * and that the name of IBM not be used in advertising or
 * publicity pertaining to distribution of the software without
 * specific, written prior permission.
 *
 * IBM PROVIDES THIS SOFTWARE "AS IS", WITHOUT ANY WARRANTIES
 * OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING, BUT NOT
 * LIMITED TO ANY IMPLIED WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS.  THE ENTIRE RISK AS TO THE QUALITY AND
 * PERFORMANCE OF THE SOFTWARE, INCLUDING ANY DUTY TO SUPPORT
 * OR MAINTAIN, BELONGS TO THE LICENSEE.  SHOULD ANY PORTION OF
 * THE SOFTWARE PROVE DEFECTIVE, THE LICENSEE (NOT IBM) ASSUMES
 * THE ENTIRE COST OF ALL SERVICING, REPAIR AND CORRECTION.  IN
 * NO EVENT SHALL IBM BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING
 * FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */
/* $XFree86: xc/lib/font/Type1/t1stub.c,v 1.8 2001/01/17 19:43:24 dawes Exp $ */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef FONTMODULE
#include "Xdefs.h"	/* Bool declaration */
#include "Xmd.h"	/* INT32 declaration */
#include "os.h"
#include "xf86_ansic.h"
#else 
#include <stdio.h>
#endif
#include "objects.h"	/* get #define for Abort() */

static void 
xiStub(void)
{
       printf("xiStub called\n");
       Abort("xiStub called");
}
 
void 
t1_DumpText(void)
{
       xiStub();
}
