/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nsc/gfx/gfx_defs.h,v 1.2 2002/12/11 22:51:01 dawes Exp $ */
/*
 * $Workfile: gfx_defs.h $
 *
 * This header file contains the macros used to access the hardware.  These
 * macros assume that 32-bit access is possible, which is true for most 
 * applications.  Projects using 16-bit compilers (the Windows98 display
 * driver) and special purpose applications (such as Darwin) need to define 
 * their own versions of these macros, which typically call a subroutine.
 *
 * NSC_LIC_ALTERNATIVE_PREAMBLE
 *
 * Revision 1.0
 *
 * National Semiconductor Alternative GPL-BSD License
 *
 * National Semiconductor Corporation licenses this software 
 * ("Software"):
 *
 *      Durango
 *
 * under one of the two following licenses, depending on how the 
 * Software is received by the Licensee.
 * 
 * If this Software is received as part of the Linux Framebuffer or
 * other GPL licensed software, then the GPL license designated 
 * NSC_LIC_GPL applies to this Software; in all other circumstances 
 * then the BSD-style license designated NSC_LIC_BSD shall apply.
 *
 * END_NSC_LIC_ALTERNATIVE_PREAMBLE */

/* NSC_LIC_BSD
 *
 * National Semiconductor Corporation Open Source License for Durango
 *
 * (BSD License with Export Notice)
 *
 * Copyright (c) 1999-2001
 * National Semiconductor Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 *   * Redistributions of source code must retain the above copyright 
 *     notice, this list of conditions and the following disclaimer. 
 *
 *   * Redistributions in binary form must reproduce the above 
 *     copyright notice, this list of conditions and the following 
 *     disclaimer in the documentation and/or other materials provided 
 *     with the distribution. 
 *
 *   * Neither the name of the National Semiconductor Corporation nor 
 *     the names of its contributors may be used to endorse or promote 
 *     products derived from this software without specific prior 
 *     written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
 * NATIONAL SEMICONDUCTOR CORPORATION OR CONTRIBUTORS BE LIABLE FOR ANY 
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE 
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER 
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE,
 * INTELLECTUAL PROPERTY INFRINGEMENT, OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * EXPORT LAWS: THIS LICENSE ADDS NO RESTRICTIONS TO THE EXPORT LAWS OF 
 * YOUR JURISDICTION. It is licensee's responsibility to comply with 
 * any export regulations applicable in licensee's jurisdiction. Under 
 * CURRENT (2001) U.S. export regulations this software 
 * is eligible for export from the U.S. and can be downloaded by or 
 * otherwise exported or reexported worldwide EXCEPT to U.S. embargoed 
 * destinations which include Cuba, Iraq, Libya, North Korea, Iran, 
 * Syria, Sudan, Afghanistan and any other country to which the U.S. 
 * has embargoed goods and services. 
 *
 * END_NSC_LIC_BSD */

/* NSC_LIC_GPL
 *
 * National Semiconductor Corporation Gnu General Public License for Durango
 *
 * (GPL License with Export Notice)
 *
 * Copyright (c) 1999-2001
 * National Semiconductor Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted under the terms of the GNU General 
 * Public License as published by the Free Software Foundation; either 
 * version 2 of the License, or (at your option) any later version  
 *
 * In addition to the terms of the GNU General Public License, neither 
 * the name of the National Semiconductor Corporation nor the names of 
 * its contributors may be used to endorse or promote products derived 
 * from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
 * NATIONAL SEMICONDUCTOR CORPORATION OR CONTRIBUTORS BE LIABLE FOR ANY 
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE 
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER 
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE, 
 * INTELLECTUAL PROPERTY INFRINGEMENT, OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. See the GNU General Public License for more details. 
 *
 * EXPORT LAWS: THIS LICENSE ADDS NO RESTRICTIONS TO THE EXPORT LAWS OF 
 * YOUR JURISDICTION. It is licensee's responsibility to comply with 
 * any export regulations applicable in licensee's jurisdiction. Under 
 * CURRENT (2001) U.S. export regulations this software 
 * is eligible for export from the U.S. and can be downloaded by or 
 * otherwise exported or reexported worldwide EXCEPT to U.S. embargoed 
 * destinations which include Cuba, Iraq, Libya, North Korea, Iran, 
 * Syria, Sudan, Afghanistan and any other country to which the U.S. 
 * has embargoed goods and services. 
 *
 * You should have received a copy of the GNU General Public License 
 * along with this file; if not, write to the Free Software Foundation, 
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 * END_NSC_LIC_GPL */

#ifndef _gfx_defs_h
#define _gfx_defs_h

/* ACCESS TO THE CPU REGISTERS */

#define WRITE_REG8(offset, value) \
	(*(volatile unsigned char *)(gfx_virt_regptr + (offset))) = (value)

#define WRITE_REG16(offset, value) \
	(*(volatile unsigned short *)(gfx_virt_regptr + (offset))) = (value)

#define WRITE_REG32(offset, value) \
	(*(volatile unsigned long *)(gfx_virt_regptr + (offset))) = (value)

#define READ_REG16(offset) \
    (*(volatile unsigned short *)(gfx_virt_regptr + (offset)))

#define READ_REG32(offset) \
    (*(volatile unsigned long *)(gfx_virt_regptr + (offset)))

/* ACCESS TO THE ACCELERATOR REGISTERS (REDCLOUD ONLY) */

#define WRITE_GP8(offset, value) \
	(*(volatile unsigned char *)(gfx_virt_gpptr + (offset))) = (value)

#define WRITE_GP16(offset, value) \
	(*(volatile unsigned short *)(gfx_virt_gpptr + (offset))) = (value)

#define WRITE_GP32(offset, value) \
	(*(volatile unsigned long *)(gfx_virt_gpptr + (offset))) = (value)

#define READ_GP16(offset) \
    (*(volatile unsigned short *)(gfx_virt_gpptr + (offset)))

#define READ_GP32(offset) \
    (*(volatile unsigned long *)(gfx_virt_gpptr + (offset)))

/* ACCESS TO THE FRAME BUFFER */

#define WRITE_FB32(offset, value) \
	(*(volatile unsigned long *)(gfx_virt_fbptr + (offset))) = (value)

#define WRITE_FB16(offset, value) \
	(*(volatile unsigned short *)(gfx_virt_fbptr + (offset))) = (value)

#define WRITE_FB8(offset, value) \
	(*(volatile unsigned char *)(gfx_virt_fbptr + (offset))) = (value)

/* ACCESS TO THE VIDEO HARDWARE */

#define READ_VID32(offset) \
	(*(volatile unsigned long *)(gfx_virt_vidptr + (offset)))

#define WRITE_VID32(offset, value) \
	(*(volatile unsigned long *)(gfx_virt_vidptr + (offset))) = (value)

/* ACCESS TO THE VIP HARDWARE */

#define READ_VIP32(offset) \
	(*(volatile unsigned long *)(gfx_virt_vipptr + (offset)))

#define WRITE_VIP32(offset, value) \
	(*(volatile unsigned long *)(gfx_virt_vipptr + (offset))) = (value)

/* ACCESS TO THE SCRATCHPAD RAM */

#define WRITE_SCRATCH32(offset, value) \
	(*(volatile unsigned long *)(gfx_virt_spptr + (offset))) = (value)

#define WRITE_SCRATCH16(offset, value) \
	(*(volatile unsigned short *)(gfx_virt_spptr + (offset))) = (value)

#define WRITE_SCRATCH8(offset, value) \
	(*(volatile unsigned char *)(gfx_virt_spptr + (offset))) = (value)

#define READ_SCRATCH16(offset) \
    (*(volatile unsigned short *)(gfx_virt_spptr + (offset)))

#define READ_SCRATCH32(offset) \
    (*(volatile unsigned long *)(gfx_virt_spptr + (offset)))

/* ACCESS TO MSRS */

void gfx_msr_asm_write(unsigned short msrReg, unsigned long msrAddr,
		       unsigned long *ptrHigh, unsigned long *ptrLow);
void gfx_msr_asm_read(unsigned short msrReg, unsigned long msrAddr,
		      unsigned long *ptrHigh, unsigned long *ptrLow);

#define MSR_READ( MBD_MSR_CAP, address, valueHigh_ptr, valueLow_ptr ) \
					 gfx_msr_asm_read( ((unsigned short)(MBD_MSR_CAP)), address, valueHigh_ptr, valueLow_ptr )

#define MSR_WRITE( MBD_MSR_CAP, address, valueHigh_ptr, valueLow_ptr ) \
					 gfx_msr_asm_write( ((unsigned short)(MBD_MSR_CAP)), address, valueHigh_ptr, valueLow_ptr )

/* OPTIMIZATION MACROS */
/* The following macros have been added to allow more complete optimization of the */
/* bitmap-to-screen routines in Durango.  These routines also allow Durango to run */
/* properly within a 16-bit environment.                                           */

/************************************************************************************
 * Macro:   SET_SCRATCH_BASE 
 * Purpose: Record the base address of the BLT buffers.  The WRITE_SCRATCH_STRINGxx 
 *          macros assume that this address is used as the base for all writes.
 *                                                         
 * Arguments:                                              
 *    scratch_base -   offset into the GX base for the first BLT buffer byte.
 ************************************************************************************/

#define SET_SCRATCH_BASE(scratch_base) \
	{ gfx_gx1_scratch_base = (unsigned long)gfx_virt_spptr + scratch_base; }

#ifdef GFX_OPTIMIZE_ASSEMBLY

/************************************************************************************
 * Macro:   WRITE_SCRATCH_STRING  
 * Purpose: Write multiple bytes to the scratchpad buffer 
 *                                                         
 * Arguments:                                              
 *    dword_bytes  -   number of bytes to transfer.  This number will always.
 *                     be a multiple of 4.  It cannot be modified within the 
 *                     macro (ex. bytes -= 4)
 *    bytes_extra  -   number of non-DWORD aligned bytes
 *    array        -   pointer to an array of unsigned characters. 
 *    array_offset -   offset into the array from which to pull the first character.
 ************************************************************************************/

#define WRITE_SCRATCH_STRING(dwords, bytes, array, array_offset)                            \
{                                                                                           \
	_asm { mov edi, gfx_gx1_scratch_base }                                                  \
	_asm { mov esi, array }                                                                 \
	_asm { add esi, array_offset }                                                          \
	_asm { mov ecx, dwords }                                                                \
	_asm { shr ecx, 2 }                                                                     \
	_asm { rep movsd }                                                                      \
	_asm { mov ecx, bytes }                                                                 \
	_asm { rep movsb }                                                                      \
}

/************************************************************************************
 * Macro:   WRITE_FRAME_BUFFER_STRING32  
 * Purpose: Write multiple dwords to the Frame buffer 
 *                                                         
 * Arguments:                                              
 *    fboffset     -   offset to the beginning frame buffer location.
 *    bytes        -   number of bytes to transfer.  This number will always.
 *                     be a multiple of 4.  It cannot be modified within the 
 *                     macro (ex. bytes -= 4)
 *    array        -   pointer to an array of unsigned characters. 
 *    array_offset -   offset into the array from which to pull the first character.
 ************************************************************************************/

#define WRITE_FRAME_BUFFER_STRING32(fboffset, bytes, array, array_offset)                \
{                                                                                        \
	_asm { mov ecx, bytes }                                                              \
	_asm { shr ecx, 2 }                                                                  \
	_asm { cld }                                                                         \
	_asm { mov edi, gfx_virt_fbptr }                                                     \
	_asm { add edi, fboffset }                                                           \
	_asm { mov esi, array }                                                              \
	_asm { add esi, array_offset }                                                       \
    _asm { rep movsd }                                                                   \
}

#else

/************************************************************************************
 * Macro:   WRITE_SCRATCH_STRING  
 * Purpose: Write multiple bytes to the scratchpad buffer 
 *                                                         
 * Arguments:                                              
 *    dword_bytes  -   number of bytes to transfer.  This number will always.
 *                     be a multiple of 4.  It cannot be modified within the 
 *                     macro (ex. bytes -= 4)
 *    bytes_extra  -   number of non-DWORD aligned bytes
 *    array        -   pointer to an array of unsigned characters. 
 *    array_offset -   offset into the array from which to pull the first character.
 ************************************************************************************/

#define WRITE_SCRATCH_STRING(dword_bytes, bytes_extra, array, array_offset)                             \
{                                                                                                       \
	unsigned long i, j;                                                                                 \
	unsigned long aroffset = (unsigned long)array + (array_offset);                                     \
	                                                                                                    \
	/* WRITE DWORDS */                                                                                  \
	                                                                                                    \
	for (i = 0; i < dword_bytes; i += 4)                                                                \
		*((volatile unsigned long *)(gfx_gx1_scratch_base + i)) = *((unsigned long *)(aroffset + i));   \
	                                                                                                    \
	/* WRITE BYTES */                                                                                   \
	                                                                                                    \
	j = i + bytes_extra;	                                                                            \
	while (i < j)	                                                                                    \
	{	                                                                                    \
		*((volatile unsigned char *)(gfx_gx1_scratch_base + i)) = *((unsigned char *)(aroffset + i));   \
		i++;	                                                                                    \
	}	                                                                                    \
}

/************************************************************************************
 * Macro:   WRITE_FRAME_BUFFER_STRING32  
 * Purpose: Write multiple dwords to the Frame buffer 
 *                                                         
 * Arguments:                                              
 *    fboffset     -   offset to the beginning frame buffer location.
 *    bytes        -   number of bytes to transfer.  This number will always.
 *                     be a multiple of 4.  It cannot be modified within the 
 *                     macro (ex. bytes -= 4)
 *    array        -   pointer to an array of unsigned characters. 
 *    array_offset -   offset into the array from which to pull the first character.
 ************************************************************************************/

#define WRITE_FRAME_BUFFER_STRING32(fboffset, bytes, array, array_offset)                         \
{                                                                                            \
	unsigned long i;                                                                         \
	unsigned long aroffset = (unsigned long)array + (array_offset);                          \
	for (i = 0; i < bytes; i += 4)                                                           \
		WRITE_FB32 ((fboffset) + i, *((unsigned long *)(aroffset + i)));                \
}

#endif

/************************************************************************************
 * Macro:   WRITE_FRAME_BUFFER_STRING8  
 * Purpose: Write multiple bytes to the frame buffer 
 *                                                         
 * Arguments:                                              
 *    spoffset     -   offset to the beginning frame buffer location.
 *    bytes        -   number of bytes to transfer.  This number cannot be modified within the 
 *                     macro (ex. bytes -= 4)
 *    array        -   pointer to an array of unsigned characters. 
 *    array_offset -   offset into the array from which to pull the first character.
 ************************************************************************************/

#define WRITE_FRAME_BUFFER_STRING8(fboffset, bytes, array, array_offset)                          \
{                                                                                            \
	unsigned long i;                                                                         \
	unsigned long aroffset = (unsigned long)array + (array_offset);                          \
	for (i = 0; i < bytes; i++)                                                              \
		WRITE_FB8 ((fboffset) + i, *((unsigned char *)(aroffset + i)));                      \
}

/************************************************************************************
 * Macro:   WRITE_GPREG_STRING32
 * Purpose: Write multiple dwords to one GP register.
 *                                                         
 * Arguments:                                              
 *    regoffset    -   Offset of the GP register to be written.
 *    dwords       -   number of dwords to transfer.  It cannot be modified within the 
 *                     macro (ex. dwords--)
 *    counter      -   name of a counter variable that can be used in a loop.  This
 *                     is used to optimize macros written in C.
 *    array        -   pointer to an array of unsigned characters. 
 *    array_offset -   offset into the array from which to pull the first character.
 *    temp         -   name of a temporary variable that can be used for calculations.
 *                     This argument is also used for C-only macros.
 ************************************************************************************/

#define WRITE_GPREG_STRING32(regoffset, dwords, counter, array, array_offset, temp)           \
{                                                                                            \
	temp = (unsigned long)array + (array_offset);                                            \
	for (counter = 0; counter < dwords; counter++)                                         \
		WRITE_GP32 (regoffset, *((unsigned long *)temp + counter));                        \
}

/************************************************************************************
 * Macro:   WRITE_GPREG_STRING8
 * Purpose: Write 4 or less bytes to one GP register.
 *                                                         
 * Arguments:                                              
 *    regoffset    -   Offset of the GP register to be written.
 *    bytes        -   number of bytes to transfer.  This number will always.
 *                     be less than 4.  It cannot be modified within the 
 *                     macro (ex. bytes--)
 *    shift        -   name of a shift variable that can be used as a shift count.
 *                     This variable holds the initial shift value into the GP register.
 *    counter      -   name of a counter variable that can be used in a loop.  This
 *                     is used to optimize macros written in C.
 *    array        -   pointer to an array of unsigned characters. 
 *    array_offset -   offset into the array from which to pull the first character.
 *    temp1        -   name of a temporary variable that can be used for calculations.
 *                     This argument is also used for C-only macros.
 *    temp2        -   name of a temporary variable that can be used for calculations.
 *                     This argument is also used for C-only macros.
 ************************************************************************************/
#define WRITE_GPREG_STRING8(regoffset, bytes, shift, counter, array, array_offset, temp1, temp2)           \
{                                                                                           \
	if (bytes)                                                                              \
	{                                                                                       \
		temp1 = (unsigned long)array + (array_offset);                                      \
		temp2 = 0;                                                                          \
		for (counter = 0; counter < bytes; counter++)                                       \
		{                                                                                   \
			temp2 |= ((unsigned long)(*((unsigned char *)(temp1 + counter)))) << shift;     \
			shift += 8;                                                                     \
		}                                                                                   \
		WRITE_GP32 (regoffset, temp2);                                                      \
	}                                                                                       \
}
#endif /* _gfx_defs_h */

/* END OF FILE */
