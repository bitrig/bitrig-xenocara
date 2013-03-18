/*
 * Copyright © 2008 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Soft-
 * ware"), to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, provided that the above copyright
 * notice(s) and this permission notice appear in all copies of the Soft-
 * ware and that both the above copyright notice(s) and this permission
 * notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABIL-
 * ITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY
 * RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN
 * THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSE-
 * QUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFOR-
 * MANCE OF THIS SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization of
 * the copyright holder.
 *
 * Authors:
 *   Kristian Høgsberg (krh@redhat.com)
 */


#include <stdio.h>
#include <X11/Xlibint.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>
#include <X11/extensions/dri2proto.h>

#include <GL/glx.h>
#include <GL/glxext.h>

#include "dri2.h"

/* Allow the build to work with an older versions of dri2proto.h and
 * dri2tokens.h.
 */
#if DRI2_MINOR < 1
#undef DRI2_MINOR
#define DRI2_MINOR 1
#define X_DRI2GetBuffersWithFormat 7
#endif


static char dri2ExtensionName[] = DRI2_NAME;
static XExtensionInfo *dri2Info;
static XEXT_GENERATE_CLOSE_DISPLAY (DRI2CloseDisplay, dri2Info)

static Bool
DRI2WireToEvent(Display *dpy, XEvent *event, xEvent *wire);
static Status
DRI2EventToWire(Display *dpy, XEvent *event, xEvent *wire);
static int
DRI2Error(Display *display, xError *err, XExtCodes *codes, int *ret_code);

static /* const */ XExtensionHooks dri2ExtensionHooks = {
  NULL,                   /* create_gc */
  NULL,                   /* copy_gc */
  NULL,                   /* flush_gc */
  NULL,                   /* free_gc */
  NULL,                   /* create_font */
  NULL,                   /* free_font */
  DRI2CloseDisplay,       /* close_display */
  DRI2WireToEvent,        /* wire_to_event */
  DRI2EventToWire,        /* event_to_wire */
  DRI2Error,              /* error */
  NULL,                   /* error_string */
};

static XEXT_GENERATE_FIND_DISPLAY (DRI2FindDisplay,
                                   dri2Info,
                                   dri2ExtensionName,
                                   &dri2ExtensionHooks,
                                   0, NULL)

static Bool
DRI2WireToEvent(Display *dpy, XEvent *event, xEvent *wire)
{
   XExtDisplayInfo *info = DRI2FindDisplay(dpy);

   XextCheckExtension(dpy, info, dri2ExtensionName, False);

   switch ((wire->u.u.type & 0x7f) - info->codes->first_event) {
#ifdef X_DRI2SwapBuffers
   case DRI2_BufferSwapComplete:
      /* Ignore swap events if we're not looking for them */
	   printf("BufferSwapComplete\n");
      return False;
#endif
#ifdef DRI2_InvalidateBuffers
   case DRI2_InvalidateBuffers:
	   printf("InvalidateBuffers\n");
      return False;
#endif
   default:
      /* client doesn't support server event */
      break;
   }

   return False;
}

/* We don't actually support this.  It doesn't make sense for clients to
 * send each other DRI2 events.
 */
static Status
DRI2EventToWire(Display *dpy, XEvent *event, xEvent *wire)
{
   XExtDisplayInfo *info = DRI2FindDisplay(dpy);

   XextCheckExtension(dpy, info, dri2ExtensionName, False);

   switch (event->type) {
   default:
      /* client doesn't support server event */
      break;
   }

   return Success;
}

static int
DRI2Error(Display *display, xError *err, XExtCodes *codes, int *ret_code)
{
    if (err->majorCode == codes->major_opcode &&
	err->errorCode == BadDrawable &&
	err->minorCode == X_DRI2CopyRegion)
	return True;

    /* If the X drawable was destroyed before the GLX drawable, the
     * DRI2 drawble will be gone by the time we call
     * DRI2DestroyDrawable.  So just ignore BadDrawable here. */
    if (err->majorCode == codes->major_opcode &&
	err->errorCode == BadDrawable &&
	err->minorCode == X_DRI2DestroyDrawable)
	return True;

    /* If the server is non-local DRI2Connect will raise BadRequest.
     * Swallow this so that DRI2Connect can signal this in its return code */
    if (err->majorCode == codes->major_opcode &&
        err->minorCode == X_DRI2Connect &&
        err->errorCode == BadRequest) {
	*ret_code = False;
	return True;
    }

    return False;
}

Bool
DRI2QueryExtension(Display * dpy, int *eventBase, int *errorBase)
{
   XExtDisplayInfo *info = DRI2FindDisplay(dpy);

   if (XextHasExtension(info)) {
      *eventBase = info->codes->first_event;
      *errorBase = info->codes->first_error;
      return True;
   }

   return False;
}

Bool
DRI2QueryVersion(Display * dpy, int *major, int *minor)
{
   XExtDisplayInfo *info = DRI2FindDisplay(dpy);
   xDRI2QueryVersionReply rep;
   xDRI2QueryVersionReq *req;
   int i, nevents;

   XextCheckExtension(dpy, info, dri2ExtensionName, False);

   LockDisplay(dpy);
   GetReq(DRI2QueryVersion, req);
   req->reqType = info->codes->major_opcode;
   req->dri2ReqType = X_DRI2QueryVersion;
   req->majorVersion = DRI2_MAJOR;
   req->minorVersion = DRI2_MINOR;
   if (!_XReply(dpy, (xReply *) & rep, 0, xFalse)) {
      UnlockDisplay(dpy);
      SyncHandle();
      return False;
   }
   *major = rep.majorVersion;
   *minor = rep.minorVersion;
   UnlockDisplay(dpy);
   SyncHandle();

   switch (rep.minorVersion) {
   case 1:
	   nevents = 0;
	   break;
   case 2:
	   nevents = 1;
	   break;
   case 3:
   default:
	   nevents = 2;
	   break;
   }
	
   for (i = 0; i < nevents; i++) {
       XESetWireToEvent (dpy, info->codes->first_event + i, DRI2WireToEvent);
       XESetEventToWire (dpy, info->codes->first_event + i, DRI2EventToWire);
   }

   return True;
}

Bool
DRI2Connect(Display * dpy, XID window, char **driverName, char **deviceName)
{
   XExtDisplayInfo *info = DRI2FindDisplay(dpy);
   xDRI2ConnectReply rep;
   xDRI2ConnectReq *req;

   XextCheckExtension(dpy, info, dri2ExtensionName, False);

   LockDisplay(dpy);
   GetReq(DRI2Connect, req);
   req->reqType = info->codes->major_opcode;
   req->dri2ReqType = X_DRI2Connect;
   req->window = window;
   req->driverType = DRI2DriverDRI;
   if (!_XReply(dpy, (xReply *) & rep, 0, xFalse)) {
      UnlockDisplay(dpy);
      SyncHandle();
      return False;
   }

   if (rep.driverNameLength == 0 && rep.deviceNameLength == 0) {
      UnlockDisplay(dpy);
      SyncHandle();
      return False;
   }

   *driverName = Xmalloc(rep.driverNameLength + 1);
   if (*driverName == NULL) {
      _XEatData(dpy,
                ((rep.driverNameLength + 3) & ~3) +
                ((rep.deviceNameLength + 3) & ~3));
      UnlockDisplay(dpy);
      SyncHandle();
      return False;
   }
   _XReadPad(dpy, *driverName, rep.driverNameLength);
   (*driverName)[rep.driverNameLength] = '\0';

   *deviceName = Xmalloc(rep.deviceNameLength + 1);
   if (*deviceName == NULL) {
      Xfree(*driverName);
      _XEatData(dpy, ((rep.deviceNameLength + 3) & ~3));
      UnlockDisplay(dpy);
      SyncHandle();
      return False;
   }
   _XReadPad(dpy, *deviceName, rep.deviceNameLength);
   (*deviceName)[rep.deviceNameLength] = '\0';

   UnlockDisplay(dpy);
   SyncHandle();

   return True;
}

Bool
DRI2Authenticate(Display * dpy, XID window, unsigned int magic)
{
   XExtDisplayInfo *info = DRI2FindDisplay(dpy);
   xDRI2AuthenticateReq *req;
   xDRI2AuthenticateReply rep;

   XextCheckExtension(dpy, info, dri2ExtensionName, False);

   LockDisplay(dpy);
   GetReq(DRI2Authenticate, req);
   req->reqType = info->codes->major_opcode;
   req->dri2ReqType = X_DRI2Authenticate;
   req->window = window;
   req->magic = magic;

   if (!_XReply(dpy, (xReply *) & rep, 0, xFalse)) {
      UnlockDisplay(dpy);
      SyncHandle();
      return False;
   }

   UnlockDisplay(dpy);
   SyncHandle();

   return rep.authenticated;
}

void
DRI2CreateDrawable(Display * dpy, XID drawable)
{
   XExtDisplayInfo *info = DRI2FindDisplay(dpy);
   xDRI2CreateDrawableReq *req;

   XextSimpleCheckExtension(dpy, info, dri2ExtensionName);

   LockDisplay(dpy);
   GetReq(DRI2CreateDrawable, req);
   req->reqType = info->codes->major_opcode;
   req->dri2ReqType = X_DRI2CreateDrawable;
   req->drawable = drawable;
   UnlockDisplay(dpy);
   SyncHandle();
}

void
DRI2DestroyDrawable(Display * dpy, XID drawable)
{
   XExtDisplayInfo *info = DRI2FindDisplay(dpy);
   xDRI2DestroyDrawableReq *req;

   XextSimpleCheckExtension(dpy, info, dri2ExtensionName);

   XSync(dpy, False);

   LockDisplay(dpy);
   GetReq(DRI2DestroyDrawable, req);
   req->reqType = info->codes->major_opcode;
   req->dri2ReqType = X_DRI2DestroyDrawable;
   req->drawable = drawable;
   UnlockDisplay(dpy);
   SyncHandle();
}

DRI2Buffer *
DRI2GetBuffers(Display * dpy, XID drawable,
               int *width, int *height,
               unsigned int *attachments, int count, int *outCount)
{
   XExtDisplayInfo *info = DRI2FindDisplay(dpy);
   xDRI2GetBuffersReply rep;
   xDRI2GetBuffersReq *req;
   DRI2Buffer *buffers;
   xDRI2Buffer repBuffer;
   uint32_t *p;
   int i;

   XextCheckExtension(dpy, info, dri2ExtensionName, False);

   LockDisplay(dpy);
   GetReqExtra(DRI2GetBuffers, count * 4, req);
   req->reqType = info->codes->major_opcode;
   req->dri2ReqType = X_DRI2GetBuffers;
   req->drawable = drawable;
   req->count = count;
   p = (uint32_t *) & req[1];
   for (i = 0; i < count; i++)
      p[i] = attachments[i];

   if (!_XReply(dpy, (xReply *) & rep, 0, xFalse)) {
      UnlockDisplay(dpy);
      SyncHandle();
      return NULL;
   }

   *width = rep.width;
   *height = rep.height;
   *outCount = rep.count;

   buffers = Xmalloc(rep.count * sizeof buffers[0]);
   if (buffers == NULL) {
      _XEatData(dpy, rep.count * sizeof repBuffer);
      UnlockDisplay(dpy);
      SyncHandle();
      return NULL;
   }

   for (i = 0; i < rep.count; i++) {
      _XReadPad(dpy, (char *) &repBuffer, sizeof repBuffer);
      buffers[i].attachment = repBuffer.attachment;
      buffers[i].name = repBuffer.name;
      buffers[i].pitch = repBuffer.pitch;
      buffers[i].cpp = repBuffer.cpp;
      buffers[i].flags = repBuffer.flags;
   }

   UnlockDisplay(dpy);
   SyncHandle();

   return buffers;
}


DRI2Buffer *
DRI2GetBuffersWithFormat(Display * dpy, XID drawable,
                         int *width, int *height,
                         unsigned int *attachments, int count, int *outCount)
{
   XExtDisplayInfo *info = DRI2FindDisplay(dpy);
   xDRI2GetBuffersReply rep;
   xDRI2GetBuffersReq *req;
   DRI2Buffer *buffers;
   xDRI2Buffer repBuffer;
   uint32_t *p;
   int i;

   XextCheckExtension(dpy, info, dri2ExtensionName, False);

   LockDisplay(dpy);
   GetReqExtra(DRI2GetBuffers, count * (4 * 2), req);
   req->reqType = info->codes->major_opcode;
   req->dri2ReqType = X_DRI2GetBuffersWithFormat;
   req->drawable = drawable;
   req->count = count;
   p = (uint32_t *) & req[1];
   for (i = 0; i < (count * 2); i++)
      p[i] = attachments[i];

   if (!_XReply(dpy, (xReply *) & rep, 0, xFalse)) {
      UnlockDisplay(dpy);
      SyncHandle();
      return NULL;
   }

   *width = rep.width;
   *height = rep.height;
   *outCount = rep.count;

   buffers = Xmalloc(rep.count * sizeof buffers[0]);
   if (buffers == NULL) {
      _XEatData(dpy, rep.count * sizeof repBuffer);
      UnlockDisplay(dpy);
      SyncHandle();
      return NULL;
   }

   for (i = 0; i < rep.count; i++) {
      _XReadPad(dpy, (char *) &repBuffer, sizeof repBuffer);
      buffers[i].attachment = repBuffer.attachment;
      buffers[i].name = repBuffer.name;
      buffers[i].pitch = repBuffer.pitch;
      buffers[i].cpp = repBuffer.cpp;
      buffers[i].flags = repBuffer.flags;
   }

   UnlockDisplay(dpy);
   SyncHandle();

   return buffers;
}


void
DRI2CopyRegion(Display * dpy, XID drawable, XserverRegion region,
               uint32_t dest, uint32_t src)
{
   XExtDisplayInfo *info = DRI2FindDisplay(dpy);
   xDRI2CopyRegionReq *req;
   xDRI2CopyRegionReply rep;

   XextSimpleCheckExtension(dpy, info, dri2ExtensionName);

   LockDisplay(dpy);
   GetReq(DRI2CopyRegion, req);
   req->reqType = info->codes->major_opcode;
   req->dri2ReqType = X_DRI2CopyRegion;
   req->drawable = drawable;
   req->region = region;
   req->dest = dest;
   req->src = src;

   (void) _XReply(dpy, (xReply *) & rep, 0, xFalse);

   UnlockDisplay(dpy);
   SyncHandle();
}

#ifdef X_DRI2SwapBuffers
static void
load_swap_req(xDRI2SwapBuffersReq *req, uint64_t target, uint64_t divisor,
	     uint64_t remainder)
{
    req->target_msc_hi = target >> 32;
    req->target_msc_lo = target & 0xffffffff;
    req->divisor_hi = divisor >> 32;
    req->divisor_lo = divisor & 0xffffffff;
    req->remainder_hi = remainder >> 32;
    req->remainder_lo = remainder & 0xffffffff;
}

static uint64_t
vals_to_card64(uint32_t lo, uint32_t hi)
{
    return (uint64_t)hi << 32 | lo;
}

uint64_t DRI2SwapBuffers(Display *dpy, XID drawable,
			 uint64_t target_msc, uint64_t divisor, uint64_t remainder)
{
    XExtDisplayInfo *info = DRI2FindDisplay(dpy);
    xDRI2SwapBuffersReq *req;
    xDRI2SwapBuffersReply rep;
    uint64_t count;

    XextCheckExtension (dpy, info, dri2ExtensionName, 0);

    LockDisplay(dpy);
    GetReq(DRI2SwapBuffers, req);
    req->reqType = info->codes->major_opcode;
    req->dri2ReqType = X_DRI2SwapBuffers;
    req->drawable = drawable;
    load_swap_req(req, target_msc, divisor, remainder);

    (void) _XReply(dpy, (xReply *)&rep, 0, xFalse);

    count = vals_to_card64(rep.swap_lo, rep.swap_hi);

    UnlockDisplay(dpy);
    SyncHandle();

    return count;
}
#endif

#ifdef X_DRI2GetMSC
Bool DRI2GetMSC(Display *dpy, XID drawable, uint64_t *ust, uint64_t *msc,
		uint64_t *sbc)
{
    XExtDisplayInfo *info = DRI2FindDisplay(dpy);
    xDRI2GetMSCReq *req;
    xDRI2MSCReply rep;

    XextCheckExtension (dpy, info, dri2ExtensionName, False);

    LockDisplay(dpy);
    GetReq(DRI2GetMSC, req);
    req->reqType = info->codes->major_opcode;
    req->dri2ReqType = X_DRI2GetMSC;
    req->drawable = drawable;

    if (!_XReply(dpy, (xReply *)&rep, 0, xFalse)) {
	UnlockDisplay(dpy);
	SyncHandle();
	return False;
    }

    *ust = vals_to_card64(rep.ust_lo, rep.ust_hi);
    *msc = vals_to_card64(rep.msc_lo, rep.msc_hi);
    *sbc = vals_to_card64(rep.sbc_lo, rep.sbc_hi);

    UnlockDisplay(dpy);
    SyncHandle();

    return True;
}
#endif

#ifdef X_DRI2WaitMSC
static void
load_msc_req(xDRI2WaitMSCReq *req, uint64_t target, uint64_t divisor,
	     uint64_t remainder)
{
    req->target_msc_hi = target >> 32;
    req->target_msc_lo = target & 0xffffffff;
    req->divisor_hi = divisor >> 32;
    req->divisor_lo = divisor & 0xffffffff;
    req->remainder_hi = remainder >> 32;
    req->remainder_lo = remainder & 0xffffffff;
}

Bool DRI2WaitMSC(Display *dpy, XID drawable, uint64_t target_msc, uint64_t divisor,
		 uint64_t remainder, uint64_t *ust, uint64_t *msc, uint64_t *sbc)
{
    XExtDisplayInfo *info = DRI2FindDisplay(dpy);
    xDRI2WaitMSCReq *req;
    xDRI2MSCReply rep;

    XextCheckExtension (dpy, info, dri2ExtensionName, False);

    LockDisplay(dpy);
    GetReq(DRI2WaitMSC, req);
    req->reqType = info->codes->major_opcode;
    req->dri2ReqType = X_DRI2WaitMSC;
    req->drawable = drawable;
    load_msc_req(req, target_msc, divisor, remainder);

    if (!_XReply(dpy, (xReply *)&rep, 0, xFalse)) {
	UnlockDisplay(dpy);
	SyncHandle();
	return False;
    }

    *ust = ((uint64_t)rep.ust_hi << 32) | (uint64_t)rep.ust_lo;
    *msc = ((uint64_t)rep.msc_hi << 32) | (uint64_t)rep.msc_lo;
    *sbc = ((uint64_t)rep.sbc_hi << 32) | (uint64_t)rep.sbc_lo;

    UnlockDisplay(dpy);
    SyncHandle();

    return True;
}
#endif

#ifdef X_DRI2WaitSBC
static void
load_sbc_req(xDRI2WaitSBCReq *req, uint64_t target)
{
    req->target_sbc_hi = target >> 32;
    req->target_sbc_lo = target & 0xffffffff;
}

Bool DRI2WaitSBC(Display *dpy, XID drawable, uint64_t target_sbc, uint64_t *ust,
		 uint64_t *msc, uint64_t *sbc)
{
    XExtDisplayInfo *info = DRI2FindDisplay(dpy);
    xDRI2WaitSBCReq *req;
    xDRI2MSCReply rep;

    XextCheckExtension (dpy, info, dri2ExtensionName, False);

    LockDisplay(dpy);
    GetReq(DRI2WaitSBC, req);
    req->reqType = info->codes->major_opcode;
    req->dri2ReqType = X_DRI2WaitSBC;
    req->drawable = drawable;
    load_sbc_req(req, target_sbc);

    if (!_XReply(dpy, (xReply *)&rep, 0, xFalse)) {
	UnlockDisplay(dpy);
	SyncHandle();
	return False;
    }

    *ust = ((uint64_t)rep.ust_hi << 32) | rep.ust_lo;
    *msc = ((uint64_t)rep.msc_hi << 32) | rep.msc_lo;
    *sbc = ((uint64_t)rep.sbc_hi << 32) | rep.sbc_lo;

    UnlockDisplay(dpy);
    SyncHandle();

    return True;
}
#endif

#ifdef X_DRI2SwapInterval
void DRI2SwapInterval(Display *dpy, XID drawable, int interval)
{
    XExtDisplayInfo *info = DRI2FindDisplay(dpy);
    xDRI2SwapIntervalReq *req;

    XextSimpleCheckExtension (dpy, info, dri2ExtensionName);

    LockDisplay(dpy);
    GetReq(DRI2SwapInterval, req);
    req->reqType = info->codes->major_opcode;
    req->dri2ReqType = X_DRI2SwapInterval;
    req->drawable = drawable;
    req->interval = interval;
    UnlockDisplay(dpy);
    SyncHandle();
}
#endif
