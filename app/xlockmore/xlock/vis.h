#if !defined( lint ) && !defined( SABER )
/* #ident	"@(#)vis.h	4.14 99/06/17 xlockmore" */

#endif

/*-
 * Visual stuff
 *
 * See xlock.c for copying information.
 *
 * Revision History:
 * 17-06-99: Started log. :)
 */

#ifdef __cplusplus
  extern "C" {
#endif
extern void defaultVisualInfo(Display * display, int screen);
#if 0
extern Bool setupColormap(ModeInfo * mi, int *colors, Bool * truecolor,
  unsigned long *redmask, unsigned long *bluemask, unsigned long *greenmask);
#endif
extern int  visualClassFromName(char *name);
extern const char *nameOfVisualClass(int visualclass);
extern Bool has_writable_cells(ModeInfo * mi);
extern Bool fixedColors(ModeInfo * mi);
#ifdef __cplusplus
  }
#endif

#ifdef USE_GL
#include "visgl.h"
#endif
