
/*
Copyright (C) 1994-1999 The XFree86 Project, Inc.  All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of the XFree86 Project shall not
be used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from the XFree86 Project.
*/

#ifndef _S3V_MACROS_H
#define _S3V_MACROS_H

/* use these macros and INREG/OUTREG to access the extended registers
   of s3 virge -- add any others you need here */

/* miscellaneous registers */
#define SUBSYS_STAT_REG 0x8504
#define ADV_FUNC_CNTR 0x850c

/* memory port controller registers */
#define FIFO_CONTROL_REG 0x8200
#define MIU_CONTROL_REG 0x8204
#define STREAMS_TIMEOUT_REG 0x8208
#define MISC_TIMEOUT_REG 0x820c

/* Cursor Registers */
#define HWCURSOR_MODE_CR45 0x45
#define HWCURSOR_ADDR_LOW_CR4D 0x4d
#define HWCURSOR_ADDR_HIGH_CR4C 0x4c

/* streams registers */
#define PSTREAM_CONTROL_REG 0x8180
#define COL_CHROMA_KEY_CONTROL_REG 0x8184
#define SSTREAM_CONTROL_REG 0x8190
#define CHROMA_KEY_UPPER_BOUND_REG 0x8194
#define SSTREAM_STRETCH_REG 0x8198
#define BLEND_CONTROL_REG 0x81A0
#define PSTREAM_FBADDR0_REG 0x81C0
#define PSTREAM_FBADDR1_REG 0x81C4
#define PSTREAM_STRIDE_REG 0x81C8
#define DOUBLE_BUFFER_REG 0x81CC
#define SSTREAM_FBADDR0_REG 0x81D0
#define SSTREAM_FBADDR1_REG 0x81D4
#define SSTREAM_STRIDE_REG 0x81D8
#define OPAQUE_OVERLAY_CONTROL_REG 0x81DC
#define K1_VSCALE_REG 0x81E0
#define K2_VSCALE_REG 0x81E4
#define DDA_VERT_REG 0x81E8
#define STREAMS_FIFO_REG 0x81EC
#define PSTREAM_START_REG 0x81F0
#define PSTREAM_WINDOW_SIZE_REG 0x81F4
#define SSTREAM_START_REG 0x81F8
#define SSTREAM_WINDOW_SIZE_REG 0x81FC

/* image write stuff */
#define SRC_BASE	0xA4D4
#define DEST_BASE	0xA4D8
#define CLIP_L_R	0xA4DC
#define CLIP_T_B	0xA4E0
#define DEST_SRC_STR	0xA4E4
#define MONO_PAT_0	0xA4E8
#define MONO_PAT_1	0xA4EC
#define PAT_BG_CLR	0xA4F0
#define PAT_FG_CLR	0xA4F4 
#define SRC_BG_CLR	0xA4F8
#define SRC_FG_CLR	0xA4FC
#define CMD_SET		0xA500 
#define RWIDTH_HEIGHT	0xA504
#define RSRC_XY		0xA508 
#define RDEST_XY	0xA50C

/* Local Periperal Bus Registers */

#define DDC_REG         0xFF20
#define BLT_BUG		0x00000001
#define MONO_TRANS_BUG	0x00000002


#define MAXLOOP 0x0fffff /* timeout value for engine waits, 0.5 secs */

/* Switchable per chipset, must be initialized prior to a mode */
/* switch! */
#define WAITFIFO(n) ((*ps3v->pWaitFifo)(ps3v,n))
#define WAITCMD() ((*ps3v->pWaitCmd)(ps3v))

#define WAITIDLE()\
  do { int loop=0; mem_barrier(); \
         while(((INREG(SUBSYS_STAT_REG) & 0x3f00) < 0x3000) && (loop++<MAXLOOP)) \
         if (loop >= MAXLOOP) S3VGEReset(pScrn,1,__LINE__,__FILE__); \
  } while (0)

#define CHECK_DEST_BASE(y,h)\
    if((y < ps3v->DestBaseY) || ((y + h) > (ps3v->DestBaseY + 2048))) {\
	ps3v->DestBaseY = ((y + h) <= 2048) ? 0 : y;\
	WAITFIFO(1);\
	OUTREG(DEST_BASE, ps3v->DestBaseY * ps3v->Stride);\
    }\
    y -= ps3v->DestBaseY

#define CHECK_SRC_BASE(y,h)\
    if((y < ps3v->SrcBaseY) || ((y + h) > (ps3v->SrcBaseY + 2048))) {\
	ps3v->SrcBaseY = ((y + h) <= 2048) ? 0 : y;\
	WAITFIFO(1);\
	OUTREG(SRC_BASE, ps3v->SrcBaseY * ps3v->Stride);\
    }\
    y -= ps3v->SrcBaseY


#endif /* _S3V_MACROS_H */
