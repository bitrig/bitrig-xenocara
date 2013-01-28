/**************************************************************************
 *
 * Copyright � 2007 Red Hat Inc.
 * Copyright � 2007 Intel Corporation
 * Copyright 2006 Tungsten Graphics, Inc., Bismarck, ND., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstr�m <thomas-at-tungstengraphics-dot-com>
 *          Keith Whitwell <keithw-at-tungstengraphics-dot-com>
 *	    Eric Anholt <eric@anholt.net>
 *	    Dave Airlie <airlied@linux.ie>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86drm.h>
#include <xf86atomic.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>

#include "errno.h"
#include "libdrm_lists.h"
#include "intel_bufmgr.h"
#include "intel_bufmgr_priv.h"
#include "intel_chipset.h"
#include "string.h"

#include "i915_drm.h"

#define DBG(...) do {					\
	if (bufmgr_gem->bufmgr.debug)			\
		fprintf(stderr, __VA_ARGS__);		\
} while (0)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef struct _drm_intel_bo_gem drm_intel_bo_gem;

struct drm_intel_gem_bo_bucket {
	drmMMListHead head;
	unsigned long size;
};

typedef struct _drm_intel_bufmgr_gem {
	drm_intel_bufmgr bufmgr;

	int fd;

	int max_relocs;

	pthread_mutex_t lock;

#ifndef __OpenBSD__
	struct drm_i915_gem_exec_object *exec_objects;
#endif
	struct drm_i915_gem_exec_object2 *exec2_objects;
	drm_intel_bo **exec_bos;
	int exec_size;
	int exec_count;

	/** Array of lists of cached gem objects of power-of-two sizes */
	struct drm_intel_gem_bo_bucket cache_bucket[14 * 4];
	int num_buckets;
	time_t time;

	drmMMListHead named;
	drmMMListHead vma_cache;
	int vma_count, vma_open, vma_max;

	uint64_t gtt_size;
	int available_fences;
	int pci_device;
	int gen;
	unsigned int has_bsd : 1;
	unsigned int has_blt : 1;
	unsigned int has_relaxed_fencing : 1;
	unsigned int has_llc : 1;
	unsigned int bo_reuse : 1;
	bool fenced_relocs;
} drm_intel_bufmgr_gem;

#define DRM_INTEL_RELOC_FENCE (1<<0)

typedef struct _drm_intel_reloc_target_info {
	drm_intel_bo *bo;
	int flags;
} drm_intel_reloc_target;

struct _drm_intel_bo_gem {
	drm_intel_bo bo;

	atomic_t refcount;
	uint32_t gem_handle;
	const char *name;

	/**
	 * Kenel-assigned global name for this object
	 */
	unsigned int global_name;
	drmMMListHead name_list;

	/**
	 * Index of the buffer within the validation list while preparing a
	 * batchbuffer execution.
	 */
	int validate_index;

	/**
	 * Current tiling mode
	 */
	uint32_t tiling_mode;
	uint32_t swizzle_mode;
	unsigned long stride;

	time_t free_time;

	/** Array passed to the DRM containing relocation information. */
	struct drm_i915_gem_relocation_entry *relocs;
	/**
	 * Array of info structs corresponding to relocs[i].target_handle etc
	 */
	drm_intel_reloc_target *reloc_target_info;
	/** Number of entries in relocs */
	int reloc_count;
	/** Mapped address for the buffer, saved across map/unmap cycles */
	void *mem_virtual;
	/** GTT virtual address for the buffer, saved across map/unmap cycles */
	void *gtt_virtual;
	int map_count;
	drmMMListHead vma_list;

	/** BO cache list */
	drmMMListHead head;

	/**
	 * Boolean of whether this BO and its children have been included in
	 * the current drm_intel_bufmgr_check_aperture_space() total.
	 */
	bool included_in_check_aperture;

	/**
	 * Boolean of whether this buffer has been used as a relocation
	 * target and had its size accounted for, and thus can't have any
	 * further relocations added to it.
	 */
	bool used_as_reloc_target;

	/**
	 * Boolean of whether we have encountered an error whilst building the relocation tree.
	 */
	bool has_error;

	/**
	 * Boolean of whether this buffer can be re-used
	 */
	bool reusable;

	/**
	 * Size in bytes of this buffer and its relocation descendents.
	 *
	 * Used to avoid costly tree walking in
	 * drm_intel_bufmgr_check_aperture in the common case.
	 */
	int reloc_tree_size;

	/**
	 * Number of potential fence registers required by this buffer and its
	 * relocations.
	 */
	int reloc_tree_fences;

	/** Flags that we may need to do the SW_FINSIH ioctl on unmap. */
	bool mapped_cpu_write;
};

static unsigned int
drm_intel_gem_estimate_batch_space(drm_intel_bo ** bo_array, int count);

static unsigned int
drm_intel_gem_compute_batch_space(drm_intel_bo ** bo_array, int count);

static int
drm_intel_gem_bo_get_tiling(drm_intel_bo *bo, uint32_t * tiling_mode,
			    uint32_t * swizzle_mode);

static int
drm_intel_gem_bo_set_tiling_internal(drm_intel_bo *bo,
				     uint32_t tiling_mode,
				     uint32_t stride);

static void drm_intel_gem_bo_unreference_locked_timed(drm_intel_bo *bo,
						      time_t time);

static void drm_intel_gem_bo_unreference(drm_intel_bo *bo);

static void drm_intel_gem_bo_free(drm_intel_bo *bo);

static unsigned long
drm_intel_gem_bo_tile_size(drm_intel_bufmgr_gem *bufmgr_gem, unsigned long size,
			   uint32_t *tiling_mode)
{
	unsigned long min_size, max_size;
	unsigned long i;

	if (*tiling_mode == I915_TILING_NONE)
		return size;

	/* 965+ just need multiples of page size for tiling */
	if (bufmgr_gem->gen >= 4)
		return ROUND_UP_TO(size, 4096);

	/* Older chips need powers of two, of at least 512k or 1M */
	if (bufmgr_gem->gen == 3) {
		min_size = 1024*1024;
		max_size = 128*1024*1024;
	} else {
		min_size = 512*1024;
		max_size = 64*1024*1024;
	}

	if (size > max_size) {
		*tiling_mode = I915_TILING_NONE;
		return size;
	}

	/* Do we need to allocate every page for the fence? */
	if (bufmgr_gem->has_relaxed_fencing)
		return ROUND_UP_TO(size, 4096);

	for (i = min_size; i < size; i <<= 1)
		;

	return i;
}

/*
 * Round a given pitch up to the minimum required for X tiling on a
 * given chip.  We use 512 as the minimum to allow for a later tiling
 * change.
 */
static unsigned long
drm_intel_gem_bo_tile_pitch(drm_intel_bufmgr_gem *bufmgr_gem,
			    unsigned long pitch, uint32_t *tiling_mode)
{
	unsigned long tile_width;
	unsigned long i;

	/* If untiled, then just align it so that we can do rendering
	 * to it with the 3D engine.
	 */
	if (*tiling_mode == I915_TILING_NONE)
		return ALIGN(pitch, 64);

	if (*tiling_mode == I915_TILING_X
			|| (IS_915(bufmgr_gem->pci_device)
			    && *tiling_mode == I915_TILING_Y))
		tile_width = 512;
	else
		tile_width = 128;

	/* 965 is flexible */
	if (bufmgr_gem->gen >= 4)
		return ROUND_UP_TO(pitch, tile_width);

	/* The older hardware has a maximum pitch of 8192 with tiled
	 * surfaces, so fallback to untiled if it's too large.
	 */
	if (pitch > 8192) {
		*tiling_mode = I915_TILING_NONE;
		return ALIGN(pitch, 64);
	}

	/* Pre-965 needs power of two tile width */
	for (i = tile_width; i < pitch; i <<= 1)
		;

	return i;
}

static struct drm_intel_gem_bo_bucket *
drm_intel_gem_bo_bucket_for_size(drm_intel_bufmgr_gem *bufmgr_gem,
				 unsigned long size)
{
	int i;

	for (i = 0; i < bufmgr_gem->num_buckets; i++) {
		struct drm_intel_gem_bo_bucket *bucket =
		    &bufmgr_gem->cache_bucket[i];
		if (bucket->size >= size) {
			return bucket;
		}
	}

	return NULL;
}

static void
drm_intel_gem_dump_validation_list(drm_intel_bufmgr_gem *bufmgr_gem)
{
	int i, j;

	for (i = 0; i < bufmgr_gem->exec_count; i++) {
		drm_intel_bo *bo = bufmgr_gem->exec_bos[i];
		drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;

		if (bo_gem->relocs == NULL) {
			DBG("%2d: %d (%s)\n", i, bo_gem->gem_handle,
			    bo_gem->name);
			continue;
		}

		for (j = 0; j < bo_gem->reloc_count; j++) {
			drm_intel_bo *target_bo = bo_gem->reloc_target_info[j].bo;
			drm_intel_bo_gem *target_gem =
			    (drm_intel_bo_gem *) target_bo;

			DBG("%2d: %d (%s)@0x%08llx -> "
			    "%d (%s)@0x%08lx + 0x%08x\n",
			    i,
			    bo_gem->gem_handle, bo_gem->name,
			    (unsigned long long)bo_gem->relocs[j].offset,
			    target_gem->gem_handle,
			    target_gem->name,
			    target_bo->offset,
			    bo_gem->relocs[j].delta);
		}
	}
}

static inline void
drm_intel_gem_bo_reference(drm_intel_bo *bo)
{
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;

	atomic_inc(&bo_gem->refcount);
}

#ifndef __OpenBSD__
/**
 * Adds the given buffer to the list of buffers to be validated (moved into the
 * appropriate memory type) with the next batch submission.
 *
 * If a buffer is validated multiple times in a batch submission, it ends up
 * with the intersection of the memory type flags and the union of the
 * access flags.
 */
static void
drm_intel_add_validate_buffer(drm_intel_bo *bo)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	int index;

	if (bo_gem->validate_index != -1)
		return;

	/* Extend the array of validation entries as necessary. */
	if (bufmgr_gem->exec_count == bufmgr_gem->exec_size) {
		int new_size = bufmgr_gem->exec_size * 2;

		if (new_size == 0)
			new_size = 5;

		bufmgr_gem->exec_objects =
		    realloc(bufmgr_gem->exec_objects,
			    sizeof(*bufmgr_gem->exec_objects) * new_size);
		bufmgr_gem->exec_bos =
		    realloc(bufmgr_gem->exec_bos,
			    sizeof(*bufmgr_gem->exec_bos) * new_size);
		bufmgr_gem->exec_size = new_size;
	}

	index = bufmgr_gem->exec_count;
	bo_gem->validate_index = index;
	/* Fill in array entry */
	bufmgr_gem->exec_objects[index].handle = bo_gem->gem_handle;
	bufmgr_gem->exec_objects[index].relocation_count = bo_gem->reloc_count;
	bufmgr_gem->exec_objects[index].relocs_ptr = (uintptr_t) bo_gem->relocs;
	bufmgr_gem->exec_objects[index].alignment = 0;
	bufmgr_gem->exec_objects[index].offset = 0;
	bufmgr_gem->exec_bos[index] = bo;
	bufmgr_gem->exec_count++;
}
#endif

static void
drm_intel_add_validate_buffer2(drm_intel_bo *bo, int need_fence)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *)bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *)bo;
	int index;

	if (bo_gem->validate_index != -1) {
		if (need_fence)
			bufmgr_gem->exec2_objects[bo_gem->validate_index].flags |=
				EXEC_OBJECT_NEEDS_FENCE;
		return;
	}

	/* Extend the array of validation entries as necessary. */
	if (bufmgr_gem->exec_count == bufmgr_gem->exec_size) {
		int new_size = bufmgr_gem->exec_size * 2;

		if (new_size == 0)
			new_size = 5;

		bufmgr_gem->exec2_objects =
			realloc(bufmgr_gem->exec2_objects,
				sizeof(*bufmgr_gem->exec2_objects) * new_size);
		bufmgr_gem->exec_bos =
			realloc(bufmgr_gem->exec_bos,
				sizeof(*bufmgr_gem->exec_bos) * new_size);
		bufmgr_gem->exec_size = new_size;
	}

	index = bufmgr_gem->exec_count;
	bo_gem->validate_index = index;
	/* Fill in array entry */
	bufmgr_gem->exec2_objects[index].handle = bo_gem->gem_handle;
	bufmgr_gem->exec2_objects[index].relocation_count = bo_gem->reloc_count;
	bufmgr_gem->exec2_objects[index].relocs_ptr = (uintptr_t)bo_gem->relocs;
	bufmgr_gem->exec2_objects[index].alignment = 0;
	bufmgr_gem->exec2_objects[index].offset = 0;
	bufmgr_gem->exec_bos[index] = bo;
	bufmgr_gem->exec2_objects[index].flags = 0;
	bufmgr_gem->exec2_objects[index].rsvd1 = 0;
	bufmgr_gem->exec2_objects[index].rsvd2 = 0;
	if (need_fence) {
		bufmgr_gem->exec2_objects[index].flags |=
			EXEC_OBJECT_NEEDS_FENCE;
	}
	bufmgr_gem->exec_count++;
}

#define RELOC_BUF_SIZE(x) ((I915_RELOC_HEADER + x * I915_RELOC0_STRIDE) * \
	sizeof(uint32_t))

static void
drm_intel_bo_gem_set_in_aperture_size(drm_intel_bufmgr_gem *bufmgr_gem,
				      drm_intel_bo_gem *bo_gem)
{
	int size;

	assert(!bo_gem->used_as_reloc_target);

	/* The older chipsets are far-less flexible in terms of tiling,
	 * and require tiled buffer to be size aligned in the aperture.
	 * This means that in the worst possible case we will need a hole
	 * twice as large as the object in order for it to fit into the
	 * aperture. Optimal packing is for wimps.
	 */
	size = bo_gem->bo.size;
	if (bufmgr_gem->gen < 4 && bo_gem->tiling_mode != I915_TILING_NONE) {
		int min_size;

		if (bufmgr_gem->has_relaxed_fencing) {
			if (bufmgr_gem->gen == 3)
				min_size = 1024*1024;
			else
				min_size = 512*1024;

			while (min_size < size)
				min_size *= 2;
		} else
			min_size = size;

		/* Account for worst-case alignment. */
		size = 2 * min_size;
	}

	bo_gem->reloc_tree_size = size;
}

static int
drm_intel_setup_reloc_list(drm_intel_bo *bo)
{
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	unsigned int max_relocs = bufmgr_gem->max_relocs;

	if (bo->size / 4 < max_relocs)
		max_relocs = bo->size / 4;

	bo_gem->relocs = malloc(max_relocs *
				sizeof(struct drm_i915_gem_relocation_entry));
	bo_gem->reloc_target_info = malloc(max_relocs *
					   sizeof(drm_intel_reloc_target));
	if (bo_gem->relocs == NULL || bo_gem->reloc_target_info == NULL) {
		bo_gem->has_error = true;

		free (bo_gem->relocs);
		bo_gem->relocs = NULL;

		free (bo_gem->reloc_target_info);
		bo_gem->reloc_target_info = NULL;

		return 1;
	}

	return 0;
}

static int
drm_intel_gem_bo_busy(drm_intel_bo *bo)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	struct drm_i915_gem_busy busy;
	int ret;

	memset(&busy, 0, sizeof(busy));
	busy.handle = bo_gem->gem_handle;

	ret = drmIoctl(bufmgr_gem->fd, DRM_IOCTL_I915_GEM_BUSY, &busy);

	return (ret == 0 && busy.busy);
}

static int
drm_intel_gem_bo_madvise_internal(drm_intel_bufmgr_gem *bufmgr_gem,
				  drm_intel_bo_gem *bo_gem, int state)
{
	struct drm_i915_gem_madvise madv;

	madv.handle = bo_gem->gem_handle;
	madv.madv = state;
	madv.retained = 1;
	drmIoctl(bufmgr_gem->fd, DRM_IOCTL_I915_GEM_MADVISE, &madv);

	return madv.retained;
}

static int
drm_intel_gem_bo_madvise(drm_intel_bo *bo, int madv)
{
	return drm_intel_gem_bo_madvise_internal
		((drm_intel_bufmgr_gem *) bo->bufmgr,
		 (drm_intel_bo_gem *) bo,
		 madv);
}

/* drop the oldest entries that have been purged by the kernel */
static void
drm_intel_gem_bo_cache_purge_bucket(drm_intel_bufmgr_gem *bufmgr_gem,
				    struct drm_intel_gem_bo_bucket *bucket)
{
	while (!DRMLISTEMPTY(&bucket->head)) {
		drm_intel_bo_gem *bo_gem;

		bo_gem = DRMLISTENTRY(drm_intel_bo_gem,
				      bucket->head.next, head);
		if (drm_intel_gem_bo_madvise_internal
		    (bufmgr_gem, bo_gem, I915_MADV_DONTNEED))
			break;

		DRMLISTDEL(&bo_gem->head);
		drm_intel_gem_bo_free(&bo_gem->bo);
	}
}

static drm_intel_bo *
drm_intel_gem_bo_alloc_internal(drm_intel_bufmgr *bufmgr,
				const char *name,
				unsigned long size,
				unsigned long flags,
				uint32_t tiling_mode,
				unsigned long stride)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bufmgr;
	drm_intel_bo_gem *bo_gem;
	unsigned int page_size = getpagesize();
	int ret;
	struct drm_intel_gem_bo_bucket *bucket;
	bool alloc_from_cache;
	unsigned long bo_size;
	bool for_render = false;

	if (flags & BO_ALLOC_FOR_RENDER)
		for_render = true;

	/* Round the allocated size up to a power of two number of pages. */
	bucket = drm_intel_gem_bo_bucket_for_size(bufmgr_gem, size);

	/* If we don't have caching at this size, don't actually round the
	 * allocation up.
	 */
	if (bucket == NULL) {
		bo_size = size;
		if (bo_size < page_size)
			bo_size = page_size;
	} else {
		bo_size = bucket->size;
	}

	pthread_mutex_lock(&bufmgr_gem->lock);
	/* Get a buffer out of the cache if available */
retry:
	alloc_from_cache = false;
	if (bucket != NULL && !DRMLISTEMPTY(&bucket->head)) {
		if (for_render) {
			/* Allocate new render-target BOs from the tail (MRU)
			 * of the list, as it will likely be hot in the GPU
			 * cache and in the aperture for us.
			 */
			bo_gem = DRMLISTENTRY(drm_intel_bo_gem,
					      bucket->head.prev, head);
			DRMLISTDEL(&bo_gem->head);
			alloc_from_cache = true;
		} else {
			/* For non-render-target BOs (where we're probably
			 * going to map it first thing in order to fill it
			 * with data), check if the last BO in the cache is
			 * unbusy, and only reuse in that case. Otherwise,
			 * allocating a new buffer is probably faster than
			 * waiting for the GPU to finish.
			 */
			bo_gem = DRMLISTENTRY(drm_intel_bo_gem,
					      bucket->head.next, head);
			if (!drm_intel_gem_bo_busy(&bo_gem->bo)) {
				alloc_from_cache = true;
				DRMLISTDEL(&bo_gem->head);
			}
		}

		if (alloc_from_cache) {
			if (!drm_intel_gem_bo_madvise_internal
			    (bufmgr_gem, bo_gem, I915_MADV_WILLNEED)) {
				drm_intel_gem_bo_free(&bo_gem->bo);
				drm_intel_gem_bo_cache_purge_bucket(bufmgr_gem,
								    bucket);
				goto retry;
			}

			if (drm_intel_gem_bo_set_tiling_internal(&bo_gem->bo,
								 tiling_mode,
								 stride)) {
				drm_intel_gem_bo_free(&bo_gem->bo);
				goto retry;
			}
		}
	}
	pthread_mutex_unlock(&bufmgr_gem->lock);

	if (!alloc_from_cache) {
		struct drm_i915_gem_create create;

		bo_gem = calloc(1, sizeof(*bo_gem));
		if (!bo_gem)
			return NULL;

		bo_gem->bo.size = bo_size;
		memset(&create, 0, sizeof(create));
		create.size = bo_size;

		ret = drmIoctl(bufmgr_gem->fd,
			       DRM_IOCTL_I915_GEM_CREATE,
			       &create);
		bo_gem->gem_handle = create.handle;
		bo_gem->bo.handle = bo_gem->gem_handle;
		if (ret != 0) {
			free(bo_gem);
			return NULL;
		}
		bo_gem->bo.bufmgr = bufmgr;

		bo_gem->tiling_mode = I915_TILING_NONE;
		bo_gem->swizzle_mode = I915_BIT_6_SWIZZLE_NONE;
		bo_gem->stride = 0;

		if (drm_intel_gem_bo_set_tiling_internal(&bo_gem->bo,
							 tiling_mode,
							 stride)) {
		    drm_intel_gem_bo_free(&bo_gem->bo);
		    return NULL;
		}

		DRMINITLISTHEAD(&bo_gem->name_list);
		DRMINITLISTHEAD(&bo_gem->vma_list);
	}

	bo_gem->name = name;
	atomic_set(&bo_gem->refcount, 1);
	bo_gem->validate_index = -1;
	bo_gem->reloc_tree_fences = 0;
	bo_gem->used_as_reloc_target = false;
	bo_gem->has_error = false;
	bo_gem->reusable = true;

	drm_intel_bo_gem_set_in_aperture_size(bufmgr_gem, bo_gem);

	DBG("bo_create: buf %d (%s) %ldb\n",
	    bo_gem->gem_handle, bo_gem->name, size);

	return &bo_gem->bo;
}

static drm_intel_bo *
drm_intel_gem_bo_alloc_for_render(drm_intel_bufmgr *bufmgr,
				  const char *name,
				  unsigned long size,
				  unsigned int alignment)
{
	return drm_intel_gem_bo_alloc_internal(bufmgr, name, size,
					       BO_ALLOC_FOR_RENDER,
					       I915_TILING_NONE, 0);
}

static drm_intel_bo *
drm_intel_gem_bo_alloc(drm_intel_bufmgr *bufmgr,
		       const char *name,
		       unsigned long size,
		       unsigned int alignment)
{
	return drm_intel_gem_bo_alloc_internal(bufmgr, name, size, 0,
					       I915_TILING_NONE, 0);
}

static drm_intel_bo *
drm_intel_gem_bo_alloc_tiled(drm_intel_bufmgr *bufmgr, const char *name,
			     int x, int y, int cpp, uint32_t *tiling_mode,
			     unsigned long *pitch, unsigned long flags)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *)bufmgr;
	unsigned long size, stride;
	uint32_t tiling;

	do {
		unsigned long aligned_y, height_alignment;

		tiling = *tiling_mode;

		/* If we're tiled, our allocations are in 8 or 32-row blocks,
		 * so failure to align our height means that we won't allocate
		 * enough pages.
		 *
		 * If we're untiled, we still have to align to 2 rows high
		 * because the data port accesses 2x2 blocks even if the
		 * bottom row isn't to be rendered, so failure to align means
		 * we could walk off the end of the GTT and fault.  This is
		 * documented on 965, and may be the case on older chipsets
		 * too so we try to be careful.
		 */
		aligned_y = y;
		height_alignment = 2;

		if ((bufmgr_gem->gen == 2) && tiling != I915_TILING_NONE)
			height_alignment = 16;
		else if (tiling == I915_TILING_X
			|| (IS_915(bufmgr_gem->pci_device)
			    && tiling == I915_TILING_Y))
			height_alignment = 8;
		else if (tiling == I915_TILING_Y)
			height_alignment = 32;
		aligned_y = ALIGN(y, height_alignment);

		stride = x * cpp;
		stride = drm_intel_gem_bo_tile_pitch(bufmgr_gem, stride, tiling_mode);
		size = stride * aligned_y;
		size = drm_intel_gem_bo_tile_size(bufmgr_gem, size, tiling_mode);
	} while (*tiling_mode != tiling);
	*pitch = stride;

	if (tiling == I915_TILING_NONE)
		stride = 0;

	return drm_intel_gem_bo_alloc_internal(bufmgr, name, size, flags,
					       tiling, stride);
}

/**
 * Returns a drm_intel_bo wrapping the given buffer object handle.
 *
 * This can be used when one application needs to pass a buffer object
 * to another.
 */
drm_intel_bo *
drm_intel_bo_gem_create_from_name(drm_intel_bufmgr *bufmgr,
				  const char *name,
				  unsigned int handle)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bufmgr;
	drm_intel_bo_gem *bo_gem;
	int ret;
	struct drm_gem_open open_arg;
	struct drm_i915_gem_get_tiling get_tiling;
	drmMMListHead *list;

	/* At the moment most applications only have a few named bo.
	 * For instance, in a DRI client only the render buffers passed
	 * between X and the client are named. And since X returns the
	 * alternating names for the front/back buffer a linear search
	 * provides a sufficiently fast match.
	 */
	for (list = bufmgr_gem->named.next;
	     list != &bufmgr_gem->named;
	     list = list->next) {
		bo_gem = DRMLISTENTRY(drm_intel_bo_gem, list, name_list);
		if (bo_gem->global_name == handle) {
			drm_intel_gem_bo_reference(&bo_gem->bo);
			return &bo_gem->bo;
		}
	}

	bo_gem = calloc(1, sizeof(*bo_gem));
	if (!bo_gem)
		return NULL;

	memset(&open_arg, 0, sizeof(open_arg));
	open_arg.name = handle;
	ret = drmIoctl(bufmgr_gem->fd,
		       DRM_IOCTL_GEM_OPEN,
		       &open_arg);
	if (ret != 0) {
		DBG("Couldn't reference %s handle 0x%08x: %s\n",
		    name, handle, strerror(errno));
		free(bo_gem);
		return NULL;
	}
	bo_gem->bo.size = open_arg.size;
	bo_gem->bo.offset = 0;
	bo_gem->bo.virtual = NULL;
	bo_gem->bo.bufmgr = bufmgr;
	bo_gem->name = name;
	atomic_set(&bo_gem->refcount, 1);
	bo_gem->validate_index = -1;
	bo_gem->gem_handle = open_arg.handle;
	bo_gem->bo.handle = open_arg.handle;
	bo_gem->global_name = handle;
	bo_gem->reusable = false;

	memset(&get_tiling, 0, sizeof(get_tiling));
	get_tiling.handle = bo_gem->gem_handle;
	ret = drmIoctl(bufmgr_gem->fd,
		       DRM_IOCTL_I915_GEM_GET_TILING,
		       &get_tiling);
	if (ret != 0) {
		drm_intel_gem_bo_unreference(&bo_gem->bo);
		return NULL;
	}
	bo_gem->tiling_mode = get_tiling.tiling_mode;
	bo_gem->swizzle_mode = get_tiling.swizzle_mode;
	/* XXX stride is unknown */
	drm_intel_bo_gem_set_in_aperture_size(bufmgr_gem, bo_gem);

	DRMINITLISTHEAD(&bo_gem->vma_list);
	DRMLISTADDTAIL(&bo_gem->name_list, &bufmgr_gem->named);
	DBG("bo_create_from_handle: %d (%s)\n", handle, bo_gem->name);

	return &bo_gem->bo;
}

static void
drm_intel_gem_bo_free(drm_intel_bo *bo)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	struct drm_gem_close close;
	int ret;

	DRMLISTDEL(&bo_gem->vma_list);
	if (bo_gem->mem_virtual) {
		munmap(bo_gem->mem_virtual, bo_gem->bo.size);
		bufmgr_gem->vma_count--;
	}
	if (bo_gem->gtt_virtual) {
		munmap(bo_gem->gtt_virtual, bo_gem->bo.size);
		bufmgr_gem->vma_count--;
	}

	/* Close this object */
	memset(&close, 0, sizeof(close));
	close.handle = bo_gem->gem_handle;
	ret = drmIoctl(bufmgr_gem->fd, DRM_IOCTL_GEM_CLOSE, &close);
	if (ret != 0) {
		DBG("DRM_IOCTL_GEM_CLOSE %d failed (%s): %s\n",
		    bo_gem->gem_handle, bo_gem->name, strerror(errno));
	}
	free(bo);
}

/** Frees all cached buffers significantly older than @time. */
static void
drm_intel_gem_cleanup_bo_cache(drm_intel_bufmgr_gem *bufmgr_gem, time_t time)
{
	int i;

	if (bufmgr_gem->time == time)
		return;

	for (i = 0; i < bufmgr_gem->num_buckets; i++) {
		struct drm_intel_gem_bo_bucket *bucket =
		    &bufmgr_gem->cache_bucket[i];

		while (!DRMLISTEMPTY(&bucket->head)) {
			drm_intel_bo_gem *bo_gem;

			bo_gem = DRMLISTENTRY(drm_intel_bo_gem,
					      bucket->head.next, head);
			if (time - bo_gem->free_time <= 1)
				break;

			DRMLISTDEL(&bo_gem->head);

			drm_intel_gem_bo_free(&bo_gem->bo);
		}
	}

	bufmgr_gem->time = time;
}

static void drm_intel_gem_bo_purge_vma_cache(drm_intel_bufmgr_gem *bufmgr_gem)
{
	int limit;

	DBG("%s: cached=%d, open=%d, limit=%d\n", __FUNCTION__,
	    bufmgr_gem->vma_count, bufmgr_gem->vma_open, bufmgr_gem->vma_max);

	if (bufmgr_gem->vma_max < 0)
		return;

	/* We may need to evict a few entries in order to create new mmaps */
	limit = bufmgr_gem->vma_max - 2*bufmgr_gem->vma_open;
	if (limit < 0)
		limit = 0;

	while (bufmgr_gem->vma_count > limit) {
		drm_intel_bo_gem *bo_gem;

		bo_gem = DRMLISTENTRY(drm_intel_bo_gem,
				      bufmgr_gem->vma_cache.next,
				      vma_list);
		assert(bo_gem->map_count == 0);
		DRMLISTDELINIT(&bo_gem->vma_list);

		if (bo_gem->mem_virtual) {
			munmap(bo_gem->mem_virtual, bo_gem->bo.size);
			bo_gem->mem_virtual = NULL;
			bufmgr_gem->vma_count--;
		}
		if (bo_gem->gtt_virtual) {
			munmap(bo_gem->gtt_virtual, bo_gem->bo.size);
			bo_gem->gtt_virtual = NULL;
			bufmgr_gem->vma_count--;
		}
	}
}

static void drm_intel_gem_bo_close_vma(drm_intel_bufmgr_gem *bufmgr_gem,
				       drm_intel_bo_gem *bo_gem)
{
	bufmgr_gem->vma_open--;
	DRMLISTADDTAIL(&bo_gem->vma_list, &bufmgr_gem->vma_cache);
	if (bo_gem->mem_virtual)
		bufmgr_gem->vma_count++;
	if (bo_gem->gtt_virtual)
		bufmgr_gem->vma_count++;
	drm_intel_gem_bo_purge_vma_cache(bufmgr_gem);
}

static void drm_intel_gem_bo_open_vma(drm_intel_bufmgr_gem *bufmgr_gem,
				      drm_intel_bo_gem *bo_gem)
{
	bufmgr_gem->vma_open++;
	DRMLISTDEL(&bo_gem->vma_list);
	if (bo_gem->mem_virtual)
		bufmgr_gem->vma_count--;
	if (bo_gem->gtt_virtual)
		bufmgr_gem->vma_count--;
	drm_intel_gem_bo_purge_vma_cache(bufmgr_gem);
}

static void
drm_intel_gem_bo_unreference_final(drm_intel_bo *bo, time_t time)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	struct drm_intel_gem_bo_bucket *bucket;
	int i;

	/* Unreference all the target buffers */
	for (i = 0; i < bo_gem->reloc_count; i++) {
		if (bo_gem->reloc_target_info[i].bo != bo) {
			drm_intel_gem_bo_unreference_locked_timed(bo_gem->
								  reloc_target_info[i].bo,
								  time);
		}
	}
	bo_gem->reloc_count = 0;
	bo_gem->used_as_reloc_target = false;

	DBG("bo_unreference final: %d (%s)\n",
	    bo_gem->gem_handle, bo_gem->name);

	/* release memory associated with this object */
	if (bo_gem->reloc_target_info) {
		free(bo_gem->reloc_target_info);
		bo_gem->reloc_target_info = NULL;
	}
	if (bo_gem->relocs) {
		free(bo_gem->relocs);
		bo_gem->relocs = NULL;
	}

	/* Clear any left-over mappings */
	if (bo_gem->map_count) {
		DBG("bo freed with non-zero map-count %d\n", bo_gem->map_count);
		bo_gem->map_count = 0;
		drm_intel_gem_bo_close_vma(bufmgr_gem, bo_gem);
	}

	DRMLISTDEL(&bo_gem->name_list);

	bucket = drm_intel_gem_bo_bucket_for_size(bufmgr_gem, bo->size);
	/* Put the buffer into our internal cache for reuse if we can. */
	if (bufmgr_gem->bo_reuse && bo_gem->reusable && bucket != NULL &&
	    drm_intel_gem_bo_madvise_internal(bufmgr_gem, bo_gem,
					      I915_MADV_DONTNEED)) {
		bo_gem->free_time = time;

		bo_gem->name = NULL;
		bo_gem->validate_index = -1;

		DRMLISTADDTAIL(&bo_gem->head, &bucket->head);
	} else {
		drm_intel_gem_bo_free(bo);
	}
}

static void drm_intel_gem_bo_unreference_locked_timed(drm_intel_bo *bo,
						      time_t time)
{
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;

	assert(atomic_read(&bo_gem->refcount) > 0);
	if (atomic_dec_and_test(&bo_gem->refcount))
		drm_intel_gem_bo_unreference_final(bo, time);
}

static void drm_intel_gem_bo_unreference(drm_intel_bo *bo)
{
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;

	assert(atomic_read(&bo_gem->refcount) > 0);
	if (atomic_dec_and_test(&bo_gem->refcount)) {
		drm_intel_bufmgr_gem *bufmgr_gem =
		    (drm_intel_bufmgr_gem *) bo->bufmgr;
		struct timespec time;

		clock_gettime(CLOCK_MONOTONIC, &time);

		pthread_mutex_lock(&bufmgr_gem->lock);
		drm_intel_gem_bo_unreference_final(bo, time.tv_sec);
		drm_intel_gem_cleanup_bo_cache(bufmgr_gem, time.tv_sec);
		pthread_mutex_unlock(&bufmgr_gem->lock);
	}
}

/*
 * OpenBSD only supports GTT mapping of the backing memory, not a choice of
 * faulted gtt memory, or the backing pages. This is due to cache coherency
 * issues.
 *
 * Therefore, bo_map_gtt calls bo_map.
 */
static int drm_intel_gem_bo_map(drm_intel_bo *bo, int write_enable)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	struct drm_i915_gem_set_domain set_domain;
	int ret;

	pthread_mutex_lock(&bufmgr_gem->lock);

	if (bo_gem->map_count++ == 0)
		drm_intel_gem_bo_open_vma(bufmgr_gem, bo_gem);

	if (!bo_gem->mem_virtual) {
		struct drm_i915_gem_mmap mmap_arg;

		DBG("bo_map: %d (%s), map_count=%d\n",
		    bo_gem->gem_handle, bo_gem->name, bo_gem->map_count);

		memset(&mmap_arg, 0, sizeof(mmap_arg));
		mmap_arg.handle = bo_gem->gem_handle;
		mmap_arg.offset = 0;
		mmap_arg.size = bo->size;
		ret = drmIoctl(bufmgr_gem->fd,
			       DRM_IOCTL_I915_GEM_MMAP,
			       &mmap_arg);
		if (ret != 0) {
			ret = -errno;
			DBG("%s:%d: Error mapping buffer %d (%s): %s .\n",
			    __FILE__, __LINE__, bo_gem->gem_handle,
			    bo_gem->name, strerror(errno));
			if (--bo_gem->map_count == 0)
				drm_intel_gem_bo_close_vma(bufmgr_gem, bo_gem);
			pthread_mutex_unlock(&bufmgr_gem->lock);
			return ret;
		}
		bo_gem->mem_virtual = (void *)(uintptr_t) mmap_arg.addr_ptr;
	}
	DBG("bo_map: %d (%s) -> %p\n", bo_gem->gem_handle, bo_gem->name,
	    bo_gem->mem_virtual);
	bo->virtual = bo_gem->mem_virtual;

	set_domain.handle = bo_gem->gem_handle;
	set_domain.read_domains = I915_GEM_DOMAIN_GTT /* XXX _CPU */;
	if (write_enable)
		set_domain.write_domain = I915_GEM_DOMAIN_GTT /* XXX _CPU */;
	else
		set_domain.write_domain = 0;
	ret = drmIoctl(bufmgr_gem->fd,
		       DRM_IOCTL_I915_GEM_SET_DOMAIN,
		       &set_domain);
	if (ret != 0) {
		DBG("%s:%d: Error setting to CPU domain %d: %s\n",
		    __FILE__, __LINE__, bo_gem->gem_handle,
		    strerror(errno));
	}

	if (write_enable)
		bo_gem->mapped_cpu_write = true;

	pthread_mutex_unlock(&bufmgr_gem->lock);

	return 0;
}

int drm_intel_gem_bo_map_gtt(drm_intel_bo *bo)
{
	return drm_intel_gem_bo_map(bo, 1);
#ifndef __OpenBSD__
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	struct drm_i915_gem_set_domain set_domain;
	int ret;

	pthread_mutex_lock(&bufmgr_gem->lock);

	if (bo_gem->map_count++ == 0)
		drm_intel_gem_bo_open_vma(bufmgr_gem, bo_gem);

	/* Get a mapping of the buffer if we haven't before. */
	if (bo_gem->gtt_virtual == NULL) {
		struct drm_i915_gem_mmap_gtt mmap_arg;

		DBG("bo_map_gtt: mmap %d (%s), map_count=%d\n",
		    bo_gem->gem_handle, bo_gem->name, bo_gem->map_count);

		memset(&mmap_arg, 0, sizeof(mmap_arg));
		mmap_arg.handle = bo_gem->gem_handle;

		/* Get the fake offset back... */
		ret = drmIoctl(bufmgr_gem->fd,
			       DRM_IOCTL_I915_GEM_MMAP_GTT,
			       &mmap_arg);
		if (ret != 0) {
			ret = -errno;
			DBG("%s:%d: Error preparing buffer map %d (%s): %s .\n",
			    __FILE__, __LINE__,
			    bo_gem->gem_handle, bo_gem->name,
			    strerror(errno));
			if (--bo_gem->map_count == 0)
				drm_intel_gem_bo_close_vma(bufmgr_gem, bo_gem);
			pthread_mutex_unlock(&bufmgr_gem->lock);
			return ret;
		}

		/* and mmap it */
		bo_gem->gtt_virtual = mmap(0, bo->size, PROT_READ | PROT_WRITE,
					   MAP_SHARED, bufmgr_gem->fd,
					   mmap_arg.offset);
		if (bo_gem->gtt_virtual == MAP_FAILED) {
			bo_gem->gtt_virtual = NULL;
			ret = -errno;
			DBG("%s:%d: Error mapping buffer %d (%s): %s .\n",
			    __FILE__, __LINE__,
			    bo_gem->gem_handle, bo_gem->name,
			    strerror(errno));
			if (--bo_gem->map_count == 0)
				drm_intel_gem_bo_close_vma(bufmgr_gem, bo_gem);
			pthread_mutex_unlock(&bufmgr_gem->lock);
			return ret;
		}
	}

	bo->virtual = bo_gem->gtt_virtual;

	DBG("bo_map_gtt: %d (%s) -> %p\n", bo_gem->gem_handle, bo_gem->name,
	    bo_gem->gtt_virtual);

	/* Now move it to the GTT domain so that the CPU caches are flushed */
	set_domain.handle = bo_gem->gem_handle;
	set_domain.read_domains = I915_GEM_DOMAIN_GTT;
	set_domain.write_domain = I915_GEM_DOMAIN_GTT;
	ret = drmIoctl(bufmgr_gem->fd,
		       DRM_IOCTL_I915_GEM_SET_DOMAIN,
		       &set_domain);
	if (ret != 0) {
		DBG("%s:%d: Error setting domain %d: %s\n",
		    __FILE__, __LINE__, bo_gem->gem_handle,
		    strerror(errno));
	}

	pthread_mutex_unlock(&bufmgr_gem->lock);

	return 0;
#endif
}

static int drm_intel_gem_bo_unmap(drm_intel_bo *bo)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	struct drm_i915_gem_sw_finish sw_finish;
	int ret = 0;

	if (bo == NULL)
		return 0;

	pthread_mutex_lock(&bufmgr_gem->lock);

	if (bo_gem->map_count <= 0) {
		DBG("attempted to unmap an unmapped bo\n");
		pthread_mutex_unlock(&bufmgr_gem->lock);
		/* Preserve the old behaviour of just treating this as a
		 * no-op rather than reporting the error.
		 */
		return 0;
	}

	if (bo_gem->mapped_cpu_write) {
#ifndef __OpenBSD__
		/* Cause a flush to happen if the buffer's pinned for
		 * scanout, so the results show up in a timely manner.
		 * Unlike GTT set domains, this only does work if the
		 * buffer should be scanout-related.
		 */
		sw_finish.handle = bo_gem->gem_handle;
		ret = drmIoctl(bufmgr_gem->fd,
			       DRM_IOCTL_I915_GEM_SW_FINISH,
			       &sw_finish);
		ret = ret == -1 ? -errno : 0;
#endif
		bo_gem->mapped_cpu_write = false;
	}

	/* We need to unmap after every innovation as we cannot track
	 * an open vma for every bo as that will exhaasut the system
	 * limits and cause later failures.
	 */
	if (--bo_gem->map_count == 0) {
		drm_intel_gem_bo_close_vma(bufmgr_gem, bo_gem);
		bo->virtual = NULL;
	}
	pthread_mutex_unlock(&bufmgr_gem->lock);

	return ret;
}

int drm_intel_gem_bo_unmap_gtt(drm_intel_bo *bo)
{
	return drm_intel_gem_bo_unmap(bo);
}

static int
drm_intel_gem_bo_subdata(drm_intel_bo *bo, unsigned long offset,
			 unsigned long size, const void *data)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	struct drm_i915_gem_pwrite pwrite;
	int ret;

	memset(&pwrite, 0, sizeof(pwrite));
	pwrite.handle = bo_gem->gem_handle;
	pwrite.offset = offset;
	pwrite.size = size;
	pwrite.data_ptr = (uint64_t) (uintptr_t) data;
	ret = drmIoctl(bufmgr_gem->fd,
		       DRM_IOCTL_I915_GEM_PWRITE,
		       &pwrite);
	if (ret != 0) {
		ret = -errno;
		DBG("%s:%d: Error writing data to buffer %d: (%d %d) %s .\n",
		    __FILE__, __LINE__, bo_gem->gem_handle, (int)offset,
		    (int)size, strerror(errno));
	}

	return ret;
}

static int
drm_intel_gem_get_pipe_from_crtc_id(drm_intel_bufmgr *bufmgr, int crtc_id)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bufmgr;
	struct drm_i915_get_pipe_from_crtc_id get_pipe_from_crtc_id;
	int ret;

	get_pipe_from_crtc_id.crtc_id = crtc_id;
	ret = drmIoctl(bufmgr_gem->fd,
		       DRM_IOCTL_I915_GET_PIPE_FROM_CRTC_ID,
		       &get_pipe_from_crtc_id);
	if (ret != 0) {
		/* We return -1 here to signal that we don't
		 * know which pipe is associated with this crtc.
		 * This lets the caller know that this information
		 * isn't available; using the wrong pipe for
		 * vblank waiting can cause the chipset to lock up
		 */
		return -1;
	}

	return get_pipe_from_crtc_id.pipe;
}

static int
drm_intel_gem_bo_get_subdata(drm_intel_bo *bo, unsigned long offset,
			     unsigned long size, void *data)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	struct drm_i915_gem_pread pread;
	int ret;

	memset(&pread, 0, sizeof(pread));
	pread.handle = bo_gem->gem_handle;
	pread.offset = offset;
	pread.size = size;
	pread.data_ptr = (uint64_t) (uintptr_t) data;
	ret = drmIoctl(bufmgr_gem->fd,
		       DRM_IOCTL_I915_GEM_PREAD,
		       &pread);
	if (ret != 0) {
		ret = -errno;
		DBG("%s:%d: Error reading data from buffer %d: (%d %d) %s .\n",
		    __FILE__, __LINE__, bo_gem->gem_handle, (int)offset,
		    (int)size, strerror(errno));
	}

	return ret;
}

/** Waits for all GPU rendering with the object to have completed. */
static void
drm_intel_gem_bo_wait_rendering(drm_intel_bo *bo)
{
	drm_intel_gem_bo_start_gtt_access(bo, 1);
}

/**
 * Sets the object to the GTT read and possibly write domain, used by the X
 * 2D driver in the absence of kernel support to do drm_intel_gem_bo_map_gtt().
 *
 * In combination with drm_intel_gem_bo_pin() and manual fence management, we
 * can do tiled pixmaps this way.
 */
void
drm_intel_gem_bo_start_gtt_access(drm_intel_bo *bo, int write_enable)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	struct drm_i915_gem_set_domain set_domain;
	int ret;

	set_domain.handle = bo_gem->gem_handle;
	set_domain.read_domains = I915_GEM_DOMAIN_GTT;
	set_domain.write_domain = write_enable ? I915_GEM_DOMAIN_GTT : 0;
	ret = drmIoctl(bufmgr_gem->fd,
		       DRM_IOCTL_I915_GEM_SET_DOMAIN,
		       &set_domain);
	if (ret != 0) {
		DBG("%s:%d: Error setting memory domains %d (%08x %08x): %s .\n",
		    __FILE__, __LINE__, bo_gem->gem_handle,
		    set_domain.read_domains, set_domain.write_domain,
		    strerror(errno));
	}
}

static void
drm_intel_bufmgr_gem_destroy(drm_intel_bufmgr *bufmgr)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bufmgr;
	int i;

	free(bufmgr_gem->exec2_objects);
#ifndef __OpenBSD__
	free(bufmgr_gem->exec_objects);
#endif
	free(bufmgr_gem->exec_bos);

	pthread_mutex_destroy(&bufmgr_gem->lock);

	/* Free any cached buffer objects we were going to reuse */
	for (i = 0; i < bufmgr_gem->num_buckets; i++) {
		struct drm_intel_gem_bo_bucket *bucket =
		    &bufmgr_gem->cache_bucket[i];
		drm_intel_bo_gem *bo_gem;

		while (!DRMLISTEMPTY(&bucket->head)) {
			bo_gem = DRMLISTENTRY(drm_intel_bo_gem,
					      bucket->head.next, head);
			DRMLISTDEL(&bo_gem->head);

			drm_intel_gem_bo_free(&bo_gem->bo);
		}
	}

	free(bufmgr);
}

/**
 * Adds the target buffer to the validation list and adds the relocation
 * to the reloc_buffer's relocation list.
 *
 * The relocation entry at the given offset must already contain the
 * precomputed relocation value, because the kernel will optimize out
 * the relocation entry write when the buffer hasn't moved from the
 * last known offset in target_bo.
 */
static int
do_bo_emit_reloc(drm_intel_bo *bo, uint32_t offset,
		 drm_intel_bo *target_bo, uint32_t target_offset,
		 uint32_t read_domains, uint32_t write_domain,
		 bool need_fence)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	drm_intel_bo_gem *target_bo_gem = (drm_intel_bo_gem *) target_bo;
	bool fenced_command;

	if (bo_gem->has_error)
		return -ENOMEM;

	if (target_bo_gem->has_error) {
		bo_gem->has_error = true;
		return -ENOMEM;
	}

	/* We never use HW fences for rendering on 965+ */
	if (bufmgr_gem->gen >= 4)
		need_fence = false;

	fenced_command = need_fence;
	if (target_bo_gem->tiling_mode == I915_TILING_NONE)
		need_fence = false;

	/* Create a new relocation list if needed */
	if (bo_gem->relocs == NULL && drm_intel_setup_reloc_list(bo))
		return -ENOMEM;

	/* Check overflow */
	assert(bo_gem->reloc_count < bufmgr_gem->max_relocs);

	/* Check args */
	assert(offset <= bo->size - 4);
	assert((write_domain & (write_domain - 1)) == 0);

	/* Make sure that we're not adding a reloc to something whose size has
	 * already been accounted for.
	 */
	assert(!bo_gem->used_as_reloc_target);
	if (target_bo_gem != bo_gem) {
		target_bo_gem->used_as_reloc_target = true;
		bo_gem->reloc_tree_size += target_bo_gem->reloc_tree_size;
	}
	/* An object needing a fence is a tiled buffer, so it won't have
	 * relocs to other buffers.
	 */
	if (need_fence)
		target_bo_gem->reloc_tree_fences = 1;
	bo_gem->reloc_tree_fences += target_bo_gem->reloc_tree_fences;

	bo_gem->relocs[bo_gem->reloc_count].offset = offset;
	bo_gem->relocs[bo_gem->reloc_count].delta = target_offset;
	bo_gem->relocs[bo_gem->reloc_count].target_handle =
	    target_bo_gem->gem_handle;
	bo_gem->relocs[bo_gem->reloc_count].read_domains = read_domains;
	bo_gem->relocs[bo_gem->reloc_count].write_domain = write_domain;
	bo_gem->relocs[bo_gem->reloc_count].presumed_offset = target_bo->offset;

	bo_gem->reloc_target_info[bo_gem->reloc_count].bo = target_bo;
	if (target_bo != bo)
		drm_intel_gem_bo_reference(target_bo);
	if (fenced_command)
		bo_gem->reloc_target_info[bo_gem->reloc_count].flags =
			DRM_INTEL_RELOC_FENCE;
	else
		bo_gem->reloc_target_info[bo_gem->reloc_count].flags = 0;

	bo_gem->reloc_count++;

	return 0;
}

static int
drm_intel_gem_bo_emit_reloc(drm_intel_bo *bo, uint32_t offset,
			    drm_intel_bo *target_bo, uint32_t target_offset,
			    uint32_t read_domains, uint32_t write_domain)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *)bo->bufmgr;

	return do_bo_emit_reloc(bo, offset, target_bo, target_offset,
				read_domains, write_domain,
				!bufmgr_gem->fenced_relocs);
}

static int
drm_intel_gem_bo_emit_reloc_fence(drm_intel_bo *bo, uint32_t offset,
				  drm_intel_bo *target_bo,
				  uint32_t target_offset,
				  uint32_t read_domains, uint32_t write_domain)
{
	return do_bo_emit_reloc(bo, offset, target_bo, target_offset,
				read_domains, write_domain, true);
}

int
drm_intel_gem_bo_get_reloc_count(drm_intel_bo *bo)
{
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;

	return bo_gem->reloc_count;
}

/**
 * Removes existing relocation entries in the BO after "start".
 *
 * This allows a user to avoid a two-step process for state setup with
 * counting up all the buffer objects and doing a
 * drm_intel_bufmgr_check_aperture_space() before emitting any of the
 * relocations for the state setup.  Instead, save the state of the
 * batchbuffer including drm_intel_gem_get_reloc_count(), emit all the
 * state, and then check if it still fits in the aperture.
 *
 * Any further drm_intel_bufmgr_check_aperture_space() queries
 * involving this buffer in the tree are undefined after this call.
 */
void
drm_intel_gem_bo_clear_relocs(drm_intel_bo *bo, int start)
{
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	int i;
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);

	assert(bo_gem->reloc_count >= start);
	/* Unreference the cleared target buffers */
	for (i = start; i < bo_gem->reloc_count; i++) {
		if (bo_gem->reloc_target_info[i].bo != bo) {
			drm_intel_gem_bo_unreference_locked_timed(bo_gem->
								  reloc_target_info[i].bo,
								  time.tv_sec);
		}
	}
	bo_gem->reloc_count = start;
}

#ifndef __OpenBSD__
/**
 * Walk the tree of relocations rooted at BO and accumulate the list of
 * validations to be performed and update the relocation buffers with
 * index values into the validation list.
 */
static void
drm_intel_gem_bo_process_reloc(drm_intel_bo *bo)
{
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	int i;

	if (bo_gem->relocs == NULL)
		return;

	for (i = 0; i < bo_gem->reloc_count; i++) {
		drm_intel_bo *target_bo = bo_gem->reloc_target_info[i].bo;

		if (target_bo == bo)
			continue;

		/* Continue walking the tree depth-first. */
		drm_intel_gem_bo_process_reloc(target_bo);

		/* Add the target to the validate list */
		drm_intel_add_validate_buffer(target_bo);
	}
}
#endif

static void
drm_intel_gem_bo_process_reloc2(drm_intel_bo *bo)
{
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *)bo;
	int i;

	if (bo_gem->relocs == NULL)
		return;

	for (i = 0; i < bo_gem->reloc_count; i++) {
		drm_intel_bo *target_bo = bo_gem->reloc_target_info[i].bo;
		int need_fence;

		if (target_bo == bo)
			continue;

		/* Continue walking the tree depth-first. */
		drm_intel_gem_bo_process_reloc2(target_bo);

		need_fence = (bo_gem->reloc_target_info[i].flags &
			      DRM_INTEL_RELOC_FENCE);

		/* Add the target to the validate list */
		drm_intel_add_validate_buffer2(target_bo, need_fence);
	}
}


#ifndef __OpenBSD__
static void
drm_intel_update_buffer_offsets(drm_intel_bufmgr_gem *bufmgr_gem)
{
	int i;

	for (i = 0; i < bufmgr_gem->exec_count; i++) {
		drm_intel_bo *bo = bufmgr_gem->exec_bos[i];
		drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;

		/* Update the buffer offset */
		if (bufmgr_gem->exec_objects[i].offset != bo->offset) {
			DBG("BO %d (%s) migrated: 0x%08lx -> 0x%08llx\n",
			    bo_gem->gem_handle, bo_gem->name, bo->offset,
			    (unsigned long long)bufmgr_gem->exec_objects[i].
			    offset);
			bo->offset = bufmgr_gem->exec_objects[i].offset;
		}
	}
}
#endif

static void
drm_intel_update_buffer_offsets2 (drm_intel_bufmgr_gem *bufmgr_gem)
{
	int i;

	for (i = 0; i < bufmgr_gem->exec_count; i++) {
		drm_intel_bo *bo = bufmgr_gem->exec_bos[i];
		drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *)bo;

		/* Update the buffer offset */
		if (bufmgr_gem->exec2_objects[i].offset != bo->offset) {
			DBG("BO %d (%s) migrated: 0x%08lx -> 0x%08llx\n",
			    bo_gem->gem_handle, bo_gem->name, bo->offset,
			    (unsigned long long)bufmgr_gem->exec2_objects[i].offset);
			bo->offset = bufmgr_gem->exec2_objects[i].offset;
		}
	}
}

#ifndef __OpenBSD__
static int
drm_intel_gem_bo_exec(drm_intel_bo *bo, int used,
		      drm_clip_rect_t * cliprects, int num_cliprects, int DR4)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	struct drm_i915_gem_execbuffer execbuf;
	int ret, i;

	if (bo_gem->has_error)
		return -ENOMEM;

	pthread_mutex_lock(&bufmgr_gem->lock);
	/* Update indices and set up the validate list. */
	drm_intel_gem_bo_process_reloc(bo);

	/* Add the batch buffer to the validation list.  There are no
	 * relocations pointing to it.
	 */
	drm_intel_add_validate_buffer(bo);

	execbuf.buffers_ptr = (uintptr_t) bufmgr_gem->exec_objects;
	execbuf.buffer_count = bufmgr_gem->exec_count;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = used;
	execbuf.cliprects_ptr = (uintptr_t) cliprects;
	execbuf.num_cliprects = num_cliprects;
	execbuf.DR1 = 0;
	execbuf.DR4 = DR4;

	ret = drmIoctl(bufmgr_gem->fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER,
		       &execbuf);
	if (ret != 0) {
		ret = -errno;
		if (errno == ENOSPC) {
			DBG("Execbuffer fails to pin. "
			    "Estimate: %u. Actual: %u. Available: %u\n",
			    drm_intel_gem_estimate_batch_space(bufmgr_gem->exec_bos,
							       bufmgr_gem->
							       exec_count),
			    drm_intel_gem_compute_batch_space(bufmgr_gem->exec_bos,
							      bufmgr_gem->
							      exec_count),
			    (unsigned int)bufmgr_gem->gtt_size);
		}
	}
	drm_intel_update_buffer_offsets(bufmgr_gem);

	if (bufmgr_gem->bufmgr.debug)
		drm_intel_gem_dump_validation_list(bufmgr_gem);

	for (i = 0; i < bufmgr_gem->exec_count; i++) {
		drm_intel_bo *bo = bufmgr_gem->exec_bos[i];
		drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;

		/* Disconnect the buffer from the validate list */
		bo_gem->validate_index = -1;
		bufmgr_gem->exec_bos[i] = NULL;
	}
	bufmgr_gem->exec_count = 0;
	pthread_mutex_unlock(&bufmgr_gem->lock);

	return ret;
}
#endif

static int
drm_intel_gem_bo_mrb_exec2(drm_intel_bo *bo, int used,
			drm_clip_rect_t *cliprects, int num_cliprects, int DR4,
			unsigned int flags)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *)bo->bufmgr;
	struct drm_i915_gem_execbuffer2 execbuf;
	int ret, i;

	switch (flags & 0x7) {
	default:
		return -EINVAL;
	case I915_EXEC_BLT:
		if (!bufmgr_gem->has_blt)
			return -EINVAL;
		break;
	case I915_EXEC_BSD:
		if (!bufmgr_gem->has_bsd)
			return -EINVAL;
		break;
	case I915_EXEC_RENDER:
	case I915_EXEC_DEFAULT:
		break;
	}

	pthread_mutex_lock(&bufmgr_gem->lock);
	/* Update indices and set up the validate list. */
	drm_intel_gem_bo_process_reloc2(bo);

	/* Add the batch buffer to the validation list.  There are no relocations
	 * pointing to it.
	 */
	drm_intel_add_validate_buffer2(bo, 0);

	execbuf.buffers_ptr = (uintptr_t)bufmgr_gem->exec2_objects;
	execbuf.buffer_count = bufmgr_gem->exec_count;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = used;
#ifndef __OpenBSD__
	execbuf.cliprects_ptr = (uintptr_t)cliprects;
	execbuf.num_cliprects = num_cliprects;
	execbuf.DR1 = 0;
	execbuf.DR4 = DR4;
#endif
	execbuf.flags = flags;
	execbuf.rsvd1 = 0;
	execbuf.rsvd2 = 0;

	ret = drmIoctl(bufmgr_gem->fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER2,
		       &execbuf);
	if (ret != 0) {
		ret = -errno;
		if (ret == -ENOSPC) {
			DBG("Execbuffer fails to pin. "
			    "Estimate: %u. Actual: %u. Available: %u\n",
			    drm_intel_gem_estimate_batch_space(bufmgr_gem->exec_bos,
							       bufmgr_gem->exec_count),
			    drm_intel_gem_compute_batch_space(bufmgr_gem->exec_bos,
							      bufmgr_gem->exec_count),
			    (unsigned int) bufmgr_gem->gtt_size);
		}
	}
	drm_intel_update_buffer_offsets2(bufmgr_gem);

	if (bufmgr_gem->bufmgr.debug)
		drm_intel_gem_dump_validation_list(bufmgr_gem);

	for (i = 0; i < bufmgr_gem->exec_count; i++) {
		drm_intel_bo *bo = bufmgr_gem->exec_bos[i];
		drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *)bo;

		/* Disconnect the buffer from the validate list */
		bo_gem->validate_index = -1;
		bufmgr_gem->exec_bos[i] = NULL;
	}
	bufmgr_gem->exec_count = 0;
	pthread_mutex_unlock(&bufmgr_gem->lock);

	return ret;
}

static int
drm_intel_gem_bo_exec2(drm_intel_bo *bo, int used,
		       drm_clip_rect_t *cliprects, int num_cliprects,
		       int DR4)
{
	return drm_intel_gem_bo_mrb_exec2(bo, used,
					cliprects, num_cliprects, DR4,
					I915_EXEC_RENDER);
}

static int
drm_intel_gem_bo_pin(drm_intel_bo *bo, uint32_t alignment)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	struct drm_i915_gem_pin pin;
	int ret;

	memset(&pin, 0, sizeof(pin));
	pin.handle = bo_gem->gem_handle;
	pin.alignment = alignment;

	ret = drmIoctl(bufmgr_gem->fd,
		       DRM_IOCTL_I915_GEM_PIN,
		       &pin);
	if (ret != 0)
		return -errno;

	bo->offset = pin.offset;
	return 0;
}

static int
drm_intel_gem_bo_unpin(drm_intel_bo *bo)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	struct drm_i915_gem_unpin unpin;
	int ret;

	memset(&unpin, 0, sizeof(unpin));
	unpin.handle = bo_gem->gem_handle;

	ret = drmIoctl(bufmgr_gem->fd, DRM_IOCTL_I915_GEM_UNPIN, &unpin);
	if (ret != 0)
		return -errno;

	return 0;
}

static int
drm_intel_gem_bo_set_tiling_internal(drm_intel_bo *bo,
				     uint32_t tiling_mode,
				     uint32_t stride)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	struct drm_i915_gem_set_tiling set_tiling;
	int ret;

	if (bo_gem->global_name == 0 &&
	    tiling_mode == bo_gem->tiling_mode &&
	    stride == bo_gem->stride)
		return 0;

	memset(&set_tiling, 0, sizeof(set_tiling));
	do {
		/* set_tiling is slightly broken and overwrites the
		 * input on the error path, so we have to open code
		 * rmIoctl.
		 */
		set_tiling.handle = bo_gem->gem_handle;
		set_tiling.tiling_mode = tiling_mode;
		set_tiling.stride = stride;

		ret = ioctl(bufmgr_gem->fd,
			    DRM_IOCTL_I915_GEM_SET_TILING,
			    &set_tiling);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
	if (ret == -1)
		return -errno;

	bo_gem->tiling_mode = set_tiling.tiling_mode;
	bo_gem->swizzle_mode = set_tiling.swizzle_mode;
	bo_gem->stride = set_tiling.stride;
	return 0;
}

static int
drm_intel_gem_bo_set_tiling(drm_intel_bo *bo, uint32_t * tiling_mode,
			    uint32_t stride)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	int ret;

	/* Linear buffers have no stride. By ensuring that we only ever use
	 * stride 0 with linear buffers, we simplify our code.
	 */
	if (*tiling_mode == I915_TILING_NONE)
		stride = 0;

	ret = drm_intel_gem_bo_set_tiling_internal(bo, *tiling_mode, stride);
	if (ret == 0)
		drm_intel_bo_gem_set_in_aperture_size(bufmgr_gem, bo_gem);

	*tiling_mode = bo_gem->tiling_mode;
	return ret;
}

static int
drm_intel_gem_bo_get_tiling(drm_intel_bo *bo, uint32_t * tiling_mode,
			    uint32_t * swizzle_mode)
{
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;

	*tiling_mode = bo_gem->tiling_mode;
	*swizzle_mode = bo_gem->swizzle_mode;
	return 0;
}

static int
drm_intel_gem_bo_flink(drm_intel_bo *bo, uint32_t * name)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bo->bufmgr;
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	struct drm_gem_flink flink;
	int ret;

	if (!bo_gem->global_name) {
		memset(&flink, 0, sizeof(flink));
		flink.handle = bo_gem->gem_handle;

		ret = drmIoctl(bufmgr_gem->fd, DRM_IOCTL_GEM_FLINK, &flink);
		if (ret != 0)
			return -errno;
		bo_gem->global_name = flink.name;
		bo_gem->reusable = false;

		DRMLISTADDTAIL(&bo_gem->name_list, &bufmgr_gem->named);
	}

	*name = bo_gem->global_name;
	return 0;
}

/**
 * Enables unlimited caching of buffer objects for reuse.
 *
 * This is potentially very memory expensive, as the cache at each bucket
 * size is only bounded by how many buffers of that size we've managed to have
 * in flight at once.
 */
void
drm_intel_bufmgr_gem_enable_reuse(drm_intel_bufmgr *bufmgr)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *) bufmgr;

	bufmgr_gem->bo_reuse = true;
}

/**
 * Enable use of fenced reloc type.
 *
 * New code should enable this to avoid unnecessary fence register
 * allocation.  If this option is not enabled, all relocs will have fence
 * register allocated.
 */
void
drm_intel_bufmgr_gem_enable_fenced_relocs(drm_intel_bufmgr *bufmgr)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *)bufmgr;

	if (bufmgr_gem->bufmgr.bo_exec == drm_intel_gem_bo_exec2)
		bufmgr_gem->fenced_relocs = true;
}

/**
 * Return the additional aperture space required by the tree of buffer objects
 * rooted at bo.
 */
static int
drm_intel_gem_bo_get_aperture_space(drm_intel_bo *bo)
{
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	int i;
	int total = 0;

	if (bo == NULL || bo_gem->included_in_check_aperture)
		return 0;

	total += bo->size;
	bo_gem->included_in_check_aperture = true;

	for (i = 0; i < bo_gem->reloc_count; i++)
		total +=
		    drm_intel_gem_bo_get_aperture_space(bo_gem->
							reloc_target_info[i].bo);

	return total;
}

/**
 * Count the number of buffers in this list that need a fence reg
 *
 * If the count is greater than the number of available regs, we'll have
 * to ask the caller to resubmit a batch with fewer tiled buffers.
 *
 * This function over-counts if the same buffer is used multiple times.
 */
static unsigned int
drm_intel_gem_total_fences(drm_intel_bo ** bo_array, int count)
{
	int i;
	unsigned int total = 0;

	for (i = 0; i < count; i++) {
		drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo_array[i];

		if (bo_gem == NULL)
			continue;

		total += bo_gem->reloc_tree_fences;
	}
	return total;
}

/**
 * Clear the flag set by drm_intel_gem_bo_get_aperture_space() so we're ready
 * for the next drm_intel_bufmgr_check_aperture_space() call.
 */
static void
drm_intel_gem_bo_clear_aperture_space_flag(drm_intel_bo *bo)
{
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	int i;

	if (bo == NULL || !bo_gem->included_in_check_aperture)
		return;

	bo_gem->included_in_check_aperture = false;

	for (i = 0; i < bo_gem->reloc_count; i++)
		drm_intel_gem_bo_clear_aperture_space_flag(bo_gem->
							   reloc_target_info[i].bo);
}

/**
 * Return a conservative estimate for the amount of aperture required
 * for a collection of buffers. This may double-count some buffers.
 */
static unsigned int
drm_intel_gem_estimate_batch_space(drm_intel_bo **bo_array, int count)
{
	int i;
	unsigned int total = 0;

	for (i = 0; i < count; i++) {
		drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo_array[i];
		if (bo_gem != NULL)
			total += bo_gem->reloc_tree_size;
	}
	return total;
}

/**
 * Return the amount of aperture needed for a collection of buffers.
 * This avoids double counting any buffers, at the cost of looking
 * at every buffer in the set.
 */
static unsigned int
drm_intel_gem_compute_batch_space(drm_intel_bo **bo_array, int count)
{
	int i;
	unsigned int total = 0;

	for (i = 0; i < count; i++) {
		total += drm_intel_gem_bo_get_aperture_space(bo_array[i]);
		/* For the first buffer object in the array, we get an
		 * accurate count back for its reloc_tree size (since nothing
		 * had been flagged as being counted yet).  We can save that
		 * value out as a more conservative reloc_tree_size that
		 * avoids double-counting target buffers.  Since the first
		 * buffer happens to usually be the batch buffer in our
		 * callers, this can pull us back from doing the tree
		 * walk on every new batch emit.
		 */
		if (i == 0) {
			drm_intel_bo_gem *bo_gem =
			    (drm_intel_bo_gem *) bo_array[i];
			bo_gem->reloc_tree_size = total;
		}
	}

	for (i = 0; i < count; i++)
		drm_intel_gem_bo_clear_aperture_space_flag(bo_array[i]);
	return total;
}

/**
 * Return -1 if the batchbuffer should be flushed before attempting to
 * emit rendering referencing the buffers pointed to by bo_array.
 *
 * This is required because if we try to emit a batchbuffer with relocations
 * to a tree of buffers that won't simultaneously fit in the aperture,
 * the rendering will return an error at a point where the software is not
 * prepared to recover from it.
 *
 * However, we also want to emit the batchbuffer significantly before we reach
 * the limit, as a series of batchbuffers each of which references buffers
 * covering almost all of the aperture means that at each emit we end up
 * waiting to evict a buffer from the last rendering, and we get synchronous
 * performance.  By emitting smaller batchbuffers, we eat some CPU overhead to
 * get better parallelism.
 */
static int
drm_intel_gem_check_aperture_space(drm_intel_bo **bo_array, int count)
{
	drm_intel_bufmgr_gem *bufmgr_gem =
	    (drm_intel_bufmgr_gem *) bo_array[0]->bufmgr;
	unsigned int total = 0;
	unsigned int threshold = bufmgr_gem->gtt_size * 3 / 4;
	int total_fences;

	/* Check for fence reg constraints if necessary */
	if (bufmgr_gem->available_fences) {
		total_fences = drm_intel_gem_total_fences(bo_array, count);
		if (total_fences > bufmgr_gem->available_fences)
			return -ENOSPC;
	}

	total = drm_intel_gem_estimate_batch_space(bo_array, count);

	if (total > threshold)
		total = drm_intel_gem_compute_batch_space(bo_array, count);

	if (total > threshold) {
		DBG("check_space: overflowed available aperture, "
		    "%dkb vs %dkb\n",
		    total / 1024, (int)bufmgr_gem->gtt_size / 1024);
		return -ENOSPC;
	} else {
		DBG("drm_check_space: total %dkb vs bufgr %dkb\n", total / 1024,
		    (int)bufmgr_gem->gtt_size / 1024);
		return 0;
	}
}

/*
 * Disable buffer reuse for objects which are shared with the kernel
 * as scanout buffers
 */
static int
drm_intel_gem_bo_disable_reuse(drm_intel_bo *bo)
{
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;

	bo_gem->reusable = false;
	return 0;
}

static int
drm_intel_gem_bo_is_reusable(drm_intel_bo *bo)
{
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;

	return bo_gem->reusable;
}

static int
_drm_intel_gem_bo_references(drm_intel_bo *bo, drm_intel_bo *target_bo)
{
	drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *) bo;
	int i;

	for (i = 0; i < bo_gem->reloc_count; i++) {
		if (bo_gem->reloc_target_info[i].bo == target_bo)
			return 1;
		if (bo == bo_gem->reloc_target_info[i].bo)
			continue;
		if (_drm_intel_gem_bo_references(bo_gem->reloc_target_info[i].bo,
						target_bo))
			return 1;
	}

	return 0;
}

/** Return true if target_bo is referenced by bo's relocation tree. */
static int
drm_intel_gem_bo_references(drm_intel_bo *bo, drm_intel_bo *target_bo)
{
	drm_intel_bo_gem *target_bo_gem = (drm_intel_bo_gem *) target_bo;

	if (bo == NULL || target_bo == NULL)
		return 0;
	if (target_bo_gem->used_as_reloc_target)
		return _drm_intel_gem_bo_references(bo, target_bo);
	return 0;
}

static void
add_bucket(drm_intel_bufmgr_gem *bufmgr_gem, int size)
{
	unsigned int i = bufmgr_gem->num_buckets;

	assert(i < ARRAY_SIZE(bufmgr_gem->cache_bucket));

	DRMINITLISTHEAD(&bufmgr_gem->cache_bucket[i].head);
	bufmgr_gem->cache_bucket[i].size = size;
	bufmgr_gem->num_buckets++;
}

static void
init_cache_buckets(drm_intel_bufmgr_gem *bufmgr_gem)
{
	unsigned long size, cache_max_size = 64 * 1024 * 1024;

	/* OK, so power of two buckets was too wasteful of memory.
	 * Give 3 other sizes between each power of two, to hopefully
	 * cover things accurately enough.  (The alternative is
	 * probably to just go for exact matching of sizes, and assume
	 * that for things like composited window resize the tiled
	 * width/height alignment and rounding of sizes to pages will
	 * get us useful cache hit rates anyway)
	 */
	add_bucket(bufmgr_gem, 4096);
	add_bucket(bufmgr_gem, 4096 * 2);
	add_bucket(bufmgr_gem, 4096 * 3);

	/* Initialize the linked lists for BO reuse cache. */
	for (size = 4 * 4096; size <= cache_max_size; size *= 2) {
		add_bucket(bufmgr_gem, size);

		add_bucket(bufmgr_gem, size + size * 1 / 4);
		add_bucket(bufmgr_gem, size + size * 2 / 4);
		add_bucket(bufmgr_gem, size + size * 3 / 4);
	}
}

void
drm_intel_bufmgr_gem_set_vma_cache_size(drm_intel_bufmgr *bufmgr, int limit)
{
	drm_intel_bufmgr_gem *bufmgr_gem = (drm_intel_bufmgr_gem *)bufmgr;

	bufmgr_gem->vma_max = limit;

	drm_intel_gem_bo_purge_vma_cache(bufmgr_gem);
}

/**
 * Initializes the GEM buffer manager, which uses the kernel to allocate, map,
 * and manage map buffer objections.
 *
 * \param fd File descriptor of the opened DRM device.
 */
drm_intel_bufmgr *
drm_intel_bufmgr_gem_init(int fd, int batch_size)
{
	drm_intel_bufmgr_gem *bufmgr_gem;
	struct drm_i915_gem_get_aperture aperture;
	drm_i915_getparam_t gp;
	int ret, tmp;
#ifndef __OpenBSD__
	bool exec2 = false;
#endif

	bufmgr_gem = calloc(1, sizeof(*bufmgr_gem));
	if (bufmgr_gem == NULL)
		return NULL;

	bufmgr_gem->fd = fd;

	if (pthread_mutex_init(&bufmgr_gem->lock, NULL) != 0) {
		free(bufmgr_gem);
		return NULL;
	}

	ret = drmIoctl(bufmgr_gem->fd,
		       DRM_IOCTL_I915_GEM_GET_APERTURE,
		       &aperture);

	if (ret == 0)
		bufmgr_gem->gtt_size = aperture.aper_available_size;
	else {
		fprintf(stderr, "DRM_IOCTL_I915_GEM_APERTURE failed: %s\n",
			strerror(errno));
		bufmgr_gem->gtt_size = 128 * 1024 * 1024;
		fprintf(stderr, "Assuming %dkB available aperture size.\n"
			"May lead to reduced performance or incorrect "
			"rendering.\n",
			(int)bufmgr_gem->gtt_size / 1024);
	}

	gp.param = I915_PARAM_CHIPSET_ID;
	gp.value = &bufmgr_gem->pci_device;
	ret = drmIoctl(bufmgr_gem->fd, DRM_IOCTL_I915_GETPARAM, &gp);
	if (ret) {
		fprintf(stderr, "get chip id failed: %d [%d]\n", ret, errno);
		fprintf(stderr, "param: %d, val: %d\n", gp.param, *gp.value);
	}

	if (IS_GEN2(bufmgr_gem->pci_device))
		bufmgr_gem->gen = 2;
	else if (IS_GEN3(bufmgr_gem->pci_device))
		bufmgr_gem->gen = 3;
	else if (IS_GEN4(bufmgr_gem->pci_device))
		bufmgr_gem->gen = 4;
	else if (IS_GEN5(bufmgr_gem->pci_device))
		bufmgr_gem->gen = 5;
	else if (IS_GEN6(bufmgr_gem->pci_device))
		bufmgr_gem->gen = 6;
	else if (IS_GEN7(bufmgr_gem->pci_device))
        	bufmgr_gem->gen = 7;
	else
        	assert(0);

	if (IS_GEN3(bufmgr_gem->pci_device) &&
	    bufmgr_gem->gtt_size > 256*1024*1024) {
		/* The unmappable part of gtt on gen 3 (i.e. above 256MB) can't
		 * be used for tiled blits. To simplify the accounting, just
		 * substract the unmappable part (fixed to 256MB on all known
		 * gen3 devices) if the kernel advertises it. */
		bufmgr_gem->gtt_size -= 256*1024*1024;
	}

	gp.value = &tmp;

#ifndef __OpenBSD__
	gp.param = I915_PARAM_HAS_EXECBUF2;
	ret = drmIoctl(bufmgr_gem->fd, DRM_IOCTL_I915_GETPARAM, &gp);
	if (!ret)
		exec2 = true;
#endif

	gp.param = I915_PARAM_HAS_BSD;
	ret = drmIoctl(bufmgr_gem->fd, DRM_IOCTL_I915_GETPARAM, &gp);
	bufmgr_gem->has_bsd = ret == 0;

	gp.param = I915_PARAM_HAS_BLT;
	ret = drmIoctl(bufmgr_gem->fd, DRM_IOCTL_I915_GETPARAM, &gp);
	bufmgr_gem->has_blt = ret == 0;

	gp.param = I915_PARAM_HAS_RELAXED_FENCING;
	ret = drmIoctl(bufmgr_gem->fd, DRM_IOCTL_I915_GETPARAM, &gp);
	bufmgr_gem->has_relaxed_fencing = ret == 0;

#ifndef __OpenBSD__
	gp.param = I915_PARAM_HAS_LLC;
	ret = drmIoctl(bufmgr_gem->fd, DRM_IOCTL_I915_GETPARAM, &gp);
	if (ret == -EINVAL) {
#endif
		/* Kernel does not supports HAS_LLC query, fallback to GPU
		 * generation detection and assume that we have LLC on GEN6/7
		 */
		bufmgr_gem->has_llc = (IS_GEN6(bufmgr_gem->pci_device) |
				IS_GEN7(bufmgr_gem->pci_device));
#ifndef __OpenBSD__
	} else
		bufmgr_gem->has_llc = ret == 0;
#endif

	if (bufmgr_gem->gen < 4) {
		gp.param = I915_PARAM_NUM_FENCES_AVAIL;
		gp.value = &bufmgr_gem->available_fences;
		ret = drmIoctl(bufmgr_gem->fd, DRM_IOCTL_I915_GETPARAM, &gp);
		if (ret) {
			fprintf(stderr, "get fences failed: %d [%d]\n", ret,
				errno);
			fprintf(stderr, "param: %d, val: %d\n", gp.param,
				*gp.value);
			bufmgr_gem->available_fences = 0;
		} else {
			/* XXX The kernel reports the total number of fences,
			 * including any that may be pinned.
			 *
			 * We presume that there will be at least one pinned
			 * fence for the scanout buffer, but there may be more
			 * than one scanout and the user may be manually
			 * pinning buffers. Let's move to execbuffer2 and
			 * thereby forget the insanity of using fences...
			 */
			bufmgr_gem->available_fences -= 2;
			if (bufmgr_gem->available_fences < 0)
				bufmgr_gem->available_fences = 0;
		}
	}

	/* Let's go with one relocation per every 2 dwords (but round down a bit
	 * since a power of two will mean an extra page allocation for the reloc
	 * buffer).
	 *
	 * Every 4 was too few for the blender benchmark.
	 */
	bufmgr_gem->max_relocs = batch_size / sizeof(uint32_t) / 2 - 2;

	bufmgr_gem->bufmgr.bo_alloc = drm_intel_gem_bo_alloc;
	bufmgr_gem->bufmgr.bo_alloc_for_render =
	    drm_intel_gem_bo_alloc_for_render;
	bufmgr_gem->bufmgr.bo_alloc_tiled = drm_intel_gem_bo_alloc_tiled;
	bufmgr_gem->bufmgr.bo_reference = drm_intel_gem_bo_reference;
	bufmgr_gem->bufmgr.bo_unreference = drm_intel_gem_bo_unreference;
	bufmgr_gem->bufmgr.bo_map = drm_intel_gem_bo_map;
	bufmgr_gem->bufmgr.bo_unmap = drm_intel_gem_bo_unmap;
	bufmgr_gem->bufmgr.bo_subdata = drm_intel_gem_bo_subdata;
	bufmgr_gem->bufmgr.bo_get_subdata = drm_intel_gem_bo_get_subdata;
	bufmgr_gem->bufmgr.bo_wait_rendering = drm_intel_gem_bo_wait_rendering;
	bufmgr_gem->bufmgr.bo_emit_reloc = drm_intel_gem_bo_emit_reloc;
	bufmgr_gem->bufmgr.bo_emit_reloc_fence = drm_intel_gem_bo_emit_reloc_fence;
	bufmgr_gem->bufmgr.bo_pin = drm_intel_gem_bo_pin;
	bufmgr_gem->bufmgr.bo_unpin = drm_intel_gem_bo_unpin;
	bufmgr_gem->bufmgr.bo_get_tiling = drm_intel_gem_bo_get_tiling;
	bufmgr_gem->bufmgr.bo_set_tiling = drm_intel_gem_bo_set_tiling;
	bufmgr_gem->bufmgr.bo_flink = drm_intel_gem_bo_flink;
#ifndef __OpenBSD__
	/*
	 * Only the new one is available on OpenBSD
	 */
	/* Use the new one if available */
	if (exec2) {
#endif
		bufmgr_gem->bufmgr.bo_exec = drm_intel_gem_bo_exec2;
		bufmgr_gem->bufmgr.bo_mrb_exec = drm_intel_gem_bo_mrb_exec2;
#ifndef __OpenBSD__
	} else
		bufmgr_gem->bufmgr.bo_exec = drm_intel_gem_bo_exec;
#endif
	bufmgr_gem->bufmgr.bo_busy = drm_intel_gem_bo_busy;
	bufmgr_gem->bufmgr.bo_madvise = drm_intel_gem_bo_madvise;
	bufmgr_gem->bufmgr.destroy = drm_intel_bufmgr_gem_destroy;
	bufmgr_gem->bufmgr.debug = 0;
	bufmgr_gem->bufmgr.check_aperture_space =
	    drm_intel_gem_check_aperture_space;
	bufmgr_gem->bufmgr.bo_disable_reuse = drm_intel_gem_bo_disable_reuse;
	bufmgr_gem->bufmgr.bo_is_reusable = drm_intel_gem_bo_is_reusable;
	bufmgr_gem->bufmgr.get_pipe_from_crtc_id =
	    drm_intel_gem_get_pipe_from_crtc_id;
	bufmgr_gem->bufmgr.bo_references = drm_intel_gem_bo_references;

	DRMINITLISTHEAD(&bufmgr_gem->named);
	init_cache_buckets(bufmgr_gem);

	DRMINITLISTHEAD(&bufmgr_gem->vma_cache);
	bufmgr_gem->vma_max = -1; /* unlimited by default */

	return &bufmgr_gem->bufmgr;
}
