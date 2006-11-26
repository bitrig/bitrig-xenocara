#if !defined( lint ) && !defined( SABER )
static const char sccsid[] = "@(#)b_lockglue.c  5.01 2001/03/01 xlockmore";

#endif

/*-
 * BUBBLE3D (C) 1998 Richard W.M. Jones.
 * b_lockglue.c: Glue to make this all work with xlockmore.
 *
 * Revision History:
 * 01-Mar-2001: Added FPS stuff - Eric Lassauge <lassauge AT users.sourceforge.net>
 *
 */

#include "bubble3d.h"

/* XXX This lot should eventually be made configurable using the
 * options stuff below.
 */
struct glb_config glb_config =
{
#if GLB_SLOW_GL
	2,			/* subdivision_depth */
#else
	3,			/* subdivision_depth */
#endif
	5,			/* nr_nudge_axes */
	0.3,			/* nudge_angle_factor */
	0.15,			/* nudge_factor */
	0.1,			/* rotation_factor */
	8,			/* create_bubbles_every */
	8,			/* max_bubbles */
	{0.7, 0.8, 0.9, 1.0},	/* p_bubble_group */
	0.5,			/* max_size */
	0.1,			/* min_size */
	0.1,			/* max_speed */
	0.03,			/* min_speed */
	1.5,			/* scale_factor */
	-4,			/* screen_bottom */
	4,			/* screen_top */
#if 1
	{0.1, 0.0, 0.4, 0.0},	/* bg_colour */
#else
	{0.0, 0.0, 0.0, 0.0},	/* bg_colour */
#endif
#if 0
	{0.7, 0.7, 0.0, 0.3}	/* bubble_colour */
#else
	{0.0, 0.0, 0.7, 0.3}	/* bubble_colour */
#endif
};

#ifdef STANDALONE
#define MODE_bubble3d
#define PROGCLASS "Bubble3D"
#define HACK_INIT init_bubble3d
#define HACK_DRAW draw_bubble3d
#define bubble3d_opts xlockmore_opts
#define DEFAULTS	"*delay:	20000 \n"
#include "xlockmore.h"
#else
#include "xlock.h"
#include "vis.h"
#endif

#ifdef MODE_bubble3d

ModeSpecOpt bubble3d_opts =
{0, (XrmOptionDescRec *) NULL, 0, (argtype *) NULL, (OptionStruct *) NULL};

#ifdef USE_MODULES
ModStruct   bubble3d_description =
{(char *) "bubble3d", (char *) "init_bubble3d",
 (char *) "draw_bubble3d", (char *) "release_bubble3d",
 (char *) "draw_bubble3d", (char *) "change_bubble3d",
 (char *) NULL, &bubble3d_opts,
 20000, 1, 1, 1, 64, 1.0, (char *) "",
 (char *) "Richard Jones's GL bubbles", 0, NULL};

#endif /* USE_MODULES */

struct context {
	GLXContext *glx_context;
	void       *draw_context;
};

static struct context *contexts = 0;

static void
init(struct context *c)
{
	glb_sphere_init();
	c->draw_context = glb_draw_init();
}

static void
reshape(int w, int h)
{
	glViewport(0, 0, (GLsizei) w, (GLsizei) h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(45, (GLdouble) w / (GLdouble) h, 3, 8);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0, 0, -5);
}

static void
do_display(struct context *c)
{
	glb_draw_step(c->draw_context);
}

void
init_bubble3d(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	struct context *c;

	if (contexts == NULL) {
		if ((contexts = (struct context *) malloc(sizeof (struct context) *
				MI_NUM_SCREENS(mi))) == NULL)
			return;
	}
	c = &contexts[MI_SCREEN(mi)];
	if ((c->glx_context = init_GL(mi)) != NULL) {
		init(c);
		reshape(MI_WIDTH(mi), MI_HEIGHT(mi));
		do_display(c);
		glFinish();
		glXSwapBuffers(display, window);
	} else
		MI_CLEARWINDOW(mi);
}

void
draw_bubble3d(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	struct context *c;

	if (contexts == NULL)
		return;
	c = &contexts[MI_SCREEN(mi)];

	MI_IS_DRAWN(mi) = True;
	if (!c->glx_context)
		return;

	glDrawBuffer(GL_BACK);
	glXMakeCurrent(display, window, *(c->glx_context));

	do_display(c);

	if (MI_IS_FPS(mi)) do_fps (mi);
	glFinish();
	glXSwapBuffers(display, window);
}

void
change_bubble3d(ModeInfo * mi)
{
	/* nothing */
}

void
release_bubble3d(ModeInfo * mi)
{

	if (contexts != NULL) {
		int         screen;

		for (screen = 0; screen < MI_NUM_SCREENS(mi); screen++) {
			struct context *c = &contexts[screen];

			if (c->glx_context) {
	               		glXMakeCurrent(MI_DISPLAY(mi), MI_WINDOW(mi), *(c->glx_context));
				if (c->draw_context) {
					glb_draw_end(c->draw_context);
				}
			}
		}
		free(contexts);
		contexts = (struct context *) NULL;
	}
	FreeAllGL(mi);
}

#endif /* MODE_bubble3d */
