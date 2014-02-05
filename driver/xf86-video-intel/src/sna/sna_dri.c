/**************************************************************************

Copyright 2001 VA Linux Systems Inc., Fremont, California.
Copyright © 2002 by David Dawes

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
on the rights to use, copy, modify, merge, publish, distribute, sub
license, and/or sell copies of the Software, and to permit persons to whom
the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
ATI, VA LINUX SYSTEMS AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

/*
 * Authors: Jeff Hartmann <jhartmann@valinux.com>
 *          David Dawes <dawes@xfree86.org>
 *          Keith Whitwell <keith@tungstengraphics.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#include "sna.h"
#include "intel_options.h"

#include <xf86drm.h>
#include <i915_drm.h>
#include <dri2.h>
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,12,99,901,0)
#include <compositeext.h>
#endif

#if DRI2INFOREC_VERSION < 2
#error DRI2 version supported by the Xserver is too old
#endif

#if DRI2INFOREC_VERSION < 10
#undef USE_ASYNC_SWAP
#define USE_ASYNC_SWAP 0
#endif

#define COLOR_PREFER_TILING_Y 0

enum frame_event_type {
	DRI2_WAITMSC = 0,
	DRI2_SWAP,
	DRI2_SWAP_WAIT,
	DRI2_SWAP_THROTTLE,
	DRI2_FLIP,
	DRI2_FLIP_THROTTLE,
};

struct sna_dri_private {
	PixmapPtr pixmap;
	struct kgem_bo *bo;
	bool scanout;
	uint32_t size;
	int refcnt;
};

static inline struct sna_dri_private *
get_private(void *buffer)
{
	return (struct sna_dri_private *)((DRI2Buffer2Ptr)buffer+1);
}

static inline struct kgem_bo *ref(struct kgem_bo *bo)
{
	assert(bo->refcnt);
	bo->refcnt++;
	return bo;
}

static inline void unref(struct kgem_bo *bo)
{
	assert(bo->refcnt > 1);
	bo->refcnt--;
}

/* Prefer to enable TILING_Y if this buffer will never be a
 * candidate for pageflipping
 */
static uint32_t color_tiling(struct sna *sna, DrawablePtr draw)
{
	uint32_t tiling;

	if (COLOR_PREFER_TILING_Y &&
	    (draw->width  != sna->front->drawable.width ||
	     draw->height != sna->front->drawable.height))
		tiling = I915_TILING_Y;
	else
		tiling = I915_TILING_X;

	return kgem_choose_tiling(&sna->kgem, -tiling,
				  draw->width,
				  draw->height,
				  draw->bitsPerPixel);
}

static uint32_t other_tiling(struct sna *sna, DrawablePtr draw)
{
	/* XXX Can mix color X / depth Y? */
	return kgem_choose_tiling(&sna->kgem,
				  sna->kgem.gen >= 040 ? -I915_TILING_Y : -I915_TILING_X,
				  draw->width,
				  draw->height,
				  draw->bitsPerPixel);
}

static struct kgem_bo *sna_pixmap_set_dri(struct sna *sna,
					  PixmapPtr pixmap)
{
	struct sna_pixmap *priv;
	int tiling;

	DBG(("%s: attaching DRI client to pixmap=%ld\n",
	     __FUNCTION__, pixmap->drawable.serialNumber));

	priv = sna_pixmap(pixmap);
	if (priv != NULL && priv->shm) {
		DBG(("%s: SHM Pixmap, BadAlloc\n", __FUNCTION__));
		return NULL;
	}

	priv = sna_pixmap_move_to_gpu(pixmap,
				      MOVE_READ | MOVE_WRITE | __MOVE_FORCE | __MOVE_DRI);
	if (priv == NULL) {
		DBG(("%s: failed to move to GPU, BadAlloc\n", __FUNCTION__));
		return NULL;
	}

	assert(priv->flush == false);
	assert(priv->cow == NULL);
	assert(priv->cpu_damage == NULL);
	assert(priv->gpu_bo);
	assert(priv->gpu_bo->proxy == NULL);
	assert(priv->gpu_bo->flush == false);

	tiling = color_tiling(sna, &pixmap->drawable);
	if (tiling < 0)
		tiling = -tiling;
	if (priv->gpu_bo->tiling != tiling)
		sna_pixmap_change_tiling(pixmap, tiling);

	return priv->gpu_bo;
}

pure static inline void *sna_pixmap_get_buffer(PixmapPtr pixmap)
{
	assert(pixmap->refcnt);
	return ((void **)__get_private(pixmap, sna_pixmap_key))[2];
}

static inline void sna_pixmap_set_buffer(PixmapPtr pixmap, void *ptr)
{
	assert(pixmap->refcnt);
	((void **)__get_private(pixmap, sna_pixmap_key))[2] = ptr;
}

void
sna_dri_pixmap_update_bo(struct sna *sna, PixmapPtr pixmap)
{
	DRI2BufferPtr buffer;
	struct sna_dri_private *private;
	struct kgem_bo *bo;

	buffer = sna_pixmap_get_buffer(pixmap);
	if (buffer == NULL)
		return;

	DBG(("%s: pixmap=%ld, old handle=%d, new handle=%d\n", __FUNCTION__,
	     pixmap->drawable.serialNumber,
	     get_private(buffer)->bo->handle,
	     sna_pixmap(pixmap)->gpu_bo->handle));

	private = get_private(buffer);
	assert(private->pixmap == pixmap);

	bo = sna_pixmap(pixmap)->gpu_bo;
	if (private->bo == bo)
		return;

	private->bo->flush = false;
	kgem_bo_destroy(&sna->kgem, private->bo);

	buffer->name = kgem_bo_flink(&sna->kgem, bo);
	private->bo = ref(bo);

	assert(bo->flush);

	/* XXX DRI2InvalidateDrawable(&pixmap->drawable); */
}

static DRI2Buffer2Ptr
sna_dri_create_buffer(DrawablePtr draw,
		      unsigned int attachment,
		      unsigned int format)
{
	struct sna *sna = to_sna_from_drawable(draw);
	DRI2Buffer2Ptr buffer;
	struct sna_dri_private *private;
	PixmapPtr pixmap;
	struct kgem_bo *bo;
	unsigned flags = 0;
	uint32_t size;
	int bpp;

	DBG(("%s pixmap=%ld, (attachment=%d, format=%d, drawable=%dx%d)\n",
	     __FUNCTION__,
	     get_drawable_pixmap(draw)->drawable.serialNumber,
	     attachment, format, draw->width, draw->height));

	pixmap = NULL;
	size = (uint32_t)draw->height << 16 | draw->width;
	switch (attachment) {
	case DRI2BufferFrontLeft:
		pixmap = get_drawable_pixmap(draw);
		buffer = sna_pixmap_get_buffer(pixmap);
		if (buffer) {
			private = get_private(buffer);

			DBG(("%s: reusing front buffer attachment, pixmap=%ld, handle=%d, name=%d\n",
			     __FUNCTION__, pixmap->drawable.serialNumber,
			     private->bo->handle, buffer->name));

			assert(private->pixmap == pixmap);
			assert(sna_pixmap(pixmap)->flush);
			assert(sna_pixmap(pixmap)->gpu_bo == private->bo);
			assert(sna_pixmap(pixmap)->pinned & PIN_DRI2);
			assert(kgem_bo_flink(&sna->kgem, private->bo) == buffer->name);
			assert(8*private->bo->pitch >= pixmap->drawable.width * pixmap->drawable.bitsPerPixel);
			assert(private->bo->pitch * pixmap->drawable.height <= kgem_bo_size(private->bo));

			private->refcnt++;
			return buffer;
		}

		bo = sna_pixmap_set_dri(sna, pixmap);
		if (bo == NULL)
			return NULL;

		assert(sna_pixmap(pixmap) != NULL);

		bo = ref(bo);
		bpp = pixmap->drawable.bitsPerPixel;
		if (pixmap == sna->front)
			flags |= CREATE_SCANOUT;
		DBG(("%s: attaching to front buffer %dx%d [%p:%d], scanout? %d\n",
		     __FUNCTION__,
		     pixmap->drawable.width, pixmap->drawable.height,
		     pixmap, pixmap->refcnt, flags & CREATE_SCANOUT));
		size = (uint32_t)pixmap->drawable.height << 16 | pixmap->drawable.width;
		break;

	case DRI2BufferBackLeft:
		if (draw->width  == sna->front->drawable.width &&
		    draw->height == sna->front->drawable.height &&
		    (sna->flags & (SNA_NO_WAIT | SNA_NO_FLIP)) == 0)
			flags |= CREATE_SCANOUT;
	case DRI2BufferBackRight:
	case DRI2BufferFrontRight:
	case DRI2BufferFakeFrontLeft:
	case DRI2BufferFakeFrontRight:
		bpp = draw->bitsPerPixel;
		DBG(("%s: creating back buffer %dx%d, suitable for scanout? %d\n",
		     __FUNCTION__,
		     draw->width, draw->height,
		     flags & CREATE_SCANOUT));

		bo = kgem_create_2d(&sna->kgem,
				    draw->width,
				    draw->height,
				    draw->bitsPerPixel,
				    color_tiling(sna, draw),
				    flags);
		break;

	case DRI2BufferStencil:
		/*
		 * The stencil buffer has quirky pitch requirements.  From Vol
		 * 2a, 11.5.6.2.1 3DSTATE_STENCIL_BUFFER, field "Surface
		 * Pitch":
		 *    The pitch must be set to 2x the value computed based on
		 *    width, as the stencil buffer is stored with two rows
		 *    interleaved.
		 * To accomplish this, we resort to the nasty hack of doubling
		 * the drm region's cpp and halving its height.
		 *
		 * If we neglect to double the pitch, then
		 * drm_intel_gem_bo_map_gtt() maps the memory incorrectly.
		 *
		 * The alignment for W-tiling is quite different to the
		 * nominal no-tiling case, so we have to account for
		 * the tiled access pattern explicitly.
		 *
		 * The stencil buffer is W tiled. However, we request from
		 * the kernel a non-tiled buffer because the kernel does
		 * not understand W tiling and the GTT is incapable of
		 * W fencing.
		 */
		bpp = format ? format : draw->bitsPerPixel;
		bpp *= 2;
		bo = kgem_create_2d(&sna->kgem,
				    ALIGN(draw->width, 64),
				    ALIGN((draw->height + 1) / 2, 64),
				    bpp, I915_TILING_NONE, flags);
		break;

	case DRI2BufferDepth:
	case DRI2BufferDepthStencil:
	case DRI2BufferHiz:
	case DRI2BufferAccum:
		bpp = format ? format : draw->bitsPerPixel,
		bo = kgem_create_2d(&sna->kgem,
				    draw->width, draw->height, bpp,
				    other_tiling(sna, draw),
				    flags);
		break;

	default:
		return NULL;
	}
	if (bo == NULL)
		return NULL;

	buffer = calloc(1, sizeof *buffer + sizeof *private);
	if (buffer == NULL)
		goto err;

	private = get_private(buffer);
	buffer->attachment = attachment;
	buffer->pitch = bo->pitch;
	buffer->cpp = bpp / 8;
	buffer->driverPrivate = private;
	buffer->format = format;
	buffer->flags = 0;
	buffer->name = kgem_bo_flink(&sna->kgem, bo);
	private->refcnt = 1;
	private->bo = bo;
	private->pixmap = pixmap;
	private->scanout = !!(flags & CREATE_SCANOUT);
	private->size = size;

	if (buffer->name == 0)
		goto err;

	if (pixmap) {
		struct sna_pixmap *priv;

		assert(attachment == DRI2BufferFrontLeft);
		assert(sna_pixmap_get_buffer(pixmap) == NULL);

		sna_pixmap_set_buffer(pixmap, buffer);
		assert(sna_pixmap_get_buffer(pixmap) == buffer);
		pixmap->refcnt++;

		priv = sna_pixmap(pixmap);
		assert(priv->flush == false);
		assert((priv->pinned & PIN_DRI2) == 0);

		/* Don't allow this named buffer to be replaced */
		priv->pinned |= PIN_DRI2;

		/* We need to submit any modifications to and reads from this
		 * buffer before we send any reply to the Client.
		 *
		 * As we don't track which Client, we flush for all.
		 */
		priv->flush = true;
		sna_accel_watch_flush(sna, 1);
	}

	assert(bo->flush == true);

	return buffer;

err:
	kgem_bo_destroy(&sna->kgem, bo);
	free(buffer);
	return NULL;
}

static void _sna_dri_destroy_buffer(struct sna *sna, DRI2Buffer2Ptr buffer)
{
	struct sna_dri_private *private = get_private(buffer);

	if (buffer == NULL)
		return;

	DBG(("%s: %p [handle=%d] -- refcnt=%d, pixmap=%ld\n",
	     __FUNCTION__, buffer, private->bo->handle, private->refcnt,
	     private->pixmap ? private->pixmap->drawable.serialNumber : 0));
	assert(private->refcnt > 0);
	if (--private->refcnt)
		return;

	assert(private->bo);
	if (private->pixmap) {
		PixmapPtr pixmap = private->pixmap;
		struct sna_pixmap *priv = sna_pixmap(pixmap);

		assert(sna_pixmap_get_buffer(pixmap) == buffer);
		assert(priv->gpu_bo == private->bo);
		assert(priv->gpu_bo->flush);
		assert(priv->pinned & PIN_DRI2);
		assert(priv->flush);

		/* Undo the DRI markings on this pixmap */
		DBG(("%s: releasing last DRI pixmap=%ld, scanout?=%d\n",
		     __FUNCTION__,
		     pixmap->drawable.serialNumber,
		     pixmap == sna->front));

		list_del(&priv->flush_list);

		priv->gpu_bo->flush = false;
		priv->pinned &= ~PIN_DRI2;

		priv->flush = false;
		sna_accel_watch_flush(sna, -1);

		sna_pixmap_set_buffer(pixmap, NULL);
		pixmap->drawable.pScreen->DestroyPixmap(pixmap);
	} else
		private->bo->flush = false;

	kgem_bo_destroy(&sna->kgem, private->bo);
	free(buffer);
}

static void sna_dri_destroy_buffer(DrawablePtr draw, DRI2Buffer2Ptr buffer)
{
	_sna_dri_destroy_buffer(to_sna_from_drawable(draw), buffer);
}

static void sna_dri_reference_buffer(DRI2BufferPtr buffer)
{
	get_private(buffer)->refcnt++;
}

static inline void damage(PixmapPtr pixmap, struct sna_pixmap *priv, RegionPtr region)
{
	assert(priv->gpu_bo);
	if (DAMAGE_IS_ALL(priv->gpu_damage))
		goto done;

	if (region == NULL) {
damage_all:
		priv->gpu_damage = _sna_damage_all(priv->gpu_damage,
						   pixmap->drawable.width,
						   pixmap->drawable.height);
		sna_damage_destroy(&priv->cpu_damage);
		list_del(&priv->flush_list);
	} else {
		sna_damage_subtract(&priv->cpu_damage, region);
		if (priv->cpu_damage == NULL)
			goto damage_all;
		sna_damage_add(&priv->gpu_damage, region);
	}
done:
	priv->cpu = false;
	priv->clear = false;
}

static void set_bo(PixmapPtr pixmap, struct kgem_bo *bo)
{
	struct sna *sna = to_sna_from_pixmap(pixmap);
	struct sna_pixmap *priv = sna_pixmap(pixmap);
	RegionRec region;

	DBG(("%s: pixmap=%ld, handle=%d\n",
	     __FUNCTION__, pixmap->drawable.serialNumber, bo->handle));

	assert(pixmap->drawable.width * pixmap->drawable.bitsPerPixel <= 8*bo->pitch);
	assert(pixmap->drawable.height * bo->pitch <= kgem_bo_size(bo));
	assert(bo->proxy == NULL);
	assert(bo->flush);
	assert(priv->pinned & PIN_DRI2);
	assert((priv->pinned & PIN_PRIME) == 0);
	assert(priv->flush);

	if (priv->cow && priv->gpu_bo != bo)
		sna_pixmap_undo_cow(sna, priv, 0);

	/* Post damage on the new front buffer so that listeners, such
	 * as DisplayLink know take a copy and shove it over the USB,
	 * also for software cursors and the like.
	 */
	region.extents.x1 = region.extents.y1 = 0;
	region.extents.x2 = pixmap->drawable.width;
	region.extents.y2 = pixmap->drawable.height;
	region.data = NULL;
	DamageRegionAppend(&pixmap->drawable, &region);

	damage(pixmap, priv, NULL);

	assert(bo->refcnt);
	if (priv->gpu_bo != bo) {
		if (priv->gpu_bo) {
			sna_pixmap_unmap(pixmap, priv);
			kgem_bo_destroy(&sna->kgem, priv->gpu_bo);
		}
		priv->gpu_bo = ref(bo);
	}
	if (bo->domain != DOMAIN_GPU)
		bo->domain = DOMAIN_NONE;

	DamageRegionProcessPending(&pixmap->drawable);
}

static void sna_dri_select_mode(struct sna *sna, struct kgem_bo *dst, struct kgem_bo *src, bool sync)
{
	struct drm_i915_gem_busy busy;
	int mode;

	if (sna->kgem.gen < 060)
		return;

	if (sync) {
		DBG(("%s: sync, force %s ring\n", __FUNCTION__,
		     sna->kgem.gen >= 070 ? "BLT" : "RENDER"));
		kgem_set_mode(&sna->kgem,
			      sna->kgem.gen >= 070 ? KGEM_BLT : KGEM_RENDER,
			      dst);
		return;
	}

	if (sna->kgem.mode != KGEM_NONE) {
		DBG(("%s: busy, not switching\n", __FUNCTION__));
		return;
	}

	VG_CLEAR(busy);
	busy.handle = src->handle;
	if (drmIoctl(sna->kgem.fd, DRM_IOCTL_I915_GEM_BUSY, &busy))
		return;

	DBG(("%s: src busy?=%x\n", __FUNCTION__, busy.busy));
	if (busy.busy == 0) {
		busy.handle = dst->handle;
		if (drmIoctl(sna->kgem.fd, DRM_IOCTL_I915_GEM_BUSY, &busy))
			return;
		DBG(("%s: dst busy?=%x\n", __FUNCTION__, busy.busy));
		if (busy.busy == 0) {
			DBG(("%s: src/dst is idle, using defaults\n", __FUNCTION__));
			return;
		}
	}

	/* Sandybridge introduced a separate ring which it uses to
	 * perform blits. Switching rendering between rings incurs
	 * a stall as we wait upon the old ring to finish and
	 * flush its render cache before we can proceed on with
	 * the operation on the new ring.
	 *
	 * As this buffer, we presume, has just been written to by
	 * the DRI client using the RENDER ring, we want to perform
	 * our operation on the same ring, and ideally on the same
	 * ring as we will flip from (which should be the RENDER ring
	 * as well).
	 *
	 * The ultimate question is whether preserving the ring outweighs
	 * the cost of the query.
	 */
	mode = KGEM_RENDER;
	if (busy.busy & (1 << 17))
		mode = KGEM_BLT;
	kgem_bo_mark_busy(busy.handle == src->handle ? src : dst, mode);
	_kgem_set_mode(&sna->kgem, mode);
}

static void
sna_dri_copy_fallback(struct sna *sna, int bpp,
		      struct kgem_bo *src_bo, int sx, int sy,
		      struct kgem_bo *dst_bo, int dx, int dy,
		      const BoxRec *box, int n)
{
	void *dst = kgem_bo_map__gtt(&sna->kgem, dst_bo);
	void *src = kgem_bo_map__gtt(&sna->kgem, src_bo);

	if (dst == NULL || src == NULL)
		return;

	DBG(("%s: src(%d, %d), dst(%d, %d) x %d\n",
	     __FUNCTION__, sx, sy, dx, dy, n));

	if (sigtrap_get() == 0) {
		do {
			memcpy_blt(src, dst, bpp,
				   src_bo->pitch, dst_bo->pitch,
				   box->x1 + sx, box->y1 + sy,
				   box->x1 + dx, box->y1 + dy,
				   box->x2 - box->x1, box->y2 - box->y1);
			box++;
		} while (--n);
		sigtrap_put();
	}
}

static struct kgem_bo *
__sna_dri_copy_region(struct sna *sna, DrawablePtr draw, RegionPtr region,
		      DRI2BufferPtr src, DRI2BufferPtr dst,
		      bool sync)
{
	PixmapPtr pixmap = get_drawable_pixmap(draw);
	struct sna_dri_private *src_priv = get_private(src);
	struct sna_dri_private *dst_priv = get_private(dst);
	pixman_region16_t clip;
	struct kgem_bo *bo = NULL;
	struct kgem_bo *src_bo;
	struct kgem_bo *dst_bo;
	BoxRec *boxes;
	int16_t dx, dy, sx, sy;
	int w, h, n;

	/* To hide a stale DRI2Buffer, one may choose to substitute
	 * pixmap->gpu_bo instead of dst/src->bo, however you then run
	 * the risk of copying around invalid data. So either you may not
	 * see the results of the copy, or you may see the wrong pixels.
	 * Either way you eventually lose.
	 *
	 * We also have to be careful in case that the stale buffers are
	 * now attached to invalid (non-DRI) pixmaps.
	 */

	assert(dst->attachment == DRI2BufferFrontLeft ||
	       src->attachment == DRI2BufferFrontLeft);
	assert(dst->attachment != src->attachment);

	/* Copy the minimum of the Drawable / src / dst extents */
	w = draw->width;
	if ((src_priv->size & 0xffff) < w)
		w = src_priv->size & 0xffff;
	if ((dst_priv->size & 0xffff) < w)
		w = dst_priv->size & 0xffff;

	h = draw->height;
	if ((src_priv->size >> 16) < h)
		h = src_priv->size >> 16;
	if ((dst_priv->size >> 16) < h)
		h = dst_priv->size >> 16;

	clip.extents.x1 = draw->x;
	clip.extents.y1 = draw->y;
	clip.extents.x2 = draw->x + w;
	clip.extents.y2 = draw->y + h;
	clip.data = NULL;

	if (region) {
		pixman_region_translate(region, draw->x, draw->y);
		pixman_region_intersect(&clip, &clip, region);
		region = &clip;
	}

	if (clip.extents.x1 >= clip.extents.x2 ||
	    clip.extents.y1 >= clip.extents.y2) {
		DBG(("%s: all clipped\n", __FUNCTION__));
		return NULL;
	}

	sx = sy = dx = dy = 0;
	if (dst->attachment == DRI2BufferFrontLeft) {
		sx = -draw->x;
		sy = -draw->y;
	} else {
		dx = -draw->x;
		dy = -draw->y;
	}
	if (draw->type == DRAWABLE_WINDOW) {
		WindowPtr win = (WindowPtr)draw;
		int16_t tx, ty;

		if (is_clipped(&win->clipList, draw)) {
			DBG(("%s: draw=(%d, %d), delta=(%d, %d), clip.extents=(%d, %d), (%d, %d)\n",
						__FUNCTION__, draw->x, draw->y,
						get_drawable_dx(draw), get_drawable_dy(draw),
						win->clipList.extents.x1, win->clipList.extents.y1,
						win->clipList.extents.x2, win->clipList.extents.y2));

			assert(region == NULL || region == &clip);
			pixman_region_intersect(&clip, &win->clipList, &clip);
			if (!pixman_region_not_empty(&clip)) {
				DBG(("%s: all clipped\n", __FUNCTION__));
				return NULL;
			}

			region = &clip;
		}

		if (get_drawable_deltas(draw, pixmap, &tx, &ty)) {
			if (dst->attachment == DRI2BufferFrontLeft) {
				pixman_region_translate(region ?: &clip, tx, ty);
				sx -= tx;
				sy -= ty;
			} else {
				sx += tx;
				sy += ty;
			}
		}
	} else
		sync = false;

	src_bo = src_priv->bo;
	if (src->attachment == DRI2BufferFrontLeft) {
		struct sna_pixmap *priv;

		priv = sna_pixmap_move_to_gpu(pixmap, MOVE_READ);
		if (priv)
			src_bo = priv->gpu_bo;
	}

	dst_bo = dst_priv->bo;
	if (dst->attachment == DRI2BufferFrontLeft) {
		struct sna_pixmap *priv;
		unsigned int flags;

		flags = MOVE_WRITE | __MOVE_FORCE;
		if (clip.data ||
		    clip.extents.x1 > 0 ||
		    clip.extents.x2 < pixmap->drawable.width ||
		    clip.extents.y1 > 0 ||
		    clip.extents.y2 < pixmap->drawable.height)
			flags |= MOVE_READ;

		priv = sna_pixmap_move_to_gpu(pixmap, flags);
		if (priv) {
			damage(pixmap, priv, region);
			dst_bo = priv->gpu_bo;
		}
	} else
		sync = false;

	if (!wedged(sna)) {
		xf86CrtcPtr crtc;

		crtc = NULL;
		if (sync && sna_pixmap_is_scanout(sna, pixmap))
			crtc = sna_covering_crtc(sna, &clip.extents, NULL);
		sna_dri_select_mode(sna, dst_bo, src_bo, crtc != NULL);

		sync = (crtc != NULL&&
			sna_wait_for_scanline(sna, pixmap, crtc,
					      &clip.extents));
	}

	if (region) {
		boxes = REGION_RECTS(region);
		n = REGION_NUM_RECTS(region);
		assert(n);
	} else {
		region = &clip;
		boxes = &clip.extents;
		n = 1;
	}
	DamageRegionAppend(&pixmap->drawable, region);

	if (wedged(sna)) {
		sna_dri_copy_fallback(sna, draw->bitsPerPixel,
				      src_bo, sx, sy,
				      dst_bo, dx, dy,
				      boxes, n);
	} else {
		unsigned flags;

		flags = COPY_LAST;
		if (sync)
			flags |= COPY_SYNC;
		sna->render.copy_boxes(sna, GXcopy,
				       pixmap, src_bo, sx, sy,
				       pixmap, dst_bo, dx, dy,
				       boxes, n, flags);

		DBG(("%s: flushing? %d\n", __FUNCTION__, sync));
		if (sync) { /* STAT! */
			struct kgem_request *rq = sna->kgem.next_request;
			kgem_submit(&sna->kgem);
			if (rq->bo)
				bo = kgem_bo_reference(rq->bo);
		}
	}

	DamageRegionProcessPending(&pixmap->drawable);

	if (clip.data)
		pixman_region_fini(&clip);

	return bo;
}

static void
sna_dri_copy_region(DrawablePtr draw,
		    RegionPtr region,
		    DRI2BufferPtr dst,
		    DRI2BufferPtr src)
{
	PixmapPtr pixmap = get_drawable_pixmap(draw);
	struct sna *sna = to_sna_from_pixmap(pixmap);

	DBG(("%s: pixmap=%ld, src=%u (refs=%d/%d, flush=%d, attach=%d) , dst=%u (refs=%d/%d, flush=%d, attach=%d)\n",
	     __FUNCTION__,
	     pixmap->drawable.serialNumber,
	     get_private(src)->bo->handle,
	     get_private(src)->refcnt,
	     get_private(src)->bo->refcnt,
	     get_private(src)->bo->flush,
	     src->attachment,
	     get_private(dst)->bo->handle,
	     get_private(dst)->refcnt,
	     get_private(dst)->bo->refcnt,
	     get_private(dst)->bo->flush,
	     dst->attachment));

	assert(src != dst);

	assert(get_private(src)->refcnt);
	assert(get_private(dst)->refcnt);

	assert(get_private(src)->bo->refcnt);
	assert(get_private(src)->bo->flush);

	assert(get_private(dst)->bo->refcnt);
	assert(get_private(dst)->bo->flush);

	DBG(("%s: region (%d, %d), (%d, %d) x %ld\n",
	     __FUNCTION__,
	     region->extents.x1, region->extents.y1,
	     region->extents.x2, region->extents.y2,
	     (long)REGION_NUM_RECTS(region)));

	__sna_dri_copy_region(sna, draw, region, src, dst, false);
}

static inline int sna_wait_vblank(struct sna *sna, drmVBlank *vbl)
{
	DBG(("%s\n", __FUNCTION__));
	return drmIoctl(sna->kgem.fd, DRM_IOCTL_WAIT_VBLANK, vbl);
}

#if DRI2INFOREC_VERSION >= 4

struct dri_bo {
	struct list link;
	struct kgem_bo *bo;
	uint32_t name;
};

struct sna_dri_frame_event {
	DrawablePtr draw;
	ClientPtr client;
	enum frame_event_type type;
	int pipe;
	int count;

	/* for swaps & flips only */
	DRI2SwapEventPtr event_complete;
	void *event_data;
	DRI2BufferPtr front;
	DRI2BufferPtr back;
	struct kgem_bo *bo;

	struct sna_dri_frame_event *chain;

	unsigned int fe_frame;
	unsigned int fe_tv_sec;
	unsigned int fe_tv_usec;

	struct {
		struct kgem_bo *bo;
		uint32_t name;
	} scanout[2];

	struct list cache;

	int mode;
};

static inline struct sna_dri_frame_event *
to_frame_event(uintptr_t  data)
{
	 return (struct sna_dri_frame_event *)(data & ~1);
}

static int
sna_dri_get_pipe(DrawablePtr draw)
{
	struct sna *sna = to_sna_from_drawable(draw);
	xf86CrtcPtr crtc;
	BoxRec box;
	int pipe;

	if (draw->type == DRAWABLE_PIXMAP)
		return -1;

	box.x1 = draw->x;
	box.y1 = draw->y;
	box.x2 = box.x1 + draw->width;
	box.y2 = box.y1 + draw->height;

	crtc = sna_covering_crtc(sna, &box, NULL);

	/* Make sure the CRTC is valid and this is the real front buffer */
	pipe = -1;
	if (crtc != NULL)
		pipe = sna_crtc_to_pipe(crtc);

	DBG(("%s(box=((%d, %d), (%d, %d)), pipe=%d)\n",
	     __FUNCTION__, box.x1, box.y1, box.x2, box.y2, pipe));

	return pipe;
}

static struct sna_dri_frame_event *
sna_dri_window_get_chain(WindowPtr win)
{
	return ((void **)__get_private(win, sna_window_key))[1];
}

static void
sna_dri_window_set_chain(WindowPtr win,
			 struct sna_dri_frame_event *chain)
{
	DBG(("%s: head now %p\n", __FUNCTION__, chain));
	assert(win->drawable.type == DRAWABLE_WINDOW);
	((void **)__get_private(win, sna_window_key))[1] = chain;
}

static void
sna_dri_remove_frame_event(WindowPtr win,
			    struct sna_dri_frame_event *info)
{
	struct sna_dri_frame_event *chain;

	DBG(("%s: remove[%p] from window %ld)\n",
	     __FUNCTION__, info, (long)win->drawable.id));

	chain = sna_dri_window_get_chain(win);
	if (chain == NULL)
		return;

	if (chain == info) {
		sna_dri_window_set_chain(win, info->chain);
		return;
	}

	while (chain->chain != info)
		chain = chain->chain;
	assert(chain != info);
	assert(info->chain != chain);
	chain->chain = info->chain;
}

static void
sna_dri_add_frame_event(DrawablePtr draw, struct sna_dri_frame_event *info)
{
	struct sna_dri_frame_event *chain;

	if (draw->type != DRAWABLE_WINDOW)
		return;

	DBG(("%s: add[%p] to window %ld)\n",
	     __FUNCTION__, info, (long)draw->id));

	chain = sna_dri_window_get_chain((WindowPtr)draw);
	if (chain == NULL) {
		sna_dri_window_set_chain((WindowPtr)draw, info);
		return;
	}

	assert(chain != info);
	while (chain->chain != NULL)
		chain = chain->chain;

	assert(chain != info);
	chain->chain = info;
}

static void
sna_dri_frame_event_info_free(struct sna *sna,
			      DrawablePtr draw,
			      struct sna_dri_frame_event *info)
{
	if (draw && draw->type == DRAWABLE_WINDOW)
		sna_dri_remove_frame_event((WindowPtr)draw, info);
	_sna_dri_destroy_buffer(sna, info->front);
	_sna_dri_destroy_buffer(sna, info->back);

	assert(info->scanout[1].bo == NULL);

	if (info->scanout[0].bo) {
		assert(info->scanout[0].bo->scanout);
		kgem_bo_destroy(&sna->kgem, info->scanout[0].bo);
	}

	while (!list_is_empty(&info->cache)) {
		struct dri_bo *c;

		c = list_first_entry(&info->cache, struct dri_bo, link);
		list_del(&c->link);

		if (c->bo) {
			assert(c->bo->refcnt == 1);
			kgem_bo_destroy(&sna->kgem, c->bo);
		}

		free(c);
	}

	if (info->bo)
		kgem_bo_destroy(&sna->kgem, info->bo);

	free(info);
}

void sna_dri_destroy_window(WindowPtr win)
{
	struct sna *sna = to_sna_from_drawable(&win->drawable);
	struct sna_dri_frame_event *info, *chain;

	info = sna_dri_window_get_chain(win);
	if (info == NULL)
		return;

	DBG(("%s: window=%ld\n", __FUNCTION__, win->drawable.serialNumber));
	info->draw = NULL;

	chain = info->chain;
	info->chain = NULL;

	while ((info = chain)) {
		chain = info->chain;
		sna_dri_frame_event_info_free(sna, NULL, info);
	}
}

static bool
sna_dri_page_flip(struct sna *sna, struct sna_dri_frame_event *info)
{
	struct kgem_bo *bo = get_private(info->back)->bo;
	struct dri_bo tmp;

	DBG(("%s()\n", __FUNCTION__));

	assert(sna_pixmap_get_buffer(sna->front) == info->front);
	assert(get_drawable_pixmap(info->draw)->drawable.height * bo->pitch <= kgem_bo_size(bo));
	assert(info->scanout[0].bo);
	assert(info->scanout[0].bo->scanout);
	assert(info->scanout[1].bo == NULL);
	assert(bo->refcnt);

	info->count = sna_page_flip(sna, bo, info, info->pipe);
	if (!info->count)
		return false;

	DBG(("%s: mark handle=%d as scanout, swap front (handle=%d, name=%d) and back (handle=%d, name=%d)\n",
	     __FUNCTION__, bo->handle,
	     get_private(info->front)->bo->handle, info->front->name,
	     get_private(info->back)->bo->handle, info->back->name));

	info->scanout[1] = info->scanout[0];
	info->scanout[0].bo = ref(bo);
	info->scanout[0].name = info->back->name;
	assert(info->scanout[0].bo->scanout);

	tmp.bo = get_private(info->front)->bo;
	tmp.name = info->front->name;

	set_bo(sna->front, bo);

	info->front->name = info->back->name;
	get_private(info->front)->bo = bo;

	info->back->name = tmp.name;
	get_private(info->back)->bo = tmp.bo;

	sna->dri.flip_pending = info;
	return true;
}

static bool
can_flip(struct sna * sna,
	 DrawablePtr draw,
	 DRI2BufferPtr front,
	 DRI2BufferPtr back)
{
	WindowPtr win = (WindowPtr)draw;
	PixmapPtr pixmap;

	assert((sna->flags & SNA_NO_WAIT) == 0);

	if (draw->type == DRAWABLE_PIXMAP)
		return false;

	if (!sna->scrn->vtSema) {
		DBG(("%s: no, not attached to VT\n", __FUNCTION__));
		return false;
	}

	if (sna->flags & SNA_NO_FLIP) {
		DBG(("%s: no, pageflips disabled\n", __FUNCTION__));
		return false;
	}

	if (front->format != back->format) {
		DBG(("%s: no, format mismatch, front = %d, back = %d\n",
		     __FUNCTION__, front->format, back->format));
		return false;
	}

	if (front->attachment != DRI2BufferFrontLeft) {
		DBG(("%s: no, front attachment [%d] is not FrontLeft [%d]\n",
		     __FUNCTION__,
		     front->attachment,
		     DRI2BufferFrontLeft));
		return false;
	}

	if (sna->mode.shadow_active) {
		DBG(("%s: no, shadow enabled\n", __FUNCTION__));
		return false;
	}

	pixmap = get_window_pixmap(win);
	if (pixmap != sna->front) {
		DBG(("%s: no, window (pixmap=%ld) is not attached to the front buffer (pixmap=%ld)\n",
		     __FUNCTION__, pixmap->drawable.serialNumber, sna->front->drawable.serialNumber));
		return false;
	}

	if (sna_pixmap_get_buffer(pixmap) != front) {
		DBG(("%s: no, DRI2 drawable is no longer attached (old name=%d, new name=%d) to pixmap=%ld\n",
		     __FUNCTION__, front->name,
		     sna_pixmap_get_buffer(pixmap) ? ((DRI2BufferPtr)sna_pixmap_get_buffer(pixmap))->name : 0,
		     pixmap->drawable.serialNumber));
		return false;
	}

	assert(get_private(front)->pixmap == sna->front);
	assert(sna_pixmap(sna->front)->gpu_bo == get_private(front)->bo);

	if (!get_private(back)->scanout) {
		DBG(("%s: no, DRI2 drawable was too small at time of creation)\n",
		     __FUNCTION__));
		return false;
	}

	if (get_private(back)->size != get_private(front)->size) {
		DBG(("%s: no, DRI2 drawable does not fit into scanout\n",
		     __FUNCTION__));
		return false;
	}

	DBG(("%s: window size: %dx%d, clip=(%d, %d), (%d, %d) x %d\n",
	     __FUNCTION__,
	     win->drawable.width, win->drawable.height,
	     win->clipList.extents.x1, win->clipList.extents.y1,
	     win->clipList.extents.x2, win->clipList.extents.y2,
	     RegionNumRects(&win->clipList)));
	if (!RegionEqual(&win->clipList, &draw->pScreen->root->winSize)) {
		DBG(("%s: no, window is clipped: clip region=(%d, %d), (%d, %d), root size=(%d, %d), (%d, %d)\n",
		     __FUNCTION__,
		     win->clipList.extents.x1,
		     win->clipList.extents.y1,
		     win->clipList.extents.x2,
		     win->clipList.extents.y2,
		     draw->pScreen->root->winSize.extents.x1,
		     draw->pScreen->root->winSize.extents.y1,
		     draw->pScreen->root->winSize.extents.x2,
		     draw->pScreen->root->winSize.extents.y2));
		return false;
	}

	if (draw->x != 0 || draw->y != 0 ||
#ifdef COMPOSITE
	    draw->x != pixmap->screen_x ||
	    draw->y != pixmap->screen_y ||
#endif
	    draw->width != pixmap->drawable.width ||
	    draw->height != pixmap->drawable.height) {
		DBG(("%s: no, window is not full size (%dx%d)!=(%dx%d)\n",
		     __FUNCTION__,
		     draw->width, draw->height,
		     pixmap->drawable.width,
		     pixmap->drawable.height));
		return false;
	}

	/* prevent an implicit tiling mode change */
	if (get_private(front)->bo->tiling != get_private(back)->bo->tiling) {
		DBG(("%s -- no, tiling mismatch: front %d, back=%d\n",
		     __FUNCTION__,
		     get_private(front)->bo->tiling,
		     get_private(back)->bo->tiling));
		return false;
	}

	if (sna_pixmap(pixmap)->pinned & ~(PIN_DRI2 | PIN_SCANOUT)) {
		DBG(("%s -- no, pinned: front %x\n",
		     __FUNCTION__, sna_pixmap(pixmap)->pinned));
		return false;
	}

	return true;
}

inline static uint32_t pipe_select(int pipe)
{
	/* The third pipe was introduced with IvyBridge long after
	 * multiple pipe support was added to the kernel, hence
	 * we can safely ignore the capability check - if we have more
	 * than two pipes, we can assume that they are fully supported.
	 */
	if (pipe > 1)
		return pipe << DRM_VBLANK_HIGH_CRTC_SHIFT;
	else if (pipe > 0)
		return DRM_VBLANK_SECONDARY;
	else
		return 0;
}

static void
sna_dri_exchange_buffers(DrawablePtr draw,
			 DRI2BufferPtr front,
			 DRI2BufferPtr back)
{
	struct kgem_bo *back_bo, *front_bo;
	PixmapPtr pixmap;
	int tmp;

	pixmap = get_drawable_pixmap(draw);

	back_bo = get_private(back)->bo;
	front_bo = get_private(front)->bo;

	DBG(("%s: exchange front=%d/%d and back=%d/%d, pixmap=%ld %dx%d\n",
	     __FUNCTION__,
	     front_bo->handle, front->name,
	     back_bo->handle, back->name,
	     pixmap->drawable.serialNumber,
	     pixmap->drawable.width,
	     pixmap->drawable.height));

	DBG(("%s: back_bo pitch=%d, size=%d, ref=%d\n",
	     __FUNCTION__, back_bo->pitch, kgem_bo_size(back_bo), back_bo->refcnt));
	DBG(("%s: front_bo pitch=%d, size=%d, ref=%d\n",
	     __FUNCTION__, front_bo->pitch, kgem_bo_size(front_bo), front_bo->refcnt));
	assert(front_bo->refcnt);
	assert(back_bo->refcnt);

	assert(sna_pixmap_get_buffer(pixmap) == front);
	assert(pixmap->drawable.height * back_bo->pitch <= kgem_bo_size(back_bo));
	assert(pixmap->drawable.height * front_bo->pitch <= kgem_bo_size(front_bo));

	set_bo(pixmap, back_bo);

	get_private(front)->bo = back_bo;
	get_private(back)->bo = front_bo;

	tmp = front->name;
	front->name = back->name;
	back->name = tmp;
}

static void chain_swap(struct sna *sna,
		       DrawablePtr draw,
		       int frame, unsigned int tv_sec, unsigned int tv_usec,
		       struct sna_dri_frame_event *chain)
{
	drmVBlank vbl;

	assert(chain == sna_dri_window_get_chain((WindowPtr)draw));
	DBG(("%s: chaining type=%d\n", __FUNCTION__, chain->type));
	switch (chain->type) {
	case DRI2_SWAP_THROTTLE:
		break;
	default:
		return;
	}

	DBG(("%s: emitting chained vsync'ed blit\n", __FUNCTION__));

	if (sna->mode.shadow_flip && !sna->mode.shadow_damage) {
		/* recursed from wait_for_shadow(), simply requeue */
		DBG(("%s -- recursed from wait_for_shadow(), requeuing\n", __FUNCTION__));
		chain->type = DRI2_SWAP;

		VG_CLEAR(vbl);
		vbl.request.type =
			DRM_VBLANK_RELATIVE |
			DRM_VBLANK_EVENT |
			pipe_select(chain->pipe);
		vbl.request.sequence = 1;
		vbl.request.signal = (unsigned long)chain;

		if (!sna_wait_vblank(sna, &vbl))
			return;

		DBG(("%s -- requeue failed, errno=%d\n", __FUNCTION__, errno));
	} else {
		chain->bo = __sna_dri_copy_region(sna, draw, NULL,
						  chain->back, chain->front, true);
	}

	DRI2SwapComplete(chain->client, draw,
			 frame, tv_sec, tv_usec,
			 DRI2_BLIT_COMPLETE,
			 chain->client ? chain->event_complete : NULL, chain->event_data);

	VG_CLEAR(vbl);
	vbl.request.type =
		DRM_VBLANK_RELATIVE |
		DRM_VBLANK_NEXTONMISS |
		DRM_VBLANK_EVENT |
		pipe_select(chain->pipe);
	vbl.request.sequence = 0;
	vbl.request.signal = (unsigned long)chain;
	if (sna_wait_vblank(sna, &vbl))
		sna_dri_frame_event_info_free(sna, draw, chain);
}

static bool sna_dri_blit_complete(struct sna *sna,
				  struct sna_dri_frame_event *info)
{
	if (info->bo && __kgem_bo_is_busy(&sna->kgem, info->bo)) {
		drmVBlank vbl;

		DBG(("%s: vsync'ed blit is still busy, postponing\n",
		     __FUNCTION__));

		VG_CLEAR(vbl);
		vbl.request.type =
			DRM_VBLANK_RELATIVE |
			DRM_VBLANK_EVENT |
			pipe_select(info->pipe);
		vbl.request.sequence = 1;
		vbl.request.signal = (unsigned long)info;
		if (!sna_wait_vblank(sna, &vbl))
			return false;
	}

	DBG(("%s: blit finished\n", __FUNCTION__));
	return true;
}

void sna_dri_vblank_handler(struct sna *sna, struct drm_event_vblank *event)
{
	struct sna_dri_frame_event *info = (void *)(uintptr_t)event->user_data;
	DrawablePtr draw;

	DBG(("%s(type=%d)\n", __FUNCTION__, info->type));

	draw = info->draw;
	if (draw == NULL) {
		DBG(("%s -- drawable gone\n", __FUNCTION__));
		goto done;
	}

	switch (info->type) {
	case DRI2_FLIP:
		/* If we can still flip... */
		if (can_flip(sna, draw, info->front, info->back) &&
		    sna_dri_page_flip(sna, info))
			return;

		/* else fall through to blit */
	case DRI2_SWAP:
		if (sna->mode.shadow_flip && !sna->mode.shadow_damage) {
			drmVBlank vbl;

			/* recursed from wait_for_shadow(), simply requeue */
			DBG(("%s -- recursed from wait_for_shadow(), requeuing\n", __FUNCTION__));

			VG_CLEAR(vbl);
			vbl.request.type =
				DRM_VBLANK_RELATIVE |
				DRM_VBLANK_EVENT |
				pipe_select(info->pipe);
			vbl.request.sequence = 1;
			vbl.request.signal = (unsigned long)info;

			if (!sna_wait_vblank(sna, &vbl))
				return;

			DBG(("%s -- requeue failed, errno=%d\n", __FUNCTION__, errno));
		} else {
			info->bo = __sna_dri_copy_region(sna, draw, NULL,
							 info->back, info->front, true);
			info->type = DRI2_SWAP_WAIT;
		}
		/* fall through to SwapComplete */
	case DRI2_SWAP_WAIT:
		if (!sna_dri_blit_complete(sna, info))
			return;

		DRI2SwapComplete(info->client,
				 draw, event->sequence,
				 event->tv_sec, event->tv_usec,
				 DRI2_BLIT_COMPLETE,
				 info->client ? info->event_complete : NULL,
				 info->event_data);
		break;

	case DRI2_SWAP_THROTTLE:
		DBG(("%s: %d complete, frame=%d tv=%d.%06d\n",
		     __FUNCTION__, info->type,
		     event->sequence, event->tv_sec, event->tv_usec));
		break;

	case DRI2_WAITMSC:
		DRI2WaitMSCComplete(info->client, draw,
				    event->sequence,
				    event->tv_sec,
				    event->tv_usec);
		break;
	default:
		xf86DrvMsg(sna->scrn->scrnIndex, X_WARNING,
			   "%s: unknown vblank event received\n", __func__);
		/* Unknown type */
		break;
	}

	if (info->chain) {
		sna_dri_remove_frame_event((WindowPtr)draw, info);
		chain_swap(sna, draw,
			   event->sequence, event->tv_sec, event->tv_usec,
			   info->chain);
		draw = NULL;
	}

done:
	sna_dri_frame_event_info_free(sna, draw, info);
}

static bool
sna_dri_immediate_blit(struct sna *sna,
		       struct sna_dri_frame_event *info,
		       bool sync, bool event)
{
	DrawablePtr draw = info->draw;
	bool ret = false;

	if (sna->flags & SNA_NO_WAIT)
		sync = false;

	DBG(("%s: emitting immediate blit, throttling client, synced? %d, chained? %d, send-event? %d\n",
	     __FUNCTION__, sync, sna_dri_window_get_chain((WindowPtr)draw) != info,
	     event));

	if (sync) {
		info->type = DRI2_SWAP_THROTTLE;
		if (sna_dri_window_get_chain((WindowPtr)draw) == info) {
			drmVBlank vbl;

			DBG(("%s: no pending blit, starting chain\n",
			     __FUNCTION__));

			info->bo = __sna_dri_copy_region(sna, draw, NULL,
							 info->back,
							 info->front,
							 true);
			if (event) {
				DRI2SwapComplete(info->client, draw, 0, 0, 0,
						 DRI2_BLIT_COMPLETE,
						 info->event_complete,
						 info->event_data);

				VG_CLEAR(vbl);
				vbl.request.type =
					DRM_VBLANK_RELATIVE |
					DRM_VBLANK_NEXTONMISS |
					DRM_VBLANK_EVENT |
					pipe_select(info->pipe);
				vbl.request.sequence = 0;
				vbl.request.signal = (unsigned long)info;
				ret = !sna_wait_vblank(sna, &vbl);
			}
		} else {
			DBG(("%s: pending blit, chained\n", __FUNCTION__));
			ret = true;
		}
	} else {
		DBG(("%s: immediate blit\n", __FUNCTION__));
		info->bo = __sna_dri_copy_region(sna, draw, NULL,
						 info->back, info->front, false);
		if (event)
			DRI2SwapComplete(info->client, draw, 0, 0, 0,
					 DRI2_BLIT_COMPLETE,
					 info->event_complete,
					 info->event_data);
	}

	DBG(("%s: continue? %d\n", __FUNCTION__, ret));
	return ret;
}

static void
sna_dri_flip_get_back(struct sna *sna, struct sna_dri_frame_event *info)
{
	struct kgem_bo *bo;
	uint32_t name;

	DBG(("%s: scanout=(%d, %d), back=%d, cache?=%d\n",
	     __FUNCTION__,
	     info->scanout[0].bo ? info->scanout[0].bo->handle : 0,
	     info->scanout[1].bo ? info->scanout[1].bo->handle : 0,
	     get_private(info->back)->bo->handle,
	     !list_is_empty(&info->cache)));

	bo = get_private(info->back)->bo;
	assert(bo->refcnt);
	assert(bo->flush);
	if (!(bo == info->scanout[0].bo || bo == info->scanout[1].bo)) {
		DBG(("%s: reuse unattached back\n", __FUNCTION__));
		return;
	}

	bo = NULL;
	if (!list_is_empty(&info->cache)) {
		struct dri_bo *c = list_first_entry(&info->cache, struct dri_bo, link);
		if (c->bo) {
			bo = c->bo;
			name = c->name;
			DBG(("%s: reuse cache handle=%d,name=%d\n", __FUNCTION__,
			     bo->handle, name));
			list_move_tail(&c->link, &info->cache);
			c->bo = NULL;
		}
	}
	if (bo == NULL) {
		DBG(("%s: allocating new backbuffer\n", __FUNCTION__));
		bo = kgem_create_2d(&sna->kgem,
				    info->draw->width,
				    info->draw->height,
				    info->draw->bitsPerPixel,
				    get_private(info->front)->bo->tiling,
				    CREATE_SCANOUT);
		if (bo == NULL)
			return;

		name = kgem_bo_flink(&sna->kgem, bo);
		if (name == 0) {
			kgem_bo_destroy(&sna->kgem, bo);
			return;
		}
	}

	assert(!(bo == info->scanout[0].bo || bo == info->scanout[1].bo));
	assert(name);

	unref(get_private(info->back)->bo);
	get_private(info->back)->bo = bo;
	info->back->name = name;

	assert(get_private(info->back)->bo != info->scanout[0].bo);
	assert(get_private(info->back)->bo != info->scanout[1].bo);

	assert(bo->refcnt == 1);
	assert(bo->flush);
}

static bool
sna_dri_flip_continue(struct sna *sna, struct sna_dri_frame_event *info)
{
	DBG(("%s(mode=%d)\n", __FUNCTION__, info->mode));

	if (info->mode > 1){
		if (get_private(info->front)->bo != sna_pixmap(sna->front)->gpu_bo)
			return false;

		info->count = sna_page_flip(sna,
					    get_private(info->front)->bo,
					    info, info->pipe);
		if (!info->count)
			return false;

		assert(info->scanout[0].bo->scanout);
		info->scanout[1] = info->scanout[0];
		info->scanout[0].bo = ref(get_private(info->front)->bo);
		info->scanout[0].name = info->front->name;
		assert(info->scanout[0].bo->scanout);
		sna->dri.flip_pending = info;
	} else {
		if (!info->draw)
			return false;

		if (!can_flip(sna, info->draw, info->front, info->back))
			return false;

		assert(sna_pixmap_get_buffer(get_drawable_pixmap(info->draw)) == info->front);
		if (!sna_dri_page_flip(sna, info))
			return false;

		sna_dri_flip_get_back(sna, info);
		DRI2SwapComplete(info->client, info->draw,
				 0, 0, 0,
				 DRI2_FLIP_COMPLETE,
				 info->client ? info->event_complete : NULL,
				 info->event_data);
	}

	info->mode = 0;
	return true;
}

static void chain_flip(struct sna *sna)
{
	struct sna_dri_frame_event *chain = sna->dri.flip_pending;

	assert(chain->type == DRI2_FLIP);
	DBG(("%s: chaining type=%d\n", __FUNCTION__, chain->type));

	sna->dri.flip_pending = NULL;
	if (chain->draw == NULL) {
		sna_dri_frame_event_info_free(sna, NULL, chain);
		return;
	}

	assert(chain == sna_dri_window_get_chain((WindowPtr)chain->draw));

	if (chain->type == DRI2_FLIP &&
	    can_flip(sna, chain->draw, chain->front, chain->back) &&
	    sna_dri_page_flip(sna, chain)) {
		DBG(("%s: performing chained flip\n", __FUNCTION__));
	} else {
		DBG(("%s: emitting chained vsync'ed blit\n", __FUNCTION__));
		chain->bo = __sna_dri_copy_region(sna, chain->draw, NULL,
						  chain->back, chain->front,
						  true);
		DRI2SwapComplete(chain->client, chain->draw, 0, 0, 0,
				 DRI2_BLIT_COMPLETE, chain->client ? chain->event_complete : NULL, chain->event_data);
		sna_dri_frame_event_info_free(sna, chain->draw, chain);
	}
}

static void sna_dri_flip_event(struct sna *sna,
			       struct sna_dri_frame_event *flip)
{
	DBG(("%s(frame=%d, tv=%d.%06d, type=%d)\n",
	     __FUNCTION__,
	     flip->fe_frame,
	     flip->fe_tv_sec,
	     flip->fe_tv_usec,
	     flip->type));

	assert(!sna->mode.shadow_flip);

	if (flip->scanout[1].bo) {
		struct dri_bo *c = NULL;

		DBG(("%s: retiring previous scanout handle=%d, name=%d, refcnt=%d\n",
		     __FUNCTION__,
		     flip->scanout[1].bo->handle,
		     flip->scanout[1].name,
		     flip->scanout[1].bo->refcnt));

		if (flip->scanout[1].bo != flip->scanout[0].bo &&
		    flip->scanout[1].bo->refcnt == 1) {
			if (!list_is_empty(&flip->cache))
				c = list_last_entry(&flip->cache, struct dri_bo, link);
			if (c) {
				if (c->bo == NULL)
					_list_del(&c->link);
				else
					c = NULL;
			}
			if (c == NULL)
				c = malloc(sizeof(*c));
			if (c != NULL) {
				c->bo = flip->scanout[1].bo;
				c->name = flip->scanout[1].name;
				list_add(&c->link, &flip->cache);
			}
		}

		if (c == NULL)
			kgem_bo_destroy(&sna->kgem, flip->scanout[1].bo);

		flip->scanout[1].bo = NULL;
	}

	if (sna->dri.flip_pending == flip)
		sna->dri.flip_pending = NULL;

	/* We assume our flips arrive in order, so we don't check the frame */
	switch (flip->type) {
	case DRI2_FLIP:
		DBG(("%s: flip complete (drawable gone? %d), msc=%d\n",
		     __FUNCTION__, flip->draw == NULL, flip->fe_frame));
		if (flip->draw)
			DRI2SwapComplete(flip->client, flip->draw,
					 flip->fe_frame,
					 flip->fe_tv_sec,
					 flip->fe_tv_usec,
					 DRI2_FLIP_COMPLETE,
					 flip->client ? flip->event_complete : NULL,
					 flip->event_data);

		sna_dri_frame_event_info_free(sna, flip->draw, flip);

		if (sna->dri.flip_pending)
			chain_flip(sna);
		break;

	case DRI2_FLIP_THROTTLE:
		if (sna->dri.flip_pending) {
			sna_dri_frame_event_info_free(sna, flip->draw, flip);
			chain_flip(sna);
		} else if (!flip->mode) {
			DBG(("%s: flip chain complete\n", __FUNCTION__));

			if (flip->chain) {
				sna_dri_remove_frame_event((WindowPtr)flip->draw,
							   flip);
				chain_swap(sna, flip->draw,
					   flip->fe_frame,
					   flip->fe_tv_sec,
					   flip->fe_tv_usec,
					   flip->chain);
				flip->draw = NULL;
			}

			sna_dri_frame_event_info_free(sna, flip->draw, flip);
		} else if (!sna_dri_flip_continue(sna, flip)) {
			DBG(("%s: no longer able to flip\n", __FUNCTION__));
			if (flip->draw == NULL || !sna_dri_immediate_blit(sna, flip, false, flip->mode == 1))
				sna_dri_frame_event_info_free(sna, flip->draw, flip);
		}
		break;

	default: /* Unknown type */
		xf86DrvMsg(sna->scrn->scrnIndex, X_WARNING,
			   "%s: unknown vblank event received\n", __func__);
		sna_dri_frame_event_info_free(sna, flip->draw, flip);
		if (sna->dri.flip_pending)
			chain_flip(sna);
		break;
	}
}

void
sna_dri_page_flip_handler(struct sna *sna,
			  struct drm_event_vblank *event)
{
	struct sna_dri_frame_event *info = to_frame_event(event->user_data);

	DBG(("%s: pending flip_count=%d\n", __FUNCTION__, info->count));
	assert(info->count > 0);

	/* Is this the event whose info shall be delivered to higher level? */
	if (event->user_data & 1) {
		info->fe_frame = event->sequence;
		info->fe_tv_sec = event->tv_sec;
		info->fe_tv_usec = event->tv_usec;
	}

	if (--info->count)
		return;

	sna_dri_flip_event(sna, info);
}

static CARD64
get_current_msc_for_target(struct sna *sna, CARD64 target_msc, int pipe)
{
	CARD64 ret = -1;

	if (target_msc && (sna->flags & SNA_NO_WAIT) == 0) {
		drmVBlank vbl;

		VG_CLEAR(vbl);
		vbl.request.type = DRM_VBLANK_RELATIVE | pipe_select(pipe);
		vbl.request.sequence = 0;
		if (sna_wait_vblank(sna, &vbl) == 0)
			ret = vbl.reply.sequence;
	}

	return ret;
}

static Bool find(pointer value, XID id, pointer cdata)
{
	return TRUE;
}

static int use_triple_buffer(struct sna *sna, ClientPtr client)
{
	struct sna_client *priv;

	if ((sna->flags & SNA_TRIPLE_BUFFER) == 0)
		return DRI2_FLIP;

	/* Hack: Disable triple buffering for compositors */

#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,12,99,901,0)
	priv = sna_client(client);
	if (priv->is_compositor == 0)
		priv->is_compositor =
			LookupClientResourceComplex(client,
						    CompositeClientWindowType+1,
						    find, NULL) ? DRI2_FLIP : DRI2_FLIP_THROTTLE;

	return priv->is_compositor;
#else
	return DRI2_FLIP_THROTTLE;
#endif
}

static bool
sna_dri_schedule_flip(ClientPtr client, DrawablePtr draw,
		      DRI2BufferPtr front, DRI2BufferPtr back, int pipe,
		      CARD64 *target_msc, CARD64 divisor, CARD64 remainder,
		      DRI2SwapEventPtr func, void *data)
{
	struct sna *sna = to_sna_from_drawable(draw);
	struct sna_dri_frame_event *info;
	drmVBlank vbl;
	CARD64 current_msc;

	current_msc = get_current_msc_for_target(sna, *target_msc, pipe);

	DBG(("%s: target_msc=%u, current_msc=%u, divisor=%u\n", __FUNCTION__,
	     (uint32_t)*target_msc, (uint32_t)current_msc, (uint32_t)divisor));

	if (divisor == 0 && current_msc >= *target_msc - 1) {
		info = sna->dri.flip_pending;

		DBG(("%s: performing immediate swap on pipe %d, pending? %d, mode: %d\n",
		     __FUNCTION__, pipe, info != NULL, info ? info->mode : 0));

		if (info && info->draw == draw) {
			assert(info->type == DRI2_FLIP_THROTTLE);
			assert(info->front == front);
			if (info->back != back) {
				_sna_dri_destroy_buffer(sna, info->back);
				info->back = back;
				sna_dri_reference_buffer(back);
			}
			if (current_msc >= *target_msc) {
				DBG(("%s: executing xchg of pending flip\n",
				     __FUNCTION__));
				sna_dri_exchange_buffers(draw, front, back);
				info->mode = 2;
				current_msc = *target_msc;
				goto new_back;
			} else {
				DBG(("%s: chaining flip\n", __FUNCTION__));
				info->mode = 1;
				current_msc++;
				goto out;
			}
		}

		info = calloc(1, sizeof(struct sna_dri_frame_event));
		if (info == NULL)
			return false;

		list_init(&info->cache);
		info->type = use_triple_buffer(sna, client);
		info->draw = draw;
		info->client = client;
		info->event_complete = func;
		info->event_data = data;
		info->front = front;
		info->back = back;
		info->pipe = pipe;

		info->scanout[0].bo = ref(get_private(front)->bo);
		info->scanout[0].name = info->front->name;
		assert(info->scanout[0].bo->scanout);

		sna_dri_add_frame_event(draw, info);
		sna_dri_reference_buffer(front);
		sna_dri_reference_buffer(back);

		if (sna->dri.flip_pending) {
			/* We need to first wait (one vblank) for the
			 * async flips to complete before this client
			 * can take over.
			 */
			DBG(("%s: queueing flip after pending completion\n",
			     __FUNCTION__));
			info->type = DRI2_FLIP;
			sna->dri.flip_pending = info;
		} else {
			if (!sna_dri_page_flip(sna, info)) {
				sna_dri_frame_event_info_free(sna, draw, info);
				return false;
			}
		}

		current_msc++;
		if (info->type != DRI2_FLIP) {
new_back:
			sna_dri_flip_get_back(sna, info);
			DRI2SwapComplete(client, draw, 0, 0, 0,
					 DRI2_EXCHANGE_COMPLETE,
					 func, data);
		}
out:
		DBG(("%s: target_msc=%lu\n", __FUNCTION__, (unsigned long)current_msc));
		*target_msc = current_msc;
		return true;
	}

	info = calloc(1, sizeof(struct sna_dri_frame_event));
	if (info == NULL)
		return false;

	list_init(&info->cache);
	info->draw = draw;
	info->client = client;
	info->event_complete = func;
	info->event_data = data;
	info->front = front;
	info->back = back;
	info->pipe = pipe;
	info->type = DRI2_FLIP;

	info->scanout[0].bo = ref(get_private(front)->bo);
	info->scanout[0].name = info->front->name;
	assert(info->scanout[0].bo->scanout);

	sna_dri_add_frame_event(draw, info);
	sna_dri_reference_buffer(front);
	sna_dri_reference_buffer(back);

	*target_msc &= 0xffffffff;
	remainder &= 0xffffffff;

	VG_CLEAR(vbl);

	vbl.request.type =
		DRM_VBLANK_ABSOLUTE |
		DRM_VBLANK_EVENT |
		pipe_select(pipe);

	/*
	 * If divisor is zero, or current_msc is smaller than target_msc
	 * we just need to make sure target_msc passes before initiating
	 * the swap.
	 */
	if (current_msc <= *target_msc - 1) {
		DBG(("%s: waiting for swap: current=%d, target=%d, divisor=%d\n",
		     __FUNCTION__,
		     (int)current_msc,
		     (int)*target_msc,
		     (int)divisor));
		vbl.request.sequence = *target_msc;
	} else {
		DBG(("%s: missed target, queueing event for next: current=%d, target=%d, divisor=%d\n",
		     __FUNCTION__,
		     (int)current_msc,
		     (int)*target_msc,
		     (int)divisor));

		if (divisor == 0)
			divisor = 1;

		vbl.request.sequence = current_msc - current_msc % divisor + remainder;

		/*
		 * If the calculated deadline vbl.request.sequence is
		 * smaller than or equal to current_msc, it means
		 * we've passed the last point when effective onset
		 * frame seq could satisfy *seq % divisor == remainder,
		 * so we need to wait for the next time this will
		 * happen.
		 *
		 * This comparison takes the 1 frame swap delay
		 * in pageflipping mode into account.
		 */
		if (vbl.request.sequence <= current_msc)
			vbl.request.sequence += divisor;

		/* Adjust returned value for 1 frame pageflip offset */
		*target_msc = vbl.reply.sequence;
	}

	/* Account for 1 frame extra pageflip delay */
	vbl.request.sequence -= 1;
	vbl.request.signal = (unsigned long)info;
	if (sna_wait_vblank(sna, &vbl)) {
		sna_dri_frame_event_info_free(sna, draw, info);
		return false;
	}

	return true;
}

/*
 * ScheduleSwap is responsible for requesting a DRM vblank event for the
 * appropriate frame.
 *
 * In the case of a blit (e.g. for a windowed swap) or buffer exchange,
 * the vblank requested can simply be the last queued swap frame + the swap
 * interval for the drawable.
 *
 * In the case of a page flip, we request an event for the last queued swap
 * frame + swap interval - 1, since we'll need to queue the flip for the frame
 * immediately following the received event.
 *
 * The client will be blocked if it tries to perform further GL commands
 * after queueing a swap, though in the Intel case after queueing a flip, the
 * client is free to queue more commands; they'll block in the kernel if
 * they access buffers busy with the flip.
 *
 * When the swap is complete, the driver should call into the server so it
 * can send any swap complete events that have been requested.
 */
static int
sna_dri_schedule_swap(ClientPtr client, DrawablePtr draw, DRI2BufferPtr front,
		      DRI2BufferPtr back, CARD64 *target_msc, CARD64 divisor,
		      CARD64 remainder, DRI2SwapEventPtr func, void *data)
{
	struct sna *sna = to_sna_from_drawable(draw);
	drmVBlank vbl;
	int pipe;
	struct sna_dri_frame_event *info = NULL;
	enum frame_event_type swap_type = DRI2_SWAP;
	CARD64 current_msc;

	DBG(("%s: pixmap=%ld, back=%u (refs=%d/%d, flush=%d) , fron=%u (refs=%d/%d, flush=%d)\n",
	     __FUNCTION__,
	     get_drawable_pixmap(draw)->drawable.serialNumber,
	     get_private(back)->bo->handle,
	     get_private(back)->refcnt,
	     get_private(back)->bo->refcnt,
	     get_private(back)->bo->flush,
	     get_private(front)->bo->handle,
	     get_private(front)->refcnt,
	     get_private(front)->bo->refcnt,
	     get_private(front)->bo->flush));

	DBG(("%s(target_msc=%llu, divisor=%llu, remainder=%llu)\n",
	     __FUNCTION__,
	     (long long)*target_msc,
	     (long long)divisor,
	     (long long)remainder));

	/* Truncate to match kernel interfaces; means occasional overflow
	 * misses, but that's generally not a big deal */
	*target_msc &= 0xffffffff;
	divisor &= 0xffffffff;
	remainder &= 0xffffffff;

	assert(get_private(front)->refcnt);
	assert(get_private(back)->refcnt);

	assert(get_private(front)->bo->refcnt);
	assert(get_private(front)->bo->flush);

	assert(get_private(back)->bo->refcnt);
	assert(get_private(back)->bo->flush);

	if (get_private(front)->pixmap != get_drawable_pixmap(draw))
		goto skip;

	assert(sna_pixmap_from_drawable(draw)->flush);

	/* Drawable not displayed... just complete the swap */
	pipe = -1;
	if ((sna->flags & SNA_NO_WAIT) == 0)
		pipe = sna_dri_get_pipe(draw);
	if (pipe == -1) {
		DBG(("%s: off-screen, immediate update\n", __FUNCTION__));
		goto blit;
	}

	if (can_flip(sna, draw, front, back) &&
	    sna_dri_schedule_flip(client, draw, front, back, pipe,
				  target_msc, divisor, remainder,
				  func, data))
		return TRUE;

	VG_CLEAR(vbl);

	info = calloc(1, sizeof(struct sna_dri_frame_event));
	if (!info)
		goto blit;

	list_init(&info->cache);
	info->draw = draw;
	info->client = client;
	info->event_complete = func;
	info->event_data = data;
	info->front = front;
	info->back = back;
	info->pipe = pipe;

	sna_dri_add_frame_event(draw, info);
	sna_dri_reference_buffer(front);
	sna_dri_reference_buffer(back);

	info->type = swap_type;

	current_msc = get_current_msc_for_target(sna, *target_msc, pipe);
	DBG(("%s: target_msc=%u, current_msc=%u, divisor=%u\n", __FUNCTION__,
	     (uint32_t)*target_msc, (uint32_t)current_msc, (uint32_t)divisor));

	if (divisor == 0 && current_msc >= *target_msc - 1) {
		bool sync = current_msc < *target_msc;
		if (!sna_dri_immediate_blit(sna, info, sync, true))
			sna_dri_frame_event_info_free(sna, draw, info);
		if (*target_msc)
			*target_msc = current_msc + sync;
		return TRUE;
	}

	/*
	 * If divisor is zero, or current_msc is smaller than target_msc
	 * we just need to make sure target_msc passes before initiating
	 * the swap.
	 */
	if (current_msc < *target_msc) {
		DBG(("%s: waiting for swap: current=%d, target=%d, divisor=%d\n",
		     __FUNCTION__,
		     (int)current_msc,
		     (int)*target_msc,
		     (int)divisor));

		info->type = DRI2_SWAP;

		vbl.request.type =
			DRM_VBLANK_ABSOLUTE |
			DRM_VBLANK_EVENT |
			pipe_select(pipe);
		vbl.request.sequence = *target_msc;
		vbl.request.signal = (unsigned long)info;
		if (sna_wait_vblank(sna, &vbl))
			goto blit;

		return TRUE;
	}

	/*
	 * If we get here, target_msc has already passed or we don't have one,
	 * and we need to queue an event that will satisfy the divisor/remainder
	 * equation.
	 */
	DBG(("%s: missed target, queueing event for next: current=%d, target=%d,  divisor=%d\n",
	     __FUNCTION__,
	     (int)current_msc,
	     (int)*target_msc,
	     (int)divisor));

	if (divisor == 0)
		divisor = 1;

	vbl.request.type =
		DRM_VBLANK_ABSOLUTE |
		DRM_VBLANK_EVENT |
		DRM_VBLANK_NEXTONMISS |
		pipe_select(pipe);

	vbl.request.sequence = current_msc - current_msc % divisor + remainder;
	/*
	 * If the calculated deadline vbl.request.sequence is smaller than
	 * or equal to current_msc, it means we've passed the last point
	 * when effective onset frame seq could satisfy
	 * seq % divisor == remainder, so we need to wait for the next time
	 * this will happen.
	 */
	if (vbl.request.sequence < current_msc)
		vbl.request.sequence += divisor;
	*target_msc = vbl.reply.sequence;

	vbl.request.sequence -= 1;
	vbl.request.signal = (unsigned long)info;
	if (sna_wait_vblank(sna, &vbl))
		goto blit;

	return TRUE;

blit:
	DBG(("%s -- blit\n", __FUNCTION__));
	__sna_dri_copy_region(sna, draw, NULL, back, front, false);
	if (info)
		sna_dri_frame_event_info_free(sna, draw, info);
skip:
	DRI2SwapComplete(client, draw, 0, 0, 0, DRI2_BLIT_COMPLETE, func, data);
	*target_msc = 0; /* offscreen, so zero out target vblank count */
	return TRUE;
}

static uint64_t gettime_us(void)
{
	struct timespec tv;

	if (clock_gettime(CLOCK_MONOTONIC, &tv))
		return 0;

	return (uint64_t)tv.tv_sec * 1000000 + tv.tv_nsec / 1000;
}

/*
 * Get current frame count and frame count timestamp, based on drawable's
 * crtc.
 */
static int
sna_dri_get_msc(DrawablePtr draw, CARD64 *ust, CARD64 *msc)
{
	struct sna *sna = to_sna_from_drawable(draw);
	drmVBlank vbl;
	int pipe;


	pipe = sna_dri_get_pipe(draw);
	DBG(("%s(pipe=%d)\n", __FUNCTION__, pipe));
	if (pipe == -1) {
fail:
		/* Drawable not displayed, make up a *monotonic* value */
		*ust = gettime_us();
		*msc = 0;
		return TRUE;
	}

	VG_CLEAR(vbl);
	vbl.request.type = DRM_VBLANK_RELATIVE | pipe_select(pipe);
	vbl.request.sequence = 0;
	if (sna_wait_vblank(sna, &vbl) == 0) {
		*ust = ((CARD64)vbl.reply.tval_sec * 1000000) + vbl.reply.tval_usec;
		*msc = vbl.reply.sequence;
		DBG(("%s: msc=%llu, ust=%llu\n", __FUNCTION__,
		     (long long)*msc, (long long)*ust));
	} else {
		DBG(("%s: query failed on pipe %d, ret=%d\n",
		     __FUNCTION__, pipe, errno));
		goto fail;
	}

	return TRUE;
}

/*
 * Request a DRM event when the requested conditions will be satisfied.
 *
 * We need to handle the event and ask the server to wake up the client when
 * we receive it.
 */
static int
sna_dri_schedule_wait_msc(ClientPtr client, DrawablePtr draw, CARD64 target_msc,
			  CARD64 divisor, CARD64 remainder)
{
	struct sna *sna = to_sna_from_drawable(draw);
	struct sna_dri_frame_event *info = NULL;
	int pipe = sna_dri_get_pipe(draw);
	CARD64 current_msc;
	drmVBlank vbl;

	DBG(("%s(pipe=%d, target_msc=%llu, divisor=%llu, rem=%llu)\n",
	     __FUNCTION__, pipe,
	     (long long)target_msc,
	     (long long)divisor,
	     (long long)remainder));

	/* Truncate to match kernel interfaces; means occasional overflow
	 * misses, but that's generally not a big deal */
	target_msc &= 0xffffffff;
	divisor &= 0xffffffff;
	remainder &= 0xffffffff;

	/* Drawable not visible, return immediately */
	if (pipe == -1)
		goto out_complete;

	VG_CLEAR(vbl);

	/* Get current count */
	vbl.request.type = DRM_VBLANK_RELATIVE | pipe_select(pipe);
	vbl.request.sequence = 0;
	if (sna_wait_vblank(sna, &vbl))
		goto out_complete;

	current_msc = vbl.reply.sequence;

	/* If target_msc already reached or passed, set it to
	 * current_msc to ensure we return a reasonable value back
	 * to the caller. This keeps the client from continually
	 * sending us MSC targets from the past by forcibly updating
	 * their count on this call.
	 */
	if (divisor == 0 && current_msc >= target_msc) {
		target_msc = current_msc;
		goto out_complete;
	}

	info = calloc(1, sizeof(struct sna_dri_frame_event));
	if (!info)
		goto out_complete;

	list_init(&info->cache);
	info->draw = draw;
	info->client = client;
	info->type = DRI2_WAITMSC;
	sna_dri_add_frame_event(draw, info);

	/*
	 * If divisor is zero, or current_msc is smaller than target_msc,
	 * we just need to make sure target_msc passes before waking up the
	 * client.
	 */
	if (divisor == 0 || current_msc < target_msc) {
		vbl.request.type =
			DRM_VBLANK_ABSOLUTE |
			DRM_VBLANK_EVENT |
			pipe_select(pipe);
		vbl.request.sequence = target_msc;
		vbl.request.signal = (unsigned long)info;
		if (sna_wait_vblank(sna, &vbl))
			goto out_free_info;

		DRI2BlockClient(client, draw);
		return TRUE;
	}

	/*
	 * If we get here, target_msc has already passed or we don't have one,
	 * so we queue an event that will satisfy the divisor/remainder
	 * equation.
	 */
	vbl.request.type =
		DRM_VBLANK_ABSOLUTE | DRM_VBLANK_EVENT | pipe_select(pipe);

	vbl.request.sequence = current_msc - current_msc % divisor + remainder;

	/*
	 * If calculated remainder is larger than requested remainder,
	 * it means we've passed the last point where
	 * seq % divisor == remainder, so we need to wait for the next time
	 * that will happen.
	 */
	if ((current_msc % divisor) >= remainder)
		vbl.request.sequence += divisor;

	vbl.request.signal = (unsigned long)info;
	if (sna_wait_vblank(sna, &vbl))
		goto out_free_info;

	DRI2BlockClient(client, draw);
	return TRUE;

out_free_info:
	sna_dri_frame_event_info_free(sna, draw, info);
out_complete:
	DRI2WaitMSCComplete(client, draw, target_msc, 0, 0);
	return TRUE;
}
#else
void sna_dri_destroy_window(WindowPtr win) { }
#endif

static bool has_i830_dri(void)
{
	return access(DRI_DRIVER_PATH "/i830_dri.so", R_OK) == 0;
}

static int
namecmp(const char *s1, const char *s2)
{
	char c1, c2;

	if (!s1 || *s1 == 0) {
		if (!s2 || *s2 == 0)
			return 0;
		else
			return 1;
	}

	while (*s1 == '_' || *s1 == ' ' || *s1 == '\t')
		s1++;

	while (*s2 == '_' || *s2 == ' ' || *s2 == '\t')
		s2++;

	c1 = isupper(*s1) ? tolower(*s1) : *s1;
	c2 = isupper(*s2) ? tolower(*s2) : *s2;
	while (c1 == c2) {
		if (c1 == '\0')
			return 0;

		s1++;
		while (*s1 == '_' || *s1 == ' ' || *s1 == '\t')
			s1++;

		s2++;
		while (*s2 == '_' || *s2 == ' ' || *s2 == '\t')
			s2++;

		c1 = isupper(*s1) ? tolower(*s1) : *s1;
		c2 = isupper(*s2) ? tolower(*s2) : *s2;
	}

	return c1 - c2;
}

static bool is_bool(const char *str)
{
	if (str == NULL)
		return true;

	if (*str == '\0')
		return true;

	if (namecmp(str, "1") == 0)
		return true;
	if (namecmp(str, "on") == 0)
		return true;
	if (namecmp(str, "true") == 0)
		return true;
	if (namecmp(str, "yes") == 0)
		return true;

	if (namecmp(str, "0") == 0)
		return true;
	if (namecmp(str, "off") == 0)
		return true;
	if (namecmp(str, "false") == 0)
		return true;
	if (namecmp(str, "no") == 0)
		return true;

	return false;
}

static const char *dri_driver_name(struct sna *sna)
{
	const char *s = xf86GetOptValString(sna->Options, OPTION_DRI);

	if (is_bool(s)) {
		if (sna->kgem.gen < 030)
			return has_i830_dri() ? "i830" : "i915";
		else if (sna->kgem.gen < 040)
			return "i915";
		else
			return "i965";
	}

	return s;
}

bool sna_dri_open(struct sna *sna, ScreenPtr screen)
{
	DRI2InfoRec info;
	int major = 1, minor = 0;
#if DRI2INFOREC_VERSION >= 4
	const char *driverNames[2];
#endif

	DBG(("%s()\n", __FUNCTION__));

	if (wedged(sna)) {
		xf86DrvMsg(sna->scrn->scrnIndex, X_WARNING,
			   "loading DRI2 whilst the GPU is wedged.\n");
	}

	if (xf86LoaderCheckSymbol("DRI2Version"))
		DRI2Version(&major, &minor);

	if (minor < 1) {
		xf86DrvMsg(sna->scrn->scrnIndex, X_WARNING,
			   "DRI2 requires DRI2 module version 1.1.0 or later\n");
		return false;
	}

	memset(&info, '\0', sizeof(info));
	info.fd = sna->kgem.fd;
	info.driverName = dri_driver_name(sna);
	info.deviceName = intel_get_client_name(sna->scrn);

	DBG(("%s: loading dri driver '%s' [gen=%d] for device '%s'\n",
	     __FUNCTION__, info.driverName, sna->kgem.gen, info.deviceName));

#if DRI2INFOREC_VERSION == 2
	/* The ABI between 2 and 3 was broken so we could get rid of
	 * the multi-buffer alloc functions.  Make sure we indicate the
	 * right version so DRI2 can reject us if it's version 3 or above. */
	info.version = 2;
#else
	info.version = 3;
#endif
	info.CreateBuffer = sna_dri_create_buffer;
	info.DestroyBuffer = sna_dri_destroy_buffer;

	info.CopyRegion = sna_dri_copy_region;
#if DRI2INFOREC_VERSION >= 4
	info.version = 4;
	info.ScheduleSwap = sna_dri_schedule_swap;
	info.GetMSC = sna_dri_get_msc;
	info.ScheduleWaitMSC = sna_dri_schedule_wait_msc;
	info.numDrivers = 2;
	info.driverNames = driverNames;
	driverNames[0] = info.driverName;
	driverNames[1] = info.driverName;
#endif

#if DRI2INFOREC_VERSION >= 6
	info.version = 6;
	info.SwapLimitValidate = NULL;
	info.ReuseBufferNotify = NULL;
#endif

#if USE_ASYNC_SWAP
	info.version = 10;
	info.scheduleSwap0 = 1;
#endif

	return DRI2ScreenInit(screen, &info);
}

void sna_dri_close(struct sna *sna, ScreenPtr screen)
{
	DBG(("%s()\n", __FUNCTION__));
	DRI2CloseScreen(screen);
}
