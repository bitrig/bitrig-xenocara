/* -*- Mode: C; tab-width: 4 -*- */
/* morph3d --- Shows 3D morphing objects */

#if !defined( lint ) && !defined( SABER )
static const char sccsid[] = "@(#)morph3d.c	5.01 2001/03/01 xlockmore";

#endif

#undef DEBUG_CULL_FACE

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
 * The original code for this mode was written by Marcelo Fernandes Vianna
 * (me...) and was inspired on a WindowsNT(R)'s screen saver (Flower Box).
 * It was written from scratch and it was not based on any other source code.
 *
 * Porting it to xlock (the final objective of this code since the moment I
 * decided to create it) was possible by comparing the original Mesa's gear
 * demo with it's ported version to xlock, so thanks for Danny Sung (look at
 * gear.c) for his indirect help.
 *
 * Thanks goes also to Brian Paul for making it possible and inexpensive
 * to use OpenGL at home.
 *
 * If you are interested in the original version of this program (not a xlock
 * mode, please refer to the Mesa package (ftp iris.ssec.wisc.edu on /pub/Mesa)
 *
 * Since I'm not a native English speaker, my apologies for any grammatical
 * mistakes.
 *
 * My e-mail address is
 * mfvianna@centroin.com.br
 *
 * Marcelo F. Vianna (Feb-13-1997)
 *
 * Revision History:
 * 05-Apr-2002: Removed all gllist uses (fix some bug with nvidia driver)
 * 01-Mar-2001: Added FPS stuff E.Lassauge <lassauge AT users.sourceforge.net>
 * 27-Jul-1997: Speed ups by Marcelo F. Vianna.
 * 08-May-1997: Speed ups by Marcelo F. Vianna.
 *
 */

/* Mathematics.txt doc
 Basics of morph3d Engine

This document is VERY incomplete, but tries to describe the mathematics used
in the program. At this moment it just describes how the polyhedra are 
generated. On further versions, this document will probably be improved.

Marcelo Fernandes Vianna 
- Feb-13-1997

POLYHEDRA GENERATION

For the purpose of this program it's not sufficient to know the polyhedra
vertexes coordinates. Since the morphing algorithm applies a nonlinear 
transformation over the surfaces (faces) of the polyhedron, each face has
to be divided into smaller ones. The morphing algorithm needs to transform 
each vertex of these smaller faces individually. It's a very time consuming
task.

In order to reduce calculation overload, and since all the macro faces of
the polyhedron are transformed by the same way, the generation is made by 
creating only one face of the polyhedron, morphing it and then rotating it
around the polyhedron center. 

What we need to know is the face radius of the polyhedron (the radius of 
the inscribed sphere) and the angle between the center of two adjacent 
faces using the center of the sphere as the angle's vertex.

The face radius of the regular polyhedra are known values which I decided
to not waste my time calculating. Following is a table of face radius for
the regular polyhedra with edge length = 1:

    TETRAHEDRON  : 1/(2*sqrt(2))/sqrt(3)
    CUBE         : 1/2
    OCTAHEDRON   : 1/sqrt(6)
    DODECAHEDRON : T^2 * sqrt((T+2)/5) / 2     -> where T=(sqrt(5)+1)/2
    ICOSAHEDRON  : (3*sqrt(3)+sqrt(15))/12

I've not found any reference about the mentioned angles, so I needed to
calculate them, not a trivial task until I figured out how :)
Curiously these angles are the same for the tetrahedron and octahedron.
A way to obtain this value is inscribing the tetrahedron inside the cube
by matching their vertexes. So you'll notice that the remaining unmatched
vertexes are in the same straight line starting in the cube/tetrahedron
center and crossing the center of each tetrahedron's face. At this point
it's easy to obtain the bigger angle of the isosceles triangle formed by
the center of the cube and two opposite vertexes on the same cube face.
The edges of this triangle have the following lengths: sqrt(2) for the base
and sqrt(3)/2 for the other two other edges. So the angle we want is:
     +-----------------------------------------------------------+
     | 2*ARCSIN(sqrt(2)/sqrt(3)) = 109.47122063449069174 degrees |
     +-----------------------------------------------------------+
For the cube this angle is obvious, but just for formality it can be
easily obtained because we also know it's isosceles edge lengths:
sqrt(2)/2 for the base and 1/2 for the other two edges. So the angle we 
want is:
     +-----------------------------------------------------------+
     | 2*ARCSIN((sqrt(2)/2)/1)   = 90.000000000000000000 degrees |
     +-----------------------------------------------------------+
For the octahedron we use the same idea used for the tetrahedron, but now
we inscribe the cube inside the octahedron so that all cubes's vertexes
matches exactly the center of each octahedron's face. It's now clear that
this angle is the same of the tetrahedron one:
     +-----------------------------------------------------------+
     | 2*ARCSIN(sqrt(2)/sqrt(3)) = 109.47122063449069174 degrees |
     +-----------------------------------------------------------+
For the dodecahedron it's a little bit harder because it's only relationship
with the cube is useless to us. So we need to solve the problem by another
way. The concept of Face radius also exists on 2D polygons with the name
Edge radius:
  Edge Radius For Pentagon (ERp)
  ERp = (1/2)/TAN(36 degrees) * VRp = 0.6881909602355867905
  (VRp is the pentagon's vertex radio).
  Face Radius For Dodecahedron
  FRd = T^2 * sqrt((T+2)/5) / 2 = 1.1135163644116068404
Why we need ERp? Well, ERp and FRd segments forms a 90 degrees angle, 
completing this triangle, the lesser angle is a half of the angle we are 
looking for, so this angle is:
     +-----------------------------------------------------------+
     | 2*ARCTAN(ERp/FRd)         = 63.434948822922009981 degrees |
     +-----------------------------------------------------------+
For the icosahedron we can use the same method used for dodecahedron (well
the method used for dodecahedron may be used for all regular polyhedra)
  Edge Radius For Triangle (this one is well known: 1/3 of the triangle height)
  ERt = sin(60)/3 = sqrt(3)/6 = 0.2886751345948128655
  Face Radius For Icosahedron
  FRi= (3*sqrt(3)+sqrt(15))/12 = 0.7557613140761707538
So the angle is:
     +-----------------------------------------------------------+
     | 2*ARCTAN(ERt/FRi)         = 41.810314895778596167 degrees |
     +-----------------------------------------------------------+

Update May-23-2005  [Maintainer wants to add a rhombic dodecahedron and the
    rhombic triacontahedron]

   In the below figure, you will find most of the entities I mention in
   the "Mathematics.txt" doc I provided years ago along with morph3d.
   I'm writing this new document because I foresee a pitfall you will
   have to deal to add non-regular polyhedra to the mode.  Please, read
   the Mathematics.txt document together with this e-mail for the sake of
   completeness.
   One important thing you need to elaborate before adding any solid to
   morph3d is how to divide each of its faces in smaller ones (triangles
   or squares).  This is the only way you will have inner vertexes  (as P
   in the example of the below figure) for each face that will be subject
   for the transformations in the mode.

    Figure/Diagram example (not here) shows a dodecahedron
       SC = Solid Center (Center of polyhedron)
       FC = Face Center (Center of a polygon) 
       P =  Arbitrary Point inside the face plan
       Alpha = Faces Angle (internal angle from center of polyhedron)

   Face Radius (see Mathematics.txt) = Distance between FC and SC
   (mentioned in Mathematics.txt
   Alpha = the angle between two faces using SC as the vertex.
   Both entities (Face Radius and alpha) are useful for regular
   polyhedra, but for non-regular ones you will have to make other
   calculation and probably need more than one macro (actually one for
   each different face shape and radius).  They are actually used to
   minimize calculation by drawing one face only and than replicate it.
   This won't be possible on non-regular polyhedron, at least not for all
   the faces, but only for the equivalent ones.
   The distortion effect is obtained by multiplying the P vector ( P-SC
   where SC is (0,0,0) ) by a factor that is proportional to the P-FC
   vector, so the face center simply is unchanged and the face edges are
   the most affected.
   The big pitfall here is that the distortion very near the edges are
   always equivalent for neighbor faces, while it may not be true for
   non-regular ones, so you will probably need to use an adjustment
   factor, otherwise, if the distances are different, you will se non
   continuous faces, actually a rip will occur.
   Thinking about this now, makes me think you could probably use a
   factor other than P-FC for the distortion intensity.  Considering that
   for some non-regular solids you still have all the face radius with
   the same value (it seems to be the case of some you mentioned to me)
   you can think of imaginarily inscribing a sphere inside the solid and
   using the distance between P and the sphere as the multiplyer.  This
   surely will avoid the rips, but won't work for a number of other
   solids.

Update May-24-2005

   Actually the edge lengths are not of much importance, but the face
   radius is.  You'll understand it better when you read Mathematics.txt
   together with the previous e-mail.

   About my comment on using the distance between an inscribed sphere and
   the point P instead of the face center and point P, this actually shall
   work for all solids AFAICS.

   Dividing the faces into triangles is not a big problem, actually the
   pentagon really was the only difficult one (I made it dividing it into 5
   triangles at first, and then dividing each triangle into smaller ones).
   For cube, as far as I remember, I divided the faces into smaller
   squares, not triangles, although triangles would also be possible.

*/

#ifdef VMS
/*-
 * due to a Bug/feature in VMS X11/Intrinsic.h has to be placed before xlock.
 * otherwise caddr_t is not defined correctly
 */

#include <X11/Intrinsic.h>
#endif

#ifdef STANDALONE
#define MODE_moebius
#define PROGCLASS "Morph3d"
#define HACK_INIT init_morph3d
#define HACK_DRAW draw_morph3d
#define morph3d_opts xlockmore_opts
#define DEFAULTS "*delay: 1000 \n" \
 "*count: 0 \n"
#include "xlockmore.h"		/* from the xscreensaver distribution */
#else /* !STANDALONE */
#include "xlock.h"		/* from the xlockmore distribution */
#include "vis.h"
#endif /* !STANDALONE */

#ifdef MODE_moebius

ModeSpecOpt morph3d_opts =
{0, (XrmOptionDescRec *) NULL, 0, (argtype *) NULL, (OptionStruct *) NULL};

#ifdef USE_MODULES
ModStruct   morph3d_description =
{"morph3d", "init_morph3d", "draw_morph3d", "release_morph3d",
 "draw_morph3d", "change_morph3d", (char *) NULL, &morph3d_opts,
 1000, 0, 1, 1, 4, 1.0, "",
 "Shows GL morphing polyhedra", 0, NULL};

#endif

#define Scale4Window               0.3
#define Scale4Iconic               1.0

#define VectMul(X1,Y1,Z1,X2,Y2,Z2) (Y1)*(Z2)-(Z1)*(Y2),(Z1)*(X2)-(X1)*(Z2),(X1)*(Y2)-(Y1)*(X2)
#define sqr(A)                     ((A)*(A))

/* Increasing this values produces better image quality, the price is speed. */
#define tetradivisions             23
#define cubedivisions              20
#define octadivisions              21
#define dodecadivisions            10
#define icodivisions               15

#define tetraangle                 109.47122063449069174
#define cubeangle                  90.000000000000000000
#define octaangle                  109.47122063449069174
#define dodecaangle                63.434948822922009981
#define icoangle                   41.810314895778596167

#ifndef Pi
#define Pi                         M_PI
#endif
#define SQRT2                      1.4142135623730951455
#define SQRT3                      1.7320508075688771932
#define SQRT5                      2.2360679774997898051
#define SQRT6                      2.4494897427831778813
#define SQRT15                     3.8729833462074170214
#define cossec36_2                 0.8506508083520399322
#define cos72                      0.3090169943749474241
#define sin72                      0.9510565162951535721
#define cos36                      0.8090169943749474241
#define sin36                      0.5877852522924731292

/*************************************************************************/

typedef struct {
	GLint       WindH, WindW;
	GLfloat     step;
	GLfloat     seno;
	int         object;
	int         edgedivisions;
	int         VisibleSpikes;
	void        (*draw_object) (ModeInfo * mi);
	float       Magnitude;
	float      *MaterialColor[20];
	GLXContext *glx_context;
} morph3dstruct;

static float front_shininess[] =
{60.0};
static float front_specular[] =
{0.7, 0.7, 0.7, 1.0};
static float ambient[] =
{0.0, 0.0, 0.0, 1.0};
static float diffuse[] =
{1.0, 1.0, 1.0, 1.0};
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
{0.5, 0.5, 0.5, 1.0};

static morph3dstruct *morph3d = (morph3dstruct *) NULL;

#define TRIANGLE(Edge, Amp, Divisions, Z, VS)                                                                    \
{                                                                                                                \
  GLfloat   Xf,Yf,Xa,Yb=0.0,Xf2=0.0,Yf2=0.0,Yf_2=0.0,Yb2,Yb_2;                                                   \
  GLfloat   Factor=0.0,Factor1,Factor2;                                                                          \
  GLfloat   VertX,VertY,VertZ,NeiAX,NeiAY,NeiAZ,NeiBX,NeiBY,NeiBZ;                                               \
  GLfloat   Ax,Ay;                                                                                               \
  int       Ri,Ti;                                                                                               \
  GLfloat   Vr=(Edge)*SQRT3/3;                                                                                   \
  GLfloat   AmpVr2=(Amp)/sqr(Vr);                                                                                \
  GLfloat   Zf=(Edge)*(Z);                                                                                       \
                                                                                                                 \
  Ax=(Edge)*(+0.5/(Divisions)), Ay=(Edge)*(-SQRT3/(2*Divisions));                                                \
                                                                                                                 \
  Yf=Vr+Ay; Yb=Yf+0.001;                                                                                         \
  for (Ri=1; Ri<=(Divisions); Ri++) {                                                                            \
    glBegin(GL_TRIANGLE_STRIP);                                                                                  \
    Xf=(float)Ri*Ax; Xa=Xf+0.001;                                                                                \
    Yf2=sqr(Yf); Yf_2=sqr(Yf-Ay);                                                                                \
    Yb2=sqr(Yb); Yb_2=sqr(Yb-Ay);                                                                                \
    for (Ti=0; Ti<Ri; Ti++) {                                                                                    \
      Factor=1-(((Xf2=sqr(Xf))+Yf2)*AmpVr2);                                                                     \
      Factor1=1-((sqr(Xa)+Yf2)*AmpVr2);                                                                          \
      Factor2=1-((Xf2+Yb2)*AmpVr2);                                                                              \
      VertX=Factor*Xf;        VertY=Factor*Yf;        VertZ=Factor*Zf;                                           \
      NeiAX=Factor1*Xa-VertX; NeiAY=Factor1*Yf-VertY; NeiAZ=Factor1*Zf-VertZ;                                    \
      NeiBX=Factor2*Xf-VertX; NeiBY=Factor2*Yb-VertY; NeiBZ=Factor2*Zf-VertZ;                                    \
      glNormal3f(VectMul(NeiAX, NeiAY, NeiAZ, NeiBX, NeiBY, NeiBZ));                                             \
      glVertex3f(VertX, VertY, VertZ);                                                                           \
                                                                                                                 \
      Xf-=Ax; Yf-=Ay; Xa-=Ax; Yb-=Ay;                                                                            \
                                                                                                                 \
      Factor=1-(((Xf2=sqr(Xf))+Yf_2)*AmpVr2);                                                                    \
      Factor1=1-((sqr(Xa)+Yf_2)*AmpVr2);                                                                         \
      Factor2=1-((Xf2+Yb_2)*AmpVr2);                                                                             \
      VertX=Factor*Xf;        VertY=Factor*Yf;        VertZ=Factor*Zf;                                           \
      NeiAX=Factor1*Xa-VertX; NeiAY=Factor1*Yf-VertY; NeiAZ=Factor1*Zf-VertZ;                                    \
      NeiBX=Factor2*Xf-VertX; NeiBY=Factor2*Yb-VertY; NeiBZ=Factor2*Zf-VertZ;                                    \
      glNormal3f(VectMul(NeiAX, NeiAY, NeiAZ, NeiBX, NeiBY, NeiBZ));                                             \
      glVertex3f(VertX, VertY, VertZ);                                                                           \
                                                                                                                 \
      Xf-=Ax; Yf+=Ay; Xa-=Ax; Yb+=Ay;                                                                            \
    }                                                                                                            \
    Factor=1-(((Xf2=sqr(Xf))+(Yf2=sqr(Yf)))*AmpVr2);                                                             \
    Factor1=1-((sqr(Xa)+Yf2)*AmpVr2);                                                                            \
    Factor2=1-((Xf2+sqr(Yb))*AmpVr2);                                                                            \
    VertX=Factor*Xf;        VertY=Factor*Yf;        VertZ=Factor*Zf;                                             \
    NeiAX=Factor1*Xa-VertX; NeiAY=Factor1*Yf-VertY; NeiAZ=Factor1*Zf-VertZ;                                      \
    NeiBX=Factor2*Xf-VertX; NeiBY=Factor2*Yb-VertY; NeiBZ=Factor2*Zf-VertZ;                                      \
    glNormal3f(VectMul(NeiAX, NeiAY, NeiAZ, NeiBX, NeiBY, NeiBZ));                                               \
    glVertex3f(VertX, VertY, VertZ);                                                                             \
    Yf+=Ay; Yb+=Ay;                                                                                              \
    glEnd();                                                                                                     \
  }                                                                                                              \
  VS=(Factor<0);                                                                                                 \
}

#define SQUARE(Edge, Amp, Divisions, Z, VS)                                                                      \
{                                                                                                                \
  int       Xi,Yi;                                                                                               \
  GLfloat   Xf,Yf,Y,Xf2,Yf2,Y2,Xa,Xa2,Yb;                                                                        \
  GLfloat   Factor=0.0,Factor1,Factor2;                                                                          \
  GLfloat   VertX,VertY,VertZ,NeiAX,NeiAY,NeiAZ,NeiBX,NeiBY,NeiBZ;                                               \
  GLfloat   Zf=(Edge)*(Z);                                                                                       \
  GLfloat   AmpVr2=(Amp)/sqr((Edge)*SQRT2/2);                                                                    \
                                                                                                                 \
  for (Yi=0; Yi<(Divisions); Yi++) {                                                                             \
    Yf=-((Edge)/2.0) + ((float)Yi)/(Divisions)*(Edge);                                                           \
    Yf2=sqr(Yf);                                                                                                 \
    Y=Yf+1.0/(Divisions)*(Edge);                                                                                 \
    Y2=sqr(Y);                                                                                                   \
    glBegin(GL_QUAD_STRIP);                                                                                      \
    for (Xi=0; Xi<=(Divisions); Xi++) {                                                                          \
      Xf=-((Edge)/2.0) + ((float)Xi)/(Divisions)*(Edge);                                                         \
      Xf2=sqr(Xf);                                                                                               \
                                                                                                                 \
      Xa=Xf+0.001; Yb=Y+0.001;                                                                                   \
      Factor=1-((Xf2+Y2)*AmpVr2);                                                                                \
      Factor1=1-(((Xa2=sqr(Xa))+Y2)*AmpVr2);                                                                     \
      Factor2=1-((Xf2+sqr(Yb))*AmpVr2);                                                                          \
      VertX=Factor*Xf;        VertY=Factor*Y;         VertZ=Factor*Zf;                                           \
      NeiAX=Factor1*Xa-VertX; NeiAY=Factor1*Y-VertY;  NeiAZ=Factor1*Zf-VertZ;                                    \
      NeiBX=Factor2*Xf-VertX; NeiBY=Factor2*Yb-VertY; NeiBZ=Factor2*Zf-VertZ;                                    \
      glNormal3f(VectMul(NeiAX, NeiAY, NeiAZ, NeiBX, NeiBY, NeiBZ));                                             \
      glVertex3f(VertX, VertY, VertZ);                                                                           \
                                                                                                                 \
      Yb=Yf+0.001;                                                                                               \
      Factor=1-((Xf2+Yf2)*AmpVr2);                                                                               \
      Factor1=1-((Xa2+Yf2)*AmpVr2);                                                                              \
      Factor2=1-((Xf2+sqr(Yb))*AmpVr2);                                                                          \
      VertX=Factor*Xf;        VertY=Factor*Yf;        VertZ=Factor*Zf;                                           \
      NeiAX=Factor1*Xa-VertX; NeiAY=Factor1*Yf-VertY; NeiAZ=Factor1*Zf-VertZ;                                    \
      NeiBX=Factor2*Xf-VertX; NeiBY=Factor2*Yb-VertY; NeiBZ=Factor2*Zf-VertZ;                                    \
      glNormal3f(VectMul(NeiAX, NeiAY, NeiAZ, NeiBX, NeiBY, NeiBZ));                                             \
      glVertex3f(VertX, VertY, VertZ);                                                                           \
    }                                                                                                            \
    glEnd();                                                                                                     \
  }                                                                                                              \
  VS=(Factor<0);                                                                                                 \
}

#define PENTAGON(Edge, Amp, Divisions, Z, VS)                                                                    \
{                                                                                                                \
  int       Ri,Ti,Fi;                                                                                            \
  GLfloat   Xf,Yf,Xa,Yb,Xf2,Yf2;                                                                                 \
  GLfloat   Factor=0.0,Factor1,Factor2;                                                                          \
  GLfloat   VertX,VertY,VertZ,NeiAX,NeiAY,NeiAZ,NeiBX,NeiBY,NeiBZ;                                               \
  GLfloat   Zf=(Edge)*(Z);                                                                                       \
  GLfloat   AmpVr2=(Amp)/sqr((Edge)*cossec36_2);                                                                 \
                                                                                                                 \
  static    GLfloat x[6],y[6];                                                                                   \
  static    int arrayninit=1;                                                                                    \
                                                                                                                 \
  if (arrayninit) {                                                                                              \
    for(Fi=0;Fi<6;Fi++) {                                                                                        \
      x[Fi]=-cos( Fi*2*Pi/5 + Pi/10 )/(Divisions)*cossec36_2*(Edge);                                             \
      y[Fi]=sin( Fi*2*Pi/5 + Pi/10 )/(Divisions)*cossec36_2*(Edge);                                              \
    }                                                                                                            \
    arrayninit=0;                                                                                                \
  }                                                                                                              \
                                                                                                                 \
  for (Ri=1; Ri<=(Divisions); Ri++) {                                                                            \
    for (Fi=0; Fi<5; Fi++) {                                                                                     \
      glBegin(GL_TRIANGLE_STRIP);                                                                                \
      for (Ti=0; Ti<Ri; Ti++) {                                                                                  \
        Xf=(float)(Ri-Ti)*x[Fi] + (float)Ti*x[Fi+1];                                                             \
        Yf=(float)(Ri-Ti)*y[Fi] + (float)Ti*y[Fi+1];                                                             \
        Xa=Xf+0.001; Yb=Yf+0.001;                                                                                \
	Factor=1-(((Xf2=sqr(Xf))+(Yf2=sqr(Yf)))*AmpVr2);                                                         \
	Factor1=1-((sqr(Xa)+Yf2)*AmpVr2);                                                                        \
	Factor2=1-((Xf2+sqr(Yb))*AmpVr2);                                                                        \
        VertX=Factor*Xf;        VertY=Factor*Yf;        VertZ=Factor*Zf;                                         \
        NeiAX=Factor1*Xa-VertX; NeiAY=Factor1*Yf-VertY; NeiAZ=Factor1*Zf-VertZ;                                  \
        NeiBX=Factor2*Xf-VertX; NeiBY=Factor2*Yb-VertY; NeiBZ=Factor2*Zf-VertZ;                                  \
        glNormal3f(VectMul(NeiAX, NeiAY, NeiAZ, NeiBX, NeiBY, NeiBZ));                                           \
	glVertex3f(VertX, VertY, VertZ);                                                                         \
                                                                                                                 \
        Xf-=x[Fi]; Yf-=y[Fi]; Xa-=x[Fi]; Yb-=y[Fi];                                                              \
                                                                                                                 \
	Factor=1-(((Xf2=sqr(Xf))+(Yf2=sqr(Yf)))*AmpVr2);                                                         \
	Factor1=1-((sqr(Xa)+Yf2)*AmpVr2);                                                                        \
	Factor2=1-((Xf2+sqr(Yb))*AmpVr2);                                                                        \
        VertX=Factor*Xf;        VertY=Factor*Yf;        VertZ=Factor*Zf;                                         \
        NeiAX=Factor1*Xa-VertX; NeiAY=Factor1*Yf-VertY; NeiAZ=Factor1*Zf-VertZ;                                  \
        NeiBX=Factor2*Xf-VertX; NeiBY=Factor2*Yb-VertY; NeiBZ=Factor2*Zf-VertZ;                                  \
        glNormal3f(VectMul(NeiAX, NeiAY, NeiAZ, NeiBX, NeiBY, NeiBZ));                                           \
	glVertex3f(VertX, VertY, VertZ);                                                                         \
                                                                                                                 \
      }                                                                                                          \
      Xf=(float)Ri*x[Fi+1];                                                                                      \
      Yf=(float)Ri*y[Fi+1];                                                                                      \
      Xa=Xf+0.001; Yb=Yf+0.001;                                                                                  \
      Factor=1-(((Xf2=sqr(Xf))+(Yf2=sqr(Yf)))*AmpVr2);                                                           \
      Factor1=1-((sqr(Xa)+Yf2)*AmpVr2);                                                                          \
      Factor2=1-((Xf2+sqr(Yb))*AmpVr2);                                                                          \
      VertX=Factor*Xf;        VertY=Factor*Yf;        VertZ=Factor*Zf;                                           \
      NeiAX=Factor1*Xa-VertX; NeiAY=Factor1*Yf-VertY; NeiAZ=Factor1*Zf-VertZ;                                    \
      NeiBX=Factor2*Xf-VertX; NeiBY=Factor2*Yb-VertY; NeiBZ=Factor2*Zf-VertZ;                                    \
      glNormal3f(VectMul(NeiAX, NeiAY, NeiAZ, NeiBX, NeiBY, NeiBZ));                                             \
      glVertex3f(VertX, VertY, VertZ);                                                                           \
      glEnd();                                                                                                   \
    }                                                                                                            \
  }                                                                                                              \
  VS=(Factor<0);                                                                                             \
}

static void
draw_tetra(ModeInfo * mi)
{
	morph3dstruct *mp = &morph3d[MI_SCREEN(mi)];

	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[0]);

	TRIANGLE(2, mp->seno, mp->edgedivisions, 0.5 / SQRT6, mp->VisibleSpikes);

	glPushMatrix();
	glRotatef(180, 0, 0, 1);
	glRotatef(-tetraangle, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[1]);
	TRIANGLE(2, mp->seno, mp->edgedivisions, 0.5 / SQRT6, mp->VisibleSpikes);
	glPopMatrix();
	glPushMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-180 + tetraangle, 0.5, SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[2]);
	TRIANGLE(2, mp->seno, mp->edgedivisions, 0.5 / SQRT6, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-180 + tetraangle, 0.5, -SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[3]);
	TRIANGLE(2, mp->seno, mp->edgedivisions, 0.5 / SQRT6, mp->VisibleSpikes);
}

static void
draw_cube(ModeInfo * mi)
{
	morph3dstruct *mp = &morph3d[MI_SCREEN(mi)];

	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[0]);

	SQUARE(2, mp->seno, mp->edgedivisions, 0.5, mp->VisibleSpikes)

	glRotatef(cubeangle, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[1]);
	SQUARE(2, mp->seno, mp->edgedivisions, 0.5, mp->VisibleSpikes)
	glRotatef(cubeangle, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[2]);
	SQUARE(2, mp->seno, mp->edgedivisions, 0.5, mp->VisibleSpikes)
	glRotatef(cubeangle, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[3]);
	SQUARE(2, mp->seno, mp->edgedivisions, 0.5, mp->VisibleSpikes)
	glRotatef(cubeangle, 0, 1, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[4]);
	SQUARE(2, mp->seno, mp->edgedivisions, 0.5, mp->VisibleSpikes)
	glRotatef(2 * cubeangle, 0, 1, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[5]);
	SQUARE(2, mp->seno, mp->edgedivisions, 0.5, mp->VisibleSpikes)
}

static void
draw_octa(ModeInfo * mi)
{
	morph3dstruct *mp = &morph3d[MI_SCREEN(mi)];

	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[0]);
	TRIANGLE(2, mp->seno, mp->edgedivisions, 1 / SQRT6, mp->VisibleSpikes);

	glPushMatrix();
	glRotatef(180, 0, 0, 1);
	glRotatef(-180 + octaangle, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[1]);
	TRIANGLE(2, mp->seno, mp->edgedivisions, 1 / SQRT6, mp->VisibleSpikes);
	glPopMatrix();
	glPushMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-octaangle, 0.5, SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[2]);
	TRIANGLE(2, mp->seno, mp->edgedivisions, 1 / SQRT6, mp->VisibleSpikes);
	glPopMatrix();
	glPushMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-octaangle, 0.5, -SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[3]);
	TRIANGLE(2, mp->seno, mp->edgedivisions, 1 / SQRT6, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(180, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[4]);
	TRIANGLE(2, mp->seno, mp->edgedivisions, 1 / SQRT6, mp->VisibleSpikes);
	glPushMatrix();
	glRotatef(180, 0, 0, 1);
	glRotatef(-180 + octaangle, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[5]);
	TRIANGLE(2, mp->seno, mp->edgedivisions, 1 / SQRT6, mp->VisibleSpikes);
	glPopMatrix();
	glPushMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-octaangle, 0.5, SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[6]);
	TRIANGLE(2, mp->seno, mp->edgedivisions, 1 / SQRT6, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-octaangle, 0.5, -SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[7]);
	TRIANGLE(2, mp->seno, mp->edgedivisions, 1 / SQRT6, mp->VisibleSpikes);
}

static void
draw_dodeca(ModeInfo * mi)
{
	morph3dstruct *mp = &morph3d[MI_SCREEN(mi)];

#define TAU ((SQRT5+1)/2)

	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[0]);

	PENTAGON(1, mp->seno, mp->edgedivisions, sqr(TAU) * sqrt((TAU + 2) / 5) / 2, mp->VisibleSpikes);

	glPushMatrix();
	glRotatef(180, 0, 0, 1);
	glPushMatrix();
	glRotatef(-dodecaangle, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[1]);
	PENTAGON(1, mp->seno, mp->edgedivisions, sqr(TAU) * sqrt((TAU + 2) / 5) / 2, mp->VisibleSpikes);
	glPopMatrix();
	glPushMatrix();
	glRotatef(-dodecaangle, cos72, sin72, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[2]);
	PENTAGON(1, mp->seno, mp->edgedivisions, sqr(TAU) * sqrt((TAU + 2) / 5) / 2, mp->VisibleSpikes);
	glPopMatrix();
	glPushMatrix();
	glRotatef(-dodecaangle, cos72, -sin72, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[3]);
	PENTAGON(1, mp->seno, mp->edgedivisions, sqr(TAU) * sqrt((TAU + 2) / 5) / 2, mp->VisibleSpikes);
	glPopMatrix();
	glPushMatrix();
	glRotatef(dodecaangle, cos36, -sin36, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[4]);
	PENTAGON(1, mp->seno, mp->edgedivisions, sqr(TAU) * sqrt((TAU + 2) / 5) / 2, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(dodecaangle, cos36, sin36, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[5]);
	PENTAGON(1, mp->seno, mp->edgedivisions, sqr(TAU) * sqrt((TAU + 2) / 5) / 2, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(180, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[6]);
	PENTAGON(1, mp->seno, mp->edgedivisions, sqr(TAU) * sqrt((TAU + 2) / 5) / 2, mp->VisibleSpikes);
	glRotatef(180, 0, 0, 1);
	glPushMatrix();
	glRotatef(-dodecaangle, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[7]);
	PENTAGON(1, mp->seno, mp->edgedivisions, sqr(TAU) * sqrt((TAU + 2) / 5) / 2, mp->VisibleSpikes);
	glPopMatrix();
	glPushMatrix();
	glRotatef(-dodecaangle, cos72, sin72, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[8]);
	PENTAGON(1, mp->seno, mp->edgedivisions, sqr(TAU) * sqrt((TAU + 2) / 5) / 2, mp->VisibleSpikes);
	glPopMatrix();
	glPushMatrix();
	glRotatef(-dodecaangle, cos72, -sin72, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[9]);
	PENTAGON(1, mp->seno, mp->edgedivisions, sqr(TAU) * sqrt((TAU + 2) / 5) / 2, mp->VisibleSpikes);
	glPopMatrix();
	glPushMatrix();
	glRotatef(dodecaangle, cos36, -sin36, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[10]);
	PENTAGON(1, mp->seno, mp->edgedivisions, sqr(TAU) * sqrt((TAU + 2) / 5) / 2, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(dodecaangle, cos36, sin36, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[11]);
	PENTAGON(1, mp->seno, mp->edgedivisions, sqr(TAU) * sqrt((TAU + 2) / 5) / 2, mp->VisibleSpikes);
}

static void
draw_icosa(ModeInfo * mi)
{
	morph3dstruct *mp = &morph3d[MI_SCREEN(mi)];

	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[0]);

	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);

	glPushMatrix();

	glPushMatrix();
	glRotatef(180, 0, 0, 1);
	glRotatef(-icoangle, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[1]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPushMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-180 + icoangle, 0.5, SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[2]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-180 + icoangle, 0.5, -SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[3]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPopMatrix();
	glPushMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-180 + icoangle, 0.5, SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[4]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPushMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-180 + icoangle, 0.5, SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[5]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(180, 0, 0, 1);
	glRotatef(-icoangle, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[6]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-180 + icoangle, 0.5, -SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[7]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPushMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-180 + icoangle, 0.5, -SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[8]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(180, 0, 0, 1);
	glRotatef(-icoangle, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[9]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(180, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[10]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPushMatrix();
	glRotatef(180, 0, 0, 1);
	glRotatef(-icoangle, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[11]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPushMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-180 + icoangle, 0.5, SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[12]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-180 + icoangle, 0.5, -SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[13]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPopMatrix();
	glPushMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-180 + icoangle, 0.5, SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[14]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPushMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-180 + icoangle, 0.5, SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[15]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(180, 0, 0, 1);
	glRotatef(-icoangle, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[16]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-180 + icoangle, 0.5, -SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[17]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPushMatrix();
	glRotatef(180, 0, 1, 0);
	glRotatef(-180 + icoangle, 0.5, -SQRT3 / 2, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[18]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
	glPopMatrix();
	glRotatef(180, 0, 0, 1);
	glRotatef(-icoangle, 1, 0, 0);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mp->MaterialColor[19]);
	TRIANGLE(1.5, mp->seno, mp->edgedivisions, (3 * SQRT3 + SQRT15) / 12, mp->VisibleSpikes);
}

static void
reshape(ModeInfo * mi, int width, int height)
{
	morph3dstruct *mp = &morph3d[MI_SCREEN(mi)];

	glViewport(0, 0, mp->WindW = (GLint) width, mp->WindH = (GLint) height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(-1.0, 1.0, -1.0, 1.0, 5.0, 15.0);
	glMatrixMode(GL_MODELVIEW);
}

static void
pinit(ModeInfo * mi)
{
	morph3dstruct *mp = &morph3d[MI_SCREEN(mi)];

	glClearDepth(1.0);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glColor3f(1.0, 1.0, 1.0);

	glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
	glLightfv(GL_LIGHT0, GL_POSITION, position0);
	glLightfv(GL_LIGHT1, GL_AMBIENT, ambient);
	glLightfv(GL_LIGHT1, GL_DIFFUSE, diffuse);
	glLightfv(GL_LIGHT1, GL_POSITION, position1);
	glLightModelfv(GL_LIGHT_MODEL_AMBIENT, lmodel_ambient);
	glLightModelfv(GL_LIGHT_MODEL_TWO_SIDE, lmodel_twoside);
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);
	glEnable(GL_LIGHT1);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_NORMALIZE);

	glShadeModel(GL_SMOOTH);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, front_shininess);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, front_specular);

	switch (mp->object) {
		case 2:
			mp->draw_object = draw_cube;
			mp->MaterialColor[0] = MaterialRed;
			mp->MaterialColor[1] = MaterialGreen;
			mp->MaterialColor[2] = MaterialCyan;
			mp->MaterialColor[3] = MaterialMagenta;
			mp->MaterialColor[4] = MaterialYellow;
			mp->MaterialColor[5] = MaterialBlue;
			mp->edgedivisions = cubedivisions;
			mp->Magnitude = 2.0;
			break;
		case 3:
			mp->draw_object = draw_octa;
			mp->MaterialColor[0] = MaterialRed;
			mp->MaterialColor[1] = MaterialGreen;
			mp->MaterialColor[2] = MaterialBlue;
			mp->MaterialColor[3] = MaterialWhite;
			mp->MaterialColor[4] = MaterialCyan;
			mp->MaterialColor[5] = MaterialMagenta;
			mp->MaterialColor[6] = MaterialGray;
			mp->MaterialColor[7] = MaterialYellow;
			mp->edgedivisions = octadivisions;
			mp->Magnitude = 2.5;
			break;
		case 4:
			mp->draw_object = draw_dodeca;
			mp->MaterialColor[0] = MaterialRed;
			mp->MaterialColor[1] = MaterialGreen;
			mp->MaterialColor[2] = MaterialCyan;
			mp->MaterialColor[3] = MaterialBlue;
			mp->MaterialColor[4] = MaterialMagenta;
			mp->MaterialColor[5] = MaterialYellow;
			mp->MaterialColor[6] = MaterialGreen;
			mp->MaterialColor[7] = MaterialCyan;
			mp->MaterialColor[8] = MaterialRed;
			mp->MaterialColor[9] = MaterialMagenta;
			mp->MaterialColor[10] = MaterialBlue;
			mp->MaterialColor[11] = MaterialYellow;
			mp->edgedivisions = dodecadivisions;
			mp->Magnitude = 2.0;
			break;
		case 5:
			mp->draw_object = draw_icosa;
			mp->MaterialColor[0] = MaterialRed;
			mp->MaterialColor[1] = MaterialGreen;
			mp->MaterialColor[2] = MaterialBlue;
			mp->MaterialColor[3] = MaterialCyan;
			mp->MaterialColor[4] = MaterialYellow;
			mp->MaterialColor[5] = MaterialMagenta;
			mp->MaterialColor[6] = MaterialRed;
			mp->MaterialColor[7] = MaterialGreen;
			mp->MaterialColor[8] = MaterialBlue;
			mp->MaterialColor[9] = MaterialWhite;
			mp->MaterialColor[10] = MaterialCyan;
			mp->MaterialColor[11] = MaterialYellow;
			mp->MaterialColor[12] = MaterialMagenta;
			mp->MaterialColor[13] = MaterialRed;
			mp->MaterialColor[14] = MaterialGreen;
			mp->MaterialColor[15] = MaterialBlue;
			mp->MaterialColor[16] = MaterialCyan;
			mp->MaterialColor[17] = MaterialYellow;
			mp->MaterialColor[18] = MaterialMagenta;
			mp->MaterialColor[19] = MaterialGray;
			mp->edgedivisions = icodivisions;
			mp->Magnitude = 2.5;
			break;
		default:
			mp->draw_object = draw_tetra;
			mp->MaterialColor[0] = MaterialRed;
			mp->MaterialColor[1] = MaterialGreen;
			mp->MaterialColor[2] = MaterialBlue;
			mp->MaterialColor[3] = MaterialWhite;
			mp->edgedivisions = tetradivisions;
			mp->Magnitude = 2.5;
			break;
	}
	if (MI_IS_MONO(mi)) {
		int         loop;

		for (loop = 0; loop < 20; loop++)
			mp->MaterialColor[loop] = MaterialGray;
	}
}

void
init_morph3d(ModeInfo * mi)
{
	morph3dstruct *mp;

	if (morph3d == NULL) {
		if ((morph3d = (morph3dstruct *) calloc(MI_NUM_SCREENS(mi),
					    sizeof (morph3dstruct))) == NULL)
			return;
	}
	mp = &morph3d[MI_SCREEN(mi)];
	mp->step = NRAND(90);
	mp->VisibleSpikes = 1;

	if ((mp->glx_context = init_GL(mi)) != NULL) {

		reshape(mi, MI_WIDTH(mi), MI_HEIGHT(mi));
		glDrawBuffer(GL_BACK);
		mp->object = MI_COUNT(mi);
		if (mp->object <= 0 || mp->object > 5)
			mp->object = NRAND(5) + 1;
		pinit(mi);
	} else {
		MI_CLEARWINDOW(mi);
	}
}

void
draw_morph3d(ModeInfo * mi)
{
	Display    *display = MI_DISPLAY(mi);
	Window      window = MI_WINDOW(mi);
	morph3dstruct *mp;

	if (morph3d == NULL)
		return;
	mp = &morph3d[MI_SCREEN(mi)];

	MI_IS_DRAWN(mi) = True;

	if (!mp->glx_context)
		return;

	glXMakeCurrent(display, window, *(mp->glx_context));

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glPushMatrix();

	glTranslatef(0.0, 0.0, -10.0);

	if (!MI_IS_ICONIC(mi)) {
		glScalef(Scale4Window * mp->WindH / mp->WindW, Scale4Window, Scale4Window);
		glTranslatef(2.5 * mp->WindW / mp->WindH * sin(mp->step * 1.11), 2.5 * cos(mp->step * 1.25 * 1.11), 0);
	} else {
		glScalef(Scale4Iconic * mp->WindH / mp->WindW, Scale4Iconic, Scale4Iconic);
	}

	glRotatef(mp->step * 100, 1, 0, 0);
	glRotatef(mp->step * 95, 0, 1, 0);
	glRotatef(mp->step * 90, 0, 0, 1);

	mp->seno = (sin(mp->step) + 1.0 / 3.0) * (4.0 / 5.0) * mp->Magnitude;

	if (mp->VisibleSpikes) {
#ifdef DEBUG_CULL_FACE
		int         loop;

		for (loop = 0; loop < 20; loop++)
			mp->MaterialColor[loop] = MaterialGray;
#endif
		glDisable(GL_CULL_FACE);
	} else {
#ifdef DEBUG_CULL_FACE
		int         loop;

		for (loop = 0; loop < 20; loop++)
			mp->MaterialColor[loop] = MaterialWhite;
#endif
		glEnable(GL_CULL_FACE);
	}

	mp->draw_object(mi);

	glPopMatrix();

	glFlush();
	if (MI_IS_FPS(mi)) do_fps (mi);
	glXSwapBuffers(display, window);

	mp->step += 0.05;
}

void
change_morph3d(ModeInfo * mi)
{
	morph3dstruct *mp = &morph3d[MI_SCREEN(mi)];

	if (!mp->glx_context)
		return;

	mp->object = (mp->object) % 5 + 1;
	pinit(mi);
}

void
release_morph3d(ModeInfo * mi)
{
	if (morph3d != NULL) {
		free(morph3d);
		morph3d = (morph3dstruct *) NULL;
	}
	FreeAllGL(mi);
}

#endif
