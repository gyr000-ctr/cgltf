/*
 * cgltf_meshopt.cpp - C-ABI adapter implementing cgltf's meshopt decode hook.
 *
 * All actual codec work is delegated to the meshoptimizer library (MIT, vendored
 * alongside this file). The adapter only translates cgltf enums and maps the
 * meshoptimizer integer return codes onto cgltf_meshopt_decode_status values, so
 * corrupt input never reaches the accessor layer.
 */

#include "cgltf_meshopt.h"

#include "meshoptimizer.h"

#include <stddef.h>

extern "C" {

cgltf_meshopt_decode_status cgltf_meshopt_decode(
		const cgltf_memory_options* memory,
		cgltf_meshopt_compression_mode mode,
		cgltf_meshopt_compression_filter filter,
		cgltf_size stride,
		cgltf_size count,
		const uint8_t* compressed,
		cgltf_size compressed_size,
		void** out_decoded)
{
	*out_decoded = NULL;

	if (memory == NULL || memory->alloc_func == NULL || memory->free_func == NULL ||
	    compressed == NULL || stride == 0 || count == 0)
	{
		return cgltf_meshopt_decode_status_unsupported;
	}

	const cgltf_size decoded_size = stride * count; /* cgltf already rejected overflow */
	void* decoded = memory->alloc_func(memory->user_data, decoded_size);
	if (decoded == NULL)
	{
		return cgltf_meshopt_decode_status_out_of_memory;
	}

	int rc = -1;

	switch (mode)
	{
	case cgltf_meshopt_compression_mode_attributes:
		rc = meshopt_decodeVertexBuffer(decoded, count, stride, compressed, compressed_size);
		if (rc == 0)
		{
			switch (filter)
			{
			case cgltf_meshopt_compression_filter_none:
			case cgltf_meshopt_compression_filter_color: /* COLOR only changes normalization; raw bytes pass through */
				break;
			case cgltf_meshopt_compression_filter_octahedral:
				meshopt_decodeFilterOct(decoded, count, stride);
				break;
			case cgltf_meshopt_compression_filter_quaternion:
				meshopt_decodeFilterQuat(decoded, count, stride);
				break;
			case cgltf_meshopt_compression_filter_exponential:
				meshopt_decodeFilterExp(decoded, count, stride);
				break;
			default:
				rc = -1;
				break;
			}
		}
		break;

	case cgltf_meshopt_compression_mode_triangles:
		/* Mode 1 per EXT/KHR_meshopt_compression: meshopt index buffer codec. */
		rc = meshopt_decodeIndexBuffer(decoded, count, stride, compressed, compressed_size);
		break;

	case cgltf_meshopt_compression_mode_indices:
		/* Mode 2: meshopt index sequence codec (no triangle-topology assumptions). */
		rc = meshopt_decodeIndexSequence(decoded, count, stride, compressed, compressed_size);
		break;

	default:
		/* Unknown/unrecognized mode: the decoder cannot handle it, distinct from a bad bitstream. */
		memory->free_func(memory->user_data, decoded);
		return cgltf_meshopt_decode_status_unsupported;
	}

	if (rc != 0)
	{
		memory->free_func(memory->user_data, decoded);
		return cgltf_meshopt_decode_status_corrupt;
	}

	*out_decoded = decoded;
	return cgltf_meshopt_decode_status_ok;
}

cgltf_meshopt_decode_fn cgltf_meshopt_decode_hook = &cgltf_meshopt_decode;

} /* extern "C" */
