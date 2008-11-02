/*
 * Copyright 2000 ATI Technologies Inc., Markham, Ontario,
 *                VA Linux Systems Inc., Fremont, California.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL ATI, VA LINUX SYSTEMS AND/OR
 * THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Authors:
 *   Kevin E. Martin <martin@xfree86.org>
 *   Gareth Hughes <gareth@valinux.com>
 *
 */

#ifndef _RADEON_SAREA_H_
#define _RADEON_SAREA_H_

/* WARNING: If you change any of these defines, make sure to change the
 * defines in the kernel file (radeon_drm.h)
 */
#ifndef __RADEON_SAREA_DEFINES__
#define __RADEON_SAREA_DEFINES__

/* What needs to be changed for the current vertex buffer? */
#define RADEON_UPLOAD_CONTEXT		0x00000001
#define RADEON_UPLOAD_VERTFMT		0x00000002
#define RADEON_UPLOAD_LINE		0x00000004
#define RADEON_UPLOAD_BUMPMAP		0x00000008
#define RADEON_UPLOAD_MASKS		0x00000010
#define RADEON_UPLOAD_VIEWPORT		0x00000020
#define RADEON_UPLOAD_SETUP		0x00000040
#define RADEON_UPLOAD_TCL		0x00000080
#define RADEON_UPLOAD_MISC		0x00000100
#define RADEON_UPLOAD_TEX0		0x00000200
#define RADEON_UPLOAD_TEX1		0x00000400
#define RADEON_UPLOAD_TEX2		0x00000800
#define RADEON_UPLOAD_TEX0IMAGES	0x00001000
#define RADEON_UPLOAD_TEX1IMAGES	0x00002000
#define RADEON_UPLOAD_TEX2IMAGES	0x00004000
#define RADEON_UPLOAD_CLIPRECTS		0x00008000 /* handled client-side */
#define RADEON_REQUIRE_QUIESCENCE	0x00010000
#define RADEON_UPLOAD_ZBIAS		0x00020000
#define RADEON_UPLOAD_ALL		0x0002ffff
#define RADEON_UPLOAD_CONTEXT_ALL       0x000201ff

#define RADEON_FRONT			0x1
#define RADEON_BACK			0x2
#define RADEON_DEPTH			0x4
#define RADEON_STENCIL                  0x8

/* Primitive types */
#define RADEON_POINTS			0x1
#define RADEON_LINES			0x2
#define RADEON_LINE_STRIP		0x3
#define RADEON_TRIANGLES		0x4
#define RADEON_TRIANGLE_FAN		0x5
#define RADEON_TRIANGLE_STRIP		0x6
#define RADEON_3VTX_POINTS		0x9
#define RADEON_3VTX_LINES		0xa

/* Vertex/indirect buffer size */
#define RADEON_BUFFER_SIZE		65536

/* Byte offsets for indirect buffer data */
#define RADEON_INDEX_PRIM_OFFSET	20
#define RADEON_HOSTDATA_BLIT_OFFSET	32

#define RADEON_SCRATCH_REG_OFFSET	32

/* Keep these small for testing */
#define RADEON_NR_SAREA_CLIPRECTS	12

#define RADEON_MAX_TEXTURE_LEVELS	12
#define RADEON_MAX_TEXTURE_UNITS	3

/* Blits have strict offset rules.  All blit offset must be aligned on
 * a 1K-byte boundary.
 */
#define RADEON_OFFSET_SHIFT		10
#define RADEON_OFFSET_ALIGN		(1 << RADEON_OFFSET_SHIFT)
#define RADEON_OFFSET_MASK		(RADEON_OFFSET_ALIGN - 1)

#endif /* __RADEON_SAREA_DEFINES__ */

typedef struct {
    unsigned int red;
    unsigned int green;
    unsigned int blue;
    unsigned int alpha;
} radeon_color_regs_t;

typedef struct {
    /* Context state */
    unsigned int pp_misc;
    unsigned int pp_fog_color;
    unsigned int re_solid_color;
    unsigned int rb3d_blendcntl;
    unsigned int rb3d_depthoffset;
    unsigned int rb3d_depthpitch;
    unsigned int rb3d_zstencilcntl;

    unsigned int pp_cntl;
    unsigned int rb3d_cntl;
    unsigned int rb3d_coloroffset;
    unsigned int re_width_height;
    unsigned int rb3d_colorpitch;
    unsigned int se_cntl;

    /* Vertex format state */
    unsigned int se_coord_fmt;

    /* Line state */
    unsigned int re_line_pattern;
    unsigned int re_line_state;

    unsigned int se_line_width;

    /* Bumpmap state */
    unsigned int pp_lum_matrix;

    unsigned int pp_rot_matrix_0;
    unsigned int pp_rot_matrix_1;

    /* Mask state */
    unsigned int rb3d_stencilrefmask;
    unsigned int rb3d_ropcntl;
    unsigned int rb3d_planemask;

    /* Viewport state */
    unsigned int se_vport_xscale;
    unsigned int se_vport_xoffset;
    unsigned int se_vport_yscale;
    unsigned int se_vport_yoffset;
    unsigned int se_vport_zscale;
    unsigned int se_vport_zoffset;

    /* Setup state */
    unsigned int se_cntl_status;

    /* Misc state */
    unsigned int re_top_left;
    unsigned int re_misc;
} radeon_context_regs_t;

/* Setup registers for each texture unit */
typedef struct {
    unsigned int pp_txfilter;
    unsigned int pp_txformat;
    unsigned int pp_txoffset;
    unsigned int pp_txcblend;
    unsigned int pp_txablend;
    unsigned int pp_tfactor;
    unsigned int pp_border_color;
} radeon_texture_regs_t;

typedef struct {
    /* The channel for communication of state information to the kernel
     * on firing a vertex buffer.
     */
    radeon_context_regs_t ContextState;
    radeon_texture_regs_t TexState[RADEON_MAX_TEXTURE_UNITS];
    unsigned int dirty;
    unsigned int vertsize;
    unsigned int vc_format;

    /* The current cliprects, or a subset thereof */
    XF86DRIClipRectRec boxes[RADEON_NR_SAREA_CLIPRECTS];
    unsigned int nbox;

    /* Counters for throttling of rendering clients */
    unsigned int last_frame;
    unsigned int last_dispatch;
    unsigned int last_clear;

    /* Maintain an LRU of contiguous regions of texture space.  If you
     * think you own a region of texture memory, and it has an age
     * different to the one you set, then you are mistaken and it has
     * been stolen by another client.  If global texAge hasn't changed,
     * there is no need to walk the list.
     *
     * These regions can be used as a proxy for the fine-grained texture
     * information of other clients - by maintaining them in the same
     * lru which is used to age their own textures, clients have an
     * approximate lru for the whole of global texture space, and can
     * make informed decisions as to which areas to kick out.  There is
     * no need to choose whether to kick out your own texture or someone
     * else's - simply eject them all in LRU order.
     */
				/* Last elt is sentinal */
    drmTextureRegion texList[ATI_NR_TEX_HEAPS][ATI_NR_TEX_REGIONS+1];
				/* last time texture was uploaded */
    unsigned int texAge[ATI_NR_TEX_HEAPS];

    int ctxOwner;		/* last context to upload state */
    int pfAllowPageFlip;	/* set by the 2d driver, read by the client */
    int pfCurrentPage;		/* set by kernel, read by others */
    int crtc2_base;		/* for pageflipping with CloneMode */
} RADEONSAREAPriv, *RADEONSAREAPrivPtr;

#endif
