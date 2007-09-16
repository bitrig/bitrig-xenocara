/* $Xorg: xmodmap.c,v 1.4 2001/02/09 02:05:56 xorgcvs Exp $ */
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
/* $XFree86: xc/programs/xmodmap/xmodmap.c,v 1.8tsi Exp $ */

#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "xmodmap.h"

const char *ProgramName;
Display *dpy = NULL;
int min_keycode, max_keycode;
Bool verbose = False;
Bool dontExecute = False;

static void 
Exit(int status)
{
    if (dpy) {
	XCloseDisplay (dpy);
	dpy = NULL;
    }
    exit (status);
}

void *
chk_malloc(size_t n_bytes)
{
    void *buf = malloc(n_bytes);
    if (!buf) {
	fprintf(stderr, "%s: Could not allocate %d bytes\n", ProgramName, (int)n_bytes);
	Exit(-1);
    }
    return buf;
}

static const char help_message[] = 
"\nwhere options include:\n"
"    -display host:dpy            X server to use\n"
"    -verbose, -quiet             turn logging on or off\n"
"    -n                           don't execute changes, just show like make\n"
"    -e expression                execute string\n"
"    -pm                          print modifier map\n"
"    -pk                          print keymap table\n"
"    -pke                         print keymap table as expressions\n"
"    -pp                          print pointer map\n"
"    -grammar                     print out short help on allowable input\n"
"    -                            read standard input\n"
"\n";


static void 
usage(void)
{
    fprintf (stderr, "usage:  %s [-options ...] [filename]\n", ProgramName);
    fprintf (stderr, "%s\n", help_message);
    Exit (1);
}

static const char grammar_message[] = 
"    pointer = default              reset pointer buttons to default\n"
"    pointer = NUMBER ...           set pointer button codes\n"
"    keycode NUMBER = [KEYSYM ...]  map keycode to given keysyms\n"
"    keysym KEYSYM = [KEYSYM ...]   look up keysym and do a keycode operation\n"
"    clear MODIFIER                 remove all keys for this modifier\n"
"    add MODIFIER = KEYSYM ...      add the keysyms to the modifier\n"
"    remove MODIFIER = KEYSYM ...   remove the keysyms from the modifier\n"
"\n"
"where NUMBER is a decimal, octal, or hex constant; KEYSYM is a valid\n"
"Key Symbol name; and MODIFIER is one of the eight modifier names:  Shift,\n"
"Lock, Control, Mod1, Mod2, Mod3, Mod4, or Mod5.  Lines beginning with\n"
"an exclamation mark (!) are taken as comments.  Case is significant except\n"
"for MODIFIER names.\n"
"\n"
"Keysyms on the left hand side of the = sign are looked up before any changes\n"
"are made; keysyms on the right are looked up after all of those on the left\n"
"have been resolved.  This makes it possible to swap modifier keys.\n"
"\n";


static void 
grammar_usage(void)
{
    fprintf (stderr, "%s accepts the following input expressions:\n\n",
	     ProgramName);
    fprintf (stderr, "%s\n", grammar_message);
    Exit (0);
}

int parse_errors = 0;

int
main(int argc, char *argv[])
{
    int i;
    char *displayname = NULL;
    int status;
    Bool printMap = False;
    Bool printKeyTable = False;
    Bool printKeyTableExprs = False;
    Bool printPointerMap = False;
    Bool didAnything = False;

    ProgramName = argv[0];

    /*
     * scan the arg list once to find out which display to use
     */

    for (i = 1; i < argc; i++) {
	if (strncmp (argv[i], "-d", 2) == 0) {
	    if (++i >= argc) usage ();
	    displayname = argv[i];
	}
    }

    dpy = XOpenDisplay (displayname);
    if (!dpy) {
	fprintf (stderr, "%s:  unable to open display '%s'\n",
		 ProgramName, XDisplayName (displayname));
	Exit (1);
    }

    XDisplayKeycodes (dpy, &min_keycode, &max_keycode);

    initialize_map ();

    /*
     * scan the arg list again to do the actual work (since it requires
     * the display being open.
     */

    status = 0;
    for (i = 1; i < argc; i++) {
	char *arg = argv[i];

	if (arg[0] == '-') {
	    switch (arg[1]) {
	      case 'd':			/* -display host:dpy */
		++i;			/* handled above */
		continue;
	      case 'v':			/* -verbose */
		verbose = True;
		continue;
	      case 'q':			/* -quiet */
		verbose = False;
		continue;
	      case 'n':			/* -n (like make) */
		dontExecute = True;
		continue;
	      case 'e':			/* -e expression */
		didAnything = True;
		if (++i >= argc) usage ();
		process_line (argv[i]);
		continue;
	      case 'p':			/* -p... */
		switch (arg[2]) {
		  case '\0':
		  case 'm':		/* -pm */
		    printMap = True;
		    break;
		  case 'k':		/* -pk, -pke */
		    switch (arg[3]) {
		    case '\0':
			printKeyTable = True;
			break;
		    case 'e':
			printKeyTableExprs = True;
			break;
		    default:
			usage ();
		    }
		    break;
		  case 'p':		/* -pp */
		    printPointerMap = True;
		    break;
		  default:
		    usage ();
		    /* NOTREACHED */
		}
		didAnything = True;
		continue;
	      case 'g':			/* -grammar */
		grammar_usage ();
		/*NOTREACHED*/
	      case '\0':		/* - (use standard input) */
		didAnything = True;
		process_file (NULL);
		continue;

	      /*
	       * provide old xmodmap args
	       */
	      case 'S':
		didAnything = True;
		process_line ("clear shift");
		continue;
	      case 'L':
		didAnything = True;
		process_line ("clear lock");
		continue;
	      case 'C':
		didAnything = True;
		process_line ("clear control");
		continue;
	      case '1':
	      case '2':
	      case '3':
	      case '4':
	      case '5': {
		  char cmd[11] = "clear modX";
		  cmd[9] = arg[1];
		  process_line (cmd);
		  continue;
	      }
	      case 's':
	      case 'l':
	      case 'c': {
		  char *cmd;
		  didAnything = True;
		  if (++i >= argc) usage ();
		  cmd = chk_malloc (strlen ("remove control = ") + strlen (argv[i]) + 1);
		  (void) sprintf (cmd, "remove %s = %s",
				  ((arg[1] == 's') ? "shift" :
				   ((arg[1] == 'l') ? "lock" :
				    "control")), argv[i]);
		  process_line (cmd);
		  continue;
	      }
	      default:
		usage ();
		/*NOTREACHED*/
	    }
	} else if (arg[0] == '+') {	/* old xmodmap args */
	    switch (arg[1]) {
	      case '1':
	      case '2':
	      case '3':
	      case '4':
	      case '5': {
		  char *cmd;
		  didAnything = True;
		  if (++i >= argc) usage ();
		  cmd = chk_malloc (strlen ("add modX = ") + strlen (argv[i]) + 1);
		  (void) sprintf (cmd, "add mod%c = %s", arg[1], argv[i]);
		  process_line (cmd);
		  continue;
	      }
	      case 'S':
	      case 'L':
	      case 'C':
		arg[1] = tolower (arg[1]);
		/* fall through to handler below */
	      case 's':
	      case 'l':
	      case 'c': {
		  char *cmd;
		  didAnything = True;
		  if (++i >= argc) usage ();
		  cmd = chk_malloc (strlen ("add control = ") + strlen (argv[i]) + 1);
		  (void) sprintf (cmd, "add %s = %s",
				  ((arg[1] == 's') ? "shift" :
				   ((arg[1] == 'l') ? "lock" :
				    "control")), argv[i]);
		  process_line (cmd);
		  continue;
	      }
	      default:
		usage ();
	    }
	} else {
	    didAnything = True;
	    process_file (arg);
	    continue;
	}
    }					/* end for loop */

    /* for compatibility */
    if (!didAnything) printMap = True;

    /*
     * at this point, the work list has been built and we can view it or
     * execute it
     */

    if (dontExecute) {
	print_work_queue ();
	Exit (0);
    }

    if (parse_errors != 0) {
	fprintf (stderr, "%s:  %d error%s encountered, aborting.\n",
		 ProgramName, parse_errors,
		 (parse_errors == 1 ? "" : "s"));
	status = -1;	/* return an error condition */
    } else {
	status = execute_work_queue ();
    }

    if (printMap) {
	print_modifier_map ();
    }

    if (printKeyTable) {
	print_key_table (False);
    }

    if (printKeyTableExprs) {
	print_key_table (True);
    }

    if (printPointerMap) {
	print_pointer_map ();
    }

    Exit (status < 0 ? 1 : 0);

    /* Muffle gcc */
    return 0;
}

