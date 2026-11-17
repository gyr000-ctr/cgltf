/*
 * Self-test for KHR_animation_pointer parsing and validation.
 *
 * Covers:
 *  - valid pointer channels (node TRS, material factors, camera, light, weights)
 *  - raw pointer string retention and resolved target exposure
 *  - malformed / unknown JSON pointer -> cgltf_result_invalid_gltf at parse time
 *  - pointer at a non-existent node -> cgltf_result_invalid_gltf at parse time
 *  - sampler output type mismatch -> cgltf_result_invalid_gltf at validate time
 *  - legacy channels keep parsing identically; array lengths never change
 */

#define CGLTF_IMPLEMENTATION
#define CGLTF_WRITE_IMPLEMENTATION
#include "../cgltf_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_num_checks = 0;
static int g_num_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		++g_num_checks;                                                   \
		if (!(cond))                                                      \
		{                                                                 \
			++g_num_failures;                                             \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
		}                                                                 \
	} while (0)

static const char* gltf_header =
"{"
"  \"asset\": {\"version\": \"2.0\"},"
"  \"extensionsUsed\": [\"KHR_animation_pointer\"],"
"  \"extensions\": {\"KHR_lights_punctual\": {\"lights\": ["
"    {\"name\": \"light0\", \"intensity\": 1.0, \"range\": 10.0, \"color\": [1.0, 1.0, 1.0], \"type\": \"point\"}"
"  ]}},"
"  \"scenes\": [{\"nodes\": [0]}],"
"  \"nodes\": ["
"    {\"name\": \"node0\", \"translation\": [0, 0, 0], \"weights\": [0.0, 0.0], \"mesh\": 0}"
"  ],"
"  \"meshes\": [{"
"    \"weights\": [0.0, 0.0],"
"    \"primitives\": [{\"attributes\": {\"POSITION\": 0}, \"targets\": [{\"POSITION\": 1}, {\"POSITION\": 1}]}]"
"  }],"
"  \"cameras\": [{"
"    \"type\": \"perspective\","
"    \"perspective\": {\"aspectRatio\": 1.77, \"yfov\": 0.8, \"znear\": 0.1, \"zfar\": 100.0}"
"  }],"
"  \"materials\": [{"
"    \"pbrMetallicRoughness\": {\"baseColorFactor\": [1.0, 1.0, 1.0, 1.0], \"metallicFactor\": 1.0, \"roughnessFactor\": 1.0},"
"    \"emissiveFactor\": [0.0, 0.0, 0.0],"
"    \"alphaCutoff\": 0.5,"
"    \"normalTexture\": {\"index\": 0, \"scale\": 1.0},"
"    \"extensions\": {\"KHR_materials_ior\": {\"ior\": 1.5}}"
"  }],"
"  \"textures\": [{\"sampler\": 0, \"source\": 0}],"
"  \"samplers\": [{\"magFilter\": 9729}],"
"  \"images\": [{\"uri\": \"x.png\"}],"
"  \"accessors\": ["
"    {\"componentType\": 5126, \"count\": 1, \"type\": \"VEC3\"},"
"    {\"componentType\": 5126, \"count\": 1, \"type\": \"VEC3\"},"
"    {\"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\"},"
"    {\"componentType\": 5126, \"count\": 1, \"type\": \"SCALAR\"},"
"    {\"componentType\": 5126, \"count\": 2, \"type\": \"VEC3\"},"
"    {\"componentType\": 5126, \"count\": 2, \"type\": \"VEC4\"},"
"    {\"componentType\": 5126, \"count\": 4, \"type\": \"SCALAR\"}"
"  ],";

/* One animation with only legacy TRS/weights channels: baseline compatibility. */
static const char* gltf_legacy =
"  \"animations\": [{"
"    \"samplers\": ["
"      {\"input\": 2, \"output\": 4},"
"      {\"input\": 2, \"output\": 5},"
"      {\"input\": 2, \"output\": 6}"
"    ],"
"    \"channels\": ["
"      {\"sampler\": 0, \"target\": {\"node\": 0, \"path\": \"translation\"}},"
"      {\"sampler\": 1, \"target\": {\"node\": 0, \"path\": \"rotation\"}},"
"      {\"sampler\": 2, \"target\": {\"node\": 0, \"path\": \"weights\"}}"
"    ]"
"  }]"
"}";

/*
 * Builds an animation whose single channel targets `pointer` with an output
 * accessor of `output_type`. output_type values: "SCALAR", "VEC3", "VEC4".
 * The caller may also inject a raw target object fragment via target_extra
 * (e.g. setting "node": 0 to test the node/pointer mutual exclusion).
 */
static void make_pointer_gltf(char* out, cgltf_size out_size, const char* pointer, const char* output_type, const char* target_extra)
{
	/*
	 * Accessor layout (index -> type):
	 *   0 VEC3, 1 VEC3, 2 SCALAR (animation input, 2 keyframes),
	 *   3 <output_type>, 4 VEC3, 5 VEC4
	 * LINEAR output holds one element per keyframe, so count == input count (2).
	 */
	int output_count = 2;

	snprintf(out, out_size,
		"%.*s"
		"  \"accessors\": ["
		"    {\"componentType\": 5126, \"count\": 1, \"type\": \"VEC3\"},"
		"    {\"componentType\": 5126, \"count\": 1, \"type\": \"VEC3\"},"
		"    {\"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\"},"
		"    {\"componentType\": 5126, \"count\": %d, \"type\": \"%s\"},"
		"    {\"componentType\": 5126, \"count\": 1, \"type\": \"VEC3\"},"
		"    {\"componentType\": 5126, \"count\": 1, \"type\": \"VEC4\"}"
		"  ],"
		"  \"animations\": [{"
		"    \"samplers\": [{\"input\": 2, \"output\": 3, \"interpolation\": \"LINEAR\"}],"
		"    \"channels\": [{"
		"      \"sampler\": 0,"
		"      \"target\": {"
		"        \"path\": \"pointer\"%s%s,"
		"        \"extensions\": {\"KHR_animation_pointer\": {\"pointer\": \"%s\"}}"
		"      }"
		"    }]"
		"  }]"
		"}",
		(int)(strstr(gltf_header, "  \"accessors\"") - gltf_header), gltf_header,
		output_count, output_type,
		target_extra ? ", " : "",
		target_extra ? target_extra : "",
		pointer);
}

static cgltf_data* parse_string(const char* json, cgltf_result* parse_result)
{
	cgltf_options options;
	memset(&options, 0, sizeof(options));

	cgltf_data* data = NULL;
	*parse_result = cgltf_parse(&options, json, strlen(json), &data);
	return data;
}

static void test_valid_pointers(void)
{
	struct {
		const char* pointer;
		const char* output_type;
		cgltf_animation_pointer_type expected_type;
		cgltf_type expected_component;
		cgltf_int expected_component_index;
	} cases[] = {
		{"/nodes/0/translation", "VEC3", cgltf_animation_pointer_type_node, cgltf_type_vec3, -1},
		{"/nodes/0/rotation", "VEC4", cgltf_animation_pointer_type_node, cgltf_type_vec4, -1},
		{"/nodes/0/scale", "VEC3", cgltf_animation_pointer_type_node, cgltf_type_vec3, -1},
		{"/nodes/0/weights/1", "SCALAR", cgltf_animation_pointer_type_node, cgltf_type_scalar, 1},
		{"/meshes/0/weights/0", "SCALAR", cgltf_animation_pointer_type_mesh, cgltf_type_scalar, 0},
		{"/materials/0/pbrMetallicRoughness/metallicFactor", "SCALAR", cgltf_animation_pointer_type_material, cgltf_type_scalar, -1},
		{"/materials/0/pbrMetallicRoughness/baseColorFactor/2", "SCALAR", cgltf_animation_pointer_type_material, cgltf_type_scalar, 2},
		{"/materials/0/emissiveFactor/0", "SCALAR", cgltf_animation_pointer_type_material, cgltf_type_scalar, 0},
		{"/materials/0/alphaCutoff", "SCALAR", cgltf_animation_pointer_type_material, cgltf_type_scalar, -1},
		{"/materials/0/normalTexture/scale", "SCALAR", cgltf_animation_pointer_type_material, cgltf_type_scalar, -1},
		{"/materials/0/extensions/KHR_materials_ior/ior", "SCALAR", cgltf_animation_pointer_type_material, cgltf_type_scalar, -1},
		{"/cameras/0/perspective/yfov", "SCALAR", cgltf_animation_pointer_type_camera, cgltf_type_scalar, -1},
		{"/cameras/0/perspective/aspectRatio", "SCALAR", cgltf_animation_pointer_type_camera, cgltf_type_scalar, -1},
		{"/extensions/KHR_lights_punctual/lights/0/intensity", "SCALAR", cgltf_animation_pointer_type_light, cgltf_type_scalar, -1},
		{"/extensions/KHR_lights_punctual/lights/0/color/1", "SCALAR", cgltf_animation_pointer_type_light, cgltf_type_scalar, 1},
	};

	char json[4096];

	for (cgltf_size i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		make_pointer_gltf(json, sizeof(json), cases[i].pointer, cases[i].output_type, NULL);

		cgltf_result parse_result;
		cgltf_data* data = parse_string(json, &parse_result);
		CHECK(parse_result == cgltf_result_success);
		CHECK(data != NULL);
		if (!data)
		{
			continue;
		}

		CHECK(data->animations_count == 1);
		CHECK(data->animations[0].channels_count == 1);
		CHECK(data->animations[0].samplers_count == 1);

		cgltf_animation_channel* channel = &data->animations[0].channels[0];
		CHECK(channel->has_animation_pointer == 1);
		CHECK(channel->target_path == cgltf_animation_path_type_pointer);
		CHECK(channel->target_node == NULL);
		CHECK(channel->animation_pointer.resolved == 1);
		CHECK(channel->animation_pointer.type == cases[i].expected_type);
		CHECK(channel->animation_pointer.component_type == cases[i].expected_component);
		CHECK(channel->animation_pointer.component_index == cases[i].expected_component_index);
		CHECK(strcmp(channel->animation_pointer.pointer, cases[i].pointer) == 0);

		switch (cases[i].expected_type)
		{
		case cgltf_animation_pointer_type_node:
			CHECK(channel->animation_pointer.node == &data->nodes[0]);
			break;
		case cgltf_animation_pointer_type_mesh:
			CHECK(channel->animation_pointer.mesh == &data->meshes[0]);
			break;
		case cgltf_animation_pointer_type_camera:
			CHECK(channel->animation_pointer.camera == &data->cameras[0]);
			break;
		case cgltf_animation_pointer_type_material:
			CHECK(channel->animation_pointer.material == &data->materials[0]);
			break;
		case cgltf_animation_pointer_type_light:
			CHECK(channel->animation_pointer.light == &data->lights[0]);
			break;
		default:
			CHECK(0);
			break;
		}

		cgltf_result validate_result = cgltf_validate(data);
		CHECK(validate_result == cgltf_result_success);

		cgltf_free(data);
	}
}

static void test_parse_failures(void)
{
	/* Malformed pointers and unresolvable targets must fail during parsing. */
	const char* bad_pointers[] = {
		"nodes/0/translation",                 /* missing leading slash */
		"/nodes/0/matrix",                     /* immutable property */
		"/nodes/5/translation",                /* node does not exist */
		"/nodes/0/unknown",                    /* unknown property */
		"/nodes/0/weights/9",                  /* weight component out of range */
		"/meshes/9/weights/0",                 /* mesh does not exist */
		"/materials/9/alphaCutoff",            /* material does not exist */
		"/materials/0/doubleSided",            /* boolean property, not mutable */
		"/materials/0/emissiveFactor",         /* missing component index */
		"/materials/0/emissiveFactor/9",       /* component out of range */
		"/cameras/0/orthographic/xmag",        /* camera type mismatch */
		"/cameras/9/perspective/yfov",         /* camera does not exist */
		"/extensions/KHR_lights_punctual/lights/9/intensity", /* light missing */
		"/skins/0/joints/0",                   /* unsupported root object */
		"/nodes",                              /* too few tokens */
		"/nodes/0/translation/extra",          /* too many tokens */
	};

	char json[4096];

	for (cgltf_size i = 0; i < sizeof(bad_pointers) / sizeof(bad_pointers[0]); ++i)
	{
		make_pointer_gltf(json, sizeof(json), bad_pointers[i], "SCALAR", NULL);

		cgltf_result parse_result;
		cgltf_data* data = parse_string(json, &parse_result);
		CHECK(parse_result == cgltf_result_invalid_gltf);
		CHECK(data == NULL);
	}

	/* "node" must not be combined with KHR_animation_pointer. */
	make_pointer_gltf(json, sizeof(json), "/nodes/0/translation", "VEC3", "\"node\": 0");
	{
		cgltf_result parse_result;
		cgltf_data* data = parse_string(json, &parse_result);
		CHECK(parse_result == cgltf_result_invalid_gltf);
		CHECK(data == NULL);
	}

	/* path "pointer" without the extension is malformed. */
	snprintf(json, sizeof(json),
		"%s"
		"  \"animations\": [{"
		"    \"samplers\": [{\"input\": 2, \"output\": 3}],"
		"    \"channels\": [{\"sampler\": 0, \"target\": {\"path\": \"pointer\"}}]"
		"  }]}",
		gltf_header);
	{
		cgltf_result parse_result;
		cgltf_data* data = parse_string(json, &parse_result);
		CHECK(parse_result == cgltf_result_invalid_gltf);
		CHECK(data == NULL);
	}
}

static void test_type_mismatch(void)
{
	/*
	 * /nodes/0/translation resolves fine (parse succeeds) but requires a VEC3
	 * output; a SCALAR sampler output must fail validation.
	 */
	char json[4096];
	make_pointer_gltf(json, sizeof(json), "/nodes/0/translation", "SCALAR", NULL);

	cgltf_result parse_result;
	cgltf_data* data = parse_string(json, &parse_result);
	CHECK(parse_result == cgltf_result_success);
	CHECK(data != NULL);
	if (data)
	{
		CHECK(data->animations[0].channels[0].animation_pointer.resolved == 1);
		cgltf_result validate_result = cgltf_validate(data);
		CHECK(validate_result == cgltf_result_invalid_gltf);
		cgltf_free(data);
	}

	/* A SCALAR property fed by a VEC3 output fails validation as well. */
	make_pointer_gltf(json, sizeof(json), "/materials/0/alphaCutoff", "VEC3", NULL);
	data = parse_string(json, &parse_result);
	CHECK(parse_result == cgltf_result_success);
	CHECK(data != NULL);
	if (data)
	{
		cgltf_result validate_result = cgltf_validate(data);
		CHECK(validate_result == cgltf_result_invalid_gltf);
		cgltf_free(data);
	}
}

static void test_legacy_compatibility(void)
{
	char json[4096];
	snprintf(json, sizeof(json), "%s%s", gltf_header, gltf_legacy);

	cgltf_result parse_result;
	cgltf_data* data = parse_string(json, &parse_result);
	CHECK(parse_result == cgltf_result_success);
	CHECK(data != NULL);
	if (!data)
	{
		return;
	}

	/* Array lengths are unchanged. */
	CHECK(data->animations_count == 1);
	CHECK(data->animations[0].samplers_count == 3);
	CHECK(data->animations[0].channels_count == 3);

	const cgltf_animation_path_type expected_paths[3] = {
		cgltf_animation_path_type_translation,
		cgltf_animation_path_type_rotation,
		cgltf_animation_path_type_weights
	};

	for (cgltf_size i = 0; i < 3; ++i)
	{
		cgltf_animation_channel* channel = &data->animations[0].channels[i];
		CHECK(channel->has_animation_pointer == 0);
		CHECK(channel->target_path == expected_paths[i]);
		CHECK(channel->target_node == &data->nodes[0]);
		CHECK(channel->animation_pointer.pointer == NULL);
		CHECK(channel->animation_pointer.resolved == 0);
	}

	/* Legacy validation (output count vs weights components) still works. */
	cgltf_result validate_result = cgltf_validate(data);
	CHECK(validate_result == cgltf_result_success);

	cgltf_free(data);
}

/* Escaped reference tokens (~0/~1) must be unescaped before matching. */
static void test_pointer_escaping(void)
{
	/*
	 * "/nodes/0/~1weights~1/0" unescapes to "/nodes/0//weights//0" which does not
	 * match "weights", so it must fail; this proves unescaping actually runs.
	 */
	char json[4096];
	make_pointer_gltf(json, sizeof(json), "/nodes/0/~1weights~1/0", "SCALAR", NULL);

	cgltf_result parse_result;
	cgltf_data* data = parse_string(json, &parse_result);
	CHECK(parse_result == cgltf_result_invalid_gltf);
	CHECK(data == NULL);

	/* A stray '~' not followed by 0/1 is a malformed RFC 6901 token. */
	make_pointer_gltf(json, sizeof(json), "/nodes/0/weig~2hts", "SCALAR", NULL);
	data = parse_string(json, &parse_result);
	CHECK(parse_result == cgltf_result_invalid_gltf);
	CHECK(data == NULL);
}

/* CUBICSPLINE output holds three elements per input keyframe. */
static void test_cubic_spline(void)
{
	/*
	 * Input accessor (index 0) has 2 keyframes; a cubic spline VEC3 output needs
	 * 2 keyframes x 3 (in-tangent/value/out-tangent) = 6 VEC3 elements.
	 */
	const char* json =
		"{"
		"  \"asset\": {\"version\": \"2.0\"},"
		"  \"scenes\": [{\"nodes\": [0]}],"
		"  \"nodes\": [{\"name\": \"node0\", \"translation\": [0, 0, 0]}],"
		"  \"accessors\": ["
		"    {\"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\"},"
		"    {\"componentType\": 5126, \"count\": 6, \"type\": \"VEC3\"}"
		"  ],"
		"  \"animations\": [{"
		"    \"samplers\": [{\"input\": 0, \"output\": 1, \"interpolation\": \"CUBICSPLINE\"}],"
		"    \"channels\": [{\"sampler\": 0, \"target\": {"
		"      \"path\": \"pointer\","
		"      \"extensions\": {\"KHR_animation_pointer\": {\"pointer\": \"/nodes/0/translation\"}}"
		"    }}]"
		"  }]"
		"}";

	cgltf_result parse_result;
	cgltf_data* data = parse_string(json, &parse_result);
	CHECK(parse_result == cgltf_result_success);
	CHECK(data != NULL);
	if (data)
	{
		cgltf_animation_channel* channel = &data->animations[0].channels[0];
		CHECK(channel->sampler->interpolation == cgltf_interpolation_type_cubic_spline);
		CHECK(channel->animation_pointer.component_type == cgltf_type_vec3);
		CHECK(cgltf_validate(data) == cgltf_result_success);
		cgltf_free(data);
	}

	/* Same document but with a LINEAR sampler: 3 output elements vs 2 keyframes fails. */
	const char* json_linear =
		"{"
		"  \"asset\": {\"version\": \"2.0\"},"
		"  \"scenes\": [{\"nodes\": [0]}],"
		"  \"nodes\": [{\"name\": \"node0\", \"translation\": [0, 0, 0]}],"
		"  \"accessors\": ["
		"    {\"componentType\": 5126, \"count\": 2, \"type\": \"SCALAR\"},"
		"    {\"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\"}"
		"  ],"
		"  \"animations\": [{"
		"    \"samplers\": [{\"input\": 0, \"output\": 1, \"interpolation\": \"LINEAR\"}],"
		"    \"channels\": [{\"sampler\": 0, \"target\": {"
		"      \"path\": \"pointer\","
		"      \"extensions\": {\"KHR_animation_pointer\": {\"pointer\": \"/nodes/0/translation\"}}"
		"    }}]"
		"  }]"
		"}";

	data = parse_string(json_linear, &parse_result);
	CHECK(parse_result == cgltf_result_success);
	CHECK(data != NULL);
	if (data)
	{
		CHECK(cgltf_validate(data) == cgltf_result_invalid_gltf);
		cgltf_free(data);
	}
}

/* Writing a parsed pointer document and re-parsing it must preserve the channel. */
static void test_write_roundtrip(void)
{
	char json[4096];
	make_pointer_gltf(json, sizeof(json), "/materials/0/pbrMetallicRoughness/baseColorFactor/2", "SCALAR", NULL);

	cgltf_result parse_result;
	cgltf_data* data = parse_string(json, &parse_result);
	CHECK(parse_result == cgltf_result_success);
	CHECK(data != NULL);
	if (!data)
	{
		return;
	}

	cgltf_options options;
	memset(&options, 0, sizeof(options));

	cgltf_size written_size = cgltf_write(&options, NULL, 0, data);
	CHECK(written_size > 0);

	char* written = (char*)malloc(written_size);
	CHECK(written != NULL);
	cgltf_size actual = cgltf_write(&options, written, written_size, data);
	CHECK(actual == written_size);

	cgltf_data* reparsed = NULL;
	cgltf_result reparse_result = cgltf_parse(&options, written, written_size - 1, &reparsed);
	CHECK(reparse_result == cgltf_result_success);
	CHECK(reparsed != NULL);
	if (reparsed)
	{
		/* Array lengths are preserved through the write/parse round trip. */
		CHECK(reparsed->animations_count == 1);
		CHECK(reparsed->animations[0].channels_count == 1);
		cgltf_animation_channel* channel = &reparsed->animations[0].channels[0];
		CHECK(channel->has_animation_pointer == 1);
		CHECK(channel->target_path == cgltf_animation_path_type_pointer);
		CHECK(channel->target_node == NULL);
		CHECK(channel->animation_pointer.resolved == 1);
		CHECK(channel->animation_pointer.type == cgltf_animation_pointer_type_material);
		CHECK(channel->animation_pointer.component_index == 2);
		CHECK(strcmp(channel->animation_pointer.pointer,
			"/materials/0/pbrMetallicRoughness/baseColorFactor/2") == 0);
		CHECK(cgltf_validate(reparsed) == cgltf_result_success);
		cgltf_free(reparsed);
	}

	free(written);
	cgltf_free(data);
}

int main(void)
{
	test_valid_pointers();
	test_parse_failures();
	test_type_mismatch();
	test_legacy_compatibility();
	test_pointer_escaping();
	test_cubic_spline();
	test_write_roundtrip();

	printf("KHR_animation_pointer tests: %d checks, %d failures\n", g_num_checks, g_num_failures);
	return g_num_failures ? 1 : 0;
}
