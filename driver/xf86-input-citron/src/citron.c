/* $Id: citron.c,v 1.3 2009/05/03 13:37:01 matthieu Exp $
 * Copyright (c) 1998  Metro Link Incorporated
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, cpy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE X CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of the Metro Link shall not be
 * used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from Metro Link.
 *
 */

/* $XFree86: xc/programs/Xserver/hw/xfree86/input/citron/citron.c,v 1.4 2000/11/03 13:13:31 tsi Exp $ */

/*
 * Based, in part, on code with the following copyright notice:
 *
 * Copyright 1999-2001 by Thomas Thanner, Citron GmbH, Germany. <support@citron.de>
 * Copyright 1999-2006 by Peter Kunzmann, Citron GmbH, Germany. <kunzmann@citron.de>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is  hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and that
 * the name of Thomas Thanner and Citron GmbH not be used in advertising or
 * publicity pertaining to distribution of the software without specific, written
 * prior permission. Thomas Thanner and Citron GmbH makes no representations about
 * the suitability of this software for any purpose. It is provided "as is"
 * without express or implied warranty.
 *
 * THOMAS THANNER AND CITRON GMBH DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS,
 * IN NO EVENT SHALL THOMAS THANNER OR CITRON GMBH BE LIABLE FOR ANY SPECIAL,
 * INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RSULTING FROM
 * LOSS OF USE, DATA  OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS  ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */
 
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * Citron specific extensions:
 *
 * 1) Configuration file entries:
 *      a) "SleepTime"  Touch idle time in seconds before sleep mode
 *                      (reduced scanning) is entered.
 *                        0 = immediately; 65535=never;
 *                        default=65535
 *      b) "ActivePWM"  PWM duty cycle during regular operation.
 *                        default=255
 *      c) "SleepPWM"   PWM duty cycle during sleep mode.
 *                        default=255
 *      d) "ClickMode"  Button click emulation mode;
 *                        1 = Enter Mode
 *                        2 = Dual Touch Mode
 *                        3 = Dual Exit Mode
 *                        4 = Z-Press Mode
 *						  5 = Z-Press Exit Mode
 *                        default = 1
 *
 * 2) Additional modes in SetMode() function:
 *    (These modes are only activated if the CIT_MODE_EXT macro is defined
 *     at compile time. Until now the mode values are not defined, yet)
 *      a) ClickMode_Enter		set the button click emulation mode to 1
 *      b) ClickMode_Dual		set the button click emulation mode to 2
 *      c) ClickMode_DualExit	set the button click emulation mode to 3
 *      d) ClickMode_ZPress		set the button click emulation mode to 4
 *		e) ClickMode_ZPressExit	set the button click emulation mode to 5
 *
 *!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
/*
 ---------------------------------------------------------------------------
 Revision history:
	   
 Ver	Date		Description of changes								Name
 ---------------------------------------------------------------------------
 2.03	17.09.00	Reconnect when getting breaks changed, changes
 					when powering off the system and reconnecting
					parser for commands from "xcit" added 
					"cit_ParseCommand" to set the variables not
					only on the touch side but also in the priv rec 	pk
 2.04	19.10.00	reconnect enhanced									pk
 2.05	27.02.01	QueryHardware enhanced, connection faster			pk
 2.06	15.03.01	cit_SetBlockDuration added, cit_Flush modified		pk
 2.07	16.03.01	The problem that the touch didn't connect to the 
 					X-server was a hardware problem
 					with the winbond chip (interrupt was dead after
					receiving spurious characters when the touch screen
					is switched on). The SIO has to be reinitialized to
					get rid of that problem. 							pk
 2.08	20.03.01	if cit_GetPacket has an overrun 
 					cit_ReinitSerial has to be called, Vmin set to "1"	
					cit_SuperVisionTimer, cit_SetEnterCount added
					debuglevel, entercount(z) from external now poss.
					enter_touched not reset by coord_exit				pk
 2.09	25.03.01	enter_touched also reset by press_exit, a new 
 					command (SetPWMFreq) for backlight dimming added
 2.10	24.04.01
					command (SendLockZ) for 3-D support added
					userstring commands added							pk
 2.11	07.02.03	Blockduration time raised to 1sec					pk
 2.12	19.11.03	PostButton Event command when breaking beams            pk

 2.13	23.02.05	xf86ScaleAxis width and height bot (-1)                         pk

 2.14	22.03.05	keys, ambient overload and pwmex channels added
					beep for keyclick added                                                         pk
 2.15	23.02.06	minor changes, adapted for shared objects
					if (( res == SUCCESS) ... semicolon deleted
					default_options[] NULL added                       
 ============================================================================

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#define _citron_C_
#define PK	0
#define INITT 0		/* Initialisation of touch in first loop */


char version[]="Touch Driver V2.2.0  (c) 1999-2006 Citron GmbH";

#define CITOUCH_VERSION PACKAGE_VERSION
#define CITOUCH_VERSION_MAJOR PACKAGE_VERSION_MAJOR
#define CITOUCH_VERSION_MINOR PACKAGE_VERSION_MINOR
#define CITOUCH_VERSION_PATCH PACKAGE_VERSION_PATCHLEVEL


/*****************************************************************************
 *	Standard Headers
 ****************************************************************************/

#include "misc.h"
#include "xf86.h"
#define NEED_XF86_TYPES
#include "xf86_OSproc.h"
/*#include "xf86Optrec.h" */
#include "xf86Xinput.h"
#include "xisb.h"
#include "exevents.h"				/* Needed for InitValuator/Proximity stuff*/


#include <X11/keysym.h>


/* I have to explicit declare this function, because I get an error if I compile */
/* with "#define XF86_OS_PRIVS". I also have to put a "SYMVAR(xf86SoundKbdBell)" */
/* statement into the  "/loader/xf86sym.c file" to be able to access this function (PK) */
/* extern void xf86SoundKbdBell(int loudness, int pitch, int duration); */

/* #define CIT_TIM	*/	/* Enable timer */
#define CIT_BEEP		/* enable beep feature */

/*****************************************************************************
 *	Local Headers
 ****************************************************************************/
#include "citron.h"

/*****************************************************************************
 *	Variables without includable headers
 ****************************************************************************/

/*****************************************************************************
 *	defines
 ****************************************************************************/
#define	CIT_DEF_MIN_X	0
#define	CIT_DEF_MAX_X	0xFFFF
#define	CIT_DEF_MIN_Y	0
#define	CIT_DEF_MAX_Y	0xFFFF

#define CIT_BUFFER_SIZE	1024



/******************************************************************************
 * debugging macro
 *****************************************************************************/
#ifdef DBG
#undef DBG
#endif
#ifdef DEBUG
#undef DEBUG
#endif

static int      debug_level = 0;

/*
 debug is always on, because we can set the debug-level
 in XF86Config (Option DebugLevel)
*/
#define DEBUG

#ifdef DEBUG
#define DBG(lvl, f) {if ((lvl) <= debug_level) f;}
#else
#define DBG(lvl, f)
#endif

/* Debugging levels for various routines */
#define PP	5		/* cit_ProcessPacket */
#define RI	6		/* cit_ReadInput */
#define GP	6		/* cit_GetPacket */
#define DDS 5		/* DDS package */
#define DC	5		/* cit_DriverComm */

#define XFREE86_V4

#ifdef XFREE86_V4
#define WAIT(t)												\
    err = xf86WaitForInput(-1, ((t) * 1000));				\
    if (err == -1) {										\
	ErrorF("Citron select error\n");	\
	return !Success;										\
    }
#else
#define WAIT(t)												\
    timeout.tv_sec = 0;										\
    timeout.tv_usec = (t) * 1000;							\
    SYSCALL(err = select(0, NULL, NULL, NULL, &timeout));	\
    if (err == -1) {										\
	ErrorF("Citron select error : %s\n", strerror(errno));	\
	return !Success;										\
    }
#endif




/*****************************************************************************
 *	Local Variables
 ****************************************************************************/


static InputInfoPtr CitronPreInit(InputDriverPtr drv, IDevPtr dev, int flags);


_X_EXPORT InputDriverRec CITRON = {
	1,
	"citron",
	NULL,
	CitronPreInit,
	/*CitronUnInit*/ NULL,
	NULL,
	0
};

static KeySym cikeymap[] =
{
    /* 0x00 .. 0x07 */
    NoSymbol,NoSymbol,NoSymbol,NoSymbol,NoSymbol,NoSymbol,NoSymbol,NoSymbol,
    /* 0x08 .. 0x0f */
    XK_F1,  XK_F2,  XK_F3,  XK_F4,  XK_F5,  XK_F6,  XK_F7,  XK_F8,
    /* 0x10 .. 0x17 */
    XK_F9,  XK_F10, XK_F11, XK_F12, XK_F13, XK_F14, XK_F15, XK_F16,
    /* 0x18 .. 0x1f */
    XK_F17, XK_F18, XK_F19, XK_F20, XK_F21, XK_F22, XK_F23, XK_F24,
    /* 0x20 .. 0x27 */
    XK_F25, XK_F26, XK_F27, XK_F28, XK_F29, XK_F30, XK_F31, XK_F32
};

/* minKeyCode = 8 because this is the min legal key code */
static KeySymsRec keys =
{
  /* map        minKeyCode  maxKC   width */
	cikeymap,	8,			0x27,	1
};



#ifdef XFree86LOADER


/*
 ***************************************************************************
 *
 * Dynamic loading functions
 *
 ***************************************************************************
 */


static XF86ModuleVersionInfo VersionRec =
{
	"citron",					/* name of module */
	MODULEVENDORSTRING,			/* vendor specific string */
	MODINFOSTRING1,				
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,		/* Current Xorg version */
	CITOUCH_VERSION_MAJOR,		/* Module-specific major version */
	CITOUCH_VERSION_MINOR,		/* Module-specific minor version */
	CITOUCH_VERSION_PATCH,		/* Module-specific patch level */
	ABI_CLASS_XINPUT,
	ABI_XINPUT_VERSION,
	MOD_CLASS_XINPUT,
	{0, 0, 0, 0}				/* signature of the version info structure */
};


static char *UserStrNames[] =
{
	"NAME",
	"REV",
	"RECEIVERBOARD",
	"TOUCH",
	"DISPLAY",
	"INVERTER",
	"MECHANICS",
	"PAR1",
	"PAR2",
	NULL
};

/* ************************************************************************
 * [SetupProc] --
 *
 * called when the module subsection is found in XF86Config
 *
 * ************************************************************************/

static pointer
SetupProc(	pointer module,
			pointer options,
			int *errmaj,
			int *errmin )
{
/*	xf86LoaderReqSymLists(reqSymbols, NULL); */
	xf86AddInputDriver(&CITRON, module, 0);
	DBG(5, ErrorF ("%sSetupProc called\n", CI_INFO));

	return (pointer) 1;
}

/*****************************************************************************
 *	[TearDownProc]
 ****************************************************************************/
static void
TearDownProc (pointer p)
{
	DBG(5, ErrorF ("%sTearDownProc Called\n", CI_INFO));
}


_X_EXPORT XF86ModuleData citronModuleData = {
    &VersionRec,
    SetupProc,
    TearDownProc
};

#endif /* XFree86LOADER */





/*
 * Be sure to set vmin appropriately for your device's protocol. You want to
 * read a full packet before returning
 */
static const char *default_options[] =
{

	"BaudRate", 	"19200",
	"StopBits", 	"1",
	"DataBits", 	"8",
	"Parity", 		"None",
/*
  In non-canonical input processing mode, input is not assembled into
  lines and input processing (erase, kill, delete, etc.) does not occur.
  Two parameters control the behavior of this mode: c_cc[VTIME] sets the
  character timer, and c_cc[VMIN] sets the minimum number of characters
  to receive before satisfying the read.
 
  If MIN > 0 and TIME = 0, MIN sets the number of characters to receive
  before the read is satisfied. As TIME is zero, the timer is not used.
 
  If MIN = 0 and TIME > 0, TIME serves as a timeout value. The read will
  be satisfied if a single character is read, or TIME is exceeded (t =
  TIME *0.1 s). If TIME is exceeded, no character will be returned.
 
 
  If MIN > 0 and TIME > 0, TIME serves as an inter-character timer. The
  read will be satisfied if MIN characters are received, or the time
  between two characters exceeds TIME. The timer is restarted every time
  a character is received and only becomes active after the first
  character has been received.
 
  If MIN = 0 and TIME = 0, read will be satisfied immediately. The
  number of characters currently available, or the number of characters
  requested will be returned. According to Antonino (see contributions),
  you could issue a fcntl(fd, F_SETFL, FNDELAY); before reading to get
  the same result.
 
  By modifying newtio.c_cc[VTIME] and newtio.c_cc[VMIN] all modes
  described above can be tested.
  (Copied from serial-programming-howto)
*/


/*	Vmin was set to 3 (Three characters have to arrive until Read_Input is called).
	I set it to 1 because of the winbond SIO (maybe the interrupt disappears after 1 character
	and X waits for another two.
*/

	"Vmin", 		"1",	/* blocking read until 1 chars received */
	"Vtime", 		"1",
	"FlowControl", 	"None",
	"ClearDTR", 	"",
	NULL
};


/*****************************************************************************
 *	Function Definitions
 ****************************************************************************/
static void cit_SendLockZ(cit_PrivatePtr priv);
static int cit_SendString(XISBuffer *b, unsigned char cmd, int cnt, char *ustr);
static Bool cit_GetUserString(cit_PrivatePtr priv, char *ustr_name, char *ustr_cont, Bool scan_flg);



/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/* [xf86CitronFeedback]													*/
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/*  Online driver parameter change										*/
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

static void
cit_SendtoTouch(DeviceIntPtr dev)
{
	LocalDevicePtr		local = (LocalDevicePtr) dev->public.devicePrivate;
    cit_PrivatePtr		priv  = (cit_PrivatePtr)(local->private);
	int i,j;
	unsigned char buf[MAX_BYTES_TO_TRANSFER*2+2];

	DBG(DDS, ErrorF("%scit_SendtoTouch(numbytes=0x%02X, ", CI_INFO,	priv->dds.numbytes));
	
	for(i=0; i<priv->dds.numbytes; i++)
	{
		DBG(DDS, ErrorF("%02x ", priv->dds.data[i]));
	}
	DBG(DDS, ErrorF(")\n"));

	j=0;
	buf[j++] = CTS_STX;			/* transmit start of packet	*/

	for(i=0; i<priv->dds.numbytes; i++)
	{
		if (priv->dds.data[i] >= CTS_CTRLMIN && priv->dds.data[i] <= CTS_CTRLMAX)
		{	/* data has to be encoded	*/
			buf[j++] = CTS_ESC;
			buf[j++] = priv->dds.data[i] | CTS_ENCODE;
		}
		else buf[j++] = priv->dds.data[i];
	}
	buf[j++] = CTS_ETX;		/* end of packet */

	XisbWrite(priv->buffer, buf, j);


	for(i=0; i<j; i++)
	{
		if(i == 0)
		{
			DBG(DDS, ErrorF("%s sent=", CI_INFO));
		}
		else if(i%16 == 0)
		{
			DBG(DDS, ErrorF("\n"));
		}
		DBG(DDS, ErrorF("%02x ",buf[i]));
	}


	DBG(DDS, ErrorF("\n"));
}


static void
cit_ParseCommand(DeviceIntPtr dev)
{
	LocalDevicePtr		local = (LocalDevicePtr) dev->public.devicePrivate;
    cit_PrivatePtr		priv  = (cit_PrivatePtr)(local->private);
	int i;
	unsigned short tmp;

	DBG(DDS, ErrorF("%scit_ParseCommand: numbytes=0x%02X, data= ", CI_INFO, priv->dds.numbytes));

	for(i=0; i<priv->dds.numbytes; i++)
		DBG(DDS, ErrorF("%02x ", priv->dds.data[i]));
		
	DBG(DDS,ErrorF("\n"));

	i=1;
	switch(priv->dds.data[0]&0xff)
	{
		case C_SETPWM:
			priv->pwm_active = priv->dds.data[1];
			priv->pwm_sleep = priv->dds.data[2];
			/* And now a dirty hack */
			priv->dds.data[1] = cit_AdjustBright(priv, priv->dds.data[1]);
			priv->dds.data[2] = cit_AdjustBright(priv, priv->dds.data[2]);
			DBG(DDS, ErrorF("%scit_ParseCommand: PWM Active:%d PWM Sleep:%d \n", CI_INFO, priv->dds.data[1], priv->dds.data[2]));
		break;
		
		case C_SETPWMFREQ:
			priv->pwm_freq = (int)priv->dds.data[1] | (int)(priv->dds.data[2] << 8);
			DBG(DDS, ErrorF("%scit_ParseCommand: PWM Freq:%d\n", CI_INFO, priv->pwm_freq));
		break;
		
		case C_SETSLEEPMODE:
			if(priv->dds.data[1] == 0)
			{
				priv->sleep_time_act = priv->dds.data[2] | (priv->dds.data[3] << 8);
			}
			DBG(DDS, ErrorF("%scit_ParseCommand: Sleep Time act:%d \n", CI_INFO, priv->sleep_time_act));
		break;
		
		case C_SETDOZEMODE:
			if(priv->dds.data[1] == 0)
			{
				priv->doze_time_act = priv->dds.data[2] | (priv->dds.data[3] << 8);
			}
			DBG(DDS, ErrorF("%scit_ParseCommand: Doze Time act:%d \n", CI_INFO, priv->doze_time_act));
		break;
		
		case C_SETAREAPRESSURE:
			priv->button_threshold = priv->dds.data[1];
			DBG(DDS, ErrorF("%scit_ParseCommand: Button Threshold:%d \n", CI_INFO, priv->button_threshold));
		break;

		case C_SETLOCKZ:
			priv->lockz_enter_time = priv->dds.data[1];
			priv->lockz_exit_time = priv->dds.data[2];
			priv->lockz_lock_time = priv->dds.data[3];
			DBG(DDS, ErrorF("%scit_ParseCommand: LockZ: enter %d, exit %d, lock %d \n", CI_INFO,
				priv->lockz_enter_time, priv->lockz_exit_time, priv->lockz_lock_time));
		break;

		case C_SETPWMEX:
			priv->pwmex_channel = priv->dds.data[i++];
			priv->pwmex_duty = priv->dds.data[i++];
			tmp = priv->dds.data[i++];
			tmp += priv->dds.data[i++] << 8;
			priv->pwmex_freq = tmp;
			DBG(DDS, ErrorF("%scit_ParseCommand: channel=%02x, duty cycle=%02x, freqency=%04x\n",
				CI_INFO, priv->pwmex_channel, priv->pwmex_duty, priv->pwmex_freq));
		break;

		default:
			DBG(DDS, ErrorF("%scit_ParseCommand: Command %d [0x%02x] not found\n", CI_INFO, priv->dds.data[0], priv->dds.data[0]));
		break;

	}
}



static void
cit_DriverComm(DeviceIntPtr dev)
{
	LocalDevicePtr		local = (LocalDevicePtr) dev->public.devicePrivate;
    cit_PrivatePtr		priv  = (cit_PrivatePtr)(local->private);
	int i;
	unsigned short tmp;

	DBG(DC, ErrorF("%scit_DriverComm(numbytes=0x%02X, data[1]=%02x, ...)\n", CI_INFO, priv->dds.numbytes, priv->dds.data[1]));

	i=1;
	switch(priv->dds.data[i++])	/* command word */
	{
		case D_SETCLICKMODE:
			priv->click_mode = priv->dds.data[i++];
			ErrorF("%sClick Mode: %d\n", CI_INFO, priv->click_mode);
			cit_SetEnterCount(priv);	/* set enter_count according to click_mode */
		break;
		
		case D_BEEP:
			priv->beep = priv->dds.data[i++];
			ErrorF("%sBeep: %s\n", CI_INFO, (priv->beep > 0) ? "activated":"not activated");
		break;
		
		case D_SETBEEP:
			priv->press_vol = priv->dds.data[i++];
			ErrorF("%sBeep Pressure Volume: %d\n", CI_INFO, priv->press_vol);
			tmp = priv->dds.data[i++];
			tmp += priv->dds.data[i++] << 8;
			priv->press_pitch = tmp;
			ErrorF("%sBeep Pressure Pitch: %d\n", CI_INFO, priv->press_pitch);
			priv->press_dur = priv->dds.data[i++];
			ErrorF("%sBeep Pressure Duration: %d\n", CI_INFO, priv->press_dur);
			priv->rel_vol = priv->dds.data[i++];
			ErrorF("%sBeep Release Volume: %d\n", CI_INFO, priv->rel_vol);
			tmp = priv->dds.data[i++];
			tmp += priv->dds.data[i++] << 8;
			priv->rel_pitch = tmp;
			ErrorF("%sBeep Release Pitch: %d\n", CI_INFO, priv->rel_pitch);
			priv->rel_dur = priv->dds.data[i++];
			ErrorF("%sBeep Release Duration: %d\n", CI_INFO, priv->rel_dur);
		break;
		
		case D_DEBUG:
			debug_level = priv->dds.data[i++];
			ErrorF("%sDebug level set to %d \n", CI_INFO, debug_level);
		break;

		case D_ENTERCOUNT:
			priv->enter_count_no_Z = priv->dds.data[i++];
			cit_SetEnterCount(priv);	/* set enter_count according click_mode */
			ErrorF("%sEnterCount set to %d \n", CI_INFO, priv->enter_count_no_Z);
		break;

		case D_ZENTERCOUNT:
			priv->enter_count_Z = priv->dds.data[i++];
			cit_SetEnterCount(priv);	/* set enter_count according click_mode */
			ErrorF("%sZEnterCount set to %d \n", CI_INFO, priv->enter_count_Z);
		break;

		case D_PWMADJ:
			priv->pwm_src = priv->dds.data[i++];
			priv->pwm_dst = priv->dds.data[i++];
			cit_SendPWM(priv);				/* Set PWM for active and sleep mode */
			ErrorF("%spwm_src=%d, pwm_dst=%d \n", CI_INFO, priv->pwm_src, priv->pwm_dst);
		break;
		
		case D_PWMEX:
			priv->pwmex_channel = priv->dds.data[i++];
			priv->pwmex_duty = priv->dds.data[i++];
			tmp = priv->dds.data[i++];
			tmp += priv->dds.data[i++] << 8;
			priv->pwmex_freq = tmp;
			cit_SendPWMEx(priv);				/* Set PWMex values */
			ErrorF("%sExtended PWM: channel=%02x, duty cycle=%02x, freqency=%04x\n",
				CI_INFO, priv->pwmex_channel, priv->pwmex_duty, priv->pwmex_freq);
		break;

		default:
			ErrorF("%sNot known command: %d [0x%02x] - Get a recent driver\n", CI_WARNING, priv->dds.data[1], priv->dds.data[1]);
		break;		
	}
}


static void
xf86CitronPrint (int nr, LedCtrl *ctrl)
{
	DBG(8, ErrorF("%s------------------------------------------\n", CI_INFO));
	DBG(8, ErrorF("%sxf86CitronFeedback%d(dev, ctrl)\n", CI_INFO, nr));
	DBG(8, ErrorF("%s  ctrl->led_values.......:%d [0x%08lX]\n", CI_INFO, (int)ctrl->led_values, ctrl->led_values));
	DBG(8, ErrorF("%s  ctrl->led_mask.........:%d [0x%08lX]\n", CI_INFO, (int)ctrl->led_mask, ctrl->led_mask));
	DBG(8, ErrorF("%s  ctrl->id...............:%d\n", CI_INFO, ctrl->id));
}


static void 
xf86CitronFeedback0 (DeviceIntPtr dev, LedCtrl *ctrl)
{
	LocalDevicePtr		local = (LocalDevicePtr) dev->public.devicePrivate;
	cit_PrivatePtr		priv  = (cit_PrivatePtr)(local->private);
	COMMAND *cmd;

	DBG(DDS, ErrorF("%sEntering xf86CitronFeedback0()...\n",CI_INFO));

	cmd = (COMMAND *)&ctrl->led_values;

	DBG(DDS, ErrorF("%scmd->packet = %d\n", CI_INFO, cmd->packet));

	
    if(cmd->packet == 0)		/* test if first packet has come (with number of bytes in first byte) */
	{
		if(cmd->par[0] == 0)		/* test if something is to do at all */
		{
			DBG(DDS, ErrorF("%sxf86CitronFeedback0(): Nothing to do\n", CI_WARNING));
			return;
		}
		priv->dds.curbyte = 2;
		priv->dds.numbytes = cmd->par[0];
		priv->dds.data[0] =  cmd->par[1];
		priv->dds.data[1] =  cmd->par[2];
		priv->dds.packet = 1;
	}
    else
	{
		if(priv->dds.packet == cmd->packet)
		{
			priv->dds.data[priv->dds.packet*3-1] = cmd->par[0];
			priv->dds.data[priv->dds.packet*3] = cmd->par[1];
			priv->dds.data[priv->dds.packet*3+1] = cmd->par[2];
			priv->dds.packet++;
			priv->dds.curbyte += 3;
		}
		else
			DBG(DDS, ErrorF("%sPacket error: should be %d is %d\n", CI_WARNING, priv->dds.packet, cmd->packet));

	}
	DBG(DDS, ErrorF("%snumbytes = %d curbyte=%d\n", CI_INFO, priv->dds.numbytes, priv->dds.curbyte));
	if(priv->dds.curbyte >= priv->dds.numbytes)
	{
		if(priv->dds.data[0] == DRIVCOMM)
			cit_DriverComm(dev);			/* process command in the driver */
		else
		{
			cit_ParseCommand(dev);			/* First parse command and set parameters in priv rec */
			cit_SendtoTouch(dev);			/* send message to the touch and execute command there */
		}
	}

	DBG(DDS, ErrorF("%s 1 led_values = %08x\n", CI_INFO, (int)ctrl->led_values));
	ctrl->led_values = (unsigned long)GetTimeInMillis();
	DBG(DDS, ErrorF("%s 2 led_values = %08x\n", CI_INFO, (int)ctrl->led_values));

	DBG(DDS, ErrorF("%sExiting xf86CitronFeedback0()\n",CI_INFO));
}


static void
xf86CitronFeedback1 (DeviceIntPtr dev, LedCtrl *ctrl)
{
	static int test = 0;
	xf86CitronPrint (1, ctrl);
	ctrl->led_values = 0x8765432;
	ctrl->led_mask = test++;
}

static void
xf86CitronFeedback2 (DeviceIntPtr dev, LedCtrl *ctrl)
{
	xf86CitronPrint (2, ctrl);
	ctrl->led_values = (unsigned long)GetTimeInMillis();
	ctrl->led_mask = (unsigned long)GetTimeInMillis()&0xff;
}



#if(PK)
/* Hexdump a number of Words  */
/* len is number of words to dump */
static void hexdump (void *ioaddr, int len)
{
	int i;
	unsigned long *ptr = (unsigned long *)ioaddr;
	 
	ErrorF("  ADDR        0-3       4-7       8-B       C-F\n");
	ErrorF("---------+-----+----+----+----+----+----+----+----\n");

	while (len > 0)
	{
		ErrorF ("%08X: ", (unsigned long)ptr);
						 
		for (i=0;i < ( (len>4)?4:len);i++)
			ErrorF ("  %08x", *ptr++);
		ErrorF ("\n");
		len -= 8;
	}
	
	ErrorF("---------+-----+----+----+----+----+----+----+----\n");
}
#endif


/*****************************************************************************
 *	[cit_StartTimer]
 ****************************************************************************/

static void
cit_StartTimer(cit_PrivatePtr priv, int nr)
{
	priv->timer_ptr[nr] = TimerSet(priv->timer_ptr[nr], 0, priv->timer_val1[nr],
			 priv->timer_callback[nr], (pointer)priv);
	DBG(5, ErrorF ("%scit_StartTimer[%d] called PTR=%08x\n", CI_INFO, nr, (unsigned int)priv->timer_ptr));
}


/*****************************************************************************
 *	[cit_CloseTimer]
 ****************************************************************************/
static void
cit_CloseTimer(cit_PrivatePtr priv, int nr)
{

	DBG(5, ErrorF ("%scit_CloseTimer[%d] called PTR=%08x\n", CI_INFO, nr, (unsigned int)priv->timer_ptr));
	if(priv->timer_ptr[nr])
	{
		TimerFree(priv->timer_ptr[nr]);
		priv->timer_ptr[nr] = NULL;
	}
	else
		DBG(5, ErrorF ("%scit_CloseTimer[%d]: Nothing to close\n", CI_WARNING, nr));
}



/*****************************************************************************
 *	[cit_SuperVisionTimer] If called reset Serial device
 ****************************************************************************/
static CARD32
cit_SuperVisionTimer(OsTimerPtr timer, CARD32 now, pointer arg)
{
	cit_PrivatePtr priv = (cit_PrivatePtr) arg;
    int	sigstate;

	DBG(5, ErrorF ("%scit_SuperVisionTimer called %ld\n", CI_INFO, GetTimeInMillis()));

    sigstate = xf86BlockSIGIO ();
	
	cit_ReinitSerial(priv);

    xf86UnblockSIGIO (sigstate);

	return (0);	/* stop timer */
}


#ifdef CIT_TIM

/*****************************************************************************
 *	[cit_DualTouchTimer]
 ****************************************************************************/
static CARD32
cit_DualTouchTimer(OsTimerPtr timer, CARD32 now, pointer arg)
{
	cit_PrivatePtr priv = (cit_PrivatePtr) arg;
    int	sigstate;

	DBG(5, ErrorF ("%scit_DualTouchTimer called %d\n", CI_INFO, GetTimeInMillis()));

	priv->packet[0] = R_EXIT;	/* build a exit message */
	priv->packet[1] = LOBYTE(priv->raw_x);
	priv->packet[2] = HIBYTE(priv->raw_x);
	priv->packet[3] = LOBYTE(priv->raw_y);
	priv->packet[4] = HIBYTE(priv->raw_y);
	priv->packeti = 5;
	priv->fake_exit = TRUE;
    sigstate = xf86BlockSIGIO ();
	

	priv->local->read_input(priv->local);		/* faking up an exit message */
    xf86UnblockSIGIO (sigstate);

	DBG(3, ErrorF ("%scit_DualTouchTimer: Faking Exit Message Sent\n", CI_INFO));

	return (0);	/* stop timer */
}

#endif

/*****************************************************************************
 *	[CitronPreInit]
 ****************************************************************************/
static InputInfoPtr
CitronPreInit (InputDriverPtr drv, IDevPtr dev, int flags)
{
	LocalDevicePtr local = xf86AllocateInput(drv, 0);
	cit_PrivatePtr priv = (cit_PrivatePtr) xcalloc (1, sizeof (cit_PrivateRec));
	char *s;
#if(INITT)
	int errmaj, errmin;
#endif

	ErrorF ("%sCitronPreInit called - xcalloc=%d\n", CI_INFO, sizeof(cit_PrivateRec));
/*	DBG(2, ErrorF("\txf86Verbose=%d\n", xf86Verbose));*/
	if ((!local) || (!priv))
	{
		ErrorF("%s\t- unable to allocate structures!\n", CI_ERROR);
		goto SetupProc_fail;
	}

	priv->local = local;				/* save local device pointer */


 	/* this results in an xf86strdup that must be freed later */
	local->name = xf86SetStrOption(local->options, "DeviceName", "CiTouch");
	ErrorF("%sDevice name: %s\n", CI_INFO, local->name);

	local->type_name = XI_TOUCHSCREEN;

	/*
	 * Standard setup for the local device record
	 */
	local->device_control = DeviceControl;
	local->read_input = ReadInput;
	local->control_proc = ControlProc;
	local->close_proc = CloseProc;
	local->switch_mode = SwitchMode;
	local->conversion_proc = ConvertProc;
	local->dev = NULL;
	local->private = priv;
	local->private_flags = 0;
	local->history_size = xf86SetIntOption(local->options, "HistorySize", 0);
	local->flags = XI86_POINTER_CAPABLE | XI86_SEND_DRAG_EVENTS;
	local->conf_idev = dev;

	xf86CollectInputOptions(local, default_options, NULL);

/*	xf86OptionListReport(local->options); */



	debug_level = xf86SetIntOption(local->options, "DebugLevel", 0);
	if(debug_level)
	{
#ifdef DEBUG
	    ErrorF("%sDebug level set to %d\n", CI_CONFIG, debug_level);
#else
    	ErrorF("%sDebug not available\n", CI_INFO);
#endif
	}


#if(INITT)


	DBG(5, ErrorF ("%sOpenSerial will be called\n", CI_INFO));

	local->fd = xf86OpenSerial (local->options);
	if (local->fd == -1)
	{
		ErrorF ("%s\t- unable to open device %s\n", CI_ERROR, xf86FindOptionValue (local->options, "Device"));
		goto SetupProc_fail;
	}

	DBG(6, ErrorF("%s\t+ %s opened successfully.\n", CI_INFO, xf86FindOptionValue (local->options, "Device")));
#endif

	/* 
	 * Process the options for the IRT
	 */
	priv->screen_num = xf86SetIntOption(local->options, "ScreenNumber", 0);
	ErrorF("%sAssociated screen: %d\n", CI_CONFIG, priv->screen_num);
	priv->min_x = xf86SetIntOption(local->options, "MinX", CIT_DEF_MIN_X);
	ErrorF("%sMinimum x position: %d\n", CI_CONFIG, priv->min_x);
	priv->max_x = xf86SetIntOption(local->options, "MaxX", CIT_DEF_MAX_X);
	ErrorF("%sMaximum x position: %d\n", CI_CONFIG, priv->max_x);
	priv->min_y = xf86SetIntOption(local->options, "MinY", CIT_DEF_MIN_Y);
	ErrorF("%sMinimum y position: %d\n", CI_CONFIG, priv->min_y);
	priv->max_y = xf86SetIntOption(local->options, "MaxY", CIT_DEF_MAX_Y);
	ErrorF("%sMaximum y position: %d\n", CI_CONFIG, priv->max_y);
	priv->button_number = xf86SetIntOption(local->options, "ButtonNumber", 1);
	ErrorF("%sButton Number: %d\n", CI_CONFIG, priv->button_number);
	priv->proximity_number = xf86SetIntOption(local->options, "ProximityNumber", 2);
	ErrorF("%sProximity Number: %d\n", CI_CONFIG, priv->proximity_number);
	priv->genproxbutev = xf86SetIntOption(local->options, "GenProximityButtonEvents", 0);
	ErrorF("%sGenerate Proximity Button Events: %d\n", CI_CONFIG, priv->genproxbutev);
	priv->button_threshold = xf86SetIntOption(local->options, "ButtonThreshold", 10);
	ErrorF("%sButton Threshold: %d\n", CI_CONFIG, priv->button_threshold);
	priv->sleep_mode = xf86SetIntOption(local->options, "SleepMode", 0);
	ErrorF("%sSleep Mode: %d\n", CI_CONFIG, priv->sleep_mode);
	priv->sleep_time_act = xf86SetIntOption(local->options, "SleepTime", 65535);
	ErrorF("%sSleep Time: %d\n", CI_CONFIG, priv->sleep_time_act);
	priv->sleep_time_scan = xf86SetIntOption(local->options, "SleepScan", 65535);
	ErrorF("%sSleep Scan: %d\n", CI_CONFIG, priv->sleep_time_scan);
	priv->pwm_src = xf86SetIntOption(local->options, "PWMAdjSrc", -1);
	ErrorF("%sPWMAdjSrc: %d\n", CI_CONFIG, priv->pwm_src);
	priv->pwm_dst = xf86SetIntOption(local->options, "PWMAdjDst", -1);
	ErrorF("%sPWMAdjDst: %d\n", CI_CONFIG, priv->pwm_dst);
	priv->pwm_active = xf86SetIntOption(local->options, "PWMActive", 255);
	ErrorF("%sPWM Active: %d\n", CI_CONFIG, priv->pwm_active);
	priv->pwm_sleep = xf86SetIntOption(local->options, "PWMSleep", 255);
	ErrorF("%sPWM Sleep: %d\n", CI_CONFIG, priv->pwm_sleep);
	priv->click_mode = xf86SetIntOption(local->options, "ClickMode", NO_CLICK_MODE);
	ErrorF("%sClick Mode: %d\n", CI_CONFIG, priv->click_mode);
	priv->origin = xf86SetIntOption(local->options, "Origin", 0);
	ErrorF("%sOrigin: %d\n", CI_CONFIG, priv->origin);
	priv->doze_mode = xf86SetIntOption(local->options, "DozeMode", 0);
	ErrorF("%sDoze Mode: %d\n", CI_CONFIG, priv->doze_mode);
	priv->doze_time_act = xf86SetIntOption(local->options, "DozeTime", 10);
	ErrorF("%sDoze Time: %d\n", CI_CONFIG, priv->doze_time_act);
	priv->doze_time_scan = xf86SetIntOption(local->options, "DozeScan", 25);
	ErrorF("%sDoze Scan: %d\n", CI_CONFIG, priv->doze_time_scan);
	priv->delta_x = xf86SetIntOption(local->options, "DeltaX", 0) & 0xff;
	ErrorF("%sDelta X: %d\n", CI_CONFIG, priv->delta_x);
	priv->delta_y = xf86SetIntOption(local->options, "DeltaY", 0) & 0xff;
	ErrorF("%sDelta Y: %d\n", CI_CONFIG, priv->delta_y);
	priv->beep = xf86SetIntOption(local->options, "Beep", 0);
	ErrorF("%sBeep: %s\n", CI_CONFIG, (priv->beep > 0) ? "activated":"not activated");
	priv->press_vol = xf86SetIntOption(local->options, "PressVol", 100);
	ErrorF("%sBeep Pressure Volume: %d\n", CI_CONFIG, priv->press_vol);
	priv->press_pitch = xf86SetIntOption(local->options, "PressPitch", 880);
	ErrorF("%sBeep Pressure Pitch: %d\n", CI_CONFIG, priv->press_pitch);
	priv->press_dur = xf86SetIntOption(local->options, "PressDur", 15) & 0xff;
	ErrorF("%sBeep Pressure Duration: %d\n", CI_CONFIG, priv->press_dur);
	priv->rel_vol = xf86SetIntOption(local->options, "ReleaseVol", 100);
	ErrorF("%sBeep Release Volume: %d\n", CI_CONFIG, priv->rel_vol);
	priv->rel_pitch = xf86SetIntOption(local->options, "ReleasePitch", 1200);
	ErrorF("%sBeep Release Pitch: %d\n", CI_CONFIG, priv->rel_pitch);
	priv->rel_dur = xf86SetIntOption(local->options, "ReleaseDur", 10) & 0xff;
	ErrorF("%sBeep Release Duration: %d\n", CI_CONFIG, priv->rel_dur);

	priv->beepkey = xf86SetIntOption(local->options, "BeepKey", 0);
	ErrorF("%sBeepKey: %s\n", CI_CONFIG, (priv->beep > 0) ? "activated":"not activated");
	priv->presskey_vol = xf86SetIntOption(local->options, "PressVolKey", 50);
	ErrorF("%sBeep Pressure Volume Keyboard: %d\n", CI_CONFIG, priv->press_vol);
	priv->presskey_pitch = xf86SetIntOption(local->options, "PressPitchKey", 1500);
	ErrorF("%sBeep Pressure Pitch Keyboard: %d\n", CI_CONFIG, priv->press_pitch);
	priv->presskey_dur = xf86SetIntOption(local->options, "PressDurKey", 120);
	ErrorF("%sBeep Pressure Duration Keyboard: %d\n", CI_CONFIG, priv->press_dur);
	priv->relkey_vol = xf86SetIntOption(local->options, "ReleaseVolKey", 50);
	ErrorF("%sBeep Release Volume Keyboard: %d\n", CI_CONFIG, priv->rel_vol);
	priv->relkey_pitch = xf86SetIntOption(local->options, "ReleasePitchKey", 1500*2);
	ErrorF("%sBeep Release Pitch Keyboard: %d\n", CI_CONFIG, priv->rel_pitch);
	priv->relkey_dur = xf86SetIntOption(local->options, "ReleaseDurKey", 120/2);
	ErrorF("%sBeep Release Duration Keyboard: %d\n", CI_CONFIG, priv->rel_dur);

	priv->beam_timeout = xf86SetIntOption(local->options, "BeamTimeout", 30) & 0xffff;
	ErrorF("%sBeam Timeout: %d\n", CI_CONFIG, priv->beam_timeout);
	priv->touch_time = xf86SetIntOption(local->options, "TouchTime", 0) & 0xff;
	ErrorF("%sTouch Time: %d\n", CI_CONFIG, priv->touch_time);
	priv->max_dual_count = xf86SetIntOption(local->options, "DualCount", MAX_DUAL_TOUCH_COUNT);
	ErrorF("%sDual Count: %d\n", CI_CONFIG, priv->max_dual_count);
	priv->enter_count_no_Z = xf86SetIntOption(priv->local->options, "EnterCount", 3);
	ErrorF("%sEnterCount: %d\n", CI_CONFIG, priv->enter_count_no_Z);
	priv->enter_count_Z = xf86SetIntOption(priv->local->options, "ZEnterCount", 2);
	ErrorF("%sZEnterCount: %d\n", CI_CONFIG, priv->enter_count_Z);
	priv->pwm_freq = xf86SetIntOption(priv->local->options, "PWMFreq", -1);
	ErrorF("%sPWMFreq: %d\n", CI_CONFIG, priv->pwm_freq);
	priv->lockz_enter_time = xf86SetIntOption(priv->local->options, "LockZEnterTime", 1);
	ErrorF("%sLockZEnterTime: %d\n", CI_CONFIG, priv->lockz_enter_time);
	priv->lockz_exit_time = xf86SetIntOption(priv->local->options, "LockZExitTime", 1);
	ErrorF("%sLockZExitTime: %d\n", CI_CONFIG, priv->lockz_exit_time);
	priv->lockz_lock_time = xf86SetIntOption(priv->local->options, "LockZLockTime", 10);
	ErrorF("%sLockLockTime: %d\n", CI_CONFIG, priv->lockz_lock_time);
	priv->key_matrix = xf86SetIntOption(priv->local->options, "KeyMatrix", 0);
	ErrorF("%sKeyMatrix: %s\n", CI_CONFIG, priv->key_matrix > 0 ? "enabled":"disabled");
	priv->ambient_overload = xf86SetIntOption(priv->local->options, "AmbientOverload", 0);
	ErrorF("%sAmbientOverload: %s\n", CI_CONFIG, priv->ambient_overload > 0 ? "enabled":"disabled");

	cit_SetEnterCount(priv);	/* set enter_count according click_mode */

/* trace the min and max values */
	priv->raw_min_x = CIT_DEF_MAX_X;
	priv->raw_max_x = 0;
	priv->raw_min_y = CIT_DEF_MAX_Y;
	priv->raw_max_y = 0;

#ifdef CIT_TIM
/* preset timer values */
	priv->timer_ptr[FAKE_TIMER] = NULL;
	priv->timer_val1[FAKE_TIMER] = 0;
	priv->timer_val2[FAKE_TIMER] = 0;
	priv->timer_callback[FAKE_TIMER] = NULL;
#endif
	priv->timer_ptr[SV_TIMER] = NULL;
	priv->timer_val1[SV_TIMER] = 0;
	priv->timer_val2[SV_TIMER] = 0;
	priv->timer_callback[SV_TIMER] = NULL;

	priv->fake_exit = FALSE;
	priv->enter_touched = 0;			/* preset */

 	DBG(6, ErrorF("%s\t+ options read\n", CI_INFO));
	s = xf86FindOptionValue (local->options, "ReportingMode");
	if ((s) && (xf86NameCmp (s, "raw") == 0))
		priv->reporting_mode = TS_Raw;
	else
		priv->reporting_mode = TS_Scaled;

#if(INITT)
	/* 
	 * Create an X Input Serial Buffer, because IRT is connected to a serial port
	 */
	priv->buffer = XisbNew (local->fd, CIT_BUFFER_SIZE);
#endif
	priv->proximity = FALSE;
	priv->button_down = FALSE;
	priv->dual_touch_count = 0;
	priv->dual_flg = 0;
	priv->state = 0;
	priv->lex_mode = cit_idle;
	priv->last_x = 0;
	priv->last_y = 0;
	priv->query_state = 0;	/* first query */

#if(INITT)
	DBG (8, XisbTrace (priv->buffer, 1));


	/* 
	 * Verify that the IRT is attached and functional
	 */
	if (QueryHardware (local, &errmaj, &errmin) != Success)
	{
		ErrorF ("%s\t- Unable to query/initialize Citron hardware.\n", CI_INFO);
		goto SetupProc_fail;
	}
#endif

	xf86ProcessCommonOptions(local, local->options);
	local->flags |= XI86_CONFIGURED;
#if(PK)
	if (xf86FindOption (local->options, "DemandLoaded"))
	{
		DBG (5, ErrorF ("%s\tCitron module was demand loaded\n", CI_INFO));
		xf86AddLocalDevice (local, TRUE);
	}
	else
		xf86AddLocalDevice (local, FALSE);
#endif

	if (local->fd >= 0)
	{
		RemoveEnabledDevice (local->fd);
#if(INITT)
		if (priv->buffer)
		{
			XisbFree(priv->buffer);
			priv->buffer = NULL;
		}
		xf86CloseSerial(local->fd);
		local->fd = 0;
		
#endif
	}


	/* return the LocalDevice */
	DBG(5, ErrorF ("%sCitronPreInit success\n", CI_INFO));
	return (local);


	/*
	 * If something went wrong, cleanup and return NULL
	 */
  SetupProc_fail:
#if(INITT)
	if ((local) && (local->fd))
	{
		xf86CloseSerial (local->fd);
		local->fd = 0;
	}
#endif
	if ((local) && (local->name))
		xfree (local->name);
	if (local)
		xfree (local);
#if(INITT)
	if ((priv) && (priv->buffer))
		XisbFree (priv->buffer);
#endif
	if (priv)
		xfree (priv);
	ErrorF ("%sCitronPreInit returning NULL\n", CI_ERROR);
	return (NULL);
}



/*****************************************************************************
 *	[DeviceControl]
 ****************************************************************************/
static Bool
DeviceControl (DeviceIntPtr dev, int mode)
{
	Bool RetVal;

	DBG(5, ErrorF ("%sDeviceControl called; mode = %d\n", CI_INFO, mode));
	switch (mode)
	{
	case DEVICE_INIT:
		DBG(6, ErrorF ("%s\tINIT\n", CI_INFO));
		DeviceInit (dev);
		RetVal = Success;
		break;
	case DEVICE_ON:
		DBG(6, ErrorF ("%s\tON\n", CI_INFO));
		RetVal = DeviceOn (dev);
		break;
	case DEVICE_OFF:
		DBG(6, ErrorF ("%s\tOFF\n", CI_INFO));
		RetVal = DeviceOff (dev);
		break;
	case DEVICE_CLOSE:
		DBG(6, ErrorF ("%s\tCLOSE\n", CI_INFO));
		RetVal = DeviceClose (dev);
		break;
	default:
		ErrorF ("%sDeviceControl Mode (%d) not found\n", CI_ERROR, mode);
		RetVal = BadValue;
	}
	DBG(2, ErrorF ("%sDeviceControl: RetVal = %d\n", CI_INFO, RetVal));
	return(RetVal);
}

/*****************************************************************************
 *	[DeviceOn]
 ****************************************************************************/
static Bool
DeviceOn (DeviceIntPtr dev)
{
	LocalDevicePtr local = (LocalDevicePtr) dev->public.devicePrivate;
	cit_PrivatePtr priv = (cit_PrivatePtr) (local->private);
	int errmaj, errmin;

	DBG(5, ErrorF ("%sDeviceOn called\n", CI_INFO));

	local->fd = xf86OpenSerial(local->options);
	if (local->fd == -1)
	{
		DBG(5, ErrorF("%s%s: cannot open input device\n", CI_ERROR, local->name));
		goto DeviceOn_fail;
	}
	priv->buffer = XisbNew (local->fd, CIT_BUFFER_SIZE);
	if (!priv->buffer)
		goto DeviceOn_fail;

	xf86FlushInput(local->fd);

	cit_SendCommand(priv->buffer, C_SOFTRESET, 0);	/* Make a Reset in case of a connected touch */

	if (QueryHardware (local, &errmaj, &errmin) != Success)
	{
		ErrorF ("%s\t- DeviceOn: Unable to query/initialize hardware.\n", CI_ERROR);
		goto DeviceOn_fail;
	}

	AddEnabledDevice(local->fd);
	dev->public.on = TRUE;
	DBG(5, ErrorF ("%sDeviceOn Success\n", CI_INFO));
	return (Success);

	/*
	 * If something went wrong, cleanup
	 */
  DeviceOn_fail:

	if ((local) && (local->fd))
	{
		xf86CloseSerial (local->fd);
		local->fd = 0;
	}

	if ((local) && (local->name))
		xfree (local->name);
	if (local)
	{
		xfree (local);
		local = NULL;
	}
	if ((priv) && (priv->buffer))
		XisbFree (priv->buffer);
	if (priv)
	{
		xfree (priv);
		priv = NULL;
	}
	ErrorF ("%sDeviceOn failed\n", CI_ERROR);
	return (!Success);
}

/*****************************************************************************
 *	[DeviceOff]
 ****************************************************************************/
static Bool
DeviceOff (DeviceIntPtr dev)
{
	DBG(5, ErrorF ("%sDeviceOff called\n", CI_INFO));
	return DeviceClose(dev);
}

/*****************************************************************************
 *	[DeviceClose]
 ****************************************************************************/
static Bool
DeviceClose (DeviceIntPtr dev)
{
	LocalDevicePtr local = (LocalDevicePtr) dev->public.devicePrivate;
	cit_PrivatePtr priv = (cit_PrivatePtr) (local->private);
	int c;

	DBG(5, ErrorF ("%sDeviceClose called\n",CI_INFO));

	cit_Flush(priv);

	cit_SendCommand(priv->buffer, C_SOFTRESET, 0);
#ifdef CIT_TIM
	cit_CloseTimer(priv, FAKE_TIMER);  		/* Close timer if started */
#endif
	cit_CloseTimer(priv, SV_TIMER);			/* Do not know if it is running, close anyway */

	XisbTrace(priv->buffer, 1); /* trace on */
	cit_SetBlockDuration (priv, 500000);
 	c = XisbRead (priv->buffer);
	if(c == CTS_NAK)
	{
		DBG(6, ErrorF ("%sTouch Reset executed\n",CI_INFO));
	}
	else
	{
		DBG(6, ErrorF ("%sTouch Reset not executed\n",CI_ERROR));
	}


/* Now free all allocated memory */
	if (local->fd >= 0)
	{
		RemoveEnabledDevice (local->fd);
		if (priv->buffer)
		{
			XisbFree(priv->buffer);
			priv->buffer = NULL;
		}
		xf86CloseSerial(local->fd);
		local->fd = 0;
	}

	dev->public.on = FALSE;
	ErrorF("%sx-range = [%d..%d]\n", CI_INFO, priv->raw_min_x, priv->raw_max_x);
	ErrorF("%sy-range = [%d..%d]\n", CI_INFO, priv->raw_min_y, priv->raw_max_y);

	return (Success);
}


/*****************************************************************************
 *	[DeviceInit]
 ****************************************************************************/
static Bool
DeviceInit (DeviceIntPtr dev)
{
	LocalDevicePtr local = (LocalDevicePtr) dev->public.devicePrivate;
	cit_PrivatePtr priv = (cit_PrivatePtr) (local->private);

	unsigned char map[] = {0, 1, 2, 3, 4, 5};

	DBG (5, ErrorF("%sDeviceInit called\n", CI_INFO));
	/* 
	 * these have to be here instead of in the SetupProc, because when the
	 * SetupProc is run and server startup, screenInfo is not setup yet
	 */
	priv->screen_width = screenInfo.screens[priv->screen_num]->width;
	priv->screen_height = screenInfo.screens[priv->screen_num]->height;

	DBG (5, ErrorF("%sScreen Number: %d Screen Width: %d Screen Height: %d\n", CI_INFO,
						priv->screen_num, priv->screen_width, priv->screen_height));

	/* 
	 * Device reports button press for up to 5 buttons.
	 */
	if (InitButtonClassDeviceStruct (dev, 5, map) == FALSE)
	{
		ErrorF ("%sUnable to allocate Citron touchscreen ButtonClassDeviceStruct\n", CI_ERROR);
		return !Success;
	}

	/* 
	 * Device reports motions on 2 axes in absolute coordinates.
	 * Axes min and max values are reported in raw coordinates.
	 * Resolution is computed roughly by the difference between
	 * max and min values scaled from the approximate size of the
	 * screen to fit one meter.
	 * Device may reports touch pressure on the 3rd axis.
	 */
	if (InitValuatorClassDeviceStruct (dev, 2,
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 3
                    xf86GetMotionEvents,
#endif
									local->history_size, Absolute) == FALSE)
	{
		ErrorF ("%sUnable to allocate Citron touchscreen ValuatorClassDeviceStruct\n", CI_ERROR);
		return !Success;
	}
	else
	{
		InitValuatorAxisStruct (dev, 0,
								priv->min_x,	/* min val */
								priv->max_x,	/* max val */
								CIT_DEF_MAX_X,	/* resolution */
								CIT_DEF_MIN_X,	/* min_res */
								CIT_DEF_MAX_X);	/* max_res */
		InitValuatorAxisStruct (dev, 1,
								priv->min_y, 	/* min val */
								priv->max_y,	/* max val */
								CIT_DEF_MAX_Y,	/* resolution */
								CIT_DEF_MIN_Y,	/* min_res */
								CIT_DEF_MAX_Y);	/* max_res */
	}

	if (InitProximityClassDeviceStruct (dev) == FALSE)
	{
		ErrorF ("%sUnable to allocate Citron touchscreen ProximityClassDeviceStruct\n", CI_ERROR);
		return !Success;
	}

	if (InitKeyClassDeviceStruct(dev, &keys, NULL) == FALSE)
	{
		ErrorF("%sUnable to init Key Class Device\n", CI_ERROR);
		return !Success;
	}


	/*
	 * Use the LedFeedbackClass to set some driver parameters
	 */

	/* ID=0 --> Return driver version (RO) */
	
	if (InitLedFeedbackClassDeviceStruct(dev, xf86CitronFeedback0) == FALSE)
	{
		ErrorF("Unable to allocate CITRON touchscreen LedFeedbackClassDeviceStruct, id=0\n");
		return !Success;
	}
	/* ID=1 --> ENTER_COUNT  */
	if (InitLedFeedbackClassDeviceStruct(dev, xf86CitronFeedback1) == FALSE)
	{
		ErrorF("Unable to allocate CITRON touchscreen LedFeedbackClassDeviceStruct, id=1\n");
		return !Success;
	}
	
	/* ID=2 -->   */
	if (InitLedFeedbackClassDeviceStruct(dev, xf86CitronFeedback2) == FALSE)
	{
		ErrorF("Unable to allocate CITRON touchscreen LedFeedbackClassDeviceStruct, id=2\n");
		return !Success;
	}


	/*
	 * Allocate the motion events buffer.
	 */
	xf86MotionHistoryAllocate (local);
	return (Success);
}

/*****************************************************************************
 *	[ReadInput]
 ****************************************************************************/
static void
ReadInput (LocalDevicePtr local)
{
	int x, y;
	int motionevent = 0;
	cit_PrivatePtr priv = (cit_PrivatePtr) (local->private);

 	DBG(RI, ErrorF("%sReadInput called\n", CI_INFO));

	/* 
	 * set blocking to -1 on the first call because we know there is data to
	 * read. Xisb automatically clears it after one successful read so that
	 * succeeding reads are preceeded by a select with a 0 timeout to prevent
	 * read from blocking indefinately.
	 */
	if(!priv->fake_exit)
	{
		cit_SetBlockDuration (priv, -1);
	}
	while (
#ifdef CIT_TIM
			priv->fake_exit ||
#endif
			(cit_GetPacket (priv) == Success))
	{
		cit_ProcessPacket(priv);



		if (priv->reporting_mode == TS_Scaled)
		{
			x = xf86ScaleAxis (priv->raw_x, 0, priv->screen_width-1, priv->min_x,
							   priv->max_x);
			y = xf86ScaleAxis (priv->raw_y, 0, priv->screen_height-1, priv->min_y,
							   priv->max_y);
			DBG(RI, ErrorF("%s\tscaled coordinates: (%d, %d)\n", CI_INFO, x, y));
		}
		else
		{
			x = priv->raw_x;
			y = priv->raw_y;
		}

		xf86XInputSetScreen (local, priv->screen_num, x, y);


		/*
		 * Send events.
		 *
		 * We *must* generate a motion before a button change if pointer
		 * location has changed as DIX assumes this. This is why we always
		 * emit a motion, regardless of the kind of packet processed.
		 * First test if coordinates have changed a predefined amount of pixels
		 */


		/* If the ccordinates are inside the hysteresis, or the coordinates are outside of a 
		 * rectangle with delta_x and delta_y clearence and coordinates have not changed,
		 * no motion event is sent. 
		 * Only one motion event per touch event 
		 */


		if (((x >= (priv->last_x + priv->delta_x)) ||	/* coordinate inside of hyteresis rectangle */
		 	  (x <= (priv->last_x - priv->delta_x)) ||
		 	  (y >= (priv->last_y + priv->delta_y)) ||
		 	  (y <= (priv->last_y - priv->delta_y)))
		  
			||
				
		   ( (((x < priv->delta_x) ||						/* outside of a clearance of delta_x and delta_y */
		 	  (x > (priv->screen_width - priv->delta_x))) ||
			 ((y < priv->delta_y) ||
		 	  (y > (priv->screen_height - priv->delta_y))))

			&&

			  ( (x != priv->last_x) || (y != priv->last_y) ))) /* but only if the coordinates have changed */

		{
        	xf86PostMotionEvent (local->dev, TRUE, 0, 2, x, y);
			motionevent++;				/* motionevent has happened */
			DBG(RI, ErrorF("%s\tPostMotionEvent(x=%d, y=%d, last_x=%d, last_y=%d)\n", CI_INFO,
							 x, y, priv->last_x, priv->last_y));

			priv->last_x = x; 		/* save cooked data */
			priv->last_y = y;
		}


 		if ((priv->proximity == FALSE) && (priv->state & CIT_TOUCHED))
		{
			priv->proximity = TRUE;
			/* We *must* generate a motion before a button change */
			if(motionevent <= 0)	/* send a motion event if not already done */
			{
	        	xf86PostMotionEvent (local->dev, TRUE, 0, 2, x, y);
				motionevent++;		/* motionevent occurered */
			}
			xf86PostProximityEvent (local->dev, 1, 0, 2, x, y);
			if(priv->genproxbutev)
				xf86PostButtonEvent (local->dev, TRUE, priv->proximity_number, 1, 0, 2, x, y);
			DBG(RI, ErrorF("%s\tproximity(TRUE, x=%d, y=%d, genproxbutev=%d, prox_num=%d)\n", 
				CI_INFO, x, y, priv->genproxbutev, priv->proximity_number));
		}

		/* 
		 * Emit a button press or release.
		 */

		if ((priv->button_down == FALSE) && (priv->state & CIT_BUTTON))
		{
			if(priv->enter_touched < priv->enter_count)
				priv->enter_touched++;

			if(priv->enter_touched == priv->enter_count)
			{
				priv->enter_touched++; /* increment count one more time to prevent further enter events */

				if(motionevent <= 0)	/* send a motion event if not already done */
				{
		        	xf86PostMotionEvent (local->dev, TRUE, 0, 2, x, y);
					motionevent++; 
				}

				xf86PostButtonEvent (local->dev, TRUE,
					     priv->button_number, 1, 0, 2, x, y);
				cit_Beep(priv, 1);

				DBG(RI, ErrorF("%s\tPostButtonEvent(DOWN, x=%d, y=%d)\n", CI_INFO, x, y));

				priv->button_down = TRUE;
			}
		}

		if ((priv->button_down == TRUE) && !(priv->state & CIT_BUTTON))
		{

			if(motionevent <= 0)	/* send a motion event if not already done */
			{
	        	xf86PostMotionEvent (local->dev, TRUE, 0, 2, x, y);
				motionevent++;		/* motionevent occurered */
			}
			xf86PostButtonEvent (local->dev, TRUE,
					     priv->button_number, 0, 0, 2, x, y);
			cit_Beep(priv, 0);
			DBG(RI, ErrorF("%s\tPostButtonEvent(UP, x=%d, y=%d)\n", CI_INFO, x, y));
			priv->button_down = FALSE;
		}


		/* 
		 * the untouch should always come after the button release
		 */
		if ((priv->proximity == TRUE) && !(priv->state & CIT_TOUCHED))
		{
			priv->proximity = FALSE;

			if(motionevent <= 0)	/* send a motion event if not already done */
			{
	        	xf86PostMotionEvent (local->dev, TRUE, 0, 2, x, y);
				motionevent++;		/* motionevent occurered */
			}

			xf86PostProximityEvent (local->dev, 0, 0, 2, x, y);
			if(priv->genproxbutev)
				xf86PostButtonEvent (local->dev, TRUE, priv->proximity_number, 0, 0, 2, x, y);
			DBG(RI, ErrorF("%s\tproximity(FALSE, x=%d, y=%d, genproxbutev=%d, prox_num=%d)\n", 
				CI_INFO, x, y, priv->genproxbutev, priv->proximity_number));
		}
		


		/*
		 * Any key changed ? 
		 */

		if (priv->key_changed == TRUE)
		{
			xf86PostKeyEvent(local->dev, priv->key_number, (priv->state & CIT_KEYPRESSED) ? TRUE : FALSE, TRUE, 0, 2, x, y);
/*			xf86PostKeyEvent(local->dev, 0x10, FALSE, TRUE, 0, 2, y, x); */
		
			DBG (RI, ErrorF ("%s\tPostKeyEvent: key %d %s\n",
						CI_INFO, priv->key_number,
						(priv->state & CIT_KEYPRESSED) ? "Pressed" : "Released"));

			cit_BeepKey(priv, (priv->state & CIT_KEYPRESSED) ? 1 : 0);
			priv->key_changed = FALSE;
		}


		/*
		 * Ambient light overflow ? 
		 */

		if (priv->ambientoverflow_changed == TRUE)
		{
			xf86PostKeyEvent(local->dev, AMBIENTOFLOW_KEY, (priv->state & CIT_AMBIENTOVERFLOW) ? TRUE : FALSE, TRUE, 0, 2, x, y);
/*			xf86PostKeyEvent(local->dev, 0x10, FALSE, TRUE, 0, 2, y, x); */
		
			DBG (RI, ErrorF ("%s\tPostKeyEvent: key %d %s\n",
						CI_INFO, AMBIENTOFLOW_KEY,
						(priv->state & CIT_AMBIENTOVERFLOW) ? "Overflow" : "No Overflow"));

			priv->ambientoverflow_changed = FALSE;
		}


		DBG (RI, ErrorF ("%sTouchScreen: x(%d), y(%d), %s, %d motion events\n",
						CI_INFO, x, y,
						(priv->state == CIT_TOUCHED) ? "Touched" : "Released", motionevent));


#ifdef CIT_TIM
		DBG (RI, ErrorF ("%s\tFAKE: YES\n", CI_INFO));
		if(priv->fake_exit)
		{
			priv->fake_exit = FALSE;		/* do not sent any further faked exit messages */
			return;
		}
#endif
	}
 	DBG(RI, ErrorF("%sExit ReadInput\n", CI_INFO));
}


/*****************************************************************************
 *	[cit_GetPacket]
 ****************************************************************************/
static Bool
cit_GetPacket (cit_PrivatePtr priv)
{
	int c;
	int errmaj, errmin;
	int loop = 0;
	DBG(GP, ErrorF("%scit_GetPacket called\n", CI_INFO));
	DBG(GP, ErrorF("%s\t* initial lex_mode =%d (%s)\n", CI_INFO, priv->lex_mode,
			    priv->lex_mode==cit_idle	?"idle":
			    priv->lex_mode==cit_getID	?"getID":
			    priv->lex_mode==cit_collect	?"collect":
			    priv->lex_mode==cit_escape	?"escape":
			    "???"));
	while ((c = XisbRead (priv->buffer)) >= 0)
	{
#if(0)		
		DBG(GP, ErrorF("%s c=%d\n",CI_INFO, c));
#endif
		loop++;
	 	if (c == CTS_STX)
		{
			DBG(GP, ErrorF("%s\t+ STX detected\n", CI_INFO));
			/* start of report received */
			if (priv->lex_mode != cit_idle)
				DBG(7, ErrorF("%s\t- no ETX received before this STX!\n", CI_WARNING));
			priv->lex_mode = cit_getID;
			DBG(GP, ErrorF("%s\t+ new lex_mode == getID\n", CI_INFO));
			/* Start supervision timer at the beginning of a command */
			priv->timer_val1[SV_TIMER] = 2000;		/* Timer delay [ms] 2s */
			priv->timer_callback[SV_TIMER] = (OsTimerCallback)cit_SuperVisionTimer;		/* timer callback routine	*/
			cit_StartTimer(priv, SV_TIMER);			

		}
		else if (c == CTS_ETX)
		{
			DBG(GP, ErrorF("%s\t+ ETX detected\n", CI_INFO));
			/* end of command received */
			/* always IDLE after report completion */
			DBG(GP, ErrorF("%s\t+ new lex_mode == idle\n", CI_INFO));
			if (priv->lex_mode == cit_collect)
			{
			    DBG(GP, ErrorF("%s\t+ Good report received\n", CI_INFO));
			    priv->lex_mode = cit_idle;
				cit_CloseTimer(priv, SV_TIMER);			/* stop supervision */
			    return (Success);
			}
			DBG(GP, ErrorF("%s\t- unexpected ETX received!\n", CI_WARNING));
			priv->lex_mode = cit_idle;
		}
		else if (c == CTS_ESC)
		{
			DBG(GP, ErrorF("%s\t+ escape detected\n", CI_INFO));
			/* next character is encoded */
			if (priv->lex_mode != cit_collect)
			{
				DBG(GP, ErrorF("%s\t- unexpected control character received\n", CI_WARNING));
			}
			else
			{
				priv->lex_mode = cit_escape;
				DBG(GP, ErrorF("%s\t+ new lex_mode == escape\n", CI_INFO));
			}
		}
		else if ((c < CTS_CTRLMIN) || (c > CTS_CTRLMAX))
		{
			/* regular report data received */
			if (priv->lex_mode == cit_getID)
			{	/* receive report ID */
				priv->packeti = 0;
				priv->packet[priv->packeti++] = (unsigned char)c;
				priv->lex_mode = cit_collect;
				DBG(GP, ErrorF("%s\t+ identifier captured, new lex_mode == collect\n", CI_INFO));
			}
			else if ((priv->lex_mode == cit_collect) || (priv->lex_mode == cit_escape))
			{	/* receive command data */
				if (priv->lex_mode == cit_escape)
				{	/* decode encoded data byte */
					c &= CTS_DECODE;	/* decode data */
					priv->lex_mode = cit_collect;
					DBG(GP, ErrorF("%s\t+ decoded character = 0x%02X\n", CI_INFO, c));
					DBG(GP, ErrorF("%s\t+ new lex_mode = collect\n", CI_INFO));
				}
				if (priv->packeti < CTS_PACKET_SIZE)
				{	/* add data bytes to buffer */
					priv->packet[priv->packeti++] = (unsigned char)c;
				}
				else
				{
					DBG(GP, ErrorF("%s\t- command buffer overrun, loop[%d]\n", CI_ERROR, loop));
					/* let's reinitialize the touch - maybe it sends breaks */
					/* The touch assembles breaks until the buffer has an overrun */
					/* 100ms x 256 -> 26 seconds */
					
					priv->lex_mode = cit_idle;
					cit_ReinitSerial(priv);

				}
			}
			else
			{
				/* this happens e.g. when the touch sends breaks, so we try to reconnect */
				DBG(GP, ErrorF("%s\t- unexpected non control received! [%d, 0x%02x, loop[%d]]\n", CI_WARNING, c, c, loop));
				DBG(GP, ErrorF("%s\t- Device not connected - trying to reconnect ...\n", CI_WARNING));
				if (QueryHardware (priv->local, &errmaj, &errmin) != Success)
					ErrorF ("%s\t- Unable to query/initialize Citron Touch hardware.\n", CI_ERROR);
				else
					ErrorF ("%s\t- Citron Touch reconnected\n", CI_INFO);

				return(!Success);
			}
		}
		else if (c != CTS_XON && c != CTS_XOFF)
		{
			DBG(GP, ErrorF("%s\t- unhandled control character received! loop[%d]\n", CI_WARNING, loop));
		}
	} /* end while */
	DBG(GP, ErrorF("%scit_GetPacket exit !Success - loop[%d]\n", CI_INFO, loop));

	return (!Success);
}



/*****************************************************************************
 *	[cit_ProcessPacket]
 ****************************************************************************/
static void cit_ProcessPacket(cit_PrivatePtr priv)
{
	int	i;

	DBG(PP, ErrorF("%scit_ProcessPacket called\n", CI_INFO));
	DBG(PP, ErrorF("%s\t+ enter state = 0x%04X, dual touch count=%d\n", CI_INFO, priv->state, priv->dual_touch_count));
	/* examine message identifier */

	priv->dual_flg = TRUE;			/* Dual Touch Error occurred */
#ifdef CIT_TIM
	priv->timer_val1[FAKE_TIMER] = 1000;		/* Timer delay [ms]*/
	priv->timer_callback[FAKE_TIMER] = (OsTimerCallback)cit_DualTouchTimer;		/* timer callback routine	*/
	cit_StartTimer(priv, FAKE_TIMER);			
#endif

	switch (priv->packet[0])
	{
		case R_COORD:	/* new touch coordinates received */
			if (priv->packeti < 5)
			{
				DBG(PP, ErrorF("%s\t- coordinate message packet too short (%d bytes)\n", CI_ERROR, priv->packeti));
				break;
			}


			if (priv->dual_touch_count > 0)
				priv->dual_touch_count--;

			priv->raw_x = 0x0001U * priv->packet[1]
				 	  	+ 0x0100U * priv->packet[2];
			priv->raw_y = 0x0001U * priv->packet[3]
				  		+ 0x0100U * priv->packet[4];

			priv->raw_min_x = min(priv->raw_min_x, priv->raw_x);
			priv->raw_max_x = max(priv->raw_max_x, priv->raw_x);
			priv->raw_min_y = min(priv->raw_min_y, priv->raw_y);
			priv->raw_max_y = max(priv->raw_max_y, priv->raw_y);

			priv->state |= CIT_TOUCHED;

			DBG(PP, ErrorF("%s\t+ COORD message raw (%d,%d)\n", CI_INFO, priv->raw_x, priv->raw_y));
			break;
		
		case R_EXIT:	/* touch area no longer interrupted */
			if (priv->packeti < 5)
			{
				DBG(PP, ErrorF("%s\t- exit message packet too short (%d bytes)\n", CI_ERROR, priv->packeti));
				break;
			}

			priv->state &= ~(CIT_TOUCHED | CIT_PRESSED);
			priv->dual_touch_count = 0;
			priv->enter_touched = 0;		/* reset coordinate report counter */
			priv->raw_x = 0x0001U * priv->packet[1]
						+ 0x0100U * priv->packet[2];
			priv->raw_y = 0x0001U * priv->packet[3]
						+ 0x0100U * priv->packet[4];
#ifdef CIT_TIM
			cit_CloseTimer(priv, FAKE_TIMER);	/* close timer if exit message was received */
#endif
			DBG(PP, ErrorF("%s\t+ EXIT message (%d,%d)\n", CI_INFO, priv->raw_x, priv->raw_y));
			break;
		
		case R_DUALTOUCHERROR:
			if (priv->dual_touch_count < priv->max_dual_count)
				priv->dual_touch_count++;
			DBG(PP, ErrorF("%s\t+ DUAL TOUCH ERROR message received\n", CI_INFO));
			break;
		
		case R_PRESSURE:	/* pressure message received */
			if (priv->packeti < 2)
			{
				DBG(PP, ErrorF("%s\t- pressure message packet too short (%d bytes)\n", CI_ERROR, priv->packeti));
				break;
			}
			priv->state |= CIT_TOUCHED;
			if (priv->packet[1] == PRESS_EXCEED)
				priv->state |= CIT_PRESSED;
			else if(priv->packet[1] == PRESS_BELOW)
			{
				priv->enter_touched = 0;		/* reset coordinate report counter */
				priv->state &= ~CIT_PRESSED;
			}
			else
				DBG(PP, ErrorF("%sPressure Message Error\n", CI_ERROR));

			DBG(PP, ErrorF("%s\t+ pressure %s message\n", CI_INFO, priv->packet[1] ? "enter":"exit"));
			break;

		case R_KEYCHANGE:	/* Key state has changed */
			if (priv->packeti < CTS_KEYCHANGE_LEN)
			{
				DBG(PP, ErrorF("%s\t+ keychange message packet too short (%d bytes)\n", CI_ERROR, priv->packeti));
				break;
			}

			priv->key_changed = TRUE;
			priv->key_number = (int)priv->packet[1];

			if(priv->packet[2])
				priv->state |= CIT_KEYPRESSED;
			else
				priv->state &= ~CIT_KEYPRESSED; 

			DBG(PP, ErrorF("%s\t+ key=%d state=%d \n", CI_INFO, priv->packet[1], priv->packet[2]));
			
			break;
		

		case R_AMBIENTOVERLOADERROR:	/* Ambient light overload error*/
			if (priv->packeti < 2)
			{
				DBG(PP, ErrorF("%s\t- ambient overload error message packet too short (%d bytes)\n", CI_ERROR, priv->packeti));
				break;
			}
			
			priv->ambientoverflow_changed = TRUE;

			if(priv->packet[1])
				priv->state |= CIT_AMBIENTOVERFLOW;
			else
				priv->state &= ~CIT_AMBIENTOVERFLOW; 

			DBG(PP, ErrorF("%s\t- Ambient overflow %s limit\n", CI_INFO, priv->state & CIT_AMBIENTOVERFLOW ? "exceeds" : "below"));
			break;

		default:
			DBG(PP, ErrorF("%s\t* unhandled message:", CI_ERROR));
			for (i=0; i<priv->packeti; i++)
			{
				DBG(PP, ErrorF(" 0x%02X", priv->packet[i]));
			}
			DBG(PP, ErrorF("\n"));
	}
	/* generate button state */
	switch (priv->click_mode)
	{
		case CM_ZPRESS:
			DBG(PP, ErrorF("%s\t+ ZPress, button ", CI_INFO));
			if (priv->state & CIT_PRESSED)
			{
				priv->state |= CIT_BUTTON;
				DBG(PP, ErrorF("down"));
			}
			else
			{
				priv->state &= ~CIT_BUTTON;
				DBG(PP, ErrorF("up"));
			}

			break;

		case CM_ZPRESSEXIT:
			DBG(PP, ErrorF("%s\t+ ZPressExit, button ", CI_INFO));
			if (priv->state & CIT_PRESSED)
			{
				priv->state |= CIT_BUTTON;
				DBG(PP, ErrorF("down"));
			}
			else if (!(priv->state & CIT_TOUCHED))
			{
				priv->state &= ~CIT_BUTTON;
				DBG(PP, ErrorF("up"));
			}
			break;

		case CM_DUAL:
			DBG(PP, ErrorF("%s\t+ Dual Touch, button ", CI_INFO));
			if ((priv->dual_touch_count == priv->max_dual_count) && (priv->state & CIT_TOUCHED))
			{
				priv->state |= CIT_BUTTON;
				DBG(PP, ErrorF("down"));
			}
			else if (priv->dual_touch_count == 0)
			{
				priv->state &= ~CIT_BUTTON;
				DBG(PP, ErrorF("up"));
			}
			break;
		
		case CM_DUALEXIT:
			DBG(PP, ErrorF("%s\t+ Dual Exit, button ", CI_INFO));
			if ((priv->dual_touch_count == priv->max_dual_count) && (priv->state & CIT_TOUCHED))
			{
				priv->dual_flg = TRUE;
				priv->state |= CIT_BUTTON;
				DBG(PP, ErrorF("down"));
			}
			else if (!(priv->state & CIT_TOUCHED))
			{
				priv->state &= ~CIT_BUTTON;
				DBG(PP, ErrorF("up"));
			}
			break;

		default:	/* default to enter mode */
			DBG(PP, ErrorF("%s\t+ Enter Mode, button ", CI_INFO));
			if (priv->state & CIT_TOUCHED)
			{
				priv->state |= CIT_BUTTON;
				DBG(PP, ErrorF("down"));
			}
			else
			{
				priv->state &= ~CIT_BUTTON;
				DBG(PP, ErrorF("up"));
			}
			break;
	}
	DBG(PP, ErrorF("\n"));
	DBG(PP, ErrorF("%s\t+ Click Mode=%d\n", CI_INFO, priv->click_mode));
	DBG(PP+1, ErrorF("%s\t+ exit state  = 0x%04X, dual touch count=%d\n", CI_INFO, priv->state, priv->dual_touch_count));
	DBG(PP+1, ErrorF("%s\t  raw_x=%d, raw_y=%d\n", CI_INFO, priv->raw_x, priv->raw_y));
}






/*****************************************************************************
 *	[ControlProc]
 ****************************************************************************/
static int
ControlProc (LocalDevicePtr local, xDeviceCtl * control)
{
	xDeviceAbsCalibCtl *c = (xDeviceAbsCalibCtl *) control;
	cit_PrivatePtr priv = (cit_PrivatePtr) (local->private);

 	DBG(5, ErrorF("%sControlProc called\n", CI_INFO));

	priv->min_x = c->min_x;
	priv->max_x = c->max_x;
	priv->min_y = c->min_y;
	priv->max_y = c->max_y;


	return (Success);
}

/*****************************************************************************
 *	[CloseProc]
 ****************************************************************************/
static void
CloseProc (LocalDevicePtr local)
{
 	DBG(5, ErrorF("%sCloseProc called\n", CI_INFO));
}

/*****************************************************************************
 *	[SwitchMode]
 ****************************************************************************/
static int
SwitchMode (ClientPtr client, DeviceIntPtr dev, int mode)
{
	LocalDevicePtr local = (LocalDevicePtr) dev->public.devicePrivate;
	cit_PrivatePtr priv = (cit_PrivatePtr) (local->private);
 	DBG(5, ErrorF("%sSwitchMode called; mode = %d\n", CI_INFO, mode));
	if ((mode == TS_Raw) || (mode == TS_Scaled))
	{
		priv->reporting_mode = mode;
		DBG(6, ErrorF("%s\treporting mode = %s\n", CI_INFO, mode==TS_Raw?"raw":"scaled"));
		return (Success);
	}
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) == 0
	else if ((mode == SendCoreEvents) || (mode == DontSendCoreEvents))
	{
		xf86XInputSetSendCoreEvents (local, (mode == SendCoreEvents));
		DBG(6, ErrorF("%s\tmode = %sSend Core Events\n", CI_INFO, mode==DontSendCoreEvents?"Don\'t ":""));
		return (Success);
	}
#endif
#ifdef CIT_MODE_EXT
	else if (mode == ClickMode_Enter)
	{
		priv->click_mode = CM_ENTER;
		DBG(6, ErrorF("%s\tset click mode to ENTER\n", CI_INFO));
		return (Success);
	}
	else if (mode == ClickMode_Dual)
	{
		priv->click_mode = CM_DUAL;
		DBG(6, ErrorF("%s\tset click mode to DUAL TOUCH\n", CI_INFO));
		return (Success);
	}
	else if (mode == ClickMode_ZPress)
	{
		priv->click_mode = CM_ZPRESS;
		DBG(6, ErrorF("%s\tset click mode to Z-Press\n", CI_INFO));
		return (Success);
	}
#endif
	else
	{
		ErrorF("%sUnknown mode for Citron Touchscreen Switchmode Function: 0x%02x!\n", CI_ERROR, mode);
		return (!Success);
	}
}

/*****************************************************************************
 *	[ConvertProc]
 ****************************************************************************/
static Bool
ConvertProc (LocalDevicePtr local,
			 int first,
			 int num,
			 int v0,
			 int v1,
			 int v2,
			 int v3,
			 int v4,
			 int v5,
			 int *x,
			 int *y)
{
	cit_PrivatePtr priv = (cit_PrivatePtr) (local->private);

 	DBG(5, ErrorF("%sConvertProc called(first=%d, num=%d, v0=%d, v1=%d, v2=%d, v3=%d\n",
					CI_INFO, first, num, v0, v1, v2, v3));
	if (priv->reporting_mode == TS_Raw)
	{
		*x = xf86ScaleAxis (v0, 0, priv->screen_width-1, priv->min_x,
							priv->max_x);
		*y = xf86ScaleAxis (v1, 0, priv->screen_height-1, priv->min_y,
							priv->max_y);
	}
	else
	{
		*x = v0;
		*y = v1;
	}
	DBG(6, ErrorF("%s\t+ x=%d, y=%d\n",CI_INFO, *x, *y));
	return (TRUE);
}

/*****************************************************************************
 *	[QueryHardware]
 ****************************************************************************/
static Bool
QueryHardware (LocalDevicePtr local, int *errmaj, int *errmin)
{
	cit_PrivatePtr	priv = (cit_PrivatePtr) (local->private);
	unsigned char	x;
	int		i, cnt;
	int err;		/* for WAIT */
	char UsrStr[CTS_USERSTRING_LEN+1];

	/* Reset the IRT from any mode and wait for end of warmstart */
	DBG(5, ErrorF("%sQueryHardware called\n", CI_INFO));



/* Will not work with XFree86 4.0.1 */
/*	xf86SerialSendBreak (local->fd, 2); */
	cit_Flush(priv); 



	/*
	 * IRT signals end of startup by sending BREAKS with 100 ms length.
	 * wait a maximum of 2 seconds for at least 2 consecutive breaks
	 * to be sure the IRT is really initialized
	*/
	cit_Flush(priv); /* clear the buffer and wait for break */
	DBG(6, ErrorF("%s\t* waiting for BREAKS...\n", CI_INFO));
	
	cit_SetBlockDuration (priv, 1);

	for (i=0, cnt=0; (i<20) && (cnt<3); i++)
	{
		cit_Flush(priv); /* clear the buffer and wait for break */
		WAIT(150);	/* wait a little bit longer than 100 ms */
		DBG(7, ErrorF("%s\t (loop %d)\n", CI_INFO, i));
		if (XisbRead(priv->buffer) == 0)
		{
			cnt++;
			DBG(6, ErrorF("%s\t+ BREAK %d detected\n", CI_INFO, cnt));
		}
		else
		{
			cnt = 0;
		}
		if ( i == 12)
		{
			cit_SendCommand(priv->buffer, C_SOFTRESET, 0);
			DBG(6, ErrorF("%s\t+ SOFTRESET sent\n", CI_INFO));
		}
#if(0)
		/* maybe we are in Debug Mode and have to sent a soft reset */
		if(i == 15)
		{
			XisbWrite(priv->buffer, (unsigned char *)"r2", 2);
			x = 0x0d;
			XisbWrite(priv->buffer, &x, 1);
			DBG(6, ErrorF("%s\t+ DEBUG-MODE SOFTRESET sent\n", CI_INFO));
		}
#endif
	}
	if (cnt < 2)
	{
		ErrorF("%sCannot reset Citron Infrared Touch!\n", CI_ERROR);

/*	Workaround - bugfix
	Normally when the touch is connected and the driver thinks it didn't sent
	breaks we have the problem, that spurious characters, which are sent at
	the startup of the touch screen	make the winbond SIO chip not to sent
	any further interrupts. To overcome	this problem we reinitialize the
	winbond chip with a close and open of the serial line.
	Thanks a lot winbond team. Without it I wouldn't have
	written these wonderful lines of source code ;-).
*/ 
		cit_ReinitSerial(priv);
		return (!Success);
	}
	/* Now initialize IRT to CTS Protocol */
	DBG(6, ErrorF("%s\t* initializing to CTS mode\n", CI_INFO));
	x = 0x0d;
	for (i=0; i<2; i++)
	{
		XisbWrite(priv->buffer, &x, 1);
		WAIT(50);
	}
	x = MODE_D;
	XisbWrite(priv->buffer, &x, 1);

	/* wait max. 0.5 seconds for acknowledge */
	DBG(6, ErrorF("%s\t* waiting for acknowledge\n", CI_INFO));
	cit_SetBlockDuration (priv, 500000);
	cnt = 0;
	while ((i=XisbRead(priv->buffer)) != -1)
	{
	    DBG(7, ErrorF("%s\t* 0x%02X received - waiting for CTS_XON\n",CI_INFO, i));
		if ((unsigned char)i == CTS_XON)
	    	break;
		if(cnt++ > 50) return (Success);	/* emergency stop */
	}
	if ((unsigned char)i != CTS_XON)
	{
	    ErrorF("%sNo acknowledge from Citron Infrared Touch!\n", CI_ERROR);
		cit_ReinitSerial(priv);
    	return (!Success);
	}

	/* now we have the touch connected, do the initialization stuff */
	DBG(6, ErrorF("%s\t+ Touch connected!\n",CI_INFO));
	cit_Flush(priv);

	DBG(6, ErrorF("%s\t+ requesting pressure sensors report\n",CI_INFO));
	if (cit_GetPressureSensors(priv)!=Success)
	{
		ErrorF("%sNo pressure sensors report received from Citron Touchscreen!\n",CI_ERROR);
	}

	DBG(6, ErrorF("%s\t+ requesting peripherals report\n",CI_INFO));
	if (cit_GetPeripherals(priv)!=Success)
	{
		ErrorF("%sNo peripheralss report received from Citron Touchscreen!\n",CI_ERROR);
	}

	DBG(5, ErrorF("%s ClickMode is %d\n",CI_INFO, priv->click_mode));
	if(priv->click_mode == NO_CLICK_MODE)	/* if no click mode set in XF86Config set it automatically */
	{
		priv->click_mode = (priv->pressure_sensors > 0) ? CM_ZPRESS : CM_ENTER;
		DBG(5, ErrorF("%sClickMode set to %d\n",CI_INFO, priv->click_mode));
		cit_SetEnterCount(priv);
	}

	cit_SendCommand(priv->buffer, C_SETAREAFLAGS, 1,  AOF_ADDEXIT
													| AOF_ADDCOORD
													| AOF_ACTIVE
													| AOF_ADDPRESS);

	cit_SendCommand(priv->buffer, C_SETAREAMODE, 1, AOM_CONT);

	cit_SendCommand(priv->buffer, C_SETCONTTIME, 1, 20);

	cit_SendCommand(priv->buffer, C_SETDUALTOUCHING, 1, DT_ERROR);

	cit_SendCommand(priv->buffer, C_SETAREAPRESSURE, 1, LOBYTE(priv->button_threshold));

	cit_SendCommand(priv->buffer, C_SETRESOLUTION, 4,
													LOBYTE(CIT_DEF_MAX_X),
													HIBYTE(CIT_DEF_MAX_X),
													LOBYTE(CIT_DEF_MAX_Y),
													HIBYTE(CIT_DEF_MAX_Y));

	cit_SendCommand(priv->buffer, C_SETBEAMTIMEOUT, 2,
													LOBYTE(priv->beam_timeout),
													HIBYTE(priv->beam_timeout));

	cit_SendCommand(priv->buffer, C_SETORIGIN, 1, LOBYTE(priv->origin));
	cit_SendCommand(priv->buffer, C_SETTOUCHTIME, 1, LOBYTE(priv->touch_time));

	cit_SendCommand(priv->buffer, C_SETSLEEPMODE, 5,
													LOBYTE(priv->sleep_mode),
													LOBYTE(priv->sleep_time_act),
													HIBYTE(priv->sleep_time_act),
													LOBYTE(priv->sleep_time_scan),
													HIBYTE(priv->sleep_time_scan));

	cit_SendCommand(priv->buffer, C_SETDOZEMODE, 5,
													LOBYTE(priv->doze_mode),
													LOBYTE(priv->doze_time_act),
													HIBYTE(priv->doze_time_act),
													LOBYTE(priv->doze_time_scan),
													HIBYTE(priv->doze_time_scan));

	cit_SendCommand(priv->buffer, C_SETTRANSMISSION, 1, TM_TRANSMIT);
/*	cit_SendCommand(priv->buffer, C_SETSCANNING, 1, SC_ENABLE); */

	cit_SendPWM(priv);				/* Set PWM for active and sleep mode */
	cit_SendPWMFreq(priv);			/* Set PWM Frequency */
	cit_SendLockZ(priv);			/* LockZ timing */

	if(priv->query_state == 0)		/* do error reporting only 1 time */
	{
		priv->query_state++;

		DBG(6, ErrorF("%s\t+ requesting initial errors report\n",CI_INFO));
		if (cit_GetInitialErrors(priv)!=Success)
		{
			ErrorF("%sNo initial error report received from Citron Touchscreen!\n",CI_ERROR);
			*errmaj = LDR_NOHARDWARE;
		return (!Success);
		}
		DBG(6, ErrorF("\t+ requesting defective beams report\n"));
		if (cit_GetDefectiveBeams(priv)!=Success)
		{
			ErrorF("%sNo defective beams report received from Citron Touchscreen!\n",CI_ERROR);
			*errmaj = LDR_NOHARDWARE;
			return (!Success);
		}
		DBG(6, ErrorF("\t+ requesting touch revisions\n"));
		if (cit_GetDesignator(priv)!=Success)
		{
			ErrorF("%sNo designator received from Citron Touchscreen!\n",CI_ERROR);
			*errmaj = LDR_NOHARDWARE;
			return (!Success);
		}
		if (cit_GetRevision(priv, GR_SYSMGR)!=Success)
		{
			ErrorF("%sNo system manager module revision received from Citron Touchscreen!\n",CI_ERROR);
			*errmaj = LDR_NOHARDWARE;
			return (!Success);
		}
		if (cit_GetRevision(priv, GR_HARDWARE)!=Success)
		{
			ErrorF("%sNo hardware module revision received from Citron Touchscreen!\n",CI_ERROR);
			*errmaj = LDR_NOHARDWARE;
			return (!Success);
		}
		if (cit_GetRevision(priv, GR_PROCESS)!=Success)
		{
			ErrorF("%sNo process module revision received from Citron Touchscreen!\n",CI_ERROR);
			*errmaj = LDR_NOHARDWARE;
			return (!Success);
		}
		if (cit_GetRevision(priv, GR_PROTOCOL)!=Success)
		{
			ErrorF("%sNo protocol module revision received from Citron Touchscreen!\n",CI_ERROR);
			*errmaj = LDR_NOHARDWARE;
			return (!Success);
		}
		if (cit_GetRevision(priv, GR_HWPARAM)!=Success)
		{
			ErrorF("%sNo hardware parameter module revision received from Citron Touchscreen!\n",CI_ERROR);
			*errmaj = LDR_NOHARDWARE;
			return (!Success);
		}

		i=0;
		while(UserStrNames[i] != NULL)
		{
			if (cit_GetUserString(priv, UserStrNames[i], UsrStr, FALSE) != Success)	/* scanning is still disabled */
			{
				ErrorF("%sNo UserString read from Touch.\n",CI_NOTICE);
				break;
			}
			else
				ErrorF("%sUserString: %s=%s\n",CI_INFO, UserStrNames[i], UsrStr);
			i++;
		}

	}
	
	/* now we can start beam scanning */
	cit_SendCommand(priv->buffer, C_SETSCANNING, 1, SC_ENABLE);

	/* Test if key matrix is available and enable reports */
	if(priv->key_matrix)
	{
		/* at least 1 key has to be defined */
		if(priv->peripherals & PR_KEYMATRIX)
		{
			cit_SendCommand(priv->buffer, C_SETKEYMODE, 1, KM_ENABLE);
			DBG(6, ErrorF("%s\t+ Key matrix reports enabled\n",CI_INFO));
		}
		else
			ErrorF("%sKeys enabled on a touch without key matrix\n",CI_ERROR);
		
	}

	/* Test if ambient overload detection has to be enabled */
	if(priv->ambient_overload)
	{
		if(priv->ambient_overload <= 2)
		{
			cit_SendCommand(priv->buffer, C_SETAMBIENTOVERLOAD, 1, priv->ambient_overload);
			DBG(6, ErrorF("%s\t+ Ambient overload reports enabled in mode %d\n",CI_INFO, priv->ambient_overload));
		}
		
	}


	DBG(6, ErrorF("%s\t+ Touch initialized - %d\n",CI_INFO, priv->query_state));
	
	return (Success);
}



/*****************************************************************************
 *	[cit_ReinitSerial] Reinitialize serial port
 ****************************************************************************/
static void
cit_ReinitSerial(cit_PrivatePtr priv)
{
	if(priv->local->fd)
	{
		xf86CloseSerial(priv->local->fd);
		priv->local->fd = 0;
		priv->local->fd = xf86OpenSerial (priv->local->options);
		DBG(6, ErrorF("%s\t* cit_ReinitSerial: Serial connection reinitialized\n", CI_INFO));
	}
	else
		DBG(6, ErrorF("%s\t* cit_ReinitSerial: Serial connection not opened\n", CI_ERROR));
}

/*****************************************************************************
 *	[cit_Flush]
 ****************************************************************************/
static void
cit_Flush (cit_PrivatePtr priv)
{
	int old_block_duration;
	
	DBG(7, ErrorF("%scit_Flush called\n", CI_INFO));
	old_block_duration = priv->buffer->block_duration;
	XisbBlockDuration(priv->buffer, 1000);		/* wait for at least 10ms for the next character */
	while (XisbRead(priv->buffer) >= 0);
	cit_SetBlockDuration(priv, old_block_duration);			/* Restore the last set block duration (cit_SetBlockDuration) */
}

/*****************************************************************************
 *	[cit_SetBlockDuration]
 ****************************************************************************/
static void
cit_SetBlockDuration (cit_PrivatePtr priv, int block_duration)
{
	DBG(7, ErrorF("%scit_SetBlockDuration called [%d]\n", CI_INFO, block_duration));
	XisbBlockDuration(priv->buffer, block_duration);
}


/*****************************************************************************
 *	[cit_Beep]
 ****************************************************************************/
static void
cit_Beep(cit_PrivatePtr priv, int press)
{
#ifdef CIT_BEEP
	if(priv->beep == 0)
		return;

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) == 0
	/* ring release bell */
	if(press == 0)

	/*               [0]: volume, [1]: pitch, [2]: duration */
	/* formula is: ((1193190 / freq) & 0xffff) |				*/
	/*            (((unsigned long)duration * loudness / 50) << 16)) */
	/* .. whatever the inventor wants to intend by it, I don't know (PK) */

	/* Description (PK): The argument (for ioctl) consists of the tone value in 
		the low word and the duration in the high word.
		The tone value is not the frequency.
		The PC mainboard timer 8254 is clocked at 1.19 MHz
		and so it's 1,193,190/frequency. The duration is measured in timer ticks.
		Formula: (1193190/freq & 0xffff) | (unsigned long) (dur * vol / 50) << 16
		Example: If you set vol to 50 then dur is the duration of the tone in ms.
		         if you want 100 Hz you have to put 11,931 to freq
		         (1,193,190 / 11,931) = 100
	*/



		xf86SoundKbdBell(priv->rel_vol, priv->rel_pitch, priv->rel_dur);

	else
	/* ring press bell */
		xf86SoundKbdBell(priv->press_vol,priv->press_pitch, priv->press_dur);
#endif

	DBG(7, ErrorF("%scit_Beep called - %s\n", CI_INFO, (press == 0) ? "release" : "press"));
#endif
}


/*****************************************************************************
 *	[cit_BeepKey]
 ****************************************************************************/
static void
cit_BeepKey(cit_PrivatePtr priv, int press)
{
#ifdef CIT_BEEP
	if(priv->beepkey == 0)
		return;

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) == 0
	/* ring release bell */
	if(press <= 0)

	/*               [0]: volume, [1]: pitch, [2]: duration */
	/* formula is: ((1193190 / freq) & 0xffff) |				*/
	/*            (((unsigned long)duration * loudness / 50) << 16)) */
	/* .. whatever the inventor wants to intend by it, I don't know (PK) */

	/* Description (PK): The argument (for ioctl) consists of the tone value in 
		the low word and the duration in the high word.
		The tone value is not the frequency.
		The PC mainboard timer 8254 is clocked at 1.19 MHz
		and so it's 1,193,190/frequency. The duration is measured in timer ticks.
		Formula: (1193190/freq & 0xffff) | (unsigned long) (dur * vol / 50) << 16
		Example: If you set vol to 50 then dur is the duration of the tone in ms.
		         if you want 100 Hz you have to put 11,931 to freq
		         (1,193,190 / 11,931) = 100
	*/

		xf86SoundKbdBell(priv->relkey_vol, priv->relkey_pitch, priv->relkey_dur);

	else
	/* ring press bell */
		xf86SoundKbdBell(priv->presskey_vol,priv->presskey_pitch, priv->presskey_dur);
#endif

	DBG(7, ErrorF("%scit_BeepKey called - %s\n", CI_INFO, (press == 0) ? "release" : "press"));
#endif
}


/*****************************************************************************
 *	[cit_SendCommand]
 ****************************************************************************/
static void
cit_SendCommand (XISBuffer *b, unsigned char cmd, int cnt, ...)
{
	va_list	ap;
	unsigned char	data, x;
	
	va_start(ap, cnt);

	DBG(7, ErrorF("%scit_SendCommand(cmd=0x%02X, cnt=%d, ...)\n", CI_INFO, cmd, cnt));
	x = CTS_STX;
	XisbWrite(b, &x, 1);			/* transmit start of packet	*/
	XisbWrite(b, &cmd, 1);			/* transmit command code	*/
	x = CTS_ESC;
	while (cnt-- > 0)
	{	/* encode and transmit optional parameters	*/
		data = va_arg(ap, int);
		if (data >= CTS_CTRLMIN && data <= CTS_CTRLMAX)
		{	/* data has to be encoded	*/
			data |= CTS_ENCODE;
			XisbWrite(b, &x, 1);	/* mark coded data	*/
		}
		XisbWrite(b, &data, 1);		/* transmit data */
	}
	x = CTS_ETX;
	XisbWrite(b, &x, 1);			/* transmit end of packet	*/
	va_end(ap);
}

/*****************************************************************************
 *	[cit_SendString]
 ****************************************************************************/
static int
cit_SendString(XISBuffer *b, unsigned char cmd, int cnt, char *ustr)
{
	int i,j;
	unsigned char buf[CTS_OEMSTRING_LEN];

	if(((strlen((char *)ustr)+1) > cnt) || (cnt > sizeof(buf)))
	{
		DBG(5, ErrorF("%scit_SendString: String too long\n", CI_ERROR));
		return(!Success);
	}

	
	DBG(9, ErrorF("%scit_SendString(cmd=0x%02x numbytes=0x%02X, %s\n", CI_INFO,	cmd, cnt, ustr));
	

	j=0;
	buf[j++] = CTS_STX;			/* transmit start of packet	*/
	buf[j++] = cmd;				/* Transmit command */
	
	for(i=0; i<cnt; i++)
	{
		if ((ustr[i] >= CTS_CTRLMIN) && (ustr[i] <= CTS_CTRLMAX))
		{	/* data has to be encoded	*/
			buf[j++] = CTS_ESC;
			buf[j++] = ustr[i] | CTS_ENCODE;
		}
		else buf[j++] = ustr[i];
	}
	buf[j++] = CTS_ETX;		/* end of packet */

	XisbWrite(b, buf, j);


	for(i=0; i<j; i++)
	{
		if(i == 0)
		{
			DBG(9, ErrorF("%s sent=", CI_INFO));
		}
		else if(i%16 == 0)
		{
			DBG(9, ErrorF("\n"));
		}
		DBG(9, ErrorF("%02x ",buf[i]));
	}


	DBG(9, ErrorF("\n"));
	return(Success);
}

/*****************************************************************************
 *	[cit_GetInitialErrors]
 ****************************************************************************/
static Bool cit_GetInitialErrors(cit_PrivatePtr priv)
{
	unsigned long	errors;
	int				i;
	Bool			res;

	cit_Flush(priv);
	cit_SendCommand(priv->buffer, C_GETERRORS, 1, GE_INITIAL);
	/*
	    touch responds within 1 millisecond,
	    but it takes some time, until the command is sent!
	*/
	for (i=0; i<5; i++)
	{
		cit_SetBlockDuration(priv, 500000);
		res = cit_GetPacket(priv);
		if ((res == Success) || (priv->lex_mode == cit_idle))
			break;
	}
	if (res != Success)
	{
		DBG(5, ErrorF("%sNo packet received!\n", CI_NOTICE));
		return (!Success);
	}
	/* examine packet */
	if (priv->packeti != 6)
	{
		DBG(5, ErrorF("%sWrong packet length (expected 6, received %d bytes)\n", CI_NOTICE, priv->packeti));
		return (!Success);
	}
	if (priv->packet[0] != (C_GETERRORS & CMD_REP_CONV))
	{
		DBG(5, ErrorF("%sWrong packet identifier (expected 0x%02X, received 0x%02X)\n", CI_NOTICE,
						(C_GETERRORS & CMD_REP_CONV), priv->packet[0]));
		return (!Success);
	}
	if (priv->packet[1] != GE_INITIAL)
	{
		DBG(5, ErrorF("%sWrong packet selector (expected 0x%02X, received 0x%02X)\n", CI_NOTICE,
						GE_INITIAL, priv->packet[1]));
		return (!Success);
	}
	/* this is our packet! check contents */
	errors =  0x00000001UL * (unsigned long)priv->packet[2]
			+ 0x00000100UL * (unsigned long)priv->packet[3]
			+ 0x00010000UL * (unsigned long)priv->packet[4]
			+ 0x10000000UL * (unsigned long)priv->packet[5];
	DBG(6, ErrorF("%sinitial errors = 0x%08lX\n", CI_NOTICE, errors));

	if (errors == 0x00000000UL)
	{
	    ErrorF("%sNo initialization errors detected.\n", CI_INFO);
	}
	if (errors & IE_SMCHKSUM)
	{
		ErrorF("%sSystem Manager Module checksum error!\n", CI_ERROR);
	}
	if (errors & IE_SMINIT)
	{
		ErrorF("%sSystem Manager Module initialization error!\n", CI_ERROR);
	}
	if (errors & IE_HWCHKSUM)
	{
		ErrorF("%sHardware Module checksum error!\n", CI_ERROR);
	}
	if (errors & IE_HWINIT)
	{
		ErrorF("%sHardware Module initialization error!\n", CI_ERROR);
	}
	if (errors & IE_HW_BEAMS)
	{
		ErrorF("%s              broken beams during initialization detected!\n", CI_ERROR);
	}
	if (errors & IE_HW_PSU)
	{
		ErrorF("%s              force sensors not operating!\n", CI_ERROR);
	}
	if (errors & IE_HW_CPU)
	{
		ErrorF("%s              CPU integrity test failed!\n", CI_ERROR);
	}
	if (errors & IE_HW_IRAM)
	{
		ErrorF("%s              internal RAM error!\n", CI_ERROR);
	}
	if (errors & IE_HW_XRAM)
	{
		ErrorF("%s              external SRAM error!\n", CI_ERROR);
	}
	if (errors & IE_PCCHKSUM)
	{
		ErrorF("%sProcess Module checksum error!\n", CI_ERROR);
	}
	if (errors & IE_PCINIT)
	{
		ErrorF("%sProcess Module initialization error!\n", CI_ERROR);
	}
	if (errors & IE_PTCHKSUM)
	{
		ErrorF("%sProtocol Module checksum error!\n", CI_ERROR);
	}
	if (errors & IE_PTINIT)
	{
		ErrorF("%sProtocol Module initialization error!\n", CI_ERROR);
	}
	if (errors & IE_BICHK)
	{
		ErrorF("%sBurnIn Module checksum error!\n", CI_ERROR);
	}
	if (errors & IE_BIINIT)
	{
		ErrorF("%sBurnIn Module initialization error!\n", CI_ERROR);
	}
	if (errors & IE_FPGACHK)
	{
		ErrorF("%sFPGA configuration checksum error!\n", CI_ERROR);
	}
	if (errors & IE_HWPCHK)
	{
		ErrorF("%sHardware Parameter checksum error!\n", CI_ERROR);
	}
	return (Success);
}


/*****************************************************************************
 *	[cit_GetDefectiveBeams]
 ****************************************************************************/
static Bool cit_GetDefectiveBeams(cit_PrivatePtr priv)
{
	unsigned	nx, ny;
	int			i;
	Bool		res;

	cit_Flush(priv);
	cit_SendCommand(priv->buffer, C_GETERRORS, 1, GE_DEFECTBEAMS);
	/*
	    touch responds within 1 millisecond,
	    but it takes some time, until the command is sent!
	*/
	for (i=0; i<5; i++)
	{
		cit_SetBlockDuration(priv, 500000);
		res = cit_GetPacket(priv);
		if ((res == Success) || (priv->lex_mode == cit_idle))
			break;
	}
	if (res != Success)
	{
		DBG(5, ErrorF("%sNo packet received!\n", CI_NOTICE));
		return (!Success);
	}
	/* examine packet */
	if (priv->packeti < 6)
	{
		DBG(5, ErrorF("%sWrong packet length (expected >= 6, received %d bytes)\n", CI_NOTICE, priv->packeti));
		return (!Success);
	}
	if (priv->packet[0] != (C_GETERRORS & CMD_REP_CONV))
	{
		DBG(5, ErrorF("%sWrong packet identifier (expected 0x%02X, received 0x%02X)\n", CI_NOTICE,
						(C_GETERRORS & CMD_REP_CONV), priv->packet[0]));
		return (!Success);
	}
	if (priv->packet[1] != GE_DEFECTBEAMS)
	{
		DBG(5, ErrorF("%sWrong packet selector (expected 0x%02X, received 0x%02X)\n", CI_NOTICE,
						GE_DEFECTBEAMS, priv->packet[1]));
		return (!Success);
	}
	/* this is our packet! check contents */
	nx =  0x0001U * (unsigned)priv->packet[2]
	    + 0x0100U * (unsigned)priv->packet[3];

	ny =  0x0001U * (unsigned)priv->packet[4]
	    + 0x0100U * (unsigned)priv->packet[5];
	/* list defective X-beams */
	if (nx > 0)
	{
	    ErrorF("%s%u defective X-Beams detected:\n", CI_ERROR, nx);
	    for (i=0; i<nx; i++)
	    {
			ErrorF("%s\tX%02u\n", CI_ERROR, (unsigned)priv->packet[6+i]);
	    }
	}
	else
	{
	    ErrorF("%sNo defective X-beams detected.\n", CI_INFO);
	}
	
	/* list defective Y-beams */
	if (ny > 0)
	{
	    ErrorF("%s%u defective Y-Beams detected:\n", CI_ERROR, ny);
	    for (i=0; i<ny; i++)
	    {
		ErrorF("%s\tY%02u\n", CI_ERROR, (unsigned)priv->packet[6+nx+i]);
	    }
	}
	else
	{
	    ErrorF("%sNo defective Y-beams detected.\n", CI_INFO);
	}
	return (Success);
}


/*****************************************************************************
 *	[cit_GetDesignator]
 ****************************************************************************/
static Bool cit_GetDesignator(cit_PrivatePtr priv)
{
	int		i,n;
	Bool	res;

	cit_Flush(priv);
	cit_SendCommand(priv->buffer, C_GETREVISIONS, 1, GR_DESIGNATOR);
	/*
	    touch responds within 1 millisecond,
	    but it takes some time, until the command is sent and received!
	*/
	for (i=0; i<5; i++)
	{
		cit_SetBlockDuration(priv, 500000);
		res = cit_GetPacket(priv);
		if ((res == Success) || (priv->lex_mode == cit_idle))
			break;
	}
	if (res != Success)
	{
		DBG(5, ErrorF("%sNo packet received!\n", CI_NOTICE));
		return (!Success);
	}
	/* examine packet */
	if (priv->packeti < 2+CTS_DESIGNATOR_LEN+CTS_ASSY_LEN)
	{
		DBG(5, ErrorF("%sWrong packet length (expected >= %d, received %d bytes)\n", CI_NOTICE,
		    2+CTS_DESIGNATOR_LEN+CTS_ASSY_LEN,
		    priv->packeti));
		return (!Success);
	}
	if (priv->packet[0] != (C_GETREVISIONS & CMD_REP_CONV))
	{
		DBG(5, ErrorF("%sWrong packet identifier (expected 0x%02X, received 0x%02X)\n", CI_NOTICE,
						(C_GETREVISIONS & CMD_REP_CONV), priv->packet[0]));
		return (!Success);
	}
	if (priv->packet[1] != GR_DESIGNATOR)
	{
		DBG(5, ErrorF("%sWrong packet selector (expected 0x%02X, received 0x%02X)\n", CI_NOTICE,
						GR_DESIGNATOR, priv->packet[1]));
		return (!Success);
	}
	/* this is our packet! check contents */
	ErrorF("%sDesignator \"", CI_INFO);
	i = 2;
	n = 0;
	while (n++ < CTS_DESIGNATOR_LEN && priv->packet[i]!=0)
	{
	    ErrorF("%c", priv->packet[i++]);
	}
	ErrorF("\"\n%sAssembly   \"", CI_INFO);
	i = 2 + CTS_DESIGNATOR_LEN;
	n = 0;
	while (n++ < CTS_ASSY_LEN && priv->packet[i]!=0)
	{
	    ErrorF("%c", priv->packet[i++]);
	}
	ErrorF("\"\n");
	return (Success);
}


/*****************************************************************************
 *	[cit_GetRevision]
 ****************************************************************************/
static Bool cit_GetRevision(cit_PrivatePtr priv, int selection)
{
	int	i,n;
	Bool	res;

	cit_Flush(priv);
	cit_SendCommand(priv->buffer, C_GETREVISIONS, 1, (unsigned char)selection);
	/*
	    touch responds within 1 millisecond,
	    but it takes some time, until the command is sent and received!
	*/
	cit_SetBlockDuration(priv, 500000);
	while (((res = cit_GetPacket(priv)) != Success) && (priv->lex_mode != cit_idle));
	if (res != Success)
	{
		DBG(5, ErrorF("%sNo packet received!\n", CI_NOTICE));
		return (!Success);
	}
	/* examine packet */
	if (priv->packeti < 2)
	{
		DBG(5, ErrorF("%sWrong packet length (expected >= %d, received %d bytes)\n", CI_NOTICE,
		    2, priv->packeti));
		return (!Success);
	}
	if (priv->packet[0] != (C_GETREVISIONS & CMD_REP_CONV))
	{
		DBG(5, ErrorF("%sWrong packet identifier (expected 0x%02X, received 0x%02X)\n", CI_NOTICE,
						(C_GETREVISIONS & CMD_REP_CONV), priv->packet[0]));
		return (!Success);
	}
	if (priv->packet[1] != selection)
	{
		DBG(5, ErrorF("%sWrong packet selector (expected 0x%02X, received 0x%02X)\n", CI_NOTICE,
						selection, priv->packet[1]));
		return (!Success);
	}
	/* this is our packet! check contents */
	DBG(5, ErrorF("%s%s module revision ", CI_INFO,
	    selection == GR_SYSMGR		? "SysMgr  " :
	    selection == GR_HARDWARE	? "Hardware" : 
	    selection == GR_PROCESS		? "Process " :
	    selection == GR_PROTOCOL	? "Protocol" :
	    selection == GR_HWPARAM		? "HWParam " :
	    "???"));
	i = 2;
	n = 0;
	DBG(5, ErrorF("\""));
	while (n < priv->packeti && priv->packet[i]!=0)
	{
	    DBG(5, ErrorF("%c", priv->packet[i]));
	    i++;
	}
	DBG(5, ErrorF("\"\n"));
	return (Success);
}



/*****************************************************************************
 *	[cit_GetUserString]
 ****************************************************************************/
static Bool cit_GetUserString(cit_PrivatePtr priv, char *ustr_name, char *ustr_cont, Bool scan_flg)
{
	int ustrlen;
	int err;
	Bool res;

	DBG(8, ErrorF("%scit_GetUserString called\n", CI_INFO));

	ustrlen = strlen((char *)ustr_name);
	
	/* Test if there is a string at all and that this string is not too long */
	if((ustrlen <= 0) || (ustrlen > CTS_USERNAME_LEN))
	{
		DBG(5, ErrorF("%scit_GetUserString: No strname specified or string too long\n", CI_ERROR));
		return (!Success);
	}

/* switch off scanning to get no further coordinate packets if scanning is enabled*/
	if(scan_flg)
	{
		cit_SendCommand(priv->buffer, C_SETSCANNING, 1, SC_DISABLE);
	
		WAIT(150);		/* wait for 150ms to enshure the command has been sent */
	}	
	cit_Flush(priv);	/* now flush the buffer in order to get the userstring */

	cit_SendString(priv->buffer, C_GETUSERSTRING, ustrlen+1, ustr_name);
	/*
	    touch responds within 1 millisecond,
	    but it takes some time, until the command is sent and received!
		This time was measured with serial logger and is according to commands up to
		628ms, so we set duration time to 1 sec
	*/
	cit_SetBlockDuration(priv, 1000000);	/* 1 sec */
	while (((res = cit_GetPacket(priv)) != Success) && (priv->lex_mode != cit_idle));

	if(scan_flg)
		cit_SendCommand(priv->buffer, C_SETSCANNING, 1, SC_ENABLE);

	if (res != Success)
	{
		DBG(5, ErrorF("%sNo packet received!\n", CI_NOTICE));
		return (!Success);
	}
	/* examine packet */
	if (priv->packeti < 2)
	{
		DBG(5, ErrorF("%sWrong packet length (expected >= %d, received %d bytes)\n", CI_NOTICE,
		    2, priv->packeti));
		return (!Success);
	}
	if (priv->packet[0] != (C_GETUSERSTRING & CMD_REP_CONV))
	{
		DBG(5, ErrorF("%sWrong packet identifier (expected 0x%02X, received 0x%02X)\n", CI_NOTICE,
						(C_GETUSERSTRING & CMD_REP_CONV), priv->packet[0]));
		return (!Success);
	}

	/* this is our packet! check contents */
	if(strncmp(ustr_name, (char *)&priv->packet[1], CTS_USERNAME_LEN) == 0)	/* Name is ok */
	{
		strncpy(ustr_cont, (char *)&priv->packet[strlen((char *)&priv->packet[1])+2], CTS_USERSTRING_LEN);
		DBG(5, ErrorF("%s cit_GetUserString: %s=%s  \n", CI_INFO, ustr_name, ustr_cont));
	}
	else
	{
		DBG(5, ErrorF("%s cit_GetUserString: %s != %s\n", CI_ERROR,
						ustr_name, &priv->packet[1]));
		return (!Success);
	}

	return (Success);
}


/*****************************************************************************
 *	[cit_GetPressureSensors]
 ****************************************************************************/
static Bool cit_GetPressureSensors(cit_PrivatePtr priv)
{
	int		i;
	Bool	res;

	cit_Flush(priv);
	cit_SendCommand(priv->buffer, C_GETHARDWARE, 1, GH_SENSORCOUNT);
	/*
	    touch responds within 1 millisecond,
	    but it takes some time, until the command is sent and received!
	*/
	for (i=0; i<5; i++)
	{
		cit_SetBlockDuration(priv, 500000);
		res = cit_GetPacket(priv);
		if ((res == Success) || (priv->lex_mode == cit_idle))
			break;
	}
	if (res != Success)
	{
		DBG(5, ErrorF("%sNo packet received!\n", CI_NOTICE));
		return (!Success);
	}
	/* examine packet */
	if (priv->packeti < 2+CTS_SENSORCOUNT_LEN)
	{
		DBG(5, ErrorF("%sWrong packet length (expected >= %d, received %d bytes)\n", CI_NOTICE,
		    2+CTS_SENSORCOUNT_LEN,
		    priv->packeti));
		return (!Success);
	}
	if (priv->packet[0] != (C_GETHARDWARE & CMD_REP_CONV))
	{
		DBG(5, ErrorF("%sWrong packet identifier (expected 0x%02X, received 0x%02X)\n", CI_NOTICE,
						(C_GETHARDWARE & CMD_REP_CONV), priv->packet[0]));
		return (!Success);
	}
	if (priv->packet[1] != GH_SENSORCOUNT)
	{
		DBG(5, ErrorF("%sWrong packet selector (expected 0x%02X, received 0x%02X)\n", CI_NOTICE,
						GH_SENSORCOUNT, priv->packet[1]));
		return (!Success);
	}
	/* this is our packet! check contents */
	ErrorF("%sPressureSensors: \"%d\"\n", CI_INFO, priv->packet[2]);
	priv->pressure_sensors = priv->packet[2];
	return (Success);
}


/*****************************************************************************
 *	[cit_GetPeripherals]
 ****************************************************************************/
static Bool cit_GetPeripherals(cit_PrivatePtr priv)
{
	int		i;
	Bool	res;

	cit_Flush(priv);
	cit_SendCommand(priv->buffer, C_GETHARDWARE, 1, GH_PERIPHERALS);
	/*
	    touch responds within 1 millisecond,
	    but it takes some time, until the command is sent and received!
	*/
	for (i=0; i<5; i++)
	{
		cit_SetBlockDuration(priv, 500000);
		res = cit_GetPacket(priv);
		if ((res == Success) || (priv->lex_mode == cit_idle))
			break;
	}
	if (res != Success)
	{
                DBG(5, ErrorF("%s cit_GetPeripherals: No packet received!\n", CI_NOTICE));
		return (!Success);
	}
	/* examine packet */
	if (priv->packeti < 2+CTS_PERIPHERAL_LEN)
	{
		DBG(5, ErrorF("%sWrong packet length (expected >= %d, received %d bytes)\n", CI_NOTICE,
		    2+CTS_PERIPHERAL_LEN,
		    priv->packeti));
		return (!Success);
	}
	if (priv->packet[0] != (C_GETHARDWARE & CMD_REP_CONV))
	{
		DBG(5, ErrorF("%sWrong packet identifier (expected 0x%02X, received 0x%02X)\n", CI_NOTICE,
						(C_GETHARDWARE & CMD_REP_CONV), priv->packet[0]));
		return (!Success);
	}
	if (priv->packet[1] != GH_PERIPHERALS)
	{
		DBG(5, ErrorF("%sWrong packet selector (expected 0x%02X, received 0x%02X)\n", CI_NOTICE,
						GH_PERIPHERALS, priv->packet[1]));
		return (!Success);
	}
	/* this is our packet! check contents */
	priv->peripherals = priv->packet[2] + (priv->packet[3] * 0x100) + (priv->packet[4] * 0x10000) + (priv->packet[5] * 0x1000000);

	ErrorF("%sPeripherals: \"%08lx\"\n", CI_INFO, priv->peripherals);
	return (Success);
}


/*****************************************************************************
 *	[cit_ZPress] tell if click mode is ZPress (True if ZPress)
 ****************************************************************************/
static int cit_ZPress(cit_PrivatePtr priv)
{
	if((priv->click_mode == CM_ZPRESS) || (priv->click_mode == CM_ZPRESSEXIT))
		return (TRUE);
	else
		return (FALSE);
}


/*****************************************************************************
 *	[cit_SetEnterCount] set enter_count according click_mode
 ****************************************************************************/
static void cit_SetEnterCount(cit_PrivatePtr priv)
{
	if(cit_ZPress(priv))	/* Test if 3D Mode is active and set Options according */
		priv->enter_count = priv->enter_count_Z;
	else priv->enter_count = priv->enter_count_no_Z;
	ErrorF("%scit_SetEnterCount: Count=%d\n", CI_CONFIG, priv->enter_count);
}


/*****************************************************************************
 *	[cit_SendPWM] send pwm for active and sleep mode
 ****************************************************************************/
static void cit_SendPWM(cit_PrivatePtr priv)
{
	int pwm_active, pwm_sleep;
	
	pwm_active = cit_AdjustBright(priv, priv->pwm_active);
	pwm_sleep = cit_AdjustBright(priv, priv->pwm_sleep);

	cit_SendCommand(priv->buffer, C_SETPWM, 2,	LOBYTE(pwm_active),
												LOBYTE(pwm_sleep));

	DBG(3,ErrorF("%scit_SendPWM: active=%d, sleep=%d\n", CI_CONFIG, pwm_active, pwm_sleep));
}


/*****************************************************************************
 *	[cit_SendPWMFreq] send pwm frequency of PWM signal to the touch
 ****************************************************************************/
static void cit_SendPWMFreq(cit_PrivatePtr priv)
{
	if(priv->pwm_freq >= 0)	/* -1 is switched off (no SetPWMFreq set in XF86Config */
	{
		cit_SendCommand(priv->buffer, C_SETPWMFREQ, 2,
														LOBYTE(priv->pwm_freq),
														HIBYTE(priv->pwm_freq));
		DBG(3,ErrorF("%scit_SendPWMFreq: Freq=%d\n", CI_CONFIG, priv->pwm_freq));
	}
	else
		DBG(3,ErrorF("%scit_SendPWMFreq: Frequency not set\n", CI_CONFIG));
}

/*****************************************************************************
 *	[cit_SendPWMEX] send channel, duty cycle and frequency
 ****************************************************************************/
static void cit_SendPWMEx(cit_PrivatePtr priv)
{

	cit_SendCommand(priv->buffer, C_SETPWMEX, 4,	LOBYTE(priv->pwmex_channel),
													LOBYTE(priv->pwmex_duty),
													LOBYTE(priv->pwmex_freq),
													HIBYTE(priv->pwmex_freq));

	DBG(3,ErrorF("%scit_SendPWMEX: channel=%02x, duty=%02x, freq=%04x\n", 
		CI_CONFIG, priv->pwmex_channel, priv->pwmex_duty, priv->pwmex_freq));
}


/*****************************************************************************
 *	[cit_SendLockZ] send LockZ timing to the touch
 ****************************************************************************/
static void cit_SendLockZ(cit_PrivatePtr priv)
{
	cit_SendCommand(priv->buffer, C_SETLOCKZ, 3,	LOBYTE(priv->lockz_enter_time),
													LOBYTE(priv->lockz_exit_time),
													LOBYTE(priv->lockz_lock_time));
	DBG(3,ErrorF("%scit_SendLockZ: enter=%d, exit=%d, lock=%d\n", CI_CONFIG, 
		priv->lockz_enter_time,priv->lockz_exit_time, priv->lockz_lock_time));
}


/*****************************************************************************
 *	[cit_AdjustBright] Adjust Brightness according to the brightness table
 *  if you have a AC and want to adjust it that it works like a TDK you have
 *	to set src to BL_TDK and dst to BL_AC
 ****************************************************************************/
static int cit_AdjustBright(cit_PrivatePtr priv, int val)
{
	int i;
	int src_val;

	DBG(9,ErrorF("%scit_AdjustBright entered val=%d, src=%d, dst=%d\n", CI_CONFIG, val, priv->pwm_src, priv->pwm_dst)); 

	if((priv->pwm_src < 0) || (priv->pwm_dst < 0))
		return(val);			/* No adjust */
	
	if((priv->pwm_src <= BL_AC) && (priv->pwm_dst <= BL_MAX) && (val < 256))		/* test if value is ok */
		src_val = cit_bright_adjust[priv->pwm_src][val];	/* get value in candela */
	else
	{
		DBG(3,ErrorF("%scit_AdjustBright: Wrong value src=%d dst=%d\n", CI_CONFIG, priv->pwm_src, priv->pwm_dst)); 
		return(-1);
	}
	
	for(i=0; i<256; i++)
	{
		if(cit_bright_adjust[priv->pwm_dst][i] >= src_val)
			return (i);
	}
	return (255);
}


