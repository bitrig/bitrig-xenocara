/* $Xorg: lock.c,v 1.4 2001/02/09 02:05:59 xorgcvs Exp $ */
/******************************************************************************

Copyright 1994, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.
******************************************************************************/
/* $XFree86: xc/programs/xsm/lock.c,v 3.4 2001/12/14 20:02:25 dawes Exp $ */

#include "xsm.h"
#include "lock.h"
#include "choose.h"
#include <sys/types.h>


static const char *
GetPath(void)
{
    const char *path = getenv ("SM_SAVE_DIR");

    if (!path)
    {
	path = getenv ("HOME");
	if (!path)
	    path = ".";
    }

    return (path);
}


Status
LockSession(const char *session_name, Bool write_id)
{
    const char *path;
    char lock_file[PATH_MAX];
    char temp_lock_file[PATH_MAX];
    Status status;
    int fd;

    path = GetPath ();

    snprintf (lock_file, sizeof(lock_file), "%s/.XSMlock-%s",
	      path, session_name);
    snprintf (temp_lock_file, sizeof(temp_lock_file), "%s/.XSMtlock-%s",
	      path, session_name);

    if ((fd = creat (temp_lock_file, 0444)) < 0)
	return (0);

    if ((write_id &&
        (write (fd, networkIds, strlen (networkIds)) != strlen (networkIds))) ||
	(write (fd, "\n", 1) != 1))
    {
	close (fd);
	return (0);
    }

    close (fd);

    status = 1;

    if (link (temp_lock_file, lock_file) < 0)
	status = 0;

    if (remove (temp_lock_file) < 0)
	status = 0;

    return (status);
}


void
UnlockSession(const char *session_name)
{
    const char *path;
    char lock_file[PATH_MAX];

    path = GetPath ();

    snprintf (lock_file, sizeof(lock_file), "%s/.XSMlock-%s",
	      path, session_name);

    remove (lock_file);
}


char *
GetLockId(const char *session_name)
{
    const char *path;
    FILE *fp;
    char lock_file[PATH_MAX];
    char buf[256];
    char *ret;

    path = GetPath ();

    snprintf (lock_file, sizeof(lock_file), "%s/.XSMlock-%s",
	      path, session_name);

    if ((fp = fopen (lock_file, "r")) == NULL)
    {
	return (NULL);
    }

    buf[0] = '\0';
    fscanf (fp, "%255s\n", buf);
    ret = XtNewString (buf);

    fclose (fp);

    return (ret);
}


Bool
CheckSessionLocked(const char *session_name, Bool get_id, char **id_ret)
{
    if (get_id)
	*id_ret = GetLockId (session_name);

    if (!LockSession (session_name, False))
	return (1);

    UnlockSession (session_name);
    return (0);
}


void
UnableToLockSession(const char *session_name)
{
    /*
     * We should popup a dialog here giving error.
     */

#ifdef XKB
    XkbStdBell(XtDisplay(topLevel),XtWindow(topLevel),0,XkbBI_Failure);
#else
    XBell (XtDisplay (topLevel), 0);
#endif
    sleep (2);

    ChooseSession ();
}
