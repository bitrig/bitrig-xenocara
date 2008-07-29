/*
 * Copyright 2006 by VMware, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */

/*
 * vmwarectrl.c --
 *
 *      The implementation of the VMWARE_CTRL protocol extension that
 *      allows X clients to communicate with the driver.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define NEED_REPLIES
#define NEED_EVENTS
#include "dixstruct.h"
#include "extnsionst.h"
#include <X11/X.h>
#include <X11/extensions/panoramiXproto.h>

#include "vmware.h"
#include "vmwarectrlproto.h"


/*
 *----------------------------------------------------------------------------
 *
 * VMwareCtrlQueryVersion --
 *
 *      Implementation of QueryVersion command handler. Initialises and
 *      sends a reply.
 *
 * Results:
 *      Standard response codes.
 *
 * Side effects:
 *      Writes reply to client
 *
 *----------------------------------------------------------------------------
 */

static int
VMwareCtrlQueryVersion(ClientPtr client)
{
   xVMwareCtrlQueryVersionReply rep = { 0, };
   register int n;

   REQUEST_SIZE_MATCH(xVMwareCtrlQueryVersionReq);

   rep.type = X_Reply;
   rep.length = 0;
   rep.sequenceNumber = client->sequence;
   rep.majorVersion = VMWARE_CTRL_MAJOR_VERSION;
   rep.minorVersion = VMWARE_CTRL_MINOR_VERSION;
   if (client->swapped) {
      swaps(&rep.sequenceNumber, n);
      swapl(&rep.length, n);
      swapl(&rep.majorVersion, n);
      swapl(&rep.minorVersion, n);
   }
   WriteToClient(client, sizeof(xVMwareCtrlQueryVersionReply), (char *)&rep);

   return client->noClientException;
}


/*
 *----------------------------------------------------------------------------
 *
 * VMwareCtrlDoSetRes --
 *
 *      Set the custom resolution into the mode list.
 *
 *      This is done by alternately updating one of two dynamic modes. It is
 *      done this way because the server gets upset if you try to switch
 *      to a new resolution that has the same index as the current one.
 *
 * Results:
 *      TRUE on success, FALSE otherwise.
 *
 * Side effects:
 *      One dynamic mode will be updated if successful.
 *
 *----------------------------------------------------------------------------
 */

static Bool
VMwareCtrlDoSetRes(ScrnInfoPtr pScrn,
                   CARD32 x,
                   CARD32 y,
                   Bool resetXinerama)
{
   DisplayModePtr mode;
   VMWAREPtr pVMWARE = VMWAREPTR(pScrn);

   if (pScrn && pScrn->modes) {
      VmwareLog(("DoSetRes: %d %d\n", x, y));
  
      if (resetXinerama) {
         xfree(pVMWARE->xineramaNextState);
         pVMWARE->xineramaNextState = NULL;
         pVMWARE->xineramaNextNumOutputs = 0;
      }

      /* 
       * Don't resize larger than possible but don't
       * return an X Error either.
       */
      if (x > pVMWARE->maxWidth ||
          y > pVMWARE->maxHeight) {
         return TRUE;
      }

      /*
       * Switch the dynamic modes so that we alternate
       * which one gets updated on each call.
       */
      mode = pVMWARE->dynMode1;
      pVMWARE->dynMode1 = pVMWARE->dynMode2;
      pVMWARE->dynMode2 = mode;

      /*
       * Initialise the dynamic mode if it hasn't been used before.
       */
      if (!pVMWARE->dynMode1) {
         pVMWARE->dynMode1 = VMWAREAddDisplayMode(pScrn, "DynMode", 1, 1);
      }
      mode = pVMWARE->dynMode1;

      mode->HDisplay = x;
      mode->VDisplay = y;

      return TRUE;
   } else {
      return FALSE;
   }
}


/*
 *----------------------------------------------------------------------------
 *
 * VMwareCtrlSetRes --
 *
 *      Implementation of SetRes command handler. Initialises and sends a
 *      reply.
 *
 * Results:
 *      Standard response codes.
 *
 * Side effects:
 *      Writes reply to client
 *
 *----------------------------------------------------------------------------
 */

static int
VMwareCtrlSetRes(ClientPtr client)
{
   REQUEST(xVMwareCtrlSetResReq);
   xVMwareCtrlSetResReply rep = { 0, };
   ScrnInfoPtr pScrn;
   ExtensionEntry *ext;
   register int n;

   REQUEST_SIZE_MATCH(xVMwareCtrlSetResReq);

   if (!(ext = CheckExtension(VMWARE_CTRL_PROTOCOL_NAME))) {
      return BadMatch;
   }

   pScrn = ext->extPrivate;
   if (pScrn->scrnIndex != stuff->screen) {
      return BadMatch;
   }

   if (!VMwareCtrlDoSetRes(pScrn, stuff->x, stuff->y, TRUE)) {
      return BadValue;
   }

   rep.type = X_Reply;
   rep.length = (sizeof(xVMwareCtrlSetResReply) - sizeof(xGenericReply)) >> 2;
   rep.sequenceNumber = client->sequence;
   rep.screen = stuff->screen;
   rep.x = stuff->x;
   rep.y = stuff->y;
   if (client->swapped) {
      swaps(&rep.sequenceNumber, n);
      swapl(&rep.length, n);
      swapl(&rep.screen, n);
      swapl(&rep.x, n);
      swapl(&rep.y, n);
   }
   WriteToClient(client, sizeof(xVMwareCtrlSetResReply), (char *)&rep);

   return client->noClientException;
}


/*
 *----------------------------------------------------------------------------
 *
 * VMwareCtrlDoSetTopology --
 *
 *      Set the custom topology and set a dynamic mode to the bounding box
 *      of the passed topology. If a topology is already pending, then do
 *      nothing but do not return failure.
 *
 * Results:
 *      TRUE on success, FALSE otherwise.
 *
 * Side effects:
 *      One dynamic mode and the pending xinerama state will be updated if
 *      successful.
 *
 *----------------------------------------------------------------------------
 */

static Bool
VMwareCtrlDoSetTopology(ScrnInfoPtr pScrn,
                        xXineramaScreenInfo *extents,
                        unsigned long number)
{
   VMWAREPtr pVMWARE = VMWAREPTR(pScrn);

   if (pVMWARE && pVMWARE->xinerama) { 
      VMWAREXineramaPtr xineramaState;
      short maxX = 0;
      short maxY = 0;
      size_t i;

      if (pVMWARE->xineramaNextState) {
         VmwareLog(("DoSetTopology: Aborting due to existing pending state\n"));
         return TRUE;
      }

      for (i = 0; i < number; i++) {
         maxX = MAX(maxX, extents[i].x_org + extents[i].width);
         maxY = MAX(maxY, extents[i].y_org + extents[i].height);
      }

      VmwareLog(("DoSetTopology: %d %d\n", maxX, maxY));

      xineramaState = (VMWAREXineramaPtr)xcalloc(number, sizeof(VMWAREXineramaRec));
      if (xineramaState) {
         memcpy(xineramaState, extents, number * sizeof (VMWAREXineramaRec));

         xfree(pVMWARE->xineramaNextState);
         pVMWARE->xineramaNextState = xineramaState;
         pVMWARE->xineramaNextNumOutputs = number;

         return VMwareCtrlDoSetRes(pScrn, maxX, maxY, FALSE);
      } else {
         return FALSE;
      }
   } else {
      return FALSE;
   }
}


/*
 *----------------------------------------------------------------------------
 *
 * VMwareCtrlSetTopology --
 *
 *      Implementation of SetTopology command handler. Initialises and sends a
 *      reply.
 *
 * Results:
 *      Standard response codes.
 *
 * Side effects:
 *      Writes reply to client
 *
 *----------------------------------------------------------------------------
 */

static int
VMwareCtrlSetTopology(ClientPtr client)
{
   REQUEST(xVMwareCtrlSetTopologyReq);
   xVMwareCtrlSetTopologyReply rep = { 0, };
   ScrnInfoPtr pScrn;
   ExtensionEntry *ext;
   register int n;
   xXineramaScreenInfo *extents;

   REQUEST_AT_LEAST_SIZE(xVMwareCtrlSetTopologyReq);

   if (!(ext = CheckExtension(VMWARE_CTRL_PROTOCOL_NAME))) {
      return BadMatch;
   }

   pScrn = ext->extPrivate;
   if (pScrn->scrnIndex != stuff->screen) {
      return BadMatch;
   }

   extents = (xXineramaScreenInfo *)(stuff + 1);
   if (!VMwareCtrlDoSetTopology(pScrn, extents, stuff->number)) {
      return BadValue;
   }

   rep.type = X_Reply;
   rep.length = (sizeof(xVMwareCtrlSetTopologyReply) - sizeof(xGenericReply)) >> 2;
   rep.sequenceNumber = client->sequence;
   rep.screen = stuff->screen;
   if (client->swapped) {
      swaps(&rep.sequenceNumber, n);
      swapl(&rep.length, n);
      swapl(&rep.screen, n);
   }
   WriteToClient(client, sizeof(xVMwareCtrlSetTopologyReply), (char *)&rep);

   return client->noClientException;
}


/*
 *----------------------------------------------------------------------------
 *
 * VMwareCtrlDispatch --
 *
 *      Dispatcher for VMWARE_CTRL commands. Calls the correct handler for
 *      each command type.
 *
 * Results:
 *      Standard response codes.
 *
 * Side effects:
 *      Side effects of individual command handlers.
 *
 *----------------------------------------------------------------------------
 */

static int
VMwareCtrlDispatch(ClientPtr client)
{
   REQUEST(xReq);

   switch(stuff->data) {
   case X_VMwareCtrlQueryVersion:
      return VMwareCtrlQueryVersion(client);
   case X_VMwareCtrlSetRes:
      return VMwareCtrlSetRes(client);
   case X_VMwareCtrlSetTopology:
      return VMwareCtrlSetTopology(client);
   }
   return BadRequest;
}


/*
 *----------------------------------------------------------------------------
 *
 * SVMwareCtrlQueryVersion --
 *
 *      Wrapper for QueryVersion handler that handles input from other-endian
 *      clients.
 *
 * Results:
 *      Standard response codes.
 *
 * Side effects:
 *      Side effects of unswapped implementation.
 *
 *----------------------------------------------------------------------------
 */

static int
SVMwareCtrlQueryVersion(ClientPtr client)
{
   register int n;

   REQUEST(xVMwareCtrlQueryVersionReq);
   REQUEST_SIZE_MATCH(xVMwareCtrlQueryVersionReq);

   swaps(&stuff->length, n);

   return VMwareCtrlQueryVersion(client);
}


/*
 *----------------------------------------------------------------------------
 *
 * SVMwareCtrlSetRes --
 *
 *      Wrapper for SetRes handler that handles input from other-endian
 *      clients.
 *
 * Results:
 *      Standard response codes.
 *
 * Side effects:
 *      Side effects of unswapped implementation.
 *
 *----------------------------------------------------------------------------
 */

static int
SVMwareCtrlSetRes(ClientPtr client)
{
   register int n;

   REQUEST(xVMwareCtrlSetResReq);
   REQUEST_SIZE_MATCH(xVMwareCtrlSetResReq);

   swaps(&stuff->length, n);
   swapl(&stuff->screen, n);
   swapl(&stuff->x, n);
   swapl(&stuff->y, n);

   return VMwareCtrlSetRes(client);
}


/*
 *----------------------------------------------------------------------------
 *
 * SVMwareCtrlSetTopology --
 *
 *      Wrapper for SetTopology handler that handles input from other-endian
 *      clients.
 *
 * Results:
 *      Standard response codes.
 *
 * Side effects:
 *      Side effects of unswapped implementation.
 *
 *----------------------------------------------------------------------------
 */

static int
SVMwareCtrlSetTopology(ClientPtr client)
{
   register int n;

   REQUEST(xVMwareCtrlSetTopologyReq);
   REQUEST_SIZE_MATCH(xVMwareCtrlSetTopologyReq);

   swaps(&stuff->length, n);
   swapl(&stuff->screen, n);
   swapl(&stuff->number, n);
   /* Each extent is a struct of shorts. */
   SwapRestS(stuff);

   return VMwareCtrlSetTopology(client);
}


/*
 *----------------------------------------------------------------------------
 *
 * SVMwareCtrlDispatch --
 *
 *      Wrapper for dispatcher that handles input from other-endian clients.
 *
 * Results:
 *      Standard response codes.
 *
 * Side effects:
 *      Side effects of individual command handlers.
 *
 *----------------------------------------------------------------------------
 */

static int
SVMwareCtrlDispatch(ClientPtr client)
{
   REQUEST(xReq);

   switch(stuff->data) {
   case X_VMwareCtrlQueryVersion:
      return SVMwareCtrlQueryVersion(client);
   case X_VMwareCtrlSetRes:
      return SVMwareCtrlSetRes(client);
   case X_VMwareCtrlSetTopology:
      return SVMwareCtrlSetTopology(client);
   }
   return BadRequest;
}


/*
 *----------------------------------------------------------------------------
 *
 * VMwareCtrlResetProc --
 *
 *      Cleanup handler called when the extension is removed.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------------
 */

static void
VMwareCtrlResetProc(ExtensionEntry* extEntry)
{
   /* Currently, no cleanup is necessary. */
}


/*
 *----------------------------------------------------------------------------
 *
 * VMwareCtrl_ExitInit --
 *
 *      Initialiser for the VMWARE_CTRL protocol extension.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Protocol extension will be registered if successful.
 *
 *----------------------------------------------------------------------------
 */

void
VMwareCtrl_ExtInit(ScrnInfoPtr pScrn)
{
   ExtensionEntry *myext;

   if (!(myext = CheckExtension(VMWARE_CTRL_PROTOCOL_NAME))) {
      if (!(myext = AddExtension(VMWARE_CTRL_PROTOCOL_NAME, 0, 0,
                                 VMwareCtrlDispatch,
                                 SVMwareCtrlDispatch,
                                 VMwareCtrlResetProc,
                                 StandardMinorOpcode))) {
         xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "Failed to add VMWARE_CTRL extension\n");
	 return;
      }

      /*
       * For now, only support one screen as that's all the virtual
       * hardware supports.
       */
      myext->extPrivate = pScrn;

      xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                 "Initialized VMWARE_CTRL extension version %d.%d\n",
                 VMWARE_CTRL_MAJOR_VERSION, VMWARE_CTRL_MINOR_VERSION);
   }
}
