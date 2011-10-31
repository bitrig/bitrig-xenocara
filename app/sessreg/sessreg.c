/*
 * Copyright 1990, 1998  The Open Group
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of The Open Group shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization
 * from The Open Group.
 *
 */

/*
 * Copyright (c) 2005, Oracle and/or its affiliates. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Author:  Keith Packard, MIT X Consortium
 * Lastlog support and dynamic utmp entry allocation
 *   by Andreas Stolcke <stolcke@icsi.berkeley.edu>
 */

/*
 * sessreg
 *
 * simple wtmp/utmp frobber
 *
 * usage: sessreg [ -w <wtmp-file> ] [ -u <utmp-file> ]
 *		  [ -l <line> ]
 *		  [ -L <lastlog-file> ]		      / #ifdef USE_LASTLOG
 *		  [ -h <host-name> ]				/ BSD only
 *		  [ -s <slot-number> ] [ -x Xservers-file ]	/ BSD only
 *		  [ -t <ttys-file> ]				/ BSD only
 *		  [ -a ] [ -d ] user-name
 *
 * one of -a or -d must be specified
 */

#include "sessreg.h"

#include <X11/Xos.h>
#include <X11/Xfuncs.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef USE_UTMP
static void set_utmp (struct utmp *u, char *line, char *user, char *host,
		      time_t date, int addp);
#endif

#ifdef USE_UTMPX
static void set_utmpx (struct utmpx *u, const char *line, const char *user,
		       const char *host, time_t date, int addp);
#endif

static int wflag, uflag, lflag;
static const char *wtmp_file, *utmp_file;
static char *line;
#ifdef USE_UTMPX
#ifdef HAVE_UPDWTMPX
static const char *wtmpx_file = NULL;
#endif
#ifdef HAVE_UTMPXNAME
static const char *utmpx_file = NULL;
#endif
#endif
static int utmp_none, wtmp_none;
/*
 * BSD specific variables.  To make life much easier for Xstartup/Xreset
 * maintainers, these arguments are accepted but ignored for sysV
 */
static int hflag, xflag, tflag;
static char *host_name = NULL;
#if defined(USE_UTMP) && !defined(HAVE_PUTUTLINE)
static int sflag;
static int slot_number;
#endif
static char *xservers_file, *ttys_file;
static char *user_name;
static int aflag, dflag;
#ifdef USE_LASTLOG
static const char *llog_file;
static int llog_none, Lflag;
#endif

static char *program_name;

#if defined(USE_UTMP) && !defined(HAVE_PUTUTLINE)
static int findslot (char *line_name, char *host_name, int addp, int slot);
static int Xslot (char *ttys_file, char *servers_file, char *tty_line,
		  char *host_name, int addp);
#endif

static int
usage (int x)
{
	if (x) {
		fprintf (stderr, "%s: usage %s {-a -d} [-w wtmp-file] [-u utmp-file]", program_name, program_name);
#ifdef USE_LASTLOG
		fprintf (stderr, " [-L lastlog-file]");
#endif
		fprintf (stderr, "\n");
		fprintf (stderr, "             [-t ttys-file] [-l line-name] [-h host-name]\n");
		fprintf (stderr, "             [-s slot-number] [-x servers-file] user-name\n");
		exit (1);
	}
	return x;
}

static char *
getstring (char ***avp, int *flagp)
{
	char	**a = *avp;

	usage ((*flagp)++);
	if (*++*a)
		return *a;
	++a;
	usage (!*a);
	*avp = a;
	return *a;
}

#if defined(USE_UTMP) && !defined(HAVE_PUTUTLINE)
static int
syserr (int x, const char *s)
{
	if (x == -1) {
		perror (s);
		exit (1);
	}
	return x;
}
#endif

static int
sysnerr (int x, const char *s)
{
	if (x == 0) {
		perror (s);
		exit (1);
	}
	return x;
}

int
main (int argc, char **argv)
{
#if defined(USE_UTMP) && !defined(HAVE_PUTUTLINE)
	int		utmp;
#endif
#ifndef USE_UTMPX
	int		wtmp;
#endif
	time_t		current_time;
#ifdef USE_UTMP
	struct utmp	utmp_entry;
#endif
#ifdef USE_UTMPX
	struct utmpx	utmpx_entry;
#endif

	program_name = argv[0];
	while (*++argv && **argv == '-') {
		switch (*++*argv) {
		case 'w':
			wtmp_file = getstring (&argv, &wflag);
			if (!strcmp (wtmp_file, "none"))
				wtmp_none = 1;
			break;
		case 'u':
			utmp_file = getstring (&argv, &uflag);
			if (!strcmp (utmp_file, "none"))
				utmp_none = 1;
			break;
#ifdef USE_LASTLOG
		case 'L':
			llog_file = getstring (&argv, &Lflag);
			if (!strcmp (llog_file, "none"))
				llog_none = 1;
			break;
#endif
		case 't':
			ttys_file = getstring (&argv, &tflag);
			break;
		case 'l':
			line = getstring (&argv, &lflag);
			break;
		case 'h':
			host_name = getstring (&argv, &hflag);
			break;
		case 's':
#if defined(USE_UTMP) && !defined(HAVE_PUTUTLINE)
			slot_number = atoi (getstring (&argv, &sflag));
#endif
			break;
		case 'x':
			xservers_file = getstring (&argv, &xflag);
			break;
		case 'a':
			aflag++;
			break;
		case 'd':
			dflag++;
			break;
		default:
			usage (1);
		}
	}
	usage (!(user_name = *argv++));
	usage (*argv != NULL);
	/*
	 * complain if neither aflag nor dflag are set,
	 * or if both are set.
	 */
	usage (!(aflag ^ dflag));
	usage (xflag && !lflag);
	/* set up default file names */
	if (!wflag) {
		wtmp_file = WTMP_FILE;
#if defined(USE_UTMPX) && defined(HAVE_UPDWTMPX)
		wtmpx_file = WTMPX_FILE;
#endif
	}
#ifndef NO_UTMP
	if (!uflag) {
		utmp_file = UTMP_FILE;
#if defined(USE_UTMPX) && defined(HAVE_UTMPXNAME)
		utmpx_file = UTMPX_FILE;
#endif
	}
#else
	utmp_none = 1;
#endif
#ifdef USE_LASTLOG
	if (!Lflag)
		llog_file = LLOG_FILE;
#endif
#if defined(USE_UTMP) && !defined(HAVE_PUTUTLINE)
	if (!tflag)
		ttys_file = TTYS_FILE;
	if (!sflag && !utmp_none) {
		if (xflag)
			sysnerr (slot_number = Xslot (ttys_file, xservers_file, line, host_name, aflag), "Xslot");
		else
			sysnerr (slot_number = ttyslot (), "ttyslot");
	}
#endif
	if (!lflag) {
		sysnerr ((line = ttyname (0)) != NULL, "ttyname");
		if (strncmp(line, "/dev/", 5) == 0)
			line += 5;
	}
	time (&current_time);
#ifdef USE_UTMP
	set_utmp (&utmp_entry, line, user_name, host_name, current_time, aflag);
#endif

#ifdef USE_UTMPX
	/* need to set utmpxname() before calling set_utmpx() for
	   UtmpxIdOpen to work */
# ifdef HAVE_UTMPXNAME
	if (utmpx_file != NULL) {
		utmpxname (utmpx_file);
	}
# endif
	set_utmpx (&utmpx_entry, line, user_name,
		   host_name, current_time, aflag);
#endif

	if (!utmp_none) {
#ifdef USE_UTMPX
# ifdef HAVE_UTMPXNAME
		if (utmpx_file != NULL)
# endif
		{
			setutxent ();
			(void) getutxid (&utmpx_entry);
			pututxline (&utmpx_entry);
			endutxent ();
		}
#endif
#ifdef USE_UTMP
# ifdef HAVE_PUTUTLINE
		utmpname (utmp_file);
		setutent ();
		(void) getutid (&utmp_entry);
		pututline (&utmp_entry);
		endutent ();
# else
		utmp = open (utmp_file, O_RDWR);
		if (utmp != -1) {
			syserr ((int) lseek (utmp, (long) slot_number * sizeof (struct utmp), 0), "lseek");
			sysnerr (write (utmp, (char *) &utmp_entry, sizeof (utmp_entry))
					== sizeof (utmp_entry), "write utmp entry");
			close (utmp);
		}
# endif
#endif /* USE_UTMP */
	}
	if (!wtmp_none) {
#ifdef USE_UTMPX
# ifdef HAVE_UPDWTMPX
		if (wtmpx_file != NULL) {
			updwtmpx(wtmpx_file, &utmpx_entry);
		}
# endif
#else
		wtmp = open (wtmp_file, O_WRONLY|O_APPEND);
		if (wtmp != -1) {
			sysnerr (write (wtmp, (char *) &utmp_entry, sizeof (utmp_entry))
					== sizeof (utmp_entry), "write wtmp entry");
			close (wtmp);
		}
#endif
	}
#ifdef USE_LASTLOG
	if (aflag && !llog_none) {
		int llog;
		struct passwd *pwd = getpwnam(user_name);

		sysnerr( pwd != NULL, "get user id");
		llog = open (llog_file, O_RDWR);

		if (llog != -1) {
			struct lastlog ll;

			sysnerr (lseek(llog, (long) pwd->pw_uid*sizeof(ll), 0)
					!= -1, "seeking lastlog entry");
			memset(&ll, 0, sizeof(ll));
			ll.ll_time = current_time;
			if (line)
			 (void) strncpy (ll.ll_line, line, sizeof (ll.ll_line));
			if (host_name)
			 (void) strncpy (ll.ll_host, host_name, sizeof (ll.ll_host));

			sysnerr (write (llog, (char *) &ll, sizeof (ll))
					== sizeof (ll), "write lastlog entry");
			close (llog);
		}
	}
#endif
	return 0;
}

/*
 * fill in the appropriate records of the utmp entry
 */

#ifdef USE_UTMP
static void
set_utmp (struct utmp *u, char *line, char *user, char *host, time_t date, int addp)
{
	memset (u, 0, sizeof (*u));
	if (line)
		(void) strncpy (u->ut_line, line, sizeof (u->ut_line));
	else
		memset (u->ut_line, 0, sizeof (u->ut_line));
	if (addp && user)
		(void) strncpy (u->ut_name, user, sizeof (u->ut_name));
	else
		memset (u->ut_name, 0, sizeof (u->ut_name));
#ifdef HAVE_STRUCT_UTMP_UT_ID
	if (line) {
		int	i;
		/*
		 * this is a bit crufty, but
		 * follows the apparent conventions in
		 * the ttys file.  ut_id is only 4 bytes
		 * long, and the last 4 bytes of the line
		 * name are written into it, left justified.
		 */
		i = strlen (line);
		if (i >= sizeof (u->ut_id))
			i -= sizeof (u->ut_id);
		else
			i = 0;
		(void) strncpy (u->ut_id, line + i, sizeof (u->ut_id));
	} else
		memset (u->ut_id, 0, sizeof (u->ut_id));
#endif
#ifdef HAVE_STRUCT_UTMP_UT_PID
	if (addp)
		u->ut_pid = getppid ();
	else
		u->ut_pid = 0;
#endif
#ifdef HAVE_STRUCT_UTMP_UT_TYPE
	if (addp)
		u->ut_type = USER_PROCESS;
	else
		u->ut_type = DEAD_PROCESS;
#endif
#ifdef HAVE_STRUCT_UTMP_UT_HOST
	if (addp && host)
		(void) strncpy (u->ut_host, host, sizeof (u->ut_host));
	else
		memset (u->ut_host, 0, sizeof (u->ut_host));
#endif
	u->ut_time = date;
}
#endif /* USE_UTMP */

#ifdef USE_UTMPX
static int
UtmpxIdOpen( char *utmpId )
{
	struct utmpx *u;	/* pointer to entry in utmp file           */
	int    status = 1;	/* return code                             */

	setutxent();

	while ( (u = getutxent()) != NULL ) {

		if ( (strncmp(u->ut_id, utmpId, 4) == 0 ) &&
		     u->ut_type != DEAD_PROCESS ) {

			status = 0;
			break;
		}
	}

	endutxent();
	return (status);
}

static void
set_utmpx (struct utmpx *u, const char *line, const char *user,
	   const char *host, time_t date, int addp)
{
	static const char letters[] =
	       "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

	if (line)
	{
		if(strcmp(line, ":0") == 0)
			(void) strcpy(u->ut_line, "console");
		else
			(void) strncpy (u->ut_line, line, sizeof (u->ut_line));

		strncpy(u->ut_host, line, sizeof(u->ut_host));
#ifdef HAVE_STRUCT_UTMPX_UT_SYSLEN
		u->ut_syslen = strlen(line);
#endif
	}
	else
		memset (u->ut_line, 0, sizeof (u->ut_line));
	if (addp && user)
		(void) strncpy (u->ut_user, user, sizeof (u->ut_user));
	else
		memset (u->ut_user, 0, sizeof (u->ut_user));

	if (line) {
		int	i;
		/*
		 * this is a bit crufty, but
		 * follows the apparent conventions in
		 * the ttys file.  ut_id is only 4 bytes
		 * long, and the last 4 bytes of the line
		 * name are written into it, left justified.
		 */
		i = strlen (line);
		if (i >= sizeof (u->ut_id))
			i -= sizeof (u->ut_id);
		else
			i = 0;
		(void) strncpy (u->ut_id, line + i, sizeof (u->ut_id));

		/* make sure there is no entry using identical ut_id */
		if (!UtmpxIdOpen(u->ut_id) && addp) {
			int limit = sizeof(letters) - 1;
			int t = 0;

			u->ut_id[1] = line[i];
			u->ut_id[2] = line[i+1];
			u->ut_id[3] = line[i+2];
			do {
				u->ut_id[0] = letters[t];
				t++;
			} while (!UtmpxIdOpen(u->ut_id) && (t < limit));
		}
		if (!addp && strstr(line, ":") != NULL) {
			struct utmpx *tmpu;

			while ( (tmpu = getutxent()) != NULL ) {
				if ( (strcmp(tmpu->ut_host, line) == 0 ) &&
					tmpu->ut_type != DEAD_PROCESS ) {
					strncpy(u->ut_id, tmpu->ut_id,
						sizeof(u->ut_id));
					break;
				}
			}
			endutxent();
		}
	} else
		memset (u->ut_id, 0, sizeof (u->ut_id));

	if (addp) {
		u->ut_pid = getppid ();
		u->ut_type = USER_PROCESS;
	} else {
		u->ut_pid = 0;
		u->ut_type = DEAD_PROCESS;
	}
	u->ut_tv.tv_sec = date;
	u->ut_tv.tv_usec = 0;
}
#endif /* USE_UTMPX */

#if defined(USE_UTMP) && !defined(HAVE_PUTUTLINE)
/*
 * compute the slot-number for an X display.  This is computed
 * by counting the lines in /etc/ttys and adding the line-number
 * that the display appears on in Xservers.  This is a poor
 * design, but is limited by the non-existant interface to utmp.
 * If host_name is non-NULL, assume it contains the display name,
 * otherwise use the tty_line argument (i.e., the tty name).
 */

static int
Xslot (char *ttys_file, char *servers_file, char *tty_line, char *host_name,
       int addp)
{
	FILE	*ttys, *servers;
	int	c;
	int	slot = 1;
	int	column0 = 1;
	char	servers_line[1024];
	char	disp_name[512];
	int	len;
	char	*pos;

	/* remove screen number from the display name */
	memset(disp_name, 0, sizeof(disp_name));
	strncpy(disp_name, host_name ? host_name : tty_line, sizeof(disp_name)-1);
	pos = strrchr(disp_name, ':');
	if (pos) {
		pos = strchr(pos, '.');
		if (pos)
			*pos = '\0';
	}
	sysnerr ((int)(long)(ttys = fopen (ttys_file, "r")), ttys_file);
	while ((c = getc (ttys)) != EOF)
		if (c == '\n') {
			++slot;
			column0 = 1;
		} else
			column0 = 0;
	if (!column0)
		++slot;
	(void) fclose (ttys);
	sysnerr ((int)(long)(servers = fopen (servers_file, "r")), servers_file);

	len = strlen (disp_name);
	column0 = 1;
	while (fgets (servers_line, sizeof (servers_line), servers)) {
		if (column0 && *servers_line != '#') {
			if (!strncmp (disp_name, servers_line, len) &&
			    (servers_line[len] == ' ' ||
			     servers_line[len] == '\t'))
				return slot;
			++slot;
		}
		if (servers_line[strlen(servers_line)-1] != '\n')
			column0 = 0;
		else
			column0 = 1;
	}
	/*
	 * display not found in Xservers file - allocate utmp entry dinamically
	 */
	return findslot (tty_line, host_name, addp, slot);
}

/*
 * find a free utmp slot for the X display.  This allocates a new entry
 * past the regular tty entries if necessary, reusing existing entries
 * (identified by (line,hostname)) if possible.
 */
static int
findslot (char *line_name, char *host_name, int addp, int slot)
{
	int	utmp;
	struct	utmp entry;
	int	found = 0;
	int	freeslot = -1;

	syserr(utmp = open (utmp_file, O_RDONLY), "open utmp");

	/*
	 * first, try to locate a previous entry for this display
	 * also record location of a free slots in case we need a new one
	 */
	syserr ((int) lseek (utmp, (long) slot * sizeof (struct utmp), 0), "lseek");

	if (!host_name)
		host_name = "";

	while (read (utmp, (char *) &entry, sizeof (entry)) == sizeof (entry)) {
		if (strncmp(entry.ut_line, line_name,
			sizeof(entry.ut_line)) == 0
#ifdef HAVE_STRUCT_UTMP_UT_HOST
		    &&
		    strncmp(entry.ut_host, host_name,
			sizeof(entry.ut_host)) == 0
#endif
		   ) {
			found = 1;
			break;
		}
		if (freeslot < 0 && *entry.ut_name == '\0')
			freeslot = slot;
		++slot;
	}

	close (utmp);

	if (found)
		return slot;
	else if (!addp)
		return 0;	/* trying to delete a non-existing entry */
	else if (freeslot < 0)
		return slot;	/* first slot past current entries */
	else
		return freeslot;
}
#endif
