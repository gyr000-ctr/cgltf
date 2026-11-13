/* Verification harness for meshopt compression support in cgltf.
 *
 * Loads a glTF through the public cgltf entry points (parse_file ->
 * load_buffers -> validate), then dumps:
 *   1. the decoded bytes of every buffer view, taken from cgltf_buffer_view_data
 *      (the single shared data entry both compressed and plain views flow through);
 *   2. every accessor unpacked through cgltf_accessor_unpack_floats.
 * Prints the numeric cgltf_result so error paths are diagnosable.
 */
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_all(FILE* f, const void* p, size_t n)
{
	return fwrite(p, 1, n, f) == n ? 0 : -1;
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: %s <file.gltf> [dump.bin]\n", argv[0]);
		return 2;
	}

	cgltf_options options;
	memset(&options, 0, sizeof(options));

	cgltf_data* data = NULL;
	cgltf_result result = cgltf_parse_file(&options, argv[1], &data);
	if (result == cgltf_result_success)
		result = cgltf_load_buffers(&options, data, argv[1]);
	if (result == cgltf_result_success)
		result = cgltf_validate(data);

	printf("Result: %d\n", (int)result);

	if (result != cgltf_result_success)
	{
		cgltf_free(data);
		return (int)result;
	}

	if (argc >= 3)
	{
		FILE* f = fopen(argv[2], "wb");
		if (!f) { perror(argv[2]); cgltf_free(data); return 2; }

		/* 1. raw decoded view bytes - byte-exact comparison target */
		for (cgltf_size i = 0; i < data->buffer_views_count; ++i)
		{
			const cgltf_buffer_view* view = &data->buffer_views[i];
			const uint8_t* bytes = cgltf_buffer_view_data(view);
			if (bytes == NULL)
			{
				fprintf(stderr, "view %zu has no data\n", (size_t)i);
				fclose(f);
				cgltf_free(data);
				return 2;
			}
			if (write_all(f, bytes, view->size) != 0) { perror("write"); return 2; }
		}

		/* 2. accessor-level floats, exercising cgltf_accessor_unpack_floats,
		   which itself reads through cgltf_buffer_view_data */
		for (cgltf_size i = 0; i < data->accessors_count; ++i)
		{
			const cgltf_accessor* acc = &data->accessors[i];
			cgltf_size ncomp = cgltf_num_components(acc->type);
			cgltf_size floats = acc->count * ncomp;
			float* block = (float*)malloc(sizeof(float) * floats);
			if (!block) { perror("malloc"); return 2; }

			cgltf_size got = cgltf_accessor_unpack_floats(acc, block, floats);
			if (got != floats) { fprintf(stderr, "unpack failed\n"); free(block); return 2; }
			if (write_all(f, block, sizeof(float) * floats) != 0) { perror("write"); free(block); return 2; }
			free(block);
		}

		fclose(f);
	}

	cgltf_free(data);
	return (int)cgltf_result_success;
}
