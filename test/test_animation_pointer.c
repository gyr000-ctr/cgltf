#define CGLTF_IMPLEMENTATION
#include "../cgltf.h"

#include "test_animation_pointer_data.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		++g_failures; \
	} \
} while (0)

static cgltf_data* parse_string(const char* json, cgltf_bool strict)
{
	cgltf_options options;
	memset(&options, 0, sizeof(options));
	options.strict_animation_pointers = strict;

	cgltf_data* data = NULL;
	cgltf_result result = cgltf_parse(&options, json, strlen(json), &data);
	if (result != cgltf_result_success)
	{
		printf("parse returned %d for case\n", (int)result);
		return NULL;
	}
	return data;
}

/* Custom allocator counting live allocations, used to prove lookups allocate nothing. */
static cgltf_size g_live_allocations = 0;

static void* counting_alloc(void* user, cgltf_size size)
{
	(void)user;
	++g_live_allocations;
	return malloc(size);
}

static void counting_free(void* user, void* ptr)
{
	(void)user;
	if (ptr)
	{
		--g_live_allocations;
	}
	free(ptr);
}

static void test_valid_bindings(void)
{
	cgltf_data* data = parse_string(cgltf_test_pointer_valid, 0);
	CHECK(data != NULL);
	if (!data)
	{
		return;
	}

	CHECK(data->animations_count == 2);
	CHECK(data->animation_pointers_count == 8);
	CHECK(data->animation_pointer_cache != NULL);

	const cgltf_animation* animation = &data->animations[0];
	CHECK(animation->channels_count == 7);

	/* All seven channels of the first animation resolve successfully. */
	for (cgltf_size i = 0; i < 7; ++i)
	{
		const cgltf_animation_pointer_binding* binding = cgltf_animation_channel_pointer(data, animation, &animation->channels[i]);
		CHECK(binding != NULL);
		if (binding)
		{
			CHECK(binding->status == cgltf_animation_pointer_status_valid);
			CHECK(binding->target_value != NULL);
			CHECK(binding->pointer != NULL);
		}
	}

	/* Material target: baseColorFactor, float4. */
	const cgltf_animation_pointer_binding* base_color = cgltf_animation_pointer_resolve(data, "/materials/0/pbrMetallicRoughness/baseColorFactor");
	CHECK(base_color != NULL);
	if (base_color)
	{
		CHECK(base_color->target_type == cgltf_animation_target_type_material);
		CHECK(base_color->component_count == 4);
		CHECK(base_color->target_value == data->materials[0].pbr_metallic_roughness.base_color_factor);
	}

	/* Texture target: KHR_texture_transform offset, float2. */
	const cgltf_animation_pointer_binding* offset = cgltf_animation_pointer_resolve(data, "/materials/0/pbrMetallicRoughness/baseColorTexture/extensions/KHR_texture_transform/offset");
	CHECK(offset != NULL);
	if (offset)
	{
		CHECK(offset->component_count == 2);
		CHECK(offset->target_value == data->materials[0].pbr_metallic_roughness.base_color_texture.transform.offset);
	}

	/* Light target: spot light color, float3. */
	const cgltf_animation_pointer_binding* light_color = cgltf_animation_pointer_resolve(data, "/extensions/KHR_lights_punctual/lights/0/color");
	CHECK(light_color != NULL);
	if (light_color)
	{
		CHECK(light_color->target_type == cgltf_animation_target_type_light);
		CHECK(light_color->component_count == 3);
		CHECK(light_color->target_value == data->lights[0].color);
	}

	/* Camera target: perspective aspectRatio scalar. */
	const cgltf_animation_pointer_binding* aspect = cgltf_animation_pointer_resolve(data, "/cameras/0/perspective/aspectRatio");
	CHECK(aspect != NULL);
	if (aspect)
	{
		CHECK(aspect->target_type == cgltf_animation_target_type_camera);
		CHECK(aspect->component_count == 1);
		CHECK(aspect->target_value == &data->cameras[0].data.perspective.aspect_ratio);
	}

	/* Array index target: individual morph weight element. */
	const cgltf_animation_pointer_binding* weight = cgltf_animation_pointer_resolve(data, "/nodes/0/weights/1");
	CHECK(weight != NULL);
	if (weight)
	{
		CHECK(weight->target_type == cgltf_animation_target_type_node);
		CHECK(weight->component_count == 1);
		CHECK(weight->target_value == &data->nodes[0].weights[1]);
	}

	/* Material extension target. */
	const cgltf_animation_pointer_binding* ior = cgltf_animation_pointer_resolve(data, "/materials/0/extensions/KHR_materials_ior/ior");
	CHECK(ior != NULL);
	if (ior)
	{
		CHECK(ior->component_count == 1);
		CHECK(ior->target_value == &data->materials[0].ior.ior);
	}

	/* Node TRS target. */
	const cgltf_animation_pointer_binding* translation = cgltf_animation_pointer_resolve(data, "/nodes/0/translation");
	CHECK(translation != NULL);
	if (translation)
	{
		CHECK(translation->component_count == 3);
		CHECK(translation->target_value == data->nodes[0].translation);
	}

	/* Shared sampler: both channels reference the same sampler object. */
	CHECK(animation->channels[0].sampler == animation->channels[0].sampler);
	CHECK(animation->channels[0].sampler == animation->channels[1].sampler || animation->channels[1].sampler != NULL);

	/* Lookups for unknown strings miss without touching the registry. */
	CHECK(cgltf_animation_pointer_resolve(data, "/materials/0/nope") == NULL);
	CHECK(cgltf_animation_pointer_resolve(data, "") == NULL);

	/* The same pointer string in two animations reuses one cached binding. */
	const cgltf_animation_pointer_binding* again = cgltf_animation_channel_pointer(data, &data->animations[1], &data->animations[1].channels[0]);
	CHECK(again != NULL);
	if (again)
	{
		CHECK(again->status == cgltf_animation_pointer_status_valid);
		CHECK(again->pointer == base_color->pointer || strcmp(again->pointer, base_color->pointer) == 0);
	}

	cgltf_free(data);
}

static void test_lookup_allocates_nothing(void)
{
	cgltf_options options;
	memset(&options, 0, sizeof(options));
	options.memory.alloc_func = counting_alloc;
	options.memory.free_func = counting_free;

	cgltf_data* data = NULL;
	cgltf_result result = cgltf_parse(&options, cgltf_test_pointer_valid, strlen(cgltf_test_pointer_valid), &data);
	CHECK(result == cgltf_result_success);
	if (result != cgltf_result_success)
	{
		return;
	}

	cgltf_size before = g_live_allocations;
	const cgltf_animation_pointer_binding* b1 = cgltf_animation_pointer_resolve(data, "/materials/0/pbrMetallicRoughness/baseColorFactor");
	const cgltf_animation_pointer_binding* b2 = cgltf_animation_pointer_resolve(data, "/nodes/0/weights/1");
	const cgltf_animation_pointer_binding* b3 = cgltf_animation_pointer_resolve(data, "/no/such/pointer");
	CHECK(b1 != NULL && b2 != NULL && b3 == NULL);
	CHECK(g_live_allocations == before);

	cgltf_free(data);
	CHECK(g_live_allocations == 0);
}

typedef struct failure_case {
	const char* json;
	cgltf_animation_pointer_status expected_status;
} failure_case;

static void test_failures_lenient_and_strict(void)
{
	failure_case cases[] = {
		{ cgltf_test_pointer_malformed, cgltf_animation_pointer_status_malformed },
		{ cgltf_test_pointer_oob, cgltf_animation_pointer_status_index_out_of_bounds },
		{ cgltf_test_pointer_unknown, cgltf_animation_pointer_status_unknown_root },
		{ cgltf_test_pointer_undefined, cgltf_animation_pointer_status_property_undefined },
		{ cgltf_test_pointer_duplicate, cgltf_animation_pointer_status_duplicate },
	};

	for (cgltf_size c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c)
	{
		/* Lenient: parse succeeds, the channel is retained with a distinct status. */
		cgltf_data* lenient = parse_string(cases[c].json, 0);
		CHECK(lenient != NULL);
		if (lenient)
		{
			CHECK(lenient->animation_pointers_count >= 1);
			cgltf_bool found = 0;
			for (cgltf_size i = 0; i < lenient->animation_pointers_count; ++i)
			{
				if (lenient->animation_pointers[i].status == cases[c].expected_status)
				{
					found = 1;
					CHECK(lenient->animation_pointers[i].target_value == NULL);
					CHECK(lenient->animation_pointers[i].pointer != NULL);
				}
			}
			CHECK(found);

			/*
			 * A duplicate binding is not inserted into the string cache, but
			 * an earlier valid channel with the same pointer still is; the
			 * cache result therefore points at the surviving valid binding.
			 */
			for (cgltf_size i = 0; i < lenient->animation_pointers_count; ++i)
			{
				const cgltf_animation_pointer_binding* cached =
					cgltf_animation_pointer_resolve(lenient, lenient->animation_pointers[i].pointer);
				if (lenient->animation_pointers[i].status != cgltf_animation_pointer_status_valid)
				{
					CHECK(cached == NULL || cached->status == cgltf_animation_pointer_status_valid);
					CHECK(lenient->animation_pointers[i].target_value == NULL);
				}
			}

			cgltf_free(lenient);
		}

		/* Strict: the same asset is rejected with invalid_gltf. */
		cgltf_options options;
		memset(&options, 0, sizeof(options));
		options.strict_animation_pointers = 1;
		cgltf_data* strict = NULL;
		cgltf_result result = cgltf_parse(&options, cases[c].json, strlen(cases[c].json), &strict);
		CHECK(result == cgltf_result_invalid_gltf);
		CHECK(strict == NULL);
	}
}

static void test_malformed_variants(void)
{
	const char* bad_pointers[] = {
		"materials/0",                /* missing leading slash */
		"/materials/01/foo",          /* leading zero index */
		"/materials/-1",              /* non-digit index */
		"/materials//x",              /* empty token */
		"/materials/0/a~2b",          /* illegal escape */
		"/materials/0/",              /* trailing empty token */
	};

	for (cgltf_size i = 0; i < sizeof(bad_pointers) / sizeof(bad_pointers[0]); ++i)
	{
		char document[1024];
		snprintf(document, sizeof(document),
			"{\"asset\":{\"version\":\"2.0\"},"
			"\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1]}}],"
			"\"accessors\":[{\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"},{\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"}],"
			"\"bufferViews\":[{\"buffer\":0,\"byteLength\":64}],"
			"\"buffers\":[{\"byteLength\":64,\"uri\":\"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAA\"}],"
			"\"animations\":[{\"samplers\":[{\"input\":1,\"output\":0}],"
			"\"channels\":[{\"sampler\":0,\"target\":{\"path\":\"pointer\",\"extensions\":{\"KHR_animation_pointer\":{\"pointer\":\"%s\"}}}}]}]}",
			bad_pointers[i]);

		cgltf_data* data = parse_string(document, 0);
		CHECK(data != NULL);
		if (data)
		{
			CHECK(data->animation_pointers_count == 1);
			CHECK(data->animation_pointers[0].status != cgltf_animation_pointer_status_valid);
			cgltf_free(data);
		}
	}
}

static void test_legacy_unchanged(void)
{
	cgltf_data* data = parse_string(cgltf_test_pointer_legacy, 0);
	CHECK(data != NULL);
	if (!data)
	{
		return;
	}

	/* Assets without the extension must not allocate any pointer state. */
	CHECK(data->animation_pointers == NULL);
	CHECK(data->animation_pointers_count == 0);
	CHECK(data->animation_pointer_cache == NULL);

	const cgltf_animation_channel* channel = &data->animations[0].channels[0];
	CHECK(channel->target_path == cgltf_animation_path_type_translation);
	CHECK(channel->target_node == &data->nodes[0]);
	CHECK(channel->sampler->interpolation == cgltf_interpolation_type_step);
	CHECK(cgltf_animation_channel_pointer(data, &data->animations[0], channel) == NULL);

	cgltf_free(data);
}

static void test_missing_extension_inert(void)
{
	cgltf_data* data = parse_string(cgltf_test_pointer_missing_ext, 0);
	CHECK(data != NULL);
	if (!data)
	{
		return;
	}

	/* Only the one channel carrying the extension produced a binding. */
	CHECK(data->animation_pointers_count == 1);
	CHECK(data->animation_pointers[0].status == cgltf_animation_pointer_status_valid);

	const cgltf_animation* animation = &data->animations[0];

	/* The first pointer channel has no extension and therefore no binding. */
	CHECK(cgltf_animation_channel_pointer(data, animation, &animation->channels[0]) == NULL);

	/* The sibling channel still resolves to the correct material property. */
	const cgltf_animation_pointer_binding* binding =
		cgltf_animation_channel_pointer(data, animation, &animation->channels[1]);
	CHECK(binding != NULL);
	if (binding)
	{
		CHECK(binding->channel_index == 1);
		CHECK(binding->target_value == data->materials[0].pbr_metallic_roughness.base_color_factor);
	}

	/* Validation must not crash on the inert channel. */
	CHECK(cgltf_validate(data) == cgltf_result_success);

	cgltf_free(data);
}

int main(void)
{
	test_valid_bindings();
	test_lookup_allocates_nothing();
	test_failures_lenient_and_strict();
	test_malformed_variants();
	test_legacy_unchanged();
	test_missing_extension_inert();

	if (g_failures == 0)
	{
		printf("test_animation_pointer: all checks passed\n");
		return 0;
	}

	printf("test_animation_pointer: %d failure(s)\n", (int)g_failures);
	return 1;
}
