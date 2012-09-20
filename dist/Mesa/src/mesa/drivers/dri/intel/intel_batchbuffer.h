#ifndef INTEL_BATCHBUFFER_H
#define INTEL_BATCHBUFFER_H

#include "main/mtypes.h"

#include "intel_context.h"
#include "intel_bufmgr.h"
#include "intel_reg.h"

#define BATCH_RESERVED 16

void intel_batchbuffer_init(struct intel_context *intel);
void intel_batchbuffer_reset(struct intel_context *intel);
void intel_batchbuffer_free(struct intel_context *intel);

void _intel_batchbuffer_flush(struct intel_context *intel,
			      const char *file, int line);

#define intel_batchbuffer_flush(intel) \
	_intel_batchbuffer_flush(intel, __FILE__, __LINE__)



/* Unlike bmBufferData, this currently requires the buffer be mapped.
 * Consider it a convenience function wrapping multple
 * intel_buffer_dword() calls.
 */
void intel_batchbuffer_data(struct intel_context *intel,
                            const void *data, GLuint bytes, bool is_blit);

GLboolean intel_batchbuffer_emit_reloc(struct intel_context *intel,
                                       drm_intel_bo *buffer,
				       uint32_t read_domains,
				       uint32_t write_domain,
				       uint32_t offset);
GLboolean intel_batchbuffer_emit_reloc_fenced(struct intel_context *intel,
					      drm_intel_bo *buffer,
					      uint32_t read_domains,
					      uint32_t write_domain,
					      uint32_t offset);
void intel_batchbuffer_emit_mi_flush(struct intel_context *intel);
void intel_emit_post_sync_nonzero_flush(struct intel_context *intel);

static INLINE uint32_t float_as_int(float f)
{
   union {
      float f;
      uint32_t d;
   } fi;

   fi.f = f;
   return fi.d;
}

/* Inline functions - might actually be better off with these
 * non-inlined.  Certainly better off switching all command packets to
 * be passed as structs rather than dwords, but that's a little bit of
 * work...
 */
static INLINE GLint
intel_batchbuffer_space(struct intel_context *intel)
{
   return (intel->batch.state_batch_offset - intel->batch.reserved_space) - intel->batch.used*4;
}


static INLINE void
intel_batchbuffer_emit_dword(struct intel_context *intel, GLuint dword)
{
#ifdef DEBUG
   assert(intel_batchbuffer_space(intel) >= 4);
#endif
   intel->batch.map[intel->batch.used++] = dword;
}

static INLINE void
intel_batchbuffer_emit_float(struct intel_context *intel, float f)
{
   intel_batchbuffer_emit_dword(intel, float_as_int(f));
}

static INLINE void
intel_batchbuffer_require_space(struct intel_context *intel,
                                GLuint sz, int is_blit)
{

   if (intel->gen >= 6 &&
       intel->batch.is_blit != is_blit && intel->batch.used) {
      intel_batchbuffer_flush(intel);
   }

   intel->batch.is_blit = is_blit;

#ifdef DEBUG
   assert(sz < sizeof(intel->batch.map) - BATCH_RESERVED);
#endif
   if (intel_batchbuffer_space(intel) < sz)
      intel_batchbuffer_flush(intel);
}

static INLINE void
intel_batchbuffer_begin(struct intel_context *intel, int n, bool is_blit)
{
   intel_batchbuffer_require_space(intel, n * 4, is_blit);

   intel->batch.emit = intel->batch.used;
#ifdef DEBUG
   intel->batch.total = n;
#endif
}

static INLINE void
intel_batchbuffer_advance(struct intel_context *intel)
{
#ifdef DEBUG
   struct intel_batchbuffer *batch = &intel->batch;
   unsigned int _n = batch->used - batch->emit;
   assert(batch->total != 0);
   if (_n != batch->total) {
      fprintf(stderr, "ADVANCE_BATCH: %d of %d dwords emitted\n",
	      _n, batch->total);
      abort();
   }
   batch->total = 0;
#endif
}

void intel_batchbuffer_cached_advance(struct intel_context *intel);

/* Here are the crusty old macros, to be removed:
 */
#define BATCH_LOCALS

#define BEGIN_BATCH(n) intel_batchbuffer_begin(intel, n, false)
#define BEGIN_BATCH_BLT(n) intel_batchbuffer_begin(intel, n, true)
#define OUT_BATCH(d) intel_batchbuffer_emit_dword(intel, d)
#define OUT_BATCH_F(f) intel_batchbuffer_emit_float(intel,f)
#define OUT_RELOC(buf, read_domains, write_domain, delta) do {		\
   intel_batchbuffer_emit_reloc(intel, buf,			\
				read_domains, write_domain, delta);	\
} while (0)
#define OUT_RELOC_FENCED(buf, read_domains, write_domain, delta) do {	\
   intel_batchbuffer_emit_reloc_fenced(intel, buf,		\
				       read_domains, write_domain, delta); \
} while (0)

#define ADVANCE_BATCH() intel_batchbuffer_advance(intel);
#define CACHED_BATCH() intel_batchbuffer_cached_advance(intel);

#endif
