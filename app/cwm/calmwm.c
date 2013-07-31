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
 * $OpenBSD: calmwm.c,v 1.80 2013/07/15 14:50:44 okan Exp $
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "calmwm.h"

char				**cwm_argv;
Display				*X_Dpy;
Atom				 cwmh[CWMH_NITEMS];
Atom				 ewmh[EWMH_NITEMS];

struct screen_ctx_q		 Screenq = TAILQ_HEAD_INITIALIZER(Screenq);
struct client_ctx_q		 Clientq = TAILQ_HEAD_INITIALIZER(Clientq);

int				 HasRandr, Randr_ev;
struct conf			 Conf;
char				*homedir;

static void	sigchld_cb(int);
static int	x_errorhandler(Display *, XErrorEvent *);
static void	x_init(const char *);
static void	x_teardown(void);
static int	x_wmerrorhandler(Display *, XErrorEvent *);

int
main(int argc, char **argv)
{
	const char	*conf_file = NULL;
	char		*conf_path, *display_name = NULL;
	int		 ch;
	struct passwd	*pw;

	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		warnx("no locale support");
	mbtowc(NULL, NULL, MB_CUR_MAX);

	cwm_argv = argv;
	while ((ch = getopt(argc, argv, "c:d:")) != -1) {
		switch (ch) {
		case 'c':
			conf_file = optarg;
			break;
		case 'd':
			display_name = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (signal(SIGCHLD, sigchld_cb) == SIG_ERR)
		err(1, "signal");

	if ((homedir = getenv("HOME")) == NULL || *homedir == '\0') {
		pw = getpwuid(getuid());
		if (pw != NULL && pw->pw_dir != NULL && *pw->pw_dir != '\0')
			homedir = pw->pw_dir;
		else
			homedir = "/";
	}

	if (conf_file == NULL)
		xasprintf(&conf_path, "%s/%s", homedir, CONFFILE);
	else
		conf_path = xstrdup(conf_file);

	if (access(conf_path, R_OK) != 0) {
		if (conf_file != NULL)
			warn("%s", conf_file);
		free(conf_path);
		conf_path = NULL;
	}

	conf_init(&Conf);
	if (conf_path && (parse_config(conf_path, &Conf) == -1))
		warnx("config file %s has errors, not loading", conf_path);
	free(conf_path);

	x_init(display_name);
	xev_loop();
	x_teardown();

	return (0);
}

static void
x_init(const char *dpyname)
{
	int	i;

	if ((X_Dpy = XOpenDisplay(dpyname)) == NULL)
		errx(1, "unable to open display \"%s\"", XDisplayName(dpyname));

	XSetErrorHandler(x_wmerrorhandler);
	XSelectInput(X_Dpy, DefaultRootWindow(X_Dpy), SubstructureRedirectMask);
	XSync(X_Dpy, False);
	XSetErrorHandler(x_errorhandler);

	HasRandr = XRRQueryExtension(X_Dpy, &Randr_ev, &i);

	conf_atoms();

	conf_cursor(&Conf);

	for (i = 0; i < ScreenCount(X_Dpy); i++)
		screen_init(i);
}

static void
x_teardown(void)
{
	XCloseDisplay(X_Dpy);
}

static int
x_wmerrorhandler(Display *dpy, XErrorEvent *e)
{
	errx(1, "root window unavailable - perhaps another wm is running?");

	return (0);
}

static int
x_errorhandler(Display *dpy, XErrorEvent *e)
{
#ifdef DEBUG
	char msg[80], number[80], req[80];

	XGetErrorText(X_Dpy, e->error_code, msg, sizeof(msg));
	(void)snprintf(number, sizeof(number), "%d", e->request_code);
	XGetErrorDatabaseText(X_Dpy, "XRequest", number,
	    "<unknown>", req, sizeof(req));

	warnx("%s(0x%x): %s", req, (u_int)e->resourceid, msg);
#endif
	return (0);
}

static void
sigchld_cb(int which)
{
	pid_t	 pid;
	int	 save_errno = errno;
	int	 status;

	/* Collect dead children. */
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0 ||
	    (pid < 0 && errno == EINTR))
		;

	errno = save_errno;
}

__dead void
usage(void)
{
	extern char	*__progname;

	(void)fprintf(stderr, "usage: %s [-c file] [-d display]\n",
	    __progname);
	exit(1);
}
