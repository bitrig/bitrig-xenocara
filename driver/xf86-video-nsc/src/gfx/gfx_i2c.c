/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nsc/gfx/gfx_i2c.c,v 1.1 2002/12/10 15:12:25 alanh Exp $ */
/*
 * $Workfile: gfx_i2c.c $
 *
 * This file contains routines to write to and read from the I2C bus.
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

/* INCLUDE ROUTINES FOR ACCESS.BUS, IF SPECIFIED */
/* This is for SC1200 systems. */

#if GFX_I2C_ACCESS
#include "i2c_acc.c"
#endif

/* INCLUDE ROUTINES FOR CS5530 GPIOs, IF SPECIFIED */
/* This is for GXLV systems that use GPIOs on the CS5530 for I2C. */

#if GFX_I2C_GPIO
#include "i2c_gpio.c"
#endif

/* WRAPPERS IF DYNAMIC SELECTION */
/* Extra layer to call either ACCESS.bus or GPIO routines. */

#if GFX_I2C_DYNAMIC

/*---------------------------------------------------------------------------
 * gfx_i2c_reset
 *---------------------------------------------------------------------------
 */
int
gfx_i2c_reset(unsigned char busnum, short adr, char freq)
{
   int status = GFX_STATUS_UNSUPPORTED;

#if GFX_I2C_ACCESS
   if (gfx_i2c_type & GFX_I2C_TYPE_ACCESS)
      status = acc_i2c_reset(busnum, adr, freq);
#endif
#if GFX_I2C_GPIO
   if (gfx_i2c_type & GFX_I2C_TYPE_GPIO)
      status = gpio_i2c_reset(busnum, adr, freq);
#endif
   return (status);
}

/*---------------------------------------------------------------------------
 * gfx_i2c_select_gpio
 *---------------------------------------------------------------------------
 */
int
gfx_i2c_select_gpio(int clock, int data)
{
#if GFX_I2C_ACCESS
   if (gfx_i2c_type & GFX_I2C_TYPE_ACCESS)
      acc_i2c_select_gpio(clock, data);
#endif
#if GFX_I2C_GPIO
   if (gfx_i2c_type & GFX_I2C_TYPE_GPIO)
      gpio_i2c_select_gpio(clock, data);
#endif
   return (0);
}

/*---------------------------------------------------------------------------
 * gfx_i2c_write
 *---------------------------------------------------------------------------
 */
int
gfx_i2c_write(unsigned char busnum, unsigned char chipadr,
	      unsigned char subadr, unsigned char bytes, unsigned char *data)
{
   int status = -1;

#if GFX_I2C_ACCESS
   if (gfx_i2c_type & GFX_I2C_TYPE_ACCESS)
      status = acc_i2c_write(busnum, chipadr, subadr, bytes, data);
#endif
#if GFX_I2C_GPIO
   if (gfx_i2c_type & GFX_I2C_TYPE_GPIO)
      status = gpio_i2c_write(busnum, chipadr, subadr, bytes, data);
#endif
   return (status);
}

/*---------------------------------------------------------------------------
 * gfx_i2c_read
 *---------------------------------------------------------------------------
 */
int
gfx_i2c_read(unsigned char busnum, unsigned char chipadr,
	     unsigned char subadr, unsigned char bytes, unsigned char *data)
{
   int status = -1;

#if GFX_I2C_ACCESS
   if (gfx_i2c_type & GFX_I2C_TYPE_ACCESS)
      status = acc_i2c_read(busnum, chipadr, subadr, bytes, data);
#endif
#if GFX_I2C_GPIO
   if (gfx_i2c_type & GFX_I2C_TYPE_GPIO)
      status = gpio_i2c_read(busnum, chipadr, subadr, bytes, data);
#endif
   return (status);
}

/*---------------------------------------------------------------------------
 * gfx_i2c_init
 *---------------------------------------------------------------------------
 */
int
gfx_i2c_init(void)
{
   int status = -1;

#if GFX_I2C_ACCESS
   if (gfx_i2c_type & GFX_I2C_TYPE_ACCESS)
      status = acc_i2c_init();
#endif
#if GFX_I2C_GPIO
   if (gfx_i2c_type & GFX_I2C_TYPE_GPIO)
      status = gpio_i2c_init();
#endif
   return (status);
}

/*---------------------------------------------------------------------------
 * gfx_i2c_cleanup
 *---------------------------------------------------------------------------
 */
void
gfx_i2c_cleanup(void)
{
#if GFX_I2C_ACCESS
   if (gfx_i2c_type & GFX_I2C_TYPE_ACCESS)
      acc_i2c_cleanup();
#endif
#if GFX_I2C_GPIO
   if (gfx_i2c_type & GFX_I2C_TYPE_GPIO)
      gpio_i2c_cleanup();
#endif
}

#endif /* GFX_I2C_DYNAMIC */

/* END OF FILE */
