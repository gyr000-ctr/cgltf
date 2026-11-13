#define CGLTF_IMPLEMENTATION
#define CGLTF_WRITE_IMPLEMENTATION
#include "../cgltf_write.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct AttrSpec
{
	const char* semantic;
	int component_type; // glTF enum, e.g. 5120
	const char* type_name; // e.g. "VEC3"
	bool normalized;
	int stride;
	bool morph_target;
	std::vector<uint8_t> bytes; // vertex_count * stride bytes
};

int component_size(int component_type)
{
	switch (component_type)
	{
		case 5120: case 5121: return 1;
		case 5122: case 5123: return 2;
		default: return 4;
	}
}

void put_u32(std::vector<uint8_t>& v, uint32_t x)
{
	v.push_back(uint8_t(x));
	v.push_back(uint8_t(x >> 8));
	v.push_back(uint8_t(x >> 16));
	v.push_back(uint8_t(x >> 24));
}

void pad4(std::vector<uint8_t>& v)
{
	while (v.size() % 4 != 0)
		v.push_back(0);
}

void pad4(std::string& s)
{
	while (s.size() % 4 != 0)
		s.push_back(' ');
}

// Builds a self-contained GLB in memory. Each attribute gets its own buffer view
// inside the single BIN chunk; attribute order equals accessor order.
std::vector<uint8_t> build_glb(const std::vector<AttrSpec>& attributes, cgltf_size vertex_count,
	bool include_used, bool include_required, const std::vector<cgltf_size>* forced_strides = NULL)
{
	std::vector<uint8_t> bin;
	struct ViewInfo { cgltf_size offset; cgltf_size length; int stride; };
	std::vector<ViewInfo> views;

	for (size_t a = 0; a < attributes.size(); ++a)
	{
		pad4(bin);
		ViewInfo info;
		info.offset = bin.size();
		info.length = attributes[a].bytes.size();
		info.stride = forced_strides ? int((*forced_strides)[a]) : attributes[a].stride;
		bin.insert(bin.end(), attributes[a].bytes.begin(), attributes[a].bytes.end());
		views.push_back(info);
	}
	pad4(bin);

	std::string json = "{";
	if (include_used || include_required)
	{
		json += "\"extensionsUsed\":[";
		if (include_used) json += "\"KHR_mesh_quantization\"";
		json += "],";
		json += "\"extensionsRequired\":[";
		if (include_required) json += "\"KHR_mesh_quantization\"";
		json += "],";
	}
	json += "\"asset\":{\"version\":\"2.0\"},";

	json += "\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}],";

	json += "\"bufferViews\":[";
	for (size_t i = 0; i < views.size(); ++i)
	{
		if (i) json += ",";
		json += "{\"buffer\":0,\"byteOffset\":" + std::to_string(views[i].offset) +
			",\"byteLength\":" + std::to_string(views[i].length) +
			",\"byteStride\":" + std::to_string(views[i].stride) + "}";
	}
	json += "],";

	json += "\"accessors\":[";
	for (size_t i = 0; i < attributes.size(); ++i)
	{
		if (i) json += ",";
		json += "{\"bufferView\":" + std::to_string(i) + ",\"componentType\":" +
			std::to_string(attributes[i].component_type) + ",\"count\":" +
			std::to_string(vertex_count) + ",\"type\":\"" + attributes[i].type_name + "\"";
		if (attributes[i].normalized)
			json += ",\"normalized\":true";
		json += "}";
	}
	json += "],";

	json += "\"meshes\":[{\"primitives\":[{\"attributes\":{";
	bool first = true;
	for (size_t i = 0; i < attributes.size(); ++i)
	{
		if (attributes[i].morph_target)
			continue;
		if (!first) json += ",";
		first = false;
		json += "\"" + std::string(attributes[i].semantic) + "\":" + std::to_string(i);
	}
	json += "},\"mode\":4";

	bool has_targets = false;
	for (size_t i = 0; i < attributes.size(); ++i)
		if (attributes[i].morph_target) { has_targets = true; break; }
	if (has_targets)
	{
		json += ",\"targets\":[{";
		first = true;
		for (size_t i = 0; i < attributes.size(); ++i)
		{
			if (!attributes[i].morph_target)
				continue;
			if (!first) json += ",";
			first = false;
			json += "\"" + std::string(attributes[i].semantic) + "\":" + std::to_string(i);
		}
		json += "}]";
	}
	json += "}]}],\"nodes\":[{\"mesh\":0}],\"scene\":0,\"scenes\":[{\"nodes\":[0]}]}";

	pad4(json);
	std::vector<uint8_t> bin_padded = bin;
	pad4(bin_padded);

	std::vector<uint8_t> glb;
	put_u32(glb, 0x46546C67); // "glTF"
	put_u32(glb, 2);
	put_u32(glb, uint32_t(12 + 8 + json.size() + 8 + bin_padded.size()));
	put_u32(glb, uint32_t(json.size()));
	put_u32(glb, 0x4E4F534A); // "JSON"
	glb.insert(glb.end(), json.begin(), json.end());
	put_u32(glb, uint32_t(bin_padded.size()));
	put_u32(glb, 0x004E4942); // "BIN\0"
	glb.insert(glb.end(), bin_padded.begin(), bin_padded.end());
	return glb;
}

cgltf_result parse_glb(const std::vector<uint8_t>& glb, cgltf_data** out)
{
	cgltf_options options = {};
	cgltf_result result = cgltf_parse(&options, glb.data(), glb.size(), out);
	if (result == cgltf_result_success)
		result = cgltf_load_buffers(&options, *out, NULL);
	if (result == cgltf_result_success)
		result = cgltf_validate(*out);
	return result;
}

int g_failures = 0;

void check(bool condition, const char* what)
{
	if (!condition)
	{
		printf("FAIL: %s\n", what);
		++g_failures;
	}
}

// Packs one element's components into the attribute buffer at the given stride.
void write_element(AttrSpec& attr, cgltf_size vertex, const std::vector<int>& values)
{
	int cs = component_size(attr.component_type);
	for (size_t c = 0; c < values.size(); ++c)
	{
		size_t off = size_t(vertex) * attr.stride + c * cs;
		int v = values[c];
		if (cs == 1)
			attr.bytes[off] = uint8_t(v);
		else if (cs == 2)
		{
			attr.bytes[off] = uint8_t(uint16_t(v));
			attr.bytes[off + 1] = uint8_t(uint16_t(v) >> 8);
		}
	}
}

AttrSpec make_attr(const char* semantic, int component_type, const char* type_name, bool normalized,
	cgltf_size vertex_count, bool morph = false)
{
	AttrSpec attr;
	attr.semantic = semantic;
	attr.component_type = component_type;
	attr.type_name = type_name;
	attr.normalized = normalized;
	attr.morph_target = morph;
	int components = strcmp(type_name, "SCALAR") == 0 ? 1 : int(strlen(type_name) - 1);
	attr.stride = components * component_size(component_type);
	while (attr.stride % 4 != 0)
		attr.stride += 1;
	attr.bytes.assign(size_t(vertex_count) * attr.stride, 0);
	return attr;
}

void test_valid_quantization_and_bounded_error()
{
	const cgltf_size N = 4;

	AttrSpec position = make_attr("POSITION", 5122, "VEC3", false, N); // SHORT
	AttrSpec normal = make_attr("NORMAL", 5120, "VEC3", true, N);      // BYTE normalized, stride 4
	AttrSpec tangent = make_attr("TANGENT", 5122, "VEC4", true, N);    // SHORT normalized, stride 8
	AttrSpec texcoord = make_attr("TEXCOORD_0", 5123, "VEC2", false, N); // UNSIGNED_SHORT

	write_element(position, 0, {100, -200, 300});
	write_element(position, 1, {-32768, 0, 32767});
	write_element(normal, 0, {127, -128, 0});
	write_element(normal, 1, {64, -64, 1});
	write_element(tangent, 0, {32767, -32768, 0, 32767});
	write_element(texcoord, 0, {0, 65535});
	write_element(texcoord, 1, {1234, 4321});

	std::vector<AttrSpec> attrs;
	attrs.push_back(position);
	attrs.push_back(normal);
	attrs.push_back(tangent);
	attrs.push_back(texcoord);

	std::vector<uint8_t> glb = build_glb(attrs, N, true, true);

	cgltf_data* data = NULL;
	check(parse_glb(glb, &data) == cgltf_result_success, "valid quantized GLB parses and validates");
	check(cgltf_data_has_extension(data, "KHR_mesh_quantization") == 1, "extension detected via public API");
	check(cgltf_data_has_extension(data, "KHR_draco_mesh_compression") == 0, "absent extension reported as absent");

	cgltf_float out[16];

	// Unnormalized integers keep their range: no accidental normalization.
	check(cgltf_accessor_read_float(&data->accessors[0], 0, out, 16) &&
		out[0] == 100.0f && out[1] == -200.0f && out[2] == 300.0f, "unnormalized SHORT POSITION range preserved");
	check(cgltf_accessor_read_float(&data->accessors[3], 0, out, 16) &&
		out[0] == 0.0f && out[1] == 65535.0f, "unnormalized UNSIGNED_SHORT TEXCOORD range preserved");

	// Normalized dequantization follows the shared cgltf_component_read_float formula.
	check(cgltf_accessor_read_float(&data->accessors[1], 0, out, 16) &&
		out[0] == 127.0f / 127.0f && out[1] == -128.0f / 127.0f && out[2] == 0.0f,
		"normalized BYTE NORMAL dequantized through shared path");
	check(cgltf_accessor_read_float(&data->accessors[2], 0, out, 16) &&
		out[0] == 32767.0f / 32767.0f && out[3] == 32767.0f / 32767.0f,
		"normalized SHORT TANGENT dequantized through shared path");
	cgltf_free(data);

	// Bounded-error sweep over the signed BYTE normalized domain: encode originals on the
	// unit grid, decode via cgltf, and require the error to stay within one quantization step.
	const cgltf_size S = 256;
	AttrSpec sweep = make_attr("NORMAL", 5120, "VEC3", true, S);
	for (int k = 0; k < int(S); ++k)
	{
		float f = -1.0f + 2.0f * k / float(S - 1);
		int c = int(std::lround(f * 127.0f));
		c = std::max(-127, std::min(127, c));
		write_element(sweep, k, {c, 0, 0});
	}
	std::vector<AttrSpec> sweep_attrs;
	sweep_attrs.push_back(sweep);
	std::vector<uint8_t> sweep_glb = build_glb(sweep_attrs, S, true, true);
	cgltf_data* sdata = NULL;
	check(parse_glb(sweep_glb, &sdata) == cgltf_result_success, "sweep file validates");
	float max_error = 0.0f;
	for (cgltf_size k = 0; k < S; ++k)
	{
		float original = -1.0f + 2.0f * float(k) / float(S - 1);
		cgltf_accessor_read_float(&sdata->accessors[0], k, out, 16);
		max_error = std::max(max_error, std::fabs(out[0] - original));
	}
	check(max_error <= 1.0f / 127.0f + 1e-7f, "BYTE normalized dequantization error bounded by one step");
	printf("  max BYTE normalized error: %.6f (bound %.6f)\n", max_error, 1.0f / 127.0f);
	cgltf_free(sdata);
}

void test_roundtrip()
{
	const cgltf_size N = 3;
	AttrSpec position = make_attr("POSITION", 5123, "VEC3", false, N);  // UNSIGNED_SHORT
	AttrSpec normal = make_attr("NORMAL", 5122, "VEC3", true, N);       // SHORT normalized
	AttrSpec texcoord = make_attr("TEXCOORD_0", 5121, "VEC2", false, N); // UNSIGNED_BYTE
	for (cgltf_size v = 0; v < N; ++v)
	{
		write_element(position, v, {1000 + int(v), 2000, 3000});
		write_element(normal, v, {int(v) - 1, 0, 100});
		write_element(texcoord, v, {int(v) * 17, 255});
	}
	std::vector<AttrSpec> attrs;
	attrs.push_back(position);
	attrs.push_back(normal);
	attrs.push_back(texcoord);

	std::vector<uint8_t> glb = build_glb(attrs, N, true, true);
	cgltf_options options = {};
	cgltf_data* data0 = NULL;
	check(cgltf_parse(&options, glb.data(), glb.size(), &data0) == cgltf_result_success, "roundtrip: parse");
	check(cgltf_load_buffers(&options, data0, NULL) == cgltf_result_success, "roundtrip: load buffers");
	check(cgltf_validate(data0) == cgltf_result_success, "roundtrip: validate");

	options.type = cgltf_file_type_glb;
	const char* out_path = "quantization_roundtrip.glb";
	check(cgltf_write_file(&options, out_path, data0) == cgltf_result_success, "roundtrip: write GLB");

	cgltf_data* data1 = NULL;
	check(cgltf_parse_file(&options, out_path, &data1) == cgltf_result_success, "roundtrip: reparse");
	check(cgltf_load_buffers(&options, data1, out_path) == cgltf_result_success, "roundtrip: reload buffers");
	check(cgltf_validate(data1) == cgltf_result_success, "roundtrip: revalidate");
	check(cgltf_data_has_extension(data1, "KHR_mesh_quantization") == 1, "roundtrip: extension preserved in used");

	bool required_preserved = false;
	for (cgltf_size i = 0; i < data1->extensions_required_count; ++i)
		if (strcmp(data1->extensions_required[i], "KHR_mesh_quantization") == 0)
			required_preserved = true;
	check(required_preserved, "roundtrip: extension preserved in required");

	check(data0->accessors_count == data1->accessors_count, "roundtrip: accessor count");
	bool same_decoded = true;
	bool same_format = true;
	for (cgltf_size a = 0; a < data0->accessors_count; ++a)
	{
		same_format = same_format &&
			data0->accessors[a].component_type == data1->accessors[a].component_type &&
			data0->accessors[a].normalized == data1->accessors[a].normalized &&
			data0->accessors[a].type == data1->accessors[a].type;
		for (cgltf_size v = 0; v < data0->accessors[a].count; ++v)
		{
			cgltf_float f0[16], f1[16];
			cgltf_accessor_read_float(&data0->accessors[a], v, f0, 16);
			cgltf_accessor_read_float(&data1->accessors[a], v, f1, 16);
			cgltf_size comps = cgltf_num_components(data0->accessors[a].type);
			for (cgltf_size c = 0; c < comps; ++c)
				if (f0[c] != f1[c])
					same_decoded = false;
		}
	}
	check(same_format, "roundtrip: componentType/normalized/type preserved");
	check(same_decoded, "roundtrip: decoded values bit-identical");
	check(data0->bin_size == data1->bin_size &&
		memcmp(data0->bin, data1->bin, data0->bin_size) == 0, "roundtrip: BIN chunk byte-identical");

	std::remove(out_path);
	cgltf_free(data1);
	cgltf_free(data0);
}

void expect_invalid(const std::vector<AttrSpec>& attrs, cgltf_size count, bool used, bool required,
	const char* name, const std::vector<cgltf_size>* forced_strides = NULL)
{
	std::vector<uint8_t> glb = build_glb(attrs, count, used, required, forced_strides);
	cgltf_data* data = NULL;
	cgltf_options options = {};
	cgltf_result pr = cgltf_parse(&options, glb.data(), glb.size(), &data);
	if (pr == cgltf_result_success)
		pr = cgltf_load_buffers(&options, data, NULL);
	if (pr != cgltf_result_success)
	{
		printf("FAIL: %s (parse error %d)\n", name, pr);
		++g_failures;
		return;
	}
	cgltf_result vr = cgltf_validate(data);
	check(vr == cgltf_result_invalid_gltf, name);
	cgltf_free(data);
}

void test_invalid_parameters()
{
	const cgltf_size N = 2;

	{
		AttrSpec p = make_attr("POSITION", 5122, "VEC3", false, N);
		std::vector<AttrSpec> attrs; attrs.push_back(p);
		expect_invalid(attrs, N, true, false, "extension in used but missing from required");
	}
	{
		AttrSpec p = make_attr("POSITION", 5123, "VEC3", false, N);
		AttrSpec n = make_attr("NORMAL", 5120, "VEC3", false, N);
		std::vector<AttrSpec> attrs; attrs.push_back(p); attrs.push_back(n);
		expect_invalid(attrs, N, true, true, "unnormalized BYTE NORMAL rejected");
	}
	{
		AttrSpec p = make_attr("POSITION", 5123, "VEC3", false, N);
		AttrSpec t = make_attr("TANGENT", 5120, "VEC3", true, N);
		std::vector<AttrSpec> attrs; attrs.push_back(p); attrs.push_back(t);
		expect_invalid(attrs, N, true, true, "VEC3 primitive TANGENT rejected");
	}
	{
		// Core-legal normalized unsigned TEXCOORD must stay accepted under the extension.
		AttrSpec p = make_attr("POSITION", 5123, "VEC3", false, N);
		AttrSpec t = make_attr("TEXCOORD_0", 5121, "VEC2", true, N);
		std::vector<AttrSpec> attrs; attrs.push_back(p); attrs.push_back(t);
		std::vector<uint8_t> glb = build_glb(attrs, N, true, true);
		cgltf_data* data = NULL;
		check(parse_glb(glb, &data) == cgltf_result_success, "normalized UNSIGNED_BYTE TEXCOORD stays valid");
		cgltf_free(data);
	}
	{
		// JOINTS must be unnormalized.
		AttrSpec p = make_attr("POSITION", 5123, "VEC3", false, N);
		AttrSpec j = make_attr("JOINTS_0", 5121, "VEC4", true, N);
		std::vector<AttrSpec> attrs; attrs.push_back(p); attrs.push_back(j);
		expect_invalid(attrs, N, true, true, "normalized UNSIGNED_BYTE JOINTS rejected");
	}
	{
		// Morph unsigned TEXCOORD stays normalized-only per the core morph table.
		AttrSpec p = make_attr("POSITION", 5123, "VEC3", false, N);
		AttrSpec mt = make_attr("TEXCOORD_0", 5123, "VEC2", false, N, true);
		std::vector<AttrSpec> attrs; attrs.push_back(p); attrs.push_back(mt);
		expect_invalid(attrs, N, true, true, "unnormalized unsigned morph TEXCOORD rejected");
	}
	{
		AttrSpec p = make_attr("POSITION", 5123, "VEC3", false, N);
		AttrSpec mp = make_attr("POSITION", 5123, "VEC3", false, N, true);
		std::vector<AttrSpec> attrs; attrs.push_back(p); attrs.push_back(mp);
		expect_invalid(attrs, N, true, true, "UNSIGNED_SHORT morph POSITION rejected");
	}
	{
		// Primitive COLOR accepts normalized unsigned only; signed is not in either table.
		AttrSpec p = make_attr("POSITION", 5123, "VEC3", false, N);
		AttrSpec c = make_attr("COLOR_0", 5120, "VEC3", true, N);
		std::vector<AttrSpec> attrs; attrs.push_back(p); attrs.push_back(c);
		expect_invalid(attrs, N, true, true, "signed BYTE primitive COLOR rejected");
	}
	{
		AttrSpec p = make_attr("POSITION", 5123, "VEC3", false, N);
		AttrSpec n = make_attr("NORMAL", 5120, "VEC3", true, N);
		std::vector<AttrSpec> attrs; attrs.push_back(p); attrs.push_back(n);
		std::vector<cgltf_size> strides;
		strides.push_back(attrs[0].stride);
		strides.push_back(3);
		expect_invalid(attrs, N, true, true, "stride 3 BYTE VEC3 alignment rejected", &strides);
	}
}

void test_no_extension_unchanged()
{
	const cgltf_size N = 2;
	// BYTE NORMAL without the extension declared: none of the new checks may fire.
	AttrSpec position = make_attr("POSITION", 5123, "VEC3", false, N);
	AttrSpec normal = make_attr("NORMAL", 5120, "VEC3", true, N);
	write_element(normal, 0, {127, -128, 0});
	std::vector<AttrSpec> attrs;
	attrs.push_back(position);
	attrs.push_back(normal);

	std::vector<uint8_t> glb = build_glb(attrs, N, false, false);
	cgltf_data* data = NULL;
	check(parse_glb(glb, &data) == cgltf_result_success, "file without extension unaffected by new checks");

	// Reads still go through the single shared conversion path.
	cgltf_float out[4];
	check(cgltf_accessor_read_float(&data->accessors[1], 0, out, 4) &&
		out[0] == 1.0f, "normalized read path works without extension declared");
	cgltf_free(data);

	// A stray required-only entry does not activate the quantization-specific validation.
	std::vector<uint8_t> req_only = build_glb(attrs, N, false, true);
	check(parse_glb(req_only, &data) == cgltf_result_success, "required-only listing leaves non-quantized files unchanged");
	cgltf_free(data);
}

} // namespace

int main()
{
	printf("[1/4] valid quantization + bounded error\n");
	test_valid_quantization_and_bounded_error();
	printf("[2/4] write round-trip\n");
	test_roundtrip();
	printf("[3/4] invalid parameters\n");
	test_invalid_parameters();
	printf("[4/4] no-extension zero regression\n");
	test_no_extension_unchanged();

	if (g_failures == 0)
	{
		printf("All quantization tests passed.\n");
		return 0;
	}
	printf("%d test(s) failed.\n", g_failures);
	return 1;
}
