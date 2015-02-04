/*
 * Copyright © 2014 Broadcom
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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

#include "util/u_memory.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_surface.h"
#include "util/u_blitter.h"

#include "vc4_screen.h"
#include "vc4_context.h"
#include "vc4_resource.h"
#include "vc4_tiling.h"

static void
vc4_resource_bo_alloc(struct vc4_resource *rsc)
{
        struct pipe_resource *prsc = &rsc->base.b;
        struct pipe_screen *pscreen = prsc->screen;

        vc4_bo_unreference(&rsc->bo);
        rsc->bo = vc4_bo_alloc(vc4_screen(pscreen),
                               rsc->slices[0].offset +
                               rsc->slices[0].size +
                               rsc->cube_map_stride * (prsc->array_size - 1),
                               "resource");
}

static void
vc4_resource_transfer_unmap(struct pipe_context *pctx,
                            struct pipe_transfer *ptrans)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        struct vc4_transfer *trans = vc4_transfer(ptrans);
        struct pipe_resource *prsc = ptrans->resource;
        struct vc4_resource *rsc = vc4_resource(prsc);
        struct vc4_resource_slice *slice = &rsc->slices[ptrans->level];

        if (trans->map) {
                if (ptrans->usage & PIPE_TRANSFER_WRITE) {
                        vc4_store_tiled_image(rsc->bo->map + slice->offset +
                                              ptrans->box.z * rsc->cube_map_stride,
                                              slice->stride,
                                              trans->map, ptrans->stride,
                                              slice->tiling, rsc->cpp,
                                              &ptrans->box);
                }
                free(trans->map);
        }

        pipe_resource_reference(&ptrans->resource, NULL);
        util_slab_free(&vc4->transfer_pool, ptrans);
}

static void *
vc4_resource_transfer_map(struct pipe_context *pctx,
                          struct pipe_resource *prsc,
                          unsigned level, unsigned usage,
                          const struct pipe_box *box,
                          struct pipe_transfer **pptrans)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        struct vc4_resource *rsc = vc4_resource(prsc);
        struct vc4_resource_slice *slice = &rsc->slices[level];
        struct vc4_transfer *trans;
        struct pipe_transfer *ptrans;
        enum pipe_format format = prsc->format;
        char *buf;

        if (usage & PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE) {
                vc4_resource_bo_alloc(rsc);
        } else if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) {
                if (vc4_cl_references_bo(pctx, rsc->bo)) {
                        if ((usage & PIPE_TRANSFER_DISCARD_RANGE) &&
                            prsc->last_level == 0 &&
                            prsc->width0 == box->width &&
                            prsc->height0 == box->height &&
                            prsc->depth0 == box->depth) {
                                vc4_resource_bo_alloc(rsc);
                        } else {
                                vc4_flush(pctx);
                        }
                }
        }

        if (usage & PIPE_TRANSFER_WRITE)
                rsc->writes++;

        trans = util_slab_alloc(&vc4->transfer_pool);
        if (!trans)
                return NULL;

        /* XXX: Handle DONTBLOCK, DISCARD_RANGE, PERSISTENT, COHERENT. */

        /* util_slab_alloc() doesn't zero: */
        memset(trans, 0, sizeof(*trans));
        ptrans = &trans->base;

        pipe_resource_reference(&ptrans->resource, prsc);
        ptrans->level = level;
        ptrans->usage = usage;
        ptrans->box = *box;

        /* Note that the current kernel implementation is synchronous, so no
         * need to do syncing stuff here yet.
         */

        buf = vc4_bo_map(rsc->bo);
        if (!buf) {
                fprintf(stderr, "Failed to map bo\n");
                goto fail;
        }

        *pptrans = ptrans;

        if (rsc->tiled) {
                uint32_t utile_w = vc4_utile_width(rsc->cpp);
                uint32_t utile_h = vc4_utile_height(rsc->cpp);

                /* No direct mappings of tiled, since we need to manually
                 * tile/untile.
                 */
                if (usage & PIPE_TRANSFER_MAP_DIRECTLY)
                        return NULL;

                /* We need to align the box to utile boundaries, since that's
                 * what load/store operate on.
                 */
                uint32_t box_start_x = ptrans->box.x & (utile_w - 1);
                uint32_t box_start_y = ptrans->box.y & (utile_h - 1);
                ptrans->box.width += box_start_x;
                ptrans->box.x -= box_start_x;
                ptrans->box.height += box_start_y;
                ptrans->box.y -= box_start_y;
                ptrans->box.width = align(ptrans->box.width, utile_w);
                ptrans->box.height = align(ptrans->box.height, utile_h);

                ptrans->stride = ptrans->box.width * rsc->cpp;
                ptrans->layer_stride = ptrans->stride;

                trans->map = malloc(ptrans->stride * ptrans->box.height);
                if (usage & PIPE_TRANSFER_READ) {
                        vc4_load_tiled_image(trans->map, ptrans->stride,
                                             buf + slice->offset +
                                             box->z * rsc->cube_map_stride,
                                             slice->stride,
                                             slice->tiling, rsc->cpp,
                                             &ptrans->box);
                }
                return (trans->map +
                        box_start_x * rsc->cpp +
                        box_start_y * ptrans->stride);
        } else {
                ptrans->stride = slice->stride;
                ptrans->layer_stride = ptrans->stride;

                return buf + slice->offset +
                        box->y / util_format_get_blockheight(format) * ptrans->stride +
                        box->x / util_format_get_blockwidth(format) * rsc->cpp +
                        box->z * rsc->cube_map_stride;
        }


fail:
        vc4_resource_transfer_unmap(pctx, ptrans);
        return NULL;
}

static void
vc4_resource_destroy(struct pipe_screen *pscreen,
                     struct pipe_resource *prsc)
{
        struct vc4_resource *rsc = vc4_resource(prsc);
        pipe_resource_reference(&rsc->shadow_parent, NULL);
        vc4_bo_unreference(&rsc->bo);
        free(rsc);
}

static boolean
vc4_resource_get_handle(struct pipe_screen *pscreen,
                        struct pipe_resource *prsc,
                        struct winsys_handle *handle)
{
        struct vc4_resource *rsc = vc4_resource(prsc);

        return vc4_screen_bo_get_handle(pscreen, rsc->bo, rsc->slices[0].stride,
                                        handle);
}

static const struct u_resource_vtbl vc4_resource_vtbl = {
        .resource_get_handle      = vc4_resource_get_handle,
        .resource_destroy         = vc4_resource_destroy,
        .transfer_map             = vc4_resource_transfer_map,
        .transfer_flush_region    = u_default_transfer_flush_region,
        .transfer_unmap           = vc4_resource_transfer_unmap,
        .transfer_inline_write    = u_default_transfer_inline_write,
};

static void
vc4_setup_slices(struct vc4_resource *rsc)
{
        struct pipe_resource *prsc = &rsc->base.b;
        uint32_t width = prsc->width0;
        uint32_t height = prsc->height0;
        uint32_t pot_width = util_next_power_of_two(width);
        uint32_t pot_height = util_next_power_of_two(height);
        uint32_t offset = 0;
        uint32_t utile_w = vc4_utile_width(rsc->cpp);
        uint32_t utile_h = vc4_utile_height(rsc->cpp);

        for (int i = prsc->last_level; i >= 0; i--) {
                struct vc4_resource_slice *slice = &rsc->slices[i];

                uint32_t level_width, level_height;
                if (i == 0) {
                        level_width = width;
                        level_height = height;
                } else {
                        level_width = u_minify(pot_width, i);
                        level_height = u_minify(pot_height, i);
                }

                if (rsc->tiled == VC4_TILING_FORMAT_LINEAR) {
                        slice->tiling = VC4_TILING_FORMAT_LINEAR;
                        level_width = align(level_width, 16);
                } else {
                        if (vc4_size_is_lt(level_width, level_height,
                                           rsc->cpp)) {
                                slice->tiling = VC4_TILING_FORMAT_LT;
                                level_width = align(level_width, utile_w);
                                level_height = align(level_height, utile_h);
                        } else {
                                slice->tiling = VC4_TILING_FORMAT_T;
                                level_width = align(level_width,
                                                    4 * 2 * utile_w);
                                level_height = align(level_height,
                                                     4 * 2 * utile_h);
                        }
                }

                slice->offset = offset;
                slice->stride = level_width * rsc->cpp;
                slice->size = level_height * slice->stride;

                offset += slice->size;
        }

        /* The texture base pointer that has to point to level 0 doesn't have
         * intra-page bits, so we have to align it, and thus shift up all the
         * smaller slices.
         */
        uint32_t page_align_offset = (align(rsc->slices[0].offset, 4096) -
                                      rsc->slices[0].offset);
        if (page_align_offset) {
                for (int i = 0; i <= prsc->last_level; i++)
                        rsc->slices[i].offset += page_align_offset;
        }

        /* Cube map faces appear as whole miptrees at a page-aligned offset
         * from the first face's miptree.
         */
        if (prsc->target == PIPE_TEXTURE_CUBE) {
                rsc->cube_map_stride = align(rsc->slices[0].offset +
                                             rsc->slices[0].size, 4096);
        }
}

static struct vc4_resource *
vc4_resource_setup(struct pipe_screen *pscreen,
                   const struct pipe_resource *tmpl)
{
        struct vc4_resource *rsc = CALLOC_STRUCT(vc4_resource);
        if (!rsc)
                return NULL;
        struct pipe_resource *prsc = &rsc->base.b;

        *prsc = *tmpl;

        pipe_reference_init(&prsc->reference, 1);
        prsc->screen = pscreen;

        rsc->base.vtbl = &vc4_resource_vtbl;
        rsc->cpp = util_format_get_blocksize(tmpl->format);

        assert(rsc->cpp);

        return rsc;
}

static enum vc4_texture_data_type
get_resource_texture_format(struct pipe_resource *prsc)
{
        struct vc4_resource *rsc = vc4_resource(prsc);
        uint8_t format = vc4_get_tex_format(prsc->format);

        if (!rsc->tiled) {
                assert(format == VC4_TEXTURE_TYPE_RGBA8888);
                return VC4_TEXTURE_TYPE_RGBA32R;
        }

        return format;
}

struct pipe_resource *
vc4_resource_create(struct pipe_screen *pscreen,
                    const struct pipe_resource *tmpl)
{
        struct vc4_resource *rsc = vc4_resource_setup(pscreen, tmpl);
        struct pipe_resource *prsc = &rsc->base.b;

        /* We have to make shared be untiled, since we don't have any way to
         * communicate metadata about tiling currently.
         */
        if (tmpl->target == PIPE_BUFFER ||
            (tmpl->bind & (PIPE_BIND_SCANOUT |
                           PIPE_BIND_LINEAR |
                           PIPE_BIND_SHARED |
                           PIPE_BIND_CURSOR))) {
                rsc->tiled = false;
        } else {
                rsc->tiled = true;
        }

        vc4_setup_slices(rsc);
        vc4_resource_bo_alloc(rsc);
        if (!rsc->bo)
                goto fail;

        if (tmpl->target != PIPE_BUFFER)
                rsc->vc4_format = get_resource_texture_format(prsc);

        return prsc;
fail:
        vc4_resource_destroy(pscreen, prsc);
        return NULL;
}

static struct pipe_resource *
vc4_resource_from_handle(struct pipe_screen *pscreen,
                         const struct pipe_resource *tmpl,
                         struct winsys_handle *handle)
{
        struct vc4_resource *rsc = vc4_resource_setup(pscreen, tmpl);
        struct pipe_resource *prsc = &rsc->base.b;
        struct vc4_resource_slice *slice = &rsc->slices[0];

        if (!rsc)
                return NULL;

        rsc->tiled = false;
        rsc->bo = vc4_screen_bo_from_handle(pscreen, handle, &slice->stride);
        if (!rsc->bo)
                goto fail;

#ifdef USE_VC4_SIMULATOR
        slice->stride = align(prsc->width0 * rsc->cpp, 16);
#endif
        slice->tiling = VC4_TILING_FORMAT_LINEAR;

        rsc->vc4_format = get_resource_texture_format(prsc);

        return prsc;

fail:
        vc4_resource_destroy(pscreen, prsc);
        return NULL;
}

static struct pipe_surface *
vc4_create_surface(struct pipe_context *pctx,
                   struct pipe_resource *ptex,
                   const struct pipe_surface *surf_tmpl)
{
        struct vc4_surface *surface = CALLOC_STRUCT(vc4_surface);
        struct vc4_resource *rsc = vc4_resource(ptex);

        if (!surface)
                return NULL;

        assert(surf_tmpl->u.tex.first_layer == surf_tmpl->u.tex.last_layer);

        struct pipe_surface *psurf = &surface->base;
        unsigned level = surf_tmpl->u.tex.level;

        pipe_reference_init(&psurf->reference, 1);
        pipe_resource_reference(&psurf->texture, ptex);

        psurf->context = pctx;
        psurf->format = surf_tmpl->format;
        psurf->width = u_minify(ptex->width0, level);
        psurf->height = u_minify(ptex->height0, level);
        psurf->u.tex.level = level;
        psurf->u.tex.first_layer = surf_tmpl->u.tex.first_layer;
        psurf->u.tex.last_layer = surf_tmpl->u.tex.last_layer;
        surface->offset = rsc->slices[level].offset;
        surface->tiling = rsc->slices[level].tiling;

        return &surface->base;
}

static void
vc4_surface_destroy(struct pipe_context *pctx, struct pipe_surface *psurf)
{
        pipe_resource_reference(&psurf->texture, NULL);
        FREE(psurf);
}

static void
vc4_flush_resource(struct pipe_context *pctx, struct pipe_resource *resource)
{
        struct vc4_context *vc4 = vc4_context(pctx);

        /* XXX: Skip this if we don't have any queued drawing to it. */
        vc4->base.flush(pctx, NULL, 0);
}
static bool
render_blit(struct pipe_context *ctx, struct pipe_blit_info *info)
{
        struct vc4_context *vc4 = vc4_context(ctx);

        if (!util_blitter_is_blit_supported(vc4->blitter, info)) {
                fprintf(stderr, "blit unsupported %s -> %s",
                    util_format_short_name(info->src.resource->format),
                    util_format_short_name(info->dst.resource->format));
                return false;
        }

        util_blitter_save_vertex_buffer_slot(vc4->blitter, vc4->vertexbuf.vb);
        util_blitter_save_vertex_elements(vc4->blitter, vc4->vtx);
        util_blitter_save_vertex_shader(vc4->blitter, vc4->prog.bind_vs);
        util_blitter_save_rasterizer(vc4->blitter, vc4->rasterizer);
        util_blitter_save_viewport(vc4->blitter, &vc4->viewport);
        util_blitter_save_scissor(vc4->blitter, &vc4->scissor);
        util_blitter_save_fragment_shader(vc4->blitter, vc4->prog.bind_fs);
        util_blitter_save_blend(vc4->blitter, vc4->blend);
        util_blitter_save_depth_stencil_alpha(vc4->blitter, vc4->zsa);
        util_blitter_save_stencil_ref(vc4->blitter, &vc4->stencil_ref);
        util_blitter_save_sample_mask(vc4->blitter, vc4->sample_mask);
        util_blitter_save_framebuffer(vc4->blitter, &vc4->framebuffer);
        util_blitter_save_fragment_sampler_states(vc4->blitter,
                        vc4->fragtex.num_samplers,
                        (void **)vc4->fragtex.samplers);
        util_blitter_save_fragment_sampler_views(vc4->blitter,
                        vc4->fragtex.num_textures, vc4->fragtex.textures);

        util_blitter_blit(vc4->blitter, info);

        return true;
}

/* Optimal hardware path for blitting pixels.
 * Scaling, format conversion, up- and downsampling (resolve) are allowed.
 */
static void
vc4_blit(struct pipe_context *pctx, const struct pipe_blit_info *blit_info)
{
        struct pipe_blit_info info = *blit_info;

        if (info.src.resource->nr_samples > 1 &&
            info.dst.resource->nr_samples <= 1 &&
            !util_format_is_depth_or_stencil(info.src.resource->format) &&
            !util_format_is_pure_integer(info.src.resource->format)) {
                fprintf(stderr, "color resolve unimplemented");
                return;
        }

        if (util_try_blit_via_copy_region(pctx, &info)) {
                return; /* done */
        }

        if (info.mask & PIPE_MASK_S) {
                fprintf(stderr, "cannot blit stencil, skipping");
                info.mask &= ~PIPE_MASK_S;
        }

        render_blit(pctx, &info);
}

void
vc4_update_shadow_baselevel_texture(struct pipe_context *pctx,
                                    struct pipe_sampler_view *view)
{
        struct vc4_resource *shadow = vc4_resource(view->texture);
        struct vc4_resource *orig = vc4_resource(shadow->shadow_parent);
        assert(orig);

        if (shadow->writes == orig->writes)
                return;

        for (int i = 0; i <= shadow->base.b.last_level; i++) {
                struct pipe_box box = {
                        .x = 0,
                        .y = 0,
                        .z = 0,
                        .width = u_minify(shadow->base.b.width0, i),
                        .height = u_minify(shadow->base.b.height0, i),
                        .depth = 1,
                };

                util_resource_copy_region(pctx,
                                          &shadow->base.b, i, 0, 0, 0,
                                          &orig->base.b,
                                          view->u.tex.first_level + i,
                                          &box);
        }

        shadow->writes = orig->writes;
}

/**
 * Converts a 4-byte index buffer to 2 bytes.
 *
 * Since GLES2 only has support for 1 and 2-byte indices, the hardware doesn't
 * include 4-byte index support, and we have to shrink it down.
 *
 * There's no fallback support for when indices end up being larger than 2^16,
 * though it will at least assertion fail.  Also, if the original index data
 * was in user memory, it would be nice to not have uploaded it to a VBO
 * before translating.
 */
void
vc4_update_shadow_index_buffer(struct pipe_context *pctx,
                               const struct pipe_index_buffer *ib)
{
        struct vc4_resource *shadow = vc4_resource(ib->buffer);
        struct vc4_resource *orig = vc4_resource(shadow->shadow_parent);
        uint32_t count = shadow->base.b.width0 / 2;

        if (shadow->writes == orig->writes)
                return;

        struct pipe_transfer *src_transfer;
        uint32_t *src = pipe_buffer_map_range(pctx, &orig->base.b,
                                              ib->offset,
                                              count * 4,
                                              PIPE_TRANSFER_READ, &src_transfer);

        struct pipe_transfer *dst_transfer;
        uint16_t *dst = pipe_buffer_map_range(pctx, &shadow->base.b,
                                              0,
                                              count * 2,
                                              PIPE_TRANSFER_WRITE, &dst_transfer);

        for (int i = 0; i < count; i++) {
                uint32_t src_index = src[i];
                assert(src_index <= 0xffff);
                dst[i] = src_index;
        }

        pctx->transfer_unmap(pctx, dst_transfer);
        pctx->transfer_unmap(pctx, src_transfer);

        shadow->writes = orig->writes;
}

void
vc4_resource_screen_init(struct pipe_screen *pscreen)
{
        pscreen->resource_create = vc4_resource_create;
        pscreen->resource_from_handle = vc4_resource_from_handle;
        pscreen->resource_get_handle = u_resource_get_handle_vtbl;
        pscreen->resource_destroy = u_resource_destroy_vtbl;
}

void
vc4_resource_context_init(struct pipe_context *pctx)
{
        pctx->transfer_map = u_transfer_map_vtbl;
        pctx->transfer_flush_region = u_transfer_flush_region_vtbl;
        pctx->transfer_unmap = u_transfer_unmap_vtbl;
        pctx->transfer_inline_write = u_transfer_inline_write_vtbl;
        pctx->create_surface = vc4_create_surface;
        pctx->surface_destroy = vc4_surface_destroy;
        pctx->resource_copy_region = util_resource_copy_region;
        pctx->blit = vc4_blit;
        pctx->flush_resource = vc4_flush_resource;
}
