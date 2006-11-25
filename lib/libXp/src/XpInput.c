/* $Xorg: XpInput.c,v 1.4 2000/08/17 19:46:07 cpqbld Exp $ */
/******************************************************************************
 ******************************************************************************
 **
 ** (c) Copyright 1996 Hewlett-Packard Company
 ** (c) Copyright 1996 International Business Machines Corp.
 ** (c) Copyright 1996 Sun Microsystems, Inc.
 ** (c) Copyright 1996 Novell, Inc.
 ** (c) Copyright 1996 Digital Equipment Corp.
 ** (c) Copyright 1996 Fujitsu Limited
 ** (c) Copyright 1996 Hitachi, Ltd.
 ** 
 ** Permission is hereby granted, free of charge, to any person obtaining a copy
 ** of this software and associated documentation files (the "Software"), to deal
 ** in the Software without restriction, including without limitation the rights
 ** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 ** copies of the Software, and to permit persons to whom the Software is
 ** furnished to do so, subject to the following conditions:
 **
 ** The above copyright notice and this permission notice shall be included in
 ** all copies or substantial portions of the Software.
 **
 ** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 ** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 ** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 ** COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 ** IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 ** CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 **
 ** Except as contained in this notice, the names of the copyright holders shall
 ** not be used in advertising or otherwise to promote the sale, use or other
 ** dealings in this Software without prior written authorization from said
 ** copyright holders.
 **
 ******************************************************************************
 *****************************************************************************/
/* $XFree86: xc/lib/Xp/XpInput.c,v 1.4 2001/01/17 19:43:02 dawes Exp $ */

#define NEED_REPLIES

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/extensions/Printstr.h>
#include <X11/Xlibint.h>
#include "XpExtUtil.h"


void
XpSelectInput (
    Display       *dpy,
    XPContext     print_context,
    unsigned long event_mask
)
{
    xPrintSelectInputReq   *req;
    XExtDisplayInfo *info = (XExtDisplayInfo *) xp_find_display (dpy);

    if (XpCheckExtInit(dpy, XP_INITIAL_RELEASE) == -1)
	return; /* NoSuchExtension NULL */

    LockDisplay (dpy);

    GetReq(PrintSelectInput,req);
    req->reqType = info->codes->major_opcode;
    req->printReqType = X_PrintSelectInput;
    req->printContext = print_context;
    req->eventMask = event_mask;

    UnlockDisplay (dpy);
    SyncHandle ();
}

unsigned long
XpInputSelected (
    Display       *dpy,
    XPContext     print_context,
    unsigned long *all_events_mask
)
{
    xPrintInputSelectedReq     *req;
    xPrintInputSelectedReply   rep;
    XExtDisplayInfo *info = (XExtDisplayInfo *) xp_find_display (dpy);

    if (XpCheckExtInit(dpy, XP_DONT_CHECK) == -1)
        return (/* No such extension */ 0L );

    LockDisplay (dpy);

    GetReq(PrintInputSelected,req);
    req->reqType = info->codes->major_opcode;
    req->printReqType = X_PrintInputSelected;
    req->printContext = print_context;

    if (! _XReply (dpy, (xReply *) &rep, 0, xTrue)) {
        UnlockDisplay(dpy);
        SyncHandle();
        return(/* No such extension */ 0L);
    }

    *all_events_mask = rep.allEventsMask;

    UnlockDisplay(dpy);
    SyncHandle();
    return ( rep.eventMask );
}

