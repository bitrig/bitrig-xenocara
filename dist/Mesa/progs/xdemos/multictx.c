/*
 * Copyright (C) 2009  VMware, Inc.   All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Test rendering with two contexts into one window.
 * Setup different rendering state for each context to check that
 * context switching is handled properly.
 *
 * Brian Paul
 * 6 Aug 2009
 */


#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <GL/gl.h>
#include <GL/glx.h>



#ifndef M_PI
#define M_PI 3.14159265
#endif


/** Event handler results: */
#define NOP 0
#define EXIT 1
#define DRAW 2

static GLfloat view_rotx = 0.0, view_roty = 210.0, view_rotz = 0.0;
static GLint gear1, gear2;
static GLfloat angle = 0.0;

static GLboolean animate = GL_TRUE;	/* Animation */


static double
current_time(void)
{
   struct timeval tv;
#ifdef __VMS
   (void) gettimeofday(&tv, NULL );
#else
   struct timezone tz;
   (void) gettimeofday(&tv, &tz);
#endif
   return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
}


/*
 *
 *  Draw a gear wheel.  You'll probably want to call this function when
 *  building a display list since we do a lot of trig here.
 * 
 *  Input:  inner_radius - radius of hole at center
 *          outer_radius - radius at center of teeth
 *          width - width of gear
 *          teeth - number of teeth
 *          tooth_depth - depth of tooth
 */
static void
gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
     GLint teeth, GLfloat tooth_depth)
{
   GLint i;
   GLfloat r0, r1, r2;
   GLfloat angle, da;
   GLfloat u, v, len;

   r0 = inner_radius;
   r1 = outer_radius - tooth_depth / 2.0;
   r2 = outer_radius + tooth_depth / 2.0;

   da = 2.0 * M_PI / teeth / 4.0;

   glShadeModel(GL_FLAT);

   glNormal3f(0.0, 0.0, 1.0);

   /* draw front face */
   glBegin(GL_QUAD_STRIP);
   for (i = 0; i <= teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;
      glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
      glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
      if (i < teeth) {
	 glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
	 glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		    width * 0.5);
      }
   }
   glEnd();

   /* draw front sides of teeth */
   glBegin(GL_QUADS);
   da = 2.0 * M_PI / teeth / 4.0;
   for (i = 0; i < teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;

      glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
      glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
      glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
		 width * 0.5);
      glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		 width * 0.5);
   }
   glEnd();

   glNormal3f(0.0, 0.0, -1.0);

   /* draw back face */
   glBegin(GL_QUAD_STRIP);
   for (i = 0; i <= teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;
      glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
      glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
      if (i < teeth) {
	 glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		    -width * 0.5);
	 glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
      }
   }
   glEnd();

   /* draw back sides of teeth */
   glBegin(GL_QUADS);
   da = 2.0 * M_PI / teeth / 4.0;
   for (i = 0; i < teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;

      glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		 -width * 0.5);
      glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
		 -width * 0.5);
      glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
      glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
   }
   glEnd();

   /* draw outward faces of teeth */
   glBegin(GL_QUAD_STRIP);
   for (i = 0; i < teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;

      glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
      glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
      u = r2 * cos(angle + da) - r1 * cos(angle);
      v = r2 * sin(angle + da) - r1 * sin(angle);
      len = sqrt(u * u + v * v);
      u /= len;
      v /= len;
      glNormal3f(v, -u, 0.0);
      glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
      glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
      glNormal3f(cos(angle), sin(angle), 0.0);
      glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
		 width * 0.5);
      glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
		 -width * 0.5);
      u = r1 * cos(angle + 3 * da) - r2 * cos(angle + 2 * da);
      v = r1 * sin(angle + 3 * da) - r2 * sin(angle + 2 * da);
      glNormal3f(v, -u, 0.0);
      glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		 width * 0.5);
      glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		 -width * 0.5);
      glNormal3f(cos(angle), sin(angle), 0.0);
   }

   glVertex3f(r1 * cos(0), r1 * sin(0), width * 0.5);
   glVertex3f(r1 * cos(0), r1 * sin(0), -width * 0.5);

   glEnd();

   glShadeModel(GL_SMOOTH);

   /* draw inside radius cylinder */
   glBegin(GL_QUAD_STRIP);
   for (i = 0; i <= teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;
      glNormal3f(-cos(angle), -sin(angle), 0.0);
      glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
      glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
   }
   glEnd();
}


static void
draw(int ctx)
{
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   glPushMatrix();
   glRotatef(view_rotx, 1.0, 0.0, 0.0);
   glRotatef(view_roty + angle, 0.0, 1.0, 0.0);
   glRotatef(view_rotz, 0.0, 0.0, 1.0);

   if (ctx == 0) {
      glDisable(GL_CULL_FACE);
      glPushMatrix();
      glRotatef(angle, 0.0, 0.0, 1.0);
      glCallList(gear1);
      glPopMatrix();
      /* This should not effect the other context's rendering */
      glEnable(GL_CULL_FACE);
      glCullFace(GL_FRONT_AND_BACK);
   }
   else {
      glPushMatrix();
      glRotatef(-2.0 * angle - 9.0, 0.0, 0.0, 1.0);
      glCallList(gear2);
      glPopMatrix();
   }

   glPopMatrix();

   /* this flush is important since we'll be switching contexts next */
   glFlush();
}



static void
draw_frame(Display *dpy, Window win, GLXContext ctx1, GLXContext ctx2)
{
   static double tRot0 = -1.0;
   double dt, t = current_time();

   if (tRot0 < 0.0)
      tRot0 = t;
   dt = t - tRot0;
   tRot0 = t;

   if (animate) {
      /* advance rotation for next frame */
      angle += 70.0 * dt;  /* 70 degrees per second */
      if (angle > 3600.0)
         angle -= 3600.0;
   }

   glXMakeCurrent(dpy, (GLXDrawable) win, ctx1);
   draw(0);

   glXMakeCurrent(dpy, (GLXDrawable) win, ctx2);
   draw(1);

   glXSwapBuffers(dpy, win);
}


/* new window size or exposure */
static void
reshape(Display *dpy, Window win,
        GLXContext ctx1, GLXContext ctx2, int width, int height)
{
   int i;

   width /= 2;

   /* loop: left half of window, right half of window */
   for (i = 0; i < 2; i++) {
      if (i == 0)
         glXMakeCurrent(dpy, win, ctx1);
      else
         glXMakeCurrent(dpy, win, ctx2);

      glViewport(width * i, 0, width, height);
      glScissor(width * i, 0, width, height);

      {
         GLfloat h = (GLfloat) height / (GLfloat) width;

         glMatrixMode(GL_PROJECTION);
         glLoadIdentity();
         glFrustum(-1.0, 1.0, -h, h, 5.0, 60.0);
      }

      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();
      glTranslatef(0.0, 0.0, -30.0);
   }
}
   


static void
init(Display *dpy, Window win, GLXContext ctx1, GLXContext ctx2)
{
   static GLfloat pos[4] = { 5.0, 5.0, 10.0, 0.0 };
   static GLfloat red[4] = { 0.8, 0.1, 0.0, 1.0 };
   static GLfloat green[4] = { 0.0, 0.8, 0.2, 0.5 };
   /*static GLfloat blue[4] = { 0.2, 0.2, 1.0, 1.0 };*/

   /* first ctx */
   {
      static GLuint stipple[32] = {
         0x00ff00ff, 0x00ff00ff, 0x00ff00ff, 0x00ff00ff,
         0x00ff00ff, 0x00ff00ff, 0x00ff00ff, 0x00ff00ff,

         0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00,
         0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00,

         0x00ff00ff, 0x00ff00ff, 0x00ff00ff, 0x00ff00ff,
         0x00ff00ff, 0x00ff00ff, 0x00ff00ff, 0x00ff00ff,

         0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00,
         0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00
      };

      glXMakeCurrent(dpy, win, ctx1);

      glLightfv(GL_LIGHT0, GL_POSITION, pos);
      glEnable(GL_LIGHTING);
      glEnable(GL_LIGHT0);
      glEnable(GL_DEPTH_TEST);

      gear1 = glGenLists(1);
      glNewList(gear1, GL_COMPILE);
      glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
      gear(1.0, 4.0, 1.0, 20, 0.7);
      glEndList();

      glEnable(GL_NORMALIZE);
      glEnable(GL_SCISSOR_TEST);
      glClearColor(0.4, 0.4, 0.4, 1.0);

      glPolygonStipple((GLubyte *) stipple);
      glEnable(GL_POLYGON_STIPPLE);
   }

   /* second ctx */
   {
      glXMakeCurrent(dpy, win, ctx2);

      glLightfv(GL_LIGHT0, GL_POSITION, pos);
      glEnable(GL_LIGHTING);
      glEnable(GL_LIGHT0);
      glEnable(GL_DEPTH_TEST);

      gear2 = glGenLists(1);
      glNewList(gear2, GL_COMPILE);
      glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
      gear(1.5, 3.0, 1.5, 16, 0.7);
      glEndList();

      glEnable(GL_NORMALIZE);
      glEnable(GL_SCISSOR_TEST);
      glClearColor(0.6, 0.6, 0.6, 1.0);

      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
   }
}


/**
 * Create an RGB, double-buffered window.
 * Return the window and two context handles.
 */
static void
make_window_and_contexts( Display *dpy, const char *name,
                          int x, int y, int width, int height,
                          Window *winRet,
                          GLXContext *ctxRet1,
                          GLXContext *ctxRet2)
{
   int attribs[] = { GLX_RGBA,
                     GLX_RED_SIZE, 1,
                     GLX_GREEN_SIZE, 1,
                     GLX_BLUE_SIZE, 1,
                     GLX_DOUBLEBUFFER,
                     GLX_DEPTH_SIZE, 1,
                     None };
   int scrnum;
   XSetWindowAttributes attr;
   unsigned long mask;
   Window root;
   Window win;
   XVisualInfo *visinfo;

   scrnum = DefaultScreen( dpy );
   root = RootWindow( dpy, scrnum );

   visinfo = glXChooseVisual( dpy, scrnum, attribs );
   if (!visinfo) {
      printf("Error: couldn't get an RGB, Double-buffered visual\n");
      exit(1);
   }

   /* window attributes */
   attr.background_pixel = 0;
   attr.border_pixel = 0;
   attr.colormap = XCreateColormap( dpy, root, visinfo->visual, AllocNone);
   attr.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask;
   mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

   win = XCreateWindow( dpy, root, x, y, width, height,
		        0, visinfo->depth, InputOutput,
		        visinfo->visual, mask, &attr );

   /* set hints and properties */
   {
      XSizeHints sizehints;
      sizehints.x = x;
      sizehints.y = y;
      sizehints.width  = width;
      sizehints.height = height;
      sizehints.flags = USSize | USPosition;
      XSetNormalHints(dpy, win, &sizehints);
      XSetStandardProperties(dpy, win, name, name,
                              None, (char **)NULL, 0, &sizehints);
   }

   *winRet = win;
   *ctxRet1 = glXCreateContext( dpy, visinfo, NULL, True );
   *ctxRet2 = glXCreateContext( dpy, visinfo, NULL, True );

   if (!*ctxRet1 || !*ctxRet2) {
      printf("Error: glXCreateContext failed\n");
      exit(1);
   }

   XFree(visinfo);
}


/**
 * Handle one X event.
 * \return NOP, EXIT or DRAW
 */
static int
handle_event(Display *dpy, Window win, GLXContext ctx1, GLXContext ctx2,
             XEvent *event)
{
   (void) dpy;
   (void) win;

   switch (event->type) {
   case Expose:
      return DRAW;
   case ConfigureNotify:
      reshape(dpy, win, ctx1, ctx2,
              event->xconfigure.width, event->xconfigure.height);
      break;
   case KeyPress:
      {
         char buffer[10];
         int r, code;
         code = XLookupKeysym(&event->xkey, 0);
         if (code == XK_Left) {
            view_roty += 5.0;
         }
         else if (code == XK_Right) {
            view_roty -= 5.0;
         }
         else if (code == XK_Up) {
            view_rotx += 5.0;
         }
         else if (code == XK_Down) {
            view_rotx -= 5.0;
         }
         else {
            r = XLookupString(&event->xkey, buffer, sizeof(buffer),
                              NULL, NULL);
            if (buffer[0] == 27) {
               /* escape */
               return EXIT;
            }
            else if (buffer[0] == 'a' || buffer[0] == 'A') {
               animate = !animate;
            }
         }
         return DRAW;
      }
   }
   return NOP;
}


static void
event_loop(Display *dpy, Window win, GLXContext ctx1, GLXContext ctx2)
{
   while (1) {
      int op;
      while (!animate || XPending(dpy) > 0) {
         XEvent event;
         XNextEvent(dpy, &event);
         op = handle_event(dpy, win, ctx1, ctx2, &event);
         if (op == EXIT)
            return;
         else if (op == DRAW)
            break;
      }

      draw_frame(dpy, win, ctx1, ctx2);
   }
}


int
main(int argc, char *argv[])
{
   unsigned int winWidth = 800, winHeight = 400;
   int x = 0, y = 0;
   Display *dpy;
   Window win;
   GLXContext ctx1, ctx2;
   char *dpyName = NULL;
   GLboolean printInfo = GL_FALSE;
   int i;

   for (i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-display") == 0) {
         dpyName = argv[i+1];
         i++;
      }
      else {
         return 1;
      }
   }

   dpy = XOpenDisplay(dpyName);
   if (!dpy) {
      printf("Error: couldn't open display %s\n",
	     dpyName ? dpyName : getenv("DISPLAY"));
      return -1;
   }

   make_window_and_contexts(dpy, "multictx", x, y, winWidth, winHeight,
                            &win, &ctx1, &ctx2);
   XMapWindow(dpy, win);

   if (printInfo) {
      printf("GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER));
      printf("GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION));
      printf("GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR));
      printf("GL_EXTENSIONS = %s\n", (char *) glGetString(GL_EXTENSIONS));
   }

   init(dpy, win, ctx1, ctx2);

   /* Set initial projection/viewing transformation.
    * We can't be sure we'll get a ConfigureNotify event when the window
    * first appears.
    */
   reshape(dpy, win, ctx1, ctx2, winWidth, winHeight);

   event_loop(dpy, win, ctx1, ctx2);

   glDeleteLists(gear1, 1);
   glDeleteLists(gear2, 1);
   glXDestroyContext(dpy, ctx1);
   glXDestroyContext(dpy, ctx2);
   XDestroyWindow(dpy, win);
   XCloseDisplay(dpy);

   return 0;
}
