// test_draco_real.cpp - round-trip tests for cgltf's optional Draco decoding using
// Google's Draco library. Real Draco-compressed glTF assets (GLB) are generated at
// runtime with draco::Encoder, parsed with cgltf, decoded through a draco::Decoder
// adapter and compared against the original data.
//
// This target is only built when CMake is configured with -DDRACO_ROOT=<prefix>;
// the rest of the test suite (including test_draco.cpp) does not depend on Draco.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "draco/compression/decode.h"
#include "draco/compression/encode.h"
#include "draco/core/encoder_buffer.h"
#include "draco/mesh/mesh.h"

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
/* Asset generation: encode meshes with the real Draco encoder               */
/* ------------------------------------------------------------------------- */

struct AttrSpec
{
	const char* gltf_name;          /* POSITION, NORMAL, ... */
	draco::GeometryAttribute::Type draco_type;
	int components;
	std::vector<float> data;        /* num_points * components */
	int quantization_bits;
};

struct MeshSpec
{
	int num_points;
	std::vector<AttrSpec> attrs;
	std::vector<uint32_t> indices;
};

/* Encodes a mesh and returns the Draco bitstream; out_unique_ids receives the
   Draco unique id of each attribute (in AttrSpec order). */
static std::string encode_draco_mesh(const MeshSpec& spec, std::vector<int>* out_unique_ids)
{
	draco::Mesh mesh;
	mesh.set_num_points(spec.num_points);

	for (size_t i = 0; i < spec.attrs.size(); ++i)
	{
		const AttrSpec& a = spec.attrs[i];
		draco::GeometryAttribute ga;
		ga.Init(a.draco_type, nullptr, (uint8_t)a.components, draco::DT_FLOAT32, false,
				a.components * (int64_t)sizeof(float), 0);
		int att_id = mesh.AddAttribute(ga, true, spec.num_points);
		draco::PointAttribute* pa = mesh.attribute(att_id);
		for (int p = 0; p < spec.num_points; ++p)
			pa->SetAttributeValue(draco::AttributeValueIndex(p), &a.data[(size_t)p * a.components]);
		out_unique_ids->push_back(mesh.attribute(att_id)->unique_id());
	}

	for (size_t i = 0; i + 2 < spec.indices.size(); i += 3)
	{
		draco::Mesh::Face face = {draco::PointIndex(spec.indices[i]),
								  draco::PointIndex(spec.indices[i + 1]),
								  draco::PointIndex(spec.indices[i + 2])};
		mesh.AddFace(face);
	}

	draco::Encoder encoder;
	for (size_t i = 0; i < spec.attrs.size(); ++i)
		encoder.SetAttributeQuantization(spec.attrs[i].draco_type, spec.attrs[i].quantization_bits);

	draco::EncoderBuffer buffer;
	draco::Status status = encoder.EncodeMeshToBuffer(mesh, &buffer);
	if (!status.ok())
	{
		std::printf("FAIL: draco encoding failed: %s\n", status.error_msg());
		++failures;
		return std::string();
	}
	return std::string(buffer.data(), buffer.size());
}

/* ------------------------------------------------------------------------- */
/* GLB assembly                                                              */
/* ------------------------------------------------------------------------- */

static void append_u32(std::string* out, uint32_t value)
{
	out->append((const char*)&value, 4);
}

static std::string build_glb(const std::string& json, const std::string& bin)
{
	std::string json_padded = json;
	while (json_padded.size() % 4)
		json_padded += ' ';
	std::string bin_padded = bin;
	while (bin_padded.size() % 4)
		bin_padded += '\0';

	std::string glb;
	append_u32(&glb, 0x46546C67); /* magic */
	append_u32(&glb, 2);          /* version */
	append_u32(&glb, (uint32_t)(12 + 8 + json_padded.size() + 8 + bin_padded.size()));
	append_u32(&glb, (uint32_t)json_padded.size());
	append_u32(&glb, 0x4E4F534A); /* JSON */
	glb += json_padded;
	append_u32(&glb, (uint32_t)bin_padded.size());
	append_u32(&glb, 0x004E4942); /* BIN */
	glb += bin_padded;
	return glb;
}

/* ------------------------------------------------------------------------- */
/* Decoder adapter: bridges cgltf's decode callback to draco::Decoder        */
/* ------------------------------------------------------------------------- */

static cgltf_draco_result draco_decode_callback(void* user_data, const cgltf_data* data, const cgltf_primitive* primitive,
												const void* compressed_data, cgltf_size compressed_size, cgltf_draco_mesh* out_mesh)
{
	(void)user_data;

	draco::DecoderBuffer buffer;
	buffer.Init((const char*)compressed_data, (size_t)compressed_size);

	draco::Decoder decoder;
	draco::StatusOr<std::unique_ptr<draco::Mesh>> status_or = decoder.DecodeMeshFromBuffer(&buffer);
	if (!status_or.ok())
		return cgltf_draco_result_decode_failed;

	std::unique_ptr<draco::Mesh> mesh = std::move(status_or).value();
	if (!mesh)
		return cgltf_draco_result_decode_failed;

	if (mesh->num_faces() > 0)
	{
		std::vector<uint32_t> indices((size_t)mesh->num_faces() * 3);
		for (draco::FaceIndex f(0); f.value() < mesh->num_faces(); ++f)
		{
			const draco::Mesh::Face& face = mesh->face(f);
			for (int k = 0; k < 3; ++k)
				indices[(size_t)f.value() * 3 + k] = face[k].value();
		}
		cgltf_draco_result r = cgltf_draco_mesh_set_indices(out_mesh, cgltf_component_type_r_32u, indices.size(), indices.data());
		if (r != cgltf_draco_result_success)
			return r;
	}

	/* map decoded Draco attributes to glTF semantics through the extension's
	   attribute mapping (semantic -> Draco unique id) */
	const cgltf_draco_mesh_compression* ext = &primitive->draco_mesh_compression;
	for (cgltf_size i = 0; i < ext->attributes_count; ++i)
	{
		const cgltf_attribute* mapping = &ext->attributes[i];
		uint32_t unique_id = (uint32_t)cgltf_accessor_index(data, mapping->data);
		const draco::PointAttribute* pa = mesh->GetAttributeByUniqueId(unique_id);
		if (!pa || pa->data_type() != draco::DT_FLOAT32)
			return cgltf_draco_result_attribute_mismatch;

		int components = pa->num_components();
		cgltf_type accessor_type =
			components == 1 ? cgltf_type_scalar :
			components == 2 ? cgltf_type_vec2 :
			components == 3 ? cgltf_type_vec3 :
			components == 4 ? cgltf_type_vec4 : cgltf_type_invalid;
		if (accessor_type == cgltf_type_invalid)
			return cgltf_draco_result_attribute_mismatch;

		std::vector<float> values((size_t)mesh->num_points() * components);
		for (draco::PointIndex p(0); p.value() < mesh->num_points(); ++p)
			pa->GetValue(pa->mapped_index(p), &values[(size_t)p.value() * components]);

		cgltf_draco_result r = cgltf_draco_mesh_add_attribute(out_mesh, mapping->type, (cgltf_size)mapping->index,
															  cgltf_component_type_r_32f, accessor_type,
															  (cgltf_size)mesh->num_points(), values.data());
		if (r != cgltf_draco_result_success)
			return r;
	}

	return cgltf_draco_result_success;
}

static cgltf_draco_options draco_options()
{
	cgltf_draco_options opts;
	opts.decode_func = draco_decode_callback;
	opts.user_data = NULL;
	return opts;
}

/* ------------------------------------------------------------------------- */
/* Test fixtures                                                             */
/* ------------------------------------------------------------------------- */

static MeshSpec make_quad_spec()
{
	MeshSpec spec;
	spec.num_points = 4;
	spec.indices = {0, 1, 2, 0, 2, 3};

	AttrSpec pos;
	pos.gltf_name = "POSITION";
	pos.draco_type = draco::GeometryAttribute::POSITION;
	pos.components = 3;
	pos.data = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
	pos.quantization_bits = 16;
	spec.attrs.push_back(pos);

	AttrSpec nrm;
	nrm.gltf_name = "NORMAL";
	nrm.draco_type = draco::GeometryAttribute::NORMAL;
	nrm.components = 3;
	nrm.data = {0, 0, 1, 0.1f, 0, 0.99f, 0.2f, 0, 0.98f, 0.3f, 0, 0.95f};
	nrm.quantization_bits = 14;
	spec.attrs.push_back(nrm);

	AttrSpec tan;
	tan.gltf_name = "TANGENT";
	tan.draco_type = draco::GeometryAttribute::GENERIC; /* draco has no tangent type; the glTF mapping carries the semantic */
	tan.components = 4;
	tan.data = {1, 0, 0, 1, 0.9f, 0.1f, 0, 1, 0.8f, 0.2f, 0, -1, 0.7f, 0.3f, 0, 1};
	tan.quantization_bits = 14;
	spec.attrs.push_back(tan);

	AttrSpec uv;
	uv.gltf_name = "TEXCOORD_0";
	uv.draco_type = draco::GeometryAttribute::TEX_COORD;
	uv.components = 2;
	uv.data = {0, 0, 1, 0, 1, 1, 0, 1};
	uv.quantization_bits = 16;
	spec.attrs.push_back(uv);

	AttrSpec col;
	col.gltf_name = "COLOR_0";
	col.draco_type = draco::GeometryAttribute::COLOR;
	col.components = 4;
	col.data = {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1};
	col.quantization_bits = 14;
	spec.attrs.push_back(col);

	return spec;
}

static MeshSpec make_triangle_spec()
{
	MeshSpec spec;
	spec.num_points = 3;
	spec.indices = {0, 1, 2};

	AttrSpec pos;
	pos.gltf_name = "POSITION";
	pos.draco_type = draco::GeometryAttribute::POSITION;
	pos.components = 3;
	pos.data = {0, 0, 0, 1, 0, 0, 0, 1, 0};
	pos.quantization_bits = 16;
	spec.attrs.push_back(pos);

	AttrSpec uv;
	uv.gltf_name = "TEXCOORD_0";
	uv.draco_type = draco::GeometryAttribute::TEX_COORD;
	uv.components = 2;
	uv.data = {0, 0, 1, 0, 0, 1};
	uv.quantization_bits = 16;
	spec.attrs.push_back(uv);

	return spec;
}

static MeshSpec make_normal_quad_spec()
{
	MeshSpec spec;
	spec.num_points = 4;
	spec.indices = {0, 1, 2, 0, 2, 3};

	AttrSpec pos;
	pos.gltf_name = "POSITION";
	pos.draco_type = draco::GeometryAttribute::POSITION;
	pos.components = 3;
	pos.data = {0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1};
	pos.quantization_bits = 16;
	spec.attrs.push_back(pos);

	AttrSpec nrm;
	nrm.gltf_name = "NORMAL";
	nrm.draco_type = draco::GeometryAttribute::NORMAL;
	nrm.components = 3;
	nrm.data = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
	nrm.quantization_bits = 14;
	spec.attrs.push_back(nrm);

	return spec;
}

/* Builds the JSON part of a glTF file for a list of draco-compressed primitives.
   Each primitive gets its own buffer view into a single shared buffer that holds
   the concatenated Draco streams. */
static std::string build_gltf_json(const std::vector<MeshSpec>& specs, const std::vector<std::string>& streams,
								   const std::vector<std::vector<int>>& unique_ids, cgltf_size bin_size)
{
	std::string json = "{"
		"\"asset\":{\"version\":\"2.0\"},"
		"\"extensionsUsed\":[\"KHR_draco_mesh_compression\"],"
		"\"extensionsRequired\":[\"KHR_draco_mesh_compression\"],";

	/* accessors: for each primitive, one placeholder per attribute plus one for the indices */
	std::string accessors = "\"accessors\":[";
	std::string buffer_views = "\"bufferViews\":[";
	std::string primitives;
	int accessor_index = 0;
	cgltf_size stream_offset = 0;

	for (size_t p = 0; p < specs.size(); ++p)
	{
		const MeshSpec& spec = specs[p];
		stream_offset = (stream_offset + 3) & ~(cgltf_size)3; /* streams are 4-byte aligned in the bin */
		std::string declared = "\"attributes\":{";
		std::string mapping = "\"attributes\":{";
		for (size_t a = 0; a < spec.attrs.size(); ++a)
		{
			const AttrSpec& attr = spec.attrs[a];
			const char* type = attr.components == 1 ? "SCALAR" : attr.components == 2 ? "VEC2" : attr.components == 3 ? "VEC3" : "VEC4";
			if (a) { accessors += ","; declared += ","; mapping += ","; }
			char buf[128];
			std::snprintf(buf, sizeof(buf), "{\"componentType\":5126,\"count\":%d,\"type\":\"%s\"}", spec.num_points, type);
			accessors += buf;
			std::snprintf(buf, sizeof(buf), "\"%s\":%d", attr.gltf_name, accessor_index);
			declared += buf;
			std::snprintf(buf, sizeof(buf), "\"%s\":%d", attr.gltf_name, unique_ids[p][a]);
			mapping += buf;
			++accessor_index;
		}
		declared += "}";
		mapping += "}";

		/* placeholder accessor for the indices */
		char buf[256];
		if (accessor_index) accessors += ",";
		std::snprintf(buf, sizeof(buf), "{\"componentType\":5125,\"count\":%d,\"type\":\"SCALAR\"}", (int)spec.indices.size());
		accessors += buf;
		int indices_accessor = accessor_index++;

		if (p) { buffer_views += ","; primitives += ","; }
		std::snprintf(buf, sizeof(buf), "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}",
					  (unsigned)stream_offset, (unsigned)streams[p].size());
		buffer_views += buf;
		stream_offset += streams[p].size();

		char pbuf[1024];
		std::snprintf(pbuf, sizeof(pbuf),
					  "{\"mode\":4,\"indices\":%d,%s,\"extensions\":{\"KHR_draco_mesh_compression\":{\"bufferView\":%d,%s}}}",
					  indices_accessor, declared.c_str(), (int)p, mapping.c_str());
		primitives += pbuf;
	}
	accessors += "]";
	buffer_views += "]";

	json += accessors + "," + buffer_views + ",";
	json += "\"buffers\":[{\"byteLength\":" + std::to_string(bin_size) + "}],";
	json += "\"meshes\":[{\"primitives\":[" + primitives + "]}],";
	json += "\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";
	return json;
}

struct GlbAsset
{
	std::vector<MeshSpec> specs;
	std::vector<std::string> streams;
	std::vector<std::vector<int>> unique_ids;
	std::string glb;
};

static GlbAsset make_asset_from_streams(const std::vector<MeshSpec>& specs, const std::vector<std::string>& streams,
										const std::vector<std::vector<int>>& unique_ids)
{
	GlbAsset asset;
	asset.specs = specs;
	asset.streams = streams;
	asset.unique_ids = unique_ids;

	std::string bin;
	for (size_t i = 0; i < streams.size(); ++i)
	{
		while (bin.size() % 4)
			bin += '\0';
		bin += streams[i];
	}

	std::string json = build_gltf_json(specs, streams, unique_ids, bin.size());
	asset.glb = build_glb(json, bin);
	return asset;
}

static GlbAsset make_asset(const std::vector<MeshSpec>& specs)
{
	std::vector<std::string> streams;
	std::vector<std::vector<int>> unique_ids;
	for (size_t i = 0; i < specs.size(); ++i)
	{
		std::vector<int> ids;
		streams.push_back(encode_draco_mesh(specs[i], &ids));
		unique_ids.push_back(ids);
	}
	return make_asset_from_streams(specs, streams, unique_ids);
}

/* ------------------------------------------------------------------------- */
/* Verification helpers                                                      */
/* ------------------------------------------------------------------------- */

static const AttrSpec* find_attr(const MeshSpec& spec, const char* name)
{
	for (size_t i = 0; i < spec.attrs.size(); ++i)
		if (std::strcmp(spec.attrs[i].gltf_name, name) == 0)
			return &spec.attrs[i];
	return NULL;
}

static bool nearly_equal(float a, float b, float eps)
{
	return std::fabs(a - b) <= eps;
}

/* Matches every decoded vertex to an original vertex by position and compares
   all attributes through that permutation. Draco is free to reorder vertices,
   so the comparison must be order-independent. */
static void check_decoded_primitive(const cgltf_primitive* prim, const MeshSpec& spec)
{
	const cgltf_accessor* pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
	CHECK(pos != NULL);
	if (!pos)
		return;
	CHECK((int)pos->count == spec.num_points);

	const AttrSpec* pos_spec = find_attr(spec, "POSITION");
	CHECK(pos_spec != NULL);
	if (!pos_spec)
		return;

	const float eps_pos = 1e-3f;  /* 16-bit position quantization over unit range */
	const float eps_attr = 1e-2f; /* 14-bit quantization for the other attributes */

	/* build the vertex permutation via positions */
	std::vector<int> perm(spec.num_points, -1);
	for (int i = 0; i < spec.num_points; ++i)
	{
		cgltf_float decoded[3];
		CHECK(cgltf_accessor_read_float(pos, (cgltf_size)i, decoded, 3));
		for (int j = 0; j < spec.num_points; ++j)
		{
			const float* orig = &pos_spec->data[(size_t)j * 3];
			if (nearly_equal(decoded[0], orig[0], eps_pos) && nearly_equal(decoded[1], orig[1], eps_pos) && nearly_equal(decoded[2], orig[2], eps_pos))
			{
				CHECK(perm[i] < 0); /* positions must be unique */
				perm[i] = j;
				break;
			}
		}
		CHECK(perm[i] >= 0); /* every decoded vertex must match an original vertex */
	}

	/* compare the remaining attributes through the permutation */
	for (size_t a = 0; a < spec.attrs.size(); ++a)
	{
		const AttrSpec& attr = spec.attrs[a];
		if (std::strcmp(attr.gltf_name, "POSITION") == 0)
			continue;

		cgltf_attribute_type type = cgltf_attribute_type_invalid;
		if (std::strcmp(attr.gltf_name, "NORMAL") == 0) type = cgltf_attribute_type_normal;
		else if (std::strcmp(attr.gltf_name, "TANGENT") == 0) type = cgltf_attribute_type_tangent;
		else if (std::strcmp(attr.gltf_name, "TEXCOORD_0") == 0) type = cgltf_attribute_type_texcoord;
		else if (std::strcmp(attr.gltf_name, "COLOR_0") == 0) type = cgltf_attribute_type_color;
		CHECK(type != cgltf_attribute_type_invalid);

		const cgltf_accessor* acc = cgltf_find_accessor(prim, type, 0);
		CHECK(acc != NULL);
		if (!acc)
			continue;
		CHECK((int)acc->count == spec.num_points);

		for (int i = 0; i < spec.num_points; ++i)
		{
			if (perm[i] < 0)
				continue;
			cgltf_float decoded[4];
			CHECK(cgltf_accessor_read_float(acc, (cgltf_size)i, decoded, (cgltf_size)attr.components));
			const float* orig = &attr.data[(size_t)perm[i] * attr.components];
			for (int c = 0; c < attr.components; ++c)
			{
				if (!nearly_equal(decoded[c], orig[c], eps_attr))
				{
					std::printf("FAIL: attribute %s vertex %d component %d: decoded %f, expected %f\n",
								attr.gltf_name, i, c, decoded[c], orig[c]);
					++failures;
				}
			}
		}
	}

	/* compare the index data as unordered triangles over original vertex ids */
	CHECK(prim->indices != NULL);
	if (!prim->indices)
		return;
	CHECK((int)prim->indices->count == (int)spec.indices.size());

	std::vector<bool> matched(spec.indices.size() / 3, false);
	for (cgltf_size t = 0; t + 2 < prim->indices->count; t += 3)
	{
		int tri[3];
		for (int k = 0; k < 3; ++k)
		{
			cgltf_size decoded_index = cgltf_accessor_read_index(prim->indices, t + k);
			CHECK((int)decoded_index < spec.num_points);
			tri[k] = (int)decoded_index < spec.num_points ? perm[(int)decoded_index] : -1;
		}
		bool found = false;
		for (size_t u = 0; u + 2 < spec.indices.size(); u += 3)
		{
			if (matched[u / 3])
				continue;
			int used[3] = {0, 0, 0};
			int ok = 0;
			for (int k = 0; k < 3; ++k)
				for (int m = 0; m < 3; ++m)
					if (!used[m] && tri[k] == (int)spec.indices[u + m])
					{
						used[m] = 1;
						++ok;
						break;
					}
			if (ok == 3)
			{
				matched[u / 3] = true;
				found = true;
				break;
			}
		}
		CHECK(found); /* every decoded triangle must exist in the original mesh */
	}
}

static cgltf_data* parse_asset(const GlbAsset& asset)
{
	cgltf_options options;
	std::memset(&options, 0, sizeof(options));
	cgltf_data* data = NULL;
	cgltf_result r = cgltf_parse(&options, asset.glb.data(), asset.glb.size(), &data);
	CHECK(r == cgltf_result_success);
	if (r != cgltf_result_success)
		return NULL;
	r = cgltf_load_buffers(&options, data, NULL);
	CHECK(r == cgltf_result_success);
	return data;
}

/* ------------------------------------------------------------------------- */

static void test_real_decode_single_primitive()
{
	GlbAsset asset = make_asset({make_quad_spec()});
	cgltf_data* data = parse_asset(asset);
	CHECK(data != NULL);
	if (!data)
		return;

	cgltf_primitive* prim = &data->meshes[0].primitives[0];
	CHECK(prim->has_draco_mesh_compression);
	CHECK(prim->draco_mesh_compression.attributes_count == 5);

	cgltf_draco_options opts = draco_options();
	CHECK(cgltf_decode_draco_primitive(data, prim, &opts) == cgltf_draco_result_success);

	CHECK(prim->attributes_count == 5);
	check_decoded_primitive(prim, asset.specs[0]);

	cgltf_free(data);
}

static void test_real_multi_primitive_on_demand()
{
	GlbAsset asset = make_asset({make_triangle_spec(), make_normal_quad_spec()});
	cgltf_data* data = parse_asset(asset);
	CHECK(data != NULL);
	if (!data)
		return;

	/* both primitives share one buffer (two buffer views into the same BIN chunk) */
	CHECK(data->buffers_count == 1);
	cgltf_primitive* prim0 = &data->meshes[0].primitives[0];
	cgltf_primitive* prim1 = &data->meshes[0].primitives[1];
	CHECK(prim0->draco_mesh_compression.buffer_view->buffer == prim1->draco_mesh_compression.buffer_view->buffer);

	cgltf_draco_options opts = draco_options();

	/* decode the second primitive first: the first one must stay compressed */
	CHECK(cgltf_decode_draco_primitive(data, prim1, &opts) == cgltf_draco_result_success);
	CHECK(prim0->attributes_count == 2);
	CHECK(prim0->attributes[0].data == &data->accessors[0]);

	CHECK(cgltf_decode_draco_primitive(data, prim0, &opts) == cgltf_draco_result_success);

	check_decoded_primitive(prim0, asset.specs[0]);
	check_decoded_primitive(prim1, asset.specs[1]);

	/* the shared source buffer is still valid */
	CHECK(data->buffers[0].data != NULL);

	cgltf_free(data);
}

static void test_real_corrupted_stream()
{
	MeshSpec spec = make_quad_spec();
	std::vector<int> ids;
	std::string stream = encode_draco_mesh(spec, &ids);
	CHECK(!stream.empty());

	/* variant 1: garbage instead of a Draco stream */
	{
		std::string garbage = stream;
		for (size_t i = 0; i < garbage.size(); ++i)
			garbage[i] = (char)(i * 31 + 7);

		GlbAsset asset = make_asset_from_streams({spec}, {garbage}, {ids});
		cgltf_data* data = parse_asset(asset);
		CHECK(data != NULL);
		if (data)
		{
			cgltf_draco_options opts = draco_options();
			cgltf_draco_result r = cgltf_decode_draco_primitive(data, &data->meshes[0].primitives[0], &opts);
			CHECK(r == cgltf_draco_result_decode_failed);
			/* the primitive must be untouched after a failed decode */
			CHECK(data->meshes[0].primitives[0].attributes[0].data == &data->accessors[0]);
			cgltf_free(data);
		}
	}

	/* variant 2: truncated Draco stream */
	{
		std::string truncated = stream.substr(0, stream.size() / 2);

		GlbAsset asset = make_asset_from_streams({spec}, {truncated}, {ids});
		cgltf_data* data = parse_asset(asset);
		CHECK(data != NULL);
		if (data)
		{
			cgltf_draco_options opts = draco_options();
			cgltf_draco_result r = cgltf_decode_draco_primitive(data, &data->meshes[0].primitives[0], &opts);
			CHECK(r != cgltf_draco_result_success);
			CHECK(data->meshes[0].primitives[0].attributes[0].data == &data->accessors[0]);
			cgltf_free(data);
		}
	}

	/* variant 3: single corrupted byte inside the stream; the decode must either
	   fail cleanly or (if the bitstream still parses) produce data that cgltf can
	   validate - in no case may it crash or corrupt memory (checked by ASan) */
	{
		std::string corrupted = stream;
		for (size_t i = 8; i < corrupted.size(); i += 97)
			corrupted[i] ^= 0xFF;

		GlbAsset asset = make_asset_from_streams({spec}, {corrupted}, {ids});
		cgltf_data* data = parse_asset(asset);
		CHECK(data != NULL);
		if (data)
		{
			cgltf_draco_options opts = draco_options();
			cgltf_decode_draco_primitive(data, &data->meshes[0].primitives[0], &opts);
			cgltf_free(data);
		}
	}
}

int main()
{
	test_real_decode_single_primitive();
	test_real_multi_primitive_on_demand();
	test_real_corrupted_stream();

	if (failures == 0)
		std::printf("test_draco_real: all tests passed\n");
	else
		std::printf("test_draco_real: %d check(s) failed\n", failures);
	return failures ? 1 : 0;
}
