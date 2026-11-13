// test_draco.cpp - tests for the optional KHR_draco_mesh_compression decoding support in cgltf.
//
// These tests deliberately do NOT link against a Draco decoder library: the decoding
// pipeline in cgltf (attribute mapping, index validation, view construction, caching,
// memory ownership and error reporting) is exercised through a mock decoder callback.
// This file also doubles as the "no dependency" build test: it must compile and pass
// without any Draco headers or libraries present.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CGLTF_IMPLEMENTATION
#include "../cgltf.h"

static int failures = 0;

#define CHECK(cond) do { \
		if (!(cond)) { \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++failures; \
		} \
	} while (0)

/* ------------------------------------------------------------------------- */
/* Tracked allocator: counts live bytes so we can verify leak-freedom and    */
/* the peak-memory bound of on-demand decoding.                              */
/* ------------------------------------------------------------------------- */

struct MemTracker
{
	cgltf_size current;
	cgltf_size peak;
	cgltf_size mark;
	bool recording;
};

static void* tracked_alloc(void* user, cgltf_size size)
{
	MemTracker* t = (MemTracker*)user;
	size_t* raw = (size_t*)std::malloc(sizeof(size_t) + (size_t)(size ? size : 1));
	if (!raw)
		return NULL;
	raw[0] = (size_t)size;
	t->current += size;
	if (t->recording && t->current > t->peak)
		t->peak = t->current;
	return raw + 1;
}

static void tracked_free(void* user, void* ptr)
{
	MemTracker* t = (MemTracker*)user;
	if (!ptr)
		return;
	size_t* raw = (size_t*)ptr - 1;
	t->current -= raw[0];
	std::free(raw);
}

/* ------------------------------------------------------------------------- */
/* Mock decoder                                                              */
/* ------------------------------------------------------------------------- */

struct MockAttr
{
	cgltf_attribute_type type;
	cgltf_size index;
	cgltf_component_type component;
	cgltf_type accessor_type;
	cgltf_size count;
	std::vector<uint8_t> bytes;
};

struct MockDecoder
{
	cgltf_draco_result result; /* returned immediately if not success */
	std::vector<MockAttr> attrs;
	bool has_indices;
	cgltf_component_type indices_component;
	cgltf_size indices_count;
	std::vector<uint8_t> indices_bytes;

	int call_count;
	const void* last_data;
	cgltf_size last_size;

	MockDecoder() : result(cgltf_draco_result_success), has_indices(false),
		indices_component(cgltf_component_type_r_32u), indices_count(0),
		call_count(0), last_data(NULL), last_size(0) {}
};

static MockAttr make_float_attr(cgltf_attribute_type type, cgltf_size index, cgltf_type accessor_type, const float* values, cgltf_size value_count)
{
	MockAttr a;
	a.type = type;
	a.index = index;
	a.component = cgltf_component_type_r_32f;
	a.accessor_type = accessor_type;
	cgltf_size comps = cgltf_num_components(accessor_type);
	a.count = comps ? value_count / comps : 0;
	a.bytes.resize(value_count * sizeof(float));
	std::memcpy(a.bytes.data(), values, a.bytes.size());
	return a;
}

static MockAttr make_uint16_attr(cgltf_attribute_type type, cgltf_size index, cgltf_type accessor_type, const uint16_t* values, cgltf_size value_count)
{
	MockAttr a;
	a.type = type;
	a.index = index;
	a.component = cgltf_component_type_r_16u;
	a.accessor_type = accessor_type;
	cgltf_size comps = cgltf_num_components(accessor_type);
	a.count = comps ? value_count / comps : 0;
	a.bytes.resize(value_count * sizeof(uint16_t));
	std::memcpy(a.bytes.data(), values, a.bytes.size());
	return a;
}

template <typename T>
static void set_indices(MockDecoder* dec, cgltf_component_type component, const T* indices, cgltf_size count)
{
	dec->has_indices = true;
	dec->indices_component = component;
	dec->indices_count = count;
	dec->indices_bytes.resize(count * sizeof(T));
	std::memcpy(dec->indices_bytes.data(), indices, dec->indices_bytes.size());
}

static cgltf_draco_result mock_decode(void* user_data, const cgltf_data* data, const cgltf_primitive* primitive, const void* compressed_data, cgltf_size compressed_size, cgltf_draco_mesh* mesh)
{
	MockDecoder* dec = (MockDecoder*)user_data;
	dec->call_count++;
	dec->last_data = compressed_data;
	dec->last_size = compressed_size;
	(void)data;
	(void)primitive;

	if (dec->result != cgltf_draco_result_success)
		return dec->result;

	for (size_t i = 0; i < dec->attrs.size(); ++i)
	{
		MockAttr& a = dec->attrs[i];
		cgltf_draco_result r = cgltf_draco_mesh_add_attribute(mesh, a.type, a.index, a.component, a.accessor_type, a.count, a.bytes.empty() ? NULL : a.bytes.data());
		if (r != cgltf_draco_result_success)
			return r;
	}

	if (dec->has_indices)
	{
		cgltf_draco_result r = cgltf_draco_mesh_set_indices(mesh, dec->indices_component, dec->indices_count, dec->indices_bytes.empty() ? NULL : dec->indices_bytes.data());
		if (r != cgltf_draco_result_success)
			return r;
	}

	return cgltf_draco_result_success;
}

/* ------------------------------------------------------------------------- */
/* Test asset: one buffer shared by several Draco-compressed primitives.     */
/* ------------------------------------------------------------------------- */

static std::string make_gltf(bool with_buffer_uri)
{
	/* 48 zero bytes, base64-encoded (content is opaque to the mock decoder) */
	std::string b64(64, 'A');

	std::string json = "{"
		"\"asset\":{\"version\":\"2.0\"},"
		"\"extensionsUsed\":[\"KHR_draco_mesh_compression\"],"
		"\"accessors\":["
			"{\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"  /* 0: POSITION, prim0 */
			"{\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"  /* 1: TEXCOORD_0, prim0 */
			"{\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"  /* 2: POSITION, prim1 */
			"{\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"  /* 3: POSITION, prim2 */
			"{\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}"   /* 4: POSITION, prim3 */
		"],"
		"\"bufferViews\":["
			"{\"buffer\":0,\"byteOffset\":0,\"byteLength\":16},"
			"{\"buffer\":0,\"byteOffset\":16,\"byteLength\":16},"
			"{\"buffer\":0,\"byteOffset\":32,\"byteLength\":32}"       /* 2: out of bounds */
		"],"
		"\"buffers\":[{\"byteLength\":48";
	if (with_buffer_uri)
		json += ",\"uri\":\"data:application/octet-stream;base64," + b64 + "\"";
	json += "}],"
		"\"meshes\":["
			"{\"primitives\":["
				/* prim0: POSITION + TEXCOORD_0, declared placeholder accessors + draco mapping */
				"{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1},\"mode\":4,\"extensions\":{\"KHR_draco_mesh_compression\":{\"bufferView\":0,\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1}}}},"
				/* prim1: POSITION only */
				"{\"attributes\":{\"POSITION\":2},\"mode\":4,\"extensions\":{\"KHR_draco_mesh_compression\":{\"bufferView\":1,\"attributes\":{\"POSITION\":2}}}}"
			"]},"
			"{\"primitives\":["
				/* prim2: shares buffer view 0 with prim0 */
				"{\"attributes\":{\"POSITION\":3},\"mode\":4,\"extensions\":{\"KHR_draco_mesh_compression\":{\"bufferView\":0,\"attributes\":{\"POSITION\":3}}}}"
			"]},"
			"{\"primitives\":["
				"{\"attributes\":{}}"
			"]},"
			"{\"primitives\":["
				/* prim3: buffer view range is out of bounds */
				"{\"attributes\":{\"POSITION\":4},\"mode\":4,\"extensions\":{\"KHR_draco_mesh_compression\":{\"bufferView\":2,\"attributes\":{\"POSITION\":4}}}}"
			"]},"
			"{\"primitives\":["
				/* prim4: no declared attributes; the extension mapping is used as fallback */
				"{\"attributes\":{},\"mode\":4,\"extensions\":{\"KHR_draco_mesh_compression\":{\"bufferView\":1,\"attributes\":{\"POSITION\":4}}}}"
			"]}"
		"]}";
	return json;
}

struct Fixture
{
	MemTracker tracker;
	cgltf_options options;
	std::string json; /* cgltf_parse does not copy the input; keep it alive */
	cgltf_data* data;

	Fixture(bool with_buffer_uri, bool load_buffers) : data(NULL)
	{
		std::memset(&tracker, 0, sizeof(tracker));
		std::memset(&options, 0, sizeof(options));
		options.memory.alloc_func = tracked_alloc;
		options.memory.free_func = tracked_free;
		options.memory.user_data = &tracker;

		json = make_gltf(with_buffer_uri);
		cgltf_result r = cgltf_parse(&options, json.data(), json.size(), &data);
		CHECK(r == cgltf_result_success);
		if (data && load_buffers)
		{
			r = cgltf_load_buffers(&options, data, NULL);
			CHECK(r == cgltf_result_success);
		}
	}

	~Fixture()
	{
		if (data)
			cgltf_free(data);
		/* every allocation made through cgltf must be released by cgltf_free */
		CHECK(tracker.current == 0);
	}

	cgltf_primitive* prim(cgltf_size mesh, cgltf_size index)
	{
		return &data->meshes[mesh].primitives[index];
	}
};

static cgltf_draco_options make_options(MockDecoder* dec)
{
	cgltf_draco_options opts;
	opts.decode_func = mock_decode;
	opts.user_data = dec;
	return opts;
}

/* ------------------------------------------------------------------------- */

static void test_unsupported_without_decoder()
{
	Fixture f(true, true);
	cgltf_draco_options empty;
	empty.decode_func = NULL;
	empty.user_data = NULL;
	CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &empty) == cgltf_draco_result_unsupported);
	CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), NULL) == cgltf_draco_result_unsupported);
	/* failed decode must not have changed the primitive */
	CHECK(f.prim(0, 0)->attributes_count == 2);
	CHECK(f.prim(0, 0)->attributes[0].data == &f.data->accessors[0]);
}

static void test_not_compressed()
{
	Fixture f(true, true);
	MockDecoder dec;
	cgltf_draco_options opts = make_options(&dec);
	CHECK(cgltf_decode_draco_primitive(f.data, f.prim(2, 0), &opts) == cgltf_draco_result_not_compressed);
	CHECK(dec.call_count == 0);
}

static void test_invalid_arguments()
{
	Fixture f(true, true);
	MockDecoder dec;
	cgltf_draco_options opts = make_options(&dec);
	CHECK(cgltf_decode_draco_primitive(NULL, f.prim(0, 0), &opts) == cgltf_draco_result_invalid_arguments);
	CHECK(cgltf_decode_draco_primitive(f.data, NULL, &opts) == cgltf_draco_result_invalid_arguments);
	/* buffer view range exceeds the buffer size */
	CHECK(cgltf_decode_draco_primitive(f.data, f.prim(3, 0), &opts) == cgltf_draco_result_invalid_arguments);
	CHECK(dec.call_count == 0);
}

static void test_buffer_not_loaded()
{
	Fixture f(false, false); /* buffer without uri: data never becomes available */
	MockDecoder dec;
	cgltf_draco_options opts = make_options(&dec);
	CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts) == cgltf_draco_result_buffer_not_loaded);
	CHECK(dec.call_count == 0);
}

static void test_decode_attributes_and_indices()
{
	Fixture f(true, true);

	static const float positions[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
	static const float texcoords[] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
	static const uint32_t indices[] = {0, 1, 2};

	MockDecoder dec;
	dec.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions, 9));
	dec.attrs.push_back(make_float_attr(cgltf_attribute_type_texcoord, 0, cgltf_type_vec2, texcoords, 6));
	set_indices(&dec, cgltf_component_type_r_32u, indices, 3);

	cgltf_draco_options opts = make_options(&dec);
	cgltf_primitive* prim = f.prim(0, 0);

	CHECK(cgltf_decode_draco_primitive(f.data, prim, &opts) == cgltf_draco_result_success);
	CHECK(dec.call_count == 1);
	CHECK(dec.last_size == 16); /* size of buffer view 0 */
	CHECK(dec.last_data == (const uint8_t*)f.data->buffers[0].data + 0);

	/* the primitive now exposes decoded accessors */
	CHECK(prim->attributes_count == 2);
	CHECK(prim->attributes[0].type == cgltf_attribute_type_position);
	CHECK(prim->attributes[1].type == cgltf_attribute_type_texcoord);
	CHECK(prim->attributes[0].name && std::strcmp(prim->attributes[0].name, "POSITION") == 0);

	const cgltf_accessor* pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
	const cgltf_accessor* tex = cgltf_find_accessor(prim, cgltf_attribute_type_texcoord, 0);
	CHECK(pos && pos->count == 3 && pos->type == cgltf_type_vec3 && pos->component_type == cgltf_component_type_r_32f);
	CHECK(tex && tex->count == 3 && tex->type == cgltf_type_vec2);
	/* the decoded accessors are new views, not the placeholder accessors */
	CHECK(pos != &f.data->accessors[0] && tex != &f.data->accessors[1]);

	cgltf_float v[4];
	CHECK(cgltf_accessor_read_float(pos, 1, v, 3) && v[0] == 4 && v[1] == 5 && v[2] == 6);
	CHECK(cgltf_accessor_read_float(tex, 2, v, 2) && v[0] == 0.5f && v[1] == 0.6f);

	cgltf_float unpacked[9];
	CHECK(cgltf_accessor_unpack_floats(pos, unpacked, 9) == 9);
	CHECK(unpacked[0] == 1 && unpacked[4] == 5 && unpacked[8] == 9);

	CHECK(prim->indices && prim->indices->count == 3);
	CHECK(cgltf_accessor_read_index(prim->indices, 0) == 0);
	CHECK(cgltf_accessor_read_index(prim->indices, 2) == 2);

	/* decoded views are backed by a readable buffer */
	CHECK(pos->buffer_view && pos->buffer_view->buffer && pos->buffer_view->buffer->data);
	CHECK(cgltf_buffer_view_data(pos->buffer_view) != NULL);

	/* decoding is cached: a second call must not decode again */
	CHECK(cgltf_decode_draco_primitive(f.data, prim, &opts) == cgltf_draco_result_success);
	CHECK(dec.call_count == 1);
}

static void test_on_demand_and_shared_buffer()
{
	Fixture f(true, true);

	static const float positions0[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
	static const float positions1[] = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21};
	static const uint16_t indices1[] = {0, 1, 2, 3, 2, 1};

	MockDecoder dec0;
	dec0.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions0, 9));
	dec0.attrs.push_back(make_float_attr(cgltf_attribute_type_texcoord, 0, cgltf_type_vec2, positions0, 6));

	MockDecoder dec1;
	dec1.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions1, 12));
	set_indices(&dec1, cgltf_component_type_r_16u, indices1, 6);

	/* decode prim0 only: prim1 of the same mesh must stay compressed */
	cgltf_draco_options opts0 = make_options(&dec0);
	CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts0) == cgltf_draco_result_success);
	CHECK(dec0.call_count == 1);
	/* prim1 is not decoded yet: it still points to the placeholder accessor */
	CHECK(f.prim(0, 1)->attributes_count == 1);
	CHECK(f.prim(0, 1)->attributes[0].data == &f.data->accessors[2]);

	/* now decode prim1 on demand (16-bit indices) */
	cgltf_draco_options opts1 = make_options(&dec1);
	CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 1), &opts1) == cgltf_draco_result_success);
	CHECK(dec1.call_count == 1);
	CHECK(dec1.last_data == (const uint8_t*)f.data->buffers[0].data + 16);
	CHECK(f.prim(0, 1)->indices && f.prim(0, 1)->indices->count == 6);
	CHECK(f.prim(0, 1)->indices->component_type == cgltf_component_type_r_16u);
	CHECK(cgltf_accessor_read_index(f.prim(0, 1)->indices, 5) == 1);

	/* prim2 of mesh 1 shares buffer view 0 with prim0; both decode independently */
	MockDecoder dec2;
	dec2.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions0, 9));
	cgltf_draco_options opts2 = make_options(&dec2);
	CHECK(cgltf_decode_draco_primitive(f.data, f.prim(1, 0), &opts2) == cgltf_draco_result_success);
	CHECK(dec2.last_data == (const uint8_t*)f.data->buffers[0].data + 0);

	const cgltf_accessor* pos0 = cgltf_find_accessor(f.prim(0, 0), cgltf_attribute_type_position, 0);
	const cgltf_accessor* pos2 = cgltf_find_accessor(f.prim(1, 0), cgltf_attribute_type_position, 0);
	CHECK(pos0 && pos2 && pos0 != pos2);
	CHECK(cgltf_buffer_view_data(pos0->buffer_view) != cgltf_buffer_view_data(pos2->buffer_view));

	/* the shared source buffer is still intact and owned by cgltf_data */
	CHECK(f.data->buffers[0].data != NULL);
}

static void test_attribute_mismatch()
{
	static const float positions[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
	static const float texcoords[] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
	static const uint16_t texcoords_u16[] = {1, 2, 3, 4, 5, 6};

	/* case 1: decoded attribute set is smaller than the mapping */
	{
		Fixture f(true, true);
		MockDecoder dec;
		dec.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions, 9));
		cgltf_draco_options opts = make_options(&dec);
		CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts) == cgltf_draco_result_attribute_mismatch);
		CHECK(f.prim(0, 0)->attributes_count == 2);
		CHECK(f.prim(0, 0)->attributes[0].data == &f.data->accessors[0]);
	}

	/* case 2: decoded attribute set is larger than the mapping */
	{
		Fixture f(true, true);
		MockDecoder dec;
		dec.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions, 9));
		dec.attrs.push_back(make_float_attr(cgltf_attribute_type_texcoord, 0, cgltf_type_vec2, texcoords, 6));
		dec.attrs.push_back(make_float_attr(cgltf_attribute_type_normal, 0, cgltf_type_vec3, positions, 9));
		cgltf_draco_options opts = make_options(&dec);
		CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts) == cgltf_draco_result_attribute_mismatch);
	}

	/* case 3: same count, wrong semantic (NORMAL instead of TEXCOORD_0) */
	{
		Fixture f(true, true);
		MockDecoder dec;
		dec.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions, 9));
		dec.attrs.push_back(make_float_attr(cgltf_attribute_type_normal, 0, cgltf_type_vec3, positions, 9));
		cgltf_draco_options opts = make_options(&dec);
		CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts) == cgltf_draco_result_attribute_mismatch);
	}

	/* case 4: decoded attributes disagree on the vertex count */
	{
		Fixture f(true, true);
		MockDecoder dec;
		dec.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions, 9));
		dec.attrs.push_back(make_float_attr(cgltf_attribute_type_texcoord, 0, cgltf_type_vec2, texcoords, 4));
		cgltf_draco_options opts = make_options(&dec);
		CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts) == cgltf_draco_result_attribute_mismatch);
	}

	/* case 5: decoded component type does not match the placeholder accessor */
	{
		Fixture f(true, true);
		MockDecoder dec;
		dec.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions, 9));
		dec.attrs.push_back(make_uint16_attr(cgltf_attribute_type_texcoord, 0, cgltf_type_vec2, texcoords_u16, 6));
		cgltf_draco_options opts = make_options(&dec);
		CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts) == cgltf_draco_result_attribute_mismatch);
	}

	/* case 6: duplicate decoded attribute */
	{
		Fixture f(true, true);
		MockDecoder dec;
		dec.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions, 9));
		dec.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions, 9));
		cgltf_draco_options opts = make_options(&dec);
		CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts) == cgltf_draco_result_attribute_mismatch);
	}
}

static void test_index_out_of_range()
{
	static const float positions[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
	static const float texcoords[] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
	static const uint32_t indices[] = {0, 1, 3}; /* 3 >= vertex count */

	Fixture f(true, true);
	MockDecoder dec;
	dec.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions, 9));
	dec.attrs.push_back(make_float_attr(cgltf_attribute_type_texcoord, 0, cgltf_type_vec2, texcoords, 6));
	set_indices(&dec, cgltf_component_type_r_32u, indices, 3);

	cgltf_draco_options opts = make_options(&dec);
	CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts) == cgltf_draco_result_index_out_of_range);
	CHECK(f.prim(0, 0)->attributes_count == 2);
	CHECK(f.prim(0, 0)->indices == NULL);
}

static void test_corrupted_stream()
{
	Fixture f(true, true);
	MockDecoder dec;
	dec.result = cgltf_draco_result_decode_failed; /* decoder rejects the bitstream */
	cgltf_draco_options opts = make_options(&dec);
	CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts) == cgltf_draco_result_decode_failed);
	CHECK(dec.call_count == 1);
	CHECK(f.prim(0, 0)->attributes_count == 2);
	CHECK(f.prim(0, 0)->attributes[0].data == &f.data->accessors[0]);
	CHECK(f.data->draco_decoded == NULL);

	/* the primitive can be decoded again after a failure (no stale state) */
	static const float positions[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
	static const float texcoords[] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
	dec.result = cgltf_draco_result_success;
	dec.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions, 9));
	dec.attrs.push_back(make_float_attr(cgltf_attribute_type_texcoord, 0, cgltf_type_vec2, texcoords, 6));
	CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts) == cgltf_draco_result_success);
	CHECK(f.prim(0, 0)->attributes_count == 2);
}

static void test_memory_bound()
{
	Fixture f(true, true);

	/* 1000 vertices: POSITION (12 KB) + TEXCOORD_0 (8 KB) + 3000 uint32 indices (12 KB) */
	const cgltf_size vertex_count = 1000;
	const cgltf_size index_count = 3000;
	std::vector<float> positions(vertex_count * 3);
	std::vector<float> texcoords(vertex_count * 2);
	std::vector<uint32_t> indices(index_count);
	for (cgltf_size i = 0; i < vertex_count; ++i)
	{
		positions[i * 3 + 0] = (float)i;
		positions[i * 3 + 1] = (float)i * 2;
		positions[i * 3 + 2] = (float)i * 3;
		texcoords[i * 2 + 0] = (float)i / vertex_count;
		texcoords[i * 2 + 1] = 1.0f - (float)i / vertex_count;
	}
	for (cgltf_size i = 0; i < index_count; ++i)
		indices[i] = (uint32_t)(i % vertex_count);

	MockDecoder dec;
	dec.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions.data(), positions.size()));
	dec.attrs.push_back(make_float_attr(cgltf_attribute_type_texcoord, 0, cgltf_type_vec2, texcoords.data(), texcoords.size()));
	set_indices(&dec, cgltf_component_type_r_32u, indices.data(), indices.size());

	cgltf_size decoded_size = positions.size() * sizeof(float) + texcoords.size() * sizeof(float) + indices.size() * sizeof(uint32_t);

	cgltf_draco_options opts = make_options(&dec);

	f.tracker.recording = true;
	f.tracker.peak = f.tracker.current;
	cgltf_size baseline = f.tracker.current;

	CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts) == cgltf_draco_result_success);

	cgltf_size peak_extra = f.tracker.peak - baseline;
	f.tracker.recording = false;

	/* peak memory during decode must be a small constant multiple of the decoded
	   data of this single primitive (the mock decoder allocates outside cgltf) */
	CHECK(peak_extra <= 2 * decoded_size + 4096);

	/* only this primitive was decoded */
	CHECK(f.prim(0, 1)->attributes_count == 1);
	CHECK(f.prim(0, 1)->attributes[0].data == &f.data->accessors[2]);
}

static void test_free_after_decode_and_failure()
{
	/* decode two primitives, then release everything; the tracked allocator
	   (checked in ~Fixture) verifies that nothing leaks */
	{
		Fixture f(true, true);
		static const float positions[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
		static const float texcoords[] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
		static const uint32_t indices[] = {0, 1, 2};

		MockDecoder dec;
		dec.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions, 9));
		dec.attrs.push_back(make_float_attr(cgltf_attribute_type_texcoord, 0, cgltf_type_vec2, texcoords, 6));
		set_indices(&dec, cgltf_component_type_r_32u, indices, 3);
		cgltf_draco_options opts = make_options(&dec);

		CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts) == cgltf_draco_result_success);

		MockDecoder dec2;
		dec2.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions, 9));
		cgltf_draco_options opts2 = make_options(&dec2);
		CHECK(cgltf_decode_draco_primitive(f.data, f.prim(1, 0), &opts2) == cgltf_draco_result_success);
	}

	/* failure paths must not leak either */
	{
		Fixture f(true, true);
		MockDecoder dec;
		dec.result = cgltf_draco_result_decode_failed;
		cgltf_draco_options opts = make_options(&dec);
		CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts) == cgltf_draco_result_decode_failed);
	}
	{
		Fixture f(true, true);
		static const float positions[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
		MockDecoder dec;
		dec.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions, 9));
		cgltf_draco_options opts = make_options(&dec);
		CHECK(cgltf_decode_draco_primitive(f.data, f.prim(0, 0), &opts) == cgltf_draco_result_attribute_mismatch);
	}
}

static void test_extension_mapping_fallback()
{
	/* prim(4, 0) declares no attributes; the decoded attribute list is then
	   built from the extension mapping */
	Fixture f(true, true);
	static const float positions[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};

	MockDecoder dec;
	dec.attrs.push_back(make_float_attr(cgltf_attribute_type_position, 0, cgltf_type_vec3, positions, 9));
	cgltf_draco_options opts = make_options(&dec);

	cgltf_primitive* prim = f.prim(4, 0);
	CHECK(prim->attributes_count == 0);
	CHECK(cgltf_decode_draco_primitive(f.data, prim, &opts) == cgltf_draco_result_success);
	CHECK(prim->attributes_count == 1);
	CHECK(prim->attributes[0].type == cgltf_attribute_type_position);
	CHECK(prim->attributes[0].name && std::strcmp(prim->attributes[0].name, "POSITION") == 0);
	cgltf_float v[3];
	CHECK(cgltf_accessor_read_float(prim->attributes[0].data, 2, v, 3) && v[0] == 7 && v[2] == 9);
}

static void test_no_draco_usage_unchanged()
{
	/* when Draco decoding is never requested, parsing and freeing a file that
	   contains KHR_draco_mesh_compression behaves exactly as before */
	Fixture f(true, true);
	CHECK(f.data->meshes_count == 5);
	CHECK(f.prim(0, 0)->has_draco_mesh_compression);
	CHECK(f.prim(0, 0)->draco_mesh_compression.attributes_count == 2);
	CHECK(f.prim(0, 0)->attributes_count == 2);
	CHECK(f.prim(2, 0)->has_draco_mesh_compression == 0);
	CHECK(f.data->draco_decoded == NULL);
}

int main()
{
	test_unsupported_without_decoder();
	test_not_compressed();
	test_invalid_arguments();
	test_buffer_not_loaded();
	test_decode_attributes_and_indices();
	test_on_demand_and_shared_buffer();
	test_attribute_mismatch();
	test_index_out_of_range();
	test_corrupted_stream();
	test_memory_bound();
	test_free_after_decode_and_failure();
	test_extension_mapping_fallback();
	test_no_draco_usage_unchanged();

	if (failures == 0)
		std::printf("test_draco: all tests passed\n");
	else
		std::printf("test_draco: %d check(s) failed\n", failures);
	return failures ? 1 : 0;
}
