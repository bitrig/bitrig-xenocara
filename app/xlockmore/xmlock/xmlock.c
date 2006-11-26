#if !defined( lint ) && !defined( SABER )

static const char sccsid[] = "@(#)xmlock.c	4.08 98/02/18 xlockmore";

#endif

/*-
 * xmlock.c - main file for xmlock, the gui interface to xlock.
 *
 * Copyright (c) 1996 by Charles Vidal
 *
 * See xlock.c for copying information.
 *
 * Revision History:
 * Jun-03: Code added from Xlockup by Thad Phetteplace
 *     tdphette@dexter.glaci.com
 *   Sun -- works (both timeouts seem connected to the same device)
 *   Linux -- mouse timeout works, keyboard timeout does not work
 *   Cygwin -- mouse timout does not work, keyboard timeout does not work
 *   Alpha -- "does something"  ;)
 *   Since keyboard timeout does not seem to help, it was removed.
 * Nov-96: Continual minor improvements by Charles Vidal and David Bagley.
 * Oct-96: written.
 */

/*-
  XmLock Problems
  1. Allowing only one in -inroot.  Need a way to kill it.
  2. XLock resources need to be read and used to set initial values.
  3. Integer and floating point and string input.
 */

/*
 * 01/2001
 * I'm confused because the source is old and really not well programming
 * I put boolean option and option in the menubar
 * I have had the option.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/signal.h>
#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#define HAVE_UNISTD_H 1
#endif /* HAVE_CONFIG_H */
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#ifdef VMS
#include "vms_x_fix.h"
#include <descrip.h>
#include <lib$routines.h>
#endif

#ifdef USE_MB
#include <X11/Xlocale.h>
#endif

#include <Xm/PanedW.h>
#include <Xm/RowColumn.h>
#include <Xm/ToggleB.h>
#include <Xm/List.h>
#include <Xm/PushB.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/Scale.h>

#if USE_XMU
#include <X11/Xmu/Editres.h>
#endif

#include "option.h"

#ifdef SOLARIS2
extern int  kill(pid_t, int);
extern pid_t  wait(int *);
#else
#if 0
extern int  kill(int, int);
extern pid_t  wait(int *);

#endif
#endif

#include "bitmaps/m-xlock.xbm"	/* icon bitmap */

#define SKIPDELAY 60000 /* 1 minute */
#define MAXMIN 120

/* like an enum */
enum ACTION {
  LAUNCH=0,
  ROOT,
  WINDOW
	};

/* extern variable */
extern Widget Menuoption;
extern Widget Toggles[TOGGLES];
extern OptionStruct Opt[];

extern char *r_Toggles[];

static pid_t numberprocess = -1;	/* PID of xlock */

/*************************
 * To know the number of element
 * in the Opt Array
**************************/
extern int getNumberofElementofOpt();

/* Widget */
Widget      toplevel;
static Widget Labelxlock;
static Widget      timeoutSlider;

static time_t timenow;

/* If this worked on all systems it would be 30 min or 1800 */
static long timeout = 0;

static Widget ScrolledListModes, PushButtons[PUSHBUTTONS];

 /*Resource string */
#include "modes.h"

#if 0
static XmStringCharSet char_set = (XmStringCharSet) XmSTRING_DEFAULT_CHARSET;
#endif

/* some resources of buttons and toggles not really good programming :( */

static char *r_PushButtons[PUSHBUTTONS] =
{
	(char *) "Launch",
	(char *) "In Root",
	(char *) "In Window",
};

static int  numberinlist = 0;

static void checkTime();
/* CallBack */

void exitcallback(Widget w, XtPointer client_data, XtPointer call_data)
{
	if (numberprocess != -1) {
		(void) kill(numberprocess, SIGKILL);
		/*(void) wait(&n);*/
	}
	exit(0);
}

static void
callLocker(int choice)
{
	int         i;
	char        command[500];

#ifdef VMS
	int         mask = 17;
	struct dsc$descriptor_d vms_image;

#endif

	(void) sprintf(command, "%s ", XLOCK);

	/* booleans (+/-) options */

	for (i = 0; i < TOGGLES; i++) {
		if (XmToggleButtonGetState(Toggles[i])) {
			(void) strcat(command, "-");
			(void) strcat(command, r_Toggles[i]);
			(void) strcat(command, " ");
		}
	}
	/*
		sizeof(Opt)/sizeof(OptionStruct) is not good to
		know the number of element in Opt
		so I have made a function getNumberofElementofOpt
	*/
	for (i = 0; i < getNumberofElementofOpt(); i++)
		if (Opt[i].userdata != NULL) {
			(void) strcat(command, "-");
			(void) strcat(command, Opt[i].cmd);
			(void) strcat(command, " ");
			(void) strcat(command, Opt[i].userdata);
			(void) strcat(command, " ");
		}

	switch (choice) {
		case LAUNCH:
			/* the default value then nothing to do */
			break;
		case WINDOW:
			(void) strcat(command, "-inwindow ");
			break;
		case ROOT:
			(void) strcat(command, "-inroot ");
			break;
	}
	(void) strcat(command, "-mode ");
	(void) strcat(command, LockProcs[numberinlist].cmdline_arg);
#ifdef VMS
	vms_image.dsc$w_length = strlen(command);
	vms_image.dsc$a_pointer = command;
	vms_image.dsc$b_class = DSC$K_CLASS_S;
	vms_image.dsc$b_dtype = DSC$K_DTYPE_T;
	(void) printf("%s\n", command);
	(void) lib$spawn(&vms_image, 0, 0, &mask);
#else
	if (choice != LAUNCH)
		(void) strcat(command, " & ");
	(void) printf("%s\n", command);
	(void) system(command);
#endif
	if (choice == LAUNCH)
		(void) XtAppAddTimeOut(XtWidgetToApplicationContext(toplevel),
			SKIPDELAY, (XtTimerCallbackProc) checkTime, toplevel);
}

/*----------------------------------------------------------*/
/* Code taken from Xlockup by Thad Phetteplace              */
/*    tdphette@dexter.glaci.com used by permission          */
/* CHECKTIME: This routine is called periodicaly by the     */
/*    openwin event handler.  It checks the last access     */
/*    time/date stamp on the mouse and keyboard devices     */
/*    and compares it to the timeout value.                 */
/*----------------------------------------------------------*/

static void checkTime()
{
  struct stat statmouse;
#if 0
  struct stat statkeybd;
#endif

  (void) time( &timenow );                   /* get current time */
  /* next does not work on Cygwin */
  (void) stat("/dev/mouse", &statmouse );   /* get mouse status */
#if 0
  /* does not seem to help on Solaris and hinders on Linux */
  (void) stat("/dev/kbd", &statkeybd );     /* get keyboard status */
#endif

  /* check if last access time was larger than timeout */
  if (timeout > 0 && (timenow-statmouse.st_atime >= timeout) /* &&
      (timenow-statkeybd.st_atime >= timeout)*/) {
     callLocker(LAUNCH);
  }
#ifdef DEBUG
  printf("timeout %ld,  timenow - amouse %ld\n",
    timeout, timenow - statmouse.st_atime);
  printf("timeout %ld,  timenow - akeybd %ld\n",
    timeout, timenow - statkeybd.st_atime);
#endif
  (void) XtAppAddTimeOut(XtWidgetToApplicationContext(toplevel),
	SKIPDELAY, (XtTimerCallbackProc) checkTime, toplevel);
}

static void
f_PushButtons(Widget w, XtPointer client_data, XtPointer call_data)
{
	callLocker((int) client_data);
}

static void
f_ScrolledListModes(Widget w, XtPointer client_data, XtPointer call_data)
{
	char        numberwidget[50];
	char        str[50];
	int         n;

	numberinlist = ((XmListCallbackStruct *) call_data)->item_position - 1;
	(void) sprintf(numberwidget, "%ld", XtWindow(Labelxlock));
	(void) sprintf(str, "%s", LockProcs[numberinlist].cmdline_arg);
	if (numberprocess != -1) {
		(void) kill(numberprocess, SIGKILL);
		(void) wait(&n);
	}
#ifdef VMS
#define FORK vfork
#else
#define FORK fork
#endif
	if ((numberprocess = FORK()) == -1)
		(void) fprintf(stderr, "Fork error\n");
	else if (numberprocess == 0) {
		(void) execlp(XLOCK, XLOCK, "-parent", numberwidget,
			"-mode", str, "-geometry", WINDOW_GEOMETRY,
			"-delay", "100000",
			"-nolock", "-inwindow", "+install", 0);
	}
}

static void
TimeoutSlider(Widget w, XtPointer clientData, XmScaleCallbackStruct * cbs)
{
	timeout = cbs->value * 60;
}

/* Setup Widget */

/**********************/

static void
Setup_Widget(Widget father)
{
	Arg         args[15];
	int         i, ac;
	Widget      PushButtonRow, exitwidgetB;
	char        string[160];
	XmString    label_str;
	XmString    TabXmStr[numprocs];

/* buttons in the bottom */
	ac = 0;
	XtSetArg(args[ac], XmNorientation, XmHORIZONTAL);
	ac++;
	XtSetArg(args[ac], XmNrightAttachment, XmATTACH_FORM);
	ac++;
	XtSetArg(args[ac], XmNleftAttachment, XmATTACH_FORM);
	ac++;
	XtSetArg(args[ac], XmNtopAttachment, XmATTACH_FORM);
	ac++;

	Menuoption = XmCreateMenuBar(father, (char *) "MenuBar", args,ac);
	XtManageChild(Menuoption);

	ac = 0;
	XtSetArg(args[ac], XmNwidth, WINDOW_WIDTH);
	ac++;
	XtSetArg(args[ac], XmNheight, WINDOW_HEIGHT);
	ac++;

	XtSetArg(args[ac], XmNtopAttachment, XmATTACH_WIDGET); ac++;
	XtSetArg(args[ac], XmNtopWidget, Menuoption); ac++;

#if 0
	int ModeLabel;
	XtSetArg(args[ac], XmNleftAttachment, XmATTACH_FORM); ac++;

	XtSetArg(args[ac], XmNrightAttachment, XmATTACH_FORM); ac++;

	XtSetArg(args[ac], XmNleftWidget, ModeLabel); ac++;
#endif

	Labelxlock = XmCreateLabel(father, (char *) "Window", args, ac);
	XtManageChild(Labelxlock);

	/* buttons in the bottom */
	 ac = 0;

	XtSetArg(args[ac], XmNorientation, XmHORIZONTAL);
	ac++;
	XtSetArg(args[ac], XmNrightAttachment, XmATTACH_FORM);
	ac++;
	XtSetArg(args[ac], XmNleftAttachment, XmATTACH_FORM);
	ac++;
	XtSetArg(args[ac], XmNbottomAttachment, XmATTACH_FORM);
	ac++;
	PushButtonRow = XmCreateRowColumn(father, (char *) "PushButtonRow", args, ac);

	for (i = 0; i < PUSHBUTTONS; i++) {
		ac = 0;
#ifndef USE_MB
		label_str = XmStringCreate(r_PushButtons[i], XmSTRING_DEFAULT_CHARSET);
		XtSetArg(args[ac], XmNlabelString, label_str);
		ac++;
#endif
		PushButtons[i] = XmCreatePushButton(PushButtonRow,
			 r_PushButtons[i], args, ac);
#ifndef USE_MB
		XmStringFree(label_str);
#endif
		XtAddCallback(PushButtons[i], XmNactivateCallback,
			f_PushButtons, (XtPointer) i);
		XtManageChild(PushButtons[i]);
	}
/*
 *  For the exit button
 */
	ac = 0;
#ifndef USE_MB
	label_str = XmStringCreate((char *) "Exit",
		XmSTRING_DEFAULT_CHARSET);
	XtSetArg(args[ac], XmNlabelString, label_str);
	ac++;
#endif
	exitwidgetB = XmCreatePushButton(PushButtonRow,
		(char *) "Exit", args, ac);
#ifndef USE_MB
	XmStringFree(label_str);
#endif
	XtAddCallback(exitwidgetB, XmNactivateCallback, exitcallback,
		(XtPointer) NULL);
	XtManageChild(exitwidgetB);
	timeoutSlider = XtVaCreateManagedWidget("timeout",
		xmScaleWidgetClass, PushButtonRow,
		XtVaTypedArg, XmNtitleString, XmRString, "Timeout", 8,
		XmNminimum, 0,
		XmNmaximum, MAXMIN,
		XmNvalue, timeout / 60,
		XmNshowValue, True,
		XmNorientation, XmHORIZONTAL, NULL);
	XtAddCallback(timeoutSlider, XmNvalueChangedCallback,
		(XtCallbackProc) TimeoutSlider, (XtPointer) NULL);

	XtManageChild(PushButtonRow);

/* list and toggles in row like that (row(list)(TogglesRow(toggles...))) */
	ac = 0;
	XtSetArg(args[ac], XmNtopAttachment, XmATTACH_WIDGET);
	ac++;
	XtSetArg(args[ac], XmNtopWidget, Labelxlock);
	ac++;
	XtSetArg(args[ac], XmNrightAttachment, XmATTACH_FORM);
	ac++;
	XtSetArg(args[ac], XmNleftAttachment, XmATTACH_FORM);
	ac++;
	XtSetArg(args[ac], XmNbottomAttachment, XmATTACH_WIDGET);
	ac++;
	XtSetArg(args[ac], XmNbottomWidget, PushButtonRow);
	ac++;
	XtSetArg(args[ac], XmNitems, TabXmStr);
	ac++;
	XtSetArg(args[ac], XmNitemCount, numprocs);
	ac++;
	XtSetArg(args[ac], XmNvisibleItemCount, 10);
	ac++;


	for (i = 0; i < (int) numprocs; i++) {
		(void) sprintf(string, "%-14s%s", LockProcs[i].cmdline_arg,
			       LockProcs[i].desc);
		TabXmStr[i] = XmStringCreate(string, XmSTRING_DEFAULT_CHARSET);
	}
		ScrolledListModes = XmCreateScrolledList(father, (char *) "ScrolledListModes",
						 args, ac);
	XtAddCallback(ScrolledListModes, XmNbrowseSelectionCallback,
		      f_ScrolledListModes, NULL);
	XtManageChild(ScrolledListModes);

#if 0
	int TogglesRow, Row;
	TogglesRow = XmCreateRowColumn(Row, (char *) "TogglesRow", NULL, 0);
	for (i = 0; i < TOGGLES; i++) {
#ifndef USE_MB
		ac = 0;
		label_str = XmStringCreate(r_Toggles[i], XmSTRING_DEFAULT_CHARSET);
		XtSetArg(args[ac], XmNlabelString, label_str);
		ac++;
#endif
		Toggles[i] = XmCreateToggleButton(TogglesRow, r_Toggles[i], args, ac);
#ifndef USE_MB
		XmStringFree(label_str);
#endif
		XtManageChild(Toggles[i]);
	}
	XtManageChild(TogglesRow);

	XtManageChild(Row);
#endif

	for (i = 0; i < (int) numprocs; i++) {
		XmStringFree(TabXmStr[i]);
	}
}

extern void setup_Option(Widget MenuBar);

int
main(int argc, char **argv)
{
	Widget      form;
	Arg         args[15];

#ifdef USE_MB
	setlocale(LC_ALL, "");
	XtSetLanguageProc(NULL, NULL, NULL);
#endif

/* PURIFY 4.0.1 on Solaris 2 reports an unitialized memory read on the next
   line. */
	toplevel = XtInitialize(argv[0], "XmLock", (XrmOptionDescRec*) NULL, 0,
		&argc, argv);
	XtSetArg(args[0], XtNiconPixmap,
		 XCreateBitmapFromData(XtDisplay(toplevel),
			RootWindowOfScreen(XtScreen(toplevel)),
			(char *) image_bits, image_width, image_height));
	XtSetValues(toplevel, args, 1);
	/* creation Widget */
/* PURIFY 4.0.1 on Solaris 2 reports an unitialized memory read on the next
   line. */
	form = XmCreateForm(toplevel, (char *) "Form", (Arg *) NULL, 0);
	Setup_Widget(form);
	setup_Option(Menuoption);
	XtManageChild(form);
	XtRealizeWidget(toplevel);
	(void) XtAppAddTimeOut(XtWidgetToApplicationContext(toplevel),
		SKIPDELAY, (XtTimerCallbackProc) checkTime, toplevel);
#if USE_XMU
	/* With this handler you can use editres */
	XtAddEventHandler(toplevel, (EventMask) 0, TRUE,
		(XtEventHandler) _XEditResCheckMessages, NULL);
#endif
	XtMainLoop();
#ifdef VMS
	return 1;
#else
	return 0;
#endif
}
