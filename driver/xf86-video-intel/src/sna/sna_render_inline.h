#ifndef SNA_RENDER_INLINE_H
#define SNA_RENDER_INLINE_H

static inline bool need_tiling(struct sna *sna, int16_t width, int16_t height)
{
	/* Is the damage area too large to fit in 3D pipeline,
	 * and so do we need to split the operation up into tiles?
	 */
	return (width > sna->render.max_3d_size ||
		height > sna->render.max_3d_size);
}

static inline bool need_redirect(struct sna *sna, PixmapPtr dst)
{
	/* Is the pixmap too large to render to? */
	return (dst->drawable.width > sna->render.max_3d_size ||
		dst->drawable.height > sna->render.max_3d_size);
}

static inline float pack_2s(int16_t x, int16_t y)
{
	union {
		struct sna_coordinate p;
		float f;
	} u;
	u.p.x = x;
	u.p.y = y;
	return u.f;
}

static inline int vertex_space(struct sna *sna)
{
	return sna->render.vertex_size - sna->render.vertex_used;
}
static inline void vertex_emit(struct sna *sna, float v)
{
	assert(sna->render.vertex_used < sna->render.vertex_size);
	sna->render.vertices[sna->render.vertex_used++] = v;
}
static inline void vertex_emit_2s(struct sna *sna, int16_t x, int16_t y)
{
	vertex_emit(sna, pack_2s(x, y));
}

static inline int batch_space(struct sna *sna)
{
	assert(sna->kgem.nbatch <= KGEM_BATCH_SIZE(&sna->kgem));
	assert(sna->kgem.nbatch + KGEM_BATCH_RESERVED <= sna->kgem.surface);
	return sna->kgem.surface - sna->kgem.nbatch - KGEM_BATCH_RESERVED;
}

static inline void batch_emit(struct sna *sna, uint32_t dword)
{
	assert(sna->kgem.mode != KGEM_NONE);
	assert(sna->kgem.nbatch + KGEM_BATCH_RESERVED < sna->kgem.surface);
	sna->kgem.batch[sna->kgem.nbatch++] = dword;
}

static inline void batch_emit_float(struct sna *sna, float f)
{
	union {
		uint32_t dw;
		float f;
	} u;
	u.f = f;
	batch_emit(sna, u.dw);
}

static inline bool
is_gpu(DrawablePtr drawable)
{
	struct sna_pixmap *priv = sna_pixmap_from_drawable(drawable);

	if (priv == NULL || priv->clear || priv->cpu)
		return false;

	if (priv->cpu_damage == NULL)
		return true;

	if (priv->gpu_damage && !priv->gpu_bo->proxy)
		return true;

	if (priv->cpu_bo && kgem_bo_is_busy(priv->cpu_bo))
		return true;

	return priv->gpu_bo && kgem_bo_is_busy(priv->gpu_bo);
}

static inline bool
too_small(struct sna_pixmap *priv)
{
	assert(priv);

	if (priv->gpu_bo)
		return false;

	if (priv->cpu_bo && kgem_bo_is_busy(priv->cpu_bo))
		return false;

	return (priv->create & KGEM_CAN_CREATE_GPU) == 0;
}

static inline bool
unattached(DrawablePtr drawable)
{
	struct sna_pixmap *priv = sna_pixmap_from_drawable(drawable);
	return priv == NULL || (priv->gpu_damage == NULL && priv->cpu_damage);
}

static inline bool
picture_is_gpu(PicturePtr picture)
{
	if (!picture || !picture->pDrawable)
		return false;
	return is_gpu(picture->pDrawable);
}

static inline bool sna_blt_compare_depth(DrawablePtr src, DrawablePtr dst)
{
	if (src->depth == dst->depth)
		return true;

	/* Also allow for the alpha to be discarded on a copy */
	if (src->bitsPerPixel != dst->bitsPerPixel)
		return false;

	if (dst->depth == 24 && src->depth == 32)
		return true;

	/* Note that a depth-16 pixmap is r5g6b5, not x1r5g5b5. */

	return false;
}

static inline struct kgem_bo *
sna_render_get_alpha_gradient(struct sna *sna)
{
	return kgem_bo_reference(sna->render.alpha_cache.cache_bo);
}

static inline void
sna_render_picture_extents(PicturePtr p, BoxRec *box)
{
	box->x1 = p->pDrawable->x;
	box->y1 = p->pDrawable->y;
	box->x2 = bound(box->x1, p->pDrawable->width);
	box->y2 = bound(box->y1, p->pDrawable->height);

	if (box->x1 < p->pCompositeClip->extents.x1)
		box->x1 = p->pCompositeClip->extents.x1;
	if (box->y1 < p->pCompositeClip->extents.y1)
		box->y1 = p->pCompositeClip->extents.y1;

	if (box->x2 > p->pCompositeClip->extents.x2)
		box->x2 = p->pCompositeClip->extents.x2;
	if (box->y2 > p->pCompositeClip->extents.y2)
		box->y2 = p->pCompositeClip->extents.y2;

	assert(box->x2 > box->x1 && box->y2 > box->y1);
}

static inline void
sna_render_reduce_damage(struct sna_composite_op *op,
			 int dst_x, int dst_y,
			 int width, int height)
{
	BoxRec r;

	if (op->damage == NULL || *op->damage == NULL)
		return;

	if (DAMAGE_IS_ALL(*op->damage)) {
		DBG(("%s: damage-all, dicarding damage\n",
		     __FUNCTION__));
		op->damage = NULL;
		return;
	}

	if (width == 0 || height == 0)
		return;

	r.x1 = dst_x + op->dst.x;
	r.x2 = r.x1 + width;

	r.y1 = dst_y + op->dst.y;
	r.y2 = r.y1 + height;

	if (sna_damage_contains_box__no_reduce(*op->damage, &r)) {
		DBG(("%s: damage contains render extents, dicarding damage\n",
		     __FUNCTION__));
		op->damage = NULL;
	}
}

inline static uint32_t
color_convert(uint32_t pixel,
	      uint32_t src_format,
	      uint32_t dst_format)
{
	DBG(("%s: src=%08x [%08x]\n", __FUNCTION__, pixel, src_format));

	if (src_format != dst_format) {
		uint16_t red, green, blue, alpha;

		if (!sna_get_rgba_from_pixel(pixel,
					     &red, &green, &blue, &alpha,
					     src_format))
			return 0;

		if (!sna_get_pixel_from_rgba(&pixel,
					     red, green, blue, alpha,
					     dst_format))
			return 0;
	}

	DBG(("%s: dst=%08x [%08x]\n", __FUNCTION__, pixel, dst_format));
	return pixel;
}

inline static bool dst_use_gpu(PixmapPtr pixmap)
{
	struct sna_pixmap *priv = sna_pixmap(pixmap);
	if (priv == NULL)
		return false;

	if (priv->cpu_bo && kgem_bo_is_busy(priv->cpu_bo))
		return true;

	if (priv->clear)
		return false;

	if (priv->gpu_bo && kgem_bo_is_busy(priv->gpu_bo))
		return true;

	return priv->gpu_damage && (!priv->cpu || !priv->cpu_damage);
}

inline static bool dst_use_cpu(PixmapPtr pixmap)
{
	struct sna_pixmap *priv = sna_pixmap(pixmap);
	if (priv == NULL || priv->shm)
		return true;

	return priv->cpu_damage && priv->cpu;
}

inline static bool dst_is_cpu(PixmapPtr pixmap)
{
	struct sna_pixmap *priv = sna_pixmap(pixmap);
	return priv == NULL || DAMAGE_IS_ALL(priv->cpu_damage);
}

inline static bool
untransformed(PicturePtr p)
{
	return !p->transform || pixman_transform_is_int_translate(p->transform);
}


#endif /* SNA_RENDER_INLINE_H */
