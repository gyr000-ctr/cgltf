#ifndef CGLTF_MESHOPT_H_INCLUDED__
#define CGLTF_MESHOPT_H_INCLUDED__

/*
 * cgltf_meshopt - decoding layer for KHR_meshopt_compression / EXT_meshopt_compression.
 *
 * This is a thin C-ABI adapter between cgltf (C99) and the meshoptimizer decoder
 * (C++). It implements the cgltf_meshopt_decode_fn hook declared in cgltf.h and
 * owns no accessor parsing of its own: cgltf keeps using cgltf_buffer_view_data()
 * and the existing cgltf_accessor_read_* entry points on the decoded bytes.
 */

#if defined(CGLTF_MESHOPT_HEADER_PATH)
#include CGLTF_MESHOPT_HEADER_PATH
#else
#include "cgltf.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decodes one compressed buffer view.
 *
 * On success *out_decoded is a buffer of stride*count bytes allocated through
 * cgltf's allocator (data->memory); cgltf frees it via cgltf_free(). The
 * returned status distinguishes "decoder can't handle this stream" from
 * "the stream is corrupt".
 */
cgltf_meshopt_decode_status cgltf_meshopt_decode(
		const cgltf_memory_options* memory,
		cgltf_meshopt_compression_mode mode,
		cgltf_meshopt_compression_filter filter,
		cgltf_size stride,
		cgltf_size count,
		const uint8_t* compressed,
		cgltf_size compressed_size,
		void** out_decoded);

/* Strong definition of cgltf's weak hook. */
extern cgltf_meshopt_decode_fn cgltf_meshopt_decode_hook;

#ifdef __cplusplus
}
#endif

#endif /* CGLTF_MESHOPT_H_INCLUDED__ */
