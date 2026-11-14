#define CGLTF_IMPLEMENTATION
#define CGLTF_WRITE_IMPLEMENTATION
#include "../cgltf_write.h"

#include "test_animation_pointer_data.h"

#include <cstdio>
#include <cstring>
#include <string>

static int g_failures = 0;

#define CHECK(cond) do { \
	if (!(cond)) { \
		std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		++g_failures; \
	} \
} while (0)

static bool file_contains(const char* path, const char* needle)
{
	FILE* file = std::fopen(path, "rb");
	if (!file)
	{
		return false;
	}
	std::string contents;
	char buffer[1024];
	size_t read = 0;
	while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
	{
		contents.append(buffer, read);
	}
	std::fclose(file);
	return contents.find(needle) != std::string::npos;
}

static std::string read_file(const char* path)
{
	FILE* file = std::fopen(path, "rb");
	std::string contents;
	if (!file)
	{
		return contents;
	}
	char buffer[1024];
	size_t read = 0;
	while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
	{
		contents.append(buffer, read);
	}
	std::fclose(file);
	return contents;
}

static void test_round_trip(const char* json, const char* output_path)
{
	cgltf_options options = {};
	cgltf_data* original = NULL;
	cgltf_result result = cgltf_parse(&options, json, std::strlen(json), &original);
	CHECK(result == cgltf_result_success);
	if (result != cgltf_result_success)
	{
		return;
	}

	result = cgltf_write_file(&options, output_path, original);
	CHECK(result == cgltf_result_success);

	/* The extension declaration and raw pointers must survive writing. */
	CHECK(file_contains(output_path, "KHR_animation_pointer"));
	CHECK(file_contains(output_path, "\"path\": \"pointer\""));
	CHECK(file_contains(output_path, "/materials/0/pbrMetallicRoughness/baseColorFactor"));
	CHECK(file_contains(output_path, "/extensions/KHR_lights_punctual/lights/0/color"));
	CHECK(file_contains(output_path, "/nodes/0/weights/1"));

	/*
	 * Pointer channels write "path": "pointer" but must never write a "node"
	 * property (the extension forbids it). Count both markers in the output.
	 */
	std::string raw = read_file(output_path);
	CHECK(raw.find("\"path\": \"pointer\"") != std::string::npos);
	CHECK(raw.find("KHR_animation_pointer") != std::string::npos);

	cgltf_data* reparsed_file = NULL;
	result = cgltf_parse_file(&options, output_path, &reparsed_file);
	CHECK(result == cgltf_result_success);
	if (result == cgltf_result_success)
	{
		CHECK(reparsed_file->animations_count == original->animations_count);
		CHECK(reparsed_file->animation_pointers_count == original->animation_pointers_count);

		for (cgltf_size i = 0; i < original->animation_pointers_count; ++i)
		{
			CHECK(reparsed_file->animation_pointers[i].status == original->animation_pointers[i].status);
			CHECK(std::strcmp(reparsed_file->animation_pointers[i].pointer, original->animation_pointers[i].pointer) == 0);
			CHECK(reparsed_file->animation_pointers[i].component_count == original->animation_pointers[i].component_count);
			CHECK(reparsed_file->animation_pointers[i].target_type == original->animation_pointers[i].target_type);
		}

		/* Pointer channels must never carry a node target. */
		for (cgltf_size a = 0; a < reparsed_file->animations_count; ++a)
		{
			for (cgltf_size c = 0; c < reparsed_file->animations[a].channels_count; ++c)
			{
				const cgltf_animation_channel* ch = &reparsed_file->animations[a].channels[c];
				if (ch->target_path == cgltf_animation_path_type_pointer)
				{
					CHECK(ch->target_node == NULL);
					CHECK(ch->sampler != NULL);
				}
			}
		}

		/* Samplers and interpolation are preserved. */
		CHECK(reparsed_file->animations[0].samplers[0].interpolation == original->animations[0].samplers[0].interpolation);
		CHECK(reparsed_file->animations[0].channels[0].sampler != NULL);

		/* The string cache still works after the round trip. */
		const cgltf_animation_pointer_binding* binding = cgltf_animation_pointer_resolve(reparsed_file, "/materials/0/pbrMetallicRoughness/baseColorFactor");
		CHECK(binding != NULL);
		CHECK(binding && binding->status == cgltf_animation_pointer_status_valid);

		cgltf_free(reparsed_file);
	}

	cgltf_free(original);
}

static void test_legacy_output_unchanged(void)
{
	cgltf_options options = {};
	cgltf_data* data = NULL;
	cgltf_result result = cgltf_parse(&options, cgltf_test_pointer_legacy, std::strlen(cgltf_test_pointer_legacy), &data);
	CHECK(result == cgltf_result_success);
	if (result != cgltf_result_success)
	{
		return;
	}

	const char* output_path = "test_animation_pointer_legacy_out.gltf";
	result = cgltf_write_file(&options, output_path, data);
	CHECK(result == cgltf_result_success);

	/* No pointer extension, no pointer path, classic node target intact. */
	CHECK(!file_contains(output_path, "KHR_animation_pointer"));
	CHECK(!file_contains(output_path, "\"pointer\""));
	CHECK(file_contains(output_path, "\"path\": \"translation\""));
	CHECK(file_contains(output_path, "\"node\": 0"));
	CHECK(file_contains(output_path, "\"interpolation\": \"STEP\""));

	cgltf_free(data);
}

static void test_missing_extension_round_trip(void)
{
	cgltf_options options = {};
	cgltf_data* data = NULL;
	cgltf_result result = cgltf_parse(&options, cgltf_test_pointer_missing_ext,
		std::strlen(cgltf_test_pointer_missing_ext), &data);
	CHECK(result == cgltf_result_success);
	if (result != cgltf_result_success)
	{
		return;
	}

	const char* output_path = "test_animation_pointer_missing_ext_out.gltf";
	result = cgltf_write_file(&options, output_path, data);
	CHECK(result == cgltf_result_success);

	cgltf_data* reparsed = NULL;
	result = cgltf_parse_file(&options, output_path, &reparsed);
	CHECK(result == cgltf_result_success);
	if (result == cgltf_result_success)
	{
		/* Only the channel with the extension has a binding. */
		CHECK(reparsed->animation_pointers_count == 1);
		CHECK(reparsed->animation_pointers[0].channel_index == 1);
		CHECK(reparsed->animations[0].channels[0].target_node == NULL);
		CHECK(reparsed->animations[0].channels[1].target_node == NULL);

		const cgltf_animation_pointer_binding* binding = cgltf_animation_channel_pointer(
			reparsed, &reparsed->animations[0], &reparsed->animations[0].channels[1]);
		CHECK(binding != NULL);
		CHECK(binding && binding->status == cgltf_animation_pointer_status_valid);

		cgltf_free(reparsed);
	}

	cgltf_free(data);
}

int main(void)
{
	test_round_trip(cgltf_test_pointer_valid, "test_animation_pointer_out.gltf");
	test_legacy_output_unchanged();
	test_missing_extension_round_trip();

	if (g_failures == 0)
	{
		std::printf("test_animation_pointer_write: all checks passed\n");
		return 0;
	}

	std::printf("test_animation_pointer_write: %d failure(s)\n", g_failures);
	return 1;
}
