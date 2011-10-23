/*
 * Copyright © 2009 Corbin Simpson
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */
/*
 * Authors:
 *      Corbin Simpson <MostAwesomeDude@gmail.com>
 */
#ifndef RADEON_WINSYS_H
#define RADEON_WINSYS_H

#include "r300_winsys.h"

struct radeon_drm_winsys {
    struct r300_winsys_screen base;

    int fd; /* DRM file descriptor */

    struct radeon_bo_manager *bom; /* Radeon BO manager. */
    struct pb_manager *kman;
    struct pb_manager *cman;

    uint32_t pci_id;        /* PCI ID */
    uint32_t gb_pipes;      /* GB pipe count */
    uint32_t z_pipes;       /* Z pipe count (rv530 only) */
    uint32_t gart_size;     /* GART size. */
    uint32_t vram_size;     /* VRAM size. */
    boolean squaretiling;   /* Square tiling support. */
    /* DRM 2.3.0 (R500 VAP regs, MSPOS regs, fixed tex3D size checking) */
    boolean drm_2_3_0;
    /* DRM 2.6.0 (Hyper-Z, GB_Z_PEQ_CONFIG allowed on rv350->r4xx) */
    boolean drm_2_6_0;
    /* Hyper-Z user */
    boolean hyperz;

    /* Radeon CS manager. */
    struct radeon_cs_manager *csm;
};

struct radeon_drm_cs {
    struct r300_winsys_cs base;

    /* The winsys. */
    struct radeon_drm_winsys *ws;

    /* The libdrm command stream. */
    struct radeon_cs *cs;

    /* Flush CS. */
    void (*flush_cs)(void *);
    void *flush_data;
};

static INLINE struct radeon_drm_cs *
radeon_drm_cs(struct r300_winsys_cs *base)
{
    return (struct radeon_drm_cs*)base;
}

static INLINE struct radeon_drm_winsys *
radeon_drm_winsys(struct r300_winsys_screen *base)
{
    return (struct radeon_drm_winsys*)base;
}

void radeon_winsys_init_functions(struct radeon_drm_winsys *ws);

#endif
