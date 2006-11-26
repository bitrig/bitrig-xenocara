/*

Copyright 1993, 1998  The Open Group

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

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/X.h>
#include "scrnintstr.h"
#include "misc.h"
#include "os.h"
#include "windowstr.h"
#include "resource.h"
#include "dixstruct.h"
#include "gcstruct.h"
#include "colormapst.h"
#include "servermd.h"
#include "site.h"
#include "inputstr.h"
#include "extnsionst.h"

/*
 *  See the Wrappers and devPrivates section in "Definition of the
 *  Porting Layer for the X v11 Sample Server" (doc/Server/ddx.tbl.ms)
 *  for information on how to use devPrivates.
 */

/*
 *  extension private machinery
 */

static int  extensionPrivateCount;
int extensionPrivateLen;
unsigned *extensionPrivateSizes;
unsigned totalExtensionSize;

void
ResetExtensionPrivates()
{
    extensionPrivateCount = 0;
    extensionPrivateLen = 0;
    xfree(extensionPrivateSizes);
    extensionPrivateSizes = (unsigned *)NULL;
    totalExtensionSize =
	((sizeof(ExtensionEntry) + sizeof(long) - 1) / sizeof(long)) * sizeof(long);
}

_X_EXPORT int
AllocateExtensionPrivateIndex()
{
    return extensionPrivateCount++;
}

_X_EXPORT Bool
AllocateExtensionPrivate(int index2, unsigned amount)
{
    unsigned oldamount;

    /* Round up sizes for proper alignment */
    amount = ((amount + (sizeof(long) - 1)) / sizeof(long)) * sizeof(long);

    if (index2 >= extensionPrivateLen)
    {
	unsigned *nsizes;
	nsizes = (unsigned *)xrealloc(extensionPrivateSizes,
				      (index2 + 1) * sizeof(unsigned));
	if (!nsizes)
	    return FALSE;
	while (extensionPrivateLen <= index2)
	{
	    nsizes[extensionPrivateLen++] = 0;
	    totalExtensionSize += sizeof(DevUnion);
	}
	extensionPrivateSizes = nsizes;
    }
    oldamount = extensionPrivateSizes[index2];
    if (amount > oldamount)
    {
	extensionPrivateSizes[index2] = amount;
	totalExtensionSize += (amount - oldamount);
    }
    return TRUE;
}

/*
 *  client private machinery
 */

static int  clientPrivateCount;
int clientPrivateLen;
unsigned *clientPrivateSizes;
unsigned totalClientSize;

void
ResetClientPrivates()
{
    clientPrivateCount = 0;
    clientPrivateLen = 0;
    xfree(clientPrivateSizes);
    clientPrivateSizes = (unsigned *)NULL;
    totalClientSize =
	((sizeof(ClientRec) + sizeof(long) - 1) / sizeof(long)) * sizeof(long);
}

_X_EXPORT int
AllocateClientPrivateIndex()
{
    return clientPrivateCount++;
}

_X_EXPORT Bool
AllocateClientPrivate(int index2, unsigned amount)
{
    unsigned oldamount;

    /* Round up sizes for proper alignment */
    amount = ((amount + (sizeof(long) - 1)) / sizeof(long)) * sizeof(long);

    if (index2 >= clientPrivateLen)
    {
	unsigned *nsizes;
	nsizes = (unsigned *)xrealloc(clientPrivateSizes,
				      (index2 + 1) * sizeof(unsigned));
	if (!nsizes)
	    return FALSE;
	while (clientPrivateLen <= index2)
	{
	    nsizes[clientPrivateLen++] = 0;
	    totalClientSize += sizeof(DevUnion);
	}
	clientPrivateSizes = nsizes;
    }
    oldamount = clientPrivateSizes[index2];
    if (amount > oldamount)
    {
	clientPrivateSizes[index2] = amount;
	totalClientSize += (amount - oldamount);
    }
    return TRUE;
}

/*
 *  screen private machinery
 */

int  screenPrivateCount;

void
ResetScreenPrivates()
{
    screenPrivateCount = 0;
}

/* this can be called after some screens have been created,
 * so we have to worry about resizing existing devPrivates
 */
_X_EXPORT int
AllocateScreenPrivateIndex()
{
    int		idx;
    int		i;
    ScreenPtr	pScreen;
    DevUnion	*nprivs;

    idx = screenPrivateCount++;
    for (i = 0; i < screenInfo.numScreens; i++)
    {
	pScreen = screenInfo.screens[i];
	nprivs = (DevUnion *)xrealloc(pScreen->devPrivates,
				      screenPrivateCount * sizeof(DevUnion));
	if (!nprivs)
	{
	    screenPrivateCount--;
	    return -1;
	}
	/* Zero the new private */
	bzero(&nprivs[idx], sizeof(DevUnion));
	pScreen->devPrivates = nprivs;
    }
    return idx;
}


/*
 *  window private machinery
 */

static int  windowPrivateCount;

void
ResetWindowPrivates()
{
    windowPrivateCount = 0;
}

_X_EXPORT int
AllocateWindowPrivateIndex()
{
    return windowPrivateCount++;
}

_X_EXPORT Bool
AllocateWindowPrivate(register ScreenPtr pScreen, int index2, unsigned amount)
{
    unsigned oldamount;

    /* Round up sizes for proper alignment */
    amount = ((amount + (sizeof(long) - 1)) / sizeof(long)) * sizeof(long);

    if (index2 >= pScreen->WindowPrivateLen)
    {
	unsigned *nsizes;
	nsizes = (unsigned *)xrealloc(pScreen->WindowPrivateSizes,
				      (index2 + 1) * sizeof(unsigned));
	if (!nsizes)
	    return FALSE;
	while (pScreen->WindowPrivateLen <= index2)
	{
	    nsizes[pScreen->WindowPrivateLen++] = 0;
	    pScreen->totalWindowSize += sizeof(DevUnion);
	}
	pScreen->WindowPrivateSizes = nsizes;
    }
    oldamount = pScreen->WindowPrivateSizes[index2];
    if (amount > oldamount)
    {
	pScreen->WindowPrivateSizes[index2] = amount;
	pScreen->totalWindowSize += (amount - oldamount);
    }
    return TRUE;
}


/*
 *  gc private machinery 
 */

static int  gcPrivateCount;

void
ResetGCPrivates()
{
    gcPrivateCount = 0;
}

_X_EXPORT int
AllocateGCPrivateIndex()
{
    return gcPrivateCount++;
}

_X_EXPORT Bool
AllocateGCPrivate(register ScreenPtr pScreen, int index2, unsigned amount)
{
    unsigned oldamount;

    /* Round up sizes for proper alignment */
    amount = ((amount + (sizeof(long) - 1)) / sizeof(long)) * sizeof(long);

    if (index2 >= pScreen->GCPrivateLen)
    {
	unsigned *nsizes;
	nsizes = (unsigned *)xrealloc(pScreen->GCPrivateSizes,
				      (index2 + 1) * sizeof(unsigned));
	if (!nsizes)
	    return FALSE;
	while (pScreen->GCPrivateLen <= index2)
	{
	    nsizes[pScreen->GCPrivateLen++] = 0;
	    pScreen->totalGCSize += sizeof(DevUnion);
	}
	pScreen->GCPrivateSizes = nsizes;
    }
    oldamount = pScreen->GCPrivateSizes[index2];
    if (amount > oldamount)
    {
	pScreen->GCPrivateSizes[index2] = amount;
	pScreen->totalGCSize += (amount - oldamount);
    }
    return TRUE;
}


/*
 *  pixmap private machinery
 */
#ifdef PIXPRIV
static int  pixmapPrivateCount;

void
ResetPixmapPrivates()
{
    pixmapPrivateCount = 0;
}

_X_EXPORT int
AllocatePixmapPrivateIndex()
{
    return pixmapPrivateCount++;
}

_X_EXPORT Bool
AllocatePixmapPrivate(register ScreenPtr pScreen, int index2, unsigned amount)
{
    unsigned oldamount;

    /* Round up sizes for proper alignment */
    amount = ((amount + (sizeof(long) - 1)) / sizeof(long)) * sizeof(long);

    if (index2 >= pScreen->PixmapPrivateLen)
    {
	unsigned *nsizes;
	nsizes = (unsigned *)xrealloc(pScreen->PixmapPrivateSizes,
				      (index2 + 1) * sizeof(unsigned));
	if (!nsizes)
	    return FALSE;
	while (pScreen->PixmapPrivateLen <= index2)
	{
	    nsizes[pScreen->PixmapPrivateLen++] = 0;
	    pScreen->totalPixmapSize += sizeof(DevUnion);
	}
	pScreen->PixmapPrivateSizes = nsizes;
    }
    oldamount = pScreen->PixmapPrivateSizes[index2];
    if (amount > oldamount)
    {
	pScreen->PixmapPrivateSizes[index2] = amount;
	pScreen->totalPixmapSize += (amount - oldamount);
    }
    pScreen->totalPixmapSize = BitmapBytePad(pScreen->totalPixmapSize * 8);
    return TRUE;
}
#endif


/*
 *  colormap private machinery
 */

int  colormapPrivateCount;

void
ResetColormapPrivates()
{
    colormapPrivateCount = 0;
}


_X_EXPORT int
AllocateColormapPrivateIndex (InitCmapPrivFunc initPrivFunc)
{
    int		index;
    int		i;
    ColormapPtr	pColormap;
    DevUnion	*privs;

    index = colormapPrivateCount++;

    for (i = 0; i < screenInfo.numScreens; i++)
    {
	/*
	 * AllocateColormapPrivateIndex may be called after the
	 * default colormap has been created on each screen!
	 *
	 * We must resize the devPrivates array for the default
	 * colormap on each screen, making room for this new private.
	 * We also call the initialization function 'initPrivFunc' on
	 * the new private allocated for each default colormap.
	 */

	ScreenPtr pScreen = screenInfo.screens[i];

	pColormap = (ColormapPtr) LookupIDByType (
	    pScreen->defColormap, RT_COLORMAP);

	if (pColormap)
	{
	    privs = (DevUnion *) xrealloc (pColormap->devPrivates,
		colormapPrivateCount * sizeof(DevUnion));
	    if (!privs) {
		colormapPrivateCount--;
		return -1;
	    }
	    bzero(&privs[index], sizeof(DevUnion));
	    pColormap->devPrivates = privs;
	    if (!(*initPrivFunc)(pColormap,index))
	    {
		colormapPrivateCount--;
		return -1;
	    }
	}
    }

    return index;
}

/*
 *  device private machinery
 */

static int devicePrivateIndex = 0;

_X_EXPORT int
AllocateDevicePrivateIndex()
{
    return devicePrivateIndex++;
}

_X_EXPORT Bool
AllocateDevicePrivate(DeviceIntPtr device, int index)
{
    if (device->nPrivates < ++index) {
	DevUnion *nprivs = (DevUnion *) xrealloc(device->devPrivates,
						 index * sizeof(DevUnion));
	if (!nprivs)
	    return FALSE;
	device->devPrivates = nprivs;
	bzero(&nprivs[device->nPrivates], sizeof(DevUnion)
	      * (index - device->nPrivates));
	device->nPrivates = index;
	return TRUE;
    } else {
	return TRUE;
    }
}

void
ResetDevicePrivateIndex(void)
{
    devicePrivateIndex = 0;
}
