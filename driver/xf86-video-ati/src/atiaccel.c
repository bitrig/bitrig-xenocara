/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/ati/atiaccel.c,v 1.13 2003/04/24 21:19:22 tsi Exp $ */
/*
 * Copyright 2001 through 2004 by Marc Aurele La France (TSI @ UQV), tsi@xfree86.org
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of Marc Aurele La France not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  Marc Aurele La France makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as-is" without express or implied warranty.
 *
 * MARC AURELE LA FRANCE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.  IN NO
 * EVENT SHALL MARC AURELE LA FRANCE BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * DRI support by:
 *    Leif Delgass <ldelgass@retinalburn.net>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "atiaccel.h"
#include "atiadapter.h"
#include "atimach64accel.h"
#include "atistruct.h"

#ifdef USE_XAA
/*
 * ATIInitializeAcceleration --
 *
 * This function is called to initialise both the framebuffer manager and XAA
 * on a screen.
 */
Bool
ATIInitializeAcceleration
(
    ScreenPtr   pScreen,
    ScrnInfoPtr pScreenInfo,
    ATIPtr      pATI
)
{
    if (pATI->OptionAccel)
    {
        if (!(pATI->pXAAInfo = XAACreateInfoRec()))
            return FALSE;

        switch (pATI->Adapter)
        {
            case ATI_ADAPTER_MACH64:
                ATIMach64AccelInit(pATI, pATI->pXAAInfo);
                break;

            default:
                break;
        }
    }

    if (!pATI->OptionAccel || XAAInit(pScreen, pATI->pXAAInfo))
        return TRUE;

    XAADestroyInfoRec(pATI->pXAAInfo);
    pATI->pXAAInfo = NULL;
    return FALSE;
}

FBLinearPtr
ATIResizeOffscreenLinear
(
    ScreenPtr   pScreen,
    FBLinearPtr pLinear,
    int         Size
)
{
    if (Size <= 0)
    {
        xf86FreeOffscreenLinear(pLinear);
        return NULL;
    }

    if (pLinear)
    {
        if ((pLinear->size >= Size) ||
            xf86ResizeOffscreenLinear(pLinear, Size))
        {
            pLinear->MoveLinearCallback = NULL;
            pLinear->RemoveLinearCallback = NULL;
            return pLinear;
        }

        xf86FreeOffscreenLinear(pLinear);
    }

    pLinear = xf86AllocateOffscreenLinear(pScreen, Size, 16, NULL, NULL, NULL);

    if (!pLinear)
    {
        int maxSize;

        xf86QueryLargestOffscreenLinear(pScreen, &maxSize, 16,
            PRIORITY_EXTREME);

        if (maxSize < Size)
            return NULL;

        xf86PurgeUnlockedOffscreenAreas(pScreen);
        pLinear =
            xf86AllocateOffscreenLinear(pScreen, Size, 16, NULL, NULL, NULL);
    }

    return pLinear;
}
#endif /* USE_XAA */
