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
 * $Id: conf.c,v 1.29 2008/05/15 22:18:00 oga Exp $
 */

#include "headers.h"
#include "calmwm.h"

#ifndef timespeccmp
#define timespeccmp(tsp, usp, cmp)			\
	(((tsp)->tv_sec == (usp)->tv_sec) ?		\
	    ((tsp)->tv_nsec cmp (usp)->tv_nsec) :	\
	    ((tsp)->tv_sec cmp (usp)->tv_sec))
#endif

extern struct screen_ctx	*Curscreen;

/* Add an command menu entry to the end of the menu */
void
conf_cmd_add(struct conf *c, char *image, char *label, int flags)
{
	/* "term" and "lock" have special meanings. */

	if (strcmp(label, "term") == 0) {
		strlcpy(Conf.termpath, image, sizeof(Conf.termpath));
	} else if (strcmp(label, "lock") == 0) {
		strlcpy(Conf.lockpath, image, sizeof(Conf.lockpath));
	} else {
		struct cmd *cmd;
		XMALLOC(cmd, struct cmd);
		cmd->flags = flags;
		strlcpy(cmd->image, image, sizeof(cmd->image));
		strlcpy(cmd->label, label, sizeof(cmd->label));
		TAILQ_INSERT_TAIL(&c->cmdq, cmd, entry);
	}
}

int
conf_changed(char *path)
{
	static struct timespec old_ts;
	struct stat sb;
	int changed;

	/* If the file does not exist we pretend that nothing changed */
	if (stat(path, &sb) == -1 || !(sb.st_mode & S_IFREG))
		return (0);

	changed = !timespeccmp(&sb.st_mtimespec, &old_ts, ==);
	old_ts = sb.st_mtimespec;

	return (changed);
}

void
conf_reload(struct conf *c)
{
	if (!conf_changed(c->conf_path))
		return;

	if (parse_config(c->conf_path, c) == -1) {
		warnx("config file %s has errors, not reloading", c->conf_path);
		return;
	}

	DefaultFont = font_getx(Curscreen, c->DefaultFontName);
}

void
conf_init(struct conf *c)
{
	c->flags = 0;

	TAILQ_INIT(&c->ignoreq);
	TAILQ_INIT(&c->cmdq);
	TAILQ_INIT(&c->keybindingq);
	TAILQ_INIT(&c->autogroupq);

	conf_bindname(c, "CM-Return", "terminal");
	conf_bindname(c, "CM-Delete", "lock");
	conf_bindname(c, "M-question", "exec");
	conf_bindname(c, "CM-w", "exec_wm");
	conf_bindname(c, "M-period", "ssh");
	conf_bindname(c, "M-Return", "hide");
	conf_bindname(c, "M-Down", "lower");
	conf_bindname(c, "M-Up", "raise");
	conf_bindname(c, "M-slash", "search");
	conf_bindname(c, "C-slash", "menusearch");
	conf_bindname(c, "M-Tab", "cycle");
	conf_bindname(c, "MS-Tab", "rcycle");
	conf_bindname(c, "CM-n", "label");
	conf_bindname(c, "CM-x", "delete");
	conf_bindname(c, "CM-0", "nogroup");
	conf_bindname(c, "CM-1", "group1");
	conf_bindname(c, "CM-2", "group2");
	conf_bindname(c, "CM-3", "group3");
	conf_bindname(c, "CM-4", "group4");
	conf_bindname(c, "CM-5", "group5");
	conf_bindname(c, "CM-6", "group6");
	conf_bindname(c, "CM-7", "group7");
	conf_bindname(c, "CM-8", "group8");
	conf_bindname(c, "CM-9", "group9");
	conf_bindname(c, "M-Right", "nextgroup");
	conf_bindname(c, "M-Left", "prevgroup");
	conf_bindname(c, "CM-f", "maximize");
	conf_bindname(c, "CM-equal", "vmaximize");
	conf_bindname(c, "CMS-q", "quit");

	conf_bindname(c, "M-h", "moveleft");
	conf_bindname(c, "M-j", "movedown");
	conf_bindname(c, "M-k", "moveup");
	conf_bindname(c, "M-l", "moveright");
	conf_bindname(c, "M-H", "bigmoveleft");
	conf_bindname(c, "M-J", "bigmovedown");
	conf_bindname(c, "M-K", "bigmoveup");
	conf_bindname(c, "M-L", "bigmoveright");

	conf_bindname(c, "CM-h", "resizeleft");
	conf_bindname(c, "CM-j", "resizedown");
	conf_bindname(c, "CM-k", "resizeup");
	conf_bindname(c, "CM-l", "resizeright");
	conf_bindname(c, "CM-H", "bigresizeleft");
	conf_bindname(c, "CM-J", "bigresizedown");
	conf_bindname(c, "CM-K", "bigresizeup");
	conf_bindname(c, "CM-L", "bigresizeright");

	conf_bindname(c, "C-Left", "ptrmoveleft");
	conf_bindname(c, "C-Down", "ptrmovedown");
	conf_bindname(c, "C-Up", "ptrmoveup");
	conf_bindname(c, "C-Right", "ptrmoveright");
	conf_bindname(c, "CS-Left", "bigptrmoveleft");
	conf_bindname(c, "CS-Down", "bigptrmovedown");
	conf_bindname(c, "CS-Up", "bigptrmoveup");
	conf_bindname(c, "CS-Right", "bigptrmoveright");

	/* Default term/lock */
	strlcpy(c->termpath, "xterm", sizeof(c->termpath));
	strlcpy(c->lockpath, "xlock", sizeof(c->lockpath));

	c->DefaultFontName = DEFAULTFONTNAME;
}

void
conf_setup(struct conf *c, const char *conffile)
{
	if (conffile == NULL) {
		char *home = getenv("HOME");

		if (home == NULL)
			errx(1, "No HOME directory.");

		snprintf(c->conf_path, sizeof(c->conf_path), "%s/%s", home,
		    CONFFILE);
	} else
		snprintf(c->conf_path, sizeof(c->conf_path), "%s", conffile);

	conf_init(c);

	(void)parse_config(c->conf_path, c);
}

int
conf_get_int(struct client_ctx *cc, enum conftype ctype)
{
	int val = -1, ignore = 0;
	char *wname;
	struct winmatch *wm;

	wname = cc->name;

	/* Can wname be NULL? */

	if (wname != NULL) {
		TAILQ_FOREACH(wm, &Conf.ignoreq, entry) {
			int (*cmpfun)(const char *, const char *, size_t) =
			    wm->opts & CONF_IGNORECASE ? strncasecmp : strncmp;
			if ((*cmpfun)(wm->title, wname, strlen(wm->title)) == 0) {
				ignore = 1;
				break;
			}
		}

	} else
		ignore = 1;

	switch (ctype) {
	case CONF_BWIDTH:
		/*
		 * XXX this will be a list, specified in the
		 * configuration file.
		 */
		val = ignore ? 0 : 3;
		break;
	case CONF_IGNORE:
		val = ignore;
		break;
	default:
		break;
	}

	return (val);
}

void
conf_client(struct client_ctx *cc)
{
	cc->bwidth = conf_get_int(cc, CONF_BWIDTH);
	cc->flags |= conf_get_int(cc, CONF_IGNORE) ? CLIENT_IGNORE : 0;
}

struct {
	char *tag;
	void (*handler)(struct client_ctx *, void *);
	int flags;
	void *argument;
} name_to_kbfunc[] = {
	{ "lower", kbfunc_client_lower, KBFLAG_NEEDCLIENT, 0 },
	{ "raise", kbfunc_client_raise, KBFLAG_NEEDCLIENT, 0 },
	{ "search", kbfunc_client_search, 0, 0 },
	{ "menusearch", kbfunc_menu_search, 0, 0 },
	{ "hide", kbfunc_client_hide, KBFLAG_NEEDCLIENT, 0 },
	{ "cycle", kbfunc_client_cycle, 0, 0 },
	{ "rcycle", kbfunc_client_rcycle, 0, 0 },
	{ "label", kbfunc_client_label, KBFLAG_NEEDCLIENT, 0 },
	{ "delete", kbfunc_client_delete, KBFLAG_NEEDCLIENT, 0 },
	{ "group1", kbfunc_client_group, 0, (void *)1 },
	{ "group2", kbfunc_client_group, 0, (void *)2 },
	{ "group3", kbfunc_client_group, 0, (void *)3 },
	{ "group4", kbfunc_client_group, 0, (void *)4 },
	{ "group5", kbfunc_client_group, 0, (void *)5 },
	{ "group6", kbfunc_client_group, 0, (void *)6 },
	{ "group7", kbfunc_client_group, 0, (void *)7 },
	{ "group8", kbfunc_client_group, 0, (void *)8 },
	{ "group9", kbfunc_client_group, 0, (void *)9 },
	{ "nogroup", kbfunc_client_nogroup, 0, 0 },
	{ "nextgroup", kbfunc_client_nextgroup, 0, 0 },
	{ "prevgroup", kbfunc_client_prevgroup, 0, 0 },
	{ "maximize", kbfunc_client_maximize, KBFLAG_NEEDCLIENT, 0 },
	{ "vmaximize", kbfunc_client_vmaximize, KBFLAG_NEEDCLIENT, 0 },
	{ "quit", kbfunc_quit_wm, 0, 0 },
	{ "exec", kbfunc_exec, 0, (void *)CWM_EXEC_PROGRAM },
	{ "exec_wm", kbfunc_exec, 0, (void *)CWM_EXEC_WM },
	{ "ssh", kbfunc_ssh, 0, 0 },
	{ "terminal", kbfunc_term, 0, 0 },
	{ "lock", kbfunc_lock, 0, 0 },
	{ "moveup", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_UP|CWM_MOVE) },
	{ "movedown", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_DOWN|CWM_MOVE) },
	{ "moveright", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_RIGHT|CWM_MOVE) },
	{ "moveleft", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_LEFT|CWM_MOVE) },
	{ "bigmoveup", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_UP|CWM_MOVE|CWM_BIGMOVE) },
	{ "bigmovedown", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_DOWN|CWM_MOVE|CWM_BIGMOVE) },
	{ "bigmoveright", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_RIGHT|CWM_MOVE|CWM_BIGMOVE) },
	{ "bigmoveleft", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_LEFT|CWM_MOVE|CWM_BIGMOVE) },
	{ "resizeup", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_UP|CWM_RESIZE) },
	{ "resizedown", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_DOWN|CWM_RESIZE) },
	{ "resizeright", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_RIGHT|CWM_RESIZE) },
	{ "resizeleft", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_LEFT|CWM_RESIZE) },
	{ "bigresizeup", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_UP|CWM_RESIZE|CWM_BIGMOVE) },
	{ "bigresizedown", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_DOWN|CWM_RESIZE|CWM_BIGMOVE) },
	{ "bigresizeright", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_RIGHT|CWM_RESIZE|CWM_BIGMOVE) },
	{ "bigresizeleft", kbfunc_moveresize, KBFLAG_NEEDCLIENT,
	    (void *)(CWM_LEFT|CWM_RESIZE|CWM_BIGMOVE) },
	{ "ptrmoveup", kbfunc_moveresize, 0, (void *)(CWM_UP|CWM_PTRMOVE) },
	{ "ptrmovedown", kbfunc_moveresize, 0, (void *)(CWM_DOWN|CWM_PTRMOVE) },
	{ "ptrmoveleft", kbfunc_moveresize, 0, (void *)(CWM_LEFT|CWM_PTRMOVE) },
	{ "ptrmoveright", kbfunc_moveresize, 0,
	    (void *)(CWM_RIGHT|CWM_PTRMOVE) },
	{ "bigptrmoveup", kbfunc_moveresize, 0,
	    (void *)(CWM_UP|CWM_PTRMOVE|CWM_BIGMOVE) },
	{ "bigptrmovedown", kbfunc_moveresize, 0,
	    (void *)(CWM_DOWN|CWM_PTRMOVE|CWM_BIGMOVE) },
	{ "bigptrmoveleft", kbfunc_moveresize, 0,
	    (void *)(CWM_LEFT|CWM_PTRMOVE|CWM_BIGMOVE) },
	{ "bigptrmoveright", kbfunc_moveresize, 0,
	    (void *)(CWM_RIGHT|CWM_PTRMOVE|CWM_BIGMOVE) },
	{ NULL, NULL, 0, 0},
};

void
conf_bindname(struct conf *c, char *name, char *binding)
{
	int iter;
	struct keybinding *current_binding;
	char *substring;

	XCALLOC(current_binding, struct keybinding);

	if (strchr(name, 'C') != NULL &&
	    strchr(name, 'C') < strchr(name, '-'))
		current_binding->modmask |= ControlMask;

	if (strchr(name, 'M') != NULL &&
	    strchr(name, 'M') < strchr(name, '-'))
		current_binding->modmask |= Mod1Mask;

	if (strchr(name, '2') != NULL &&
	    strchr(name, '2') < strchr(name, '-'))
		current_binding->modmask |= Mod2Mask;

	if (strchr(name, '3') != NULL &&
	    strchr(name, '3') < strchr(name, '-'))
		current_binding->modmask |= Mod3Mask;

	if (strchr(name, '4') != NULL &&
	    strchr(name, '4') < strchr(name, '-'))
		current_binding->modmask |= Mod4Mask;

	if (strchr(name, 'S') != NULL &&
	    strchr(name, 'S') < strchr(name, '-'))
		current_binding->modmask |= ShiftMask;

	substring = strchr(name, '-') + 1;

	/* If there is no '-' in name, continue as is */
	if (strchr(name, '-') == NULL)
		substring = name;

	if (substring[0] == '[' &&
	    substring[strlen(substring)-1] == ']') {
		sscanf(substring, "[%d]", &current_binding->keycode);
		current_binding->keysym = NoSymbol;
	} else {
		current_binding->keycode = 0;
		current_binding->keysym = XStringToKeysym(substring);
	}

	if (current_binding->keysym == NoSymbol &&
	    current_binding->keycode == 0) {
		xfree(current_binding);
		return;
	}

	/* We now have the correct binding, remove duplicates. */
	conf_unbind(c, current_binding);

	if (strcmp("unmap",binding) == 0)
		return;

	for (iter = 0; name_to_kbfunc[iter].tag != NULL; iter++) {
		if (strcmp(name_to_kbfunc[iter].tag, binding) != 0)
			continue;

		current_binding->callback = name_to_kbfunc[iter].handler;
		current_binding->flags = name_to_kbfunc[iter].flags;
		current_binding->argument = name_to_kbfunc[iter].argument;
		TAILQ_INSERT_TAIL(&c->keybindingq, current_binding, entry);
		break;
	}

	if (name_to_kbfunc[iter].tag != NULL)
		return;

	current_binding->callback = kbfunc_cmdexec;
	current_binding->argument = xstrdup(binding);
	current_binding->flags = 0;
	TAILQ_INSERT_TAIL(&c->keybindingq, current_binding, entry);
	return;
}

void conf_unbind(struct conf *c, struct keybinding *unbind)
{
	struct keybinding *key = NULL;

	TAILQ_FOREACH(key, &c->keybindingq, entry) {
		if (key->modmask != unbind->modmask)
			continue;

		if ((key->keycode != 0 && key->keysym == NoSymbol &&
		    key->keycode == unbind->keycode) ||
		    key->keysym == unbind->keysym)
			TAILQ_REMOVE(&c->keybindingq, key, entry);
	}
}
