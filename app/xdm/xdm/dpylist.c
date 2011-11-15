/*

Copyright 1988, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from The Open Group.

*/

/*
 * xdm - display manager daemon
 * Author:  Keith Packard, MIT X Consortium
 *
 * a simple linked list of known displays
 */

#include "dm.h"
#include "dm_error.h"

static struct display	*displays;

int
AnyDisplaysLeft (void)
{
	return displays != (struct display *) 0;
}

void
ForEachDisplay (void (*f)(struct display *))
{
	struct display	*d, *next;

	for (d = displays; d; d = next) {
		next = d->next;
		(*f) (d);
	}
}

struct display *
FindDisplayByName (char *name)
{
	struct display	*d;

	for (d = displays; d; d = d->next)
		if (!strcmp (name, d->name))
			return d;
	return NULL;
}

struct display *
FindDisplayByPid (pid_t pid)
{
	struct display	*d;

	for (d = displays; d; d = d->next)
		if (pid == d->pid)
			return d;
	return NULL;
}

struct display *
FindDisplayByServerPid (pid_t serverPid)
{
	struct display	*d;

	for (d = displays; d; d = d->next)
		if (serverPid == d->serverPid)
			return d;
	return NULL;
}

#ifdef XDMCP

struct display *
FindDisplayBySessionID (CARD32 sessionID)
{
    struct display	*d;

    for (d = displays; d; d = d->next)
	if (sessionID == d->sessionID)
	    return d;
    return NULL;
}

struct display *
FindDisplayByAddress (XdmcpNetaddr addr, int addrlen, CARD16 displayNumber)
{
    struct display  *d;

    for (d = displays; d; d = d->next)
	if (d->displayType.origin == FromXDMCP &&
	    d->displayNumber == displayNumber &&
	    addressEqual (d->from, d->fromlen, addr, addrlen))
	{
	    return d;
	}
    return NULL;
}

#endif /* XDMCP */

void
RemoveDisplay (struct display *old)
{
    struct display	*d, *p;
    char		**x;
    int			i;

    p = NULL;
    for (d = displays; d; d = d->next) {
	if (d == old) {
	    if (p)
		p->next = d->next;
	    else
		displays = d->next;
	    free (d->name);
	    free (d->class);
	    for (x = d->argv; x && *x; x++)
		free (*x);
	    free (d->argv);
	    free (d->resources);
	    free (d->xrdb);
	    free (d->setup);
	    free (d->startup);
	    free (d->reset);
	    free (d->session);
	    free (d->userPath);
	    free (d->systemPath);
	    free (d->systemShell);
	    free (d->failsafeClient);
	    free (d->chooser);
	    if (d->authorizations)
	    {
		for (i = 0; i < d->authNum; i++)
		    XauDisposeAuth (d->authorizations[i]);
		free (d->authorizations);
	    }
	    free (d->clientAuthFile);
	    if (d->authFile)
		(void) unlink (d->authFile);
	    free (d->authFile);
	    free (d->userAuthDir);
	    for (x = d->authNames; x && *x; x++)
		free (*x);
	    free (d->authNames);
	    free (d->authNameLens);
#ifdef XDMCP
	    free (d->peer);
	    free (d->from);
	    XdmcpDisposeARRAY8 (&d->clientAddr);
#endif
	    free (d->windowPath);
	    free (d);
	    break;
	}
	p = d;
    }
}

struct display *
NewDisplay (char *name, char *class)
{
    struct display	*d;

    d = calloc (1, sizeof (struct display));
    if (!d) {
	LogOutOfMem ("NewDisplay");
	return NULL;
    }
    d->next = displays;
    d->name = strdup (name);
    if (!d->name) {
	LogOutOfMem ("NewDisplay");
	free (d);
	return NULL;
    }
    if (class)
    {
	d->class = strdup (class);
	if (!d->class) {
	    LogOutOfMem ("NewDisplay");
	    free (d->name);
	    free (d);
	    return NULL;
	}
    }
    else
    {
	d->class = NULL;
    }
    /* initialize every field to avoid possible problems */
    d->argv = NULL;
    d->status = notRunning;
    d->pid = -1;
    d->serverPid = -1;
    d->state = NewEntry;
    d->resources = NULL;
    d->xrdb = NULL;
    d->setup = NULL;
    d->startup = NULL;
    d->reset = NULL;
    d->session = NULL;
    d->userPath = NULL;
    d->systemPath = NULL;
    d->systemShell = NULL;
    d->failsafeClient = NULL;
    d->chooser = NULL;
    d->authorize = FALSE;
    d->authorizations = NULL;
    d->authNum = 0;
    d->authNameNum = 0;
    d->clientAuthFile = NULL;
    d->authFile = NULL;
    d->userAuthDir = NULL;
    d->authNames = NULL;
    d->authNameLens = NULL;
    d->authComplain = 1;
    d->openDelay = 0;
    d->openRepeat = 0;
    d->openTimeout = 0;
    d->startAttempts = 0;
    d->startTries = 0;
    d->lastReserv = 0;
    d->reservAttempts = 0;
    d->reservTries = 0;
    d->terminateServer = 0;
    d->grabTimeout = 0;
#ifdef XDMCP
    d->sessionID = 0;
    d->peer = NULL;
    d->peerlen = 0;
    d->from = NULL;
    d->fromlen = 0;
    d->displayNumber = 0;
    d->useChooser = 0;
    d->clientAddr.data = NULL;
    d->clientAddr.length = 0;
    d->connectionType = 0;
    d->xdmcpFd = -1;
#endif
    d->version = 1;		/* registered with The Open Group */
    d->willing = NULL;
    d->dpy = NULL;
    d->windowPath = NULL;
    displays = d;
    return d;
}
