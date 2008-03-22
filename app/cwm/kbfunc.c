/*
 *  calmwm - the calm window manager
 * 
 *  Copyright (c) 2004 Martin Murray <mmurray@monkey.org>
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
 * $Id: kbfunc.c,v 1.15 2008/03/22 15:09:45 oga Exp $
 */

#include <paths.h>

#include "headers.h"
#include "calmwm.h"

#define KNOWN_HOSTS ".ssh/known_hosts"
#define HASH_MARKER "|1|"
#define MOVE_AMOUNT 1

void
kbfunc_client_lower(struct client_ctx *cc, void *arg)
{
	client_lower(cc);
}

void
kbfunc_client_raise(struct client_ctx *cc, void *arg)
{
	client_raise(cc);
}

void
kbfunc_client_move(struct client_ctx *cc, void *arg)
{
	int x,y,flags,amt;
	u_int mx,my;

	mx = my = 0;

	flags = (int)arg;
	amt = MOVE_AMOUNT;

	if (flags & CWM_BIGMOVE) {
		flags -= CWM_BIGMOVE;
		amt = amt*10;
	}

	switch(flags) {
	case CWM_UP:
		my -= amt;
		break;
	case CWM_DOWN: 
		my += amt;
		break;
	case CWM_RIGHT:
		mx += amt;
		break;
	case CWM_LEFT:
		mx -= amt;
		break;
	}

	cc->geom.y += my;
	cc->geom.x += mx;
	client_move(cc);
	xu_ptr_getpos(cc->pwin, &x, &y);
	cc->ptr.y = y + my;
	cc->ptr.x = x + mx;
	client_ptrwarp(cc);
}

void
kbfunc_client_resize(struct client_ctx *cc, void *arg)
{
	int flags,mx,my;
	u_int amt;

	mx = my = 0;

	flags = (int)arg;
	amt = MOVE_AMOUNT;

	if (flags & CWM_BIGMOVE) {
		flags -= CWM_BIGMOVE;
		amt = amt*10;
	}

	switch(flags) {
	case CWM_UP:
		my -= amt;
		break;
	case CWM_DOWN: 
		my += amt;
		break;
	case CWM_RIGHT:
		mx += amt;
		break;
	case CWM_LEFT:
		mx -= amt;
		break;
	}

	cc->geom.height += my;
	cc->geom.width += mx;
	client_resize(cc);

	/*
	 * Moving the cursor while resizing is problematic. Just place
	 * it in the middle of the window.
	 */
	cc->ptr.x = -1;
	cc->ptr.y = -1;
	client_ptrwarp(cc);
}

void
kbfunc_ptrmove(struct client_ctx *cc, void *arg)
{
	int px,py,mx,my,flags,amt;
	struct screen_ctx *sc = screen_current();
	my = mx = 0;

	flags = (int)arg;
	amt = MOVE_AMOUNT;

	if (flags & CWM_BIGMOVE) {
		flags -= CWM_BIGMOVE;
		amt = amt * 10;
	}
	switch(flags) {
	case CWM_UP:
		my -= amt;
		break;
	case CWM_DOWN: 
		my += amt;
		break;
	case CWM_RIGHT:
		mx += amt;
		break;
	case CWM_LEFT:
		mx -= amt;
		break;
	}

	if (cc) {
		xu_ptr_getpos(cc->pwin, &px, &py);
		xu_ptr_setpos(cc->pwin, px + mx, py + my);
	} else {
		xu_ptr_getpos(sc->rootwin, &px, &py);
		xu_ptr_setpos(sc->rootwin, px + mx, py + my);
	}
}

void
kbfunc_client_search(struct client_ctx *scratch, void *arg)
{
	struct menu_q menuq;
	struct client_ctx *cc, *old_cc = client_current(); 
	struct menu *mi;
	
	TAILQ_INIT(&menuq);
	
	TAILQ_FOREACH(cc, &Clientq, entry) {
		XCALLOC(mi, struct menu);
		strlcpy(mi->text, cc->name, sizeof(mi->text));
		mi->ctx = cc;
		TAILQ_INSERT_TAIL(&menuq, mi, entry);
	}

	if ((mi = search_start(&menuq,
		    search_match_client, search_print_client,
		        "window", 0)) != NULL) {
		cc = (struct client_ctx *)mi->ctx;
		if (cc->flags & CLIENT_HIDDEN)
			client_unhide(cc);

		if (old_cc)
			client_ptrsave(old_cc);
		client_ptrwarp(cc);
	}

	while ((mi = TAILQ_FIRST(&menuq)) != NULL) {
		TAILQ_REMOVE(&menuq, mi, entry);
		xfree(mi);
	}
}

void
kbfunc_menu_search(struct client_ctx *scratch, void *arg)
{
	struct menu_q menuq;
	struct menu *mi;
	struct cmd *cmd;

	TAILQ_INIT(&menuq);

	conf_cmd_refresh(&Conf);
	TAILQ_FOREACH(cmd, &Conf.cmdq, entry) {
		XCALLOC(mi, struct menu);
		strlcpy(mi->text, cmd->label, sizeof(mi->text));
		mi->ctx = cmd;
		TAILQ_INSERT_TAIL(&menuq, mi, entry);
	}

	if ((mi = search_start(&menuq,
		    search_match_text, NULL, "application", 0)) != NULL)
		u_spawn(((struct cmd *)mi->ctx)->image);

	while ((mi = TAILQ_FIRST(&menuq)) != NULL) {
		TAILQ_REMOVE(&menuq, mi, entry);
		xfree(mi);
	}
}

void
kbfunc_client_cycle(struct client_ctx *scratch, void *arg)
{
	client_cyclenext(0);
}

void
kbfunc_client_rcycle(struct client_ctx *scratch, void *arg)
{
	client_cyclenext(1);
}

void
kbfunc_client_hide(struct client_ctx *cc, void *arg)
{
	client_hide(cc);
}

void
kbfunc_cmdexec(struct client_ctx *cc, void *arg)
{
	u_spawn((char *)arg);
}

void
kbfunc_term(struct client_ctx *cc, void *arg)
{
	conf_cmd_refresh(&Conf);
	u_spawn(Conf.termpath);
}

void
kbfunc_lock(struct client_ctx *cc, void *arg)
{
	conf_cmd_refresh(&Conf);
	u_spawn(Conf.lockpath);
}

void
kbfunc_exec(struct client_ctx *scratch, void *arg)
{
#define NPATHS 256
	char **ap, *paths[NPATHS], *path, tpath[MAXPATHLEN];
	int l, i, j, ngroups;
	gid_t mygroups[NGROUPS_MAX];
	uid_t ruid, euid, suid;
	DIR *dirp;
	struct dirent *dp;
	struct stat sb;
	struct menu_q menuq;
	struct menu *mi;
	char *label;

	int cmd = (int)arg;
	switch(cmd) {
		case CWM_EXEC_PROGRAM:
			label = "exec";
			break;
		case CWM_EXEC_WM:
			label = "wm";
			break;
		default:
			err(1, "kbfunc_exec: invalid cmd %d", cmd);
			/*NOTREACHED*/
	}

	if (getgroups(0, mygroups) == -1)
		err(1, "getgroups failure");
	if ((ngroups = getresuid(&ruid, &euid, &suid)) == -1)
		err(1, "getresuid failure");

	TAILQ_INIT(&menuq);
	/* just use default path until we have config to set this */
	path = xstrdup(_PATH_DEFPATH);
	for (ap = paths; ap < &paths[NPATHS - 1] &&
	    (*ap = strsep(&path, ":")) != NULL;) {
		if (**ap != '\0')
			ap++;
	}
	*ap = NULL;
	for (i = 0; i < NPATHS && paths[i] != NULL; i++) {
		if ((dirp = opendir(paths[i])) == NULL)
			continue;

		while ((dp = readdir(dirp)) != NULL) {
			/* skip everything but regular files and symlinks */
			if (dp->d_type != DT_REG && dp->d_type != DT_LNK)
				continue;
			memset(tpath, '\0', sizeof(tpath));
			l = snprintf(tpath, sizeof(tpath), "%s/%s", paths[i],
			    dp->d_name);
			/* check for truncation etc */
			if (l == -1 || l >= (int)sizeof(tpath))
				continue;
			/* just ignore on stat failure */
			if (stat(tpath, &sb) == -1)
				continue;
			/* may we execute this file? */
			if (euid == sb.st_uid) {
					if (sb.st_mode & S_IXUSR)
						goto executable;
					else
						continue;
			}
			for (j = 0; j < ngroups; j++) {
				if (mygroups[j] == sb.st_gid) {
					if (sb.st_mode & S_IXGRP)
						goto executable;
					else
						continue;
				}
			}
			if (sb.st_mode & S_IXOTH)
				goto executable;
			continue;
		executable:
			/* the thing in tpath, we may execute */
			XCALLOC(mi, struct menu);
			strlcpy(mi->text, dp->d_name, sizeof(mi->text));
			TAILQ_INSERT_TAIL(&menuq, mi, entry);
		}
		(void) closedir(dirp);
	}

	if ((mi = search_start(&menuq,
		    search_match_exec, NULL, label, 1)) != NULL) {
		switch (cmd) {
			case CWM_EXEC_PROGRAM:
				u_spawn(mi->text);
				break;
			case CWM_EXEC_WM:
				exec_wm(mi->text);
				break;
			default:
				err(1, "kb_func: egad, cmd changed value!");
				break;
		}
	}

	if (mi != NULL && mi->dummy)
		xfree(mi);
	while ((mi = TAILQ_FIRST(&menuq)) != NULL) {
		TAILQ_REMOVE(&menuq, mi, entry);
		xfree(mi);
	}
	xfree(path);
}

void
kbfunc_ssh(struct client_ctx *scratch, void *arg)
{
	struct menu_q menuq;
	struct menu *mi;
	FILE *fp;
	size_t len;
	char *buf, *lbuf, *p, *home;
	char hostbuf[MAXHOSTNAMELEN], filename[MAXPATHLEN], cmd[256];
	int l;

	if ((home = getenv("HOME")) == NULL)
		return;

	l = snprintf(filename, sizeof(filename), "%s/%s", home, KNOWN_HOSTS);
	if (l == -1 || l >= sizeof(filename))
		return;

	if ((fp = fopen(filename, "r")) == NULL)
		return;

	TAILQ_INIT(&menuq);
	lbuf = NULL;
	while ((buf = fgetln(fp, &len))) {
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		else {
			/* EOF without EOL, copy and add the NUL */
			lbuf = xmalloc(len + 1);
			memcpy(lbuf, buf, len);
			lbuf[len] = '\0';
			buf = lbuf;
		}
		/* skip hashed hosts */
		if (strncmp(buf, HASH_MARKER, strlen(HASH_MARKER)) == 0)
			continue;
		for (p = buf; *p != ',' && *p != ' ' && p != buf + len; p++) {
			/* do nothing */
		}
		/* ignore badness */
		if (p - buf + 1 > sizeof(hostbuf))
			continue;
		(void) strlcpy(hostbuf, buf, p - buf + 1);
		XCALLOC(mi, struct menu);
		(void) strlcpy(mi->text, hostbuf, sizeof(mi->text));
		TAILQ_INSERT_TAIL(&menuq, mi, entry);
	}
	xfree(lbuf);
	fclose(fp);


	if ((mi = search_start(&menuq,
		    search_match_exec, NULL, "ssh", 1)) != NULL) {
		conf_cmd_refresh(&Conf);
		l = snprintf(cmd, sizeof(cmd), "%s -e ssh %s", Conf.termpath,
		    mi->text);
		if (l != -1 && l < sizeof(cmd))
			u_spawn(cmd);
	}

	if (mi != NULL && mi->dummy)
		xfree(mi);
	while ((mi = TAILQ_FIRST(&menuq)) != NULL) {
		TAILQ_REMOVE(&menuq, mi, entry);
		xfree(mi);
	}
}

void
kbfunc_client_label(struct client_ctx *cc, void *arg)
{
	grab_label(cc);
}

void
kbfunc_client_delete(struct client_ctx *cc, void *arg)
{
	client_send_delete(cc);
}

void
kbfunc_client_group(struct client_ctx *cc, void *arg)
{
	group_hidetoggle(KBTOGROUP((int)arg));
}

void
kbfunc_client_nextgroup(struct client_ctx *cc, void *arg)
{
	group_slide(1);
}

void
kbfunc_client_prevgroup(struct client_ctx *cc, void *arg)
{
	group_slide(0);
}

void
kbfunc_client_nogroup(struct client_ctx *cc, void *arg)
{
	group_alltoggle();
}

void
kbfunc_client_maximize(struct client_ctx *cc, void *arg)
{
	client_maximize(cc);
}

void
kbfunc_client_vmaximize(struct client_ctx *cc, void *arg)
{
	client_vertmaximize(cc);
}
