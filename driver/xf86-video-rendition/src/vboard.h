
/*
 * vboard.h
 *
 * functions to interact with a Verite board
 */

#ifndef __VBOARD_H__
#define __VBOARD_H__


/*
 * includes
 */

#include "rendition.h"
#include "vtypes.h"

/*
 * function prototypes
 */

int verite_initboard(ScrnInfoPtr pScreenInfo);
int verite_resetboard(ScrnInfoPtr pScreenInfo);
int verite_getmemorysize(ScrnInfoPtr pScreenInfo);

void verite_check_csucode(ScrnInfoPtr pScreenInfo);


#endif /* __VBOARD_H__ */

/*
 * end of file vboard.h
 */
