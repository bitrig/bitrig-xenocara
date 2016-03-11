/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include "brw_device_info.h"

static const struct brw_device_info brw_device_info_i965 = {
   .gen = 4,
   .has_negative_rhw_bug = true,
   .max_vs_threads = 16,
   .max_gs_threads = 2,
   .max_wm_threads = 8 * 4,
   .urb = {
      .size = 256,
   },
};

static const struct brw_device_info brw_device_info_g4x = {
   .gen = 4,
   .has_pln = true,
   .has_compr4 = true,
   .has_surface_tile_offset = true,
   .is_g4x = true,
   .max_vs_threads = 32,
   .max_gs_threads = 2,
   .max_wm_threads = 10 * 5,
   .urb = {
      .size = 384,
   },
};

static const struct brw_device_info brw_device_info_ilk = {
   .gen = 5,
   .has_pln = true,
   .has_compr4 = true,
   .has_surface_tile_offset = true,
   .max_vs_threads = 72,
   .max_gs_threads = 32,
   .max_wm_threads = 12 * 6,
   .urb = {
      .size = 1024,
   },
};

static const struct brw_device_info brw_device_info_snb_gt1 = {
   .gen = 6,
   .gt = 1,
   .has_hiz_and_separate_stencil = true,
   .has_llc = true,
   .has_pln = true,
   .has_surface_tile_offset = true,
   .needs_unlit_centroid_workaround = true,
   .max_vs_threads = 24,
   .max_gs_threads = 21, /* conservative; 24 if rendering disabled. */
   .max_wm_threads = 40,
   .urb = {
      .size = 32,
      .min_vs_entries = 24,
      .max_vs_entries = 256,
      .max_gs_entries = 256,
   },
};

static const struct brw_device_info brw_device_info_snb_gt2 = {
   .gen = 6,
   .gt = 2,
   .has_hiz_and_separate_stencil = true,
   .has_llc = true,
   .has_pln = true,
   .has_surface_tile_offset = true,
   .needs_unlit_centroid_workaround = true,
   .max_vs_threads = 60,
   .max_gs_threads = 60,
   .max_wm_threads = 80,
   .urb = {
      .size = 64,
      .min_vs_entries = 24,
      .max_vs_entries = 256,
      .max_gs_entries = 256,
   },
};

#define GEN7_FEATURES                               \
   .gen = 7,                                        \
   .has_hiz_and_separate_stencil = true,            \
   .must_use_separate_stencil = true,               \
   .has_llc = true,                                 \
   .has_pln = true,                                 \
   .has_surface_tile_offset = true

static const struct brw_device_info brw_device_info_ivb_gt1 = {
   GEN7_FEATURES, .is_ivybridge = true, .gt = 1,
   .needs_unlit_centroid_workaround = true,
   .max_vs_threads = 36,
   .max_hs_threads = 36,
   .max_ds_threads = 36,
   .max_gs_threads = 36,
   .max_wm_threads = 48,
   .max_cs_threads = 36,
   .urb = {
      .size = 128,
      .min_vs_entries = 32,
      .max_vs_entries = 512,
      .max_hs_entries = 32,
      .max_ds_entries = 288,
      .max_gs_entries = 192,
   },
};

static const struct brw_device_info brw_device_info_ivb_gt2 = {
   GEN7_FEATURES, .is_ivybridge = true, .gt = 2,
   .needs_unlit_centroid_workaround = true,
   .max_vs_threads = 128,
   .max_hs_threads = 128,
   .max_ds_threads = 128,
   .max_gs_threads = 128,
   .max_wm_threads = 172,
   .max_cs_threads = 64,
   .urb = {
      .size = 256,
      .min_vs_entries = 32,
      .max_vs_entries = 704,
      .max_hs_entries = 64,
      .max_ds_entries = 448,
      .max_gs_entries = 320,
   },
};

static const struct brw_device_info brw_device_info_byt = {
   GEN7_FEATURES, .is_baytrail = true, .gt = 1,
   .needs_unlit_centroid_workaround = true,
   .has_llc = false,
   .max_vs_threads = 36,
   .max_hs_threads = 36,
   .max_ds_threads = 36,
   .max_gs_threads = 36,
   .max_wm_threads = 48,
   .max_cs_threads = 32,
   .urb = {
      .size = 128,
      .min_vs_entries = 32,
      .max_vs_entries = 512,
      .max_hs_entries = 32,
      .max_ds_entries = 288,
      .max_gs_entries = 192,
   },
};

#define HSW_FEATURES             \
   GEN7_FEATURES,                \
   .is_haswell = true,           \
   .supports_simd16_3src = true, \
   .has_resource_streamer = true

static const struct brw_device_info brw_device_info_hsw_gt1 = {
   HSW_FEATURES, .gt = 1,
   .max_vs_threads = 70,
   .max_hs_threads = 70,
   .max_ds_threads = 70,
   .max_gs_threads = 70,
   .max_wm_threads = 102,
   .max_cs_threads = 70,
   .urb = {
      .size = 128,
      .min_vs_entries = 32,
      .max_vs_entries = 640,
      .max_hs_entries = 64,
      .max_ds_entries = 384,
      .max_gs_entries = 256,
   },
};

static const struct brw_device_info brw_device_info_hsw_gt2 = {
   HSW_FEATURES, .gt = 2,
   .max_vs_threads = 280,
   .max_hs_threads = 256,
   .max_ds_threads = 280,
   .max_gs_threads = 256,
   .max_wm_threads = 204,
   .max_cs_threads = 70,
   .urb = {
      .size = 256,
      .min_vs_entries = 64,
      .max_vs_entries = 1664,
      .max_hs_entries = 128,
      .max_ds_entries = 960,
      .max_gs_entries = 640,
   },
};

static const struct brw_device_info brw_device_info_hsw_gt3 = {
   HSW_FEATURES, .gt = 3,
   .max_vs_threads = 280,
   .max_hs_threads = 256,
   .max_ds_threads = 280,
   .max_gs_threads = 256,
   .max_wm_threads = 408,
   .max_cs_threads = 70,
   .urb = {
      .size = 512,
      .min_vs_entries = 64,
      .max_vs_entries = 1664,
      .max_hs_entries = 128,
      .max_ds_entries = 960,
      .max_gs_entries = 640,
   },
};

#define GEN8_FEATURES                               \
   .gen = 8,                                        \
   .has_hiz_and_separate_stencil = true,            \
   .has_resource_streamer = true,                   \
   .must_use_separate_stencil = true,               \
   .has_llc = true,                                 \
   .has_pln = true,                                 \
   .supports_simd16_3src = true,                    \
   .max_vs_threads = 504,                           \
   .max_hs_threads = 504,                           \
   .max_ds_threads = 504,                           \
   .max_gs_threads = 504,                           \
   .max_wm_threads = 384

static const struct brw_device_info brw_device_info_bdw_gt1 = {
   GEN8_FEATURES, .gt = 1,
   .max_cs_threads = 42,
   .urb = {
      .size = 192,
      .min_vs_entries = 64,
      .max_vs_entries = 2560,
      .max_hs_entries = 504,
      .max_ds_entries = 1536,
      .max_gs_entries = 960,
   }
};

static const struct brw_device_info brw_device_info_bdw_gt2 = {
   GEN8_FEATURES, .gt = 2,
   .max_cs_threads = 56,
   .urb = {
      .size = 384,
      .min_vs_entries = 64,
      .max_vs_entries = 2560,
      .max_hs_entries = 504,
      .max_ds_entries = 1536,
      .max_gs_entries = 960,
   }
};

static const struct brw_device_info brw_device_info_bdw_gt3 = {
   GEN8_FEATURES, .gt = 3,
   .max_cs_threads = 56,
   .urb = {
      .size = 384,
      .min_vs_entries = 64,
      .max_vs_entries = 2560,
      .max_hs_entries = 504,
      .max_ds_entries = 1536,
      .max_gs_entries = 960,
   }
};

static const struct brw_device_info brw_device_info_chv = {
   GEN8_FEATURES, .is_cherryview = 1, .gt = 1,
   .has_llc = false,
   .max_vs_threads = 80,
   .max_hs_threads = 80,
   .max_ds_threads = 80,
   .max_gs_threads = 80,
   .max_wm_threads = 128,
   .max_cs_threads = 28,
   .urb = {
      .size = 192,
      .min_vs_entries = 34,
      .max_vs_entries = 640,
      .max_hs_entries = 80,
      .max_ds_entries = 384,
      .max_gs_entries = 256,
   }
};

#define GEN9_FEATURES                               \
   .gen = 9,                                        \
   .has_hiz_and_separate_stencil = true,            \
   .has_resource_streamer = true,                   \
   .must_use_separate_stencil = true,               \
   .has_llc = true,                                 \
   .has_pln = true,                                 \
   .supports_simd16_3src = true,                    \
   .max_vs_threads = 336,                           \
   .max_gs_threads = 336,                           \
   .max_hs_threads = 336,                           \
   .max_ds_threads = 336,                           \
   .max_wm_threads = 64 * 9,                        \
   .max_cs_threads = 56,                            \
   .urb = {                                         \
      .size = 192,                                  \
      .min_vs_entries = 64,                         \
      .max_vs_entries = 1856,                       \
      .max_hs_entries = 672,                        \
      .max_ds_entries = 1120,                       \
      .max_gs_entries = 640,                        \
   }

static const struct brw_device_info brw_device_info_skl_gt1 = {
   GEN9_FEATURES, .gt = 1,
};

static const struct brw_device_info brw_device_info_skl_gt2 = {
   GEN9_FEATURES, .gt = 2,
};

static const struct brw_device_info brw_device_info_skl_gt3 = {
   GEN9_FEATURES, .gt = 3,
};

static const struct brw_device_info brw_device_info_skl_gt4 = {
   GEN9_FEATURES, .gt = 4,
   /* From the "L3 Allocation and Programming" documentation:
    *
    * "URB is limited to 1008KB due to programming restrictions.  This is not a
    * restriction of the L3 implementation, but of the FF and other clients.
    * Therefore, in a GT4 implementation it is possible for the programmed
    * allocation of the L3 data array to provide 3*384KB=1152KB for URB, but
    * only 1008KB of this will be used."
    */
   .urb.size = 1008 / 3,
};

static const struct brw_device_info brw_device_info_bxt = {
   GEN9_FEATURES,
   .is_broxton = 1,
   .gt = 1,
   .has_llc = false,

   /* XXX: These are preliminary thread counts and URB sizes. */
   .max_vs_threads = 56,
   .max_hs_threads = 56,
   .max_ds_threads = 56,
   .max_gs_threads = 56,
   .max_wm_threads = 32,
   .max_cs_threads = 28,
   .urb = {
      .size = 64,
      .min_vs_entries = 34,
      .max_vs_entries = 640,
      .max_hs_entries = 80,
      .max_ds_entries = 80,
      .max_gs_entries = 256,
   }
};

const struct brw_device_info *
brw_get_device_info(int devid, int revision)
{
   const struct brw_device_info *devinfo;
   switch (devid) {
#undef CHIPSET
#define CHIPSET(id, family, name) \
   case id: devinfo = &brw_device_info_##family; break;
#include "pci_ids/i965_pci_ids.h"
   default:
      fprintf(stderr, "i965_dri.so does not support the 0x%x PCI ID.\n", devid);
      return NULL;
   }

   return devinfo;
}
