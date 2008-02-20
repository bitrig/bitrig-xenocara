/*
 * calmwm - the calm window manager
 *
 * Copyright (c) 2004 Marius Aamodt Eriksen <marius@monkey.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $Id: util.c,v 1.5 2008/02/20 13:00:18 oga Exp $
 */

#include "headers.h"
#include "calmwm.h"

#define MAXARGLEN 20

int
u_spawn(char *argstr)
{
	char *args[MAXARGLEN], **ap;
	char **end = &args[MAXARGLEN - 1];

	switch (fork()) {
	case 0:
		ap = args;
		while (ap < end && (*ap = strsep(&argstr, " \t")) != NULL)
			ap++;

		*ap = NULL;
		setsid();
		execvp(args[0], args);
		err(1, args[0]);
		break;
	case -1:
		warn("fork");
		return (-1);
	default:
		break;
	}

	return (0);
}

void
exec_wm(char *argstr)
{
	char *args[MAXARGLEN], **ap = args;
	char **end = &args[MAXARGLEN - 1];

	while (ap < end && (*ap = strsep(&argstr, " \t")) != NULL)
		ap++;

	*ap = NULL;
	setsid();
	execvp(args[0], args);
	warn(args[0]);
}

int dirent_isdir(char *filename) {
       struct stat buffer;
       int return_value;

       return_value = stat(filename, &buffer);

       if(return_value == -1)
               return 0;
       else
               return S_ISDIR(buffer.st_mode);
}

int dirent_islink(char *filename) {
       struct stat buffer;
       int return_value;

       return_value = lstat(filename, &buffer);

       if(return_value == -1)
               return 0;
       else
               return S_ISLNK(buffer.st_mode);
}


