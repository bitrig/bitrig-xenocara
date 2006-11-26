/* -*- Mode: C; tab-width: 4 -*- */
/* pipes --- 3D selfbuiding pipe system */

#if !defined( lint ) && !defined( SABER )
static const char sccsid[] = "@(#)pipes.c	5.00 2000/11/01 xlockmore";

#endif

/*-
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appear in all copies and that
 * both that copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * This file is provided AS IS with no warranties of any kind.  The author
 * shall have no liability with respect to the infringement of copyrights,
 * trade secrets or any patents by this file or any part thereof.  In no
 * event will the author be liable for any lost revenue or profits or
 * other special, indirect and consequential damages.
 *
 * This program was inspired on a WindowsNT(R)'s screen saver. It was written
 * from scratch and it was not based on any other source code.
 *
 * ==========================================================================
 * The routine myElbow is derivated from the doughnut routine from the MesaGL
 * library (more especifically the Mesaaux library) written by Brian Paul.
 * ==========================================================================
 *
 * Thanks goes to Brian Paul for making it possible and inexpensive to use
 * OpenGL at home.
 *
 * Since I'm not a native English speaker, my apologies for any grammatical
 * mistakes.
 *
 * My e-mail addresses is
 * mfvianna@centroin.com.br
 *
 * Marcelo F. Vianna (Apr-09-1997)
 *
 * Revision History:
 * 29-Apr-97: Factory equipment by Ed Mackey.  Productive day today, eh?
 * 29-Apr-97: Less tight turns Jeff Epler <jepler@inetnebr.com>
 * 29-Apr-97: Efficiency speed-ups by Marcelo F. Vianna
 */

#ifdef VMS
/*-
 * due to a Bug/feature in VMS X11/Intrinsic.h has to be placed before xlock.
 * otherwise caddr_t is not defined correctly
 */
#include <X11/Intrinsic.h>
#endif

#ifdef STANDALONE
#define MODE_pipes
#define PROGCLASS "Pipes"
#define HACK_INIT init_pipes
#define HACK_DRAW draw_pipes
#define pipes_opts xlockmore_opts
#define DEFAULTS "*delay: 1000 \n" \
 "*count: 2 \n" \
 "*cycles: 5 \n" \
 "*size: 500 \n" \
 "*fisheye: True \n" \
 "*tightturns: False \n" \
 "*rotatepipes: True \n" \
 "*noBuffer: True \n"
#include "xlockmore.h"		/* from the xscreensaver distribution */
#else /* !STANDALONE */
#include "xlock.h"		/* from the xlockmore distribution */
#include "vis.h"
#endif /* !STANDALONE */

#ifdef MODE_pipes

#include <GL/glu.h>
#include "buildlwo.h"

#define DEF_FACTORY     "2"
#define DEF_FISHEYE     "True"
#define DEF_TIGHTTURNS  "False"
#define DEF_ROTATEPIPES "True"
#define NofSysTypes     3

static int  factory;
static Bool fisheye, tightturns, rotatepipes;

static XrmOptionDescRec opts[] =
{
	{(char *) "-factory", (char *) ".pipes.factory", XrmoptionSepArg, (caddr_t) NULL},
	{(char *) "-fisheye", (char *) ".pipes.fisheye", XrmoptionNoArg, (caddr_t) "on"},
	{(char *) "+fisheye", (char *) ".pipes.fisheye", XrmoptionNoArg, (caddr_t) "off"},
	{(char *) "-tightturns", (char *) ".pipes.tightturns", XrmoptionNoArg, (caddr_t) "on"},
	{(char *) "+tightturns", (char *) ".pipes.tightturns", XrmoptionNoArg, (caddr_t) "off"},
      {(char *) "-rotatepipes", (char *) ".pipes.rotatepipes", XrmoptionNoArg, (caddr_t) "on"},
      {(char *) "+rotatepipes", (char *) ".pipes.rotatepipes", XrmoptionNoArg, (caddr_t) "off"}
};
static argtype vars[] =
{
	{(void *) & factory, (char *) "factory", (char *) "Factory", (char *) DEF_FACTORY, t_Int},
	{(void *) & fisheye, (char *) "fisheye", (char *) "Fisheye", (char *) DEF_FISHEYE, t_Bool},
	{(void *) & tightturns, (char *) "tightturns", (char *) "Tightturns", (char *) DEF_TIGHTTURNS, t_Bool},
	{(void *) & rotatepipes, (char *) "rotatepipes", (char *) "Rotatepipes", (char *) DEF_ROTATEPIPES, t_Bool}
};
static OptionStruct desc[] =
{
	{(char *) "-factory num", (char *) "how much extra equipment in pipes (0 for none)"},
	{(char *) "-/+fisheye", (char *) "turn on/off zoomed-in view of pipes"},
	{(char *) "-/+tightturns", (char *) "turn on/off tight turns"},
	{(char *) "-/+rotatepipes", (char *) "turn on/off pipe system rotation per screenful"}
};

ModeSpecOpt pipes_opts =
{sizeof opts / sizeof opts[0], opts, sizeof vars / sizeof vars[0], vars, desc};

#ifdef USE_MODULES
ModStruct   pipes_description =
{"pipes", "init_pipes", "draw_pipes", "release_pipes",
#if defined( MESA ) && defined( SLOW )
 "draw_pipes",
#else
 "change_pipes",
#endif
 "change_pipes", (char *) NULL, &pipes_opts,
 1000, 2, 5, 500, 4, 1.0, "",
 "Shows a selfbuilding pipe system", 0, NULL};

#endif

#define Scale4Window               0.1
#define Scale4Iconic               0.07

#define one_third                  0.3333333333333333333

#define dirNone -1
#define dirUP 0
#define dirDOWN 1
#define dirLEFT 2
#define dirRIGHT 3
#define dirNEAR 4
#define dirFAR 5

#define HCELLS 33
#define VCELLS 25
#define DEFINEDCOLORS 7
#define elbowradius 0.5

/*************************************************************************/

typedef struct {
#if defined( MESA ) && defined( SLOW )
	int         flip;
#endif
	GLint       WindH, WindW;
	int         Cells[HCELLS][VCELLS][HCELLS];
	int         usedcolors[DEFINEDCOLORS];
	int         directions[6];
	int         ndirections;
	int         nowdir, olddir;
	int         system_number;
	int         counter;
	int         PX, PY, PZ;
	int         number_of_systems;
	int         system_type;
	int         system_length;
	int         turncounter;
	int         factory;
	Window      window;
	float      *system_color;
	GLfloat     initial_rotation;
	GLuint      valve, bolts, betweenbolts, elbowbolts, elbowcoins;
	GLuint      guagehead, guageface, guagedial, guageconnector;
	GLXContext *glx_context;
} pipesstruct;

extern struct lwo LWO_BigValve, LWO_PipeBetweenBolts, LWO_Bolts3D;
extern struct lwo LWO_GuageHead, LWO_GuageFace, LWO_GuageDial, LWO_GuageConnector;
extern struct lwo LWO_ElbowBolts, LWO_ElbowCoins;

static float front_shininess[] =
{60.0};
static float front_specular[] =
{0.7, 0.7, 0.7, 1.0};
static float ambient0[] =
{0.4, 0.4, 0.4, 1.0};
static float diffuse0[] =
{1.0, 1.0, 1.0, 1.0};
static float ambient1[] =
{0.2, 0.2, 0.2, 1.0};
static float diffuse1[] =
{0.5, 0.5, 0.5, 1.0};
static float position0[] =
{1.0, 1.0, 1.0, 0.0};
static float position1[] =
{-1.0, -1.0, 1.0, 0.0};
static float lmodel_ambient[] =
{0.5, 0.5, 0.5, 1.0};
static float lmodel_twoside[] =
{GL_TRUE};

static float MaterialRed[] =
{0.7, 0.0, 0.0, 1.0};
static float MaterialGreen[] =
{0.1, 0.5, 0.2, 1.0};
static float MaterialBlue[] =
{0.0, 0.0, 0.7, 1.0};
static float MaterialCyan[] =
{0.2, 0.5, 0.7, 1.0};
static float MaterialYellow[] =
{0.7, 0.7, 0.0, 1.0};
static float MaterialMagenta[] =
{0.6, 0.2, 0.5, 1.0};
static float MaterialWhite[] =
{0.7, 0.7, 0.7, 1.0};
static float MaterialGray[] =
{0.2, 0.2, 0.2, 1.0};

static pipesstruct *pipes = (pipesstruct *) NULL;


static void
MakeTube(int direction)
{
	float       an;
	float       SINan_3, COSan_3;

	/*dirUP    = 00000000 */
	/*dirDOWN  = 00000001 */
	/*dirLEFT  = 00000010 */
	/*dirRIGHT = 00000011 */
	/*dirNEAR  = 00000100 */
	/*dirFAR   = 00000101 */

	if (!(direction & 4)) {
		glRotatef(90.0, (direction & 2) ? 0.0 : 1.0,
			  (direction & 2) ? 1.0 : 0.0, 0.0);
	}
	glBegin(GL_QUAD_STRIP);
	for (an = 0.0; an <= 2.0 * M_PI; an += M_PI / 12.0) {
		glNormal3f((COSan_3 = cos(an) / 3.0), (SINan_3 = sin(an) / 3.0), 0.0);
		glVertex3f(COSan_3, SINan_3, one_third);
		glVertex3f(COSan_3, SINan_3, -one_third);
	}
	glEnd();
}

static Bool
mySphere(float radius)
{
	GLUquadricObj *quadObj;

	if ((quadObj = gluNewQuadric()) == 0)
		return False;
	gluQuadricDrawStyle(quadObj, (GLenum) GLU_FILL);
	gluSphere(quadObj, radius, 16, 16);
	gluDeleteQuadric(quadObj);
	return True;
}

static void
myElbow(ModeInfo * mi, int bolted)
{
#define nsides 25
#define rings 25
#define r one_third
#define R one_third

	pipesstruct *pp = &pipes[MI_SCREEN(mi)];

	int         i, j;
	GLfloat     p0[3], p1[3], p2[3], p3[3];
	GLfloat     n0[3], n1[3], n2[3], n3[3];
	GLfloat     COSphi, COSphi1, COStheta, COStheta1;
	GLfloat     _SINtheta, _SINtheta1;

	for (i = 0; i <= rings / 4; i++) {
		GLfloat     theta, theta1;

		theta = (GLfloat) i *2.0 * M_PI / rings;

		theta1 = (GLfloat) (i + 1) * 2.0 * M_PI / rings;
		for (j = 0; j < nsides; j++) {
			GLfloat     phi, phi1;

			phi = (GLfloat) j *2.0 * M_PI / nsides;

			phi1 = (GLfloat) (j + 1) * 2.0 * M_PI / nsides;

			p0[0] = (COStheta = cos(theta)) * (R + r * (COSphi = cos(phi)));
			p0[1] = (_SINtheta = -sin(theta)) * (R + r * COSphi);

			p1[0] = (COStheta1 = cos(theta1)) * (R + r * COSphi);
			p1[1] = (_SINtheta1 = -sin(theta1)) * (R + r * COSphi);

			p2[0] = COStheta1 * (R + r * (COSphi1 = cos(phi1)));
			p2[1] = _SINtheta1 * (R + r * COSphi1);

			p3[0] = COStheta * (R + r * COSphi1);
			p3[1] = _SINtheta * (R + r * COSphi1);

			n0[0] = COStheta * COSphi;
			n0[1] = _SINtheta * COSphi;

			n1[0] = COStheta1 * COSphi;
			n1[1] = _SINtheta1 * COSphi;

			n2[0] = COStheta1 * COSphi1;
			n2[1] = _SINtheta1 * COSphi1;

			n3[0] = COStheta * COSphi1;
			n3[1] = _SINtheta * COSphi1;

			p0[2] = p1[2] = r * (n0[2] = n1[2] = sin(phi));
			p2[2] = p3[2] = r * (n2[2] = n3[2] = sin(phi1));

			glBegin(GL_QUADS);
			glNormal3fv(n3);
			glVertex3fv(p3);
			glNormal3fv(n2);
			glVertex3fv(p2);
			glNormal3fv(n1);
			glVertex3fv(p1);
			glNormal3fv(n0);
			glVertex3fv(p0);
			glEnd();
		}
	}

	if (pp->factory > 0 && bolted) {
		/* Bolt the elbow onto the pipe system */
		glFrontFace(GL_CW);
		glPushMatrix();
		glRotatef(90.0, 0.0, 0.0, -1.0);
		glRotatef(90.0, 0.0, 1.0, 0.0);
		glTranslatef(0.0, one_third, one_third);
		glCallList(pp->elbowcoins);
		glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, MaterialGray);
		glCallList(pp->elbowbolts);
		glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, pp->system_color);
		glPopMatrix();
		glFrontFace(GL_CCW);
	}
#undef r
#undef R
#undef nsides
#undef rings
}

static void
FindNeighbors(ModeInfo * mi)
{
	pipesstruct *pp = &pipes[MI_SCREEN(mi)];

	pp->ndirections = 0;
	pp->directions[dirUP] = (!pp->Cells[pp->PX][pp->PY + 1][pp->PZ]) ? 1 : 0;
	pp->ndirections += pp->directions[dirUP];
	pp->directions[dirDOWN] = (!pp->Cells[pp->PX][pp->PY - 1][pp->PZ]) ? 1 : 0;
	pp->ndirections += pp->directions[dirDOWN];
	pp->directions[dirLEFT] = (!pp->Cells[pp->PX - 1][pp->PY][pp->PZ]) ? 1 : 0;
	pp->ndirections += pp->directions[dirLEFT];
	pp->directions[dirRIGHT] = (!pp->Cells[pp->PX + 1][pp->PY][pp->PZ]) ? 1 : 0;
	pp->ndirections += pp->directions[dirRIGHT];
	pp->directions[dirFAR] = (!pp->Cells[pp->PX][pp->PY][pp->PZ - 1]) ? 1 : 0;
	pp->ndirections += pp->directions[dirFAR];
	pp->directions[dirNEAR] = (!pp->Cells[pp->PX][pp->PY][pp->PZ + 1]) ? 1 : 0;
	pp->ndirections += pp->directions[dirNEAR];
}

static int
SelectNeighbor(ModeInfo * mi)
{
	pipesstruct *pp = &pipes[MI_SCREEN(mi)];
	int         dirlist[6];
	int         i, j;

	for (i = 0, j = 0; i < 6; i++) {
		if (pp->directions[i]) {
			dirlist[j] = i;
			j++;
		}
	}

	return dirlist[NRAND(pp->ndirections)];
}

static void
MakeValve(ModeInfo * mi, int newdir)
{
	pipesstruct *pp = &pipes[MI_SCREEN(mi)];

	/* There is a glPopMatrix() right after this subroutine returns. */
	switch (newdir) {
		case dirUP:
		case dirDOWN:
			glRotatef(90.0, 1.0, 0.0, 0.0);
			glRotatef(NRAND(3) * 90.0, 0.0, 0.0, 1.0);
			break;
		case dirLEFT:
		case dirRIGHT:
			glRotatef(90.0, 0.0, -1.0, 0.0);
			glRotatef((NRAND(3) * 90.0) - 90.0, 0.0, 0.0, 1.0);
			break;
		case dirNEAR:
		case dirFAR:
			glRotatef(NRAND(4) * 90.0, 0.0, 0.0, 1.0);
			break;
	}
	glFrontFace(GL_CW);
	glCallList(pp->betweenbolts);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, MaterialGray);
	glCallList(pp->bolts);
	if (!MI_IS_MONO(mi)) {
		if (pp->system_color == MaterialRed) {
			glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, NRAND(2) ? MaterialYellow : MaterialBlue);
		} else if (pp->system_color == MaterialBlue) {
			glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, NRAND(2) ? MaterialRed : MaterialYellow);
		} else if (pp->system_color == MaterialYellow) {
			glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, NRAND(2) ? MaterialBlue : MaterialRed);
		} else {
			switch ((NRAND(3))) {
				case 0:
					glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, MaterialRed);
					break;
				case 1:
					glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, MaterialBlue);
					break;
				case 2:
					glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, MaterialYellow);
			}
		}
	}
	glRotatef((GLfloat) (NRAND(90)), 1.0, 0.0, 0.0);
	glCallList(pp->valve);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, pp->system_color);
	glFrontFace(GL_CCW);
}

static int
MakeGuage(ModeInfo * mi, int newdir)
{
	pipesstruct *pp = &pipes[MI_SCREEN(mi)];

	/* Can't have a guage on a vertical pipe. */
	if ((newdir == dirUP) || (newdir == dirDOWN))
		return (0);

	/* Is there space above this pipe for a guage? */
	if (!pp->directions[dirUP])
		return (0);

	/* Yes!  Mark the space as used. */
	pp->Cells[pp->PX][pp->PY + 1][pp->PZ] = 1;

	glFrontFace(GL_CW);
	glPushMatrix();
	if ((newdir == dirLEFT) || (newdir == dirRIGHT))
		glRotatef(90.0, 0.0, 1.0, 0.0);
	glCallList(pp->betweenbolts);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, MaterialGray);
	glCallList(pp->bolts);
	glPopMatrix();

	glCallList(pp->guageconnector);
	glPushMatrix();
	glTranslatef(0.0, 1.33333, 0.0);
	/* Do not change the above to 1 + ONE_THIRD, because */
	/* the object really is centered on 1.3333300000. */
	glRotatef(NRAND(270) + 45.0, 0.0, 0.0, -1.0);
	/* Random rotation for the dial.  I love it. */
	glCallList(pp->guagedial);
	glPopMatrix();

	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, pp->system_color);
	glCallList(pp->guagehead);

	/* GuageFace is drawn last, in case of low-res depth buffers. */
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, MaterialWhite);
	glCallList(pp->guageface);

	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, pp->system_color);
	glFrontFace(GL_CCW);

	return (1);
}

static void
MakeShape(ModeInfo * mi, int newdir)
{
	switch (NRAND(2)) {
		case 1:
			if (!MakeGuage(mi, newdir))
				MakeTube(newdir);
			break;
		default:
			MakeValve(mi, newdir);
			break;
	}
}

static void
reshape(ModeInfo * mi, int width, int height)
{
	pipesstruct *pp = &pipes[MI_SCREEN(mi)];

	glViewport(0, 0, pp->WindW = (GLint) width, pp->WindH = (GLint) height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	/*glFrustum(-1.0, 1.0, -1.0, 1.0, 5.0, 15.0); */
	gluPerspective(65.0, (GLfloat) width / (GLfloat) height, 0.1, 20.0);
	glMatrixMode(GL_MODELVIEW);
}

static void
pinit(ModeInfo * mi, int zera)
{
	pipesstruct *pp = &pipes[MI_SCREEN(mi)];
	int         X, Y, Z;

	glClearDepth(1.0);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glColor3f(1.0, 1.0, 1.0);

	glLightfv(GL_LIGHT0, GL_AMBIENT, ambient0);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse0);
	glLightfv(GL_LIGHT0, GL_POSITION, position0);
	glLightfv(GL_LIGHT1, GL_AMBIENT, ambient1);
	glLightfv(GL_LIGHT1, GL_DIFFUSE, diffuse1);
	glLightfv(GL_LIGHT1, GL_POSITION, position1);
	glLightModelfv(GL_LIGHT_MODEL_AMBIENT, lmodel_ambient);
	glLightModelfv(GL_LIGHT_MODEL_TWO_SIDE, lmodel_twoside);
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);
	glEnable(GL_LIGHT1);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_NORMALIZE);
	glEnable(GL_CULL_FACE);

	glShadeModel(GL_SMOOTH);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, front_shininess);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, front_specular);

	if (zera) {
		pp->system_number = 1;
		glDrawBuffer(GL_FRONT_AND_BACK);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		(void) memset(pp->Cells, 0, sizeof (pp->Cells));
		for (X = 0; X < HCELLS; X++) {
			for (Y = 0; Y < VCELLS; Y++) {
				pp->Cells[X][Y][0] = 1;
				pp->Cells[X][Y][HCELLS - 1] = 1;
				pp->Cells[0][Y][X] = 1;
				pp->Cells[HCELLS - 1][Y][X] = 1;
			}
		}
		for (X = 0; X < HCELLS; X++) {
			for (Z = 0; Z < HCELLS; Z++) {
				pp->Cells[X][0][Z] = 1;
				pp->Cells[X][VCELLS - 1][Z] = 1;
			}
		}
		(void) memset(pp->usedcolors, 0, sizeof (pp->usedcolors));
		if ((pp->initial_rotation += 10.0) > 45.0) {
			pp->initial_rotation -= 90.0;
		}
	}
	pp->counter = 0;
	pp->turncounter = 0;

	if (!MI_IS_MONO(mi)) {
		int         collist[DEFINEDCOLORS];
		int         i, j, lower = 1000;

		/* Avoid repeating colors on the same screen unless necessary */
		for (i = 0; i < DEFINEDCOLORS; i++) {
			if (lower > pp->usedcolors[i])
				lower = pp->usedcolors[i];
		}
		for (i = 0, j = 0; i < DEFINEDCOLORS; i++) {
			if (pp->usedcolors[i] == lower) {
				collist[j] = i;
				j++;
			}
		}
		i = collist[NRAND(j)];
		pp->usedcolors[i]++;
		switch (i) {
			case 0:
				pp->system_color = MaterialRed;
				break;
			case 1:
				pp->system_color = MaterialGreen;
				break;
			case 2:
				pp->system_color = MaterialBlue;
				break;
			case 3:
				pp->system_color = MaterialCyan;
				break;
			case 4:
				pp->system_color = MaterialYellow;
				break;
			case 5:
				pp->system_color = MaterialMagenta;
				break;
			case 6:
				pp->system_color = MaterialWhite;
				break;
		}
	} else {
		pp->system_color = MaterialGray;
	}

	do {
		pp->PX = NRAND((HCELLS - 1)) + 1;
		pp->PY = NRAND((VCELLS - 1)) + 1;
		pp->PZ = NRAND((HCELLS - 1)) + 1;
	} while (pp->Cells[pp->PX][pp->PY][pp->PZ] ||
		 (pp->Cells[pp->PX + 1][pp->PY][pp->PZ] && pp->Cells[pp->PX - 1][pp->PY][pp->PZ] &&
		  pp->Cells[pp->PX][pp->PY + 1][pp->PZ] && pp->Cells[pp->PX][pp->PY - 1][pp->PZ] &&
		  pp->Cells[pp->PX][pp->PY][pp->PZ + 1] && pp->Cells[pp->PX][pp->PY][pp->PZ - 1]));
	pp->Cells[pp->PX][pp->PY][pp->PZ] = 1;
	pp->olddir = dirNone;

	FindNeighbors(mi);

	pp->nowdir = SelectNeighbor(mi);
}

static void
free_factory(Display *display, pipesstruct *pp)
{
	if (pp->glx_context) {
		/* Display lists MUST be freed while their glXContext is current. */
		glXMakeCurrent(display, pp->window, *(pp->glx_context));
		if (pp->valve != 0) {
			glDeleteLists(pp->valve, 1);
			pp->valve = 0;
		}
		if (pp->bolts != 0) {
			glDeleteLists(pp->bolts, 1);
			pp->bolts = 0;
		}
		if (pp->betweenbolts != 0) {
			glDeleteLists(pp->betweenbolts, 1);
			pp->betweenbolts = 0;
		}
		if (pp->elbowbolts != 0) {
			glDeleteLists(pp->elbowbolts, 1);
			pp->elbowbolts = 0;
		}
		if (pp->elbowcoins != 0) {
			glDeleteLists(pp->elbowcoins, 1);
			pp->elbowcoins = 0;
		}
		if (pp->guagehead != 0) {
			glDeleteLists(pp->guagehead, 1);
			pp->guagehead = 0;
		}
		if (pp->guageface != 0) {
			glDeleteLists(pp->guageface, 1);
			pp->guageface = 0;
		}
		if (pp->guagedial != 0) {
			glDeleteLists(pp->guagedial, 1);
			pp->guagedial = 0;
		}
		if (pp->guageconnector != 0) {
			glDeleteLists(pp->guageconnector, 1);
			pp->guageconnector = 0;
		}
	}
}

void
release_pipes(ModeInfo * mi)
{
	if (pipes != NULL) {
		int         screen;

		for (screen = 0; screen < MI_NUM_SCREENS(mi); screen++)
			free_factory(MI_DISPLAY(mi), &pipes[screen]);
		free(pipes);
		pipes = (pipesstruct *) NULL;
	}
	FreeAllGL(mi);
}

void
init_pipes(ModeInfo * mi)
{
	pipesstruct *pp;

	if (pipes == NULL) {
		if ((pipes = (pipesstruct *) calloc(MI_NUM_SCREENS(mi),
					      sizeof (pipesstruct))) == NULL)
			return;
	}
	pp = &pipes[MI_SCREEN(mi)];

	pp->window = MI_WINDOW(mi);
	pp->factory = factory;
	if ((pp->glx_context = init_GL(mi)) != NULL) {

		reshape(mi, MI_WIDTH(mi), MI_HEIGHT(mi));
		pp->initial_rotation = -10.0;
		pinit(mi, 1);

		if (pp->factory > 0) {
			if (((pp->valve = BuildLWO(MI_IS_WIREFRAME(mi),
				&LWO_BigValve)) == 0) ||
			    ((pp->bolts = BuildLWO(MI_IS_WIREFRAME(mi),
				&LWO_Bolts3D)) == 0) ||
			    ((pp->betweenbolts = BuildLWO(MI_IS_WIREFRAME(mi),
				&LWO_PipeBetweenBolts)) == 0) ||
			    ((pp->elbowbolts = BuildLWO(MI_IS_WIREFRAME(mi),
				&LWO_ElbowBolts)) == 0) ||
			    ((pp->elbowcoins = BuildLWO(MI_IS_WIREFRAME(mi),
				&LWO_ElbowCoins)) == 0) ||
			    ((pp->guagehead = BuildLWO(MI_IS_WIREFRAME(mi),
				&LWO_GuageHead)) == 0) ||
			    ((pp->guageface = BuildLWO(MI_IS_WIREFRAME(mi),
				&LWO_GuageFace)) == 0) ||
			    ((pp->guagedial = BuildLWO(MI_IS_WIREFRAME(mi),
				&LWO_GuageDial)) == 0) ||
			    ((pp->guageconnector = BuildLWO(MI_IS_WIREFRAME(mi),
				&LWO_GuageConnector)) == 0)) {
				free_factory(MI_DISPLAY(mi), pp);
				pp->factory = 0;
			}
		}
		/* else they are all 0, thanks to calloc(). */

		if (MI_COUNT(mi) < 1 || MI_COUNT(mi) > NofSysTypes + 1) {
			pp->system_type = NRAND(NofSysTypes) + 1;
		} else {
			pp->system_type = MI_COUNT(mi);
		}

		if (MI_CYCLES(mi) > 0 && MI_CYCLES(mi) < 11) {
			pp->number_of_systems = MI_CYCLES(mi);
		} else {
			pp->number_of_systems = 5;
		}

		if (MI_SIZE(mi) < 10) {
			pp->system_length = 10;
		} else if (MI_SIZE(mi) > 1000) {
			pp->system_length = 1000;
		} else {
			pp->system_length = MI_SIZE(mi);
		}
	} else {
		MI_CLEARWINDOW(mi);
	}
}

void
draw_pipes(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	int         newdir;
	int         OPX, OPY, OPZ;
	pipesstruct *pp;

	if (pipes == NULL)
		return;
	pp = &pipes[MI_SCREEN(mi)];

	MI_IS_DRAWN(mi) = True;
	if (!pp->glx_context)
		return;

	glXMakeCurrent(display, window, *(pp->glx_context));

#if defined( MESA ) && defined( SLOW )
	glDrawBuffer(GL_BACK);
#else
	glDrawBuffer(GL_FRONT);
#endif
	glPushMatrix();

	glTranslatef(0.0, 0.0, fisheye ? -3.8 : -4.8);
	if (rotatepipes)
		glRotatef(pp->initial_rotation, 0.0, 1.0, 0.0);

	if (!MI_IS_ICONIC(mi)) {
		/* Width/height ratio handled by gluPerspective() now. */
		glScalef(Scale4Window, Scale4Window, Scale4Window);
	} else {
		glScalef(Scale4Iconic, Scale4Iconic, Scale4Iconic);
	}

	FindNeighbors(mi);

	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, pp->system_color);

	/* If it's the begining of a system, draw a sphere */
	if (pp->olddir == dirNone) {
		glPushMatrix();
		glTranslatef((pp->PX - 16) / 3.0 * 4.0, (pp->PY - 12) / 3.0 * 4.0, (pp->PZ - 16) / 3.0 * 4.0);
		if (!mySphere(0.6)) {
			release_pipes(mi);
			return;
		}
		glPopMatrix();
	}
	/* Check for stop conditions */
	if (pp->ndirections == 0 || pp->counter > pp->system_length) {
		glPushMatrix();
		glTranslatef((pp->PX - 16) / 3.0 * 4.0, (pp->PY - 12) / 3.0 * 4.0, (pp->PZ - 16) / 3.0 * 4.0);
		/* Finish the system with another sphere */
		if (!mySphere(0.6)) {
			release_pipes(mi);
			return;
		}
#if defined( MESA ) && defined( SLOW )
		glXSwapBuffers(display, window);
#endif
		glPopMatrix();

		/* If the maximum number of system was drawn, restart (clearing the screen), */
		/* else start a new system. */
		if (++pp->system_number > pp->number_of_systems) {
			(void) sleep(1);
			pinit(mi, 1);
		} else {
			pinit(mi, 0);
		}

		glPopMatrix();
		return;
	}
	pp->counter++;
	pp->turncounter++;

	/* Do will the direction change? if so, determine the new one */
	newdir = pp->nowdir;
	if (!pp->directions[newdir]) {	/* cannot proceed in the current direction */
		newdir = SelectNeighbor(mi);
	} else {
		if (tightturns) {
			/* random change (20% chance) */
			if ((pp->counter > 1) && (NRAND(100) < 20)) {
				newdir = SelectNeighbor(mi);
			}
		} else {
			/* Chance to turn increases after each length of pipe drawn */
			if ((pp->counter > 1) && NRAND(50) < NRAND(pp->turncounter + 1)) {
				newdir = SelectNeighbor(mi);
				pp->turncounter = 0;
			}
		}
	}

	/* Has the direction changed? */
	if (newdir == pp->nowdir) {
		/* If not, draw the cell's center pipe */
		glPushMatrix();
		glTranslatef((pp->PX - 16) / 3.0 * 4.0, (pp->PY - 12) / 3.0 * 4.0, (pp->PZ - 16) / 3.0 * 4.0);
		/* Chance of factory shape here, if enabled. */
		if ((pp->counter > 1) && (NRAND(100) < pp->factory)) {
			MakeShape(mi, newdir);
		} else {
			MakeTube(newdir);
		}
		glPopMatrix();
	} else {
		/* If so, draw the cell's center elbow/sphere */
		int         sysT = pp->system_type;

		if (sysT == NofSysTypes + 1) {
			sysT = ((pp->system_number - 1) % NofSysTypes) + 1;
		}
		glPushMatrix();

		switch (sysT) {
			case 1:
				glTranslatef((pp->PX - 16) / 3.0 * 4.0, (pp->PY - 12) / 3.0 * 4.0, (pp->PZ - 16) / 3.0 * 4.0);
				if (!mySphere(elbowradius)) {
					release_pipes(mi);
					return;
				}
				break;
			case 2:
			case 3:
				switch (pp->nowdir) {
					case dirUP:
						switch (newdir) {
							case dirLEFT:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 - (one_third), (pp->PY - 12) / 3.0 * 4.0 - (one_third), (pp->PZ - 16) / 3.0 * 4.0);
								glRotatef(180.0, 1.0, 0.0, 0.0);
								break;
							case dirRIGHT:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 + (one_third), (pp->PY - 12) / 3.0 * 4.0 - (one_third), (pp->PZ - 16) / 3.0 * 4.0);
								glRotatef(180.0, 1.0, 0.0, 0.0);
								glRotatef(180.0, 0.0, 1.0, 0.0);
								break;
							case dirFAR:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0, (pp->PY - 12) / 3.0 * 4.0 - (one_third), (pp->PZ - 16) / 3.0 * 4.0 - (one_third));
								glRotatef(90.0, 0.0, 1.0, 0.0);
								glRotatef(180.0, 0.0, 0.0, 1.0);
								break;
							case dirNEAR:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0, (pp->PY - 12) / 3.0 * 4.0 - (one_third), (pp->PZ - 16) / 3.0 * 4.0 + (one_third));
								glRotatef(90.0, 0.0, 1.0, 0.0);
								glRotatef(180.0, 1.0, 0.0, 0.0);
								break;
						}
						break;
					case dirDOWN:
						switch (newdir) {
							case dirLEFT:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 - (one_third), (pp->PY - 12) / 3.0 * 4.0 + (one_third), (pp->PZ - 16) / 3.0 * 4.0);
								break;
							case dirRIGHT:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 + (one_third), (pp->PY - 12) / 3.0 * 4.0 + (one_third), (pp->PZ - 16) / 3.0 * 4.0);
								glRotatef(180.0, 0.0, 1.0, 0.0);
								break;
							case dirFAR:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0, (pp->PY - 12) / 3.0 * 4.0 + (one_third), (pp->PZ - 16) / 3.0 * 4.0 - (one_third));
								glRotatef(270.0, 0.0, 1.0, 0.0);
								break;
							case dirNEAR:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0, (pp->PY - 12) / 3.0 * 4.0 + (one_third), (pp->PZ - 16) / 3.0 * 4.0 + (one_third));
								glRotatef(90.0, 0.0, 1.0, 0.0);
								break;
						}
						break;
					case dirLEFT:
						switch (newdir) {
							case dirUP:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 + (one_third), (pp->PY - 12) / 3.0 * 4.0 + (one_third), (pp->PZ - 16) / 3.0 * 4.0);
								glRotatef(180.0, 0.0, 1.0, 0.0);
								break;
							case dirDOWN:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 + (one_third), (pp->PY - 12) / 3.0 * 4.0 - (one_third), (pp->PZ - 16) / 3.0 * 4.0);
								glRotatef(180.0, 1.0, 0.0, 0.0);
								glRotatef(180.0, 0.0, 1.0, 0.0);
								break;
							case dirFAR:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 + (one_third), (pp->PY - 12) / 3.0 * 4.0, (pp->PZ - 16) / 3.0 * 4.0 - (one_third));
								glRotatef(270.0, 1.0, 0.0, 0.0);
								glRotatef(180.0, 0.0, 1.0, 0.0);
								break;
							case dirNEAR:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 + (one_third), (pp->PY - 12) / 3.0 * 4.0, (pp->PZ - 16) / 3.0 * 4.0 + (one_third));
								glRotatef(270.0, 1.0, 0.0, 0.0);
								glRotatef(180.0, 0.0, 0.0, 1.0);
								break;
						}
						break;
					case dirRIGHT:
						switch (newdir) {
							case dirUP:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 - (one_third), (pp->PY - 12) / 3.0 * 4.0 + (one_third), (pp->PZ - 16) / 3.0 * 4.0);
								break;
							case dirDOWN:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 - (one_third), (pp->PY - 12) / 3.0 * 4.0 - (one_third), (pp->PZ - 16) / 3.0 * 4.0);
								glRotatef(180.0, 1.0, 0.0, 0.0);
								break;
							case dirFAR:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 - (one_third), (pp->PY - 12) / 3.0 * 4.0, (pp->PZ - 16) / 3.0 * 4.0 - (one_third));
								glRotatef(270.0, 1.0, 0.0, 0.0);
								break;
							case dirNEAR:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 - (one_third), (pp->PY - 12) / 3.0 * 4.0, (pp->PZ - 16) / 3.0 * 4.0 + (one_third));
								glRotatef(90.0, 1.0, 0.0, 0.0);
								break;
						}
						break;
					case dirNEAR:
						switch (newdir) {
							case dirLEFT:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 - (one_third), (pp->PY - 12) / 3.0 * 4.0, (pp->PZ - 16) / 3.0 * 4.0 - (one_third));
								glRotatef(270.0, 1.0, 0.0, 0.0);
								break;
							case dirRIGHT:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 + (one_third), (pp->PY - 12) / 3.0 * 4.0, (pp->PZ - 16) / 3.0 * 4.0 - (one_third));
								glRotatef(270.0, 1.0, 0.0, 0.0);
								glRotatef(180.0, 0.0, 1.0, 0.0);
								break;
							case dirUP:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0, (pp->PY - 12) / 3.0 * 4.0 + (one_third), (pp->PZ - 16) / 3.0 * 4.0 - (one_third));
								glRotatef(270.0, 0.0, 1.0, 0.0);
								break;
							case dirDOWN:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0, (pp->PY - 12) / 3.0 * 4.0 - (one_third), (pp->PZ - 16) / 3.0 * 4.0 - (one_third));
								glRotatef(90.0, 0.0, 1.0, 0.0);
								glRotatef(180.0, 0.0, 0.0, 1.0);
								break;
						}
						break;
					case dirFAR:
						switch (newdir) {
							case dirUP:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0, (pp->PY - 12) / 3.0 * 4.0 + (one_third), (pp->PZ - 16) / 3.0 * 4.0 + (one_third));
								glRotatef(90.0, 0.0, 1.0, 0.0);
								break;
							case dirDOWN:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0, (pp->PY - 12) / 3.0 * 4.0 - (one_third), (pp->PZ - 16) / 3.0 * 4.0 + (one_third));
								glRotatef(90.0, 0.0, 1.0, 0.0);
								glRotatef(180.0, 1.0, 0.0, 0.0);
								break;
							case dirLEFT:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 - (one_third), (pp->PY - 12) / 3.0 * 4.0, (pp->PZ - 16) / 3.0 * 4.0 + (one_third));
								glRotatef(90.0, 1.0, 0.0, 0.0);
								break;
							case dirRIGHT:
								glTranslatef((pp->PX - 16) / 3.0 * 4.0 + (one_third), (pp->PY - 12) / 3.0 * 4.0, (pp->PZ - 16) / 3.0 * 4.0 + (one_third));
								glRotatef(270.0, 1.0, 0.0, 0.0);
								glRotatef(180.0, 0.0, 0.0, 1.0);
								break;
						}
						break;
				}
				myElbow(mi, (sysT == 2));
				break;
		}
		glPopMatrix();
	}

	OPX = pp->PX;
	OPY = pp->PY;
	OPZ = pp->PZ;
	pp->olddir = pp->nowdir;
	pp->nowdir = newdir;
	switch (pp->nowdir) {
		case dirUP:
			pp->PY++;
			break;
		case dirDOWN:
			pp->PY--;
			break;
		case dirLEFT:
			pp->PX--;
			break;
		case dirRIGHT:
			pp->PX++;
			break;
		case dirNEAR:
			pp->PZ++;
			break;
		case dirFAR:
			pp->PZ--;
			break;
	}
	pp->Cells[pp->PX][pp->PY][pp->PZ] = 1;

	/* Cells'face pipe */
	glTranslatef(((pp->PX + OPX) / 2.0 - 16) / 3.0 * 4.0, ((pp->PY + OPY) / 2.0 - 12) / 3.0 * 4.0, ((pp->PZ + OPZ) / 2.0 - 16) / 3.0 * 4.0);
	MakeTube(newdir);

	glPopMatrix();

	glFlush();

#if defined( MESA ) && defined( SLOW )
	pp->flip = !pp->flip;
	if (pp->flip)
		glXSwapBuffers(display, window);
#endif
}

void
change_pipes(ModeInfo * mi)
{
	pipesstruct *pp;

	if (pipes == NULL)
		return;
	pp = &pipes[MI_SCREEN(mi)];

	if (!pp->glx_context)
		return;

	glXMakeCurrent(MI_DISPLAY(mi), MI_WINDOW(mi), *(pp->glx_context));
	pinit(mi, 1);
}

#endif
