/*
 * Copyright 1997,1998 by UCHIYAMA Yasushi
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of UCHIYAMA Yasushi not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  UCHIYAMA Yasushi makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * UCHIYAMA YASUSHI DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL UCHIYAMA YASUSHI BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <xorg-server.h>
#include <X11/X.h>

#include "compiler.h"

#include "xf86.h"
#include "xf86Priv.h"
#include "xf86_OSlib.h"

#include "xf86Xinput.h"
#include "xf86OSKbd.h"
#include "atKeynames.h"
#include "xf86Keymap.h"

#include <stdio.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/file.h>
#include <assert.h>
#include <mach.h>
#include <sys/ioctl.h>

typedef unsigned short kev_type;		/* kd event type */
typedef unsigned char Scancode;

struct mouse_motion {		
    short mm_deltaX;		/* units? */
    short mm_deltaY;
};

typedef struct {
    kev_type type;			/* see below */
    struct timeval time;		/* timestamp */
    union {				/* value associated with event */
	boolean_t up;		/* MOUSE_LEFT .. MOUSE_RIGHT */
	Scancode sc;		/* KEYBD_EVENT */
	struct mouse_motion mmotion;	/* MOUSE_MOTION */
    } value;
} kd_event;

/* 
 * kd_event ID's.
 */
#define MOUSE_LEFT	1		/* mouse left button up/down */
#define MOUSE_MIDDLE	2
#define MOUSE_RIGHT	3
#define MOUSE_MOTION	4		/* mouse motion */
#define KEYBD_EVENT	5		/* key up/down */

/***********************************************************************
 * Keyboard
 **********************************************************************/
static void 
SoundKbdBell(InputInfoPtr pInfo, int loudness,int pitch,int duration)
{
    return;
}

static void 
SetKbdLeds(InputInfoPtr pInfo, int leds)
{
    return;
}

static int 
GetKbdLeds(InputInfoPtr pInfo)
{
    return 0;
}

static void
KbdGetMapping(InputInfoPtr pInfo, KeySymsPtr pKeySyms, CARD8 *pModMap)
{
    pKeySyms->map        = map;
    pKeySyms->mapWidth   = GLYPHS_PER_KEY;
    pKeySyms->minKeyCode = MIN_KEYCODE;
    pKeySyms->maxKeyCode = MAX_KEYCODE;
    return;
}

static int
KbdOn(InputInfoPtr pInfo, int what)
{
    int data = 1;
    if( ioctl( pInfo->fd, _IOW('k', 1, int),&data) < 0)
	FatalError("Cannot set event mode on keyboard (%s)\n",strerror(errno));
    return Success;
}
static int
KbdOff(InputInfoPtr pInfo, int what)
{
    int data = 2;
    if( ioctl( pInfo->fd, _IOW('k', 1, int),&data) < 0)
	FatalError("can't reset keyboard mode (%s)\n",strerror(errno));
    return Success;
}

static int
KbdInit(InputInfoPtr pInfo, int what)
{
    return Success;
}

static void
ReadInput(InputInfoPtr pInfo)
{
    KbdDevPtr pKbd = (KbdDevPtr) pInfo->private;
    kd_event ke;
    while( read(pInfo->fd, &ke, sizeof(ke)) == sizeof(ke) )
	pKbd->PostEvent(pInfo, ke.value.sc & 0x7f, ke.value.sc & 0x80 ? FALSE : TRUE);
}

static Bool
OpenKeyboard(InputInfoPtr pInfo)
{
    pInfo->fd = xf86Info.consoleFd;
    return TRUE;
}

Bool
xf86OSKbdPreInit(InputInfoPtr pInfo)
{
    KbdDevPtr pKbd = pInfo->private;

    pKbd->KbdInit       = KbdInit;
    pKbd->KbdOn         = KbdOn;
    pKbd->KbdOff        = KbdOff;
    pKbd->Bell          = SoundKbdBell;
    pKbd->SetLeds       = SetKbdLeds;
    pKbd->GetLeds       = GetKbdLeds;
    pKbd->KbdGetMapping = KbdGetMapping;
    pKbd->RemapScanCode = ATScancode;
    pKbd->OpenKeyboard  = OpenKeyboard;
    pKbd->private       = NULL;
    pInfo->read_input   = ReadInput;
    return TRUE;
}
